#!/usr/bin/env bash
# Start the Python PTY simulator + gateway in a single command for development.
#
# Usage: ./scripts/run_with_simulator.sh [build-dir]
set -euo pipefail

BUILD="${1:-build}"
GATEWAY="${BUILD}/vibmon-gateway"
SIMULATOR="$(dirname "$0")/simulator.py"

if [[ ! -x "$GATEWAY" ]]; then
    echo "Gateway binary not found: $GATEWAY"
    echo "Run: cmake -B $BUILD && cmake --build $BUILD -j\$(nproc)"
    exit 1
fi

# Allocate a PTY pair and extract the slave path from the first output line
tmpout=$(mktemp)
python3 "$SIMULATOR" &> "$tmpout" &
SIM_PID=$!

# Wait until the slave path line appears
for i in $(seq 1 20); do
    SLAVE=$(grep -m1 '^PTY slave:' "$tmpout" 2>/dev/null | awk '{print $3}') && break
    sleep 0.1
done
rm -f "$tmpout"

if [[ -z "$SLAVE" ]]; then
    echo "Simulator did not report a PTY slave path" >&2
    kill "$SIM_PID" 2>/dev/null
    exit 1
fi

echo "Simulator PID $SIM_PID, gateway reading from $SLAVE"
echo "Dashboard: http://localhost:8080"
echo ""

cleanup() {
    kill "$SIM_PID" 2>/dev/null || true
    wait "$SIM_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

exec "$GATEWAY" "$SLAVE"
