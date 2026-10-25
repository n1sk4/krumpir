# Controller tuning and robustness

## Plant
The simulated motor (N20-class 6 V gearmotor parameters in `plant_sim.h`) is a second-order
system with one slow and one fast pole:

| | value |
|---|---|
| DC gain | about 5590 rpm per unit duty (no-load speed at full duty) |
| slow pole (mechanical) | -41.7 1/s, time constant 24 ms |
| fast pole (electrical) | -2459 1/s, time constant 0.4 ms |

## Method
The controller is a PI controller (`kd = 0`: the model has no sensor noise and the slow pole
dominates, so a derivative term would only add noise sensitivity).

1. Put the PI zero on the slow pole: `ki / kp = 41.7 1/s`. The loop then behaves like a
   first-order system with time constant `24 ms / (kp * 5590)`.
2. Sweep `kp` (with `ki` scaled along, so the integral time stays 24 ms) and run every scenario
   below against each value (`tools/scenarios.c sweep`).
3. Pick the gain that meets all targets with margin on the hardest scenarios.

The loop gain crossover stays far below the sample rate (about 160 rad/s against 3140 rad/s
Nyquist), so one sample of delay costs only a few degrees of phase.

## Sweep (reference gains `kp 3.6e-4, ki 0.015` times the scale)
Speed target 3000 rpm, settling band 2 %. "dip" is the fall below the target after a
disturbance or after the setpoint returns from saturation.

| gain scale | kp | ki | step: overshoot % | step: settling s | load: dip % | load: settling s | saturation: dip % | saturation: min rpm | inertia x2: overshoot % | inertia x2: settling s |
|---|---|---|---|---|---|---|---|---|---|---|
| 0.5 | 1.80e-04 | 0.0075 | 0.0 | 0.093 | 2.9 | 0.050 | 32.9 | 2014 | 7.0 | 0.178 |
| 1.0 | 3.60e-04 | 0.0150 | 0.0 | 0.058 | 2.0 | 0.000 | 33.5 | 1994 | 5.9 | 0.121 |
| 2.0 | 7.20e-04 | 0.0300 | 0.0 | 0.064 | 1.3 | 0.000 | 18.3 | 2450 | 0.4 | 0.056 |
| 3.0 | 1.08e-03 | 0.0450 | 0.0 | 0.060 | 1.0 | 0.000 | 13.3 | 2601 | 0.0 | 0.062 |

Higher gains reduce the dip and make the loop less sensitive to a change in rotor inertia. Scale 3
also meets every target but would amplify sensor noise more on a real motor, so scale 2 is the
compromise.

## Chosen gains
`kp = 7.2e-4`, `ki = 0.03`, `kd = 0` (`CTRL_PARAMS_DEFAULT` in `lib/control/include/ctrl_params.h`).

## Scenarios and targets
Each scenario is a native test (`test/test_scenarios`) using the default gains.

| Scenario | Targets |
|---|---|
| step 0 to 3000 rpm | overshoot under 10 %, settling under 0.3 s, steady-state error under 0.5 % |
| load torque step 0.001 N*m at 3000 rpm | dip under 5 %, back within the band in under 0.3 s, no steady-state error |
| saturating setpoint (10000 rpm), then 3000 rpm | output pinned at its limit while saturated, settles within 0.5 s, speed never below 60 % of the target |
| rotor inertia doubled, then step | same targets as the step |

Plots: `scenario_step.png`, `scenario_load.png`, `scenario_saturation.png`,
`scenario_inertia.png`. To regenerate them (needs gcc and the Python venv):

```
gcc -Ilib/control/include tools/scenarios.c lib/control/src/pid.c lib/control/src/plant_sim.c lib/control/src/io_sim.c lib/control/src/step_metrics.c lib/control/src/scenario.c -lm -o .pio/scenarios.exe
.pio/scenarios.exe step > docs/scenario_step.csv
python tools/plot.py docs/scenario_step.csv docs/scenario_step.png
```

## Safety behavior
- **Stop:** `stop` forces zero duty and resets the PID; the simulated motor coasts down.
- **Faults** stop the controller and stay latched until `clear`; `start` is refused while a fault
  is latched.
  - overspeed: |speed| above 7000 rpm (the no-load speed is about 5600 rpm).
  - sensor timeout: 50 consecutive non-finite speed readings (50 ms). A single bad reading is
    ignored; the PID holds its last output.
- **Watchdog:** the control task is registered with the task watchdog and feeds it every 100
  cycles. The timeout is 1 s and a timeout resets the board.

## Checks on the board
```
load 0.001          dip and recovery (use log on to capture)
inertia 2           then sp 3000 still settles
sp 10000            duty pins at 1000 permille; sp 3000 recovers
load -0.06          overspeed fault: status shows it, duty is 0, start is refused
clear               then start works again
```
For the watchdog, build once with `PLATFORMIO_BUILD_FLAGS=-DCONTROL_HANG_AT_TICK=3000`. The loop
hangs at that tick and the board must reset within about a second.
