#include "selftest_result_type.hpp"
#include <printers.h>

#include <option/has_switched_fan_test.h>
#include <option/has_toolchanger.h>
#if HAS_TOOLCHANGER()
    #include <Marlin/src/module/prusa/toolchanger.h>
#endif
#include <logging/log.hpp>

LOG_COMPONENT_REF(Selftest);

void SelftestResult_Log(const SelftestResult &results) {
    for (int e = 0; e < HOTENDS; e++) {
#if HAS_TOOLCHANGER()
        if (buddy::puppies::dwarfs[e].is_enabled() == false) {
            continue;
        }
#endif /*HAS_TOOLCHANGER()*/

        log_info(Selftest, "Print fan %u result is %s", e, ToString(results.tools[e].printFan));
        log_info(Selftest, "Heatbreak fan %u result is %s", e, ToString(results.tools[e].heatBreakFan));
#if HAS_SWITCHED_FAN_TEST()
        log_info(Selftest, "Fans switched %u result is %s", e, ToString(results.tools[e].fansSwitched));
#endif /* HAS_SWITCHED_FAN_TEST() */
        log_info(Selftest, "Nozzle heater %u result is %s", e, ToString(results.tools[e].nozzle));
#if FILAMENT_SENSOR_IS_ADC()
        log_info(Selftest, "Filament sensor %u result is %s", e, ToString(results.tools[e].fsensor));
        log_info(Selftest, "Side filament sensor %u result is %s", e, ToString(results.tools[e].sideFsensor));
#endif /*FILAMENT_SENSOR_IS_ADC()*/
#if HAS_LOADCELL()
        log_info(Selftest, "Loadcell result %u is %s", e, ToString(results.tools[e].loadcell));
#endif /*HAS_LOADCELL()*/
    }
    log_info(Selftest, "X axis result is %s", ToString(results.xaxis));
    log_info(Selftest, "Y axis result is %s", ToString(results.yaxis));
    log_info(Selftest, "Z axis result is %s", ToString(results.zaxis));
    log_info(Selftest, "Z calibration result is %s", ToString(results.zalign));
    log_info(Selftest, "Bed heater result is %s", ToString(results.bed));
    log_info(Selftest, "Ethernet result is %s", ToString(results.eth));
    log_info(Selftest, "Wifi result is %s", ToString(results.wifi));
}
