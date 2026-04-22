/// @file source-level compatible mock of logging
#pragma once

namespace internal {
void log(const char *, ...);
}

#define log_debug(component, fmt, ...)    internal::log(fmt, ##__VA_ARGS__)
#define log_info(component, fmt, ...)     internal::log(fmt, ##__VA_ARGS__)
#define log_error(component, fmt, ...)    internal::log(fmt, ##__VA_ARGS__)
#define log_warning(component, fmt, ...)  internal::log(fmt, ##__VA_ARGS__)
#define log_critical(component, fmt, ...) internal::log(fmt, ##__VA_ARGS__)

#define LOG_COMPONENT_DEF(name, severity)
#define LOG_COMPONENT_REF(component)
