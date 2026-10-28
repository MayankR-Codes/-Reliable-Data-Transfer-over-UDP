# Reliable Data Transfer over UDP

A complete, production-grade reliable file transfer system built from scratch on top of UDP sockets in C. The project implements application-layer transport reliability with **Stop-and-Wait (SW)**, **Go-Back-N (GBN)**, and **Selective Repeat (SR)** ARQ protocols, alongside a configurable **Network Channel Emulator** that simulates packet loss, latency delay, jitter, and bit corruption.

---

## 1. System Architecture

```text
               +-------------------------------------------+
               |          Channel Emulator (Proxy)         |
               |  --loss <%>  --delay <ms>  --corrupt <%>  |
               +---------------------+---------------------+
                                     ^
                         Forward     |      Forward
                         DATA Pkts   |      ACK Pkts
                                     v
+------------------------+      UDP Datagrams      +------------------------+
|      UDP Sender        +------------------------>+      UDP Receiver      |
|  ./sender              |                         |  ./receiver            |
|  - File chunking       |<------------------------+  - Packet validation   |
|  - ARQ retransmissions |     Acknowledgements    |  - Out-of-order buffer |
|  - Fixed 200ms timers  |      (Cum / Select)     |  - File reassembly     |
|  - SHA-256 hash verify |                         |  - SHA-256 hash verify |
+------------------------+                         +------------------------+
```

---

## 2. Team Members & Module Ownership

| Member | Primary Ownership | Core Modules & Deliverables |
| :--- | :--- | :--- |
| **Mayank Rana** | **Channel Emulator, CLI Binaries, Benchmarks & Visualizations** | `src/emulator.c`, `src/sender.c`, `src/receiver.c`, `Makefile`, `scripts/benchmark.py`, `scripts/plot_results.py`, `README.md` |
| **Dimple Mohapatra** | **Foundation, Packet Structure, File I/O & Buffers** | `include/packet.h`, `src/packet.c`, `include/socket_utils.h`, `src/socket_utils.c`, `include/file_io.h`, `src/file_io.c`, `include/buffer.h` |
| **Manisha Behera** | **Stop-and-Wait & Go-Back-N Protocols** | `include/protocols.h`, `src/stop_and_wait.c`, `src/go_back_n.c`, sliding window cumulative ACKs |
| **Raushan Kumar** | **Selective Repeat Protocol & Buffering** | `src/selective_repeat.c`, receiver circular buffer, per-packet timers & selective ACKs |

---

## 3. Supported ARQ Protocols

1. **Stop-and-Wait (SW):**
   - 1-bit alternating sequence number ($0 \leftrightarrow 1$).
   - Sender transmits 1 chunk and blocks waiting for ACK with a fixed 200 ms timeout.
   - Ideal for low-latency, zero-loss links; throughput degrades on delayed or lossy channels.

2. **Go-Back-N (GBN):**
   - Sliding window of size $N$ (e.g., $N=8$).
   - Pipelined packet transmission using cumulative ACKs.
   - Single base timer (200 ms). Upon timeout, retransmits the **entire unacknowledged window** $[base \dots next\_seq - 1]$.
   - Receiver accepts only in-order packets; discards out-of-order packets and re-ACKs the last in-order sequence.

3. **Selective Repeat (SR):**
   - Sliding window of size $N$ with sequence number space $\ge 2N$.
   - Independent per-packet 200 ms timer for every packet in flight.
   - Receiver buffers out-of-order packets and returns individual selective ACKs.
   - Retransmits **only** the specific packet that timed out, preventing redundant window retransmissions under loss.

---

## 4. Channel Emulator Features

The emulator (`emulator.exe`) acts as a UDP network proxy between sender and receiver:
- `--loss <percent>`: Probabilistic DATA packet drop rate (e.g., `--loss 15` for 15%).
- `--loss-ack <percent>`: Independent ACK drop rate.
- `--delay <ms>`: Artificial network latency added before forwarding packets.
- `--jitter <ms>`: Random delay variation ($\pm jitter$ ms).
- `--corrupt <percent>`: Bit corruption that randomly inverts bits in a payload byte, triggering CRC32 checksum mismatch at the receiver.
- `--seed <number>`: Deterministic random seed for reproducible benchmark experiments.

---

## 5. Build & Compilation Instructions

### Prerequisites
- GCC Compiler (MinGW on Windows or GCC on Linux)
- Python 3 with `matplotlib` (for benchmarking and plotting)

### Compilation
Build all binaries and test suites using `mingw32-make` (or `make`):
```bash
# Build all executables
mingw32-make all

# Run all automated test suites
mingw32-make test
```

This compiles:
- `sender.exe` — Unified file sender CLI
- `receiver.exe` — Unified file receiver CLI
- `emulator.exe` — Network impairment proxy

---

## 6. Command-Line Reference

### Receiver
```bash
./receiver.exe --port <port> --output <filepath> --protocol <sw|gbn|sr> [--window <N>]
```
- Example:
  ```bash
  ./receiver.exe --port 9001 --output received.bin --protocol sr --window 8
  ```

### Channel Emulator
```bash
./emulator.exe --listen <listen_port> --recv-port <recv_port> [--loss <%>] [--delay <ms>] [--corrupt <%>]
```
- Example (10% Loss, 20ms Delay, 5% Corruption):
  ```bash
  ./emulator.exe --listen 8000 --recv-port 9001 --loss 10 --delay 20 --corrupt 5
  ```

### Sender
```bash
./sender.exe --file <filepath> --ip <dest_ip> --port <port> --protocol <sw|gbn|sr> [--window <N>]
```
- Example:
  ```bash
  ./sender.exe --file sample.bin --ip 127.0.0.1 --port 8000 --protocol sr --window 8
  ```

---

## 7. End-to-End Demo Guide

Run the 3-node topology across 3 separate terminal sessions:

**Terminal 1 — Receiver:**
```bash
./receiver.exe --port 9001 --output test_data/demo_out.bin --protocol gbn --window 8
```

**Terminal 2 — Emulator (15% Loss, 10ms Delay):**
```bash
./emulator.exe --listen 8000 --recv-port 9001 --loss 15 --delay 10
```

**Terminal 3 — Sender:**
```bash
./sender.exe --file test_data/benchmark_input.bin --ip 127.0.0.1 --port 8000 --protocol gbn --window 8
```

Verify SHA-256 hash matching between source and received file:
```bash
certutil -hashfile test_data/benchmark_input.bin SHA256
certutil -hashfile test_data/demo_out.bin SHA256
```

---

## 8. Automated Benchmarks & Visualizations

Run the automated test matrix across all protocols (SW, GBN, SR) and loss rates ($0\%, 5\%, 10\%, 15\%, 20\%, 25\%$):

```bash
mingw32-make benchmark
```

This runs `scripts/benchmark.py` and `scripts/plot_results.py`, generating 3 Matplotlib plots in `results/`:
- `results/goodput_comparison.png` — Goodput (KB/s) vs. Packet Loss Rate (%)
- `results/retransmissions_comparison.png` — Retransmissions vs. Packet Loss Rate (%)
- `results/transfer_time_comparison.png` — Elapsed Transfer Time vs. Packet Loss Rate (%)

### Core Findings & Comparison
- **0% Loss:** GBN and SR achieve near-identical maximum throughput due to pipelining; Stop-and-Wait is significantly slower due to half-duplex blocking.
- **Moderate to High Loss (10% – 25%):**
  - **Selective Repeat** maintains high resilience because only dropped packets are retransmitted.
  - **Go-Back-N** experiences retransmission multiplication because dropping packet $k$ causes the entire window from $k$ onwards to be discarded and retransmitted.
  - **Stop-and-Wait** goodput drops steeply as each packet loss incurs a 200 ms timeout idle delay.

---

## 9. Verification & Data Integrity

Every transfer validates 100% data integrity:
- Packet-level CRC32 checksums detect bit-level corruptions in transit.
- Native SHA-256 cryptographic hashing verifies complete bit-for-bit file reconstruction after transfer.
