/// @file
#pragma once

#include <cstdint>

namespace ac_controller {

/// Represents a single AC controller fault. Not to be used directly.
class Fault {
private:
    uint32_t value;

    friend class Faults;

    constexpr explicit Fault(uint8_t bit)
        : value { 1u << bit } {}
};

/// Represents a collection of AC controller faults.
class Faults {
private:
    uint32_t value;

public:
    constexpr explicit Faults(uint32_t value = 0)
        : value { value } {}

    constexpr explicit Faults(Fault fault)
        : value { fault.value } {}

    constexpr Faults operator&(Fault f) const {
        return Faults { value & f.value };
    }

    constexpr explicit operator uint32_t() const {
        return value;
    }

    constexpr explicit operator bool() const {
        return value;
    }

    static constexpr Fault RCD_TRIPPED { 0 };
    static constexpr Fault POWERPANIC { 1 };
    static constexpr Fault OVERHEAT { 2 };
    static constexpr Fault PSU_FAN_NOK { 3 };
    static constexpr Fault PSU_NTC_DISCONNECT { 4 };
    static constexpr Fault PSU_NTC_SHORT { 5 };
    static constexpr Fault BED_NTC_DISCONNECT { 6 };
    static constexpr Fault BED_NTC_SHORT { 7 };
    static constexpr Fault TRIAC_NTC_DISCONNECT { 8 };
    static constexpr Fault TRIAC_NTC_SHORT { 9 };
    static constexpr Fault BED_FAN0_NOK { 10 };
    static constexpr Fault BED_FAN1_NOK { 11 };
    static constexpr Fault TRIAC_FAN_NOK { 12 };
    static constexpr Fault GRID_NOK { 13 };
    static constexpr Fault BED_LOAD_NOK { 14 };
    static constexpr Fault CHAMBER_LOAD_NOK { 15 };
    static constexpr Fault PSU_NOK { 16 };
    static constexpr Fault BED_RUNAWAY { 17 };
    static constexpr Fault MCU_OVERHEAT { 27 };
    static constexpr Fault PCB_OVERHEAT { 28 };
    static constexpr Fault DATA_TIMEOUT { 29 };
    static constexpr Fault HEARTBEAT_MISSING { 30 };
    static constexpr Fault UNKNOWN { 31 };
};

} // namespace ac_controller
