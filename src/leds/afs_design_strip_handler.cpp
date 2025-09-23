#include "leds/afs_design_strip_handler.hpp"

#include <leds/status_leds_handler.hpp>
#include <led_animation_controller/simple_transition_controller.hpp>

namespace leds {

static SimpleTransitionController &controller_instance() {
    static SimpleTransitionController instance;
    return instance;
}

AFSDesignStripHandler &AFSDesignStripHandler::instance() {
    static AFSDesignStripHandler instance;
    return instance;
}

void AFSDesignStripHandler::update() {
    std::lock_guard lock(mutex);

    auto &controller = controller_instance();
    controller.set(leds::StatusLedsHandler::instance().get_color(), 500);
    controller.update();
}

leds::ColorRGBW AFSDesignStripHandler::color() const {
    std::lock_guard lock(mutex);
    return controller_instance().color();
}

} // namespace leds
