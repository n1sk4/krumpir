# esp32-t12-controller

Temperature controller for a T12/T15 soldering tip on an ESP32 (ESP32-WROOM-32 DevKit).

**Status:** skeleton. A 1 kHz control task runs on core 1 with the loop-timing pin and the task
watchdog, and prints its period and execution-time statistics every 5 seconds. The heater cycle,
the simulated tip and the console are not in yet.

| Item | Value |
|---|---|
| Control loop | 1 kHz, period 1 ms, timer-driven task on core 1 |
| Loop timing pin | GPIO 23, high while the loop body runs |
| Console / log | UART0 through the USB-serial bridge, 115200 baud |

Shared code comes from `../../libs` (`ctrlcore` for loop statistics, `t12` for the tip model).

## Build

```
pio run -e esp32dev      # build
pio run -e esp32dev -t upload
pio device monitor
```

The monitor prints one line every 5 s: `loop period us min/mean/max 999/1000/1001  exec us max 3
overruns 0  n=4999`.
