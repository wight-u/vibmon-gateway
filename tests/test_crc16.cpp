#include <catch2/catch_test_macros.hpp>
#include "crc16.hpp"
#include "protocol.hpp"

using namespace vibmon;

TEST_CASE("CRC16-CCITT — empty input") {
    REQUIRE(crc16({}) == 0xFFFF);
}

TEST_CASE("CRC16-CCITT — known vector: \"123456789\"") {
    // Standard check value for CRC-16/CCITT-FALSE
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    REQUIRE(crc16(data) == 0x29B1);
}

TEST_CASE("CRC16-CCITT — single zero byte") {
    const uint8_t data[] = {0x00};
    REQUIRE(crc16(data) == 0xE1F0);
}

TEST_CASE("CRC16-CCITT — single 0xFF byte") {
    const uint8_t data[] = {0xFF};
    REQUIRE(crc16(data) == 0xFF00);
}

TEST_CASE("CRC16-CCITT — IMU MSG_TYPE + 12 payload bytes") {
    // Synthesise a type=0x01 frame with all-zero payload; verify is deterministic
    std::vector<uint8_t> buf(13, 0x00);
    buf[0] = vibmon::MSG_IMU_DATA;
    const uint16_t crc1 = crc16(buf);
    const uint16_t crc2 = crc16(buf);
    REQUIRE(crc1 == crc2);
    REQUIRE(crc1 != 0x0000); // non-trivial output for non-trivial input
}

TEST_CASE("CRC16-CCITT — different payloads produce different CRCs") {
    const uint8_t a[] = {0x01, 0x00, 0x00};
    const uint8_t b[] = {0x01, 0x00, 0x01};
    REQUIRE(crc16(a) != crc16(b));
}
