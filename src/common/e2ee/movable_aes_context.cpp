#include "movable_aes_context.hpp"
#include <cstring>
#include <utility>

MovableAesContext::MovableAesContext() {
    mbedtls_aes_init(context.get());
}

MovableAesContext::~MovableAesContext() {
    mbedtls_aes_free(context.get());
}

MovableAesContext::MovableAesContext(MovableAesContext &&other) {
    *this = std::move(other);
}

MovableAesContext &MovableAesContext::operator=(MovableAesContext &&other) {
    // This handling of mbedtls internals is a bit sketchy, but it works O:-)
    // In the next version of mbedtls they made the rk pointer in to an offset,
    // so if we ever upgrade, this will not be necessary. Also it will fail to
    // copile, because the rk is renamed to rk_offset.
    if (this != &other) {
        mbedtls_aes_free(context.get());
        context->nr = other.context->nr;
        memcpy(context->buf, other.context->buf, sizeof(context->buf));
        size_t offset = other.context->rk - other.context->buf;
        context->rk = context->buf + offset;
    }
    return *this;
}
