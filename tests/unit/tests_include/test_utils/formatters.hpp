#pragma once

#include <ostream>
#include <iomanip>
#include <span>
#include <optional>
#include <expected>
#include <string_view>
#include <cstdint>
#include <variant>

#include <magic_enum.hpp>

namespace std {

inline ostream &operator<<(ostream &os, const basic_string_view<byte> &bytes) {
    const auto flags = os.flags();
    os << hex;
    for (auto byte : bytes) {
        os << (int)byte;
    }
    os.flags(flags);
    return os;
}

/// Override printing uint8_t as characters
inline ostream &operator<<(ostream &os, uint8_t val) {
    os << int(val);
    return os;
}
inline ostream &operator<<(ostream &os, int8_t val) {
    os << int(val);
    return os;
}

template <typename T>
ostream &operator<<(ostream &os, const span<T> &value) {
    os << "[";
    bool i = false;
    for (const auto &item : value) {
        if (i) {
            os << ", ";
        }
        i = true;
        os << item;
    }
    os << "]";
    return os;
}

template <typename T>
ostream &operator<<(ostream &os, const unexpected<T> &value) {
    os << "unexpected(" << value.error() << ")";
    return os;
}

template <typename T, typename E>
ostream &operator<<(ostream &os, const expected<T, E> &value) {
    if (!value) {
        os << unexpected(value.error());
    } else if constexpr (!std::is_same_v<T, void>) {
        os << *value;
    } else {
        os << "(void)";
    }
    return os;
}

template <typename T>
ostream &operator<<(ostream &os, const optional<T> &value) {
    if (!value) {
        os << "nullopt";
    } else {
        os << *value;
    }
    return os;
}

template <typename... T>
ostream &operator<<(ostream &os, const variant<T...> &value) {
    std::visit([&](const auto &val) {
        os << val;
    },
        value);
    return os;
}

std::ostream &operator<<(std::ostream &out, std::byte byte) {
    // std::byte is a enum without values, the formating with magic_enum isn't nice,
    // so don't move this function below the enum formatter
    const auto flags = out.flags();
    const auto width = out.width();
    const auto fill = out.fill();
    out << "0x" << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<uint32_t>(byte);
    out.fill(fill);
    out.width(width);
    out.setf(flags);
    return out;
}

template <typename T>
std::ostream &operator<<(std::ostream &os, T value)
    requires(std::is_enum_v<T>)
{
    os << "Error::" << magic_enum::enum_name(value);
    return os;
}

} // namespace std
