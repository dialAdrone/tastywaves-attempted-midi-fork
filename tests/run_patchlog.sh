#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Host tests of the patch log (firmware/src/patchlog.h): NOR mock, fault injection, a power cut at every flash operation.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -Ifirmware/src -o build/host/patchlog_check tests/patchlog_check.c
build/host/patchlog_check
