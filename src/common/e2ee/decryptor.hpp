#pragma once

#include "movable_aes_context.hpp"
#include "e2ee.hpp"

namespace e2ee {

class Decryptor {
public:
    static constexpr uint32_t BlockSize = 16;
    using Block = std::array<uint8_t, BlockSize>;
    void setup_block(uint64_t offset, uint32_t block_size);
    bool decrypt(FILE *file, uint8_t *buffer, size_t size);
    void set_cipher_info(e2ee::SymmetricCipherInfo keys);
    Decryptor();
    Decryptor(const Decryptor &) = delete;
    Decryptor operator=(const Decryptor &) = delete;
    Decryptor(Decryptor &&) = default;
    Decryptor &operator=(Decryptor &&) = default;
    ~Decryptor();

private:
    MovableAesContext aes_ctx;
    uint32_t remaining_encrypted_data_size = 0;
    uint32_t num_of_hmacs = 0;
    uint8_t *cache_curr_pos = 0;
    uint8_t *cache_end = 0;
    Block cache = {};
    Block iv = {};
};

} // namespace e2ee
