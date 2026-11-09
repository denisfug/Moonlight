#!/usr/bin/env bash
# Serves tests/fixtures/addon over HTTP on 127.0.0.1 and runs the source tests against it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-8765}"
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$ROOT/tests/fixtures/addon" >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT
sleep 1
kill -0 $SRV 2>/dev/null || { echo "test server did not start (port $PORT busy?)"; exit 1; }
ML_ADDON_URL="http://127.0.0.1:$PORT" ML_FIXTURES="$ROOT/tests/fixtures" "$ROOT/build/moonlight_tests" sources_
