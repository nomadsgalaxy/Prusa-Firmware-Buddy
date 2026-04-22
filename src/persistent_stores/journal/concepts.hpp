#pragma once
#include <inplace_function.hpp>
#include <freertos/mutex.hpp>
#include <span>
#include <mutex>
#include <concepts>
#include <cstdint>

namespace journal {
using UpdateFunction = stdext::inplace_function<void(uint16_t, const std::span<const uint8_t> &)>;
using DumpCallback = stdext::inplace_function<void(void)>;

/// Flags for annotating store items
using ItemFlags = uint16_t;

template <typename T>
concept BackendC = requires(T &t, uint16_t id, const std::span<const uint8_t> &data, const UpdateFunction &update_function, DumpCallback dump_callback, std::span<const typename T::MigrationFunction> migration_functions) {
    { t.save(id, data) }
        -> std::same_as<void>;
    { t.lock() }
        -> std::same_as<std::unique_lock<freertos::Mutex>>;
    { t.load_all(update_function, migration_functions) };
    { T::MAX_ITEM_SIZE };
};
template <typename T>
concept ConfigC = requires() {
    { T::get_backend() };
};

template <typename T>
concept IsDeletedStoreItemC = T::is_deleted;

template <typename DataT>
concept StoreItemDataC = std::equality_comparable<DataT> && std::default_initializable<DataT> && std::is_trivially_copyable_v<DataT>;
} // namespace journal
