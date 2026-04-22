#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <cmath>
#include <array>

namespace buddy {

class ProbePositionLookbackBase {

public:
    struct Sample {
        uint32_t time = 0;
        float position = NAN;
    };

    static constexpr uint8_t NUM_SAMPLES = 16;

public:
    /// Can be called from an ISR of higher priority than add_sample (on MK4), or lower priority, or from a standard thread (on XL).
    /// This function really has to be ready for everything...
    float get_position_at(uint32_t time_us) const;

protected:
    virtual Sample generate_sample() const = 0;

    /// Called from an ISR
    void add_sample(Sample sample);

protected:
    struct AtomicSample {
        std::atomic<uint32_t> time = 0;
        std::atomic<float> position = NAN;
    };
    std::array<AtomicSample, NUM_SAMPLES> samples;
    std::atomic<uint8_t> newest_sample_pos = 0;
};

#ifndef UNITTESTS
class ProbePositionLookback : public ProbePositionLookbackBase {
public:
    /// Minimum time between samples (in us)
    static constexpr size_t SAMPLES_REQUESTED_DIFF = 1900;

    /// Called from an ISR
    void update();

private:
    Sample generate_sample() const final;
};

extern ProbePositionLookback probePositionLookback;
#endif

} // namespace buddy
