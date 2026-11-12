#pragma once

#include <cstddef>
#include <cstdint>

#include "ring_buffer.h"

enum class LineResult {
    None,    // No complete line available yet.
    Line,    // A complete line is available via Line()/Length().
    TooLong  // A line longer than MaxLen was discarded.
};

// Assembles newline-terminated command lines from bytes in a RingBuffer.
// Fixed-size storage only; no dynamic allocation.
template <std::size_t MaxLen>
class LineAssembler {
public:
    // Consumes bytes from rx until a line ends or rx is empty, and stops
    // immediately after a '\n' so later bytes stay in rx for the next call.
    // See CANDIDATE_TASK.md Part B for the exact rules.
    template <std::size_t N>
    LineResult Poll(RingBuffer<N>& rx) {
        // TODO (candidate)
        (void)rx;
        return LineResult::None;
    }

    // The most recent complete line, NUL-terminated, without "\r\n".
    // Valid until the next call to Poll().
    const char* Line() const { return line_; }
    std::size_t Length() const { return length_; }

private:
    char line_[MaxLen + 1] = {};
    std::size_t length_ = 0;
    // TODO (candidate): any further state you need.
};
