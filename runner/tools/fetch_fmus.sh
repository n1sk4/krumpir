#!/usr/bin/env bash
# Downloads the Reference FMUs release (checked against its SHA-256, cached in KRUMPIR_CACHE) and
# copies the FMUs the runner's tests use into runner/fmus: cmake/fetch_reference_fmus.cmake, which
# the runner's configure step also runs. Needs CMake 3.18+.
#
#   bash runner/tools/fetch_fmus.sh
#
# Exit status: 0 ok, 1 a step failed, 2 usage.
set -euo pipefail

usage() { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-2}"; }
case ${1:-} in
  -h | --help) usage 0 ;;
  "") ;;
  *) usage ;;
esac
exec cmake -P "$(dirname "$0")/../../cmake/fetch_reference_fmus.cmake"
