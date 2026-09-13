#!/usr/bin/env python3
"""
Reads the kernel MLFQ log (produced by the MLFQLOG(...) prints in kernel/proc.c) and draws a
timeline / scatter plot:
    X-axis : time in ticks since scheduler start
    Y-axis : queue ID (0 = highest priority .. 3 = lowest)
    colour : one colour per process (PID)
"""

import sys
import re
import matplotlib
matplotlib.use("Agg")            # write to a file, no screen needed
import matplotlib.pyplot as plt

WATERMARK = "radhika.sahu"      
BOOST_INTERVAL = 48             

# One log line, e.g.  [MLFQ] t=12 pid=4 q=1 ev=DEMOTE
LINE_RE = re.compile(r"\[MLFQ\]\s+t=(\d+)\s+pid=(\d+)\s+q=(\d+)\s+ev=(\w+)")


def parse_log(path):
    #Return {pid: [(t, q), ...]} and the max tick seen
    per_pid = {}
    max_t = 0
    with open(path) as f:
        for line in f:
            m = LINE_RE.search(line)
            if not m:
                continue
            t, pid, q, ev = int(m[1]), int(m[2]), int(m[3]), m[4]
            per_pid.setdefault(pid, []).append((t, q))
            max_t = max(max_t, t)
    return per_pid, max_t


def main():
    log_path = sys.argv[1] if len(sys.argv) > 1 else "mlfq.log"
    per_pid, max_t = parse_log(log_path)

    if not per_pid:
        print(f"No [MLFQ] lines found in '{log_path}'. "
              f"Did you build with LOG=1 and run schedulertest?")
        sys.exit(1)

    fig, ax = plt.subplots(figsize=(11, 5))

    # One colour per PID; a step line shows movement, markers show each event.
    cmap = plt.get_cmap("tab10")
    for i, pid in enumerate(sorted(per_pid)):
        pts = sorted(per_pid[pid])
        xs = [t for t, _ in pts]
        ys = [q for _, q in pts]
        colour = cmap(i % 10)
        ax.step(xs, ys, where="post", color=colour, alpha=0.6, linewidth=1.2)
        ax.scatter(xs, ys, color=colour, s=28, label=f"PID {pid}", zorder=3)

    # Vertical dashed lines at every priority boost (multiples of 48 ticks).
    for boost in range(BOOST_INTERVAL, max_t + 1, BOOST_INTERVAL):
        ax.axvline(boost, color="red", linestyle="--", alpha=0.35, linewidth=1)
    # Label the first boost line only, so the legend stays clean.
    ax.axvline(BOOST_INTERVAL, color="red", linestyle="--", alpha=0.35,
               linewidth=1, label="priority boost (48 ticks)")

    ax.set_xlabel("time elapsed (ticks since scheduler start)")
    ax.set_ylabel("queue ID  (0 = highest priority)")
    ax.set_title("xv6 MLFQ: process movement between queues over time")
    ax.set_yticks([0, 1, 2, 3])
    ax.invert_yaxis()             # queue 0 (highest priority) at the top
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper right", fontsize=8, framealpha=0.9)

    plt.text(0.95, 0.95, WATERMARK,
             ha="right", va="top",
             transform=plt.gca().transAxes,
             fontsize=10, color="gray", alpha=0.7)

    out = "mlfq_timeline.png"
    plt.tight_layout()
    plt.savefig(out, dpi=150)
    print(f"Saved {out}  ({sum(len(v) for v in per_pid.values())} events, "
          f"{len(per_pid)} processes, up to tick {max_t})")


if __name__ == "__main__":
    main()