/**
 * @file M573.cpp
 * @brief Loadcell-based Pressure Advance calibration — single-pulse diagnostic.
 *
 * This is a stripped-down single-pulse version used to validate that:
 *   1. plan_move_by's feedrate is interpreted as mm/s of tangential motion
 *      (prior 3-pulse run had slow_in running ~10x slower than expected),
 *   2. Phase marks are being recorded at actual execution time (prior version
 *      called MarkPhase at plan time; subsequent sub-phase marks all bunched
 *      into a ~200us window after the first blocking move completed).
 *
 * Design:
 *   - One pulse: slow_in → fast → slow_out at the PA-test operating point.
 *   - planner.synchronize() between sub-phases so each MarkPhase() is called
 *     after the previous move has actually completed. This introduces brief
 *     flow pauses at sub-phase boundaries — acceptable for this diagnostic
 *     since we want clean step transitions to verify signal shape and timing.
 *   - A pure-XY preamble move (10 mm at 20 mm/s → 500 ms expected) runs
 *     before the pulse to isolate XY feedrate correctness from any E-axis
 *     coupling in the planner.
 *   - Each move echoes `expected_ms` vs `observed_ms` over serial so feedrate
 *     behavior is visible in the stream log without needing to parse
 *     PA_CAPTURE phase marks.
 *
 * Once the diagnostic confirms feedrate semantics and phase-mark accuracy,
 * the multi-pulse pattern from the earlier version will be reinstated with
 * corrected timing.
 */

#include "../../../inc/MarlinConfig.h"

#if HAS_LOADCELL()

    #include "loadcell.hpp"
    #include "pa_calibration.hpp"
    #include "../../../feature/pressure_advance/pressure_advance_config.hpp"
    #include "../../../module/motion.h"
    #include "../../../module/planner.h"
    #include "../../../module/temperature.h"
    #include "../../gcode.h"
    #include "timing.h"

    #include <array>
    #include <cmath>
    #include <cstdio>
    #include <printers.h>

namespace {

// Purge-zone geometry resolved at compile time. Mirrors the tables in
// docs/loadcell_pa_calibration_strategy.md and fits within the slicer
// purge zone on each printer family.
struct PurgeGeometry {
    float purge_y; ///< Y coordinate of the purge line (machine coords, mm)
    float start_x; ///< X coordinate at the start of the measurement (mm)
    float dir; ///< Direction of X travel: +1 (left→right) or -1 (right→left)
    float purge_z; ///< Z height at which to purge (mm)
};

constexpr PurgeGeometry geom() {
    #if PRINTER_IS_PRUSA_COREONE() || PRINTER_IS_PRUSA_COREONEL()
    return { -2.5f, 249.0f, -1.0f, 0.2f };
    #elif PRINTER_IS_PRUSA_XL()
    return { -8.0f, 30.0f, +1.0f, 0.2f };
    #elif PRINTER_IS_PRUSA_MK4()
    return { -4.0f, 0.0f, +1.0f, 0.2f };
    #else
        #error "M573 purge-zone geometry not defined for this printer"
    #endif
}

// Single-pulse sub-phase description. Feedrates are explicit XY mm/s so
// there's no helper math between the config and what reaches the planner.
// E amounts match standard flow on a 0.55 × 0.2 line at the given XY speed
// (same operating point as generate_pa_test.py: 20 mm/s slow, 100 mm/s fast).
struct SubPhase {
    const char *name;
    float dx_mm;
    float de_mm;
    float feedrate_mm_s; ///< Nominal XY feedrate (mm/s)
    uint32_t expected_ms; ///< dx_mm / feedrate_mm_s in ms, precomputed for echo
};

// Total pulse duration: 0.25 + 0.20 + 0.30 = 0.75 s (~240 samples at 320 Hz).
// Plus ~0.5 s preamble + ~0.5 s cooldown overhead → well inside the 1536/
// 4.8 s buffer, no drops expected even if the feedrate bug doubles actuals.
constexpr SubPhase kSlowIn   { "slow_in",   5.0f, 0.229f,  20.0f, 250 };
constexpr SubPhase kFastStep { "fast",     20.0f, 0.915f, 100.0f, 200 };
constexpr SubPhase kSlowOut  { "slow_out",  6.0f, 0.275f,  20.0f, 300 };

// Pure-XY diagnostic preamble — no E involved. If this runs in ≈500 ms we
// know pure-XY feedrate is correct and the bug is in the E-carrying path.
// If it also runs slow we know the bug is upstream of the E-axis logic.
constexpr float kDiagDx_mm = 10.0f;
constexpr float kDiagFeed_mm_s = 20.0f;
constexpr uint32_t kDiagExpected_ms = 500;

// Small helper to emit an "expected vs observed" echo line for a completed
// move. Kept here so the runner stays readable.
void echo_move_timing(const char *tag, float dx_mm, float de_mm,
                      float fr_mm_s, uint32_t expected_ms,
                      uint32_t observed_us) {
    std::array<char, 128> buf;
    std::snprintf(buf.data(), buf.size(),
        "M573 %s dx=%.2f de=%.3f fr=%.2fmm/s exp=%lums obs=%lums",
        tag, double(dx_mm), double(de_mm), double(fr_mm_s),
        static_cast<unsigned long>(expected_ms),
        static_cast<unsigned long>(observed_us / 1000));
    SERIAL_ECHO_START();
    SERIAL_ECHOLN(buf.data());
}

// Run one sub-phase: mark start, plan, synchronize, echo timing.
// MarkPhase is called after the synchronize so the next phase's start
// timestamp is also the current phase's end timestamp — giving
// unambiguous boundaries for off-line analysis.
void run_subphase(pa_calibration::Capture &cap, const SubPhase &sp) {
    const uint32_t t0 = ticks_us();
    cap.MarkPhase(sp.name, t0);
    const float dx = geom().dir * sp.dx_mm;
    plan_move_by(sp.feedrate_mm_s, dx, 0.0f, 0.0f, sp.de_mm);
    planner.synchronize();
    const uint32_t observed_us = ticks_us() - t0;
    echo_move_timing(sp.name, sp.dx_mm, sp.de_mm, sp.feedrate_mm_s,
                     sp.expected_ms, observed_us);
}

} // namespace

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M573: Loadcell-based Pressure Advance calibration (single-pulse diagnostic)
 *
 * Runs a single slow→fast→slow pulse at the PA-test operating point, with a
 * pure-XY preamble move to isolate feedrate correctness. Each move echoes
 * `exp=<ms> obs=<ms>` to serial for direct feedrate inspection, and the
 * loadcell capture is dumped as CSV delimited by `BEGIN PA_CAPTURE` / `END
 * PA_CAPTURE`.
 *
 *#### Preconditions
 * - Hotend at extrusion temperature (M573 does not heat).
 * - Nozzle positioned at the purge zone (slicer start G-code).
 *
 *#### Output format
 * - `M573 diag_x dx=... fr=... exp=...ms obs=...ms` — pure-XY preamble.
 * - `M573 <subphase> dx=... fr=... exp=...ms obs=...ms` — each pulse phase.
 * - `M573: captured <n> samples (<m> dropped)` — capture summary.
 * - CSV block: `PA_PHASE,<name>,<time_us>` and `PA,<time_us>,<load_g>`.
 *
 *#### Usage
 *
 *    M573
 */
void GcodeSuite::M573() {
    if (thermalManager.targetTooColdToExtrude(active_extruder)) {
        SERIAL_ECHO_MSG("M573: hotend too cold to extrude");
        return;
    }

    planner.synchronize();

    // RAII: zero PA during measurement, run loadcell at full 320 Hz.
    pressure_advance::PressureAdvanceDisabler pa_guard;
    Loadcell::HighPrecisionEnabler hp_guard(loadcell);

    // Continuous-mode tare establishes the bandpass-filter baseline so
    // captured load is expressed relative to the current no-flow state.
    loadcell.Tare(Loadcell::TareMode::Continuous);

    const float start_z = current_position.z;

    // Drop to purge Z if we're above it. Slicer start-gcode is expected
    // to have parked us over the purge line in X/Y.
    if (current_position.z > geom().purge_z) {
        plan_move_by(5.0f, 0.0f, 0.0f, geom().purge_z - current_position.z, 0.0f);
        planner.synchronize();
    }

    auto &cap = pa_calibration::Capture::instance();

    // --- Preamble: pure-XY move, 10mm @ 20mm/s, expected 500ms. ---
    // Not captured. Validates XY feedrate interpretation; echo-only.
    const float start_x = current_position.x;
    {
        const uint32_t t0 = ticks_us();
        plan_move_by(kDiagFeed_mm_s, geom().dir * kDiagDx_mm, 0.0f, 0.0f, 0.0f);
        planner.synchronize();
        const uint32_t observed_us = ticks_us() - t0;
        echo_move_timing("diag_x", kDiagDx_mm, 0.0f, kDiagFeed_mm_s,
                         kDiagExpected_ms, observed_us);
    }

    // --- Prime the melt zone. ---
    // Not captured — purge/warmup only. First extruding move after
    // hotend-idle has a ~3 s startup cost before E motion hits commanded
    // feedrate (observed 2026-04-23: slow_in obs=3457 ms vs exp=250 ms,
    // fully absorbed by a preceding prime move on the next run). We keep
    // this outside the capture window because (a) it's irrelevant to the
    // τ fit, and (b) including it would push the capture past the 4.8 s
    // buffer (294 samples dropped on the prior run).
    //
    // Sizing: 40 mm XY at 10 mm/s w/ 3 mm E. 4 s planned, ~7 s actual
    // with the first-extrusion startup absorbed. Extrudes 3 mm of
    // filament (~7 mm³ PLA) at 1.8 mm³/s steady-state flow — enough to
    // lay a visible primer line and guarantee the nozzle is purged.
    {
        const uint32_t t0 = ticks_us();
        plan_move_by(10.0f, geom().dir * 40.0f, 0.0f, 0.0f, 3.0f);
        planner.synchronize();
        const uint32_t observed_us = ticks_us() - t0;
        echo_move_timing("prime", 40.0f, 3.0f, 10.0f, 4000, observed_us);
    }

    // Return to the pulse's X start without extruding, at travel speed.
    // Done BEFORE Arm() so the travel doesn't consume capture samples.
    {
        const float x_back = start_x - current_position.x;
        plan_move_by(120.0f, x_back, 0.0f, 0.0f, 0.0f);
        planner.synchronize();
    }

    // --- Capture window opens here. ---
    // Everything before this point (diag_x, prime, travel-back) is
    // warmup/diagnostic and does not need loadcell samples. Arming here
    // means the 1536-sample / 4.8 s buffer only has to hold the ~0.75 s
    // pulse plus some pre/post baseline — plenty of headroom.
    cap.Arm();
    cap.MarkPhase("pulse_start", ticks_us());

    // --- Single pulse: slow_in → fast → slow_out. ---
    run_subphase(cap, kSlowIn);
    run_subphase(cap, kFastStep);
    run_subphase(cap, kSlowOut);

    cap.MarkPhase("end", ticks_us());
    cap.Stop();

    // Return Z so subsequent slicer moves aren't affected.
    if (start_z != current_position.z) {
        plan_move_by(5.0f, 0.0f, 0.0f, start_z - current_position.z, 0.0f);
        planner.synchronize();
    }

    {
        std::array<char, 96> buf;
        std::snprintf(buf.data(), buf.size(),
            "M573: captured %u samples (%u dropped)",
            static_cast<unsigned>(cap.sample_count()),
            static_cast<unsigned>(cap.dropped_count()));
        SERIAL_ECHO_START();
        SERIAL_ECHOLN(buf.data());
    }

    cap.DumpToSerial();
}

/** @}*/

#endif // HAS_LOADCELL()
