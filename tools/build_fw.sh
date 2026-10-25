#!/bin/sh
# Build the firmware -> build/tastywaves.fwsc. Extra arguments go to build.sh (e.g. --release 0.2).
set -e
cd "$(dirname "$0")/.."
./build.sh "$@"
ls -l build/tastywaves.fwsc
