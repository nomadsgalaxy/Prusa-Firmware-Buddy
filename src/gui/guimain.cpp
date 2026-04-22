#include "DialogHandler.hpp"
#include "display.hpp"
#include "gui_time.hpp"
#include "gui.hpp"
#include "Jogwheel.hpp"
#include "knob_event.hpp"
#include "language_eeprom.hpp"
#include "marlin_client.hpp"
#include <screen_error.hpp>
#include "screen_home.hpp"
#include "screen_move_z.hpp"
#include "ScreenFactory.hpp"
#include "ScreenHandler.hpp"
#include "ScreenShot.hpp"
#include "sound.hpp"
#include "tasks.hpp"
#include <config_store/store_instance.hpp>
#include <crash_dump/dump.hpp>
#include <screen_splash.hpp>
#include <wdt.hpp>

#include <option/has_side_leds.h>
#if HAS_SIDE_LEDS()
    #include <leds/side_strip_handler.hpp>
#endif

#include <option/has_leds.h>
#if HAS_LEDS()
    #include <leds/led_manager.hpp>
#endif

Jogwheel jogwheel;

void gui_error_run(void) {
    gui_init();

    // This is not safe, because resource file could be corrupted
    // gui_error_run executes before bootstrap so resources may not be up to date resulting in artefects
    display::enable_resource_file();

    screen_node screen_initializer { ScreenFactory::Screen<ScreenError> };
    Screens::Init(screen_initializer);

    // Mark everything as displayed
    crash_dump::message_set_displayed();
    crash_dump::dump_set_displayed();

#if HAS_LEDS()
    leds::LEDManager::instance().init();
#endif

    LangEEPROM::getInstance(); // Initialize language EEPROM value

    while (true) {
        gui::StartLoop();

#if HAS_LEDS()
        leds::LEDManager::instance().update();
#endif

        Screens::Access()->Loop();
        gui_bare_loop();
        gui::EndLoop();
    }
}

void gui_run(void) {
    gui_init();

    gui::knob::RegisterHeldLeftAction(TakeAScreenshot);
    gui::knob::RegisterLongPressScreenAction([]() { Screens::Access()->Open(ScreenFactory::Screen<ScreenMoveZ>); });

    Screens::Init(ScreenFactory::Screen<ScreenSplash>);
    Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<screen_home_data_t>);

    // TIMEOUT variable getting value from EEPROM when EEPROM interface is initialized
    if (config_store().menu_timeout.get()) {
        Screens::Access()->EnableMenuTimeout();
    } else {
        Screens::Access()->DisableMenuTimeout();
    }

    Screens::Access()->Loop();
#if HAS_LEDS()
    leds::LEDManager::instance().init();
#endif
    // Show bootstrap screen untill firmware initializes
    TaskDeps::provide(TaskDeps::Dependency::gui_display_ready);
    while (!TaskDeps::check(TaskDeps::Tasks::bootstrap_done)) {
        gui_bare_loop();
    }

    marlin_client::init();

    DialogHandler::Access(); // to create class NOW, not at first call of one of callback

    marlin_client::set_event_notify(marlin_server::EVENT_MSK_DEF);

    // Close bootstrap screen, open home screen
    Screens::Access()->Close();

    Sound_Play(eSOUND_TYPE::Start);

#if HAS_SIDE_LEDS()
    leds::SideStripHandler::instance().activity_ping();
#endif

    TaskDeps::provide(TaskDeps::Dependency::gui_ready);

    // Do one initial screen loop to close the screen_splash_t and open the screen_home_t
    // Otherwise, some FSM dialogs might possibly open over the splash screen in  DialogHandler::Access().Loop();
    // and then be immediately closed.
    // BFW-6193
    Screens::Access()->Loop();

    // TODO make some kind of registration
    while (1) {
        gui::StartLoop();

        Screens::Access()->Loop();
        DialogHandler::Access().Loop();

        gui_loop();
        gui::EndLoop();
    }
}
