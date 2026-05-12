#!/usr/bin/env python3
"""
IMU frame simulator — sends valid COBS-framed IMU_DATA packets over a PTY.

Usage:
    python3 simulator.py /dev/pts/N   # write to a specific PTY slave
    python3 simulator.py              # allocate a PTY pair and print the slave path

The script emulates a 1 kHz LSM6DSO32 stream: sinusoidal accelerometer on Z
(gravity + 10 Hz vibration), low gyro noise. Heartbeats are sent every second.
"""

import os
import pty
import struct
import sys
import time
import math

# ── Protocol constants ─────────────────────────────────────────────────────────
FRAME_MAGIC    = 0xAA
MSG_IMU_DATA   = 0x01
MSG_HEARTBEAT  = 0x02

ACCEL_SCALE    = 4096   # LSB/g  (±8 g range)
GYRO_SCALE     = 65     # LSB/°s (±500 °/s range, integer approx)


# ── CRC16-CCITT-FALSE ─────────────────────────────────────────────────────────
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# ── COBS encode ───────────────────────────────────────────────────────────────
def cobs_encode(data: bytes) -> bytes:
    out = bytearray()
    overhead = 0
    out.append(0x00)   # placeholder
    code = 1

    for b in data:
        if b != 0:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[overhead] = code
                overhead = len(out)
                out.append(0x00)
                code = 1
        else:
            out[overhead] = code
            overhead = len(out)
            out.append(0x00)
            code = 1

    out[overhead] = code
    return bytes(out)


# ── Frame builder ─────────────────────────────────────────────────────────────
def build_imu_frame(ax: int, ay: int, az: int,
                    gx: int, gy: int, gz: int) -> bytes:
    payload = struct.pack('<hhhhhh', ax, ay, az, gx, gy, gz)
    msg_type = MSG_IMU_DATA
    header   = struct.pack('<BHB', FRAME_MAGIC, len(payload), msg_type)
    checksum = struct.pack('<H', crc16(bytes([msg_type]) + payload))
    raw      = header + payload + checksum
    return cobs_encode(raw) + b'\x00'   # append delimiter


def build_heartbeat_frame() -> bytes:
    msg_type = MSG_HEARTBEAT
    header   = struct.pack('<BHB', FRAME_MAGIC, 0, msg_type)
    checksum = struct.pack('<H', crc16(bytes([msg_type])))
    return cobs_encode(header + checksum) + b'\x00'


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    if len(sys.argv) > 1:
        fd = os.open(sys.argv[1], os.O_WRONLY)
        slave_path = sys.argv[1]
    else:
        master_fd, slave_fd = pty.openpty()
        slave_path = os.ttyname(slave_fd)
        print(f"PTY slave: {slave_path}", flush=True)
        fd = master_fd

    print(f"Streaming to {slave_path} at 1 kHz …  (Ctrl-C to stop)", flush=True)

    t0        = time.monotonic()
    n         = 0
    last_hb   = 0
    RATE      = 1000         # Hz
    INTERVAL  = 1.0 / RATE

    while True:
        t = time.monotonic() - t0

        # Gravity on Z (~9.81 g * 4096 LSB/g) + 10 Hz vibration + white noise
        az_g   = 1.0 + 0.05 * math.sin(2 * math.pi * 10 * t)
        ax_g   = 0.002 * math.sin(2 * math.pi * 1.3 * t)
        ay_g   = 0.003 * math.cos(2 * math.pi * 2.7 * t)

        ax = int(ax_g * ACCEL_SCALE)
        ay = int(ay_g * ACCEL_SCALE)
        az = int(az_g * ACCEL_SCALE)
        gx = int(0.5 * math.sin(2 * math.pi * 0.5 * t) * GYRO_SCALE)
        gy = int(0.3 * math.cos(2 * math.pi * 0.7 * t) * GYRO_SCALE)
        gz = 0

        frame = build_imu_frame(ax, ay, az, gx, gy, gz)
        try:
            os.write(fd, frame)
        except OSError:
            break

        n += 1
        if int(t) > last_hb:
            last_hb = int(t)
            try:
                os.write(fd, build_heartbeat_frame())
            except OSError:
                break
            print(f"[{t:.1f}s] {n} frames sent", flush=True)

        # Pace to RATE Hz (best-effort; Python sleep is not RT)
        next_t = t0 + n * INTERVAL
        sleep  = next_t - time.monotonic()
        if sleep > 0:
            time.sleep(sleep)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\nsimulator stopped")
