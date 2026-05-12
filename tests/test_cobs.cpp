#include <catch2/catch_test_macros.hpp>
#include "cobs.hpp"

using namespace vibmon;

static std::vector<uint8_t> v(std::initializer_list<uint8_t> il) { return il; }

TEST_CASE("COBS round-trip — empty input") {
    auto enc = cobs_encode({});
    REQUIRE(enc == v({0x01}));
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(dec->empty());
}

TEST_CASE("COBS round-trip — no zeros") {
    const auto in = v({0x11, 0x22, 0x33});
    auto enc = cobs_encode(in);
    REQUIRE(enc[0] == 0x04); // overhead byte: 3 data + 1
    for (auto b : enc) REQUIRE(b != 0x00);

    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}

TEST_CASE("COBS round-trip — single zero") {
    const auto in = v({0x00});
    auto enc = cobs_encode(in);
    for (auto b : enc) REQUIRE(b != 0x00);
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}

TEST_CASE("COBS round-trip — leading zero") {
    const auto in = v({0x00, 0x11, 0x22});
    auto enc = cobs_encode(in);
    for (auto b : enc) REQUIRE(b != 0x00);
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}

TEST_CASE("COBS round-trip — trailing zero") {
    const auto in = v({0x11, 0x22, 0x00});
    auto enc = cobs_encode(in);
    for (auto b : enc) REQUIRE(b != 0x00);
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}

TEST_CASE("COBS round-trip — consecutive zeros") {
    const auto in = v({0x00, 0x00, 0x00});
    auto enc = cobs_encode(in);
    for (auto b : enc) REQUIRE(b != 0x00);
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}

TEST_CASE("COBS round-trip — realistic IMU frame (zeros in len/crc)") {
    // magic + len(12,0) + type(1) + 12 payload bytes + crc(0xAB,0x00)
    const auto in = v({0xAA, 0x0C, 0x00, 0x01,
                        0x01,0x00,0x02,0x00,0x03,0x00,
                        0x04,0x00,0x05,0x00,0x06,0x00,
                        0xAB, 0x00});
    auto enc = cobs_encode(in);
    for (auto b : enc) REQUIRE(b != 0x00);
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}

TEST_CASE("COBS decode — embedded 0x00 is an error") {
    auto bad = v({0x02, 0x00, 0x01}); // 0x00 inside frame is invalid
    REQUIRE_FALSE(cobs_decode(bad).has_value());
}

TEST_CASE("COBS decode — truncated frame is an error") {
    auto bad = v({0x05, 0x01, 0x02}); // code says 4 data bytes but only 2 follow
    REQUIRE_FALSE(cobs_decode(bad).has_value());
}

TEST_CASE("COBS round-trip — 254-byte block (max without split)") {
    std::vector<uint8_t> in(253, 0x42); // 253 non-zero bytes → one full block
    auto enc = cobs_encode(in);
    for (auto b : enc) REQUIRE(b != 0x00);
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}

TEST_CASE("COBS round-trip — 255-byte block boundary") {
    // 254 non-zero bytes forces code=0xFF (no implicit zero follows)
    std::vector<uint8_t> in(254, 0x42);
    auto enc = cobs_encode(in);
    for (auto b : enc) REQUIRE(b != 0x00);
    REQUIRE(enc[0] == 0xFF);
    auto dec = cobs_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == in);
}
