#include "crash_dump_handlers.hpp"

#include <common/w25x.hpp>
#include <cstring>
#include <device/board.h>
#include <logging/log.hpp>

LOG_COMPONENT_REF(CrashDump);

namespace crash_dump {
std::span<const DumpHandler *> get_present_dumps(BufferT &buffer) {
    size_t num_present { 0 };
    for (const auto &handler : dump_handlers) {
        if (handler.presence_check()) {
            buffer[num_present++] = &handler;
        }
    }

    return { buffer.begin(), num_present };
}

} // namespace crash_dump
