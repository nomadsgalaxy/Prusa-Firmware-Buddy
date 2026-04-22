#pragma once

#include "decryptor.hpp"
#include <core/core.hpp>

namespace e2ee {

bool read_encrypted_block_header(FILE *file, bgcode::core::BlockHeader &header, e2ee::Decryptor &decryptor);

bool is_metadata_block(bgcode::core::EBlockType type);

void file_header_sha256(const bgcode::core::FileHeader &file_header, SHA256MultiuseHash &hash);

void block_header_sha256_update(SHA256MultiuseHash &hash, const bgcode::core::BlockHeader &header);

void block_crc_sha256_update(SHA256MultiuseHash &hash, FILE *file);

void block_sha_256_update(SHA256MultiuseHash &hash, const bgcode::core::BlockHeader &header, bgcode::core::EChecksumType crc, FILE *file);

class BlockSequenceValidator {
public:
    const char *metadata_found(bgcode::core::FileHeader file_header, bgcode::core::BlockHeader block_header);
    const char *identity_block_found(bgcode::core::FileHeader file_header, bgcode::core::BlockHeader block_header);
    const char *key_block_found(bgcode::core::FileHeader file_header, bgcode::core::BlockHeader block_header);
    const char *encrypted_block_found(bgcode::core::BlockHeader block_header);
    const char *gcode_block_found();
    uint32_t get_num_of_key_blocks() const;

private:
    long last_metadata_pos_end = 0;
    long identity_pos_end = 0;
    bool have_identity_block = false;
    bool have_key_block = false;
    long key_block_pos_end = 0;
    bool have_gcode_block = false;
    // So that we know how many HMAC there will be in the encrypted blocks
    uint32_t num_of_key_blocks = 0;
};
} // namespace e2ee
