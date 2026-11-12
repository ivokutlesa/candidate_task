#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Fixed-capacity byte FIFO for a serial (UART) receive path.
//
// On a real MCU, Push() is called from the UART RX interrupt (single producer)
// and Pop() from the main loop (single consumer). The buffer must therefore be
// safe for exactly one producer and one consumer running concurrently,
// without locks, dynamic allocation, exceptions or blocking.
template <std::size_t N>
class RingBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "Capacity must be a power of two");

public:
    // Appends a byte. Returns false and counts an overflow if the buffer is
    // full; unread data must never be overwritten.
    bool Push(std::uint8_t byte) {
        const std::size_t write = write_.load(std::memory_order_relaxed);
        const std::size_t read = read_.load(std::memory_order_acquire);
        if (write - read == N) {
            overflows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        storage_[write & (N - 1)] = byte;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }

    // Removes the oldest byte. Returns false if the buffer is empty.
    bool Pop(std::uint8_t& byte) {
        const std::size_t read = read_.load(std::memory_order_relaxed);
        const std::size_t write = write_.load(std::memory_order_acquire);
        if (read == write) return false;

        byte = storage_[read & (N - 1)];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }

    // Number of unread bytes (0..N).
    std::size_t Size() const {
        const std::size_t write = write_.load(std::memory_order_acquire);
        const std::size_t read = read_.load(std::memory_order_acquire);
        const std::size_t size = write - read;
        return size <= N ? size : N;
    }

    bool Empty() const { return Size() == 0; }
    bool Full() const { return Size() == N; }
    static constexpr std::size_t Capacity() { return N; }

    // Total number of bytes rejected by Push() because the buffer was full.
    std::uint32_t Overflows() const {
        return overflows_.load(std::memory_order_relaxed);
    }

private:
    std::uint8_t storage_[N] = {};
    std::atomic<std::size_t> write_{0};
    std::atomic<std::size_t> read_{0};
    std::atomic<std::uint32_t> overflows_{0};
};
