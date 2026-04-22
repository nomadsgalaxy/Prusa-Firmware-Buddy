#pragma once
#include "ccm_thread.hpp"

inline bool is_ram(uintptr_t address) {
    return is_ccm_ram(address) || (address >= SRAM1_BASE && address < SRAM1_BB_BASE);
}
