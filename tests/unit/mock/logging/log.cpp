/// @file
#include <logging/log.hpp>

#include <array>
#include <catch2/catch.hpp>
#include <cstdarg>
#include <cstdio>

void internal::log(const char *fmt, ...) {
    std::array<char, 256> buffer;
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);
    INFO(buffer.data());
}
