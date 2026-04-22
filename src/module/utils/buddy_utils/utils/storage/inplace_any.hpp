/// \file
#pragma once

#include <memory>
#include <type_traits>
#include <cassert>

#include <utils/uncopyable.hpp>

/// Just a destructor
using InplaceAnyRTTI = void (*)(void *);

template <typename T>
static consteval InplaceAnyRTTI inplace_any_rtti() {
    return [](void *data) { std::destroy_at<T>(reinterpret_cast<T *>(data)); };
}

// Sanity check that we're really creating unique pointer for each type
static_assert(inplace_any_rtti<uint32_t>() != inplace_any_rtti<uint8_t>());

/// Alternative to std::any that never dynamically allocates
/// Cannot be moved though
template <size_t storage_size, typename Alignment_ = void *>
class InplaceAny : public Uncopyable {

public:
    using Alignment = Alignment_;

public:
    constexpr InplaceAny() = default;

    ~InplaceAny() {
        reset();
    }

public:
    /// Constructs the provided type inside the storage. Destroys what was previously there.
    template <typename T, typename... Args>
    constexpr T *emplace(Args &&...args) {
        static_assert(sizeof(T) <= storage_size);
        static_assert(std::alignment_of_v<T> <= std::alignment_of_v<Alignment>);

        reset();
        type_ = inplace_any_rtti<T>();
        return std::construct_at<T, Args...>(reinterpret_cast<T *>(data_.data()), std::forward<Args>(args)...);
    }

    /// Destroys whatever is in the storage
    constexpr void reset() {
        if (type_) {
            type_(data_.data());
            type_ = nullptr;
        }
    }

public:
    /// \returns reference to the value of type T. Assumes the Any holds the correct type.
    template <typename T>
    constexpr inline T &get() {
        assert(holds_alternative<T>());
        return *reinterpret_cast<T *>(data_.data());
    }

    /// \returns reference to the value of type T. Assumes the Any holds the correct type.
    template <typename T>
    constexpr inline const T &get() const {
        assert(holds_alternative<T>());
        return *reinterpret_cast<const T *>(data_.data());
    }

    /// \returns pointer to the value of type T, if the InplaceAny is of the provided type, otherwise nullptr
    template <typename T>
    constexpr inline T *get_if() {
        return holds_alternative<T>() ? &get<T>() : nullptr;
    }

    /// \returns pointer to the value of type T, if the InplaceAny is of the provided type, otherwise nullptr
    template <typename T>
    constexpr inline const T *get_if() const {
        return holds_alternative<T>() ? &get<T>() : nullptr;
    }

    /// \returns if the variant holds alternative of the given type
    template <typename T>
    constexpr inline bool holds_alternative() const {
        return type_ == inplace_any_rtti<T>();
    }

    constexpr inline bool has_value() const {
        return type_ != nullptr;
    }

private:
    /// Contents of the variant
    alignas(Alignment) std::array<uint8_t, storage_size> data_ = { 0 };

    /// Pointer representing the type
    InplaceAnyRTTI type_ = nullptr;
};
