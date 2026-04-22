#include <catch2/catch.hpp>
#include <bsod/bsod.h>
#include <stdexcept>

extern "C" void _bsod(const char *fmt, const char *file_name, int line_number, ...) {
    throw std::runtime_error(fmt);
}
