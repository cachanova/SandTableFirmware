#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-115200}"
DURATION="${3:-9000}"

mkdir -p tmp_capture
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="tmp_capture/serial_underruns_${STAMP}.log"
FULL="tmp_capture/serial_full_${STAMP}.log"

echo "Watching serial for underruns to ${OUT} for ${DURATION}s (port=${PORT} baud=${BAUD})"
echo "Saving full serial log to ${FULL}"
stdbuf -oL python3 scripts/serial_monitor.py --port "${PORT}" --baud "${BAUD}" --duration "${DURATION}" --reset \
  | tee "${FULL}" \
  | stdbuf -oL rg --line-buffered -n -C 5 "Underrun|\\[MOTOR" \
  | tee "${OUT}"
