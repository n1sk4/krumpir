# xcpglue

XCP on Ethernet over Wi-Fi for the ESP-IDF controllers, on top of [../xcplite](../xcplite/README.md).
With it, an XCP tool (`xcpclient`, CANape) measures any variable of the firmware at the loop rate,
with timestamps from the board, and calibrates parameters while the controller runs. The variables
are described by an A2L file generated from the firmware's ELF file.

- `xcp_glue_start(name)` (`include/xcp_glue.h`): joins the Wi-Fi network from `include/wlan.h` and
  starts the XCP server on UDP port 5555. Without Wi-Fi, XCP stays passive and the controller runs
  anyway.
- `xcp.ini`: the PlatformIO settings of an XCP build (build flags, extra scripts, sdkconfig).
- `xcp_epk.py`: gives every build a new EPK (`build_<date>_<time>`). xcpclient puts it in the A2L
  file and the target reports it on connect, so an A2L file from another build is refused.
- `sdkconfig.xcp.defaults`: lwIP on core 0. XCPlite's server tasks are pinned there too
  (`-DOPTION_FREERTOS_CORE=0`), so the network side stays off the core of the control loop.

## Adding XCP to a controller

1. Wi-Fi: copy `include/wlan.h.example` to `include/wlan.h` (gitignored) and fill in a 2.4 GHz
   network. All controllers share it.
2. `platformio.ini`: load the shared settings, keep the libraries out of the normal build and add
   an XCP env:

   ```ini
   [platformio]
   extra_configs = ../../libs/esp/xcpglue/xcp.ini

   [env:esp32dev]
   lib_ignore = xcpglue, xcplite   ; the library finder does not see #ifdef KRUMPIR_XCP
   ...

   [env:esp32dev-xcp]
   extends = env:esp32dev
   lib_ignore =
   lib_deps =
     ${env:esp32dev.lib_deps}
     xcpglue
   extra_scripts = ${xcp.extra_scripts}
   board_build.cmake_extra_args = ${xcp.cmake_extra_args}
   build_flags = ${xcp.build_flags}
   ```

3. `src/CMakeLists.txt`: add `esp_wifi esp_netif esp_event nvs_flash esp_timer` to `REQUIRES`.
4. `app_main`: call `xcp_glue_start("<name>")` under `#ifdef KRUMPIR_XCP`, before the control task
   starts.
5. The control loop: declare and trigger the measurement events, and read calibration parameters
   from a calibration segment, under `#ifdef KRUMPIR_XCP`. `controllers/esp32-t12-controller/src/control_task.c`
   is the example: an event `tick` every 1 ms, an event `cycle` per heater cycle and a calibration
   page `t12_cal_page` with setpoint and gains, checked for range before it is used.

The sdkconfig defaults only apply when PlatformIO creates `sdkconfig.<env>`: delete that file after
changing `sdkconfig.xcp.defaults`.

## Using it

Needs `xcpclient` on the PC (`cargo +stable install --path XCPlite/tools/xcpclient` in a clone of
XCPlite). From the controller folder, after the last build (each build has a new EPK):

```
pio run -e esp32dev-xcp -t upload
xcpclient --offline --udp --dest-addr <ip> --elf .pio/build/esp32dev-xcp/firmware.elf --a2l docs/<name>.a2l --elf-unit-filter control_task --default-event tick --embed-aml
xcpclient --udp --dest-addr <ip> --a2l docs/<name>.a2l --list-mea "s_loop"
xcpclient --udp --dest-addr <ip> --a2l docs/<name>.a2l --mea s_loop.temp s_loop.duty --time 10 --csv docs/xcp.csv
xcpclient --udp --dest-addr <ip> --a2l docs/<name>.a2l --cal t12_cal_page.setpoint 300
```

The board prints its IP address at boot (`xcp: wifi connected, ip ...`). Let a measurement end with
`--time`: a client stopped with Ctrl+C leaves the server waiting for it, and only a reset frees it.

## Cost on the board

T12 controller, ESP32 at 160 MHz, 1 kHz loop, measured with `tools/serial/timing_check.py` and cycle
counters read over XCP:

| | exec min | exec mean | exec max |
|---|---|---|---|
| normal build | 21.6 us | 23-25 us | 76 us |
| XCP build, no client | 27.5 us | 32 us | 174 us |
| XCP build, streaming 9 values at 1 kHz | 27.4 us | 30 us | 152 us |

- The per-tick hooks cost about 6 us without a client; the event trigger copying 9 values costs
  16.6 us per tick while streaming.
- Wi-Fi alone raises the worst tick from 76 to about 150-170 us and the period jitter from +-4 to
  +-13 us.
- Before the XCP and lwIP tasks were pinned to core 0, the calibration lock stalled for up to
  80 us on a 30 ms grid; pinned, it stays at 1.6 us.
- No missed tick in any run; 0 lost samples at 1 kHz.
- Flash 86 % of the 1 MB app partition (normal build 27 %), RAM 24 % (normal build 5 %).
