/// @file
#include "screen_menu_fan_info.hpp"

MI_INFO_PRINT_FAN::MI_INFO_PRINT_FAN()
    : WI_FAN_LABEL_t(_("Print Fan"),
        [](auto) { return FanPWMAndRPM {
                       .pwm = marlin_vars().print_fan_speed,
                       .rpm = marlin_vars().active_hotend().print_fan_rpm,
                   }; } //
    ) {}

MI_INFO_HBR_FAN::MI_INFO_HBR_FAN()
    : WI_FAN_LABEL_t(PRINTER_IS_PRUSA_MK3_5() ? _("Hotend Fan") : _("Heatbreak Fan"),
        [](auto) { return FanPWMAndRPM {
                       .pwm = static_cast<uint8_t>(sensor_data().hbrFan.load()),
                       .rpm = marlin_vars().active_hotend().heatbreak_fan_rpm,
                   }; } //
    ) {}

#if HAS_BED_FAN()
MI_INFO_BED_FAN1::MI_INFO_BED_FAN1()
    // translation: menu item showing info about heated bed fan (there are two of them)
    : WI_FAN_LABEL_t {
        _("Bed Fan 1"),
        [](auto) { return FanPWMAndRPM {
                       .pwm = sensor_data().bed_fan1_pwm.load(),
                       .rpm = sensor_data().bed_fan1_rpm.load(),
                   }; },
    } {}

MI_INFO_BED_FAN2::MI_INFO_BED_FAN2()
    : WI_FAN_LABEL_t {
        // translation: menu item showing info about heated bed fan (there are two of them)
        _("Bed Fan 2"),
        [](auto) { return FanPWMAndRPM {
                       .pwm = sensor_data().bed_fan2_pwm.load(),
                       .rpm = sensor_data().bed_fan2_rpm.load(),
                   }; },
    } {}
#endif

#if HAS_PSU_FAN()
MI_INFO_PSU_FAN::MI_INFO_PSU_FAN()
    : WI_FAN_LABEL_t {
        // translation: menu item showing info about power supply unit cooling fan
        _("PSU Fan"),
        [](auto) { return FanPWMAndRPM {
                       .pwm = sensor_data().psu_fan_pwm.load(),
                       .rpm = sensor_data().psu_fan_rpm.load(),
                   }; },
    } {}
#endif

ScreenMenuFanInfo::ScreenMenuFanInfo()
    : ScreenMenuFanInfo_ {
        // translation: Header text of the screen showing information about fans.
        _("FAN INFO"),
    } {}
