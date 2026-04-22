#pragma once

#include <array>
#include <cstddef>
#include <utility>

#include "o1heap.h"

/// C++ wrapper for o1heap
template <size_t size_>
class O1Heap
{
public:
    static constexpr size_t size = size_;

    O1Heap()
    {
        if (o1heapInit(buffer_.data(), size) != instance())
        {
            std::abort();
        }
    }

    inline O1HeapInstance* instance() { return reinterpret_cast<O1HeapInstance*>(buffer_.data()); }

    inline void* alloc(size_t bytes) { return o1heapAllocate(instance(), bytes); }
    inline void  free(void* ptr) { o1heapFree(instance(), ptr); }

private:
    alignas(O1HEAP_ALIGNMENT) std::array<std::byte, size> buffer_;
};
