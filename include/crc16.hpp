#pragma once
#include <cstdint>
#include <span>

namespace vibmon {

// CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, no input/output reflection.
// Computed over [type | payload] (excludes magic and len fields).
inline uint16_t crc16(std::span<const uint8_t> data) noexcept {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : data) {
        crc ^= static_cast<uint16_t>(b) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                                  : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

} // namespace vibmon