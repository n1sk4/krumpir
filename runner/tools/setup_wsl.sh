#!/usr/bin/env bash
# Install the build and test tools for the runner on Debian/Ubuntu (WSL or Raspberry Pi OS)
# and create the Python venv used by the comparison tools.
set -euo pipefail

sudo=""
[ "$(id -u)" -ne 0 ] && sudo="sudo"

$sudo apt-get update
$sudo apt-get install -y build-essential cmake ninja-build libexpat1-dev unzip curl \
  python3-venv python3-pip

# Keep the venv on the Linux filesystem: venvs need symlinks, which /mnt/<drive> may not support.
venv="${KRUMPIR_VENV:-$HOME/.venvs/krumpir}"
python3 -m venv "$venv"
"$venv/bin/pip" install -r "$(dirname "$0")/requirements.txt"

gcc --version | head -1
cmake --version | head -1
ninja --version
"$venv/bin/python" -c "import fmpy; print('fmpy', fmpy.__version__)"
