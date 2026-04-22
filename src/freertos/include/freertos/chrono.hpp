/// @file
#pragma once

#include <chrono>

namespace freertos {

/// Traits to stitch std::chrono with freertos.
struct Clock {
    using rep = uint32_t;
    using period = std::milli;
    using duration = std::chrono::duration<Clock::rep, Clock::period>;
    using time_point = std::chrono::time_point<Clock>;
    static constexpr bool is_steady = true;
    static time_point now();
};
static_assert(std::chrono::is_clock_v<Clock>);

using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<Clock>;

} // namespace freertos
