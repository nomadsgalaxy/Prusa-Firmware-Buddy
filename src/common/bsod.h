/*
 * bsod.h
 *
 *  Created on: 2019-10-01
 *      Author: Radek Vana
 */

#pragma once

#include <bsod/bsod.h>

#include <stdint.h>
#if not defined(UNITTESTS)
    #include "error_codes.hpp"
#else
enum class ErrCode;
#endif

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/** Fatal error that causes redscreen
 * @param error - error message, that will be displayed as error description (MAX length 107 chars)
 * @param module - module affected by error will be displayed as error title (MAX length 20 chars)
 */
[[noreturn]] void fatal_error(const char *error, const char *module);

#ifdef __cplusplus
}
#endif //__cplusplus

[[noreturn]] void fatal_error(const ErrCode error_code, ...);

[[noreturn]] void raise_redscreen(ErrCode error_code, const char *error, const char *module);
