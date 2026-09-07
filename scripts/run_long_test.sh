#!/usr/bin/env bash
set -euo pipefail

BASE="${BASE:-http://100.76.149.200}"
PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-115200}"
COUNT="${COUNT:-5}"
DURATION="${DURATION:-1800}"
CLEARING="${CLEARING:-6}"
SPEEDS="${SPEEDS:-1,2,3,4,5,6,7,8,9,10}"
SPEED_INTERVAL="${SPEED_INTERVAL:-}"

TOTAL_DURATION=$((COUNT * DURATION + 60))

mkdir -p tmp_capture
STAMP="$(date +%Y%m%d_%H%M%S)"
PATTERN_LOG="tmp_capture/pattern_run_${STAMP}.jsonl"

if pgrep -x tio >/dev/null 2>&1; then
  echo "Stopping existing tio..."
  pkill -x tio
  sleep 1
fi

echo "Starting serial underrun watch in background..."
scripts/run_serial_underrun_watch.sh "${PORT}" "${BAUD}" "${TOTAL_DURATION}" &
SERIAL_PID=$!

echo "Running long pattern test (count=${COUNT}, duration=${DURATION}s)..."
ARGS=(
  --base "${BASE}"
  --count "${COUNT}"
  --duration "${DURATION}"
  --clearing "${CLEARING}"
  --speeds "${SPEEDS}"
  --log "${PATTERN_LOG}"
)
if [[ -n "${SPEED_INTERVAL}" ]]; then
  ARGS+=(--speed-interval "${SPEED_INTERVAL}")
fi
python3 scripts/run_long_patterns.py "${ARGS[@]}"

echo "Waiting for serial capture to finish..."
wait "${SERIAL_PID}"

echo "Logs:"
echo "  ${PATTERN_LOG}"
echo "  tmp_capture/serial_${STAMP}.log"
