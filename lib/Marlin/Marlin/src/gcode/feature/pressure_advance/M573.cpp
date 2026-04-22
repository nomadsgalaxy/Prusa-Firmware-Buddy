/**
 * @file M573.cpp
 * @brief Loadcell-based Pressure Advance calibration — signal-capture prototype.
 *
 * Runs a 3-pulse extrusion pattern in the purge zone while recording loadcell
 * samples. Pulse 1 is a high-flow purge (also primes melt pressure); pulses 2
 * and 3 are identical low-flow slow→fast→slow steps at the PA-test operating
 * point. Two measurement pulses give a repeatability number across the fit.
 * Samples + phase markers are dumped to serial as CSV for off-line analysis.
 *
 * This is Step 1 (signal validation) of a larger PA auto-calibration feature.
 * Once we have confirmed the pressure transients are visible, the analysis
 * engine (time-constant fit, confidence scoring) will consume these samples in
 * firmware and publish a PA value via M572_internal.
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

// Purge-zone geometry resolved at compile time.
// Values mirror the tables in docs/loadcell_pa_calibration_strategy.md and
// fit within the existing slicer purge zone on each printer family.
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

// Extrusion pattern — 3 pulses × 3 sub-phases each, shared with the
// eventual analysis engine.
//
// Pulse 1 (purge_high): high-flow purge that also primes the melt pressure
//   so the two measurement pulses don't have a cold-start bias. Matches the
//   original single-pulse M573 (5 → 13.3 → 5 mm/s E-feed, 15 mm filament).
//
// Pulses 2 & 3 (measure_1, measure_2): low-flow step at the PA-test operating
//   point (0.915 → 4.574 → 0.915 mm/s E-feed, 3 mm filament). 0.915 mm/s E
//   matches 20 mm/s X-feed on a 0.55×0.2 line; 4.574 mm/s E matches 100 mm/s
//   X-feed on the same line geometry (the fast/slow speeds of generate_pa_test.py).
//   Running two identical measurement pulses gives us a repeatability number
//   (std-dev across the 3 rise-edges and 3 fall-edges) in addition to the K
//   estimate itself.
//
// Each sub-phase runs 10 mm X with dy=0; we zig-zag in Y *between* pulses by
// shifting 1 mm per pulse, so the three stripes don't overlap on the bed.

struct SubPhase {
    const char *name; ///< Phase label written to the CSV output
    float dx_mm; ///< Unsigned X travel; sign is applied via geom().dir
    float de_mm; ///< Filament extruded during the phase
    float e_feed_mm_s; ///< Target extruder speed (mm/s)
};

struct Pulse {
    const char *name; ///< Pulse label (purge_high, measure_1, measure_2)
    float dy_offset_mm; ///< Y shift from the base purge_y for this pulse's stripe
    SubPhase slow_in; ///< Baseline-establish phase
    SubPhase fast_step; ///< Rising-edge transient phase
    SubPhase slow_out; ///< Falling-edge transient phase
};

// Numbers chosen so each measurement pulse gives ~1 s of signal per sub-phase
// at the PA-test operating point (0.915 / 4.574 mm/s E). At 320 Hz that is
// ~320 samples per sub-phase — enough to fit a first-order step response.
constexpr std::array<Pulse, 3> kPulses { {
    { "purge_high", 0.0f,
        //  name            dx    de   e_feed (mm/s)
        { "slow_in_hi", 10.0f, 5.0f, 5.0f }, // 300 mm/min
        { "fast_hi", 10.0f, 5.0f, 13.333f }, // 800 mm/min
        { "slow_out_hi", 10.0f, 5.0f, 5.0f }, // 300 mm/min
    },
    { "measure_1", 1.0f,
        { "slow_in_lo", 10.0f, 1.0f, 0.915f }, // 20 mm/s X-feed @ 0.55×0.2 lines
        { "fast_lo", 10.0f, 1.0f, 4.574f }, // 100 mm/s X-feed @ 0.55×0.2 lines
        { "slow_out_lo", 10.0f, 1.0f, 0.915f },
    },
    { "measure_2", 2.0f,
        { "slow_in_lo", 10.0f, 1.0f, 0.915f },
        { "fast_lo", 10.0f, 1.0f, 4.574f },
        { "slow_out_lo", 10.0f, 1.0f, 0.915f },
    },
} };

// Inter-pulse transition geometry: lift Z while travelling back to start_x at
// the next pulse's Y. No retract — we want the melt zone to settle naturally
// to the no-flow baseline so pulse N's slow_in phase re-establishes it.
constexpr float kTransitionLiftZ_mm = 0.8f; ///< Lift above purge_z during travel
constexpr float kTransitionFeed_mm_s = 60.0f; ///< Travel speed (XY & Z)

// Move feed rate (mm/s) — machine-tangential speed for the combined XE move.
// We size it so the extruder reaches its target e_feed_mm_s given the
// extrusion/travel ratio. With dy=0 during measurement, tangential distance
// equals dx. Not constexpr because std::sqrt would be (but we no longer need
// it since dy=0; kept as a plain helper for clarity and future flexibility).
float tangential_feed(const SubPhase &p) {
    const float time_s = p.de_mm / p.e_feed_mm_s;
    return p.dx_mm / time_s;
}

} // namespace

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M573: Loadcell-based Pressure Advance calibration (signal capture)
 *
 * Runs a 3-pulse extrusion pattern in the purge zone, captures loadcell
 * samples, and dumps them to serial as CSV.
 *
 * Pulse 1 is a high-flow purge (5 → 13.3 → 5 mm/s E-feed, 15 mm filament)
 * that also primes melt pressure. Pulses 2 and 3 are identical low-flow
 * slow → fast → slow steps (0.915 → 4.574 → 0.915 mm/s E-feed, 3 mm filament)
 * at the PA-test operating point (20 / 100 mm/s X-feed on 0.55 × 0.2 lines).
 * Each pulse is laid on a separate Y row (1 mm apart) with a short Z-lift
 * transition between them so the lines don't overlap on the bed.
 *
 *#### Preconditions
 * - Hotend must already be at extrusion temperature (M573 does not heat).
 * - Nozzle must already be positioned at the purge zone (slicer start G-code).
 *
 *#### Output format
 * The CSV dump is delimited by `BEGIN PA_CAPTURE` / `END PA_CAPTURE` markers.
 * - `PA_SAMPLES=<n> PA_DROPPED=<m>` — counts
 * - `PA_PHASE,<name>,<time_us>` — phase boundary timestamps
 * - `PA,<time_us>,<load_g>` — one line per captured sample
 *
 * Capture runs at the loadcell's native 320 Hz in HighPrecision mode; Pressure
 * Advance is zeroed for the duration of the measurement so the pressure
 * signature is not pre-shaped.
 *
 *#### Usage
 *
 *    M573
 */
void GcodeSuite::M573() {
    // Precondition: hot enough to extrude.
    if (thermalManager.targetTooColdToExtrude(active_extruder)) {
        SERIAL_ECHO_MSG("M573: hotend too cold to extrude");
        return;
    }

    // Flush any pending motion so we start from a known steady state.
    planner.synchronize();

    // RAII: zero PA during measurement (so the capture reflects the raw
    // extruder/hotend response), and enable the loadcell's full 320 Hz /
    // filter path. Both guards restore previous state on block exit.
    pressure_advance::PressureAdvanceDisabler pa_guard;
    Loadcell::HighPrecisionEnabler hp_guard(loadcell);

    // Continuous-mode tare establishes the bandpass-filter baseline so the
    // captured load is expressed relative to the current no-flow state.
    loadcell.Tare(Loadcell::TareMode::Continuous);

    // Snap a copy of the current position; we'll drive relative moves via
    // plan_move_by().
    const float start_z = current_position.z;

    // Lower to purge Z at a gentle rate. We don't move X/Y here — slicer
    // start-gcode is expected to have parked us over the purge line.
    if (current_position.z > geom().purge_z) {
        plan_move_by(5.0f, 0.0f, 0.0f, geom().purge_z - current_position.z, 0.0f);
        planner.synchronize();
    }

    auto &cap = pa_calibration::Capture::instance();
    cap.Arm();

    cap.MarkPhase("start", ticks_us());

    // Snap starting X/Y so we can return between pulses. Y is tracked
    // relative to this base via dy_offset_mm on each Pulse.
    const float start_x = current_position.x;
    const float base_y = current_position.y;

    for (std::size_t i = 0; i < kPulses.size(); ++i) {
        const Pulse &pulse = kPulses[i];

        // Move to this pulse's Y row (first iteration already there).
        if (i > 0) {
            cap.MarkPhase("transition_up", ticks_us());
            // Lift Z, travel back to start_x at the new Y offset, drop Z.
            // plan_move_by takes relative offsets, so compute deltas.
            const float x_back = start_x - current_position.x;
            const float y_delta = (base_y + pulse.dy_offset_mm) - current_position.y;
            plan_move_by(kTransitionFeed_mm_s, 0.0f, 0.0f, kTransitionLiftZ_mm, 0.0f);
            plan_move_by(kTransitionFeed_mm_s, x_back, y_delta, 0.0f, 0.0f);
            plan_move_by(kTransitionFeed_mm_s, 0.0f, 0.0f, -kTransitionLiftZ_mm, 0.0f);
            cap.MarkPhase("transition_dn", ticks_us());
        }

        cap.MarkPhase(pulse.name, ticks_us());

        // Emit the three sub-phases of this pulse.
        for (const SubPhase *sp : { &pulse.slow_in, &pulse.fast_step, &pulse.slow_out }) {
            cap.MarkPhase(sp->name, ticks_us());
            const float dx = geom().dir * sp->dx_mm;
            plan_move_by(tangential_feed(*sp), dx, 0.0f, 0.0f, sp->de_mm);
        }
    }

    // Wait for the motion to complete so the last samples are captured before
    // we tear down the guards.
    planner.synchronize();
    cap.MarkPhase("end", ticks_us());
    cap.Stop();

    // Return Z to where we started so subsequent slicer moves are unaffected.
    if (start_z != current_position.z) {
        plan_move_by(5.0f, 0.0f, 0.0f, start_z - current_position.z, 0.0f);
        planner.synchronize();
    }

    // Report. SERIAL_ECHO_MSG is literal-only (it expands to PSTR(S "\n"),
    // which depends on C string-literal concatenation), so format into a
    // buffer and use the START + LN pair for the runtime pointer.
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
