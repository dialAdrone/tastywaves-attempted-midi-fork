#!/bin/sh
# Serve the browser tool (wavetables + patch library) and open it. Use Chrome or Edge (Web MIDI).
#   tools/web_tool.sh [port]     default 8000; Ctrl-C stops it
set -e
cd "$(dirname "$0")/.."
PORT="${1:-8000}"
PY=.venv/bin/python
[ -x "$PY" ] || PY=python3
URL="http://localhost:$PORT"
echo "VAVEWave web tool at $URL (Chrome or Edge; press Connect to MIDI and allow SysEx)"
( sleep 1; open "$URL" 2>/dev/null || xdg-open "$URL" 2>/dev/null || true ) &
exec "$PY" -m http.server "$PORT" --bind 127.0.0.1 --directory tools/web
