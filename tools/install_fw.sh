#!/bin/sh
# Install a firmware image on the connected FM-1 over USB (asks before it writes).
#   tools/install_fw.sh [file.fwsc]     default build/vavewave.fwsc
#   tools/install_fw.sh --info          show what runs on the FM-1
set -e
cd "$(dirname "$0")/.."
PY=.venv/bin/python
[ -x "$PY" ] || { echo "install_fw.sh: no .venv (run tools/install.sh)"; exit 1; }
if [ "$#" -eq 0 ]; then set -- build/vavewave.fwsc; fi
exec "$PY" tools/fm1_install.py "$@"
