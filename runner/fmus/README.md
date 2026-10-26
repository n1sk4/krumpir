# Reference FMUs

The runner's tests and example scenarios use prebuilt FMI 3.0 co-simulation FMUs from the
[Reference FMUs](https://github.com/modelica/Reference-FMUs) project. The `.fmu` files are not
stored in this repository; download them once and copy them here.

## Source

| Item | Value |
|------|-------|
| Release | [v0.0.42](https://github.com/modelica/Reference-FMUs/releases/tag/v0.0.42) (2026-10-23) |
| Asset | `Reference-FMUs.zip` |
| URL | https://github.com/modelica/Reference-FMUs/releases/download/v0.0.42/Reference-FMUs.zip |
| sha256 | `b6b4a195b9c0f6bdd3459cd949cbb35fc21ee2049d6dab4cb7a3ba7da63b1e97` |
| License | see `LICENSE.txt` inside the zip |

The zip contains `3.0/*.fmu` (FMI 3.0) and `2.0/*.fmu` (FMI 2.0). Each FMU carries binaries for
`x86_64-linux`, `aarch64-linux`, `x86_64-windows` and others, so the same files work on the PC
(WSL) and on the Raspberry Pi.

## Install

Run `runner/tools/fetch_fmus.sh`, or by hand from the repository root on Linux/WSL:

```sh
curl -L -o /tmp/Reference-FMUs.zip \
  https://github.com/modelica/Reference-FMUs/releases/download/v0.0.42/Reference-FMUs.zip
echo "b6b4a195b9c0f6bdd3459cd949cbb35fc21ee2049d6dab4cb7a3ba7da63b1e97  /tmp/Reference-FMUs.zip" | sha256sum -c
unzip -j /tmp/Reference-FMUs.zip 3.0/Feedthrough.fmu 3.0/Dahlquist.fmu 3.0/VanDerPol.fmu -d runner/fmus
```

## Files used

| File | Used for |
|------|----------|
| `Feedthrough.fmu` | input to output wiring, parameters |
| `Dahlquist.fmu` | first-order dynamics compared against FMPy |
| `VanDerPol.fmu` | nonlinear dynamics compared against FMPy |

`*.fmu` is gitignored (`runner/.gitignore`). Tests that need these files skip with a message
pointing here when they are missing.

## No matching binary

If a platform is missing from a release, build the FMUs from source following the instructions in
the Reference FMUs repository and place the resulting `.fmu` files here.
