/// @file
#include <freertos/mutex.hpp>

#include <catch2/catch.hpp>

thread_local int freertos::Mutex::locked_mutex_count = 0;

freertos::Mutex::Mutex() {
}

freertos::Mutex::~Mutex() {
}

void freertos::Mutex::lock() {
    locked_mutex_count++;
}

void freertos::Mutex::unlock() {
    REQUIRE(locked_mutex_count > 0);
    locked_mutex_count--;
}
