#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace vibmon {

// Lock-free single-producer / multi-reader ring buffer.
// Oldest entry is silently overwritten when full; slow consumers skip data.
// N must be a power of two.
template<typename T, std::size_t N> class RingBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");

    alignas(64) std::array<T, N> buf_{};             // separate cache lines:
    alignas(64) std::atomic<uint64_t> write_seq_{0}; // avoids false sharing

  public:
    void push(const T& val) noexcept {
        const uint64_t seq  = write_seq_.load(std::memory_order_relaxed);
        buf_[seq & (N - 1)] = val;
        write_seq_.store(seq + 1, std::memory_order_release); // release to consumers
    }

    uint64_t write_seq() const noexcept { return write_seq_.load(std::memory_order_acquire); }

    std::size_t peek_last(T* out, std::size_t count) const noexcept {
        const uint64_t head  = write_seq_.load(std::memory_order_acquire);
        const uint64_t avail = head < N ? head : N;
        if (count > avail)
            count = static_cast<std::size_t>(avail);
        const uint64_t start = head - count;
        for (std::size_t i = 0; i < count; ++i)
            out[i] = buf_[(start + i) & (N - 1)];
        return count;
    }

    std::size_t drain(uint64_t& consumer_seq, T* out, std::size_t max_count) const noexcept {
        const uint64_t head = write_seq_.load(std::memory_order_acquire);
        if (consumer_seq >= head)
            return 0;

        const uint64_t oldest = head > N ? head - N : 0;
        if (consumer_seq < oldest)
            consumer_seq = oldest;

        const uint64_t    available = head - consumer_seq;
        const std::size_t count =
            available < max_count ? static_cast<std::size_t>(available) : max_count;
        for (std::size_t i = 0; i < count; ++i)
            out[i] = buf_[(consumer_seq + i) & (N - 1)];
        consumer_seq += count;
        return count;
    }
};

} // namespace vibmon