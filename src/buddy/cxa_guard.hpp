/// @file

#pragma once

extern "C" {

// The C++ compiler generates calls to these functions for function-local statics.
// ABI: https://github.com/ARM-software/abi-aa/blob/main/cppabi32/cppabi32.rst#323guard-object

/**
 * @brief Acquires the lock to initialize a static variable
 * @param guard Pointer to the guard variable added by the compiler
 * @return int 1 if we should proceed with initialization, 0 if it's already done
 */
int __cxa_guard_acquire(int *guard);

/**
 * @brief Releases the lock and marks initialization as complete
 */
void __cxa_guard_release(int *guard);

/**
 * @brief Aborts initialization
 */
void __cxa_guard_abort(int *guard);
}
