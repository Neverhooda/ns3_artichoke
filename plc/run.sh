#!/bin/bash
set -euo pipefail

: "${NS3_DIR:?run inside the artichoke-ns3:3.26-fast-repair container}"
RUNS="${RUNS:-10}"
INTERVALS="${INTERVALS:-1 2 3 4 5}"
JOBS="${JOBS:-$(nproc)}"

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-$HERE/results}"
rm -rf "$OUT"
mkdir -p "$OUT/raw"

A=--ns3::aodv::RoutingProtocol
CONFIGS="baseline|
tuned|$A::NetTraversalTime=1s $A::PathDiscoveryTime=2s $A::RreqRetries=1
fast-repair|--fastRepair"
if [ -n "${EXTRA_CONFIGS:-}" ]; then
  CONFIGS="$CONFIGS
$EXTRA_CONFIGS"
fi

cd "$NS3_DIR"
./waf build >/dev/null
BIN="$(find build/scratch/plc -maxdepth 1 -type f -perm -u+x | head -1)"
export LD_LIBRARY_PATH="$NS3_DIR/build"

job() {
  local tag="$1" label="$2" interval="$3" run="$4"; shift 4
  echo "$BIN --label=$label --interval=$interval --run=$run $* --csv=$OUT/raw/$tag.summary.csv --meterCsv=$OUT/raw/$tag.meters.csv --timelineCsv=$OUT/raw/$tag.timeline.csv"
}

{
  while IFS='|' read -r label args; do
    for r in $(seq 1 "$RUNS"); do
      job "configs_${label}_$r" "$label" 1 "$r" $args
      if [ "$label" != tuned ] && [ "$label" != fast-repair ]; then
        for i in $INTERVALS; do
          job "interval_${label}_${i}_$r" "$label" "$i" "$r" --time=150 $args
        done
      fi
    done
  done <<< "$CONFIGS"
} | xargs -P "$JOBS" -I{} sh -c '{} >/dev/null 2>&1 || echo "FAILED: {}" >&2'

for kind in configs interval; do
  for part in summary meters timeline; do
    files=("$OUT"/raw/${kind}_*."$part".csv)
    head -1 "${files[0]}" > "$OUT/$kind.$part.csv"
    tail -q -n +2 "${files[@]}" >> "$OUT/$kind.$part.csv"
  done
done
echo "results -> $OUT"
