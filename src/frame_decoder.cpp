#include "frame_decoder.hpp"

#include <chrono>
#include <cstring>

#include "cobs.hpp"
#include "crc16.hpp"

namespace vibmon {

// Frame layout (pre-COBS):
//   [0xAA][len:u16 LE][type:u8][payload...][crc16:u16 LE]
// CRC covers [type | payload] only.

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void FrameDecoder::push(uint8_t byte) {
    if (byte != 0x00) {
        cobs_buf_.push_back(byte);
        return;
    }

    // 0x00 == frame delimiter: try to decode whatever we accumulated
    if (!cobs_buf_.empty()) {
        process_frame(cobs_buf_);
        cobs_buf_.clear();
    }
}

void FrameDecoder::dispatch_decoded(const uint8_t* d, size_t len) {
    // Minimum: magic(1) + len(2) + type(1) + crc(2) = 6 bytes
    if (len < 6 || d[0] != FRAME_MAGIC) {
        ++frames_error_;
        return;
    }

    const uint16_t payload_len = static_cast<uint16_t>(d[1]) | (static_cast<uint16_t>(d[2]) << 8);
    const uint8_t  msg_type    = d[3];
    const std::size_t expected = 1 + 2 + 1 + payload_len + 2;
    if (len != expected) {
        ++frames_error_;
        return;
    }

    const uint16_t rx_crc =
        static_cast<uint16_t>(d[len - 2]) | (static_cast<uint16_t>(d[len - 1]) << 8);
    const uint16_t calc_crc = crc16(std::span<const uint8_t>(d + 3, 1 + payload_len));
    if (rx_crc != calc_crc) {
        ++frames_error_;
        return;
    }

    if (msg_type == MSG_IMU_DATA) {
        if (payload_len != sizeof(ImuRaw)) {
            ++frames_error_;
            return;
        }
        ImuRaw raw{};
        std::memcpy(&raw, d + 4, sizeof(ImuRaw));
        on_sample_(scale(raw, now_ms()));
        ++frames_ok_;
    }
    // HEARTBEAT / CONFIG_ACK / FW_CHUNK: acknowledged but not acted on here
}

void FrameDecoder::process_frame(const std::vector<uint8_t>& raw_cobs) {
    auto decoded = cobs_decode(raw_cobs);
    if (!decoded) {
        ++frames_error_;
        return;
    }
    dispatch_decoded(decoded->data(), decoded->size());
}

void FrameDecoder::push_frame(const uint8_t* data, size_t len) {
    dispatch_decoded(data, len);
}

} // namespace vibmon
