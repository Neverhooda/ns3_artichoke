# artichoke

ns-3 simulation of a smart-meter wireless mesh: meters on a static grid report
readings to a concentrator (node 0) over 802.11a ad-hoc Wi-Fi. Compares the
AODV, OLSR and DSDV routing protocols, optionally with one node dropping out
mid-run.

## Running on ns-3.26 (Docker)

```bash
docker build -t artichoke-ns3:3.26 docker
docker run --rm -it -v "$PWD:/opt/ns-allinone-3.26/ns-3.26/scratch/artichoke" \
  -w /opt/ns-allinone-3.26/ns-3.26 artichoke-ns3:3.26 bash

./waf --run "artichoke --protocol=olsr --size=50"
```

Inside an existing ns-3.26 tree it works the old way:

```bash
cd ns3/scratch
git clone https://github.com/Neverhooda/artichoke.git artichoke
cd ../
./waf --run artichoke
```

## Options

| Option | Default | Meaning |
|---|---|---|
| `--protocol` | `aodv` | `aodv`, `olsr` or `dsdv` |
| `--traffic` | `meters` | `meters`: every node reports to node 0; `ping`: node 0 pings the last node |
| `--size` / `--step` / `--gridWidth` | 50 / 100 m / 5 | grid layout |
| `--interval` / `--packetSize` | 1 s / 128 B | meter reporting rate |
| `--time` / `--warmup` | 60 s / 10 s | simulation length, delay before meters start |
| `--failNode` / `--failAt` | middle node / time/3 | node moved out of range; `-1` disables |
| `--run` | 1 | RNG run number |
| `--anim` / `--animFile` / `--icon` | on | NetAnim XML output and node icon |
| `--csv` / `--nodeCsv` | off | append a summary row / write per-meter stats |
| `--pcap` / `--printRoutes` | off / on | PCAP traces, routing table dump at 8 s |

## Parameter sweep

Inside the container:

```bash
bash scratch/artichoke/experiments/sweep.sh
python3 scratch/artichoke/experiments/plot.py
```

`PROTOCOLS`, `SIZES`, `RUNS`, `TIME` and `JOBS` environment variables override
the sweep grid. Results land in `experiments/results/`: `summary.csv`,
`comparison.png` (delivery ratio, delay and routing overhead against network
size) and `pdr_map.png` (per-meter delivery on the 50-node grid with the
middle node failed).
