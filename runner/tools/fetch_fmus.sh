#!/usr/bin/env bash
# Download the Reference FMUs release and copy the FMUs the runner uses into runner/fmus.
set -euo pipefail

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
