#pragma once
#include <cstdint>
#include <cstring>

namespace vibmon {

constexpr uint8_t FRAME_MAGIC    = 0xAA;
constexpr uint8_t MSG_IMU_DATA   = 0x01;
constexpr uint8_t MSG_HEARTBEAT  = 0x02;
constexpr uint8_t MSG_CONFIG_ACK = 0x03;
constexpr uint8_t MSG_FW_CHUNK   = 0x04;

constexpr int   SAMPLE_RATE_HZ = 1000;
constexpr float ACCEL_SCALE    = 1.0f / 4096.0f; // LSB → g  (±8 g range)
constexpr float GYRO_SCALE     = 1.0f / 65.5f;   // LSB → °/s (±500 °/s range)

#pragma pack(push, 1)
struct ImuRaw {
    int16_t ax, ay, az; // accelerometer, LSB
    int16_t gx, gy, gz; // gyroscope, LSB
};
#pragma pack(pop)
static_assert(sizeof(ImuRaw) == 12);

struct ImuSample {
    float   ax, ay, az; // g
    float   gx, gy, gz; // °/s
    int64_t ts_ms;      // wall-clock ms since epoch
};

inline ImuSample scale(const ImuRaw& r, int64_t ts_ms) noexcept {
    return {r.ax * ACCEL_SCALE,
            r.ay * ACCEL_SCALE,
            r.az * ACCEL_SCALE,
            r.gx * GYRO_SCALE,
            r.gy * GYRO_SCALE,
            r.gz * GYRO_SCALE,
            ts_ms};
}

} // namespace vibmon