#!/usr/bin/env bash
# Builds and tests the plant FMUs for x86_64-linux and aarch64-linux, and merges them into FMUs
# that carry both binaries (and the sources):
#
#   bash libs/plants/tools/build_fmus.sh [BUILD_DIR]     (default ~/build/plant-fmus)
#
# Result: BUILD_DIR/fmus/fmi3/*.fmu and BUILD_DIR/fmus/fmi2/*.fmu. Needs cmake, ninja, gcc,
# gcc-aarch64-linux-gnu and qemu-user; the FMPy checks also need Python with fmpy.
set -euo pipefail

src="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$HOME/build/plant-fmus}"

for arch in x86_64 aarch64; do
  toolchain=()
  if [ "$arch" = aarch64 ]; then
    toolchain=(--toolchain "$src/aarch64-linux-gnu.cmake")
  fi
  cmake -S "$src" -B "$out/$arch" -G Ninja -DKRUMPIR_FMU_SELFTEST=OFF "${toolchain[@]}"
  cmake --build "$out/$arch"
  ctest --test-dir "$out/$arch" --output-on-failure
done

python3 "$src/tools/merge_fmus.py" "$out/fmus" "$out/x86_64" "$out/aarch64"

# The merged FMUs once more through FMPy (validation, run, rebuild from sources)
python="${PYTHON:-python3}"
if "$python" -c "import fmpy" 2>/dev/null; then
  for fmu in "$out"/fmus/fmi*/*.fmu; do
    "$python" "$src/tools/check_fmu.py" "$fmu" >/dev/null && echo "checked $fmu"
  done
else
  echo "fmpy not found in $python: merged FMUs not checked (set PYTHON=...)"
fi
