#include <puppy/xbuddy_extension/modbus.hpp>

#include <catch2/catch.hpp>

#include <cstring>

using namespace modbus;

namespace {

class MockDevice1 final : public Callbacks {
public:
    static constexpr size_t reg_count = 4;
    std::array<uint16_t, reg_count> registers = { 0, 1, 2, 3 };

    virtual Status read_registers(uint16_t address, std::span<uint16_t> out) override {
        REQUIRE(reinterpret_cast<intptr_t>(out.data()) % alignof(uint16_t) == 0);
        if (address + out.size() >= reg_count) {
            return Status::IllegalAddress;
        }

        for (size_t i = 0; i < out.size(); i++) {
            out[i] = registers[address + i];
        }

        return Status::Ok;
    }

    virtual Status write_registers(uint16_t address, std::span<const uint16_t> in) override {
        REQUIRE(reinterpret_cast<intptr_t>(in.data()) % alignof(uint16_t) == 0);
        if (address + in.size() >= reg_count) {
            return Status::IllegalAddress;
        }

        for (size_t i = 0; i < in.size(); i++) {
            registers[address + i] = in[i];
        }

        return Status::Ok;
    }
};

std::vector<std::byte> s2b(const char *s, size_t len) {
    const std::byte *begin = reinterpret_cast<const std::byte *>(s);
    auto result = std::vector(begin, begin + len);
    // We assume this is true due to allocator that gives data to vector.
    // If not, we'll have to manually fix this in the tests (eg. the failure
    // would be problem of tests, not of the tested code).
    REQUIRE(reinterpret_cast<intptr_t>(result.data()) % alignof(uint16_t) == 0);
    return result;
}

std::vector<std::byte> s2b(const char *s) {
    const size_t len = strlen(s);
    return s2b(s, len);
}

std::span<const std::byte> trans_with_crc(Dispatch &dispatch, const char *s, size_t len, std::span<std::byte> out) {
    auto *b = reinterpret_cast<const std::byte *>(s);
    std::vector<std::byte> in(b, b + len);
    uint16_t crc = compute_crc(in);
    in.push_back(static_cast<std::byte>(crc & 0xFF));
    in.push_back(static_cast<std::byte>(crc >> 8));
    // We assume this is true due to allocator that gives data to vector.
    // If not, we'll have to manually fix this in the tests (eg. the failure
    // would be problem of tests, not of the tested code).
    REQUIRE(reinterpret_cast<intptr_t>(in.data()) % alignof(uint16_t) == 0);

    return handle_transaction(dispatch, in, out);
}

} // namespace

TEST_CASE("Modbus transaction - refused inputs") {
    MockDevice1 md1;
    std::array devices = {
        modbus::Dispatch::Device { 1, md1 },
    };
    modbus::Dispatch dispatch { devices };

    alignas(uint16_t) std::array<std::byte, 40> out_buffer;

    SECTION("Too short") {
        REQUIRE(handle_transaction(dispatch, {}, out_buffer).empty());
    }

    SECTION("Garbage") {
        // Complete nonsense
        auto in = s2b("hello world");
        REQUIRE(handle_transaction(dispatch, in, out_buffer).empty());
    }

    SECTION("Wrong CRC") {
        // This would be a valid message, but CRC is wrong.
        // device: 1
        // register: 4
        // address: 1
        // count: 1
        // crc: XX
        auto in = s2b("\1\4\0\1\0\1XX", 8);
        REQUIRE(handle_transaction(dispatch, in, out_buffer).empty());
    }

    SECTION("Other device") {
        REQUIRE(trans_with_crc(dispatch, "\2\4\0\1\0\1", 6, out_buffer).empty());
    }

    SECTION("Other device with unknown function") {
        REQUIRE(trans_with_crc(dispatch, "\2\8\0\1\0\1", 6, out_buffer).empty());
    }

    SECTION("Low bytes") {
        trans_with_crc(dispatch, "\1\x10\0\1\0\2\3\0AA\0", 11, out_buffer);
    }

    SECTION("High bytes") {
        trans_with_crc(dispatch, "\1\x10\0\1\0\2\5\0AA\0", 11, out_buffer);
    }

    SECTION("Short message") {
        trans_with_crc(dispatch, "\1\x10\0\1\0\2\4\0AA", 10, out_buffer);
    }
}

TEST_CASE("Invalid function") {
    MockDevice1 md1;
    std::array devices = {
        modbus::Dispatch::Device { 1, md1 },
    };
    modbus::Dispatch dispatch { devices };

    alignas(uint16_t) std::array<std::byte, 40> out_buffer;

    auto response = trans_with_crc(dispatch, "\1\x22\0\1\0\2\4\0AA\0", 11, out_buffer);
    REQUIRE(response.size() == 5);
    REQUIRE(compute_crc(response) == 0);
    const uint8_t *resp = reinterpret_cast<const uint8_t *>(response.data());

    REQUIRE(resp[0] == 1);
    // Error + function 22
    REQUIRE(resp[1] == 0x22 + 0x80);
    REQUIRE(resp[2] == 1);
}

TEST_CASE("Invalid address") {
    MockDevice1 md1;
    std::array devices = {
        modbus::Dispatch::Device { 1, md1 },
    };
    modbus::Dispatch dispatch { devices };

    alignas(uint16_t) std::array<std::byte, 40> out_buffer;

    // 3 is in-range, 4 is out of range
    auto response = trans_with_crc(dispatch, "\1\x10\0\3\0\2\4\0AA\0", 11, out_buffer);
    REQUIRE(response.size() == 5);
    REQUIRE(compute_crc(response) == 0);
    const uint8_t *resp = reinterpret_cast<const uint8_t *>(response.data());

    REQUIRE(resp[0] == 1);
    // Error + function 10
    REQUIRE(resp[1] == 0x10 + 0x80);
    REQUIRE(resp[2] == 2);
}

TEST_CASE("Success write") {
    MockDevice1 md1;
    std::array devices = {
        modbus::Dispatch::Device { 1, md1 },
    };
    modbus::Dispatch dispatch { devices };

    alignas(uint16_t) std::array<std::byte, 40> out_buffer;

    auto response = trans_with_crc(dispatch, "\1\x10\0\1\0\2\4\0AA\0", 11, out_buffer);
    REQUIRE(response.size() == 8);
    REQUIRE(compute_crc(response) == 0);
    const uint8_t *resp = reinterpret_cast<const uint8_t *>(response.data());

    REQUIRE(resp[0] == 1);
    // Error + function 10
    REQUIRE(resp[1] == 0x10);

    REQUIRE(md1.registers[0] == 0);
    REQUIRE(md1.registers[1] == 65);
    REQUIRE(md1.registers[2] == 65 << 8);
    REQUIRE(md1.registers[3] == 3);
}

TEST_CASE("Success read") {
    MockDevice1 md1;
    std::array devices = {
        modbus::Dispatch::Device { 1, md1 },
    };
    modbus::Dispatch dispatch { devices };

    alignas(uint16_t) std::array<std::byte, 40> out_buffer;

    auto response = trans_with_crc(dispatch, "\1\3\0\1\0\2", 6, out_buffer);
    REQUIRE(response.size() == 9);
    REQUIRE(compute_crc(response) == 0);
    const uint8_t *resp = reinterpret_cast<const uint8_t *>(response.data());

    REQUIRE(memcmp("\1\3\4\0\1\0\2", resp, 7) == 0);
}
