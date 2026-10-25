#!/bin/sh
# Put a release build into the web tool, for its Install button (and GitHub Pages):
#   tools/web_release.sh 1.0.0      build/tastywaves-1.0.0.fwsc -> tools/web/firmware/tastywaves.fwsc + release.json
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/web_release.sh X.Y.Z}"
F="build/tastywaves-$V.fwsc"
[ -f "$F" ] || { echo "web_release.sh: no $F (./build.sh --release $V)"; exit 1; }
mkdir -p tools/web/firmware
cp "$F" tools/web/firmware/tastywaves.fwsc
MINOR=$(echo "$V" | cut -d. -f2)
printf '{ "version": "%s", "product": "FM-1_95%s", "pkg": "firmware/tastywaves.fwsc" }\n' "$V" "$MINOR" > tools/web/firmware/release.json
cat tools/web/firmware/release.json
