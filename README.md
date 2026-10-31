# Reliable Data Transfer over UDP

An application-layer reliable file transfer system built from scratch in C on top of standard UDP sockets. This project implements and compares three core Automatic Repeat reQuest (ARQ) protocols — **Stop-and-Wait (SW)**, **Go-Back-N (GBN)**, and **Selective Repeat (SR)** — alongside an integrated **Network Channel Emulator** that simulates real-world network impairments including packet loss, artificial delay, and bit corruption.

---

## Table of Contents
1. [Project Overview & Core Motivation](#1-project-overview--core-motivation)
2. [Current Architecture & Packet Design](#2-current-architecture--packet-design)
3. [Source Code Directory & File Breakdown](#3-source-code-directory--file-breakdown)
4. [Team Work Division & Contributions](#4-team-work-division--contributions)
5. [Supported ARQ Protocols Explained](#5-supported-arq-protocols-explained)
6. [Compilation & Execution Guide](#6-compilation--execution-guide)
7. [Live 3-Terminal Demonstration Guide](#7-live-3-terminal-demonstration-guide)
8. [Automated Verification & Test Suite](#8-automated-verification--test-suite)
9. [Benchmark Insights & Visual Comparisons](#9-benchmark-insights--visual-comparisons)
10. [Future Scope & Potential Improvements](#10-future-scope--potential-improvements)

---

## 1. Project Overview & Core Motivation

Standard Internet communication relies on TCP for guaranteed, in-order file delivery. However, TCP implements all error control, sliding windows, and flow control deep inside the operating system kernel.

The primary objective of this project is to **demystify transport-layer reliability** by building reliable data transmission entirely in user-space in C over raw, unreliable UDP datagrams.

### What Happens in the Project:
1. **Sender:** Reads an arbitrary binary or text file, segments it into fixed chunks (up to 1024 bytes), serializes them into custom packets with sequence numbers and CRC32 checksums, and transmits them over UDP.
2. **Channel Emulator (Proxy):** Sits between sender and receiver. It probabilistically drops packets (`--loss`), delays them with artificial latency (`--delay`), or corrupts their payloads (`--corrupt`) to simulate real-world unreliable networks.
3. **Receiver:** Intercepts incoming datagrams, validates packet checksums, handles duplicate or out-of-order sequence arrivals, sends back ACK datagrams, reconstructs the file on disk, and verifies end-to-end data integrity using a cryptographic SHA-256 hash.

---

## 2. Current Architecture & Packet Design

```text
                  +----------------------------------------------+
                  |         Channel Emulator Proxy (Port 8000)   |
                  |  - Drops packets probabilistically (--loss)  |
                  |  - Introduces one-way delay       (--delay)  |
                  |  - Inverts payload bits           (--corrupt)|
                  +----------------------+-----------------------+
                                         ^
                             Forward     |      Forward
                             DATA Pkts   |      ACK Pkts
                                         v
+--------------------------+      UDP Datagrams       +--------------------------+
|   UDP Sender CLI         +------------------------->+   UDP Receiver CLI       |
|   (sender.exe)           |                          |   (receiver.exe)         |
|   - File segmentation    |<-------------------------+   - CRC32 verification   |
|   - Sliding window ARQ   |     Acknowledgements     |   - Out-of-order buffer  |
|   - Fixed 200ms timers   |      (Cum / Select)      |   - File reassembly      |
|   - Retransmission logic |                          |   - SHA-256 verification |
+--------------------------+                          +--------------------------+
```

### Packet Format (`include/packet.h`)
Every datagram exchanged across the network uses a unified binary packet layout (15-byte header + up to 1024 bytes payload):

| Field | Type | Size | Description |
| :--- | :--- | :---: | :--- |
| `type` | `uint8_t` | 1 Byte | `PACKET_DATA` (1), `PACKET_ACK` (2), or `PACKET_FIN` (3) |
| `seq_num` | `uint32_t` | 4 Bytes | Packet sequence number (0, 1, 2, ...) |
| `ack_num` | `uint32_t` | 4 Bytes | Acknowledged sequence number |
| `length` | `uint16_t` | 2 Bytes | Size of actual file payload in bytes (0 to 1024) |
| `checksum`| `uint32_t` | 4 Bytes | CRC32 checksum computed across header and payload |
| `payload` | `char[]` | Up to 1024 B | Raw sliced file data |

---

## 3. Source Code Directory & File Breakdown

The project follows a clean, modular structure separating protocol implementations, networking utilities, executable entry points, and test suites:

### Header Files (`include/`)
- [`packet.h`](include/packet.h): Defines the core `Packet` data structure, packet type constants (`DATA`, `ACK`, `FIN`), and function prototypes for serialization and CRC32 checksum validation.
- [`socket_utils.h`](include/socket_utils.h): Provides cross-platform UDP socket abstraction (Windows Winsock2 vs POSIX Berkeley sockets), non-blocking socket utilities, socket timeout configurations (`SO_RCVTIMEO`), and address helpers.
- [`file_io.h`](include/file_io.h): Prototypes for chunked file reading (`read_chunk`), safe sequential writing (`write_chunk`), file size measurement, and SHA-256 cryptographic hashing.
- [`buffer.h`](include/buffer.h): Window buffer abstractions including sender in-flight tracking slots and receiver circular buffers for out-of-order packet buffering.
- [`protocols.h`](include/protocols.h): Function prototypes for all sender and receiver protocol routines (`sw_sender`, `sw_receiver`, `gbn_sender`, `gbn_receiver`, `sr_sender`, `sr_receiver`).

### Source Files (`src/`)
- [`packet.c`](src/packet.c): Implements `init_packet()`, custom 32-bit CRC checksum calculation across struct fields, and `verify_packet_checksum()`.
- [`socket_utils.c`](src/socket_utils.c): Handles `WSAStartup`/`WSACleanup` initialization, UDP socket creation, binding, timeout handling, and `sendto`/`recvfrom` wrappers.
- [`file_io.c`](src/file_io.c): Implements chunk-based binary file reading, writing, and a self-contained cryptographic SHA-256 implementation used for end-to-end file comparison.
- [`stop_and_wait.c`](src/stop_and_wait.c): Implements the baseline Stop-and-Wait ARQ sender (blocks waiting for ACK with a 200 ms timeout, retransmits on timeout) and receiver (alternating bit validation, sends ACKs).
- [`go_back_n.c`](src/go_back_n.c): Implements Go-Back-N sliding window sender (window size $N$, single base timer, window-wide retransmission on timeout) and receiver (strictly cumulative ACKs, discards out-of-order frames).
- [`selective_repeat.c`](src/selective_repeat.c): Implements Selective Repeat sender (individual per-packet timers, selective retransmissions) and receiver (circular window buffer, accepts out-of-order packets, flushes contiguous ranges to disk).
- [`emulator.c`](src/emulator.c): Standalone channel proxy that intercepts datagrams between sender and receiver, applying user-configured loss rate, bit corruption, and network latency.
- [`sender.c`](src/sender.c): Unified sender CLI program that parses command-line arguments and dispatches file transmission through the selected protocol (`sw`, `gbn`, or `sr`).
- [`receiver.c`](src/receiver.c): Unified receiver CLI program that binds to the destination port and executes the matching ARQ receiver logic.

---

## 4. Team Work Division & Contributions

According to [`WORK_DIVISION.md`](WORK_DIVISION.md) and module ownership across the development sprint:

| Team Member | Primary Module Ownership | Core Contributions & Deliverables |
| :--- | :--- | :--- |
| **Dimple Mohapatra** | **Foundation, Packet Structure, File I/O & Buffers** | Authored `packet.h`, `packet.c`, `socket_utils.h`, `socket_utils.c`, `file_io.h`, `file_io.c`, CRC32 checksum engine, SHA-256 hashing utility, and `buffer.h` circular buffer structures. |
| **Manisha Behera** | **Stop-and-Wait & Go-Back-N Protocols** | Designed and implemented `stop_and_wait.c`, `go_back_n.c`, alternating 1-bit logic, GBN sliding window sender with cumulative ACK tracking, and timeout retransmission handling. |
| **Raushan Kumar** | **Selective Repeat Protocol & Buffer Management** | Implemented `selective_repeat.c`, per-packet timer tracking, individual selective ACKs, out-of-order receiver packet buffering, and in-order buffer flush algorithms. |
| **Mayank Rana** | **Channel Emulator, CLI Binaries & Project Architecture** | Developed `emulator.c` network proxy, unified CLI binaries (`sender.c`, `receiver.c`), build system (`Makefile`), automated benchmark data collection, and documentation. |

---

## 5. Supported ARQ Protocols Explained

### 1. Stop-and-Wait (SW)
- **Concept:** Half-duplex protocol. The sender sends exactly 1 packet and blocks waiting for an ACK.
- **Sequence Space:** Alternating 1-bit sequence number ($0 \leftrightarrow 1$).
- **Retransmission:** If no ACK arrives within the 200 ms timeout, the sender retransmits the single packet.
- **Pros & Cons:** Minimal memory footprint, but throughput collapses over links with high latency or packet loss.

### 2. Go-Back-N (GBN)
- **Concept:** Pipelined protocol with a sender window of size $N$ (default $N=8$). The sender keeps up to $N$ unacknowledged packets in flight.
- **Acknowledgements:** Strictly **cumulative ACKs** (`ack_num = k` means all packets up to $k$ have arrived safely).
- **Receiver Behavior:** Discards any out-of-order packet and re-sends cumulative ACK for the last in-order frame.
- **Retransmission:** Managed by a single base timer. When the timer expires, the sender retransmits the **entire window** $[base \dots next\_seq - 1]$.

### 3. Selective Repeat (SR)
- **Concept:** Advanced pipelining protocol. Both sender and receiver maintain a sliding window of size $N$.
- **Acknowledgements:** Individual **selective ACKs** (`ack_num = k` confirms arrival of only packet $k$).
- **Receiver Behavior:** Out-of-order packets falling inside the receive window are stored in a circular buffer. When the oldest unreceived packet arrives, all contiguous buffered packets are flushed to disk.
- **Retransmission:** The sender maintains per-packet timers and retransmits **only the specific dropped packet**, avoiding unnecessary duplicate transmissions.

---

## 6. Compilation & Execution Guide

### Prerequisites
- GCC Compiler (MinGW on Windows, or GCC on Linux)
- Make utility (`mingw32-make` on Windows or `make` on Linux)

### Building the Project
From the project root directory, run:
```bash
# Compile all production executables and unit test runners
mingw32-make all
```

This generates:
- `emulator.exe` — Network impairment proxy
- `sender.exe` — Unified file sender
- `receiver.exe` — Unified file receiver
- `test_*.exe` — Core test suite runners

To clean compiled objects and binaries:
```bash
mingw32-make clean
```

---

## 7. Live 3-Terminal Demonstration Guide

To demonstrate realistic network impairment recovery live to evaluators, open **three separate terminals** in the project directory:

### Step 1: Start the Receiver (Terminal 1)
Start the receiver first so it binds to port `9001` and waits for incoming packets:
```powershell
.\receiver.exe --port 9001 --output test_data/demo_received.bin --protocol sr --window 8
```

### Step 2: Start the Channel Emulator (Terminal 2)
Start the emulator proxy listening on port `8000`, forwarding to `9001`, with simulated **15% packet loss** and **20 ms latency**:
```powershell
.\emulator.exe --listen 8000 --recv-port 9001 --loss 15 --delay 20
```

### Step 3: Start the Sender (Terminal 3)
Transmit a file to the emulator's port (`8000`):
```powershell
.\sender.exe --file test_data/benchmark_input.bin --ip 127.0.0.1 --port 8000 --protocol sr --window 8
```

### What You Will Observe Across Terminals:
- **Terminal 2 (Emulator):** Prints `[EMULATOR DROP] >>> DROPPED DATA: Seq=...` as it intentionally drops datagrams.
- **Terminal 3 (Sender):** Emits `[TIMEOUT] Packet=... Retransmitting...` when the 200ms timer fires, recovering seamlessly.
- **Terminal 1 (Receiver):** Displays received packets, buffers out-of-order chunks, sends selective ACKs, and completes cleanly on FIN.

### Step 4: Verify Bit-for-Bit File Integrity
Verify that the output file is 100% identical to the source despite heavy packet drops:
```powershell
certutil -hashfile test_data/benchmark_input.bin SHA256
certutil -hashfile test_data/demo_received.bin SHA256
```
Both 64-character SHA-256 digests will match identically.

---

## 8. Automated Verification & Test Suite

The project includes 6 core unit and integration test suites validating each layer of the transport stack:

```bash
mingw32-make test
```

| Test Target | Component / Module | Scope & Verification |
| :--- | :--- | :--- |
| `test_packet.exe` | Checksum & Serialization | Verifies CRC32 calculation, single-bit flips, header tampering, and FIN frames. |
| `test_loopback.exe`| UDP Socket Layer | Tests datagram transmission, loopback binding, and bidirectional ACK roundtrips. |
| `test_file_io.exe` | File Chunking & Hash | Tests 1KB slicing, reassembly, and SHA-256 mismatch detection on corrupted files. |
| `test_sw.exe` | Stop-and-Wait ARQ | Executes end-to-end Stop-and-Wait file transfer and verifies SHA-256 match. |
| `test_gbn.exe` | Go-Back-N ARQ | Tests GBN across window sizes ($N=4, 8, 16$) and verifies 15% packet loss recovery. |
| `test_sr.exe` | Selective Repeat ARQ | Tests SR windowing and verifies out-of-order buffering under 15% packet loss. |

---

## 9. Benchmark Insights & Visual Comparisons

Comprehensive empirical benchmarks evaluated all three protocols across loss rates from $0\%$ to $25\%$:

### Benchmark Results Table

| Protocol | Loss Rate (%) | Transfer Time (s) | Effective Goodput (KB/s) | Retransmissions | SHA-256 Hash Match |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Stop-and-Wait (SW)** | 0% | 1.60 s | 31.21 KB/s | 0.0 | **PASS (100%)** |
| **Stop-and-Wait (SW)** | 10% | 2.73 s | 18.36 KB/s | 5.5 | **PASS (100%)** |
| **Stop-and-Wait (SW)** | 25% | 5.38 s | 9.39 KB/s | 18.5 | **PASS (100%)** |
| **Go-Back-N (GBN)** | 0% | 0.25 s | **200.00 KB/s** | 0.0 | **PASS (100%)** |
| **Go-Back-N (GBN)** | 10% | 1.02 s | 49.97 KB/s | 7.0 | **PASS (100%)** |
| **Go-Back-N (GBN)** | 25% | 3.98 s | 15.63 KB/s | 34.0 | **PASS (100%)** |
| **Selective Repeat (SR)**| 0% | 0.25 s | **200.00 KB/s** | 0.0 | **PASS (100%)** |
| **Selective Repeat (SR)**| 10% | 0.72 s | **70.79 KB/s** | 4.5 | **PASS (100%)** |
| **Selective Repeat (SR)**| 25% | 2.58 s | **20.88 KB/s** | 25.5 | **PASS (100%)** |

### Key Observations:
1. **Pipelining Advantage (0% Loss):** Both GBN and SR deliver **200.00 KB/s**, running **6.4x faster** than Stop-and-Wait (31.21 KB/s) because keeping $N=8$ packets in flight fully utilizes the channel capacity.
2. **Resilience to Loss (10% to 25% Loss):** At 25% loss, Selective Repeat achieves **20.88 KB/s** while GBN drops to **15.63 KB/s**. GBN causes 34 retransmissions due to discarding out-of-order packets, whereas Selective Repeat requires only 25.5 retransmissions by re-sending only the specific lost packets.

---

## 10. Future Scope & Potential Improvements

While the current codebase provides a robust, self-contained reliable transport implementation, several advanced networking enhancements could be implemented in future iterations:

1. **Dynamic / Adaptive RTO (Jacobson/Karels Algorithm):**
   - *Current:* Fixed 200 ms retransmission timeout.
   - *Improvement:* Continuously estimate Smooth Round Trip Time (SRTT) and Round Trip Time Variation (RTTVAR) to adapt timeout dynamically to fluctuating channel latency.
2. **Congestion Control Mechanisms (TCP Tahoe / Reno):**
   - *Current:* Fixed window size $N$.
   - *Improvement:* Implement Slow Start, Congestion Avoidance, and Fast Retransmit / Fast Recovery algorithms (`cwnd` scaling based on packet loss feedback).
3. **Selective Acknowledgment with Cumulative Base (SACK):**
   - *Current:* Pure cumulative ACKs for GBN, pure single-sequence ACKs for SR.
   - *Improvement:* Hybrid SACK headers carrying a cumulative ACK base along with block masks indicating discontiguous received chunks.
4. **Bidirectional Full-Duplex Data Transfer (Piggybacking):**
   - *Current:* Unidirectional file flow with reverse ACKs.
   - *Improvement:* Enable bidirectional simultaneous file exchange where ACK numbers are piggybacked directly inside outgoing DATA packet headers.
5. **Modern High-Throughput Hashing:**
   - *Current:* Standard SHA-256 for file verification.
   - *Improvement:* Support modern ultra-fast hashing algorithms (such as BLAKE3 or xxHash) for gigabit-rate file transfers where cryptographic collision resistance is secondary to raw hashing throughput.
