#!/usr/bin/env bash
# Downloads the Reference FMUs release (checked against its SHA-256) and copies the FMUs the
# runner's tests use into runner/fmus. Needs curl, sha256sum and unzip.
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

VERSION=v0.0.42
SHA256=b6b4a195b9c0f6bdd3459cd949cbb35fc21ee2049d6dab4cb7a3ba7da63b1e97
URL=https://github.com/modelica/Reference-FMUs/releases/download/$VERSION/Reference-FMUs.zip

dest="$(cd "$(dirname "$0")/../fmus" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl -fL -o "$tmp/Reference-FMUs.zip" "$URL"
echo "$SHA256  $tmp/Reference-FMUs.zip" | sha256sum -c -
unzip -j "$tmp/Reference-FMUs.zip" 3.0/Feedthrough.fmu 3.0/Dahlquist.fmu 3.0/VanDerPol.fmu -d "$dest"
echo "FMUs installed in $dest"
