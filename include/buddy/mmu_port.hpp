#pragma once

#include <device/board.h>

static_assert(BOARD_IS_XBUDDY(), "Only viable for xBuddy boards. There is no need to include this anywhere else");

/// Basic helper module for xBuddy MMU port that helps with safely turning the power on and correctly reseting the connected device based on xBuddy revision.
namespace mmu_port {

/// Does bitbanging for older board revisions to safely turn on the power to connected device (unless connected device is powered externaly)
void power_on();

/// Just for parity, but just simply turns the power off
void power_off();

} // namespace mmu_port
