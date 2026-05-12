#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace vibmon {

// Input may contain 0x00. Output is free of 0x00 bytes.
// Caller appends 0x00 as the frame delimiter.
inline std::vector<uint8_t> cobs_encode(std::span<const uint8_t> in) {
    std::vector<uint8_t> out;
    out.reserve(in.size() + in.size() / 254 + 2);

    std::size_t overhead = 0;
    out.push_back(0x00);
    uint8_t code = 0x01;

    for (uint8_t b : in) {
        if (b != 0x00) {
            out.push_back(b);
            if (++code == 0xFF) {
                out[overhead] = code;
                overhead      = out.size();
                out.push_back(0x00);
                code = 0x01;
            }
        } else {
            out[overhead] = code;
            overhead      = out.size();
            out.push_back(0x00);
            code = 0x01;
        }
    }
    out[overhead] = code;
    return out;
}

// Input is a COBS frame without the trailing 0x00 delimiter.
// Returns decoded bytes, or nullopt if the encoding is invalid.
inline std::optional<std::vector<uint8_t>> cobs_decode(std::span<const uint8_t> in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());

    std::size_t i = 0;
    while (i < in.size()) {
        const uint8_t code = in[i++];
        if (code == 0x00)
            return std::nullopt; // 0x00 must never appear inside a COBS frame

        const std::size_t n = static_cast<std::size_t>(code - 1);
        if (i + n > in.size())
            return std::nullopt;

        for (std::size_t j = 0; j < n; ++j) {
            if (in[i] == 0x00)
                return std::nullopt;
            out.push_back(in[i++]);
        }

        // code < 0xFF means this block ended with an implicit zero byte,
        // but only emit it if there is more data (not after the last block).
        if (code < 0xFF && i < in.size())
            out.push_back(0x00);
    }
    return out;
}

} // namespace vibmon