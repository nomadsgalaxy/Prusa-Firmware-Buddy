#pragma once

#include <cstddef>
#include <cstring>

/// Circular buffer capable of storing buffer_size bytes,
/// which can be read and written in arbitrary-sized chunks.
template <size_t buffer_size>
class YetAnotherCircularBuffer {
private:
    /// Index into buffer where the next try_read() happens.
    size_t read_index_ { 0 };

    /// Index into buffer where the next try_write() happens.
    size_t write_index_ { 0 };

    /// Size of the valid data in the buffer.
    /// This is maintained independently in order to tell apart between empty buffer and full buffer.
    size_t size_ { 0 };

    /// Underlying buffer.
    std::byte buffer[buffer_size];

public:
    /// Try to read read_size bytes into read_data; return false if that is not possible.
    [[nodiscard]] bool try_read(std::byte *read_data, size_t read_size) {
        if (size_ < read_size) {
            // buffer doesn't contain enough bytes
            return false;
        }

        const size_t new_read_index = read_index_ + read_size;
        if (new_read_index > buffer_size) {
            // read wraps around the end of the buffer
            const size_t s1 = buffer_size - read_index_;
            const size_t s2 = read_size - s1;
            memcpy(read_data, buffer + read_index_, s1);
            memcpy(read_data + s1, buffer, s2);
            read_index_ = s2;
        } else {
            // read doesn't wrap around the end of the buffer
            memcpy(read_data, buffer + read_index_, read_size);
            read_index_ = (new_read_index == buffer_size) ? 0 : new_read_index;
        }

        // maintain the size
        size_ -= read_size;
        return true;
    }

    /// Try to write write_size bytes into write_data; return false if that is not possible.
    [[nodiscard]] bool try_write(const std::byte *write_data, size_t write_size) {
        if (buffer_size - size_ < write_size) {
            // buffer doesn't have enough space
            return false;
        }

        const size_t new_write_index = write_index_ + write_size;
        if (new_write_index > buffer_size) {
            // write wraps around the end of the buffer
            const size_t s1 = buffer_size - write_index_;
            const size_t s2 = write_size - s1;
            memcpy(buffer + write_index_, write_data, s1);
            memcpy(buffer, write_data + s1, s2);
            write_index_ = s2;
        } else {
            // write doesn't wrap around the end of the buffer
            memcpy(buffer + write_index_, write_data, write_size);
            write_index_ = (new_write_index == buffer_size) ? 0 : new_write_index;
        }

        // maintain the size
        size_ += write_size;
        return true;
    }

    /// Return size of data available for reading in the buffer. Use this with caution.
    size_t size() const {
        return size_;
    }

    /// Return size of available empty space for writing in the buffer. Use with caution.
    size_t available() const {
        return buffer_size - size();
    }

    void clear() {
        size_ = 0;
        read_index_ = 0;
        write_index_ = 0;
    }
};
