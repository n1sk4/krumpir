#!/usr/bin/env bash
# The closed loop on this PC: a controller's own code (controllers/<x>/host) against its plant FMU
# in the runner, over a pty pair from socat. Linux or WSL.
#
#   tools/hil/pc_loop.sh motor|t12|t12-open [--live] [--check]
#
#   motor     dcmotor_hil_pc: speed setpoint 3000 rpm, a load torque step at 0.5 s
#   t12       t12tip_hil_pc: cold start to 350 degC, a joint touched at 10 s
#   t12-open  t12tip_open_hil_pc: heating, then the tip goes open at 3 s: the controller must trip
#   --live    the controller on its own 1 kHz clock and the runner paced (the *_live scenario)
#             instead of lockstep; WSL is not real time, so expect late steps
#   --check   check the run against the controller's acceptance figures (check_loop.py)
#
# Logs: runner/runs/<scenario>.csv (the runner) and runner/runs/<scenario>_ctrl.csv (the
# controller's telemetry). KRUMPIR and CONTROLLER override the programs (default: ~/build/krumpir/
# krumpir and ~/build/host-<x>/<x>_host, built with cmake -S controllers/<dir>/host); XCP_PORT
# serves XCP on that port (a build with -DKRUMPIR_XCP=ON). Exits 77 when socat is missing.
set -euo pipefail

usage() { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 1 ] || usage
kind=$1
shift
live=0 check=0
for a in "$@"; do
  case $a in
    --live) live=1 ;;
    --check) check=1 ;;
    *) usage ;;
  esac
done
case $kind in
  motor) ctrl=motor scn=dcmotor_hil_pc sp=3000 ;;
  t12) ctrl=t12 scn=t12tip_hil_pc sp=350 ;;
  t12-open) ctrl=t12 scn=t12tip_open_hil_pc sp=350 ;;
  *) usage ;;
esac
mode=--lockstep
if [ $live = 1 ]; then
  scn=${scn}_live mode="--hz 1000"
fi

command -v socat >/dev/null || { echo "pc_loop: socat is not installed (apt install socat)"; exit 77; }
here=$(cd "$(dirname "$0")" && pwd)
runner=$(cd "$here/../../runner" && pwd)
KRUMPIR=${KRUMPIR:-$HOME/build/krumpir/krumpir}
CONTROLLER=${CONTROLLER:-$HOME/build/host-$ctrl/${ctrl}_host}

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
"$CONTROLLER" --device "$dir/controller" $mode ${XCP_PORT:+--xcp $XCP_PORT} --log -c "sp $sp" -c start </dev/null \
  >"$runner/runs/${scn}_ctrl.csv" 2>"$dir/controller.err" &
cpid=$!
cd "$runner"
"$KRUMPIR" run "scenarios/$scn.toml"
sleep 0.2 # the controller writes its last rows on BYE
cat "$dir/controller.err" >&2
[ $check = 1 ] || exit 0
python3 "$here/check_loop.py" "$kind" "runs/${scn}_ctrl.csv" "runs/$scn.csv"
