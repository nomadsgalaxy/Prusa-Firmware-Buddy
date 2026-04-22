/// @file
#include <buddy/bootstrap_state.hpp>

#include <atomic>
#include <freertos/timing.hpp>
#include <option/bootloader.h>

using buddy::BootstrapStage;
using buddy::BootstrapState;

// if this is build with bootloader, we take over when it drawn 50% of progress bar, so start with that
static constexpr uint8_t STARTING_PERCENTAGE = option::bootloader ? 50 : 0;

using AtomicBootstrapState = std::atomic<BootstrapState>;
static AtomicBootstrapState bootstrap_state { {
    .stage = BootstrapStage::initial,
    .percent = STARTING_PERCENTAGE,
} };
static_assert(AtomicBootstrapState::is_always_lock_free);

void buddy::bootstrap_state_set(BootstrapState next) {
    const BootstrapState prev = bootstrap_state.exchange(next);
    if (prev != next) {
        // allow lower priority tasks to process this change
        freertos::delay(1);
    }
}

BootstrapState buddy::bootstrap_state_get() {
    return bootstrap_state.load();
}
