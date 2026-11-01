# xcplite

PlatformIO wrapper around [XCPlite](https://github.com/vectorgrp/XCPlite) by Vector Informatik (MIT
licence). The sources come from the submodule `libs/vendor/XCPlite`, a fork
([n1sk4/XCPlite](https://github.com/n1sk4/XCPlite), branch `krumpir`) of upstream commit `da32477`
(V2.3.2) with one change: `src/platform.h` pins the XCP server tasks to core `OPTION_FREERTOS_CORE`
on ESP-IDF when that is defined (marked `krumpir patch`), so they stay off the core of the control
loop.

After cloning the repository, fetch the submodule with `git submodule update --init` (or clone with
`--recurse-submodules`).

What is here:

- `library.json`: builds only the 8 sources needed for an XCP on Ethernet (UDP) server on a 32-bit
  FreeRTOS target (`xcplite.c`, `xcpappl.c`, `xcpethserver.c`, `xcpethtl.c`, `queue32m.c`, `cal.c`,
  `platform.c`, `sockets.c`), with format warnings off: ESP-IDF treats them as errors and `uint32_t`
  is `long` on Xtensa.
- `include/`: forwarding headers for `xcplib.h`, `xcplib_cfg.h` and `a2l.h`. Only code that uses
  XCP sees the XCPlite headers; a global include path to its `src/` would shadow ESP-IDF headers of
  the same name (`platform.h`).
- `xcp_sections.py`: places XCPlite's linker sections in the ESP-IDF image (see the comment at its
  top).

PlatformIO scans every folder in `libs`, so the projects that use `libs` set `lib_ignore = vendor`:
the submodule is built only through this wrapper.

The controllers use it through [../xcpglue](../xcpglue/README.md), which also holds the build flags
(`xcp.ini`). To move to a newer XCPlite, rebase the `krumpir` branch of the fork onto the upstream
release and update the submodule.
