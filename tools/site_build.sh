#!/bin/sh
# Assemble the website (tastysynth.com, GitHub Pages) into build/site: the landing page (site/), its pictures
# (docs/images) and the web tool at /tool/ (tools/web). With --serve, serve it at http://localhost:8000 too.
#   tools/site_build.sh [--serve [port]]
set -e
cd "$(dirname "$0")/.."
OUT=build/site
rm -rf "$OUT"; mkdir -p "$OUT"
cp -R site/. "$OUT/"
cp -R docs/images "$OUT/images"
cp -R tools/web "$OUT/tool"
rm -f "$OUT/tool/README.md"
echo "site in $OUT"
if [ "$1" = "--serve" ]; then
    PY=.venv/bin/python; [ -x "$PY" ] || PY=python3
    ( sleep 1; open "http://localhost:${2:-8000}" 2>/dev/null || true ) &
    exec "$PY" -I tools/web_serve.py "${2:-8000}" "$OUT"
fi
