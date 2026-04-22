#pragma once
#include <cinttypes>
#include <cstdlib>
#include <concepts>

template <std::integral T, std::integral sumT, size_t SIZE>
class SumRingBuffer {
public:
    typedef sumT sum_type;
    static_assert(SIZE > 0, "Invalid input");

    void Clear() {
        count = 0;
        index = 0;
        sum = 0;
    };
    void Put(T sample) {
        if (count < SIZE) {
            count++;
        } else {
            sum -= pdata[index];
        }
        sum += sample;
        pdata[index] = sample;
        if (++index >= SIZE) {
            index = 0;
        }
    };
    void PopLast() {
        if (count) {
            size_t last_idx = (index - count) % SIZE;
            sum -= pdata[last_idx];
            --count;
        }
    }
    inline constexpr size_t GetSize() const {
        return SIZE;
    };
    size_t GetCount() const {
        return count;
    };
    sumT GetSum() const {
        return sum;
    };

protected:
    size_t count { 0 };
    size_t index { 0 };
    T pdata[SIZE] {};
    sumT sum { 0 };
};
