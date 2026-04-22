#include "yet_another_circular_buffer.hpp"

#include <array>
#include <catch2/catch.hpp>

constexpr size_t buffer_size = 8;

using Buffer = YetAnotherCircularBuffer<buffer_size>;

TEST_CASE("Basic write and read", "[buffer]") {
    Buffer buffer;
    std::array<std::byte, 4> write_data { std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 4 } };

    REQUIRE(buffer.try_write(write_data.data(), write_data.size()) == true);

    std::array<std::byte, 4> read_data {};
    REQUIRE(buffer.try_read(read_data.data(), read_data.size()) == true);

    REQUIRE(read_data == write_data);
}

TEST_CASE("Write and read with wrap-around", "[buffer]") {
    Buffer buffer;

    std::array<std::byte, 6> part1 {
        std::byte { 1 }, std::byte { 2 }, std::byte { 3 },
        std::byte { 4 }, std::byte { 5 }, std::byte { 6 }
    };
    REQUIRE(buffer.try_write(part1.data(), part1.size()) == true);

    std::array<std::byte, 4> temp {};
    REQUIRE(buffer.try_read(temp.data(), temp.size()) == true);

    std::array<std::byte, 4> part2 {
        std::byte { 7 }, std::byte { 8 }, std::byte { 9 }, std::byte { 10 }
    };
    REQUIRE(buffer.try_write(part2.data(), part2.size()) == true); // Wraps

    std::array<std::byte, 6> read_result {};
    REQUIRE(buffer.try_read(read_result.data(), read_result.size()) == true);

    std::array<std::byte, 6> expected {
        std::byte { 5 }, std::byte { 6 },
        std::byte { 7 }, std::byte { 8 },
        std::byte { 9 }, std::byte { 10 }
    };
    REQUIRE(read_result == expected);
}

TEST_CASE("Buffer overflow check", "[buffer]") {
    Buffer buffer;

    std::array<std::byte, buffer_size> data {};
    data.fill(std::byte { 0xAA });

    REQUIRE(buffer.try_write(data.data(), data.size()) == true);
    REQUIRE(buffer.try_write(data.data(), 1) == false); // Overflow
}

TEST_CASE("Buffer underflow check", "[buffer]") {
    Buffer buffer;

    std::array<std::byte, 1> read_data {};
    REQUIRE(buffer.try_read(read_data.data(), read_data.size()) == false); // Underflow
}

TEST_CASE("Partial wrap read", "[buffer]") {
    Buffer buffer;

    std::array<std::byte, 8> input {
        std::byte { 10 }, std::byte { 11 }, std::byte { 12 }, std::byte { 13 },
        std::byte { 14 }, std::byte { 15 }, std::byte { 16 }, std::byte { 17 }
    };
    REQUIRE(buffer.try_write(input.data(), input.size()) == true);

    std::array<std::byte, 6> discard {};
    REQUIRE(buffer.try_read(discard.data(), discard.size()) == true);

    std::array<std::byte, 4> new_data {
        std::byte { 18 }, std::byte { 19 }, std::byte { 20 }, std::byte { 21 }
    };
    REQUIRE(buffer.try_write(new_data.data(), new_data.size()) == true); // Wraps

    std::array<std::byte, 6> read_result {};
    REQUIRE(buffer.try_read(read_result.data(), read_result.size()) == true);

    std::array<std::byte, 6> expected {
        std::byte { 16 }, std::byte { 17 },
        std::byte { 18 }, std::byte { 19 },
        std::byte { 20 }, std::byte { 21 }
    };
    REQUIRE(read_result == expected);
}
