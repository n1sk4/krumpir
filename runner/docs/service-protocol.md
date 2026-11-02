# Service protocol

`krumpir serve [[host:]port]` runs the model-access service: one simulation session driven over
TCP, the role of the model access port (MAPort) of ASAM XIL. Default address `127.0.0.1:7700`.

```sh
krumpir serve                 # local clients only
krumpir serve 0.0.0.0:7700    # reachable from the LAN (on the Pi)
```

There is no authentication. A client can load any FMU and write the log to any path the service
user can write. Listen on `127.0.0.1` or a trusted LAN only.

## Framing

One JSON object per line in each direction: a request, then its reply. One client at a time;
another client waits until the first one disconnects. The session belongs to the service, not to
the connection: a run keeps going after its client disconnects, and the next client finds it as
it was. SIGINT or SIGTERM stops the run, unloads it and ends the service.

Every request has `op`. An optional `tag` (any JSON value) is copied into the reply. Every reply
has `ok`; when it is `false`, `error` says why and nothing else is in the reply.

```json
{"op": "read", "names": ["speed"], "tag": 12}
{"tag": 12, "ok": true, "values": {"speed": 104.7}}
```

## Operations

| `op` | Request | Reply | XIL |
|------|---------|-------|-----|
| `info` | | `protocol` (1), `state`, `fault` | |
| `load` | `scenario`: path on the server, or `toml`: the scenario text | `state` | `LoadConfiguration`, `Configure` |
| `init` | `fmu`: path on the server | `toml`: a starter scenario for it, as `krumpir init` prints it | |
| `variables` | | `variables`: `[{name, vr, type, causality, tunable}]` | `GetVariableNames`, `GetVariableInfo` |
| `start` | | `state` | `StartSimulation` |
| `stop` | | `state` | `StopSimulation` |
| `state` | | `state`, `fault`; once loaded also `fmu`, `step`, `duration` (when set), `t`, `steps`, `overruns`, `max_step_ns`, `max_wake_ns` of the run so far | `State` |
| `files` | | `scenarios`: `scenarios/*.toml`, `fmus`: `fmus/*.fmu` in the service's directory, sorted | |
| `text` | `path`: a `.toml` file on the server (up to 64 kB) | `text` | |
| `wait` | `timeout_ms` (default: forever) | `done`, `state` | |
| `reset` | | `summary` (of the run that ended), `state` | |
| `unload` | | `summary`, `state` | |
| `read` | `names`: `[name, ...]` | `values`: `{name: number}` | `Read` |
| `write` | `values`: `{name: number}` | | `Write` |
| `signal` | `stimulus`: one `io.stimulus` entry as JSON | | `SignalGenerator` |
| `capture_start` | `vars`, `start`, `stop`, `duration`, `max_rows` | `id` | `CreateCapture`, `Start` |
| `capture_fetch` | `id`, `from` (row, default 0), `max` (rows, default all) | `capture_state`, `n_rows`, `from`, `names`, `rows` | `Fetch` |
| `capture_stop` | `id` | | `Stop` |

`state` is `IDLE`, `LOADED`, `INITIALIZED`, `RUNNING`, `STOPPED` or `FAULT`; `fault` says why a
run went to `FAULT`.

**reset**: from `STOPPED` or `FAULT` back to `INITIALIZED`, to run the same scenario again. It
unloads the run and loads its scenario anew: a fresh FMU from t = 0 and the scenario's stimuli.
The log continues: the new run's rows are appended after the old ones, without a second header,
and `n` and `t` count on from where the ended run stopped, so the file stays one rising timeline
(the run's own time, in `state` and captures, starts again from 0). Values written and
signals set during the run are gone; the reply's `summary` is the ended run's, as `unload` gives
it. Refused in any other state. If the reload fails, the session is `IDLE`.

**load**: the scenario format is [scenario-reference.md](scenario-reference.md). Leave out
`model.duration` to run until `stop`. Paths in the scenario are on the server, relative to the
directory the service was started in.

**read** and **write** follow the session's rules: any Float64 variable can be read; only the
scenario's inputs and tunable parameters can be written. While running, each takes effect at the
next step boundary and the reply comes after it has. `write` stops at the first refused value;
the values before it have been written.

**signal** replaces the value or signal of one input, from the start of the run when
`INITIALIZED`, from the next step when `RUNNING`. The object has the keys of an `io.stimulus`
entry:

```json
{"op": "signal", "stimulus": {"input": "duty", "loops": 0, "segments": [
  {"kind": "ramp", "to": 0.8, "duration": 1},
  {"kind": "const", "value": 0.2, "duration": 1}]}}
```

**capture_start** records scenario inputs and outputs between two triggers:

| Key | Meaning |
|-----|---------|
| `vars` | names among the scenario's inputs and outputs (default: all of them) |
| `start` | `{"var", "op", "value"}` with `op` one of `>`, `>=`, `<`, `<=`: recording starts at the first step where it holds (default: at once) |
| `stop` | the same form: recording ends with the first recorded step where it holds (that row is kept) |
| `duration` | s: recording ends this long after it started (default: no limit) |
| `max_rows` | buffer size (default: `duration / step + 1`, or 100000) |

A capture is `ARMED`, `RUNNING` or `FINISHED`; when the run ends all are `FINISHED`. Fetch returns
`names` (`t`, then `vars`) and `rows` (`[t, v1, ...]`), `t` being the time after the step. Fetch
large captures in pages with `from` and `max`. Up to 8 captures per load.
