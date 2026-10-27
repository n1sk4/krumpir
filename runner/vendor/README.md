# Vendored third-party code

| Directory | Project | Version | License | Used by |
|-----------|---------|---------|---------|---------|
| `tomlc99/` | [cktan/tomlc99](https://github.com/cktan/tomlc99) | commit `29076dfd095bbbbd50a3c1b2760d29f4b83e74ac` (2026-01-30) | MIT (`tomlc99/LICENSE`) | scenario parsing (`src/app/config.c`) |

| `miniz/` | [richgel999/miniz](https://github.com/richgel999/miniz) | release 3.1.2 (2026-07-01), amalgamated `miniz-3.1.2.zip` (sha256 `f0446d86…140d9a`) | MIT (`miniz/LICENSE`) | unpacking FMUs (`src/fmi/loader.c`) |

| `fmi3/` | [modelica/fmi-standard](https://github.com/modelica/fmi-standard) | tag `v3.0.2` (`headers/`) | BSD-2-Clause (in each file header) | FMI 3.0 backend (`src/fmi/fmi3.c`) |

Files are copied unmodified from the pinned commit or release (`toml.c`, `toml.h`, `miniz.c`,
`miniz.h`, `LICENSE`).

| File | sha256 |
|------|--------|
| `tomlc99/toml.c` | `1ed386df52a6d7a9ee8341c35af6689e15ecc78e053e246ede1a2eb937171e26` |
| `tomlc99/toml.h` | `54743ee323a09a99f53e409de0e14890db0bd5afcd5ac7f0b867e6e238eb594a` |
| `fmi3/fmi3FunctionTypes.h` | `d247d1c5fbf58e32379f1d38e7f73990709e0501fc4a9b3901ce3f9638503a0f` |
| `fmi3/fmi3Functions.h` | `038358fb0209c3547fe8c6f03b80582960c93ce0a7d342b0d9348c8ce6137784` |
| `fmi3/fmi3PlatformTypes.h` | `1aa81efddeb84b0d9425f9226969525bd034cb63848c086b6696655179500616` |
| `miniz/miniz.c` | `e2c1aeb66eef9191d8c3feb164db2def2335a61d039bf04ed849f6b042433b30` |
| `miniz/miniz.h` | `b53b62ed122e559b8f679e3cb787a0b0035fe87a58f909da0e44931678f4e85f` |

Both libraries are compiled without the project's `-Wall -Wextra -Werror` flags (`-w` in
`runner/CMakeLists.txt`). miniz is built with `MINIZ_NO_ZLIB_APIS`: the runner only reads and
writes zip archives and does not use the zlib-style wrappers.

System dependency: `libexpat` (`libexpat1-dev`), installed by `tools/setup_wsl.sh`.
