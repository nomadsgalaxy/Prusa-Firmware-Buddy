/// @file source-level compatible mock of freertos::Mutex
#pragma once

namespace freertos {

class Mutex {
public:
    static thread_local int locked_mutex_count;

    Mutex();
    ~Mutex();
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;
    void unlock();
    bool try_lock();
    void lock();
};

} // namespace freertos
