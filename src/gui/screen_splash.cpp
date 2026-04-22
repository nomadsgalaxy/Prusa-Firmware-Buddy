#include "screen_splash.hpp"
#include "ScreenHandler.hpp"

#include <buddy/bootstrap_state.hpp>
#include <buddy/unreachable.hpp>
#include "config.h"
#include "config_features.h"
#include <version/version.hpp>
#include "img_resources.hpp"
#include "marlin_client.hpp"
#include <config_store/store_instance.hpp>

#include "i18n.h"
#include "../lang/translator.hpp"
#include "language_eeprom.hpp"
#include "screen_menu_languages.hpp"
#include <pseudo_screen_callback.hpp>
#include "bsod.h"
#include <guiconfig/guiconfig.h>
#include <feature/factory_reset/factory_reset.hpp>
#include <window_msgbox_happy_printing.hpp>

#include <option/bootloader.h>
#include <option/developer_mode.h>
#include <option/has_translations.h>
#include <option/has_e2ee_support.h>
#include <gui/screen_printer_setup.hpp>
#include <option/has_emergency_stop.h>
#include <option/has_heatbed_screws_during_transport.h>

#include <option/has_selftest.h>
#if HAS_SELFTEST()
    #include "printer_selftest.hpp"
    #include "screen_menu_selftest_snake.hpp"
#endif // HAS_SELFTEST

#include <option/has_touch.h>
#if HAS_TOUCH()
    #include <hw/touchscreen/touchscreen.hpp>
#endif // HAS_TOUCH

#if ENABLED(POWER_PANIC)
    #include "power_panic.hpp"
#endif

#include <option/has_toolchanger.h>
#if HAS_TOOLCHANGER()
    #include <module/prusa/toolchanger.h>
#endif

#include "display.hpp"
#include <option/has_switched_fan_test.h>

#if HAS_MINI_DISPLAY()
    #define SPLASHSCREEN_PROGRESSBAR_X 16
    #define SPLASHSCREEN_PROGRESSBAR_Y 148
    #define SPLASHSCREEN_PROGRESSBAR_W 206
    #define SPLASHSCREEN_PROGRESSBAR_H 12
    #define SPLASHSCREEN_VERSION_Y     165

#elif HAS_LARGE_DISPLAY()
    #define SPLASHSCREEN_PROGRESSBAR_X 100
    #define SPLASHSCREEN_PROGRESSBAR_Y 165
    #define SPLASHSCREEN_PROGRESSBAR_W 280
    #define SPLASHSCREEN_PROGRESSBAR_H 12
    #define SPLASHSCREEN_VERSION_Y     185
#endif

using buddy::BootstrapStage;

ScreenSplash::ScreenSplash()
    : screen_t()
    , text_progress(this, Rect16(0, SPLASHSCREEN_VERSION_Y, GuiDefaults::ScreenWidth, 18), is_multiline::no)
    , progress(this, Rect16(SPLASHSCREEN_PROGRESSBAR_X, SPLASHSCREEN_PROGRESSBAR_Y, SPLASHSCREEN_PROGRESSBAR_W, SPLASHSCREEN_PROGRESSBAR_H), COLOR_BRAND, COLOR_GRAY, 6) {
    ClrMenuTimeoutClose();

    text_progress.set_font(Font::small);
    text_progress.SetAlignment(Align_t::Center());
    text_progress.SetTextColor(COLOR_GRAY);

    snprintf(text_progress_buffer, sizeof(text_progress_buffer), "Firmware %s", version::project_version_full);
    text_progress.SetText(string_view_utf8::MakeRAM(text_progress_buffer));
    progress.set_progress_percent(50);

#if ENABLED(POWER_PANIC)
    // don't present any screen or wizard if there is a powerpanic pending
    if (power_panic::state_stored()) {
        return;
    }
#endif

#if DEVELOPER_MODE()
    // don't present any screen or wizard
    return;
#endif

    Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<PseudoScreenCallback, MsgBoxHappyPrinting>);

#if HAS_EMERGENCY_STOP()
    static constexpr auto needs_emergency_stop_consent = [] {
        return !config_store().emergency_stop_enable.get()
            && !config_store().emergency_stop_disable_consent_given.get();
    };
    // Check first time - avoid black screen blinking if we're sure we won't need it
    if (needs_emergency_stop_consent()) {
        constexpr auto callback = +[] {
            // Check again - the user might have given the consent as part of the selftest snake
            if (needs_emergency_stop_consent()) {
                // Run the door sensor calibration, only ask for the consent (and run the calibration)
                marlin_client::gcode("M1980 O");
                static_assert(HAS_DOOR_SENSOR_CALIBRATION());
            }
        };
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<PseudoScreenCallback, callback>);
    }
#endif

#if HAS_SELFTEST() && !PRINTER_IS_PRUSA_iX()
    // A crude heuristic to make the wizard show only "on the first run"
    // Yes, we are ignoring other selftest results outside of this struct, but this is good enough for the purpose
    const bool run_wizard = (config_store().selftest_result.get() == config_store_ns::defaults::selftest_result);
#elif HAS_SELFTEST()
    const bool run_wizard = false;
#endif

#if HAS_SELFTEST()
    if (run_wizard) {
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<ScreenMenuSTSWizard>);
    }
#endif

#if HAS_HEATBED_SCREWS_DURING_TRANSPORT()
    //  C1L is shipped with the bed screwed into the bottom of the chassis. And hence the screws have to be removed.
    const bool bed_screws_removal_approved = config_store().heatbed_screws_removal_approved.get();

    if (!bed_screws_removal_approved) {
        // Ask the user to approve the removal of the bed screws
        static constexpr point_ui16_t icon_point = point_ui16_t(40, 20);
        constexpr auto callback = [] {
            MsgBoxIconned msgbox(
                Rect16(0, 0, GuiDefaults::ScreenWidth, GuiDefaults::ScreenHeight),
                icon_point,
                Responses_Ok,
                0,
                nullptr,
                _("Before using the 3D printer, it is necessary to remove all 3 screws, that secure the heated bed during transport.\n\nThe screws are marked with a sticker."),
                is_multiline::yes,
                &img::ac_heatbed_screw_80x246,
                is_closed_on_click_t::yes);
            Screens::Access()->gui_loop_until_dialog_closed();
            config_store().heatbed_screws_removal_approved.set(true);
        };
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<PseudoScreenCallback, callback>);
    };
#endif

    const bool network_setup_needed = !config_store().printer_network_setup_done.get();
    if (network_setup_needed) {
        constexpr auto network_callback = +[] {
            // Calls network_initial_setup_wizard
            marlin_client::gcode("M1703 A");
        };
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<PseudoScreenCallback, network_callback>);
    }

    const bool hw_config_needed = !config_store().printer_hw_config_done.get();
    if (hw_config_needed) {
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<ScreenPrinterSetup>);
    }

    if (network_setup_needed || hw_config_needed
#if HAS_SELFTEST()
        || run_wizard
#endif
    ) {
        constexpr auto pepa_callback = +[] {
            const char *txt =
#if PRINTER_IS_PRUSA_XL()
                N_("Hi, this is your\nOriginal Prusa XL printer.\n"
                   "I would like to guide you\nthrough the setup process.");
#elif PRINTER_IS_PRUSA_MK4()
                // The MK4 is left out intentionally - it could be MK4, MK4S or MK3.9, we don't know yet
                N_("Hi, this is your\nOriginal Prusa printer.\n"
                   "I would like to guide you\nthrough the setup process.");
#elif PRINTER_IS_PRUSA_MK3_5()
                N_("Hi, this is your\nOriginal Prusa MK3.5 printer.\n"
                   "I would like to guide you\nthrough the setup process.");
#elif PRINTER_IS_PRUSA_MINI()
                N_("Hi, this is your\nOriginal Prusa MINI printer.\n"
                   "I would like to guide you\nthrough the setup process.");
#elif PRINTER_IS_PRUSA_iX()
                N_("Hi, this is your\nOriginal Prusa iX printer.\n"
                   "I would like to guide you\nthrough the setup process.");
#elif PRINTER_IS_PRUSA_COREONE()
                N_("Hi, this is your\nPrusa CORE One printer.\n"
                   "I would like to guide you\nthrough the setup process.");
#elif PRINTER_IS_PRUSA_COREONEL()
                N_("Hi, this is your\nPrusa CORE One L printer.\n"
                   "I would like to guide you\nthrough the setup process.");
#else
    #error unknown config
#endif
            MsgBoxPepaCentered(_(txt), Responses_Ok);
        };
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<PseudoScreenCallback, pepa_callback>);
    }

    // Check for FW type change
    {
        auto &model_var = config_store().last_boot_base_printer_model;
        const auto model = model_var.get();
        const auto current_base_model = PrinterModelInfo::firmware_base().model;
        if (model == model_var.default_val) {
            // Not initialized - assume correct printer
            model_var.set(current_base_model);

        } else if (model != current_base_model) {
            constexpr auto callback = +[] {
                StringViewUtf8Parameters<16> params;
                MsgBoxError(
                    _("Printer type changed from %s to %s.\nFactory reset will be performed.\nSome configuration (network, filament profiles, ...) will be preserved.")
                        .formatted(params, PrinterModelInfo::get(config_store().last_boot_base_printer_model.get()).id_str, PrinterModelInfo::firmware_base().id_str),
                    { Response::Continue });

                FactoryReset::perform(false, FactoryReset::item_bitset({
                    FactoryReset::Item::network, FactoryReset::Item::stats, FactoryReset::Item::user_interface, FactoryReset::Item::user_profiles
#if HAS_E2EE_SUPPORT()
                        ,
                        FactoryReset::Item::security
#endif
                }));
            };
            Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<PseudoScreenCallback, callback>);
        }
    }

#if HAS_TOUCH()
    if (touchscreen.is_enabled() && !touchscreen.is_hw_ok()) {
        constexpr auto touch_error_callback = +[] {
            touchscreen.set_enabled(false);
            MsgBoxWarning(_("Touch driver failed to initialize, touch functionality disabled"), Responses_Ok);
        };
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<PseudoScreenCallback, touch_error_callback>);
    }
#endif // HAS_TOUCH
#if HAS_TRANSLATIONS()
    if (!LangEEPROM::getInstance().IsValid()) {
        Screens::Access()->PushBeforeCurrent(ScreenFactory::Screen<ScreenMenuLanguages, ScreenMenuLanguages::Context::initial_language_selection>);
    }
#endif

    // Set up progress mapper
    // Processes with low occurrence or short duration should have small scale number
    constexpr static ProgressMapperWorkflowArray workflow { std::to_array<ProgressMapperWorkflowStep<BootstrapStage>>(
        {
#if RESOURCES() || BOOTLOADER_UPDATE()
            { BootstrapStage::looking_for_bbf, 1 },
#endif
#if RESOURCES()
                { BootstrapStage::preparing_bootstrap, 1 },
                { BootstrapStage::copying_files, 50 },
#endif
#if BOOTLOADER_UPDATE()
                { BootstrapStage::preparing_update, 1 },
                { BootstrapStage::updating, 1 },
#endif
#if HAS_ESP()
                { BootstrapStage::flashing_esp, 1 },
                { BootstrapStage::reflashing_esp, 1 },
#endif
#if HAS_PUPPIES()
                { BootstrapStage::waking_up_puppies, 1 },
                { BootstrapStage::looking_for_puppies, 1 },
                { BootstrapStage::verifying_puppies, 1 },
    #if HAS_DWARF()
                { BootstrapStage::flashing_dwarf, 10 },
                { BootstrapStage::verifying_dwarf, 1 },
    #endif
    #if HAS_PUPPY_MODULARBED()
                { BootstrapStage::flashing_modular_bed, 10 },
                { BootstrapStage::verifying_modular_bed, 1 },
    #endif
    #if HAS_XBUDDY_EXTENSION()
                { BootstrapStage::flashing_xbuddy_extension, 10 },
                { BootstrapStage::verifying_xbuddy_extension, 1 },
    #endif
    #if HAS_AC_CONTROLLER()
                { BootstrapStage::ac_controller_unknown, 1 },
                { BootstrapStage::ac_controller_verify, 1 },
                { BootstrapStage::ac_controller_flash, 10 },
                { BootstrapStage::ac_controller_ready, 1 },
    #endif
#endif
        }) };

    progress_mapper.setup(workflow);
}

static const char *message(BootstrapStage stage) {
    switch (stage) {
    case BootstrapStage::initial:
        break;
#if RESOURCES() || BOOTLOADER_UPDATE()
    case BootstrapStage::looking_for_bbf:
        return "Looking for BBF...";
#endif
#if RESOURCES()
    case BootstrapStage::preparing_bootstrap:
        return "Preparing";
    case BootstrapStage::copying_files:
        return "Installing";
#endif
#if BOOTLOADER_UPDATE()
    case BootstrapStage::preparing_update:
    case BootstrapStage::updating:
        return "Updating bootloader";
#endif
#if HAS_ESP()
    case BootstrapStage::flashing_esp:
        return "Flashing ESP";
    case BootstrapStage::reflashing_esp:
        return "[ESP] Reflashing broken sectors";
#endif
#if HAS_PUPPIES()
    case BootstrapStage::waking_up_puppies:
        return "Waking up puppies";
    case BootstrapStage::looking_for_puppies:
        return "Looking for puppies";
    case BootstrapStage::verifying_puppies:
        return "Verifying puppies";
    #if HAS_DWARF()
    case BootstrapStage::flashing_dwarf:
        return "Flashing dwarf";
    case BootstrapStage::verifying_dwarf:
        return "Verifying dwarf";
    #endif
    #if HAS_PUPPY_MODULARBED()
    case BootstrapStage::flashing_modular_bed:
        return "Flashing modularbed";
    case BootstrapStage::verifying_modular_bed:
        return "Verifying modularbed";
    #endif
    #if HAS_XBUDDY_EXTENSION()
    case BootstrapStage::flashing_xbuddy_extension:
        return "Flashing xbuddy extension";
    case BootstrapStage::verifying_xbuddy_extension:
        return "Verifying xbuddy extension";
    #endif
    #if HAS_AC_CONTROLLER()
    case BootstrapStage::ac_controller_unknown:
        return "AC controller: unknown";
    case BootstrapStage::ac_controller_verify:
        return "AC controller: verifying";
    case BootstrapStage::ac_controller_flash:
        return "AC controller: flashing";
    case BootstrapStage::ac_controller_ready:
        return "AC controller: ready";
    #endif
#endif
    }
    BUDDY_UNREACHABLE();
}

ScreenSplash::~ScreenSplash() {
    display::enable_resource_file(); // now it is safe to use resources from xFlash
}

void ScreenSplash::draw() {
    Validate();
    progress.Invalidate();
    text_progress.Invalidate();
    screen_t::draw(); // We want to draw over bootloader's screen without flickering/redrawing
#ifdef _DEBUG
    #if HAS_MINI_DISPLAY()
    display::draw_text(Rect16(180, 91, 60, 16), string_view_utf8::MakeCPUFLASH("DEBUG"), Font::small, COLOR_BLACK, COLOR_RED);
    #endif
    #if HAS_LARGE_DISPLAY()
    display::draw_text(Rect16(340, 130, 60, 16), string_view_utf8::MakeCPUFLASH("DEBUG"), Font::small, COLOR_BLACK, COLOR_RED);
    #endif
#endif //_DEBUG
}

void ScreenSplash::windowEvent(window_t *, GUI_event_t event, void *) {
    if (event == GUI_event_t::LOOP) {
        const auto bootstrap_state = buddy::bootstrap_state_get();
        text_progress.SetText(bootstrap_state.stage == BootstrapStage::initial
                ? string_view_utf8::MakeRAM(text_progress_buffer)
                : string_view_utf8::MakeCPUFLASH(message(bootstrap_state.stage)));
        // FW Splash screen starts from progress bar on 50 %
        const uint8_t progress_percent = bootstrap_state.stage == BootstrapStage::initial ? 50 : 50 + progress_mapper.update_progress(bootstrap_state.stage, static_cast<float>(bootstrap_state.percent) / 100.f) / 2;
        progress.set_progress_percent(progress_percent);
    }
}
