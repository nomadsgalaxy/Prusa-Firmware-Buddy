#pragma once

/// Like std::lock_guard but doesn't bring in 7kB of std::crap
template <class Mutex>
class LockGuard {
private:
    Mutex &mutex;

public:
    explicit LockGuard(Mutex &mutex_)
        : mutex { mutex_ } { mutex.lock(); }
    ~LockGuard() { mutex.unlock(); }
    LockGuard(const LockGuard &) = delete;
};
