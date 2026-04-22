#include "puppies/puppy_task.hpp"

#include "task.h"

#include <iterator>
#include "cmsis_os.h"
#include <logging/log.hpp>
#include <buddy/main.h>
#include <puppies/Dwarf.hpp>
#include <option/has_xbuddy_extension.h>
#include "puppies/PuppyBootstrap.hpp"
#include "timing.h"
#include "Marlin/src/module/stepper.h"
#include "Marlin/src/module/prusa/toolchanger.h"
#include <tasks.hpp>
#include <option/has_ac_controller.h>
#include <option/has_dwarf.h>
#include <option/has_puppy_modularbed.h>
#include <buddy/ccm_thread.hpp>
#include <buddy/bootstrap_state.hpp>
#include "bsod.h"

#if HAS_AC_CONTROLLER()
    #include <puppies/ac_controller.hpp>
#endif

#if HAS_PUPPY_MODULARBED()
    #include <puppies/modular_bed.hpp>
#endif

#if HAS_XBUDDY_EXTENSION()
    #include <puppies/xbuddy_extension.hpp>
#endif

#define PUPPY_TASK_DEBUG() false

LOG_COMPONENT_DEF(Puppies, logging::Severity::debug);

namespace buddy::puppies {

osThreadId puppy_task_handle;

std::atomic<bool> stop_request = false; // when this is set to true, puppy task will gracefully stop its execution

static PuppyBootstrap::BootstrapResult bootstrap_puppies(PuppyBootstrap::BootstrapResult minimal_config) {
    // boostrap first
    log_info(Puppies, "Starting bootstrap");
    PuppyBootstrap puppy_bootstrap { PuppyModbus::share_buffer() };
    return puppy_bootstrap.run(minimal_config);
}

static void verify_puppies_running() {
    // wait for all the puppies to be reacheable
    log_info(Puppies, "Waiting for puppies to boot");

    auto err_supressor = PuppyModbus::ErrorLogSupressor();

    constexpr uint32_t WAIT_TIME = 5000;
    auto reacheability_wait_start = ticks_ms();
    do {
        bool modular_bed_ok = true;
#if HAS_PUPPY_MODULARBED()
        modular_bed_ok = !modular_bed.is_enabled() || (modular_bed.ping() != CommunicationStatus::ERROR);
#endif

        uint8_t num_dwarfs_ok = 0, num_dwarfs_dead = 0;
#if HAS_DWARF()
        for (Dwarf &dwarf : dwarfs) {
            if (dwarf.is_enabled()) {
                if (dwarf.ping() != CommunicationStatus::ERROR) {
                    ++num_dwarfs_ok;
                } else {
                    ++num_dwarfs_dead;
                }
            }
        }
#endif

#if HAS_XBUDDY_EXTENSION()
        const bool xbuddy_extension_ok = xbuddy_extension.ping() != CommunicationStatus::ERROR;
#else
        const bool xbuddy_extension_ok = true;
#endif

        // Note: We don't ping AC controller. It's not a separate device from modbus point of view, that one is virtual & proxied by extension board.

        if (num_dwarfs_dead == 0 && modular_bed_ok && xbuddy_extension_ok) {
            log_info(Puppies, "All puppies are reacheable. Continuing");
            return;
        } else if (ticks_diff(reacheability_wait_start + WAIT_TIME, ticks_ms()) > 0) {
            log_info(Puppies, "Puppies not ready (dwarfs_num: %d/%d, bed: %i, xbuddy_extension: %i), waiting another 200 ms",
                num_dwarfs_ok, num_dwarfs_ok + num_dwarfs_dead, static_cast<int>(modular_bed_ok), static_cast<int>(xbuddy_extension_ok));
            osDelay(200);
            continue;
        } else {
#if PUPPY_TASK_DEBUG()
            log_error(Puppies, "ErrCode::ERR_SYSTEM_PUPPY_RUN_TIMEOUT");
#else
            fatal_error(ErrCode::ERR_SYSTEM_PUPPY_RUN_TIMEOUT);
#endif
        }
    } while (true);
}

static void puppy_task_loop() {
#if ENABLED(PRUSA_TOOLCHANGER)
    size_t slow_stage = 0; ///< Switch slow action
#endif

    // periodically update puppies until there is a failure
    while (true) {
        if (stop_request) {
            return;
        }

        [[maybe_unused]] uint32_t cycle_ticks = ticks_ms(); ///< Only one tick read per cycle, value will be reused by last_ticks_ms()
        // One slow action
        bool worked = false;
#if ENABLED(PRUSA_TOOLCHANGER)
        if (!prusa_toolchanger.update()) {
            return;
        }

        // Get dwarf that is selected
        // The source variable is set in this thread in prusa_toolchanger.update() called above, so no race
        Dwarf &active = prusa_toolchanger.getActiveToolOrFirst(); ///< Currently selected dwarf

        // Fast fifo pull from selected dwarf
        if (active.is_selected()) {
            bool more = true; ///< Pull while there is something in fifo
            // Pull fifo only this many times
            for (int active_fifo_attempts = 5; more && active_fifo_attempts > 0; active_fifo_attempts--) {
                if (active.pull_fifo(more) == CommunicationStatus::ERROR) {
                    return;
                }
            }
        } else {
            osDelay(1); // No dwarf is selected, wait a bit
        }

        size_t orig_stage = slow_stage;
        do {
            // Increment stage, there are 2 actions per dwarf and one modular bed
            if (++slow_stage >= (2 * std::size(dwarfs) + 1)) {
                slow_stage = 0;
            }

            if (slow_stage / 2 < std::size(dwarfs)) { // Two actions per dwarf
                Dwarf &dwarf = dwarfs[slow_stage / 2];
                if (!dwarf.is_enabled()) {
                    continue; // skip if this dwarf is not enabled
                }

                if (slow_stage % 2) {
                    if (&active == &dwarf) {
                        continue; // Skip selected dwarf
                    }

                    // Fast refresh of non-selected dwarf
                    CommunicationStatus status = dwarf.fifo_refresh(cycle_ticks);
                    if (status == CommunicationStatus::ERROR) {
                        return;
                    }
                    worked |= status == CommunicationStatus::OK;
                } else {
                    // Slow refresh of non-selected dwarf
                    CommunicationStatus status = dwarf.refresh();
                    if (status == CommunicationStatus::ERROR) {
                        return;
                    }
                    worked |= status == CommunicationStatus::OK;
                }
            } else
#endif

#if HAS_PUPPY_MODULARBED()
            {
                // Try slow refresh of modular bed
                if (modular_bed.refresh() == CommunicationStatus::ERROR) {
                    return;
                }
            }
#endif
#if HAS_XBUDDY_EXTENSION()
            {
                // TODO: Deal with possibility of extension being optional
                CommunicationStatus status = xbuddy_extension.refresh();
                if (status == CommunicationStatus::ERROR) {
    #if PUPPY_TASK_DEBUG()
                    log_error(Puppies, "xbuddy_extension.refresh() == CommunicationStatus::ERROR")
    #else
                    return;
    #endif
                }

                worked |= status == CommunicationStatus::OK;
            }
#endif
#if HAS_AC_CONTROLLER()
            {
                CommunicationStatus status = ac_controller.refresh();
                if (status == CommunicationStatus::ERROR) {
                    return;
                }

                worked |= status == CommunicationStatus::OK;
            }
#endif
#if ENABLED(PRUSA_TOOLCHANGER)
        } while (!worked && slow_stage != orig_stage); // End if we did some work or if no stage has anything to do
#endif
        osDelay(worked ? 1 : 2); // Longer delay if we did no work
    }
}

static bool puppy_initial_scan() {
    // init each puppy
#if HAS_DWARF()
    for (Dwarf &dwarf : dwarfs) {
        if (dwarf.is_enabled()) {
            if (dwarf.initial_scan() == CommunicationStatus::ERROR) {
                return false;
            }
        }
    }
#endif

#if HAS_PUPPY_MODULARBED()
    if (modular_bed.initial_scan() == CommunicationStatus::ERROR) {
        return false;
    }
#endif

#if HAS_XBUDDY_EXTENSION()
    // TODO: Eventually, there'll be printers that have the extension as
    // optional at runtime - we'll have to deal with that somehow.
    if (xbuddy_extension.initial_scan() == CommunicationStatus::ERROR) {
        return false;
    }
#endif

#if HAS_AC_CONTROLLER()
    if (ac_controller.initial_scan() == CommunicationStatus::ERROR) {
        return false;
    }
#endif
    return true;
}

#if HAS_AC_CONTROLLER()
[[nodiscard]] bool wait_for_ac_controller() {
    // AC controller is vital part of the printer, there is no upper limit
    // on how long we are willing to wait for the bootstrap.
    for (;;) {
        // At this point, puppy_task_loop() is not yet running, so we must
        // manually call refresh() on puppies. Without this, XBE can't make
        // progress while flashing/veryfing ACC. It would also stop sending
        // healthy heartbeats which would in turn put ACC into safe state.
        // We should run this as often as possible to minimize time when
        // XBE is waiting for firmware chunk.
        if (xbuddy_extension.refresh() == CommunicationStatus::ERROR) {
            return false;
        }
        if (ac_controller.refresh() == CommunicationStatus::ERROR) {
            return false;
        }
        using xbuddy_extension::NodeState;
        switch (ac_controller.get_node_state()) {
        case NodeState::unknown:
            bootstrap_state_set(0, BootstrapStage::ac_controller_unknown);
            break;
        case NodeState::verify:
            bootstrap_state_set(0, BootstrapStage::ac_controller_verify);
            break;
        case NodeState::flash:
            bootstrap_state_set(xbuddy_extension.get_flash_progress_percent(), BootstrapStage::ac_controller_flash);
            break;
        case NodeState::ready:
            bootstrap_state_set(0, BootstrapStage::ac_controller_ready);
            return true;
        }
    }
}
#endif

static void puppy_task_body([[maybe_unused]] void const *argument) {
    TaskDeps::wait(TaskDeps::Tasks::puppy_task_start);

    [[maybe_unused]] bool first_run = true;

    // by default, we want one modular bed and one dwarf
    PuppyBootstrap::BootstrapResult minimal_puppy_config = PuppyBootstrap::MINIMAL_PUPPY_CONFIG;

    do {
        // reset and flash the puppies
        auto bootstrap_result = bootstrap_puppies(minimal_puppy_config);
        // once some puppies are detected, consider this minimal puppy config (do no allow disconnection of puppy while running)
        minimal_puppy_config = bootstrap_result;

#if HAS_PUPPY_MODULARBED()
        // set what puppies are connected
        modular_bed.set_enabled(bootstrap_result.is_dock_occupied(Dock::MODULAR_BED));
#endif
#if HAS_DWARF()
        for (const auto dwarf_dock : DWARFS) {
            dwarfs[to_dwarf_index(dwarf_dock)].set_enabled(bootstrap_result.is_dock_occupied(dwarf_dock));
        }
#endif

        // wait for puppies to boot up, ensure they are running
        verify_puppies_running();

        do {
            // do intial scan of puppies to init them
            if (!puppy_initial_scan()) {
                break;
            }

#if ENABLED(PRUSA_TOOLCHANGER)
            // select active tool (previously active tool, or first one when starting)
            if (!prusa_toolchanger.init(first_run)) {
                log_error(Puppies, "Unable to select tool, retrying");
                break;
            }
#endif

#if HAS_AC_CONTROLLER()
            if (!wait_for_ac_controller()) {
                break; // go to puppy recovery
            }
#endif

            TaskDeps::provide(TaskDeps::Dependency::puppies_ready);
            first_run = false;
            log_info(Puppies, "Puppies are ready");

            TaskDeps::wait(TaskDeps::Tasks::puppy_run);

#if HAS_DWARF()
            // write current Marlin's state of the E TMC
            stepperE0.push();
#endif

            // now run puppy main loop
            puppy_task_loop();
        } while (false);

        if (stop_request) {
            // stop of puppy task was requested, stop here gracefully, without holding any mutexes and such
            osThreadSuspend(nullptr);
        }

        log_error(Puppies, "Communication error, going to recovery puppies");
        osDelay(1300); // Needs to be here to give puppies time to finish dumping
    } while (true);
}

void start_puppy_task() {

    osThreadCCMDef(puppies, puppy_task_body, TASK_PRIORITY_PUPPY_TASK, 0, 896);
    puppy_task_handle = osThreadCreate(osThread(puppies), NULL);
}

void suspend_puppy_task() {
    // ask puppy thread to stop its execution
    stop_request = true;
}

} // namespace buddy::puppies
