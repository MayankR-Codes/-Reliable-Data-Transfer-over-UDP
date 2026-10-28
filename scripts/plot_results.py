#!/usr/bin/env python3
"""
plot_results.py
===============
Author : Mayank Rana
Day    : 6 — Matplotlib Comparative Visualizations

Reads benchmark results from results/benchmark_data.csv and generates
three publication-quality comparison charts:
  1. results/goodput_comparison.png
  2. results/retransmissions_comparison.png
  3. results/transfer_time_comparison.png
"""

import os
import csv
import matplotlib.pyplot as plt

CSV_PATH = "results/benchmark_data.csv"
OUTPUT_DIR = "results"

def load_data(csv_file):
    if not os.path.exists(csv_file):
        raise FileNotFoundError(f"Benchmark CSV not found at '{csv_file}'. Run benchmark.py first.")

    data = {}
    with open(csv_file, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            proto = row["protocol"].strip()
            if proto not in data:
                data[proto] = {
                    "loss": [],
                    "time": [],
                    "goodput": [],
                    "retrans": []
                }
            data[proto]["loss"].append(float(row["loss_rate"]))
            data[proto]["time"].append(float(row["transfer_time_sec"]))
            data[proto]["goodput"].append(float(row["goodput_kbps"]))
            data[proto]["retrans"].append(float(row["retransmissions"]))
    return data

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    data = load_data(CSV_PATH)

    colors = {"SW": "#E63946", "GBN": "#457B9D", "SR": "#2A9D8F"}
    markers = {"SW": "s", "GBN": "^", "SR": "o"}
    labels = {
        "SW": "Stop-and-Wait (SW)",
        "GBN": "Go-Back-N (GBN, N=8)",
        "SR": "Selective Repeat (SR, N=8)"
    }

    # -------------------------------------------------------------
    # 1. Goodput vs Loss Rate
    # -------------------------------------------------------------
    plt.figure(figsize=(9, 5.5), dpi=150)
    for proto in ["SW", "GBN", "SR"]:
        if proto in data:
            plt.plot(data[proto]["loss"], data[proto]["goodput"],
                     label=labels.get(proto, proto),
                     color=colors.get(proto, "black"),
                     marker=markers.get(proto, "o"),
                     linewidth=2.2, markersize=7)

    plt.title("Protocol Goodput vs. Packet Loss Rate", fontsize=14, fontweight="bold", pad=12)
    plt.xlabel("Simulated Packet Loss Rate (%)", fontsize=11, fontweight="bold")
    plt.ylabel("Effective Goodput (KB/sec)", fontsize=11, fontweight="bold")
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend(frameon=True, facecolor="#F8F9FA", edgecolor="#CED4DA", fontsize=10)
    plt.tight_layout()
    out1 = os.path.join(OUTPUT_DIR, "goodput_comparison.png")
    plt.savefig(out1)
    plt.close()
    print(f"[PLOT] Generated: {out1}")

    # -------------------------------------------------------------
    # 2. Retransmissions vs Loss Rate
    # -------------------------------------------------------------
    plt.figure(figsize=(9, 5.5), dpi=150)
    for proto in ["SW", "GBN", "SR"]:
        if proto in data:
            plt.plot(data[proto]["loss"], data[proto]["retrans"],
                     label=labels.get(proto, proto),
                     color=colors.get(proto, "black"),
                     marker=markers.get(proto, "o"),
                     linewidth=2.2, markersize=7)

    plt.title("Retransmissions vs. Packet Loss Rate", fontsize=14, fontweight="bold", pad=12)
    plt.xlabel("Simulated Packet Loss Rate (%)", fontsize=11, fontweight="bold")
    plt.ylabel("Total Retransmission Count", fontsize=11, fontweight="bold")
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend(frameon=True, facecolor="#F8F9FA", edgecolor="#CED4DA", fontsize=10)
    plt.tight_layout()
    out2 = os.path.join(OUTPUT_DIR, "retransmissions_comparison.png")
    plt.savefig(out2)
    plt.close()
    print(f"[PLOT] Generated: {out2}")

    # -------------------------------------------------------------
    # 3. Elapsed Transfer Time vs Loss Rate
    # -------------------------------------------------------------
    plt.figure(figsize=(9, 5.5), dpi=150)
    for proto in ["SW", "GBN", "SR"]:
        if proto in data:
            plt.plot(data[proto]["loss"], data[proto]["time"],
                     label=labels.get(proto, proto),
                     color=colors.get(proto, "black"),
                     marker=markers.get(proto, "o"),
                     linewidth=2.2, markersize=7)

    plt.title("Total Transfer Time vs. Packet Loss Rate", fontsize=14, fontweight="bold", pad=12)
    plt.xlabel("Simulated Packet Loss Rate (%)", fontsize=11, fontweight="bold")
    plt.ylabel("Elapsed Transfer Time (seconds)", fontsize=11, fontweight="bold")
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend(frameon=True, facecolor="#F8F9FA", edgecolor="#CED4DA", fontsize=10)
    plt.tight_layout()
    out3 = os.path.join(OUTPUT_DIR, "transfer_time_comparison.png")
    plt.savefig(out3)
    plt.close()
    print(f"[PLOT] Generated: {out3}")

    print("\n[DONE] All 3 comparison plots generated successfully in results/ directory!")

if __name__ == "__main__":
    main()
