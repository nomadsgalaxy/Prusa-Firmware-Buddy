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
    #include "../../../core/utility.h"
    #include "../../../module/motion.h"
    #include "../../../module/planner.h"
    #include "../../../module/temperature.h"
    #include "../../gcode.h"
    #include "timing.h"

    #include <array>
    #include <cmath>
    #include <cstdio>
    #include <config_store/store_instance.hpp>
    #include <filament.hpp>
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
// boundaries between the free-air and on-bed transitions. Nominal
// per-pulse duration (post-B1 rev): 0.25 + 0.357 + 0.05 = 0.657 s
// (~210 samples at 320 Hz). Observed expected ~0.72 s per pulse — each
// planned move carries ~30 ms of synchronize overhead. Cruise at 70 mm/s
// over 25 mm reaches ≈ 350 ms of plateau cruise (6.1 × τ at K ≈ 0.057),
// which is what the step-response fit needs for a stable F_inf.
//
// Expected capture window (MK4S, post-B1 rev, 6.5.3+):
//   zero_flow_1 (0.2 s) + pulse1 (~0.72 s) + gap (purge_2, ~2.79 s)
//   + pulse2 (~0.72 s) ≈ 4.43 s
// Fits in the 1536-sample / 4.8 s buffer with ~370 ms headroom.
// (Pre-B1 geometry was 4.81 s with near-zero headroom; B1 trimmed
// slow_out from 300 ms → 50 ms per pulse to pay for the longer fast.)
// zero_flow_1 was 500 ms in the first implementation but produced
// 61–65 dropped samples at the tail of pulse 2 (buffer-full → discard).
// Trimmed to 200 ms: still 64 baseline samples — enough to estimate a
// stable mean for subtraction (σ_mean ∝ 1/√N, imperceptible next to
// pulse amplitude). If per-move planner overhead grows further this
// budget will need a revisit; the buffer cannot grow (BSS at ~75 % on
// MK4, and enlarging it risks heap-OOM → eeprom wipe on upgrade).
//
// Adding a symmetric zero_flow_2 before slow_in_2 would overrun the
// buffer; the off-line fit for pulse 2 uses the purge2_start / purge2_end
// mask window as its baseline surrogate instead.
//
// zero_flow_1 is a pure dwell with no motion (safe_delay), placed after
// the capture is armed but before the first E move of pulse 1 — needed
// because the free-air pulse 1 absolute signal is small (tens of grams)
// and slow_in_1's slow-flow steady state is not a clean zero.
constexpr uint32_t kZeroFlow1_ms = 200;
// B1 geometry rev (2026-04-24): fast dropped from 20 mm @ 100 mm/s to
// 25 mm @ 70 mm/s so cruise time (≥ 5τ for K ≈ 0.057) fits within the
// 31 mm per-pulse bed allowance. Previous geometry peaked at ~232 ms and
// decayed before slow_out — transient pulse, not a step response, so the
// τ fit picked up a biased-high F_inf. 350 ms of cruise at 70 mm/s gives
// > 99 % plateau. See .auto-memory/pa_fit_onset_detector_result.md.
// slow_out trimmed to 1 mm / 50 ms (symmetry marker only, not fit).
// Capture budget: new pulse ~720 ms obs vs old ~910 ms, saves ~190 ms
// per pulse → ~390 ms headroom before the 4.8 s buffer limit.
constexpr SubPhase kSlowIn1   { "slow_in_1",   5.0f, 0.229f,  20.0f, 250 };
constexpr SubPhase kFastStep1 { "fast_1",     25.0f, 1.143f,  70.0f, 357 };
constexpr SubPhase kSlowOut1  { "slow_out_1",  1.0f, 0.046f,  20.0f,  50 };
constexpr SubPhase kSlowIn2   { "slow_in_2",   5.0f, 0.229f,  20.0f, 250 };
constexpr SubPhase kFastStep2 { "fast_2",     25.0f, 1.143f,  70.0f, 357 };
constexpr SubPhase kSlowOut2  { "slow_out_2",  1.0f, 0.046f,  20.0f,  50 };

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

// Run one sub-phase: queue the move, wait until the stepper ISR picks
// up the block, THEN mark the phase, synchronize, echo timing.
//
// C1 rev (2026-04-24): MarkPhase was previously called BEFORE
// plan_move_by() and recorded the queue-time timestamp. On MK4S with a
// non-empty planner (or even a cold trapezoid-planning pass), the gap
// between queue-time and execute-time is measurable — enough to bias
// any τ fit using the mark as t=0. See .auto-memory/b1_geometry_result.md
// for the H1/H2 discussion. We poll planner.movesplanned_processed() to
// detect the moment the stepper ISR promotes our block from "queued" to
// "being processed," then mark right after that transition. Cost: a few
// microseconds of tight-poll per subphase, bounded by a 10 ms timeout
// that falls back to queue-time marking with a serial warning if the
// poll ever overruns.
void run_subphase(pa_calibration::Capture &cap, const SubPhase &sp) {
    const float dx = geom().dir * sp.dx_mm;
    const uint32_t t_queued = ticks_us();
    const uint8_t processed_before = planner.movesplanned_processed();
    plan_move_by(sp.feedrate_mm_s, dx, 0.0f, 0.0f, sp.de_mm);
    // Wait for the stepper ISR to start processing this block. Wrap-safe
    // because movesplanned_processed() returns a uint8_t mod
    // BLOCK_BUFFER_SIZE and we compare for inequality.
    constexpr uint32_t kStepperPickupTimeout_us = 10'000;
    uint32_t t_mark = ticks_us();
    while (planner.movesplanned_processed() == processed_before) {
        t_mark = ticks_us();
        if (t_mark - t_queued > kStepperPickupTimeout_us) {
            // Stepper never picked up the block within 10 ms — something
            // is wrong (queue stalled? zero-length move dropped?). Fall
            // back to queue-time mark so the capture isn't lost, and log
            // a warning so off-line analysis knows to treat this phase's
            // timestamp as queue-time rather than execute-time.
            t_mark = t_queued;
            SERIAL_ECHO_START();
            SERIAL_ECHOLNPAIR(
                "M573 WARN stepper pickup timeout, queue-time mark for ",
                sp.name);
            break;
        }
    }
    cap.MarkPhase(sp.name, t_mark);
    planner.synchronize();
    const uint32_t observed_us = ticks_us() - t_mark;
    echo_move_timing(sp.name, sp.dx_mm, sp.de_mm, sp.feedrate_mm_s,
                     sp.expected_ms, observed_us);
}

// Run a pure-dwell sub-phase: mark start, wait with no motion, echo
// timing. Planner is assumed already synchronized (caller's
// responsibility) so the dwell window is extruder-idle with zero motor
// reaction force on the loadcell. safe_delay services idle()/watchdog
// so the loadcell ISR keeps running and samples accumulate normally.
void run_zero_flow(pa_calibration::Capture &cap, const char *name,
                   uint32_t dwell_ms) {
    const uint32_t t0 = ticks_us();
    cap.MarkPhase(name, t0);
    safe_delay(dwell_ms);
    const uint32_t observed_us = ticks_us() - t0;
    echo_move_timing(name, 0.0f, 0.0f, 0.0f, dwell_ms, observed_us);
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
 * - A material preset must be loaded on the active extruder (not
 *   FilamentType::none). M573 reads the preset's nozzle_temperature and
 *   drives the hotend to it, so the slicer does not need to preheat —
 *   but if it already did, wait_for_hotend returns immediately.
 * - Nozzle positioned at the purge zone (slicer start G-code).
 * - M573 is intended to run *inside* the slicer purge sequence. It does
 *   not park on entry and does not restore Z on exit — the print
 *   continues from wherever pulse 2 leaves the nozzle (Z=0.2 at the end
 *   of the purge line).
 *
 *#### Output format
 * - `M573: material=<name> target=<t>°C` — preset resolved + heated.
 * - `M573 diag_x ...`              — pure-XY preamble (feedrate check).
 * - `M573 purge_1 ...`             — anchor + hop up (before capture).
 * - `M573 zero_flow_1 ...`         — 500 ms extruder-idle baseline (pulse 1).
 * - `M573 slow_in_1|fast_1|slow_out_1 ...` — pulse 1 phases (free air).
 * - `M573 purge_2 ...`             — hop down + Seg C/D + wipe (captured).
 * - `M573 slow_in_2|fast_2|slow_out_2 ...` — pulse 2 phases (on bed).
 * - `M573: captured <n> samples (<m> dropped)` — capture summary.
 * - CSV block with phase marks: pulse1_start, zero_flow_1, slow_in_1,
 *   fast_1, slow_out_1, pulse1_end, purge2_start, purge2_end,
 *   pulse2_start, slow_in_2, fast_2, slow_out_2, pulse2_end.
 *
 *#### Usage
 *
 *    M573
 */
void GcodeSuite::M573() {
    // Resolve extrusion temperature from the loaded material preset, not
    // a hardcoded default. The τ fit is material-dependent; running the
    // dual-pulse at the wrong temperature biases K toward that material.
    //
    // FilamentType::none means no filament has been loaded (or the user
    // manually cleared the preset). There's nothing sensible we can do:
    // we don't know what material is in the melt zone, so we refuse.
    const FilamentType loaded = config_store().get_filament_type(active_extruder);
    if (loaded == FilamentType::none) {
        SERIAL_ECHO_MSG("M573: no filament loaded — load a material preset first");
        return;
    }

    const uint16_t target_temp = loaded.parameters().nozzle_temperature;

    // Drive the hotend to the preset temperature. In the normal flow
    // (M573 inside a slicer purge) the slicer has already commanded this
    // temperature, so setTargetHotend is a no-op and wait_for_hotend
    // returns immediately once we're inside the tolerance band.
    //
    // wait_for_hotend(no_wait_for_cooling=true, click_to_cancel=false)
    // mirrors the convention used by selftest_firstlayer.cpp:296.
    thermalManager.setTargetHotend(target_temp, active_extruder);
    if (!thermalManager.wait_for_hotend(active_extruder, true, false)) {
        SERIAL_ECHO_MSG("M573: hotend heat wait aborted");
        return;
    }

    // Belt-and-suspenders — if the preset temperature is somehow below
    // the extrude-safe threshold (misconfigured custom preset), bail
    // before we start planning E moves.
    if (thermalManager.targetTooColdToExtrude(active_extruder)) {
        SERIAL_ECHO_MSG("M573: hotend too cold to extrude");
        return;
    }

    {
        std::array<char, 96> buf;
        std::snprintf(buf.data(), buf.size(),
            "M573: material=%s target=%u°C",
            loaded.parameters().name.data(),
            static_cast<unsigned>(target_temp));
        SERIAL_ECHO_START();
        SERIAL_ECHOLN(buf.data());
    }

    planner.synchronize();

    // RAII: zero PA during measurement, run loadcell at full 320 Hz.
    pressure_advance::PressureAdvanceDisabler pa_guard;
    Loadcell::HighPrecisionEnabler hp_guard(loadcell);

    // Continuous-mode tare establishes the bandpass-filter baseline so
    // captured load is expressed relative to the current no-flow state.
    loadcell.Tare(Loadcell::TareMode::Continuous);

    // M573 runs as part of the slicer's purge sequence: caller is
    // responsible for positioning (X/Y over the purge zone, Z at the
    // start of purge_1). We don't drop to geom().purge_z here and we
    // don't restore the caller's Z at exit — the print continues from
    // wherever pulse 2 leaves the nozzle, which is exactly where the
    // slicer's post-purge state expects it.

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
    // Spans zero_flow_1 + pulse1 + gap + pulse2 (~4.81 s observed, at the
    // edge of the 4.8 s buffer — see budget comment above).
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
    // Zero-flow dwell — extruder idle, no motion, ~500 ms (~160 samples
    // at 320 Hz). Gives off-line analysis a true baseline window for the
    // pulse-1 τ fit, since pulse 1's free-air signal is small (tens of
    // grams) and slow_in_1's slow-flow steady state is not a clean zero.
    run_zero_flow(cap, "zero_flow_1", kZeroFlow1_ms);
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

    // Restore static tare before ret