#!/usr/bin/env bash
# rsync the gateway binary to the RPi and restart the systemd service.
#
# Environment variables (or edit defaults below):
#   RPI_HOST  — hostname or IP   (default: raspberrypi.local)
#   RPI_USER  — SSH username     (default: pi)
#   BUILD_DIR — cross-build dir  (default: build-arm)
set -euo pipefail

RPI_HOST="${RPI_HOST:-raspberrypi.local}"
RPI_USER="${RPI_USER:-pi}"
BUILD_DIR="${BUILD_DIR:-build-arm}"
REMOTE="${RPI_USER}@${RPI_HOST}"
BINARY="${BUILD_DIR}/vibmon-gateway"

if [[ ! -f "$BINARY" ]]; then
    echo "Binary not found: $BINARY"
    echo "Build first: cmake -B $BUILD_DIR -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake && cmake --build $BUILD_DIR -j\$(nproc)"
    exit 1
fi

echo "Deploying to ${REMOTE} …"
rsync -avz --progress "$BINARY" "${REMOTE}:/usr/local/bin/vibmon-gateway"
ssh "$REMOTE" "sudo systemctl restart vibmon-gateway && sudo systemctl is-active vibmon-gateway"
echo "Done. Logs: journalctl -fu vibmon-gateway"
