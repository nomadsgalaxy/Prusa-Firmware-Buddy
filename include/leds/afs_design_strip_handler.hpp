#pragma once

#include <utils/led_color.hpp>

#include <freertos/mutex.hpp>
#include <option/has_xbuddy_extension.h>

namespace leds {

class AFSDesignStripHandler {
public:
    static AFSDesignStripHandler &instance();

    void update();

    leds::ColorRGBW color() const;

private:
    mutable freertos::Mutex mutex;
};

} // namespace leds
