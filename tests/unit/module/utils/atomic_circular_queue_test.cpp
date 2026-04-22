#include <catch2/catch.hpp>
#include <utils/atomic_circular_queue.hpp>

TEST_CASE("atomic_circular_queue", "[atomic_circular_queue]") {
    AtomicCircularQueue<uint8_t, size_t, 8> queue;

    uint8_t in = 0;
    uint8_t out = 0;

    // Start empty
    REQUIRE(queue.isEmpty() == true);
    REQUIRE(queue.isFull() == false);
    REQUIRE(queue.size() == 8);
    REQUIRE(queue.count() == 0);

    // Fill up
    for (size_t i = 0; i < 7; i++) {
        REQUIRE(queue.enqueue(in++) == true);
        REQUIRE(queue.isEmpty() == false);
        REQUIRE(queue.isFull() == false);
        REQUIRE(queue.count() == i + 1);
    }

    // Last in
    REQUIRE(queue.enqueue(in++) == true);
    REQUIRE(queue.isEmpty() == false);
    REQUIRE(queue.isFull() == true);
    REQUIRE(queue.count() == 8);

    // One more won't fit
    REQUIRE(queue.enqueue(in++) == false);
    REQUIRE(queue.isEmpty() == false);
    REQUIRE(queue.isFull() == true);
    REQUIRE(queue.count() == 8);

    // Take and fill few times around
    for (size_t i = 0; i < 36; i++) {
        uint8_t dequeued = 255;
        REQUIRE(queue.dequeue(dequeued) == true);
        REQUIRE(dequeued == out++);
        REQUIRE(queue.count() == 7);
        REQUIRE(queue.isEmpty() == false);
        REQUIRE(queue.isFull() == false);
        REQUIRE(queue.enqueue(8 + i) == true);
        REQUIRE(queue.isEmpty() == false);
        REQUIRE(queue.isFull() == true);
        REQUIRE(queue.count() == 8);
    }

    // Take out except for last
    for (size_t i = 0; i < 7; i++) {
        uint8_t dequeued = 255;
        REQUIRE(queue.dequeue(dequeued) == true);
        REQUIRE(dequeued == out++);
        REQUIRE(queue.isEmpty() == false);
        REQUIRE(queue.isFull() == false);
        REQUIRE(queue.count() == 7 - i);
    }

    // Take last
    REQUIRE(queue.count() == 1);
    uint8_t dequeued = 255;
    REQUIRE(queue.dequeue(dequeued) == true);
    REQUIRE(dequeued == out++);
    REQUIRE(queue.isEmpty() == true);
    REQUIRE(queue.isFull() == false);
    REQUIRE(queue.count() == 0);
    REQUIRE(queue.dequeue(dequeued) == false);
}
