#pragma once

#include <cstdlib>

// Deleter that calls free, not delete
struct FreeDeleter {
    void operator()(void *p) {
        free(p);
    }
};
