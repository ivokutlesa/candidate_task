#pragma once

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
        // TODO (candidate)
        (void)byte;
        return false;
    }

    // Removes the oldest byte. Returns false if the buffer is empty.
    bool Pop(std::uint8_t& byte) {
        // TODO (candidate)
        (void)byte;
        return false;
    }

    // Number of unread bytes (0..N).
    std::size_t Size() const {
        // TODO (candidate)
        return 0;
    }

    bool Empty() const { return Size() == 0; }
    bool Full() const { return Size() == N; }
    static constexpr std::size_t Capacity() { return N; }

    // Total number of bytes rejected by Push() because the buffer was full.
    std::uint32_t Overflows() const {
        // TODO (candidate)
        return 0;
    }

private:
    // TODO (candidate): storage and indices.
};
