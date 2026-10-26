# Krumpir software architecture

A real-time FMU runner for the Raspberry Pi 4B. One isolated core runs the plant model at a fixed
step. The other three cores handle everything that may be slow: configuration, logging and
control. They talk only through lock-free shared memory.

## Real-time path

One step, h = 1 ms:

1. **Wait**: `clock_nanosleep` to the next absolute tick, or busy-wait.
2. **Apply params**: swap in a new parameter set if the mailbox has one.
3. **Read inputs**: I/O backend `read()` (UART value, ADC sample, or nothing).
4. **Step model**: set inputs, `fmi3DoStep(t, h)`, get outputs.
5. **Write outputs**: I/O backend `write()` (UART reply, PWM duty, DAC code).
6. **Record**: wake latency, step time and overrun flag go into the telemetry ring.

Time is computed as `t = n·h`, never accumulated, so long runs do not drift.

## Lifecycle

`IDLE → LOADED → INITIALIZED → RUNNING → STOPPED`, with `FAULT` reachable from any state.
Everything slow happens before RUNNING. Leaving RUNNING always passes through terminate, so the
FMU is freed cleanly.

| State | What happens |
| --- | --- |
| LOADED | FMU unpacked, XML parsed, binary loaded, I/O mapping checked against model variables |
| INITIALIZED | Model instantiated and initialized, buffers allocated, memory locked, stack pre-touched, I/O devices opened |
| RUNNING | RT thread pinned to the isolated core executes the step loop until the duration ends or stop is requested |
| FAULT | FMU returned an error, or overruns passed the configured limit. Outputs go to safe values, then terminate. |

## Modules

| Module | Responsibility | Runs in |
| --- | --- | --- |
| `app/` | CLI parsing, loading `scenario.toml`, wiring modules together | Linux cores |
| `core/rt_setup` | CPU affinity, `SCHED_FIFO`, `mlockall`, stack prefault | setup only |
| `core/loop` | Tick timing, step sequence, overrun monitor | isolated core |
| `fmi/loader` | Unzip, parse `modelDescription.xml`, `dlopen` | setup only |
| `fmi/backend` | One interface over the FMI calls | isolated core |
| `io/*` | One backend per I/O type behind a common interface | isolated core |
| `telemetry/ring` | Single-producer, single-consumer lock-free buffer | shared |
| `telemetry/writer` | Drains the ring, writes CSV, reports drops | Linux cores |
| `tools/*.py` | FMPy reference runs, comparison, histograms | PC |

## Seams

Swapping the I/O backend is the SIL ↔ HIL switch. Swapping the FMI backend lets the same runner
load other FMI versions.

```c
/* io/io.h: one struct per backend */
typedef struct io_backend {
  const char *name; /* "null", "uart", "pwm" */
  int (*open)(const io_cfg_t *cfg);
  int (*read)(double *in, size_t n);
  int (*write)(const double *out, size_t n);
  void (*safe_state)(void); /* on FAULT */
  void (*close)(void);
} io_backend_t;
```

```c
/* fmi/fmu.h: hides the FMI version */
typedef struct fmu_api {
  int (*instantiate)(fmu_t *m);
  int (*init)(fmu_t *m, double t0);
  int (*set_real)(fmu_t *m, const uint32_t *vr, size_t n, const double *v);
  int (*step)(fmu_t *m, double t, double h);
  int (*get_real)(fmu_t *m, const uint32_t *vr, size_t n, double *v);
  void (*terminate)(fmu_t *m);
} fmu_api_t;
```

## Configuration

A run is one file:

```toml
# scenario.toml
[model]
fmu = "fmus/dcmotor.fmu"
step = 0.001             # s
duration = 3600          # s
params = { J = 0.01, R = 1.2 }

[rt]
cpu = 3
priority = 80
wait = "sleep"           # or "busy"
max_overruns = 0

[io]
backend = "uart"         # "null" for SIL
device = "/dev/ttyAMA0"
baud = 921600
inputs = ["duty"]        # DUT → model
outputs = ["speed"]      # model → DUT

[log]
file = "runs/motor.csv"
```

## Design rules

- The RT thread never allocates memory, takes a lock, logs text or touches files. Its only system
  calls are the timer wait and the I/O backend.
- All memory is allocated and locked before RUNNING. Nothing is created inside the loop.
- Data crosses between cores only through the parameter mailbox and the telemetry ring. Both are
  lock-free and preallocated.
- If the ring is full, the sample is dropped and counted. The RT thread never waits for the writer.
- Parameter changes take effect only at a step boundary, never in the middle of a step.
- The same binary runs on PC and Pi. Only the scenario file changes between SIL and HIL.

## I/O backends

| Backend | Link | Notes |
| --- | --- | --- |
| `null` | none | SIL; generates stimulus inputs, discards outputs |
| `uart` | serial, binary frames | digital link to the device under test |
| `pwm` | PWM with RC filter | simple analog output |
| `spi` | SPI to a DAC/ADC | MCP4822 / MCP3208 |
