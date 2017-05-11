import csv
import os
import sys
from collections import OrderedDict, defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

here = os.path.dirname(os.path.abspath(__file__))
results = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "results")
FAIL_AT = 30.0

names = {
    "baseline": "ns-3.26 AODV",
    "tuned": "tuned timers",
    "fast-repair": "patch:\nFastRepair",
}
palette = ["#c0392b", "#e08e0b", "#8e6bbf", "#2a6fdb", "#1b9e77", "#555555"]


def read(name):
    return list(csv.DictReader(open(os.path.join(results, name))))


def mean(values):
    return sum(values) / len(values)


def labels_in(rows):
    seen = OrderedDict()
    for key in ["baseline", "tuned", "fast-repair"]:
        if any(r["label"] == key for r in rows):
            seen[key] = True
    for r in rows:
        seen.setdefault(r["label"], True)
    return list(seen)


def name(label):
    return names.get(label, label)


meters = read("configs.meters.csv")
configs = labels_in(meters)
colors = dict(zip(configs, palette))

worst = defaultdict(float)
for r in meters:
    key = (r["label"], r["run"])
    worst[key] = max(worst[key], float(r["worst_gap_s"]))

fig, ax = plt.subplots(figsize=(2.2 * len(configs) + 2.5, 5))
for x, c in enumerate(configs):
    values = [v for (label, _), v in worst.items() if label == c]
    ax.bar(x, mean(values), width=0.6, color=colors[c], alpha=0.35, align="center")
    ax.scatter([x] * len(values), values, color=colors[c], s=30, zorder=3)
    ax.text(x + 0.33, mean(values), "%.1f s" % mean(values), va="center", fontsize=10)
ax.axhline(1.0, color="gray", linestyle="--", linewidth=1)
ax.text(len(configs) - 0.45, 1.25, "normal: one reading per second", ha="right",
        fontsize=8, color="gray")
ax.set_xticks(range(len(configs)))
ax.set_xticklabels([name(c) for c in configs])
ax.set_ylabel("Longest silence of any meter, s")
ax.set_title("Route recovery after a meter drops off\n24-node ring feeder, 10 runs each")
ax.set_xlim(-0.6, len(configs) - 0.4)
ax.set_ylim(0, 13)
ax.grid(axis="y", alpha=0.3)
fig.tight_layout()
fig.savefig(os.path.join(results, "recovery.png"), dpi=120)

timeline = defaultdict(list)
for r in read("configs.timeline.csv"):
    if r["run"] == "1":
        timeline[r["label"]].append(
            (int(r["second"]), 100.0 * int(r["meters_heard"]) / int(r["meters_alive"])))
fig, ax = plt.subplots(figsize=(10, 4.5))
for c in configs:
    pts = sorted(timeline[c])
    ax.step([p[0] for p in pts], [p[1] for p in pts], where="post", color=colors[c],
            linewidth=2, label=name(c).replace("\n", " "))
ax.axvline(FAIL_AT, color="black", linestyle=":", linewidth=1)
ax.text(FAIL_AT + 0.3, 5, "meter 3 drops off", fontsize=9)
ax.set_xlim(20, 55)
ax.set_ylim(0, 105)
ax.set_xlabel("Simulation time, s")
ax.set_ylabel("Meters heard by the concentrator, %")
ax.set_title("Who is on the air, second by second (run 1)")
ax.legend(loc="lower right", fontsize=9)
ax.grid(alpha=0.3)
fig.tight_layout()
fig.savefig(os.path.join(results, "timeline.png"), dpi=120)

worst = defaultdict(float)
interval_meters = read("interval.meters.csv")
for r in interval_meters:
    key = (r["label"], float(r["interval"]), r["run"])
    worst[key] = max(worst[key], float(r["worst_gap_s"]))
fig, ax = plt.subplots(figsize=(8, 4.5))
intervals = sorted({k[1] for k in worst})
for c in labels_in(interval_meters):
    excess = [mean([v - i for (label, ii, _), v in worst.items() if label == c and ii == i])
              for i in intervals]
    ax.plot(intervals, excess, marker="o", color=colors.get(c, "black"), linewidth=2,
            label=name(c).replace("\n", " "))
ax.set_xlabel("Meter reporting interval, s")
ax.set_ylabel("Extra silence beyond one interval, s")
ax.set_title("Outage vs reporting interval (mean of 10 runs)")
ax.set_xticks(intervals)
ax.set_ylim(bottom=0)
ax.legend(fontsize=9)
ax.grid(alpha=0.3)
fig.tight_layout()
fig.savefig(os.path.join(results, "interval.png"), dpi=120)
print("plots written to", results)
