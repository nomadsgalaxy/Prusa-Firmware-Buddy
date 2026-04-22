#pragma once

#include <cstdint>
#include <optional>

/// Tri-state "bool" - off, on and third "other" state (undefined/middle/...)
struct Tristate {

public:
    enum Value : uint8_t {
        no = 0,
        yes = 1,
        other = 2
    };

public:
    constexpr inline Tristate() = default;
    constexpr inline Tristate(const Tristate &) = default;

    constexpr inline Tristate(Value val)
        : value(val) {}

    /// Implicit constructor from bool
    constexpr inline Tristate(bool val)
        : value(static_cast<Value>(val)) {}

    /// Creates tristate from std::optional<bool>. Nullopt is interpreted as Tristate:other
    constexpr static inline Tristate from_optional(std::optional<bool> value) {
        return value.has_value() ? Tristate(*value) : other;
    }

    constexpr inline bool operator==(const Tristate &) const = default;

public:
    Value value = Value::other;
};
static_assert(sizeof(Tristate) == 1);
