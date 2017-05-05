#!/bin/bash
set -euo pipefail

: "${NS3_DIR:?run inside the artichoke-ns3 container}"
PROTOCOLS="${PROTOCOLS:-aodv olsr dsdv}"
SIZES="${SIZES:-10 25 50 75 100}"
RUNS="${RUNS:-3}"
TIME="${TIME:-60}"
JOBS="${JOBS:-$(nproc)}"

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-$HERE/results}"
rm -rf "$OUT/raw" "$OUT/nodes"
mkdir -p "$OUT/raw" "$OUT/nodes"

cd "$NS3_DIR"
./waf build >/dev/null
BIN="$(find build/scratch/artichoke -maxdepth 1 -type f -perm -u+x | head -1)"
export LD_LIBRARY_PATH="$NS3_DIR/build"

{
  for p in $PROTOCOLS; do
    for n in $SIZES; do
      for r in $(seq 1 "$RUNS"); do
        for f in -1 -2; do
          tag="${p}_n${n}_r${r}_f${f}"
          echo "$BIN --protocol=$p --size=$n --run=$r --failNode=$f --time=$TIME --anim=false --printRoutes=false --csv=$OUT/raw/$tag.csv --nodeCsv=$OUT/nodes/$tag.csv"
        done
      done
    done
  done
} | xargs -P "$JOBS" -I{} sh -c '{} >/dev/null 2>&1 || echo "FAILED: {}" >&2'

head -1 "$(ls "$OUT"/raw/*.csv | head -1)" > "$OUT/summary.csv"
tail -q -n +2 "$OUT"/raw/*.csv >> "$OUT/summary.csv"
echo "$(($(wc -l < "$OUT/summary.csv") - 1)) runs -> $OUT/summary.csv"
