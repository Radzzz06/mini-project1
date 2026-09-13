"""
Per process:
    turnaround = etime - ctime
    response   = stime - ctime
    waiting    = turnaround - rtime
"""

import sys
import re
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

WATERMARK = "radhika.sahu"        
NAME_FILTER = "schedulertest"    

METRIC_RE = re.compile(
    r"\[METRIC\]\s+pid=(\d+)\s+name=(\S+)\s+ctime=(-?\d+)\s+stime=(-?\d+)\s+etime=(-?\d+)\s+rtime=(-?\d+)"
)

def averages(path, children_only):
    """Return (avg_turnaround, avg_waiting, avg_response) for one log file."""
    rows = []
    with open(path) as f:
        for line in f:
            m = METRIC_RE.search(line)
            if not m:
                continue
            name = m[2]
            ctime, stime, etime, rtime = int(m[3]), int(m[4]), int(m[5]), int(m[6])
            if NAME_FILTER not in name or stime < 0:
                continue
            turnaround = etime - ctime
            response = stime - ctime
            waiting = turnaround - rtime
            rows.append((turnaround, waiting, response))

    if not rows:
        return None
    if children_only and len(rows) > 1:
        rows.sort(key=lambda r: r[0], reverse=True)   # drop longest turnaround (the parent)
        rows = rows[1:]

    n = len(rows)
    return (sum(r[0] for r in rows) / n,
            sum(r[1] for r in rows) / n,
            sum(r[2] for r in rows) / n)


def main():
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    children_only = "--children-only" in sys.argv
    if not files:
        print("usage: python3 plot_compare.py [--children-only] fifo.txt rr.txt mlfq.txt")
        sys.exit(1)

    labels, data = [], []
    for path in files:
        avg = averages(path, children_only)
        if avg is None:
            print(f"warning: no [METRIC] lines in {path}, skipping")
            continue
        labels.append(path.replace("_perf.txt", "").replace(".txt", "").replace(".log", ""))
        data.append(avg)

    if not data:
        print("No usable data found. Did you build with PERF=1 and run schedulertest?")
        sys.exit(1)

    metrics = ["Turnaround", "Waiting", "Response"]
    x = range(len(metrics))
    n = len(labels)
    width = 0.8 / n                      # bars share each metric group

    fig, ax = plt.subplots(figsize=(9, 5.5))
    cmap = plt.get_cmap("Set2")
    for i, (label, avg) in enumerate(zip(labels, data)):
        offsets = [xi + (i - (n - 1) / 2) * width for xi in x]
        bars = ax.bar(offsets, avg, width, label=label, color=cmap(i))
        for b in bars:                   # value label on top of each bar
            ax.text(b.get_x() + b.get_width() / 2, b.get_height(),
                    f"{b.get_height():.0f}", ha="center", va="bottom", fontsize=8)

    ax.set_xticks(list(x))
    ax.set_xticklabels(metrics)
    ax.set_ylabel("average time (ticks)   —   lower is better")
    ax.set_title("Scheduler comparison: FIFO vs RR vs MLFQ")
    ax.legend(title="scheduler")
    ax.grid(True, axis="y", alpha=0.3)

    plt.text(0.95, 0.95, WATERMARK,
             ha="right", va="top",
             transform=plt.gca().transAxes,
             fontsize=10, color="gray", alpha=0.7)

    out = "scheduler_comparison.png"
    plt.tight_layout()
    plt.savefig(out, dpi=150)
    print(f"Saved {out}")
    for label, avg in zip(labels, data):
        print(f"  {label:<6} turnaround={avg[0]:.1f}  waiting={avg[1]:.1f}  response={avg[2]:.1f}")


if __name__ == "__main__":
    main()