#!/bin/sh
# Build the firmware -> build/vavewave.fwsc. Extra arguments go to build.sh (e.g. --release 0.2).
set -e
cd "$(dirname "$0")/.."
./build.sh "$@"
ls -l build/vavewave.fwsc
