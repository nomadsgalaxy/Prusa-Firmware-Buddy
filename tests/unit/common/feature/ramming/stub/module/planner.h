#pragma once

struct Planner {
    void synchronize() {}
    bool draining() { return false; }
};
inline Planner planner;

struct [[maybe_unused]] Temporary_Reset_Motion_Parameters {};
