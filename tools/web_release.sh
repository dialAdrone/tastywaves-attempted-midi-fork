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
P=$(sed -n 's/^PRODUCT = "\(FM-1_[0-9]*\)".*/\1/p' tools/build.py)      # the identity the package carries
printf '{ "version": "%s", "product": "%s", "pkg": "firmware/tastywaves.fwsc" }\n' "$V" "$P" > tools/web/firmware/release.json
cat tools/web/firmware/release.json
