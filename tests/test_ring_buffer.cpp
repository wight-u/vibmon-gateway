#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>
#include <vector>
#include "ring_buffer.hpp"
#include "protocol.hpp"

using namespace vibmon;

TEST_CASE("RingBuffer — empty buffer has write_seq 0") {
    RingBuffer<int, 8> rb;
    REQUIRE(rb.write_seq() == 0);
}

TEST_CASE("RingBuffer — peek_last returns nothing from empty buffer") {
    RingBuffer<int, 8> rb;
    int out[4];
    REQUIRE(rb.peek_last(out, 4) == 0);
}

TEST_CASE("RingBuffer — peek_last returns correct samples in order") {
    RingBuffer<int, 8> rb;
    for (int i = 1; i <= 5; ++i) rb.push(i);

    int out[5];
    std::size_t n = rb.peek_last(out, 5);
    REQUIRE(n == 5);
    for (int i = 0; i < 5; ++i) REQUIRE(out[i] == i + 1);
}

TEST_CASE("RingBuffer — peek_last clips to available count") {
    RingBuffer<int, 8> rb;
    rb.push(10); rb.push(20);

    int out[8];
    REQUIRE(rb.peek_last(out, 8) == 2);
    REQUIRE(out[0] == 10);
    REQUIRE(out[1] == 20);
}

TEST_CASE("RingBuffer — overwrite: only last N samples visible") {
    RingBuffer<int, 4> rb; // N=4
    for (int i = 1; i <= 7; ++i) rb.push(i); // 7 > N, wraps around

    int out[4];
    std::size_t n = rb.peek_last(out, 4);
    REQUIRE(n == 4);
    REQUIRE(out[0] == 4);
    REQUIRE(out[1] == 5);
    REQUIRE(out[2] == 6);
    REQUIRE(out[3] == 7);
}

TEST_CASE("RingBuffer — drain: basic consumption") {
    RingBuffer<int, 8> rb;
    for (int i = 1; i <= 4; ++i) rb.push(i);

    uint64_t seq = 0;
    int out[4];
    std::size_t n = rb.drain(seq, out, 4);
    REQUIRE(n == 4);
    REQUIRE(seq == 4);
    REQUIRE(out[0] == 1);
    REQUIRE(out[3] == 4);

    // No more data
    REQUIRE(rb.drain(seq, out, 4) == 0);
}

TEST_CASE("RingBuffer — drain: incremental consumption") {
    RingBuffer<int, 8> rb;
    for (int i = 0; i < 8; ++i) rb.push(i);

    uint64_t seq = 0;
    int out[4];

    REQUIRE(rb.drain(seq, out, 4) == 4);
    REQUIRE(out[0] == 0); REQUIRE(out[3] == 3);
    REQUIRE(seq == 4);

    REQUIRE(rb.drain(seq, out, 4) == 4);
    REQUIRE(out[0] == 4); REQUIRE(out[3] == 7);
    REQUIRE(seq == 8);
}

TEST_CASE("RingBuffer — drain: slow consumer fast-forwards past overwritten data") {
    RingBuffer<int, 4> rb; // N=4
    for (int i = 1; i <= 8; ++i) rb.push(i); // overwrite twice

    uint64_t seq = 0; // consumer never read anything
    int out[4];
    std::size_t n = rb.drain(seq, out, 4);
    REQUIRE(n == 4);
    // oldest available is head-N = 8-4 = 4 → values 5,6,7,8
    REQUIRE(out[0] == 5);
    REQUIRE(out[3] == 8);
}

TEST_CASE("RingBuffer — SPSC concurrent write and peek_last") {
    RingBuffer<int, 1024> rb;
    constexpr int N = 10000;

    std::atomic<bool> start{false};
    std::thread writer([&] {
        while (!start.load()) {}
        for (int i = 0; i < N; ++i) rb.push(i);
    });

    start.store(true);
    int out[64];
    // Just verify it doesn't crash or deadlock
    for (int t = 0; t < 100; ++t)
        rb.peek_last(out, 64);

    writer.join();
    REQUIRE(rb.write_seq() == N);
}
