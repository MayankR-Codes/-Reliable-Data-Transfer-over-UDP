#!/usr/bin/env python3
"""
benchmark.py
============
Author : Mayank Rana
Day    : 6 — Automated Benchmarking & Evaluation Suite

Executes automated benchmarks of Reliable UDP protocols (SW, GBN, SR)
across a spectrum of simulated network packet loss rates:
  Loss rates: 0%, 5%, 10%, 15%, 20%, 25%

Measures:
  1. Transfer Time (seconds)
  2. Effective Goodput (KB/s)
  3. Total Retransmissions
  4. 100% SHA-256 Data Integrity Verification

Outputs results to results/benchmark_data.csv.
"""

import os
import sys
import time
import subprocess
import hashlib
import csv
import argparse

EMULATOR_PORT = 8000
RECEIVER_PORT = 9001
DEFAULT_FILE_SIZE_KB = 50  # 50 KB for rapid, consistent testing
LOSS_RATES = [0, 5, 10, 15, 20, 25]
PROTOCOLS = ["sw", "gbn", "sr"]

def compute_sha256(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def create_test_file(filepath, size_bytes):
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, "wb") as f:
        # Pseudo-random deterministic byte stream
        f.write(bytes((i * 17 + 31) & 0xFF for i in range(size_bytes)))

_run_counter = 0

def run_single_transfer(protocol, loss_rate, in_path, out_path, window=8):
    global _run_counter
    _run_counter += 1
    emu_port = 8100 + (_run_counter % 50)
    recv_port = 9100 + (_run_counter % 50)

    if os.path.exists(out_path):
        os.remove(out_path)

    # 1. Start Receiver
    rx_cmd = [
        os.path.abspath("receiver.exe"),
        "--port", str(recv_port),
        "--output", out_path,
        "--protocol", protocol,
        "--window", str(window)
    ]
    rx_proc = subprocess.Popen(rx_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)  # Allow receiver socket to bind

    # 2. Start Emulator
    emu_cmd = [
        os.path.abspath("emulator.exe"),
        "--listen", str(emu_port),
        "--recv-port", str(recv_port),
        "--loss", str(loss_rate),
        "--delay", "5"  # 5ms realistic base network delay
    ]
    emu_proc = subprocess.Popen(emu_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)

    # 3. Run Sender
    tx_cmd = [
        os.path.abspath("sender.exe"),
        "--file", in_path,
        "--ip", "127.0.0.1",
        "--port", str(emu_port),
        "--protocol", protocol,
        "--window", str(window)
    ]

    t_start = time.time()
    tx_proc = subprocess.run(tx_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=45)
    t_elapsed = time.time() - t_start

    # Clean up background processes
    try:
        rx_proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        rx_proc.terminate()
        rx_proc.wait()

    try:
        emu_proc.terminate()
        emu_proc.wait()
    except Exception:
        pass

    tx_output = tx_proc.stdout

    # Parse transfer time, goodput, retransmissions from sender output
    transfer_time = t_elapsed
    goodput = 0.0
    retransmits = tx_output.count("[TIMEOUT]")

    for line in tx_output.splitlines():
        if "Elapsed Time" in line and ":" in line:
            try:
                transfer_time = float(line.split(":")[1].replace("seconds", "").strip())
            except ValueError:
                pass
        elif "Goodput" in line and ":" in line:
            try:
                goodput = float(line.split(":")[1].replace("KB/s", "").strip())
            except ValueError:
                pass
        elif "Retransmissions :" in line or "Retransmits   :" in line:
            try:
                retransmits = int(line.split(":")[1].strip())
            except ValueError:
                pass

    if goodput == 0.0 and transfer_time > 0:
        goodput = (os.path.getsize(in_path) / 1024.0) / transfer_time

    # Verify SHA-256 integrity
    integrity_ok = False
    if os.path.exists(out_path):
        in_hash = compute_sha256(in_path)
        out_hash = compute_sha256(out_path)
        integrity_ok = (in_hash == out_hash)

    return {
        "success": (tx_proc.returncode == 0 and integrity_ok),
        "time": transfer_time,
        "goodput": goodput,
        "retransmits": retransmits,
        "integrity": integrity_ok
    }

def main():
    parser = argparse.ArgumentParser(description="Reliable UDP Benchmark Suite")
    parser.add_argument("--size-kb", type=int, default=DEFAULT_FILE_SIZE_KB, help="Test file size in KB")
    parser.add_argument("--runs", type=int, default=2, help="Repetitions per data point")
    parser.add_argument("--protocols", nargs="+", default=PROTOCOLS, help="Protocols to test (sw, gbn, sr)")
    parser.add_argument("--loss-rates", nargs="+", type=int, default=LOSS_RATES, help="Loss rates in %%")
    args = parser.parse_args()

    in_file = os.path.abspath("test_data/benchmark_input.bin")
    out_file = os.path.abspath("test_data/benchmark_out.bin")
    file_size_bytes = args.size_kb * 1024

    print("================================================================")
    print("   AUTOMATED BENCHMARK SUITE — Reliable Data Transfer over UDP  ")
    print("================================================================")
    print(f"File Size        : {args.size_kb} KB ({file_size_bytes} bytes)")
    print(f"Protocols        : {', '.join(args.protocols).upper()}")
    print(f"Loss Rates (%)   : {args.loss_rates}")
    print(f"Runs per Point   : {args.runs}")
    print("================================================================\n")

    create_test_file(in_file, file_size_bytes)
    base_hash = compute_sha256(in_file)
    print(f"[PREPARE] Test payload created: {in_file}")
    print(f"[PREPARE] Reference SHA-256   : {base_hash}\n")

    os.makedirs("results", exist_ok=True)
    csv_path = "results/benchmark_data.csv"

    results = []

    for proto in args.protocols:
        print(f"\n--- Benchmarking Protocol: {proto.upper()} ---")
        for loss in args.loss_rates:
            times = []
            goodputs = []
            retrans = []

            for r in range(args.runs):
                res = run_single_transfer(proto, loss, in_file, out_file)
                if res["success"]:
                    times.append(res["time"])
                    goodputs.append(res["goodput"])
                    retrans.append(res["retransmits"])
                    status_str = "OK (SHA-256 Matched)"
                else:
                    status_str = "FAIL"
                time.sleep(0.2)

            avg_time = sum(times) / len(times) if times else 0.0
            avg_gp   = sum(goodputs) / len(goodputs) if goodputs else 0.0
            avg_rt   = sum(retrans) / len(retrans) if retrans else 0.0

            print(f"  [{proto.upper():3s}] Loss={loss:2d}% | "
                  f"Avg Time: {avg_time:6.2f}s | "
                  f"Avg Goodput: {avg_gp:7.2f} KB/s | "
                  f"Avg Retrans: {avg_rt:5.1f} | "
                  f"Integrity: {status_str}")

            results.append({
                "protocol": proto.upper(),
                "loss_rate": loss,
                "transfer_time_sec": round(avg_time, 3),
                "goodput_kbps": round(avg_gp, 2),
                "retransmissions": round(avg_rt, 1)
            })

    # Save to CSV
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["protocol", "loss_rate", "transfer_time_sec", "goodput_kbps", "retransmissions"])
        writer.writeheader()
        writer.writerows(results)

    print(f"\n[DONE] Benchmark complete! Results saved to: {csv_path}")

if __name__ == "__main__":
    main()
