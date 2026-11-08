# Building Krumpir

Everything that builds on a PC is one CMake tree with presets; the board firmware is PlatformIO's.
`tools/kb.py` runs both, in the right order, and says what this machine cannot do.
Back to the [README](README.md).

**Contents:** [Setup](#setup) · [kb.py](#kbpy) · [The CMake tree](#the-cmake-tree) ·
[Firmware](#firmware) · [Plant FMUs](#plant-fmus) · [PC controllers](#pc-controllers) ·
[CMake conventions](#cmake-conventions) · [Options and environment](#options-and-environment) ·
[Versions and downloads](#versions-and-downloads) · [Scripts](#scripts) · [CI](#ci)

## Setup

| Machine | Builds | Needs |
|---|---|---|
| Linux, WSL, the Pi | everything on the PC: libraries, controller tests, runner, plant FMUs, PC controllers, TUI; the loops and comparisons | Python 3.13+, CMake 3.25+, Ninja, gcc, libexpat; socat for the loops; for the Pi's FMUs gcc-aarch64-linux-gnu and qemu-user |
| Windows | libraries, controller tests, TUI (MinGW); the firmware | Python 3.13+, CMake, Ninja, MinGW-w64 gcc (`winget install BrechtSanders.WinLibs.POSIX.UCRT`) |
| any, for a board | the firmware | PlatformIO (`pip install platformio`, or the VS Code extension: `~/.platformio/penv/.../pio`), the DevKit's USB driver (CP210x or CH340) |

```sh
git clone --recurse-submodules https://github.com/n1sk4/krumpir   # or: git submodule update --init
python3 tools/kb.py setup --system   # Debian/Ubuntu/WSL/Pi: apt packages first (sudo)
python  tools/kb.py setup            # Windows: the venv and the Python packages
python3 tools/kb.py info             # what this machine can build, and where the build goes
```

`setup` installs only what is missing and prints what it did: with `--system` the apt packages
(build tools, libexpat, socat, the aarch64 cross compiler, qemu), then one venv with every Python
package of `tools/requirements.txt` (`~/.venvs/krumpir`, or `KRUMPIR_VENV`; on Windows `.venv\`
in the repository; the venv on WSL stays on the Linux side, since `/mnt/<drive>` lacks symlinks),
and the Reference FMUs on Linux. Empty submodules (`libs/vendor/Reference-FMUs`,
`libs/vendor/XCPlite`) are named with the command that fetches them. On Windows run it with
`python`: `python3` there is the Microsoft Store stub.

**Python 3.13+:** Debian 13, Raspberry Pi OS 13 and Ubuntu 25.04 ship it; `kb.py` refuses an older
one, and `setup` makes the venv again when it is older (after a distro upgrade). Debian 12 has
3.11; a Debian 12 WSL is upgraded to 13 in place, then `setup` runs again:

```sh
sudo sed -i 's/bookworm/trixie/g' /etc/apt/sources.list
sudo apt update && sudo apt full-upgrade -y && sudo apt autoremove -y
wsl.exe --shutdown                   # from Windows; then open WSL again
python3 tools/kb.py setup --system
```

## kb.py

| Command | Does |
|---|---|
| `info` | the platform, the build folder, what is there (cmake, ninja, compiler, cross, socat, pio, xcpclient, the venv), the controllers |
| `setup [--system]` | the above |
| `build [--preset P] [--only PARTS]` | configure and build the root tree (`--only "runner;tui"`) |
| `test [--preset P] [-R REGEX]` | ctest on it |
| `check` | the codegen self-test and `--check` of each controller, the tools' self-checks (`--selftest`), `tools/test_kb.py` |
| `all [--preset P]` | setup, build, test, check |
| `fmus [--both-archs]` | the plant FMUs; with both: x86_64 and aarch64, tested and merged |
| `fw CTRL [hil\|xcp]` | `pio run -e esp32dev[-hil\|-xcp]` in the controller's folder |
| `flash CTRL [hil\|xcp] [--monitor]` | the same with `-t upload`, then the serial monitor |
| `a2l CTRL --ip IP` | the board's A2L from its ELF (xcpclient), into the controller's `docs/` |
| `a2l CTRL --host --elf ELF [--port N]` | the PC build's A2L, fitted by `tools/hil/fix_host_a2l.py` |
| `loop KIND [--live]` | a closed loop on this PC, checked (`tools/hil/pc_loop.sh KIND --check`) |
| `compare CTRL [--board DEV --console PATH]` | SIL vs PC loop (or vs the board): `tools/hil/compare.py` |
| `clean [--preset P]` | remove the build folder |

`CTRL` is a controller's name (`motor`, `t12`, from its `controller.toml`) or its folder.
`--dry-run` (before the command) prints the commands instead of running them. Exit status: 0 ok,
1 a step failed, 2 usage.

**The build folder:** `$KRUMPIR_BUILD_ROOT/<preset>` when that is set; on WSL with the repository on
`/mnt/<drive>`, `~/build/krumpir/<preset>` (the Linux side builds much faster); else
`build/<preset>` in the repository (gitignored). The default preset is `dev`, on Windows `mingw`.

## The CMake tree

The root `CMakeLists.txt` adds every part this platform builds; `CMakePresets.json` has the
presets. Without `kb.py`:

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset dev -B ~/build/krumpir/dev && cmake --build ~/build/krumpir/dev && ctest --test-dir ~/build/krumpir/dev
```

| Preset | |
|---|---|
| `dev` | Debug, everything this platform builds |
| `release` | Release (the Pi, a TUI to hand out) |
| `asan` | Debug with the address and undefined-behaviour sanitizers (Linux) |
| `tsan` | Debug with the thread sanitizer (Linux; on WSL run its tests under `setarch -R`) |
| `pi-cross` | the plant FMUs for aarch64, cross-built (`KRUMPIR_ONLY=plants`) |
| `mingw` | Windows with MinGW: the libraries, the controllers' tests and the TUI (statically linked, no DLLs) |

| Part (`KRUMPIR_ONLY`) | Folder | Alone | Platform |
|---|---|---|---|
| `libs` | the shared libraries and their Unity tests | `cmake -S libs` | any |
| `controllers` | each controller's library and Unity tests | `cmake -S controllers/<x>/test` | any |
| `runner` | the runner, the plant FMUs (into `runner/fmus`), the PC controllers, all their tests | `cmake -S runner` | Linux |
| `plants` | the plant FMUs alone (only without the runner) | `cmake -S libs/plants` | Linux |
| `tui` | the terminal UI and its tests | `cmake -S clients/tui` | any (C++17) |

Each part still configures alone, as CI does. On `dev` Linux runs 71 tests (the closed loops skip
without socat), Windows `mingw` 25. The Unity tests also run with PlatformIO: `pio test -e native`
in `libs/` or a controller folder.

## Firmware

PlatformIO, in the controller's folder (`python tools/kb.py fw|flash CTRL [ENV]` does the same):

```sh
pio test -e native                    # the unit tests on the PC
pio run -e esp32dev                   # the firmware, with the plant simulated on the chip
pio run -e esp32dev-hil -t upload     # the plant behind the runner (UART2, docs/krumpir-interface.md)
pio run -e esp32dev-xcp -t upload     # XCP over Wi-Fi (libs/esp/xcpglue/include/wlan.h first)
pio device monitor -p COM4            # the console, 115200 baud
```

- `platformio.ini` is generated from `controller.toml` (`tools/codegen`). The firmware builds
  with `-Wall -Wextra -Werror -Wno-unused-parameter` (`build_src_flags`), and each shared library
  sets its own in `library.json`.
- Shared libraries come from `lib_extra_dirs = ../../libs, ../../libs/plants, ../../libs/esp`;
  `lib_ignore = vendor` keeps the submodules out (XCPlite builds only through `libs/esp/xcplite`).
  The XCP env loads `libs/esp/xcpglue/xcp.ini`.
- `sdkconfig.<env>` is made from `sdkconfig.defaults` (and `sdkconfig.xcp.defaults`) only when it
  does not exist: delete it after changing the defaults.

## Plant FMUs

The runner's build makes the FMI 3.0 FMUs for this machine and copies them to `runner/fmus`.
For both platforms (x86_64 and the Pi's aarch64), tested (aarch64 under qemu), merged into FMUs
that carry both binaries and the sources, and checked with FMPy:

```sh
python3 tools/kb.py fmus --both-archs     # runs libs/plants/tools/build_fmus.sh
ls <build-root>/plant-fmus/fmus/fmi3 <build-root>/plant-fmus/fmus/fmi2
```

One platform: `python3 tools/kb.py fmus` (`--only plants`), or the `pi-cross` preset for aarch64.

## PC controllers

`controllers/*/host` builds each controller for Linux (the same control task as the board, the
plant behind the HIL link), as part of the runner (`KRUMPIR_HIL_PC`, default ON): they are
`<build>/runner/host-<x>/<x>_host`. Alone, or with XCP:

```sh
cmake -S controllers/esp32-t12-controller/host -B ~/build/host-t12-xcp -DKRUMPIR_XCP=ON
cmake --build ~/build/host-t12-xcp
```

## CMake conventions

Every CMake project here includes `cmake/krumpir.cmake` (`libs/cmake/krumpir.cmake` forwards to
it), which gives:

- C11 and C++17, no compiler extensions, `compile_commands.json`.
- `krumpir::warnings`: `-Wall -Wextra -Werror` (+ `-Wpedantic` for C++), MSVC `/W4`; linked
  PRIVATE by each of our targets, never added globally. `krumpir::warnings_relaxed` adds
  `-Wno-unused-parameter` (the board's hook signatures).
- `KRUMPIR_SANITIZE`: `OFF`, `address` (also `ON`) or `thread`, for every C/C++ target.
- `krumpir_add_libs()`: the portable libraries (ctrlcore, dcmotor, t12, osal, osal_host, rte,
  hil, unity), each also `krumpir::<name>`, added once per tree.
- `krumpir_unity_tests(<dir> [PREFIX p] <libs...>)`: each `<dir>/test_*/test_main.c` one test.
- `krumpir_add_test(NAME n SOURCES ... | COMMAND ... [LIBS] [DEFINES] [ENV] [WORKDIR] [SKIP_77])`.
- `krumpir_vendor(<target>)`: third-party code, SYSTEM includes, no warnings of ours.
- `krumpir_add_subdirectory_once(<target> <dir> <bindir>)`.
- `krumpir_download()`, `krumpir_reference_fmus()`, the pins: `cmake/versions.cmake`.

The files have one shape: a header comment (what it builds, how to build it alone),
`cmake_minimum_required(VERSION 3.25)` in the roots (the controllers' ESP-IDF projects keep IDF's
own), `set(srcs ...)` one file per line and sorted, and in each library the ESP-IDF branch first
(`if(ESP_PLATFORM) idf_component_register(...) return()`), then `add_library`, the alias and the
warnings. Each library thus builds from PlatformIO (`library.json`), any CMake project and ESP-IDF
(list the folder in `EXTRA_COMPONENT_DIRS`).

## Options and environment

| CMake option | Default | |
|---|---|---|
| `KRUMPIR_ONLY` | empty: all | the parts to build (root tree) |
| `KRUMPIR_SANITIZE` | `OFF` | `address`, `thread` |
| `KRUMPIR_HIL_PC` | `ON` | the runner also builds the PC controllers and the closed-loop tests |
| `KRUMPIR_FETCH_FMUS` | `ON` | the runner downloads the Reference FMUs when they are missing |
| `KRUMPIR_XCP` | `OFF` | a PC controller serves XCP on UDP |
| `KRUMPIR_PYTHON` | `~/.venvs/krumpir/bin/python` | the Python with FMPy for the runner's `fmpy_*` and `client` tests |
| `KRUMPIR_FMU_COPY_DIR` | empty | also copy the FMI 3.0 FMUs here (the runner sets `runner/fmus`) |
| `KRUMPIR_FMU_SELFTEST` | `ON` | also build the Reference FMUs' Stair model (plants alone) |
| `KRUMPIR_CACHE` | from the environment | the download cache |

| Environment | |
|---|---|
| `KRUMPIR_BUILD_ROOT` | where `kb.py` (and `pc_loop.sh`) put the build folders |
| `KRUMPIR_VENV` | the Linux venv (default `~/.venvs/krumpir`) |
| `KRUMPIR_CACHE` | the download cache (default `~/.cache/krumpir`, on Windows `%LOCALAPPDATA%\krumpir`) |
| `KRUMPIR_RUNNER`, `KRUMPIR_CONTROLLER`, `KRUMPIR_XCP_PORT` | `pc_loop.sh`'s programs and XCP port |
| `KRUMPIR_PYTHON` | `build_fmus.sh`'s Python for the FMPy checks |
| `KRUMPIR_EXE`, `KRUMPIR_SERVICE`, `KRUMPIR_ECU_A2L` | the Python client's tests |

## Versions and downloads

- `cmake/versions.cmake` pins every download by URL and SHA-256: the Reference FMUs release (the
  runner's tests), FTXUI, Asio, nlohmann/json and doctest (the TUI). `krumpir_download()` keeps
  each archive once in `KRUMPIR_CACHE`, so a new build tree downloads nothing; a wrong hash is
  refused. The Reference FMUs by hand: `cmake -P cmake/fetch_reference_fmus.cmake`.
- Submodules: `libs/vendor/Reference-FMUs` (the plant FMUs' framework) and `libs/vendor/XCPlite`;
  vendored copies and their hashes are in
  [libs/README.md](libs/README.md#vendored-code) and [runner/README.md](runner/README.md#vendored-code).
- Python: `tools/requirements.txt` (minimum versions); the older per-folder files include it.
- PlatformIO: the platform version is pinned in each `platformio.ini` (from codegen).

## Scripts

The shell scripts (`runner/tools/setup_wsl.sh`, `libs/plants/tools/build_fmus.sh`,
`tools/hil/pc_loop.sh`) and the Python tools share one shape: a header with the usage (`--help`),
overrides named `KRUMPIR_*`, exit status 0 ok, 1 failed, 2 usage, 77 skipped (CTest's skip code),
and a `--selftest` where a tool has logic that can be checked without hardware (run by
`kb.py check`). `setup_wsl.sh` runs `kb.py setup --system`.

## CI

`.github/workflows/` runs the same `kb.py` commands as above, on demand (`workflow_dispatch`):

| Workflow | Runs |
|---|---|
| `controllers.yml` | `kb.py check`; `build --only "libs;controllers"` and `test`; per folder with a `controller.toml`: `fw CTRL`, `fw CTRL xcp`, then its generated `tools/ci.sh` (`[[ci.step]]`) |
| `runner.yml` | `setup --system`, `build --only runner` and `test` with the `dev` and `asan` presets; the FMPy overlay plots |
| `plants.yml` | `setup --system`, `fmus --both-archs` |
| `tui.yml` | `build --only tui` and `test`: `release` on Linux and arm64, `mingw` on Windows |
| `parts.yml` | each part configured alone (`cmake -S <part>`), without `kb.py` |
