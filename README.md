# vibmon-gateway

> Linux edge gateway for real-time IMU vibration monitoring. Receives
> COBS-framed sensor data from a Zephyr RTOS node over UART, computes
> FFT vibration spectrum, persists samples to SQLite, and serves
> a live WebSocket dashboard.

Part of a **Linux-RTOS hybrid vibration monitoring system**

---

## System Overview

```
┌──────────────────────────────┐  UART   ┌─────────────────────────────────┐
│  STM32 Nucleo F411RE         │────────▶│  Raspberry Pi 5                 │
│  Zephyr RTOS                 │  1kHz   │                                 │
│  LSM6DSO32 IMU (accel+gyro)  │  frames │  COBS decoder                   │
└──────────────────────────────┘         │  FFT                            │
                                         │  SQLite (24h rolling window)    │
                                         │  HTTP + WebSocket (:8080)       │
                                         │  systemd + watchdog             │
                                         │  /dev/vibmon0 (kernel mod)      │
                                         └─────────────┬───────────────────┘
                                                       │ :8080
                                         ┌─────────────▼───────────────────┐
                                         │  Browser                        │
                                         │  Live FFT spectrum              │
                                         │  XYZ accelerometer waveform     │
                                         └─────────────────────────────────┘
             
```

---

## License

MIT — see [LICENSE](./LICENSE).
