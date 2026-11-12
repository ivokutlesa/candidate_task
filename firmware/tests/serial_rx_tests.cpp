#include "line_assembler.h"
#include "ring_buffer.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using TestFn = void (*)();

static void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <std::size_t N>
static void push_text(RingBuffer<N>& rx, const char* text) {
    for (const char* p = text; *p; ++p) {
        expect(rx.Push(static_cast<std::uint8_t>(*p)), "Test setup: RingBuffer::Push failed (implement B1 first)");
    }
}

template <std::size_t MaxLen, std::size_t N>
static void expect_line(LineAssembler<MaxLen>& a, RingBuffer<N>& rx, const char* text) {
    expect(a.Poll(rx) == LineResult::Line, std::string("Expected line '") + text + "'");
    expect(a.Length() == std::strlen(text), std::string("Wrong length for '") + text + "'");
    expect(std::strcmp(a.Line(), text) == 0, std::string("Wrong content, expected '") + text + "'");
}

// ---- RingBuffer ----

static void ring_starts_empty() {
    RingBuffer<4> rb;
    std::uint8_t b = 0;
    expect(rb.Empty() && rb.Size() == 0, "New buffer must be empty");
    expect(!rb.Pop(b), "Pop on empty buffer must fail");
}

static void ring_is_fifo() {
    RingBuffer<4> rb;
    expect(rb.Push(1) && rb.Push(2) && rb.Push(3), "Push must succeed");
    expect(rb.Size() == 3, "Size must be 3");
    std::uint8_t b = 0;
    expect(rb.Pop(b) && b == 1, "First byte out must be 1");
    expect(rb.Pop(b) && b == 2, "Second byte out must be 2");
    expect(rb.Pop(b) && b == 3, "Third byte out must be 3");
    expect(rb.Empty(), "Buffer must be empty again");
}

static void ring_holds_exactly_capacity() {
    RingBuffer<4> rb;
    for (std::uint8_t i = 0; i < 4; ++i) expect(rb.Push(i), "Buffer must hold N bytes");
    expect(rb.Full() && rb.Size() == 4, "Buffer must report full");
    expect(!rb.Push(99), "Push on full buffer must fail");
    expect(rb.Overflows() == 1, "Rejected byte must be counted");
    std::uint8_t b = 0;
    for (std::uint8_t i = 0; i < 4; ++i) {
        expect(rb.Pop(b) && b == i, "Overflow must not overwrite unread data");
    }
}

static void ring_wraps_around() {
    RingBuffer<4> rb;
    std::uint8_t next_in = 0;
    std::uint8_t next_out = 0;
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (int i = 0; i < 3; ++i) expect(rb.Push(next_in++), "Push failed during wrap test");
        std::uint8_t b = 0;
        for (int i = 0; i < 3; ++i) {
            expect(rb.Pop(b) && b == next_out++, "Wrong byte after wrap-around");
        }
    }
    expect(rb.Empty() && rb.Overflows() == 0, "Buffer must be empty with no overflows");
}

static void ring_single_producer_single_consumer() {
    // Producer thread stands in for the UART ISR, consumer for the main loop.
    constexpr std::uint32_t total = 200000;
    RingBuffer<16> rb;
    std::atomic<bool> give_up{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    std::thread producer([&] {
        for (std::uint32_t i = 0; i < total && !give_up; ) {
            if (rb.Push(static_cast<std::uint8_t>(i))) ++i;
        }
    });
    std::uint32_t received = 0;
    bool in_order = true;
    while (received < total && std::chrono::steady_clock::now() < deadline) {
        std::uint8_t b = 0;
        if (rb.Pop(b)) {
            if (b != static_cast<std::uint8_t>(received)) in_order = false;
            ++received;
        }
    }
    give_up = true;
    producer.join();
    expect(received == total, "Consumer did not receive every byte within 5 s");
    expect(in_order, "Bytes were lost, duplicated or reordered under concurrency");
}

// ---- LineAssembler ----

static void line_simple() {
    RingBuffer<64> rx;
    LineAssembler<8> a;
    push_text(rx, "PING\n");
    expect_line(a, rx, "PING");
    expect(a.Poll(rx) == LineResult::None, "No further line expected");
}

static void line_strips_crlf() {
    RingBuffer<64> rx;
    LineAssembler<8> a;
    push_text(rx, "PING\r\n");
    expect_line(a, rx, "PING");
}

static void line_survives_fragmentation() {
    RingBuffer<64> rx;
    LineAssembler<8> a;
    push_text(rx, "PI");
    expect(a.Poll(rx) == LineResult::None, "Partial line must not be returned");
    push_text(rx, "NG\r");
    expect(a.Poll(rx) == LineResult::None, "Partial line must not be returned");
    push_text(rx, "\n");
    expect_line(a, rx, "PING");
}

static void line_one_per_poll() {
    RingBuffer<64> rx;
    LineAssembler<8> a;
    push_text(rx, "A\nB\n");
    expect_line(a, rx, "A");
    expect(rx.Size() == 2, "Poll must stop right after the first newline");
    expect_line(a, rx, "B");
    expect(a.Poll(rx) == LineResult::None, "No further line expected");
}

static void line_empty_and_interior_cr() {
    RingBuffer<64> rx;
    LineAssembler<8> a;
    push_text(rx, "\nA\rB\n");
    expect_line(a, rx, "");
    expect_line(a, rx, "A\rB");
}

static void line_exact_max_length() {
    RingBuffer<64> rx;
    LineAssembler<8> a;
    push_text(rx, "12345678\n12345678\r\n");
    expect_line(a, rx, "12345678");
    expect_line(a, rx, "12345678");
}

static void line_too_long_is_discarded() {
    RingBuffer<64> rx;
    LineAssembler<8> a;
    push_text(rx, "123456789");
    expect(a.Poll(rx) == LineResult::None, "Over-long line is not finished yet");
    push_text(rx, "0ABC\nOK\n");
    expect(a.Poll(rx) == LineResult::TooLong, "Over-long line must be reported once");
    expect_line(a, rx, "OK");
}

int main() {
    std::vector<std::pair<std::string, TestFn>> tests = {
        {"ring_starts_empty", ring_starts_empty},
        {"ring_is_fifo", ring_is_fifo},
        {"ring_holds_exactly_capacity", ring_holds_exactly_capacity},
        {"ring_wraps_around", ring_wraps_around},
        {"ring_single_producer_single_consumer", ring_single_producer_single_consumer},
        {"line_simple", line_simple},
        {"line_strips_crlf", line_strips_crlf},
        {"line_survives_fragmentation", line_survives_fragmentation},
        {"line_one_per_poll", line_one_per_poll},
        {"line_empty_and_interior_cr", line_empty_and_interior_cr},
        {"line_exact_max_length", line_exact_max_length},
        {"line_too_long_is_discarded", line_too_long_is_discarded},
    };
    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& e) {
            std::cerr << "FAIL " << test.first << ": " << e.what() << '\n';
            ++failed;
        }
    }
    std::cout << (tests.size() - failed) << "/" << tests.size() << " passed\n";
    return failed == 0 ? 0 : 1;
}
