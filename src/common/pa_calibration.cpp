/**
 * @file pa_calibration.cpp
 * @brief See header.
 */
#include "pa_calibration.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>

// SERIAL_* macros come transitively via gcode.h → inc/MarlinConfig.h → core/serial.h
#include <gcode/gcode.h>

#include "../Marlin/src/Marlin.h" // idle()

namespace pa_calibration {

Capture &Capture::instance() {
    static Capture inst;
    return inst;
}

void Capture::Arm() {
    // Order matters:
    //   1. Clear head/phases so any stale count is wiped.
    //   2. Publish active=true last so the ISR only starts writing after the
    //      buffer is known empty.
    head_.store(0, std::memory_order_release);
    dropped_.store(0, std::memory_order_release);
    phase_count_ = 0;
    active_.store(true, std::memory_order_release);
}

void Capture::Stop() {
    active_.store(false, std::memory_order_release);
}

void Capture::StoreSample(float load_g, uint32_t time_us) {
    // ISR context. Lock-free: bump head atomically, write the slot.
    const std::size_t idx = head_.fetch_add(1, std::memory_order_acq_rel);
    if (idx >= MAX_SAMPLES) {
        // Buffer full — count the drop; head_ stays past-the-end so subsequent
        // samples are also dropped cheaply.
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    samples_[idx].time_us = time_us;
    samples_[idx].load_g = load_g;
}

void Capture::MarkPhase(const char *name, uint32_t time_us) {
    if (phase_count_ >= MAX_PHASES) {
        return;
    }
    phases_[phase_count_].name = name;
    phases_[phase_count_].time_us = time_us;
    ++phase_count_;
}

void Capture::DumpToSerial() const {
    // All output lines are prefixed with a short tag (PA / PA_PHASE / ...) so
    // an off-line tool can just `grep '^PA,' | cut -d',' -f2,3 | ...` without
    // worrying about interleaved log lines.
    SERIAL_ECHOLN("BEGIN PA_CAPTURE");

    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t n = std::min(head, MAX_SAMPLES);
    const std::size_t dropped = dropped_.load(std::memory_order_acquire);

    {
        std::array<char, 64> buf;
        std::snprintf(buf.data(), buf.size(),
            "PA_SAMPLES=%u PA_DROPPED=%u",
            static_cast<unsigned>(n), static_cast<unsigned>(dropped));
        SERIAL_ECHOLN(buf.data());
    }

    // Phase marks
    for (std::size_t i = 0; i < phase_count_; ++i) {
        std::array<char, 96> buf;
        std::snprintf(buf.data(), buf.size(),
            "PA_PHASE,%s,%" PRIu32,
            phases_[i].name ? phases_[i].name : "?",
            phases_[i].time_us);
        SERIAL_ECHOLN(buf.data());
    }

    // Samples: "PA,<time_us>,<load_g>"
    // Batch and yield so the watchdog / serial TX queue stays happy during a
    // ~1500-line burst.
    constexpr std::size_t YIELD_EVERY = 64;
    for (std::size_t i = 0; i < n; ++i) {
        std::array<char, 48> buf;
        std::snprintf(buf.data(), buf.size(),
            "PA,%" PRIu32 ",%.3f",
            samples_[i].time_us,
            static_cast<double>(samples_[i].load_g));
        SERIAL_ECHOLN(buf.data());

        if (((i + 1) % YIELD_EVERY) == 0) {
            idle(true);
        }
    }

    SERIAL_ECHOLN("END PA_CAPTURE");
}

} // namespace pa_calibration
