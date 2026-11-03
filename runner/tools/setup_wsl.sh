#!/usr/bin/env bash
# Installs the build and test tools on Debian/Ubuntu (WSL or Raspberry Pi OS) and creates the
# Python venv (~/.venvs/krumpir, or KRUMPIR_VENV); the same as python3 tools/kb.py setup --system.
#
#   bash runner/tools/setup_wsl.sh
#
# Exit status: 0 ok, 1 a step failed, 2 usage.
set -euo pipefail

usage() { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-2}"; }
case ${1:-} in
  -h | --help) usage 0 ;;
  "") ;;
  *) usage ;;
esac
exec python3 "$(dirname "$0")/../../tools/kb.py" setup --system
