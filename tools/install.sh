#!/bin/sh
# Set up this checkout: a uv venv with Pillow, the JieLi toolchain, the AC79 SDK. Idempotent. Then build + host test.
set -e
cd "$(dirname "$0")/.."

[ -d .venv ] || uv venv                      # (uv venv on an existing .venv only complains)
uv pip install --python .venv/bin/python Pillow mido python-rtmidi

[ -x "$HOME/.jieli/toolchain/pi32v2/bin/clang" ] || tools/get_toolchain.sh   # JieLi's clang for the chip
[ -f "$HOME/fw-AC79_AIoT_SDK/cpu/wl82/tools/uboot.boot" ] || \
    git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$HOME/fw-AC79_AIoT_SDK"

./build.sh                                   # -> build/dinghy.fwsc
tests/run_host.sh                            # -> build/host/dinghy.wav and screen PNGs, no FM-1 needed
