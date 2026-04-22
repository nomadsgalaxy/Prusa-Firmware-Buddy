#pragma once

#include <atomic>

/// Master boards last activity.
///
/// The master board changes the number periodically, we can watch it is
/// changing to be sure it is alive.
extern std::atomic<uint16_t> master_activity;
