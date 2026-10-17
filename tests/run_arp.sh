#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The arpeggiator engine alone (firmware/src/arp.h) with UBSan/ASan: modes, timing, gate, swing, clock, no hung notes.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -o build/host/arp_check tests/arp_check.c
build/host/arp_check
