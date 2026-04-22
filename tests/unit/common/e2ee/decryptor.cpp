#include <gcode_reader_binary.hpp>
#include <e2ee/decryptor.hpp>
#include <fcntl.h>
#include "catch2/catch.hpp"

constexpr uint8_t encryption_key[] = {
    0xb1, 0x1c, 0x6c, 0xb1, 0xf4, 0x10, 0x22, 0x11,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};
constexpr uint64_t iv = 1234567890;

uint32_t get_file_size(FILE *file) {
    auto saved_pos = ftell(file);
    fseek(file, 0, SEEK_END);
    uint32_t size = ftell(file);
    fseek(file, saved_pos, SEEK_SET);
    return size;
}

TEST_CASE("16 bytes encrypted") {
    e2ee::Decryptor decryptor;
    e2ee::SymmetricCipherInfo keys;
    keys.valid = true;
    memcpy(keys.keys->encryption_key, encryption_key, 16);
    decryptor.set_cipher_info(keys);
    uint8_t original[] = {
        0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20,
        0x73, 0x6f, 0x6d, 0x65, 0x20, 0x74, 0x65, 0x78
    };

    uint8_t buffer[16];
    FILE *file = fopen("./encrypted_text", "rb");
    auto data_size = get_file_size(file);
    decryptor.setup_block(iv, data_size);

    SECTION("Read one block - one go") {
        decryptor.decrypt(file, buffer, sizeof(buffer));
    }

    SECTION("Read by char") {
        memset(buffer, 0, sizeof(buffer));
        for (size_t i = 0; i < sizeof(buffer); i++) {
            decryptor.decrypt(file, buffer + i, 1);
            REQUIRE(memcmp(buffer, original, i) == 0);
        }
    }

    SECTION("Read by more sizes") {
        memset(buffer, 0, sizeof(buffer));
        decryptor.decrypt(file, buffer, 8);
        REQUIRE(memcmp(buffer, original, 8) == 0);
        decryptor.decrypt(file, buffer + 8, 2);
        REQUIRE(memcmp(buffer, original, 10) == 0);
        decryptor.decrypt(file, buffer + 10, 4);
        REQUIRE(memcmp(buffer, original, 14) == 0);
        decryptor.decrypt(file, buffer + 14, 2);
    }

    REQUIRE(memcmp(buffer, original, sizeof(original)) == 0);
}

TEST_CASE("20 bytes encrypted") {
    e2ee::Decryptor decryptor;
    e2ee::SymmetricCipherInfo keys;
    keys.valid = true;
    memcpy(keys.keys->encryption_key, encryption_key, 16);
    decryptor.set_cipher_info(keys);
    uint8_t original[] = {
        0x54,
        0x68,
        0x69,
        0x73,
        0x20,
        0x69,
        0x73,
        0x20,
        0x73,
        0x6f,
        0x6d,
        0x65,
        0x20,
        0x74,
        0x65,
        0x78,
        0x74,
        0x20,
        0x66,
        0x6f,
    };

    uint8_t buffer[20];
    FILE *file = fopen("./20_bytes_encrypted_text", "rb");
    auto data_size = get_file_size(file);
    decryptor.setup_block(iv, data_size);

    SECTION("Read one block - one go") {
        decryptor.decrypt(file, buffer, sizeof(buffer));
    }

    SECTION("Read by char") {
        memset(buffer, 0, sizeof(buffer));
        for (size_t i = 0; i < sizeof(buffer); i++) {
            decryptor.decrypt(file, buffer + i, 1);
            REQUIRE(memcmp(buffer, original, i + 1) == 0);
        }
    }

    SECTION("Read by more sizes 2 goes") {
        memset(buffer, 0, sizeof(buffer));
        decryptor.decrypt(file, buffer, 18);
        REQUIRE(memcmp(buffer, original, 18) == 0);
        decryptor.decrypt(file, buffer + 18, 2);
    }

    // Note: The move has to be either on the edge of the block or read enough after
    // so that we don't just take it from cache and really call the decryption
    SECTION("Test move constructor of decryptor") {
        memset(buffer, 0, sizeof(buffer));
        decryptor.decrypt(file, buffer, 16);
        REQUIRE(memcmp(buffer, original, 16) == 0);
        e2ee::Decryptor another_decryptor(std::move(decryptor));
        another_decryptor.decrypt(file, buffer + 16, 4);
    }

    SECTION("Test move operator of decryptor") {
        memset(buffer, 0, sizeof(buffer));
        decryptor.decrypt(file, buffer, 16);
        REQUIRE(memcmp(buffer, original, 16) == 0);
        e2ee::Decryptor another_decryptor;
        another_decryptor = std::move(decryptor);
        another_decryptor.decrypt(file, buffer + 16, 4);
    }

    SECTION("Read by more sizes") {
        memset(buffer, 0, sizeof(buffer));
        decryptor.decrypt(file, buffer, 8);
        REQUIRE(memcmp(buffer, original, 8) == 0);
        decryptor.decrypt(file, buffer + 8, 2);
        REQUIRE(memcmp(buffer, original, 10) == 0);
        decryptor.decrypt(file, buffer + 10, 4);
        REQUIRE(memcmp(buffer, original, 14) == 0);
        decryptor.decrypt(file, buffer + 14, 2);
        REQUIRE(memcmp(buffer, original, 16) == 0);
        decryptor.decrypt(file, buffer + 16, 4);
    }

    REQUIRE(memcmp(buffer, original, sizeof(original)) == 0);
}
