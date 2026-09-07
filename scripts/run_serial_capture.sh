#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-115200}"
DURATION="${3:-9000}"
RETRIES="${4:-3}"

mkdir -p tmp_capture
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="tmp_capture/serial_${STAMP}.log"

echo "Capturing serial to ${OUT} for ${DURATION}s (port=${PORT} baud=${BAUD})"
attempt=1
while true; do
  if python3 scripts/serial_monitor.py --port "${PORT}" --baud "${BAUD}" --duration "${DURATION}" --reset | tee "${OUT}"; then
    break
  fi
  if [ "${attempt}" -ge "${RETRIES}" ]; then
    echo "Serial capture failed after ${RETRIES} attempts"
    exit 1
  fi
  attempt=$((attempt + 1))
  echo "Retrying serial capture (${attempt}/${RETRIES})..."
  sleep 2
done
