# Building vibmon-gateway

## Prerequisites

| Tool       | Minimum | Notes                                      |
|------------|---------|--------------------------------------------|
| CMake      | 3.20    | CPM auto-downloads remaining deps          |
| GCC / Clang| C++20   | GCC 12+ or Clang 15+ recommended           |
| Internet   | —       | Required on first configure (CPM downloads)|

**Optional (faster if present as system packages):**

```bash
# openSUSE / SLES
sudo zypper install fftw3-devel sqlite3-devel libsystemd-devel openssl-devel

# Debian / Ubuntu
sudo apt install libfftw3-dev libsqlite3-dev libsystemd-dev libssl-dev
```

If absent, CMake/CPM builds them from source (adds ~1–2 min on first configure).

## Native build (x86-64, development)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure

# Run with simulator (no hardware needed)
./scripts/run_with_simulator.sh
```

## Cross-compile for Raspberry Pi 5 (aarch64)

Install the cross-compiler toolchain:

```bash
# Debian / Ubuntu
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# openSUSE
sudo zypper install cross-aarch64-gcc14 cross-aarch64-gcc14-c++
```

Build:

```bash
cmake -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm -j$(nproc)
```

Because CPM builds all dependencies from source, no sysroot is needed. The
toolchain file sets `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY` so CMake does
not accidentally pick up x86 system libraries.

If you **do** have an RPi sysroot (e.g. from Buildroot), add:

```bash
  -DCMAKE_SYSROOT=/path/to/sysroot
```

This lets `find_package` locate pre-built FFTW3 and SQLite3, saving build time.

## Deploy

```bash
# Set your RPi hostname/user if different from defaults
export RPI_HOST=raspberrypi.local
export RPI_USER=pi

./scripts/deploy.sh
```

The script rsyncs the binary and restarts `vibmon-gateway.service` over SSH.

## CPM cache

Set `CPM_SOURCE_CACHE` to share downloads across build-directory wipes:

```bash
export CPM_SOURCE_CACHE=~/.cache/cpm
cmake -B build ...
```

## Debug build with sanitisers

```bash
cmake -B build-dbg -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg -j$(nproc)
./build-dbg/vibmon-gateway /dev/pts/N   # ASAN + UBSAN enabled
```
