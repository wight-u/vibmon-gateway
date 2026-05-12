# vibmon Wire Protocol

## Frame structure (before COBS encoding)

```
┌────────┬──────────┬────────┬─────────────┬──────────────┐
│ magic  │  len     │  type  │   payload   │   CRC16      │
│ 1 byte │ 2 bytes  │ 1 byte │  len bytes  │  2 bytes     │
│ 0xAA   │ u16 LE   │        │             │  u16 LE      │
└────────┴──────────┴────────┴─────────────┴──────────────┘
```

- **magic** — always `0xAA`; used as a quick sanity check before CRC.
- **len** — payload length in bytes (little-endian u16).
- **type** — message type (see below).
- **payload** — message-specific data.
- **CRC16** — CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF, no reflection) computed over `[type | payload]`.

## COBS framing

The raw frame bytes (which may contain `0x00`) are COBS-encoded. After encoding no `0x00` byte appears in the output. A single `0x00` byte is appended as the frame delimiter:

```
[COBS(frame_bytes)] [0x00]
```

The receiver accumulates bytes until `0x00`, then COBS-decodes the chunk and validates the frame.

**Why COBS over other schemes?**  
- No escape sequences — every byte in the encoded stream is non-zero; framing is unambiguous with a single delimiter byte.  
- Overhead is at most 1 byte per 254 bytes (≈0.4% for 12-byte IMU frames).  
- No UART synchronisation issues: loss of one frame loses only that frame.

## Message types

| Value | Name         | Payload                        | Direction   |
|-------|--------------|--------------------------------|-------------|
| 0x01  | IMU_DATA     | 12 bytes (see below)           | MCU → Pi    |
| 0x02  | HEARTBEAT    | 0 bytes                        | MCU → Pi    |
| 0x03  | CONFIG_ACK   | 0 bytes (reserved)             | Pi  → MCU   |
| 0x04  | FW_CHUNK     | variable (OTA, reserved)       | Pi  → MCU   |

## IMU_DATA payload (12 bytes, little-endian)

```c
struct imu_sample {
    int16_t ax, ay, az;  // accelerometer, ±8 g,    4096 LSB/g
    int16_t gx, gy, gz;  // gyroscope,    ±500 °/s, 65.5 LSB/°/s
};
```

Conversion: `accel_g = raw / 4096.0`, `gyro_dps = raw / 65.5`.

## HEARTBEAT

Sent by the MCU at 1 Hz. No payload. If the Pi stops receiving heartbeats for > 3 s the gateway logs a warning.

## On-wire example — IMU_DATA frame

```
Raw (pre-COBS, 18 bytes):
  AA  0C 00  01  01 00 02 00 03 00 04 00 05 00 06 00  AB CD

  magic=AA, len=0x000C=12, type=01 (IMU_DATA)
  ax=0x0001 ay=0x0002 az=0x0003 gx=0x0004 gy=0x0005 gz=0x0006
  crc=0xCDAB (LE)

After COBS + delimiter: no 0x00 bytes in stream until final 0x00 delimiter.
```
