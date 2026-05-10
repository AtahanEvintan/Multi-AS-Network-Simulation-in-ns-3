#!/usr/bin/env python3
"""
aggregate_results.py
====================
Reads summary.csv produced by multi_as_sim, computes mean ± std
across repeated runs, and generates 4 plots + a formatted summary table.

Usage:
    python3 aggregate_results.py --resultsDir /path/to/results/TIMESTAMP
"""

import argparse
import os
import sys
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ── CLI ─────────────────────────────────────────────────────────────
parser = argparse.ArgumentParser(description="Aggregate Multi-AS simulation results")
parser.add_argument("--resultsDir", required=True, help="Path to results directory containing summary.csv")
parser.add_argument("--outDir",     default=None,  help="Where to save plots (default: resultsDir/analysis)")
args = parser.parse_args()

results_dir = args.resultsDir
out_dir = args.outDir or os.path.join(results_dir, "analysis")
os.makedirs(out_dir, exist_ok=True)

csv_path = os.path.join(results_dir, "summary.csv")
if not os.path.exists(csv_path):
    print(f"ERROR: {csv_path} not found.")
    sys.exit(1)

# ── Load ─────────────────────────────────────────────────────────────
df = pd.read_csv(csv_path)
print(f"Loaded {len(df)} rows from {csv_path}")
print(df.to_string(index=False))

# ── Aggregate: mean ± std per (totalNodes, dist) ─────────────────────
agg = df.groupby(["totalNodes", "dist"]).agg(
    delay_mean    = ("delay_ms",        "mean"),
    delay_std     = ("delay_ms",        "std"),
    tput_mean     = ("throughput_mbps", "mean"),
    tput_std      = ("throughput_mbps", "std"),
    loss_mean     = ("loss_pct",        "mean"),
    loss_std      = ("loss_pct",        "std"),
    conv_mean     = ("convergence_s",   "mean"),
    conv_std      = ("convergence_s",   "std"),
).reset_index()

# Fill NaN std (single-run scenarios) with 0
agg = agg.fillna(0)

# Save aggregated table
agg_path = os.path.join(out_dir, "aggregated.csv")
agg.to_csv(agg_path, index=False, float_format="%.4f")
print(f"\nAggregated table saved: {agg_path}")
print(agg.to_string(index=False))

# ── Plotting helpers ──────────────────────────────────────────────────
COLORS = {"balanced": "#2196F3", "unbalanced": "#FF9800"}
MARKERS = {"balanced": "o", "unbalanced": "s"}
NODE_COUNTS = sorted(agg["totalNodes"].unique())

def make_plot(metric_mean, metric_std, ylabel, title, filename, ylim=None):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for dist, grp in agg.groupby("dist"):
        grp = grp.sort_values("totalNodes")
        ax.errorbar(grp["totalNodes"], grp[metric_mean], yerr=grp[metric_std],
                    label=dist.capitalize(),
                    color=COLORS[dist], marker=MARKERS[dist],
                    linewidth=2, markersize=7, capsize=5,
                    linestyle="-" if dist == "balanced" else "--")
    ax.set_xlabel("Number of Nodes", fontsize=12)
    ax.set_ylabel(ylabel, fontsize=12)
    ax.set_title(title, fontsize=13, fontweight="bold")
    ax.set_xticks(NODE_COUNTS)
    ax.legend(fontsize=11)
    ax.grid(True, linestyle="--", alpha=0.5)
    if ylim:
        ax.set_ylim(ylim)
    plt.tight_layout()
    path = os.path.join(out_dir, filename)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"Saved: {path}")

# ── Generate 4 plots ──────────────────────────────────────────────────
make_plot("delay_mean",  "delay_std",
          "Mean End-to-End Delay (ms)",
          "Mean End-to-End Delay (ms)\nvs. Node Count by Distribution",
          "delay.png")

make_plot("tput_mean",   "tput_std",
          "Mean Throughput (Mbps)",
          "Mean Throughput (Mbps)\nvs. Node Count by Distribution",
          "throughput.png")

make_plot("loss_mean",   "loss_std",
          "Packet Loss (%)",
          "Packet Loss (%)\nvs. Node Count by Distribution",
          "packet_loss.png")

make_plot("conv_mean",   "conv_std",
          "OSPF Convergence Time (s)",
          "OSPF Convergence Time (s)\nvs. Node Count by Distribution",
          "convergence.png")

# ── Print formatted summary table ─────────────────────────────────────
print("\n" + "=" * 75)
print(f"{'Nodes':>6}  {'Dist':>11}  {'Delay (ms)':>14}  {'Tput (Mbps)':>14}  "
      f"{'Loss (%)':>12}  {'Conv (s)':>10}")
print("-" * 75)
for _, row in agg.iterrows():
    print(f"{int(row.totalNodes):>6}  {row.dist:>11}  "
          f"{row.delay_mean:>6.2f} ± {row.delay_std:>5.2f}  "
          f"{row.tput_mean:>6.3f} ± {row.tput_std:>5.3f}  "
          f"{row.loss_mean:>6.4f} ± {row.loss_std:>6.4f}  "
          f"{row.conv_mean:>5.2f} ± {row.conv_std:>4.2f}")
print("=" * 75)
print(f"\nAll outputs written to: {out_dir}")
