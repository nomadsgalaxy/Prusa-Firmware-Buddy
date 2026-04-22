/// @file

#include "cxa_guard.hpp"

#include <sys.hpp>

#include <FreeRTOS.h>

static SemaphoreHandle_t static_init_mutex = nullptr;
static StaticSemaphore_t static_init_mutex_buffer;

static void ensure_mutex_exists() {
    if (static_init_mutex == nullptr) {
        static_init_mutex = xSemaphoreCreateRecursiveMutexStatic(&static_init_mutex_buffer);
        configASSERT(static_init_mutex != nullptr);
    }
}

extern "C" {

int __cxa_guard_acquire(int *guard) {
    // check first, if already initialized, don't lock
    if ((*guard & 1) != 0) {
        return 0;
    }

    // scheduler not running implies single-threaded execution, proceed with initialization
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return 1;
    }

    configASSERT(!sys_is_interrupt());

    ensure_mutex_exists();

    xSemaphoreTakeRecursive(static_init_mutex, portMAX_DELAY);

    // check again under lock
    if ((*guard & 1) != 0) {
        xSemaphoreGiveRecursive(static_init_mutex);
        return 0;
    }

    // locked now, proceed with initialization
    return 1;
}

void __cxa_guard_release(int *guard) {
    *guard = 1;

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xSemaphoreGiveRecursive(static_init_mutex);
    }
}

void __cxa_guard_abort([[maybe_unused]] int *guard) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xSemaphoreGiveRecursive(static_init_mutex);
    }
}
}
