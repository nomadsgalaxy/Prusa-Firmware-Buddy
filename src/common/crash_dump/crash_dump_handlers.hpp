#pragma once
#include "crash_dump/dump.hpp"
#include <device/board.h>
#include <option/has_puppies.h>
#if HAS_PUPPIES()
    #include <puppies/puppy_crash_dump.hpp>
#endif

namespace crash_dump {
inline constexpr const char *buddy_dump_usb_path { "/usb/dump_buddy.bin" };

struct DumpHandler {
    bool (*presence_check)();
    void (*usb_save)();
    void (*remove)();
};

inline constexpr auto dump_handlers { std::to_array<DumpHandler>({
#if HAS_PUPPIES()
    {
        .presence_check = buddy::puppies::crash_dump::is_a_dump_in_filesystem,
        .usb_save = []() { buddy::puppies::crash_dump::save_dumps_to_usb(); },
        .remove = []() { buddy::puppies::crash_dump::remove_dumps_from_filesystem(); },
    },
#endif
        {
            .presence_check = []() { return dump_is_valid() && !dump_is_exported(); },
            .usb_save = []() { save_dump_to_usb(buddy_dump_usb_path); },
            .remove = []() {
                // dump is intentinaly not removed, just marked as exported. User can later export it from menu.
                dump_set_exported(); },
        },
}) };

inline constexpr auto dump_handlers_have_valid_pointers { []() {
    for (const auto &handle : dump_handlers) {
        if (!handle.presence_check || !handle.usb_save || !handle.remove) {
            return false;
        }
    }
    return true;
}() };
static_assert(dump_handlers_have_valid_pointers, "dump handlers must have valid function pointers");

using BufferT = std::array<const DumpHandler *, dump_handlers.size()>;

/**
 * @brief Gets the _present_dumps.
 *
 * @param buffer Buffer that will be used to store present dumps
 * @return Span into the buffer with present dumps
 */
std::span<const DumpHandler *> get_present_dumps(BufferT &buffer);

} // namespace crash_dump
