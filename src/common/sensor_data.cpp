#include <common/sensor_data.hpp>
#include <printers.h>

#include <option/has_ac_controller.h>
#include <option/has_bed_fan.h>
#include <option/has_door_sensor.h>
#include <option/has_mmu2.h>
#include <option/has_advanced_power.h>
#if HAS_ADVANCED_POWER()
    #include "advanced_power.hpp"
#endif // HAS_ADVANCED_POWER()

#include <adc.hpp>
#include "../Marlin/src/module/temperature.h"
#include <timing.h>

#if HAS_AC_CONTROLLER()
    #include <puppies/ac_controller.hpp>
#endif
#if HAS_BED_FAN()
    #include <feature/bed_fan/bed_fan.hpp>
#endif
#if HAS_PSU_FAN()
    #include <feature/psu_fan/psu_fan.hpp>
#endif

#if BOARD_IS_XLBUDDY()
    #include <Marlin/src/module/prusa/toolchanger.h>
#endif

SensorData &sensor_data() {
    static SensorData instance;
    return instance;
}

SensorData::SensorData()
    : limiter(1000 / OVERSAMPLENR) {
}

void SensorData::update_MCU_temp() {
    if (limiter.check(ticks_ms())) {
        mcu_sum += AdcGet::getMCUTemp();

#if BOARD_IS_XLBUDDY()
        sandwich_sum += AdcGet::sandwichTemp();
        splitter_sum += AdcGet::splitterTemp();
#endif

        if (++sample_nr >= OVERSAMPLENR) {
            MCUTemp = static_cast<float>(mcu_sum) / OVERSAMPLENR;
            mcu_sum = 0;

#if BOARD_IS_XLBUDDY()
            sandwichTemp = Temperature::analog_to_celsius_board(sandwich_sum);
            splitterTemp = Temperature::analog_to_celsius_board(splitter_sum);
            sandwich_sum = 0;
            splitter_sum = 0;
#endif
            sample_nr = 0;
        }
    }
}

void SensorData::update() {
    // Board temperature
    boardTemp = thermalManager.degBoard();

    // MCU temperature
    update_MCU_temp();

#if HAS_DOOR_SENSOR()
    // Door sensor
    door_sensor_detailed_state = buddy::door_sensor().detailed_state();
#endif

    // Heatbreak fan speed
    hbrFan = thermalManager.fan_speed[1];

#if BOARD_IS_XBUDDY()

    #if HAS_AC_CONTROLLER()
    bedMCUTemperature = buddy::puppies::ac_controller.get_mcu_temp().value_or(0.0f);
    bed_voltage = buddy::puppies::ac_controller.get_bed_voltage().value_or(0.0f);
    #else
    bed_voltage = advancedpower.bed_voltage();
    #endif
    heater_voltage = advancedpower.heater_voltage();
    heater_current = advancedpower.heater_current();
    input_current = advancedpower.input_current();

    #if HAS_MMU2()

    // MMU current
    mmuCurrent = advancedpower.GetMMUInputCurrent();

    #endif

#elif BOARD_IS_XLBUDDY()

    // Input voltage
    inputVoltage = advancedpower.Get24VVoltage();

    // Sandwich 5V voltage
    sandwich5VVoltage = advancedpower.Get5VVoltage();

    // Sandwich 5V current
    sandwich5VCurrent = advancedpower.GetDwarfSandwitch5VCurrent();

    // XLBUDDY board 5V current
    buddy5VCurrent = advancedpower.GetXLBuddy5VCurrent();

    buddy::puppies::Dwarf &dwarf = prusa_toolchanger.getActiveToolOrFirst();

    // Dwarf board temperature
    dwarfBoardTemperature = dwarf.get_board_temperature();

    // Dwarf MCU temperature
    dwarfMCUTemperature = dwarf.get_mcu_temperature();

#endif
#if HAS_BED_FAN()
    if (auto rpm = bed_fan::bed_fan().get_rpm()) {
        bed_fan1_rpm = (*rpm)[0];
        bed_fan2_rpm = (*rpm)[1];
    } else {
        bed_fan1_rpm = 0;
        bed_fan2_rpm = 0;
    }
    bed_fan1_pwm = bed_fan::bed_fan().get_pwm().value_or(0);
    bed_fan2_pwm = bed_fan::bed_fan().get_pwm().value_or(0);
#endif
#if HAS_PSU_FAN()
    static_assert(HAS_AC_CONTROLLER());
    psu_fan_rpm = psu_fan::psu_fan().get_rpm().value_or(0);
    psu_fan_pwm = psu_fan::psu_fan().get_pwm().value_or(0);
#endif
}
