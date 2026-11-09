#!/usr/bin/env bash
# Serves the addon and radio fixtures over HTTP on 127.0.0.1 and runs the network tests against them.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-8765}"
RPORT=$((PORT + 1))
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$ROOT/tests/fixtures/addon" >/dev/null 2>&1 &
SRV=$!
python3 -m http.server "$RPORT" --bind 127.0.0.1 --directory "$ROOT/tests/fixtures/radio" >/dev/null 2>&1 &
RSRV=$!
trap 'kill $SRV $RSRV 2>/dev/null || true' EXIT
sleep 1
kill -0 $SRV 2>/dev/null && kill -0 $RSRV 2>/dev/null || { echo "test servers did not start (ports $PORT/$RPORT busy?)"; exit 1; }
export ML_ADDON_URL="http://127.0.0.1:$PORT" ML_RADIO_URL="http://127.0.0.1:$RPORT" ML_FIXTURES="$ROOT/tests/fixtures"
"$ROOT/build/moonlight_tests" _live_
