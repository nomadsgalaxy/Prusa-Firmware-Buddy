#pragma once
#include <cstdint>
#include <atomic>
#include <expected>

#include <utils/publisher.hpp>
#include <utils/atomic_circular_queue.hpp>

namespace queued_publisher {
enum class Error : uint8_t {
    overflow,
    // TODO: Is there a reason to have an EmptyQueue error?
    // EmptyQueue,
};
} // namespace queued_publisher

/**
 * @brief A publisher that queues data before calling observers.
 * Uses an atomic circular queue to store the data. This allows for data to be pushed from a different thread than the one publishing it.
 *
 * @tparam Publication Data type to be published.
 * @tparam QueueSize How many items to keep in the queue.
 *                   If the queue is full, no more data will be accepted until overflow is reported after clearing the queue.
 * @tparam Args Additional arguments to pass to the subscribers.
 */
template <typename Publication, size_t QueueSize, typename... Args>
class QueuedPublisher : public PublisherBase<const std::expected<Publication, queued_publisher::Error> &, Args...> {

public:
    using Error = queued_publisher::Error;
    using Expected = std::expected<Publication, Error>;

public:
    /// Can be called from a thread different to the one calling `call_all()`.
    /// If the queue is full, it will not accept new data until all present is sent and then an overflow is reported.
    bool push(const Publication &data) {
        if (overflow_flag) {
            // We can only track one overflow at a time,
            // so we have to wait till the overflow gets processed by the consuming thread
            // before risking causing another one
            return false;
        }
        if (!data_to_publish.enqueue(data)) {
            overflow_flag = true; // Set the overflow flag if the queue is full
            return false;
        }

        return true;
    }

    /**
     * @brief Publishes one event/data entry to all publishers.
     *
     * @param args additional arguments to pass to the subscribers
     * @returns true if there was something to publish
     */
    bool publish_one(Args &&...args) {
        Expected value {};
        if (data_to_publish.dequeue(*value)) {
            // Call all subscribers with the data and additional arguments

        } else if (overflow_flag) {
            value = std::unexpected(Error::overflow);
            overflow_flag = false; // Reset the overflow flag after reporting

        } else {
            return false;
        }

        this->call_all(value, std::forward<Args>(args)...);
        return true;
    }

    /**
     * @brief Publishes all available events/data entries to the publishers
     * @returns true if anything was published
     */
    bool publish_all() {
        bool result = false;
        while (publish_one()) {
            result = true;
        }
        return result;
    }

    bool is_empty() const {
        /// Until the overflow flag is cleared, the queue is not accepting new data.
        /// This is done so that the call_all() runs only a single call for every subscriber per call.
        /// It also means that if the call_all() is run until the queue is empty, the overflow flag will never clear and the queue will not accept new data.
        return data_to_publish.isEmpty() && !overflow_flag;
    }

private:
    using QueueIndex = uint8_t;
    AtomicCircularQueue<Publication, QueueIndex, QueueSize> data_to_publish;

    std::atomic<bool> overflow_flag { false };
};
