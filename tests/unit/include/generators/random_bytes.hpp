#pragma once
#include <catch2/catch.hpp>

struct RandomBytesGenerator final : public Catch::Generators::IGenerator<std::vector<uint8_t>> {
    RandomBytesGenerator(size_t min, size_t max)
        : min(min)
        , max(max) {
        current.reserve(max);
        next();
    }
    explicit RandomBytesGenerator(size_t length)
        : RandomBytesGenerator(length, length) {}

    bool next() override {
        size_t new_size = min;
        if (max > min) {
            new_size += rng() % (max - min + 1);
        }

        current.clear();
        for (size_t i = 0; i < new_size; ++i) {
            current.push_back(rng() % 256);
        }

        return true;
    }

    const std::vector<uint8_t> &get() const override {
        return current;
    }

    std::string stringifyImpl() const {
        std::ostringstream oss {};
        bool first = true;
        oss << "{";
        for (const auto val : current) {
            if (first) {
                first = false;
            } else {
                oss << ", ";
            }
            oss << val;
        }
        oss << "}";
        return oss.str();
    }

protected:
    std::mt19937_64 rng {};
    size_t min, max;
    std::vector<uint8_t> current {};
};

auto random_bytes(size_t min, size_t max) {
    return Catch::Generators::GeneratorWrapper<std::vector<uint8_t>>(Catch::Generators::pf::make_unique<RandomBytesGenerator>(min, max));
}

auto random_bytes(size_t length) {
    return Catch::Generators::GeneratorWrapper<std::vector<uint8_t>>(Catch::Generators::pf::make_unique<RandomBytesGenerator>(length));
}
