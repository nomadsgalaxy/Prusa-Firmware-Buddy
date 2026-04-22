#pragma once
#include <crash_dump/secret.hpp>
#include <mbedtls/aes.h>

struct MovableAesContext {
    MovableAesContext();
    MovableAesContext(const MovableAesContext &other) = delete;
    MovableAesContext operator=(const MovableAesContext &other) = delete;
    MovableAesContext(MovableAesContext &&other);
    MovableAesContext &operator=(MovableAesContext &&other);
    ~MovableAesContext();
    crash_dump::Secret<mbedtls_aes_context> context;
};
