#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "protocol.hpp"

namespace vibmon {

// COBS frame decoder state machine.
//
// Feed bytes one at a time via push(). On each complete, valid frame the
// on_sample callback is invoked. Malformed frames (bad magic, wrong length,
// CRC mismatch) are silently dropped and counted as errors.
class FrameDecoder {
  public:
    using SampleCallback = std::function<void(const ImuSample&)>;

    explicit FrameDecoder(SampleCallback cb) : on_sample_(std::move(cb)) {}

    // Feed one byte from a raw COBS stream (TTY / PTY path).
    void push(uint8_t byte);

    // Feed one already-decoded frame directly (/dev/vibmon0 char device path).
    // The kmod has already done COBS decode and CRC validation; this method
    // re-validates (belt-and-suspenders) and dispatches to on_sample_.
    void push_frame(const uint8_t* data, size_t len);

    uint64_t frames_ok() const noexcept { return frames_ok_; }
    uint64_t frames_error() const noexcept { return frames_error_; }

  private:
    void process_frame(const std::vector<uint8_t>& raw_cobs);
    void dispatch_decoded(const uint8_t* d, size_t len);

    SampleCallback       on_sample_;
    std::vector<uint8_t> cobs_buf_; // accumulates bytes between 0x00 delimiters
    uint64_t             frames_ok_    = 0;
    uint64_t             frames_error_ = 0;
};

} // namespace vibmon
