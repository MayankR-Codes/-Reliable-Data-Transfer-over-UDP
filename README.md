# Reliable Data Transfer over UDP

A complete, production-grade reliable file transfer system built from scratch on top of UDP sockets in C. The project implements application-layer transport reliability with **Stop-and-Wait (SW)**, **Go-Back-N (GBN)**, and **Selective Repeat (SR)** ARQ protocols, alongside an asynchronous **Network Channel Emulator** that simulates packet loss, latency delay, jitter, and bit corruption.

---

## 1. System Architecture

```text
               +-------------------------------------------------+
               |            Channel Emulator (Proxy)             |
               |  --loss <%>  --delay <ms>  --corrupt <%>        |
               |  - Asynchronous propagation delay queue         |
               |  - CRC32 bit corruption & drop simulation       |
               +-----------------------+-------------------------+
                                       ^
                           Forward     |      Forward
                           DATA Pkts   |      ACK Pkts
                                       v
+------------------------+      UDP Datagrams        +------------------------+
|       UDP Sender       +-------------------------->+      UDP Receiver      |
|  ./sender.exe          |                           |  ./receiver.exe        |
|  - File chunking (1KB) |<--------------------------+  - Packet validation   |
|  - ARQ retransmission  |      Acknowledgements     |  - Out-of-order buffer |
|  - 200ms socket timer  |       (Cum / Select)      |  - Circular receiver   |
|  - SHA-256 hash verify |                           |  - File reassembly     |
|                        |                           |  - SHA-256 hash verify |
+------------------------+                           +------------------------+
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
   - Sender transmits 1 chunk and blocks waiting for ACK with a fixed 200 ms timeout (`SO_RCVTIMEO`).
   - Ideal for low-latency, zero-loss links; throughput collapses when latency or loss is introduced.

2. **Go-Back-N (GBN):**
   - Sliding window of size $N$ (e.g., $N=8$).
   - Pipelined packet transmission using cumulative ACKs (`expected_seq`).
   - Single base timer (200 ms). Upon timeout, retransmits the **entire unacknowledged window** $[base \dots next\_seq - 1]$.
   - Receiver accepts strictly in-order packets; discards out-of-order packets and re-ACKs the last in-order sequence.

3. **Selective Repeat (SR):**
   - Sliding window of size $N$ with sequence number space $\ge 2N$.
   - Independent per-packet 200 ms timer for every packet in flight.
   - Receiver circular buffer stores out-of-order packets and returns individual selective ACKs.
   - Retransmits **only** the specific packet that timed out, preventing redundant window retransmissions under loss.

---

## 4. Channel Emulator Features

The emulator (`emulator.exe`) acts as a UDP network proxy between sender and receiver:
- `--listen <port>`: Proxy incoming UDP port (default: 8000).
- `--recv-port <port>`: Forwarding destination port (default: 9001).
- `--loss <percent>`: Probabilistic DATA packet drop rate (e.g., `--loss 15` for 15%).
- `--loss-ack <percent>`: Independent ACK packet drop rate.
- `--delay <ms>`: Artificial network latency added before forwarding packets.
- `--jitter <ms>`: Random delay variation ($\pm jitter$ ms).
- `--corrupt <percent>`: Bit corruption that randomly inverts bits in a payload byte, triggering CRC32 checksum mismatch at the receiver.
- `--seed <number>`: Deterministic random seed for reproducible benchmark experiments.

---

## 5. Build & Compilation Instructions

### Prerequisites
- GCC Compiler (MinGW on Windows or GCC on Linux)
- Python 3 with `matplotlib` (for automated benchmarks and plotting)

### Compilation
Build all binaries and test suites using `mingw32-make` (or `make`):
```bash
# Build all production executables and test runners
mingw32-make all

# Run the complete automated test suite (all 13 test executables)
mingw32-make test
```

### Generated Binaries
- `sender.exe` — Unified file sender CLI (supports `--protocol <sw|gbn|sr>`)
- `receiver.exe` — Unified file receiver CLI (supports `--protocol <sw|gbn|sr>`)
- `emulator.exe` — Network impairment proxy

---

## 6. Complete Automated Test Suite (13 Tests)

Every module is rigorously verified with dedicated unit and end-to-end integration tests:

| Test Binary | Author / Day | Scope & Verification |
| :--- | :--- | :--- |
| `test_packet.exe` | Manisha & Raushan (Day 1) | Checksum integrity, bit-flips, multi-byte corruptions, header tampering |
| `test_loopback.exe` | Manisha & Raushan (Day 1) | UDP socket loopback, bi-directional ACK roundtrips, payload integrity |
| `test_file_io.exe` | Dimple (Day 2) | 1KB chunk splitting, file reassembly, native SHA-256 hashing |
| `test_emulator.exe` | Mayank (Day 2) | Proxy UDP datagram forwarding between client and server |
| `test_loss.exe` | Mayank (Day 3) | Simulated 25% packet drop rate statistical accuracy |
| `test_delay.exe` | Mayank (Day 3) | Artificial propagation delay (50 ms) & jitter verification |
| `test_buffer.exe` | Dimple (Day 3 & 4) | Sender ring buffer & receiver circular buffer out-of-order slots |
| `test_sw.exe` | Manisha & Raushan (Day 2) | Stop-and-Wait 5KB smoke test + 5MB file transfer (SHA-256 match) |
| `test_gbn.exe` | Manisha (Day 3 & 4) | Go-Back-N lossless windowing (N=4, 8, 16) + 15% packet loss recovery |
| `test_corrupt.exe` | Mayank (Day 4) | Bit-level payload corruption detection and retransmission |
| `test_sr.exe` | Raushan (Day 4) | Selective Repeat windowing + 15% packet loss recovery with out-of-order buffering |
| `test_stress.exe` | Manisha & Raushan (Day 5) | GBN & SR under severe impairments (20% loss + 10% corrupt + 20ms delay), zero deadlocks |
| `test_delay_bench.exe` | Raushan (Day 3) | Side-by-side Stop-and-Wait vs. Go-Back-N delay throughput benchmark |

---

## 7. Command-Line Reference

### Receiver CLI
```bash
./receiver.exe --port <port> --output <filepath> --protocol <sw|gbn|sr> [--window <N>]
```
- Example:
  ```bash
  ./receiver.exe --port 9001 --output received.bin --protocol sr --window 8
  ```

### Channel Emulator CLI
```bash
./emulator.exe --listen <listen_port> --recv-port <recv_port> [--loss <%>] [--delay <ms>] [--corrupt <%>]
```
- Example (10% Loss, 20ms Delay, 5% Corruption):
  ```bash
  ./emulator.exe --listen 8000 --recv-port 9001 --loss 10 --delay 20 --corrupt 5
  ```

### Sender CLI
```bash
./sender.exe --file <filepath> --ip <dest_ip> --port <port> --protocol <sw|gbn|sr> [--window <N>]
```
- Example:
  ```bash
  ./sender.exe --file sample.bin --ip 127.0.0.1 --port 8000 --protocol sr --window 8
  ```

---

## 8. Live Demonstration Guide

To demonstrate the full 3-node topology live during evaluation, open 3 terminal windows:

**Terminal 1 — Receiver:**
```bash
./receiver.exe --port 9001 --output test_data/demo_out.bin --protocol sr --window 8
```

**Terminal 2 — Channel Emulator (15% Packet Loss, 20ms Delay):**
```bash
./emulator.exe --listen 8000 --recv-port 9001 --loss 15 --delay 20
```

**Terminal 3 — Sender:**
```bash
./sender.exe --file test_data/benchmark_input.bin --ip 127.0.0.1 --port 8000 --protocol sr --window 8
```

**Verify Cryptographic Integrity:**
```bash
certutil -hashfile test_data/benchmark_input.bin SHA256
certutil -hashfile test_data/demo_out.bin SHA256
```
Both outputs will match identically.

---

## 9. Benchmark Results & Comparative Analysis

The automated benchmark suite (`mingw32-make benchmark`) evaluates all three protocols on a 50 KB file across packet loss rates from $0\%$ to $25\%$.

### Summary Table

| Protocol | Loss Rate (%) | Transfer Time (s) | Effective Goodput (KB/s) | Avg Retransmissions | SHA-256 Integrity |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Stop-and-Wait** | 0% | 1.60 s | 31.21 KB/s | 0.0 | **MATCH (100%)** |
| **Stop-and-Wait** | 5% | 2.02 s | 24.80 KB/s | 2.0 | **MATCH (100%)** |
| **Stop-and-Wait** | 10% | 2.73 s | 18.36 KB/s | 5.5 | **MATCH (100%)** |
| **Stop-and-Wait** | 15% | 3.12 s | 16.02 KB/s | 7.5 | **MATCH (100%)** |
| **Stop-and-Wait** | 20% | 4.35 s | 11.50 KB/s | 13.5 | **MATCH (100%)** |
| **Stop-and-Wait** | 25% | 5.38 s | 9.39 KB/s | 18.5 | **MATCH (100%)** |
| **Go-Back-N** | 0% | 0.25 s | **200.00 KB/s** | 0.0 | **MATCH (100%)** |
| **Go-Back-N** | 5% | 0.45 s | 138.11 KB/s | 2.0 | **MATCH (100%)** |
| **Go-Back-N** | 10% | 1.02 s | 49.97 KB/s | 7.0 | **MATCH (100%)** |
| **Go-Back-N** | 15% | 1.77 s | 28.18 KB/s | 14.0 | **MATCH (100%)** |
| **Go-Back-N** | 20% | 2.84 s | 18.04 KB/s | 24.0 | **MATCH (100%)** |
| **Go-Back-N** | 25% | 3.98 s | 15.63 KB/s | 34.0 | **MATCH (100%)** |
| **Selective Repeat** | 0% | 0.25 s | **200.00 KB/s** | 0.0 | **MATCH (100%)** |
| **Selective Repeat** | 5% | 0.42 s | 118.48 KB/s | 2.5 | **MATCH (100%)** |
| **Selective Repeat** | 10% | 0.72 s | **70.79 KB/s** | 4.5 | **MATCH (100%)** |
| **Selective Repeat** | 15% | 1.32 s | **38.35 KB/s** | 9.0 | **MATCH (100%)** |
| **Selective Repeat** | 20% | 1.73 s | **29.09 KB/s** | 12.5 | **MATCH (100%)** |
| **Selective Repeat** | 25% | 2.58 s | **20.88 KB/s** | 25.5 | **MATCH (100%)** |

---

### Comparative Evaluation Visualizations

#### 1. Effective Goodput Comparison
![Goodput Comparison](results/goodput_comparison.png)

#### 2. Total Retransmissions Comparison
![Retransmissions Comparison](results/retransmissions_comparison.png)

#### 3. Transfer Time Comparison
![Transfer Time Comparison](results/transfer_time_comparison.png)

---

### Core Theoretical Conclusions
1. **Pipelining Advantage (0% Loss):**
   - Both Go-Back-N and Selective Repeat achieve **200.00 KB/s**, running **6.4x faster** than Stop-and-Wait (31.21 KB/s), proving that keeping $N=8$ packets in-flight saturates the channel bandwidth-delay product.
2. **Selective Retransmission Resilience (10% – 25% Loss):**
   - At 10% packet loss, Selective Repeat achieves **70.79 KB/s**, compared to GBN's **49.97 KB/s** and SW's **18.36 KB/s**.
   - At 25% packet loss, Go-Back-N triggers **34 retransmissions** due to full window discarding, whereas Selective Repeat requires only **25.5 retransmissions**, maintaining higher goodput (**20.88 KB/s** vs. **15.63 KB/s**).
3. **Half-Duplex Penalty in Stop-and-Wait:**
   - Stop-and-Wait transfer time scales linearly with network delay and loss ($1.60\text{ s} \to 5.38\text{ s}$), because each packet loss incurs an idle 200 ms timeout period with zero packets in transit.

---

## 10. Technical Viva Cheat Sheet

### Dimple Mohapatra (Foundation, Packet Structure & Buffers)
- **Q: Why implement reliability over UDP instead of TCP?**
  - *A:* TCP handles reliability in the OS kernel. By implementing ARQ, sequencing, timeouts, and checksums over UDP at the application layer, we can control windowing mechanics, inject channel impairments, and benchmark protocol behavior.
- **Q: Why use a 32-bit CRC checksum instead of the standard 16-bit UDP checksum?**
  - *A:* UDP checksums are optional on some IPv4 stacks and only 16 bits. CRC32 provides stronger Hamming distance error detection against bit corruptions simulated by our channel proxy.

### Manisha Behera (Stop-and-Wait & Go-Back-N)
- **Q: Why does Go-Back-N performance degrade steeply under high packet loss?**
  - *A:* GBN uses cumulative ACKs. If packet $k$ is lost, the receiver discards all subsequent packets ($k+1, \dots, k+N-1$), forcing the sender to retransmit the entire window from $k$ onwards, multiplying retransmissions.
- **Q: What is the sequence number requirement for Go-Back-N?**
  - *A:* For a window size $N$, sequence numbers must satisfy $N \le 2^k - 1$ to avoid window overlap ambiguity.

### Raushan Kumar (Selective Repeat & Out-of-Order Buffering)
- **Q: How does Selective Repeat prevent redundant retransmissions?**
  - *A:* The receiver maintains a circular buffer to accept and hold out-of-order packets. It acknowledges packets individually with selective ACKs. The sender tracks timers per packet and retransmits only the specific packet that timed out.
- **Q: Why does Selective Repeat require sequence number space $\ge 2N$?**
  - *A:* If sequence space is $< 2N$, a delayed ACK from the previous window could be misinterpreted as an ACK for a packet in the new window, causing data corruption.

### Mayank Rana (Channel Emulator & Benchmarks)
- **Q: How does the channel emulator simulate network latency without slowing down the host?**
  - *A:* The emulator uses an asynchronous event-driven delay queue with non-blocking `select()`. Incoming datagrams are stamped with departure deadlines (`now + delay`) and queued without blocking subsequent packet receptions.
- **Q: What does the Goodput vs. Loss graph demonstrate?**
  - *A:* At 0% loss, sliding window protocols maximize throughput. As loss rates increase, Selective Repeat outperforms Go-Back-N because its retransmissions scale linearly with packet loss, whereas GBN's retransmissions multiply with window size.
