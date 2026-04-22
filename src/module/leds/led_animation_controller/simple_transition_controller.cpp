#include <led_animation_controller/simple_transition_controller.hpp>
#include <cmath>
#include <freertos/timing.hpp>

namespace leds {

static float in_out_cubic(float x) {
    return x < 0.5f ? 4.f * x * x * x : 1.f - powf(-2.0f * x + 2.0f, 3.f) / 2.f;
}

void SimpleTransitionController::update() {
    if (animation_finished) {
        return;
    }

    uint32_t time_ms = freertos::millis();

    float blend = static_cast<float>(time_ms - transition_start) / transition_time;
    if (blend < 1.0f) {
        current_color = prev_color.blend(target_color, in_out_cubic(blend));
    } else {
        current_color = target_color;
        animation_finished = true;
    }
}

void SimpleTransitionController::set(ColorRGBW color, uint32_t transition_time) {
    if (color != target_color) {
        prev_color = current_color;
        target_color = color;
        transition_start = freertos::millis();
        this->transition_time = transition_time;
        animation_finished = false;
    }
}

} // namespace leds
