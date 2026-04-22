#include "e2ee.hpp"
#include <mbedtls/sha256.h>

namespace e2ee {

class SHA256MultiuseHash {
public:
    SHA256MultiuseHash();

    ~SHA256MultiuseHash();

    void get_hash(uint8_t *buffer, size_t buffer_size);

    void update(const uint8_t *data, size_t size);

private:
    mbedtls_sha256_context sha256_ctx;
    void reset();
};

} // namespace e2ee
