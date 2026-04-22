#include <freertos/chrono.hpp>

#include <FreeRTOS.h>
#include <freertos/timing.hpp>

static_assert(configTICK_RATE_HZ * freertos::Clock::period::num == freertos::Clock::period::den);

freertos::Clock::time_point freertos::Clock::now() {
    return time_point { duration { millis() } };
}
