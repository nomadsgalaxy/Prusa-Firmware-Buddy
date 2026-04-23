/**
 * @file M573.cpp
 * @brief Loadcell-based Pressure Advance calibration — dual-pulse (air + bed).
 *
 * Runs two slow→fast→slow flow pulses back-to-back during a single purge
 * sequence, measuring two different regimes:
 *
 *   1. Pulse 1 at Z=6.2 (after HOP UP, before HOP DOWN) — nozzle in free
 *      air, no bed contact. Signal reflects melt backpressure pushing
 *      through the nozzle into atmosphere, independent of print conditions.
 *      Small absolute values expected (tens of grams).
 *
 *   2. Pulse 2 at Z=0.2 (after wipe) — nozzle at first-layer height
 *      depositing a visible line on clean bed. Signal reflects backpressure
 *      plus first-layer squish reaction force. Large absolute values
 *      expected (hundreds to thousands of grams).
 *
 * The Δ between the two τ fits decomposes observed K into:
 *   - a material-intrinsic component (fluid dynamics of melt / nozzle /
 *     retraction system — this is portable across machines and setups), and
 *   - a printing-specific component (first-layer squish modulation — this
 *     is where a real print's K lands).
 *
 * Both pulses run with Z stationary so the Z motor's reaction force does
 * not couple into the loadcell baseline. Z motion only happens between
 * pulses (HOP DOWN + wipe lift/low) and is bracketed by purge2_start /
 * purge2_end marks so analysis can mask that window.
 *
 * A pure-XY preamble (10 mm at 20 mm/s → 500 ms expected) runs before the
 * purge as a regression check on feedrate interpretation.
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

// Two identical copies of the sub-phase set — one per pulse — so each
// MarkPhase() records a unique name and the off-line CSV has unambiguous
// boundaries between the free-air and on-bed transitions. Per-pulse
// duration: 0.25 + 0.20 + 0.30 = 0.75 s (~240 samples at 320 Hz nominal).
//
// Total capture window: pulse1 (~0.9 s) + gap (HOP DOWN + Seg C/D + wipe,
// ~2.7 s) + pulse2 (~0.9 s) ≈ 4.5 s. Fits in the 1536-sample / 4.8 s
// buffer with ~300 ms headroom.
constexpr SubPhase kSlowIn1   { "slow_in_1",   5.0f, 0.229f,  20.0f, 250 };
constexpr SubPhase kFastStep1 { "fast_1",     20.0f, 0.915f, 100.0f, 200 };
constexpr SubPhase kSlowOut1  { "slow_out_1",  6.0f, 0.275f,  20.0f, 300 };
constexpr SubPhase kSlowIn2   { "slow_in_2",   5.0f, 0.229f,  20.0f, 250 };
constexpr SubPhase kFastStep2 { "fast_2",     20.0f, 0.915f, 100.0f, 200 };
constexpr SubPhase kSlowOut2  { "slow_out_2",  6.0f, 0.275f,  20.0f, 300 };

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
 *### M573: Loadcell-based Pressure Advance calibration (dual-pulse)
 *
 * Runs two slow→fast→slow pulses within a single purge sequence:
 *   - Pulse 1 at Z=6.2 (free air) — raw melt backpressure.
 *   - Pulse 2 at Z=0.2 (on bed)   — backpressure + squish reaction.
 * Each move echoes `exp=<ms> obs=<ms>` to serial, and the loadcell capture
 * is dumped as CSV delimited by `BEGIN PA_CAPTURE` / `END PA_CAPTURE`.
 *
 *#### Preconditions
 * - Hotend at extrusion temperature (M573 does not heat).
 * - Nozzle positioned at the purge zone (slicer start G-code).
 *
 *#### Output format
 * - `M573 diag_x ...`              — pure-XY preamble (feedrate check).
 * - `M573 purge_1 ...`             — anchor + hop up (before capture).
 * - `M573 slow_in_1|fast_1|slow_out_1 ...` — pulse 1 phases (free air).
 * - `M573 purge_2 ...`             — hop down + Seg C/D + wipe (captured).
 * - `M573 slow_in_2|fast_2|slow_out_2 ...` — pulse 2 phases (on bed).
 * - `M573: captured <n> samples (<m> dropped)` — capture summary.
 * - CSV block with phase marks: pulse1_start, slow_in_1, fast_1, slow_out_1,
 *   pulse1_end, purge2_start, purge2_end, pulse2_start, slow_in_2, fast_2,
 *   slow_out_2, pulse2_end.
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
    // Not captured. Validates XY feedrate interpretation as a regression
    // check (already confirmed correct on 2026-04-23); echo-only.
    {
        const uint32_t t0 = ticks_us();
        plan_move_by(kDiagFeed_mm_s, geom().dir * kDiagDx_mm, 0.0f, 0.0f, 0.0f);
        planner.synchronize();
        const uint32_t observed_us = ticks_us() - t0;
        echo_move_timing("diag_x", kDiagDx_mm, 0.0f, kDiagFeed_mm_s,
                         kDiagExpected_ms, observed_us);
    }

    // --- Purge part 1: deretract + anchor + hop up. Not captured. ---
    //
    // Absorbs first-extrusion startup cost, lays the purge anchor (Seg A/B),
    // and lifts the nozzle to Z=6.2 with enough E flow to form the pull-tab
    // loop. Ends with Z settled at 6.2 so the capture window that follows
    // starts without Z-motion reaction force polluting the baseline.
    //
    // Feedrate conversions from Prusa gcode:
    //   F300  =   5 mm/s    (slow hop rise — filament has time to drag)
    //   F500  =   8.333 mm/s
    //   F650  =  10.833 mm/s
    //   F800  =  13.333 mm/s
    //   F2400 =  40 mm/s    (deretract)
    //   F8000 = 133.333 mm/s (wipe)
    //
    // geom().dir is +1 on MK4/XL, -1 on CoreOne. All horizontal motion is
    // multiplied by it so the whole sequence mirrors on CoreOne. Z motion
    // is always vertical and not mirrored.
    //
    // X progression (MK4, geom().dir = +1): diag_x ends at X=20. Purge
    // part 1 advances X=20 → 40 (Seg A +5, Seg B +10, HOP UP +5) while
    // lifting Z=0.2 → 6.2.
    {
        const uint32_t t0 = ticks_us();
        // E +2 @ F2400 — deretract
        plan_move_by(40.0f, 0.0f, 0.0f, 0.0f, 2.0f);
        // Seg A: +5 XY, +7 E @ F500 — thick blob anchoring purge to bed
        plan_move_by(8.333f, geom().dir * 5.0f, 0.0f, 0.0f, 7.0f);
        // Seg B: +10 XY, +4 E @ F500 — extends anchor
        plan_move_by(8.333f, geom().dir * 10.0f, 0.0f, 0.0f, 4.0f);
        // HOP UP: +5 XY, +6 E, dz=+6.0 @ F300 — slow rise forms pull-tab
        plan_move_by(5.0f, geom().dir * 5.0f, 0.0f, +6.0f, 6.0f);
        planner.synchronize();
        const uint32_t observed_us = ticks_us() - t0;
        echo_move_timing("purge_1", 20.0f, 19.0f, 5.0f, 3412, observed_us);
    }

    // --- Capture window opens here. ---
    // Spans pulse1 + gap + pulse2 (~4.5 s total, inside 4.8 s buffer).
    cap.Arm();

    // --- Pulse 1: free air at Z=6.2 (raw filament backpressure). ---
    //
    // Z is stationary, so no motor reaction force in the baseline. The
    // nozzle extrudes into atmosphere — absolute signal should be small
    // (tens of grams) and reflect pure melt-through-nozzle fluid dynamics.
    //
    // The extruded ~1.42 mm of filament over 31 mm XY at Z=6.2 will trail
    // as a string and drop onto the pull-tab / bed during HOP DOWN that
    // follows. The messy landing is acceptable — the purge already sacrifices
    // aesthetics for adhesion + easy removal.
    //
    // X progression: 40 → 71 at Z=6.2.
    cap.MarkPhase("pulse1_start", ticks_us());
    run_subphase(cap, kSlowIn1);
    run_subphase(cap, kFastStep1);
    run_subphase(cap, kSlowOut1);
    cap.MarkPhase("pulse1_end", ticks_us());

    // --- Purge part 2: hop down + Seg C/D + wipes. Still captured. ---
    //
    // Brings nozzle back to Z=0.2 and finishes laying the purge line. Z
    // motion during HOP DOWN and wipe couples the Z motor's reaction force
    // into the loadcell — the purge2_start / purge2_end marks bracket this
    // window so off-line analysis can mask it out of any fit.
    //
    // X progression: 71 → 102 at Z=6.2 → 0.2 → 0.05 → 0.2.
    {
        cap.MarkPhase("purge2_start", ticks_us());
        const uint32_t t0 = ticks_us();
        // HOP DOWN: +5 XY, +2 E, dz=-6.0 @ F500 — return to Z=0.2
        plan_move_by(8.333f, geom().dir * 5.0f, 0.0f, -6.0f, 2.0f);
        // Seg C: +10 XY, +4 E @ F650 — holds far end of the loop
        plan_move_by(10.833f, geom().dir * 10.0f, 0.0f, 0.0f, 4.0f);
        // Seg D: +10 XY, +4 E @ F800 — final mass on far side
        plan_move_by(13.333f, geom().dir * 10.0f, 0.0f, 0.0f, 4.0f);
        // Wipe low: +3 XY, dz=-0.15 @ F8000 (drag nozzle near bed)
        plan_move_by(133.333f, geom().dir * 3.0f, 0.0f, -0.15f, 0.0f);
        // Wipe lift: +3 XY, dz=+0.15 @ F8000 (clear the purge line)
        plan_move_by(133.333f, geom().dir * 3.0f, 0.0f, +0.15f, 0.0f);
        planner.synchronize();
        const uint32_t observed_us = ticks_us() - t0;
        cap.MarkPhase("purge2_end", ticks_us());
        echo_move_timing("purge_2", 31.0f, 10.0f, 10.833f, 2654, observed_us);
    }

    // --- Pulse 2: on bed at Z=0.2 (printing backpressure + bed squish). ---
    //
    // Nozzle at first-layer height on clean bed past the purge line. Flow
    // pushes melt through a squished 0.55 × 0.2 gap, so the signal reflects
    // both raw backpressure (as in pulse 1) and the bed-squish reaction
    // force. Absolute values will be large (hundreds to thousands of grams).
    //
    // The Δ between pulse 2 and pulse 1 quantifies how much of the observed
    // operational K is material-intrinsic vs. squish-modulated.
    //
    // X progression: 102 → 133 at Z=0.2.
    cap.MarkPhase("pulse2_start", ticks_us());
    run_subphase(cap, kSlowIn2);
    run_subphase(cap, kFastStep2);
    run_subphase(cap, kSlowOut2);
    cap.MarkPhase("pulse2_end", ticks_us());
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
