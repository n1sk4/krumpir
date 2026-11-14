# Service protocol

`krumpir serve [[host:]port]` runs the model-access service: one simulation session driven over
TCP, the role of the model access port (MAPort) of ASAM XIL. Default address `127.0.0.1:7700`.

```sh
krumpir serve                 # local clients only
krumpir serve 0.0.0.0:7700    # reachable from the LAN (on the Pi)
```

There is no authentication. A client can load any FMU. The service writes a run's log only under
`runs/` (`log.file` must be under `runs/` and end in `.csv`; `load`, `check` and `save` refuse any
other). Listen on `127.0.0.1` or a trusted LAN only.

## Framing

One JSON object per line in each direction: a request, then its reply. One client at a time; a
second one gets `{"ok":false,"code":"busy","error":"another client is connected"}` and is
disconnected. The session belongs to the service, not to
the connection: a run keeps going after its client disconnects, and the next client finds it as
it was. SIGINT or SIGTERM stops the run, unloads it and ends the service.

Every request has `op`. An optional `tag` (any JSON value) is copied into the reply. Every reply
has `ok`; when it is `false`, `code` says what kind of failure it is, `error` says why in one line
of English, and nothing else is in the reply. A client branches on `code`, never on `error`:

| `code` | When |
|--------|------|
| `bad_request` | not JSON, no `op`, unknown `op`, a wrong or missing argument |
| `bad_state` | the op is not allowed in the session's state |
| `not_found` | no such variable, file, capture or FMU |
| `not_writable` | output, fixed parameter, unmapped input |
| `bad_scenario` | the scenario does not parse or does not match its FMU |
| `exists` | `save` without `overwrite` |
| `limit` | too many captures, too large a buffer, line or file |
| `timeout` | the loop did not answer |
| `fault` | the FMU or the I/O backend returned an error |
| `internal` | out of memory and the like |
| `busy` | another client is connected |

The set is closed; a client treats a code it does not know as `internal`. The rules for changing
the protocol, and for what a client may rely on, are in [../../docs/api-spec.md](../../docs/api-spec.md).

```json
{"op": "read", "names": ["speed"], "tag": 12}
{"tag": 12, "ok": true, "values": {"speed": 104.7}}
```

## Operations

| `op` | Request | Reply | XIL |
|------|---------|-------|-----|
| `info` | | `protocol` (1), `version` (the runner's build), `limits` (`{request_bytes, scenario_bytes, name_bytes, inputs, outputs, params, segments, captures, capture_vars, capture_bytes, files, reply_ms, clients}`), `state`, `fault` | |
| `load` | `scenario`: path on the server, or `toml`: the scenario text | `state` | `LoadConfiguration`, `Configure` |
| `init` | `fmu`: path on the server | `toml`: a starter scenario for it, as `krumpir init` prints it | |
| `variables` | | `variables`: `[{name, vr, type, causality, tunable, unit, start, description, min, max}]` (`unit`, `start`, `description` are `""` when the FMU leaves them out; `min`, `max` are numbers or `null`) | `GetVariableNames`, `GetVariableInfo` |
| `describe` | `scenario`: path on the server, `toml`: the text, or `fmu`: path on the server (its starter scenario) | `text`, `scenario` (as JSON), `variables` of its FMU, `problem` | |
| `check` | `toml`: the scenario text | | |
| `save` | `path`: `scenarios/<name>.toml`, `toml`: the text, `overwrite` (default false) | | |
| `start` | | `state` | `StartSimulation` |
| `stop` | | `state` | `StopSimulation` |
| `state` | | `state`, `fault`; once loaded also `fmu`, `scenario` (the path it was loaded from, `null` for text), `step`, `duration` (when set), `t`, `steps`, `overruns`, `max_step_ns`, `max_wake_ns`, `late` (steps the uart controller did not answer in time) of the run so far | `State` |
| `files` | | `scenarios`: `scenarios/*.toml`, `fmus`: `fmus/*.fmu` in the service's directory, sorted | |
| `text` | `path`: a `.toml` file on the server (up to 1 MiB), or a log `runs/<...>.csv` with `from` (line, default 0) and `max` (lines) | `text` | |
| `wait` | `timeout_ms` (default: forever) | `done`, `state` | |
| `reset` | | `summary` (of the run that ended), `state` | |
| `unload` | | `summary`, `state` | |
| `read` | `names`: `[name, ...]` | `values`: `{name: number}`, `t` (the time of the step it was read at) | `Read` |
| `write` | `values`: `{name: number}` | | `Write` |
| `signal` | `stimulus`: one `io.stimulus` entry as JSON | | `SignalGenerator` |
| `capture_start` | `vars`, `start`, `stop`, `duration`, `max_rows` | `id` | `CreateCapture`, `Start` |
| `capture_fetch` | `id`, `from` (row, default 0), `max` (rows, default all) | `capture_state`, `n_rows`, `from`, `names`, `rows` | `Fetch` |
| `capture_stop` | `id` | | `Stop` |
| `capture_free` | `id` | | |

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

**describe** lets a client show a scenario as a form without a TOML parser and without loading.
`scenario` has every key, defaults filled in: `model` (`fmu`, `step`, `duration`: null when the
run goes until stopped, `params`: `{name: number}`), `rt` (`wait`, `max_overruns`, `cpu`, `priority`
and `timing_gpio`: null when not set), `io` (`backend`, `device`: "" when not set, `baud`, `timeout_ms`,
`late`, `max_late`, `inputs`, `outputs`, `stimulus`: entries as the
`signal` op takes them) and `log` (`file`). `variables` are those of the FMU it names, as
`variables` gives them. When the text is not a valid scenario the reply has `text` and `problem`
(why) only; when the FMU cannot be opened it has `scenario` and `problem`. Allowed in any state.

**check** parses the text and checks it against its FMU as `load` would (variable names, types and
causalities, the stimuli's data files) without loading anything: no instance, no log file.
Allowed in any state, also while a run goes on.

**save** writes the text to `scenarios/<name>.toml` (no other folder, no `..`, no leading dot),
after the same check as `check` (which includes the `log.file` rule above). An existing file is
refused unless `overwrite` is true. The file
is written to `<path>.tmp` and renamed, so a reader never sees half of it.

**text** reads a scenario (`.toml`, up to 1 MiB) or, in pages, a log under `runs/` (`.csv`): the lines
from `from` on (the header is line 0), at most `max` and at most 1 MiB a page. The reply of a log
has `text`, `from`, `lines` (how many it holds) and `more` (false at the end of the file); a last
line that is still being written is left for the next page. A log stays on the server; this is how
a remote client reads it.

**read** and **write** follow the session's rules: any Float64 variable can be read; only the
scenario's inputs and tunable parameters can be written. While running, each takes effect at the
next step boundary and the reply comes after it has. `read` samples all its names at that one
boundary, so a poll of many variables costs one step and its values belong together. `write`
applies all its values at one boundary, or none when one is refused.

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
large captures in pages with `from` and `max`; a fetch costs only the rows it returns, so polling
with `from` set to the rows already held is cheap. Up to 8 captures per load: `capture_stop` ends
the recording and keeps the rows, `capture_free` discards the capture and gives its id back, and
`unload` and `reset` free all.
