# Reliable Data Transfer over UDP

An in-depth, production-quality implementation of transport-layer reliability mechanisms built completely from scratch in C over raw UDP sockets. This project implements, tests, and rigorously compares three foundational Automatic Repeat reQuest (ARQ) error-control protocols — **Stop-and-Wait (SW)**, **Go-Back-N (GBN)**, and **Selective Repeat (SR)** — while utilizing an integrated, configurable **Network Channel Emulator** that simulates real-world packet loss, transmission latency, and payload bit corruption.

---

## Table of Contents
1. [Executive Summary & Educational Motivation](#1-executive-summary--educational-motivation)
2. [How It Works: End-to-End File Journey](#2-how-it-works-end-to-end-file-journey)
3. [System Architecture & Network Topology](#3-system-architecture--network-topology)
4. [Custom Binary Packet Format & Wire Protocol](#4-custom-binary-packet-format--wire-protocol)
5. [Comprehensive File-by-File Codebase Walkthrough](#5-comprehensive-file-by-file-codebase-walkthrough)
6. [Team Roles & Work Division (According to WORK_DIVISION.md)](#6-team-roles--work-division)
7. [Detailed Protocol Theory & State Machine Mechanics](#7-detailed-protocol-theory--state-machine-mechanics)
   - [7.1 Stop-and-Wait (SW)](#71-stop-and-wait-sw)
   - [7.2 Go-Back-N (GBN)](#72-go-back-n-gbn)
   - [7.3 Selective Repeat (SR)](#73-selective-repeat-sr)
8. [The Channel Emulator: Simulating Imperfect Networks](#8-the-channel-emulator-simulating-imperfect-networks)
9. [Step-by-Step Compilation & Execution Guide](#9-step-by-step-compilation--execution-guide)
10. [Live 3-Terminal Evaluation Demo & Output Explanations](#10-live-3-terminal-evaluation-demo)
11. [Automated Unit & Integration Test Suite](#11-automated-unit--integration-test-suite)
12. [Benchmark Analysis & Performance Comparison](#12-benchmark-analysis--performance-comparison)
13. [Technical Viva & Evaluation Defense Cheat Sheet](#13-technical-viva--evaluation-defense-cheat-sheet)
14. [Future Scope & Production Roadblocks](#14-future-scope--production-roadblocks)

---

## 1. Executive Summary & Educational Motivation

In the TCP/IP stack, the Internet Protocol (IP) provides an unreliable, connectionless "best-effort" delivery service where datagrams may be lost, duplicated, delayed, or arrive out of sequence. To build applications requiring reliable transfers (such as file downloads, webpage rendering, or email delivery), higher layers must bridge this reliability gap.

Modern systems use **TCP**, where reliability, flow control, and sliding window state machines run silently inside the OS kernel. This black-box abstraction makes it difficult for students and engineers to understand what actually happens when packets vanish in transit.

**The Purpose of this Project:**
- We deliberately bypass TCP by building on **raw User Datagram Protocol (UDP)** sockets.
- UDP provides only process-to-process multiplexing via port numbers and an optional, basic 16-bit checksum.
- By designing custom transport-layer headers, sequence numbering, timeouts, retransmission logic, cumulative/selective acknowledgments, and out-of-order buffers, we recreate transport reliability entirely in user-space in C.

---

## 2. How It Works: End-to-End File Journey

The entire file transfer pipeline operates through distinct operational stages:

```text
[Input File on Disk]
       │
       ▼ (Step 1: Segmentation)
[1024-Byte Slices]
       │
       ▼ (Step 2: Encapsulation)
[Custom Packets: Type + SeqNum + Payload + CRC32]
       │
       ▼ (Step 3: ARQ Sliding Window Dispatch)
[UDP Socket sendto()]
       │
       ▼ (Step 4: Transit through Channel Emulator)
[Probabilistic Drop / Delay / Bit Corruption]
       │
       ▼ (Step 5: Receiver Ingestion & Checksum Validation)
[Verify CRC32 Integrity]
   ├── Corrupted?  ──► Discard & Request Retransmit
   └── Valid?      ──► Buffer or Write Contiguous Data
                             │
                             ▼ (Step 6: Reassembly)
                     [Output File on Disk]
                             │
                             ▼ (Step 7: Hash Verification)
                     [SHA-256(Input) == SHA-256(Output)]
```

1. **File Ingestion & Chunking:** The sender opens the target file in binary read mode (`"rb"`), reads chunks up to `1024 bytes` into memory, and computes an initial SHA-256 reference hash.
2. **Encapsulation:** Each chunk is encapsulated into a `Packet` structure. The sender assigns a monotonic sequence number `seq_num`, writes the payload byte length, and computes a 32-bit CRC checksum across the complete struct.
3. **Pipelined Transmission:** Depending on the chosen protocol (SW, GBN, or SR), the sender dispatches packets within its allowed window limit via UDP `sendto()`.
4. **Channel Proxy Interception:** The datagrams arrive at the intermediate Channel Emulator (`emulator.exe`). The emulator rolls random numbers against user-specified probabilities:
   - If `rand() < loss_rate`: Packet is dropped immediately without forwarding.
   - If `rand() < corrupt_rate`: A random byte in the payload is inverted (`^= 0xFF`), invalidating the CRC32 checksum.
   - If `delay_ms > 0`: The packet is held in a sleep state before being forwarded.
5. **Receiver Validation & ACK Generation:** The receiver accepts the packet via `recvfrom()`:
   - Validates the packet with `verify_packet_checksum()`.
   - If corrupt, drops the packet and sends the appropriate duplicate or selective ACK.
   - If valid, buffers the packet or writes it sequentially to disk, returning an ACK packet.
6. **Connection Teardown:** Once all data chunks are transmitted, a `PACKET_FIN` handshake terminates the session cleanly.
7. **Cryptographic Integrity Check:** Both source and destination files are hashed with SHA-256 to guarantee 100% bit-for-bit parity.

---

## 3. System Architecture & Network Topology

The system uses an isolated 3-node proxy topology running over local or networked UDP sockets:

```text
+-------------------------------------------------------------------------------+
|                           CHANNEL EMULATOR (PROXY)                            |
|                          Listens on Port: 8000 (UDP)                          |
|                                                                               |
|   Incoming Sender DATA ──────► [ RNG Loss Evaluator ] ──────► DROPPED!        |
|                                         │ (If packet survives)                |
|                                         ▼                                     |
|                                [ RNG Corruptor ]                              |
|                                         │                                     |
|                                         ▼                                     |
|                                 [ Latency Delay ]                             |
|                                         │                                     |
|                                         ▼                                     |
|                           Forwarded to Port 9001 (Receiver)                   |
+-------------------------------------------------------------------------------+
                 ▲                                             │
   Forward DATA  │                               Forward DATA  │
   (Port 8000)   │                               (Port 9001)   ▼
+-----------------------------+               +-----------------------------+
|         UDP SENDER          |               |        UDP RECEIVER         |
|         (sender.exe)        |               |       (receiver.exe)        |
|                             |               |                             |
|  - Chunks file into 1KB     |               |  - Listens on Port 9001     |
|  - Maintains Window [N]     |  Reverse ACK  |  - Validates CRC32 Checksum |
|  - Manages Socket Timers    |◄──────────────┤  - Out-of-order Buffering   |
|  - Executes Retransmission  |  (via Proxy)  |  - Sequential File Writer   |
+-----------------------------+               +-----------------------------+
```

---

## 4. Custom Binary Packet Format & Wire Protocol

The custom transport packet is defined in [`include/packet.h`](include/packet.h). It packs transport headers and payload into a contiguous struct:

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Packet Type  |           Reserved / Alignment Padding        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Acknowledgment Number                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Payload Length        |       Checksum (CRC-32)       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Checksum (Continued)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                   Payload Data (0 - 1024 Bytes)              |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Struct Field Specifications:
1. `type` (`uint8_t`, 1 byte):
   - `PACKET_DATA (1)`: Identifies a packet carrying application payload data.
   - `PACKET_ACK  (2)`: Identifies a feedback control packet containing confirmation numbers.
   - `PACKET_FIN  (3)`: Teardown packet signaling end-of-file transmission.
2. `seq_num` (`uint32_t`, 4 bytes): Sequential identifier of the data chunk (starts at 0).
3. `ack_num` (`uint32_t`, 4 bytes): Sequence number being acknowledged back to the sender.
4. `length` (`uint16_t`, 2 bytes): Size of the valid payload in bytes (0 to 1024).
5. `checksum` (`uint32_t`, 4 bytes): 32-bit CRC value. Calculated with `checksum = 0` during computation, then populated before transmission.
6. `payload` (`char[1024]`, 1024 bytes): The raw binary chunk from the file.

---

## 5. Comprehensive File-by-File Codebase Walkthrough

### Header Files (`include/`)

- [`packet.h`](include/packet.h):
  - **Purpose:** Declares the universal `Packet` structure, packet type enums, and prototypes for initializing packets (`init_packet`), calculating checksums (`compute_packet_checksum`), and verifying checksums (`verify_packet_checksum`).
- [`socket_utils.h`](include/socket_utils.h):
  - **Purpose:** Isolates platform-specific networking details. Handles Winsock initialization (`WSAStartup`) on Windows and POSIX headers on Linux. Declares wrappers for socket creation, binding, timeout setting (`set_socket_timeout`), and struct address management.
- [`file_io.h`](include/file_io.h):
  - **Purpose:** Defines the disk I/O interface. Declares functions to read chunks (`read_chunk`), write chunks sequentially (`write_chunk`), determine file size (`get_file_size`), and compute 256-bit cryptographic hashes (`compute_file_sha256`).
- [`buffer.h`](include/buffer.h):
  - **Purpose:** Houses windowing abstractions. Contains the definitions for sender window slots and the receiver circular buffer (`RecvBuffer`) utilized by Selective Repeat to hold out-of-order packets.
- [`protocols.h`](include/protocols.h):
  - **Purpose:** High-level protocol entry points. Declares sender and receiver prototypes for all three ARQ strategies (`sw_sender`, `sw_receiver`, `gbn_sender`, `gbn_receiver`, `sr_sender`, `sr_receiver`).

---

### Source Files (`src/`)

- [`packet.c`](src/packet.c):
  - **What it does:** Implements `init_packet()`, which clears memory, sets headers, copies the payload, and computes the CRC32 checksum.
  - **How it works:** Implements a fast bitwise 32-bit Cyclic Redundancy Check (CRC32). When verifying, it zeroes out `pkt->checksum`, re-computes the CRC over the struct, and compares it against the transmitted value.
- [`socket_utils.c`](src/socket_utils.c):
  - **What it does:** Manages socket lifecycle. Initializes network subsystems, binds UDP sockets with `SO_REUSEADDR`, and wraps `sendto` and `recvfrom`.
  - **How it works:** Configures socket timeouts via `setsockopt(..., SO_RCVTIMEO, ...)` allowing blocking calls to timeout cleanly after 200 ms without locking the process.
- [`file_io.c`](src/file_io.c):
  - **What it does:** Reads and writes raw binary chunks. Contains an embedded, self-contained implementation of the standard NIST FIPS 180-4 SHA-256 algorithm.
  - **How it works:** Reads files in 1KB segments. After reception, computes the 64-character hexadecimal SHA-256 string for source and destination files to verify data integrity.
- [`stop_and_wait.c`](src/stop_and_wait.c):
  - **What it does:** Implements the classic Stop-and-Wait ARQ protocol.
  - **How it works:**
    - *Sender:* Sends one packet with alternating sequence number (0 or 1), sets a 200ms socket timeout, and waits for `PACKET_ACK`. On timeout, it retransmits the same packet up to 50 times.
    - *Receiver:* Checks incoming packet checksum. If valid and matching expected sequence, writes to disk, flips expected sequence, and sends ACK. If duplicate, re-sends previous ACK.
- [`go_back_n.c`](src/go_back_n.c):
  - **What it does:** Implements the sliding window Go-Back-N protocol with cumulative acknowledgments.
  - **How it works:**
    - *Sender:* Maintains `base` (oldest unacknowledged packet) and `next_seq_num`. Transmits packets as long as `next_seq_num < base + window_size`. Uses a single base timer. If timeout occurs, it **retransmits all packets in the current window** from `base` to `next_seq_num - 1`.
    - *Receiver:* Accepts packets strictly in-order (`seq_num == expected_seq`). Discards any out-of-order packet and re-sends a cumulative ACK for `expected_seq - 1`.
- [`selective_repeat.c`](src/selective_repeat.c):
  - **What it does:** Implements Selective Repeat ARQ with individual packet retransmission and out-of-order receiver buffering.
  - **How it works:**
    - *Sender:* Tracks sent time and ACK status for every packet individually in a window buffer. When a packet's individual 200ms timer expires, **only that specific packet is retransmitted**.
    - *Receiver:* Implements a circular buffer of size $N$. Out-of-order packets falling inside $[rcv\_base, rcv\_base + N - 1]$ are stored in memory and selectively acknowledged. When `rcv_base` arrives, contiguous buffered chunks are written to disk and the window slides forward.
- [`emulator.c`](src/emulator.c):
  - **What it does:** Functions as an intentional network impairment proxy.
  - **How it works:** Listens on port 8000 and forwards traffic to receiver port 9001. Randomly drops DATA packets based on `--loss`, inverts bits in payload based on `--corrupt`, and sleeps for `--delay` milliseconds before forwarding.
- [`sender.c`](src/sender.c):
  - **What it does:** User-facing CLI sender executable.
  - **How it works:** Parses CLI arguments (`--file`, `--ip`, `--port`, `--protocol`, `--window`) and invokes the corresponding sender engine (`sw_sender`, `gbn_sender`, or `sr_sender`).
- [`receiver.c`](src/receiver.c):
  - **What it does:** User-facing CLI receiver executable.
  - **How it works:** Parses CLI arguments (`--port`, `--output`, `--protocol`, `--window`) and executes the matching receiver engine.

---

## 6. Team Roles & Work Division

According to the official project contract in [`WORK_DIVISION.md`](WORK_DIVISION.md), module ownership is structured as follows:

| Member | Primary Ownership | Core Module Responsibilities & Contributions |
| :--- | :--- | :--- |
| **Dimple Mohapatra** | **Foundation, Packet Structure, File I/O & Buffers** | Authored `packet.h`, `packet.c`, `socket_utils.h`, `socket_utils.c`, `file_io.h`, `file_io.c`, CRC32 checksum engine, NIST SHA-256 hashing utility, and receiver circular buffer structures in `buffer.h`. |
| **Manisha Behera** | **Stop-and-Wait & Go-Back-N Protocols** | Designed and implemented `stop_and_wait.c`, `go_back_n.c`, alternating 1-bit sequence logic, GBN sliding window sender with cumulative ACK tracking, timeout recovery, and receiver in-order validation. |
| **Raushan Kumar** | **Selective Repeat Protocol & Buffer Management** | Implemented `selective_repeat.c`, sender per-packet timer management, individual selective ACK handling, receiver out-of-order packet buffering, and in-order chunk flushing algorithms. |
| **Mayank Rana** | **Channel Emulator, CLI Tools & Project Architecture** | Developed `emulator.c` impairment proxy, unified CLI binaries (`sender.c`, `receiver.c`), build configuration (`Makefile`), benchmark evaluation scripts, and technical documentation. |

---

## 7. Detailed Protocol Theory & State Machine Mechanics

### 7.1 Stop-and-Wait (SW)

```text
SENDER                                    RECEIVER
  │                                           │
  │── [Seq 0, Data] ─────────────────────────►│ (Valid, write chunk)
  │◄── [Ack 0] ───────────────────────────────│ (Expected flips to 1)
  │                                           │
  │── [Seq 1, Data] ──X (Dropped by Emulator) │
  │   [200ms Timeout Fires!]                  │
  │── [Seq 1, Data] (Retransmit) ────────────►│ (Valid, write chunk)
  │◄── [Ack 1] ───────────────────────────────│ (Expected flips to 0)
```

- **Sliding Window:** Size $N = 1$.
- **Sequence Space:** 1-bit ($0$ and $1$).
- **Channel Utilization:** Low. Throughput is bounded by the Round-Trip Time (RTT):
  $$\text{Utilization} = \frac{T_{\text{trans}}}{T_{\text{trans}} + 2 \times T_{\text{prop}}}$$
  As latency increases, the sender spends most of its time idle waiting for ACKs.

---

### 7.2 Go-Back-N (GBN)

```text
SENDER (Window N=4)                       RECEIVER
  │                                           │
  │── [Seq 0] ───────────────────────────────►│ (Received, Ack 0)
  │── [Seq 1] ──X (Dropped by Emulator)       │
  │── [Seq 2] ───────────────────────────────►│ (Out-of-order! Discarded!)
  │── [Seq 3] ───────────────────────────────►│ (Out-of-order! Discarded!)
  │◄── [Ack 0] ───────────────────────────────│
  │                                           │
  │   [Timer for Seq 1 Expires!]              │
  │   >>> Retransmit ENTIRE Window <<<        │
  │── [Seq 1] ───────────────────────────────►│ (Received, Ack 1)
  │── [Seq 2] ───────────────────────────────►│ (Received, Ack 2)
  │── [Seq 3] ───────────────────────────────►│ (Received, Ack 3)
```

- **Sliding Window:** Sender window size $N$ (default 8). Receiver window size $= 1$.
- **Sequence Constraint:** For $k$-bit sequence numbers, $N \le 2^k - 1$.
- **Cumulative ACKs:** ACK $k$ confirms all packets up to $k$.
- **Disadvantage:** A single dropped packet causes the receiver to discard all subsequent out-of-order packets, forcing the sender to retransmit the entire window and wasting bandwidth.

---

### 7.3 Selective Repeat (SR)

```text
SENDER (Window N=4)                       RECEIVER (Window N=4)
  │                                           │
  │── [Seq 0] ───────────────────────────────►│ (Received, Ack 0)
  │── [Seq 1] ──X (Dropped by Emulator)       │
  │── [Seq 2] ───────────────────────────────►│ (Buffered in slot 2, Ack 2)
  │── [Seq 3] ───────────────────────────────►│ (Buffered in slot 3, Ack 3)
  │◄── [Ack 0]                                │
  │◄── [Ack 2]                                │
  │◄── [Ack 3]                                │
  │                                           │
  │   [Timer for Seq 1 Expires!]              │
  │   >>> Retransmit ONLY Packet 1 <<<        │
  │── [Seq 1] ───────────────────────────────►│ (Received, Ack 1)
  │                                           │ [Flush contiguous Seq 1,2,3 to disk]
```

- **Sliding Window:** Both sender and receiver maintain window size $N$.
- **Sequence Constraint:** Sequence number space must be at least $2N$ ($N \le 2^{k-1}$) to prevent window overlap ambiguity.
- **Selective ACKs:** ACK $k$ confirms only packet $k$.
- **Advantage:** Only dropped packets are retransmitted, maintaining high throughput under lossy conditions.

---

## 8. The Channel Emulator: Simulating Imperfect Networks

The emulator (`emulator.exe`) acts as a transparent network proxy:

- `--listen <port>`: Local port where the emulator receives sender packets (default: 8000).
- `--recv-port <port>`: Destination port where packets are forwarded (default: 9001).
- `--loss <percent>`: Percentage of DATA packets dropped (e.g., `--loss 15` drops 15% of packets).
- `--corrupt <percent>`: Percentage of DATA packets subjected to bit-inversion (flipped payload byte).
- `--delay <ms>`: Delay applied before forwarding packets.

---

## 9. Step-by-Step Compilation & Execution Guide

### Compilation
Compile the entire project using `mingw32-make` (Windows) or `make` (Linux):
```bash
mingw32-make all
```

This compiles:
- `sender.exe` (Unified sender CLI)
- `receiver.exe` (Unified receiver CLI)
- `emulator.exe` (Impairment proxy)
- `test_*.exe` (Core unit and integration tests)

To remove compiled binaries and object files:
```bash
mingw32-make clean
```

---

## 10. Live 3-Terminal Evaluation Demo

To demonstrate the full 3-node topology live during evaluation, open **three separate terminals**:

### Terminal 1: Start Receiver
```powershell
.\receiver.exe --port 9001 --output test_data/demo_received.bin --protocol sr --window 8
```
*The receiver will bind to port 9001 and wait for packets.*

### Terminal 2: Start Channel Emulator (15% Loss, 20ms Delay)
```powershell
.\emulator.exe --listen 8000 --recv-port 9001 --loss 15 --delay 20
```
*The emulator will proxy packets between port 8000 and 9001, dropping 15% of data packets.*

### Terminal 3: Start Sender
```powershell
.\sender.exe --file test_data/benchmark_input.bin --ip 127.0.0.1 --port 8000 --protocol sr --window 8
```
*The sender will transmit the file via the emulator using Selective Repeat.*

### Verify Cryptographic Integrity:
```powershell
certutil -hashfile test_data/benchmark_input.bin SHA256
certutil -hashfile test_data/demo_received.bin SHA256
```
Both 64-character SHA-256 hashes will match **100% identically**, proving full data recovery.

---

## 11. Automated Unit & Integration Test Suite

Run the automated test suite with:
```bash
mingw32-make test
```

| Test Executable | Module Verified | Scope & Verification |
| :--- | :--- | :--- |
| `test_packet.exe` | Checksum & Packet Layer | Verifies CRC32 calculation, single-bit flips, header tampering, and FIN frames. |
| `test_loopback.exe`| UDP Socket Layer | Tests UDP transmission, socket binding, and bidirectional ACK roundtrips. |
| `test_file_io.exe` | File I/O & SHA-256 | Tests 1KB chunking, file reassembly, and SHA-256 corruption detection. |
| `test_sw.exe` | Stop-and-Wait Protocol | Runs full SW file transfer and verifies SHA-256 integrity match. |
| `test_gbn.exe` | Go-Back-N Protocol | Tests windowing ($N=4, 8, 16$) and recovery under 15% packet loss. |
| `test_sr.exe` | Selective Repeat Protocol | Tests out-of-order buffering and recovery under 15% packet loss. |

---

## 12. Benchmark Analysis & Performance Comparison

Benchmark results evaluating all three protocols on a 50 KB file across packet loss rates:

| Protocol | Loss Rate (%) | Transfer Time (s) | Effective Goodput (KB/s) | Retransmissions | SHA-256 Match |
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

### Key Conclusions:
1. **Pipelining Advantage (0% Loss):** GBN and SR achieve **200.00 KB/s** (**6.4x faster** than SW at 31.21 KB/s) by keeping multiple packets in flight.
2. **Selective Retransmission Resilience (10% - 25% Loss):** At 25% loss, Selective Repeat maintains **20.88 KB/s** while GBN drops to **15.63 KB/s**. GBN causes 34 retransmissions due to window discarding, whereas Selective Repeat requires only 25.5 retransmissions.

---

## 13. Technical Viva & Evaluation Defense Cheat Sheet

### Dimple Mohapatra (Foundation & Packet Layer)
- **Q: Why use UDP instead of raw sockets or TCP?**
  - *Answer:* Raw sockets require administrative privileges and manual IP framing. TCP implements reliability inside the kernel. UDP provides port multiplexing while letting us implement ARQ protocols entirely in user-space.
- **Q: Why is CRC32 needed if UDP already has a checksum?**
  - *Answer:* Standard UDP checksums are only 16 bits and optional on some IPv4 configurations. Our 32-bit CRC detects payload bit corruptions with a much stronger Hamming distance.

### Manisha Behera (Stop-and-Wait & Go-Back-N)
- **Q: Why does Go-Back-N throughput drop steeply under packet loss?**
  - *Answer:* GBN uses cumulative ACKs. If packet $k$ is lost, the receiver discards all subsequent packets ($k+1, \dots, k+N-1$), forcing the sender to retransmit the entire window from $k$ onwards.
- **Q: What is the sequence number requirement for GBN?**
  - *Answer:* For window size $N$, sequence numbers must satisfy $N \le 2^k - 1$ to prevent window overlap ambiguity.

### Raushan Kumar (Selective Repeat & Buffering)
- **Q: How does Selective Repeat prevent redundant retransmissions?**
  - *Answer:* The receiver buffers out-of-order packets and returns individual selective ACKs. The sender tracks timers per packet and retransmits only the specific packet that timed out.
- **Q: Why does Selective Repeat require sequence number space $\ge 2N$?**
  - *Answer:* If sequence space is $< 2N$, a delayed ACK from the previous window could be misinterpreted as an ACK for a packet in the new window, causing data corruption.

### Mayank Rana (Channel Emulator & Benchmarks)
- **Q: How does the emulator drop packets without kernel modifications?**
  - *Answer:* It acts as a UDP application proxy. It intercepts datagrams on a listen port, rolls a random number against the loss probability, and either drops the packet or forwards it to the destination.
- **Q: What does the Goodput vs. Loss graph demonstrate?**
  - *Answer:* At 0% loss, sliding window protocols maximize throughput. As loss rates increase, Selective Repeat outperforms Go-Back-N because its retransmissions scale linearly with packet loss, whereas GBN's retransmissions multiply with window size.

---

## 14. Future Scope & Production Roadblocks

While the project implements a complete ARQ transport framework, several advanced enhancements could be pursued in future work:

1. **Adaptive Retransmission Timeout (Jacobson/Karels Algorithm):**
   - *Current:* Fixed 200 ms socket timer.
   - *Improvement:* Dynamically calculate Smooth Round Trip Time (SRTT) and variance (RTTVAR) to adapt timeout to network conditions:
     $$\text{RTO} = \text{SRTT} + 4 \times \text{RTTVAR}$$
2. **TCP-Style Congestion Control (Tahoe / Reno):**
   - *Current:* Fixed window size $N$.
   - *Improvement:* Implement Slow Start, Congestion Avoidance, and Fast Retransmit/Recovery with dynamic `cwnd` scaling based on packet loss feedback.
3. **Cumulative + Selective Hybrid (SACK):**
   - *Current:* Pure cumulative ACKs for GBN, pure single ACKs for SR.
   - *Improvement:* Implement Selective Acknowledgments (RFC 2018) carrying a cumulative ACK base alongside SACK blocks indicating non-contiguous chunks.
4. **Bidirectional Full-Duplex Piggybacking:**
   - *Current:* Unidirectional file flow with reverse ACK packets.
   - *Improvement:* Enable simultaneous two-way file transfers where ACK numbers are piggybacked directly inside outgoing DATA packet headers.
5. **High-Performance Non-Cryptographic Hashing:**
   - *Current:* Standard SHA-256 for file verification.
   - *Improvement:* Integrate ultra-fast hashing algorithms like **BLAKE3** or **xxHash** for multi-gigabit transfers where cryptographic security is secondary to hashing speed.
