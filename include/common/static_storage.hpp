#pragma once

#include <cstddef>
#include <array>

/**
 * Convenience type for static untyped storage of a given size.
 *
 * Allows you to create, destroy and refer to some type created
 * at this storage.
 *
 * It is caller's responsibility to destroy constructed elements
 * and to track what type (if any) is currently in the storage.
 *
 * This is basically std::aligned_union_t which doesn't require
 * knowledge of the types upfront with some convenience functions.
 */
template <size_t size_, typename Alignment_ = void *>
class [[deprecated("Please use InplaceAny, StaticStorage is prone to UB")]] StaticStorage {

public:
    using Alignment = Alignment_;
    static constexpr size_t size = size_;

    /** Access the value of type T previously created at this storage */
    template <class T>
    constexpr T *as() {
        return static_cast<T *>(static_cast<void *>(bytes.data()));
    }

    /**
     * Call constructor of T. It must be preceded either by construction of this storage
     * or by call to destroy() (possibly of some other type)
     */
    template <class T, class... Args>
    constexpr T *create(Args &&...args) {
        static_assert(can_construct<T>());
        return std::construct_at(as<T>(), std::forward<Args>(args)...);
    }

    /**
     * Call destructor of T. It must have been previously created at this storage.
     */
    template <class T>
    constexpr void destroy() {
        return std::destroy_at(as<T>());
    }

    /**
     * Return true if the storage has just the right size to accomodate largest of given types.
     */
    template <class... T>
    static constexpr bool has_ideal_size_for() {
        return std::max({ sizeof(T)... }) == size;
    }

    /**
     * Return true if the storage is able to construct all provided types
     */
    template <class... T>
    static constexpr bool can_construct() {
        constexpr auto f = []<typename U>() {
            return sizeof(U) <= size && alignof(U) <= alignof(Alignment);
        };
        return (f.template operator()<T>() && ...);
    }

private:
    alignas(Alignment) std::array<std::byte, size> bytes;
};
