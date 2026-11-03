#!/usr/bin/env bash
# The closed loop on this PC: a controller's own code (controllers/<x>/host) against its plant FMU
# in the runner, over a pty pair from socat. Linux or WSL.
#
#   tools/hil/pc_loop.sh KIND [--live] [--check] [--scenario FILE] [-- ARGS...]
#
#   motor        dcmotor_hil_pc: speed setpoint 3000 rpm, a load torque step at 0.5 s
#   motor-sensor the encoder is lost at 0.5 s: the controller must trip (sensor timeout)
#   motor-link   the controller stops answering at 0.5 s: the runner must fault after max_late
#   t12          t12tip_hil_pc: cold start to 350 degC, a joint touched at 10 s
#   t12-open|-short|-stuck  the tip opens, shorts, or the heater switch sticks at 3 s: must trip
#   t12-runaway  more heat loss than the heater makes up from 6 s: must trip (thermal runaway)
#   --live    the controller on its own 1 kHz clock and the runner paced (the *_live scenario)
#             instead of lockstep; WSL is not real time, so expect late steps
#   --check   check the run against the controller's acceptance figures (check_loop.py)
#   --scenario FILE  another runner scenario on the same link; ARGS go to the controller after
#             its default setpoint and start, e.g. -- -c "sp 0" --at 0.1 "sp 3000"
#
# Logs: runner/runs/<scenario>.csv (the runner) and runner/runs/<scenario>_ctrl.csv (the
# controller's telemetry). KRUMPIR_RUNNER and KRUMPIR_CONTROLLER override the programs (default:
# the dev build of tools/kb.py, <build>/runner/krumpir and <build>/runner/host-<x>/<x>_host);
# KRUMPIR_XCP_PORT serves XCP on that port (a build with -DKRUMPIR_XCP=ON). The older names
# KRUMPIR, CONTROLLER and XCP_PORT are still read.
# Exit status: 0 ok, 1 a figure missed (--check), 2 usage or the run faulted, 77 no socat.
set -euo pipefail

usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-2}"; }
case ${1:-} in -h | --help) usage 0 ;; esac
[ $# -ge 1 ] || usage
kind=$1
shift
live=0 check=0 scenario=
while [ $# -gt 0 ]; do
  case $1 in
    --live) live=1 ;;
    --check) check=1 ;;
    --scenario) scenario=$2; shift ;;
    --) shift; break ;;
    *) usage ;;
  esac
  shift
done
extra=("$@") # after --: more controller arguments, after the default setpoint and start
case $kind in
  motor) ctrl=motor scn=dcmotor_hil_pc sp=3000 ;;
  motor-sensor) ctrl=motor scn=dcmotor_sensor_hil_pc sp=3000 ;;
  motor-link) ctrl=motor scn=dcmotor_link_hil_pc sp=3000; extra+=(--mute-at 0.5) ;;
  t12) ctrl=t12 scn=t12tip_hil_pc sp=350 ;;
  t12-open | t12-short | t12-stuck | t12-runaway) ctrl=t12 scn=t12tip_${kind#t12-}_hil_pc sp=350 ;;
  *) usage ;;
esac
mode=--lockstep
if [ $live = 1 ]; then
  scn=${scn}_live mode="--hz 1000"
fi
scenario=$(realpath "${scenario:-$(dirname "$0")/../../runner/scenarios/$scn.toml}")
scn=$(basename "$scenario" .toml)

command -v socat >/dev/null || { echo "pc_loop: socat is not installed (apt install socat)"; exit 77; }
here=$(cd "$(dirname "$0")" && pwd)
runner=$(cd "$here/../../runner" && pwd)
# The build folder as tools/kb.py picks it
repo=$(cd "$here/../.." && pwd)
build=${KRUMPIR_BUILD_ROOT:-$([[ $repo == /mnt/* ]] && echo "$HOME/build/krumpir" || echo "$repo/build")}
build=$build/dev/runner
runner_bin=${KRUMPIR_RUNNER:-${KRUMPIR:-$build/krumpir}}
controller_bin=${KRUMPIR_CONTROLLER:-${CONTROLLER:-$build/host-$ctrl/${ctrl}_host}}
xcp_port=${KRUMPIR_XCP_PORT:-${XCP_PORT:-}}

# The pty pair: the scenarios name /tmp/krumpir-hil/runner, the controller gets the other end
dir=/tmp/krumpir-hil
mkdir -p "$dir" "$runner/runs"
rm -f "$dir/runner" "$dir/controller"
socat pty,raw,echo=0,link="$dir/controller" pty,raw,echo=0,link="$dir/runner" 2>/dev/null &
spid=$!
cpid=
trap 'kill $cpid $spid 2>/dev/null || true' EXIT
for _ in $(seq 50); do
  [ -e "$dir/runner" ] && [ -e "$dir/controller" ] && break
  sleep 0.05
done

# shellcheck disable=SC2086 # $mode is two words for --live
"$controller_bin" --device "$dir/controller" $mode ${xcp_port:+--xcp $xcp_port} --log -c "sp $sp" \
  -c start "${extra[@]}" </dev/null >"$runner/runs/${scn}_ctrl.csv" 2>"$dir/controller.err" &
cpid=$!
cd "$runner"
rc=0
"$runner_bin" run "$scenario" || rc=$? # 2: the run faulted
sleep 0.2 # the controller writes its last rows on BYE
cp "$dir/controller.err" "runs/${scn}_ctrl.err" # its trips: "trip: <name> at tick <n>"
cat "$dir/controller.err" >&2
[ $check = 1 ] || exit $rc
python3 "$here/check_loop.py" "$kind" "runs/${scn}_ctrl.csv" "runs/$scn.csv" --rc $rc
