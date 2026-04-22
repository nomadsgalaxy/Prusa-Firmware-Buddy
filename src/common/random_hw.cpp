/// @file
/// @brief HW RNG implementation of random.h

#include "random.h"

#include <device/hal.h>
#include <freertos/mutex.hpp>
#include <device/peripherals.h>
#include <mutex>

#include <option/developer_mode.h>
#if DEVELOPER_MODE()
    #include <bsod.h>
#endif

static freertos::Mutex rand_strong_mutex;

RAND_DECL uint32_t rand_u() {
    // RNG could theoretically fail, check for it
    // This should theoretically never happen. But if it does...
    if (uint32_t out; rand_u_secure(&out)) {
        return out;
    }

#if DEVELOPER_MODE()
    // Dev build -> make the devs know RNG failed
    bsod("HAL RNG failed.");
#else
    // Production -> try to keep things working
    return rand_u_sw();
#endif
}

RAND_DECL bool rand_u_secure(uint32_t *result) {
    HAL_StatusTypeDef retval;

    {
        const std::lock_guard _lg(rand_strong_mutex);
        retval = HAL_RNG_GenerateRandomNumber(&hrng, result);
    }

    return retval == HAL_OK;
}

/// Replacement of the original std::rand that was using a software RNG and dynamic allocation
RAND_DECL __attribute__((used)) int __wrap_rand() {
    return int(rand_u() & INT_MAX);
}
