# Controller tuning and robustness

## Plant
The simulated tip (`libs/plants/t12`, parameters in `t12_plant.h`) is a three-node thermal network
(heater, copper tip, handle) behind a 24 V / 8 ohm heater. The values are placeholders until they
are fitted to a measured step response of a real tip:

| | value |
|---|---|
| heater power | 72 W (3 A) |
| time from cold to 350 degC at full power | about 5 s |
| power to hold 350 degC in free air | about 9 W |
| heat capacities (heater / tip / handle) | 0.25 / 0.8 / 15 J/K |
| thermal resistances (heater-tip / tip-air / tip-handle / handle-air) | 0.5 / 35 / 150 / 15 K/W |

The thermocouple sits on the heater, so the controller regulates the heater temperature. At 350 degC
in free air the tip itself is about 8 K cooler, and under load the gap grows.

## Method
The controller is a PI controller (`kd = 0`: the loop updates once per 0.1 s heater cycle, the
heating time is quantized to 1 ms and a derivative term would only amplify that quantization).

1. The output is a duty 0..1, turned into whole heating ticks (`duty * 93`); the PID clamps its
   integrator at the limits, so the loop recovers from saturation without windup.
2. The plant is dominated by one slow thermal pole (seconds) with a short lag between heater and
   tip, and the heater power is the limit while heating. Gains therefore only matter near the
   setpoint: `kp` sets how early the duty backs off, `ki` removes the steady-state error.
3. Sweep `kp` and `ki` and run the cold start and the joint-load step against each pair
   (`tools/scenarios.c sweep`).

## Sweep
Setpoint 350 degC, settling band 2 % (7 K). "Dip" is the fall below the setpoint after the
joint-load step (0.1 W/K, about 32 W).

| kp | ki | cold: overshoot % | cold: settling s | load: dip % | load: settling s |
|---|---|---|---|---|---|
| 0.02 | 0.02 | 2.1 | 7.10 | 3.0 | 2.20 |
| 0.02 | 0.05 | 2.3 | 6.40 | 2.1 | 1.00 |
| 0.02 | 0.10 | 1.9 | 5.10 | 1.5 | 0.00 |
| 0.05 | 0.02 | 0.1 | 5.40 | 1.7 | 0.00 |
| 0.05 | 0.05 | 0.4 | 5.30 | 1.3 | 0.00 |
| 0.05 | 0.10 | 0.6 | 5.20 | 1.0 | 0.00 |
| 0.10 | 0.02 | 1.4 | 5.40 | 0.7 | 0.00 |
| 0.10 | 0.05 | 1.5 | 5.40 | 0.7 | 0.00 |
| 0.10 | 0.10 | 1.6 | 5.40 | 0.6 | 0.00 |

Settling from cold is set by the 72 W heater, not by the gains: every pair from `kp 0.05` up arrives
in about 5.3 s. Low `kp` overshoots, high `kp` overshoots again and acts on the 1-tick duty
quantization. `kp 0.05` with `ki 0.05` is the middle of the flat region.

## Chosen gains
`kp = 0.05`, `ki = 0.05`, `kd = 0` (`T12_CTRL_CFG_DEFAULT` in `lib/t12ctrl/include/t12_ctrl.h`).
Re-tune on the real tip: the ratios depend on its heat capacity and on how much the handle and
the air take away.

## Scenarios and targets
Each scenario is a native test (`test/test_scenarios`) using the default gains and the default
plant. The measured figures come from `tools/scenarios.c summary`.

| Scenario | Targets | Measured |
|---|---|---|
| cold start to 350 degC | overshoot under 2 %, settling under 8 s, final error under 1 K | 0.4 %, 5.3 s, 0.0 K |
| joint load step at 30 s (0.1 W/K) | dip under 5 %, back in the band under 2 s, final error under 1.5 K | 1.3 %, 0 s, 0.0 K |
| ambient drops from 25 to 10 degC at 30 s | dip under 2 %, final error under 1.5 K | 0.1 %, 0.1 K |
| tip pulled out at 10 s | trip "no tip" within 0.5 s | 0.02 s |
| tip shorted at 10 s | trip "heater short" within 0.5 s | 0.02 s |
| heater switch sticks on at 10 s | trip "heater stuck on" within 0.5 s | 0.02 s |
| tip loses more heat than the supply can make up (0.5 W/K) | trip "thermal runaway" 19 to 25 s after the event | 20.3 s |

Plots: `scenario_cold.png`, `scenario_load.png`, `scenario_ambient.png`, `scenario_open.png`,
`scenario_short.png`, `scenario_stuck.png`, `scenario_runaway.png`. To regenerate them, from the
controller folder (needs CMake, Ninja, gcc and the Python venv):

```
cmake -S test -B .pio/tools -G Ninja && cmake --build .pio/tools --target t12_scenarios
.pio/tools/t12_scenarios cold > docs/scenario_cold.csv
python tools/plot.py docs/scenario_cold.csv docs/scenario_cold.png
.pio/tools/t12_scenarios summary
```

## Safety behavior

A trip stops the controller, sets the duty to 0 and stops the heartbeat that holds the series
switch on, so on the real board the heater loses power within 100 ms even if the first switch
fails. It stays latched until `clear`; `start` is refused in the meantime. The trips are checked
once per heater cycle (100 ms): the temperature and current trips appear within about 0.2 s of the
cause, `sensor timeout` after 5 cycles (within 0.6 s), `thermal runaway` after 20 s.

| Trip | Condition |
|---|---|
| `over temperature` | a reading above 480 degC |
| `heater stuck on` | more than 0.5 A while the heater is commanded off |
| `heater short` | 24 V divided by the heating current below 6 ohm |
| `no tip` | less than 0.3 A while heating |
| `heater resistance` | 24 V divided by the heating current above 10 ohm |
| `sensor timeout` | 5 invalid temperature readings in a row; a single one is ignored. A reading is invalid when a thermocouple sample of the window is at full scale (open input) or the HIL link is lost |
| `thermal runaway` | 20 s at 90 % duty or more without a 50 K rise |

| Condition | Result |
|---|---|
| `stop` | duty 0, PID reset; the simulated tip cools |
| missing tip while stopped | not detected: the check needs current, so it trips within one cycle after `start` |
| the control loop stops running for 1 s | the task watchdog resets the board; it comes back stopped with the defaults |

## Checks on the board
```
status              stopped, tip and handle near room temperature, trip none
start               after about 6 s the tip reads 350 C, duty about 0.16, heater 3.00 A
log on / log off    CSV rows about every 100 ms
load 0.1            the tip holds 350 C, duty rises to about 0.6; load 0 again afterwards
fault open          trip "no tip"; start is refused until fault none and clear
fault stuck         trip "heater stuck on", heater current 0.00 A afterwards
load 0.5, start     trip "thermal runaway" after about a minute
```
On the board the loop ran with an execution time of 23 us on average (about 100 us at worst),
a period of 1000 us +-15 us and no overruns. The simulation runs in double precision in the same
loop. Verified there: the heating and hold, the joint load, the no-tip and stuck-switch trips and
the refused start. The short, wrong-resistance, over-temperature, sensor and runaway trips are
covered by the native tests only. A 10 s capture of the cold start on the board
(`tools/serial/capture.py`) matches the simulation: 350 C is reached 5.7 s after heating starts
(6.0 s simulated), then the tip holds 350 to 351 C with a duty of 0.16 (0.17 simulated).
