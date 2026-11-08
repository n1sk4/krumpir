# Krumpir software architecture

A real-time FMU runner for the Raspberry Pi 4B. One isolated core runs the plant model at a fixed
step. The other three cores handle everything that may be slow: configuration, logging and
control. They talk only through lock-free shared memory.

## Real-time path

Before step 0 the device gets the initialized model's outputs, so the input of every step, the
first one too, can come from the plant before it. Then one step, h = 1 ms:

1. **Wait**: `clock_nanosleep` to the next absolute tick, or busy-wait. The timing pin
   (`rt.timing_gpio`) goes high.
2. **Commands**: handle the queued commands (`core/cmdq`): input values and signals, tunable
   parameters, reads.
3. **Read inputs**: I/O backend `read()` (the controller's newest frame over UART, or nothing); a
   stimulus or a signal replaces an input.
4. **Step model**: set inputs, `fmi3DoStep(t, h)`, get outputs.
5. **Write outputs**: I/O backend `write()` (a frame to the controller, or nothing).
6. **Record**: wake latency, step time and overrun flag go into the telemetry ring; the timing pin
   goes low.

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
| `app/serve` | Model-access service: one session over TCP, JSON lines (`runner/docs/service-protocol.md`) | Linux cores |
| `app/scaffold` | `krumpir init`: a starter scenario from an FMU's variables | Linux cores |
| `core/rt_setup` | CPU affinity, `SCHED_FIFO`, `mlockall`, stack prefault | setup only |
| `core/loop` | Tick timing, step sequence, overrun monitor | isolated core |
| `core/timing_pin` | The `rt.timing_gpio` line, high while each step runs (GPIO character device, one ioctl per edge) | isolated core |
| `core/session` | One run with explicit phases (load, start, stop, unload); the loop runs on its own thread; reads and writes while it runs | Linux cores |
| `core/cmdq` | Lock-free command and reply queues: input values and signals, tunable parameters and reads, handled at the start of a step | shared |
| `core/siggen` | Signals from segments (const, ramp, sine, pulse, idle, data file), built before the run, evaluated per step | isolated core |
| `fmi/loader` | Unzip (miniz) into a private temp directory, `dlopen` the binary | setup only |
| `fmi/modeldesc`, `fmi/varmap` | Parse `modelDescription.xml` (expat); resolve the scenario's names to value references | setup only |
| `fmi/fmu`, `fmi/fmi3` | One interface over the FMI calls (`fmu.h`), and its FMI 3.0 co-simulation implementation | isolated core |
| `io/*` | One backend per I/O type behind a common interface | isolated core |
| `telemetry/ring` | Single-producer, single-consumer lock-free buffer | shared |
| `telemetry/writer` | Drains the ring, writes CSV, reports drops, passes each record to the captures | Linux cores |
| `telemetry/capture` | Records chosen inputs and outputs between a start and a stop trigger (condition, duration, buffer size); fetched as a copy | Linux cores |
| `tools/*.py` | FMPy reference runs with overlay plots (`compare_fmpy.py`), golden-file checks (`check_golden.py`) | PC |

## Seams

Swapping the I/O backend is the SIL ↔ HIL switch. Swapping the FMI backend lets the same runner
load other FMI versions.

```c
/* io/io.h: one table of functions per backend; io_open(sc, err, errlen) picks it by [io] backend */
typedef struct io_api {
  const char *name;
  int (*read)(io_t *io, double t, double *in, size_t n);        /* before each step */
  int (*write)(io_t *io, double t, const double *out, size_t n); /* after each step */
  void (*safe_state)(io_t *io); /* called once when the run faults */
  void (*close)(io_t *io);
} io_api_t;
```

```c
/* fmi/fmu.h: hides the FMI version. The instance is made by fmi3_create(). */
typedef struct fmu_api {
  fmu_result_t (*init)(fmu_t *m, double t0);
  fmu_result_t (*set_real)(fmu_t *m, const uint32_t *vr, size_t n, const double *v);
  fmu_result_t (*get_real)(fmu_t *m, const uint32_t *vr, size_t n, double *v);
  fmu_result_t (*step)(fmu_t *m, double t, double h);
  const char *(*last_message)(const fmu_t *m);
  void (*destroy)(fmu_t *m); /* terminate if needed, free the instance and the object */
} fmu_api_t;
```

`fmu_result_t` is a plain code (`FMU_OK`, `FMU_ERROR`, `FMU_FATAL`, `FMU_DISCARD`, `FMU_EVENT`,
`FMU_EARLY_RETURN`, `FMU_TERMINATE_REQUEST`), so the real-time loop never formats text. The FMU's
last warning or error message is kept in a fixed buffer and read outside the loop.

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
  calls are the timer wait, the I/O backend and the timing pin's ioctl.
- All memory is allocated and locked before RUNNING. Nothing is created inside the loop.
- Data crosses between cores only through the command queues (`core/cmdq`) and the telemetry ring.
  All are lock-free and preallocated.
- On a paced run, if the ring is full the sample is dropped and counted: the RT thread never waits
  for the writer. An unpaced run (`wait = "none"`) waits instead, so it never drops a row.
- Parameter changes take effect only at a step boundary, never in the middle of a step.
- The same binary runs on PC and Pi. Only the scenario file changes between SIL and HIL.

## I/O backends

| Backend | Link | Notes |
| --- | --- | --- |
| `null` | none | SIL; inputs read 0 (signals replace them), discards outputs |
| `uart` | serial, binary frames (`libs/hil`) | digital link to the device under test |
| `pwm` | PWM with RC filter | later stage, not built: simple analog output |
| `spi` | SPI to a DAC/ADC | later stage, not built: MCP4822 / MCP3208 |
