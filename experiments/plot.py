import csv
import os
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

here = os.path.dirname(os.path.abspath(__file__))
results = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "results")

rows = list(csv.DictReader(open(os.path.join(results, "summary.csv"))))
groups = defaultdict(list)
for r in rows:
    failed = int(r["failNode"]) >= 0
    groups[(r["protocol"], failed, int(r["nodes"]))].append(r)

protocols = sorted({k[0] for k in groups})
colors = {"aodv": "#2a6fdb", "olsr": "#e07b28", "dsdv": "#3a9e5c"}
metrics = [
    ("pdr", "Packet delivery ratio, %"),
    ("meanDelayMs", "Mean end-to-end delay, ms"),
    ("ctrlBytes", "Routing overhead, bytes sent"),
]


def mean_std(values):
    m = sum(values) / len(values)
    s = (sum((v - m) ** 2 for v in values) / len(values)) ** 0.5
    return m, s


fig, axes = plt.subplots(1, len(metrics), figsize=(16, 4.8))
for ax, (key, label) in zip(axes, metrics):
    for p in protocols:
        for failed in (False, True):
            sizes = sorted(n for (pp, ff, n) in groups if pp == p and ff == failed)
            if not sizes:
                continue
            stats = [mean_std([float(r[key]) for r in groups[(p, failed, n)]]) for n in sizes]
            ax.errorbar(sizes, [m for m, _ in stats], yerr=[s for _, s in stats],
                        color=colors.get(p), linestyle="--" if failed else "-",
                        marker="o", capsize=3,
                        label="%s%s" % (p.upper(), " + node failure" if failed else ""))
    ax.set_xlabel("Number of meters")
    ax.set_ylabel(label)
    ax.grid(alpha=0.3)
axes[0].legend(fontsize=8)
fig.suptitle("Smart-meter mesh: AODV vs OLSR vs DSDV (6 Mbit/s 802.11a ad-hoc, 100 m grid)")
fig.tight_layout()
fig.savefig(os.path.join(results, "comparison.png"), dpi=120)

nodes_dir = os.path.join(results, "nodes")
size = 50
step, grid_width = 100.0, 5
fig, axes = plt.subplots(len(protocols), 1, figsize=(10, 3.4 * len(protocols)), squeeze=False)
axes = axes[:, 0]
for ax, p in zip(axes, protocols):
    path = os.path.join(nodes_dir, "%s_n%d_r1_f-2.csv" % (p, size))
    if not os.path.exists(path):
        ax.set_visible(False)
        continue
    pts = list(csv.DictReader(open(path)))
    meters = [r for r in pts if r["node"] != "0" and float(r["x"]) < 1e4]
    gone = [int(r["node"]) for r in pts if float(r["x"]) >= 1e4]
    sc = ax.scatter([float(r["x"]) for r in meters], [float(r["y"]) for r in meters],
                    c=[float(r["pdr"]) for r in meters], cmap="RdYlGn", vmin=50, vmax=100,
                    s=180, edgecolors="black")
    ax.scatter([0], [0], marker="s", s=240, color="black", label="concentrator")
    for i in gone:
        ax.scatter([(i // grid_width) * step], [(i % grid_width) * step], marker="x",
                   s=160, color="red", linewidths=2.5, label="failed node")
    ax.set_title("%s: per-meter delivery, %d meters, middle node failed" % (p.upper(), size))
    ax.set_aspect("equal")
    ax.set_xlim(-60, (size // grid_width) * step - step + 60)
    ax.set_ylim(-60, (grid_width - 1) * step + 60)
    ax.set_ylabel("y, m")
    ax.legend(loc="center left", bbox_to_anchor=(1.01, 0.5), fontsize=8, scatterpoints=1)
axes[-1].set_xlabel("x, m")
fig.subplots_adjust(right=0.72, hspace=0.35)
fig.colorbar(sc, ax=axes.tolist(), label="PDR, % (clipped at 50)", shrink=0.8, pad=0.26)
fig.savefig(os.path.join(results, "pdr_map.png"), dpi=120)
print("plots written to", results)
