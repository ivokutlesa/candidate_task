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
        if (reset_on_poll_) {
            length_ = 0;
            line_[0] = '\0';
            reset_on_poll_ = false;
        }

        std::uint8_t byte = 0;
        while (rx.Pop(byte)) {
            const char ch = static_cast<char>(byte);

            if (discarding_) {
                if (ch == '\n') {
                    discarding_ = false;
                    pending_cr_ = false;
                    length_ = 0;
                    line_[0] = '\0';
                    return LineResult::TooLong;
                }
                continue;
            }

            if (ch == '\n') {
                pending_cr_ = false;
                line_[length_] = '\0';
                reset_on_poll_ = true;
                return LineResult::Line;
            }

            if (pending_cr_) {
                pending_cr_ = false;
                if (!Append('\r')) continue;
            }

            if (ch == '\r') {
                pending_cr_ = true;
            } else {
                Append(ch);
            }
        }

        return LineResult::None;
    }

    // The most recent complete line, NUL-terminated, without "\r\n".
    // Valid until the next call to Poll().
    const char* Line() const { return line_; }
    std::size_t Length() const { return length_; }

private:
    bool Append(char ch) {
        if (length_ == MaxLen) {
            discarding_ = true;
            return false;
        }
        line_[length_++] = ch;
        return true;
    }

    char line_[MaxLen + 1] = {};
    std::size_t length_ = 0;
    bool pending_cr_ = false;
    bool discarding_ = false;
    bool reset_on_poll_ = false;
};
