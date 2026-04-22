/**
 * @file pa_calibration.hpp
 * @brief Loadcell-based Pressure Advance calibration — Step 1 prototype.
 *
 * This is the signal-validation stage of a larger PA auto-calibration feature.
 * For now it only captures loadcell samples during a scripted extrusion
 * pattern (see M573) so the captured data can be analyzed off-line to confirm
 * that nozzle backpressure is visible in the loadcell signal.
 *
 * The full analysis engine (transition detection, time-constant fit,
 * confidence scoring, per-filament persistence) is planned for later steps and
 * is intentionally NOT present yet.
 *
 * Threading:
 *   - StoreSample() runs from the loadcell ISR. It is lock-free and must stay
 *     so. No allocation, no blocking calls.
 *   - Arm()/Stop()/MarkPhase()/DumpToSerial() run from the main (gcode) thread.
 *   - IsActive() is read from the ISR on every loadcell sample (320 Hz); it
 *     must be cheap and safe when capture is inactive.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pa_calibration {

struct Sample {
    uint32_t time_us;
    float load_g;
};

struct PhaseMark {
    const char *name;
    uint32_t time_us;
};

class Capture {
public:
    /// ~12.8 s of samples at 320 Hz — sized for the 3-pulse pattern
    /// (1× high-flow purge + 2× low-flow measurement, plus inter-pulse
    /// transitions) with comfortable headroom if timing slips.
    static constexpr std::size_t MAX_SAMPLES = 4096;
    /// 24 marks = start + end + 3 pulses × (slow_in, fast, slow_out) +
    /// 2 transitions × 2 markers + margin.
    static constexpr std::size_t MAX_PHASES = 24;

    /// Single global instance used by the ISR hook and by M573.
    static Capture &instance();

    /// Arm the buffer for a new run. Clears previous data, marks capture active.
    /// Must be called from the main thread with the planner idle.
    void Arm();

    /// Stop the capture. The ISR hook becomes a no-op once this returns.
    void Stop();

    /// Fast check used by the loadcell ISR; safe from interrupt context.
    bool IsActive() const { return active_.load(std::memory_order_acquire); }

    /// Called from the loadcell ISR on every sample while IsActive().
    /// load_g is the tared Z load in grams. time_us is the sample timestamp.
    void StoreSample(float load_g, uint32_t time_us);

    /// Record a named phase boundary from the main thread (e.g. "fast_start").
    /// Silent no-op if MAX_PHASES is exceeded.
    void MarkPhase(const char *name, uint32_t time_us);

    /// Dump captured samples + phase marks to serial as CSV. Yields
    /// periodically so long output doesn't starve other tasks.
    void DumpToSerial() const;

    std::size_t sample_count() const {
        return head_.load(std::memory_order_acquire);
    }
    std::size_t dropped_count() const {
        return dropped_.load(std::memory_order_acquire);
    }

private:
    Capture() = default;

    std::atomic<bool> active_ { false };
    std::atomic<std::size_t> head_ { 0 };
    std::atomic<std::size_t> dropped_ { 0 };

    std::array<Sample, MAX_SAMPLES> samples_ {};
    std::array<PhaseMark, MAX_PHASES> phases_ {};
    std::size_t phase_count_ { 0 };
};

} // namespace pa_calibration
