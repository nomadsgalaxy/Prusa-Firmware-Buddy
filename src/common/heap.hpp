/// @file
#pragma once

#include <utils/stack_overflow_checker.hpp>

/// FreeRTOS only checks overflow of its task stacks, ISR stack we need to check ourselsves...
[[nodiscard]] StackOverflowChecker &isr_stack_overflow_checker();
