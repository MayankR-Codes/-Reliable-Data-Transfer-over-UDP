# Reliable Data Transfer over UDP — Detailed 7-Day Sprint & Work Division

**Project Title:** Reliable File Transfer System over UDP with ARQ Protocols  
**Team Members:** Mayank Rana, Dimple Mohapatra, Manisha Behera, Raushan Kumar  
**Target Duration:** 7 Days (Max)  
**Scope Strategy:** 100% Mandatory Core Components Only. All complex/optional features (such as dynamic Jacobson/Karels adaptive RTO formulas and complicated multi-threaded timer trees) are removed. The system strictly utilizes **Fixed Socket Retransmission Timeouts (200 ms)** to guarantee rapid, bug-free delivery.

---

## Table of Contents
1. [Overall Project Outcome & Final Deliverables](#1-overall-project-outcome--final-deliverables)
2. [Role Matrix & Module Ownership](#2-role-matrix--module-ownership)
3. [Day-by-Day Detailed Plan (Tasks, Responsibilities & Daily Outcomes)](#3-day-by-day-detailed-plan)
   - [Day 1: Packet Protocol, Checksum & UDP Loopback](#day-1-packet-protocol-checksum--udp-loopback)
   - [Day 2: File Chunking & Stop-and-Wait (SW) Protocol](#day-2-file-chunking--stop-and-wait-sw-protocol)
   - [Day 3: Channel Emulator & Go-Back-N (GBN) Protocol](#day-3-channel-emulator--go-back-n-gbn-protocol)
   - [Day 4: Selective Repeat (SR) ARQ & Out-of-Order Buffer](#day-4-selective-repeat-sr-arq--out-of-order-buffer)
   - [Day 5: Full System Integration & Impairment Hardening](#day-5-full-system-integration--impairment-hardening)
   - [Day 6: Automated Benchmarking & Matplotlib Visualizations](#day-6-automated-benchmarking--matplotlib-visualizations)
   - [Day 7: Final Code Polishing, Documentation & Viva Preparation](#day-7-final-code-polishing-documentation--viva-preparation)
4. [File & Architecture Layout](#4-file--architecture-layout)
5. [End-to-End Verification & Demo Guide](#5-end-to-end-verification--demo-guide)
6. [Viva / Evaluation Questions by Role](#6-viva--evaluation-questions-by-role)

---

## 1. Overall Project Outcome & Final Deliverables

At the conclusion of Day 7, the project will deliver:

1. **Three Working Command-Line Binaries (Compiled via C):**
   - `./sender`: Reads any file, divides it into datagrams, applies the selected ARQ protocol (SW, GBN, or SR), and retransmits lost packets upon fixed timeout.
   - `./receiver`: Listens on a UDP port, validates packet checksums, sends ACKs (cumulative or selective), reassembles chunks in correct order, writes the output file, and verifies integrity using an SHA-256 hash.
   - `./emulator`: Sits between sender and receiver as a network proxy to drop, delay, and corrupt packets based on user-specified probabilities.

2. **Automated Python Benchmark Suite (`scripts/benchmark.py`):**
   - Automatically executes sender, emulator, and receiver across a matrix of loss rates ($0\%, 5\%, 10\%, 15\%, 20\%, 25\%$) for each protocol.
   - Measures:
     - **Goodput** (Effective delivered application bytes per second)
     - **Total Retransmission Count**
     - **Total Transfer Time**

3. **Matplotlib Visual Graphs (`scripts/plot_results.py`):**
   - `goodput_vs_loss.png`: Proves that as packet loss increases, **Selective Repeat maintains significantly higher goodput** compared to Go-Back-N and Stop-and-Wait.
   - `retransmissions_vs_loss.png`: Demonstrates that Go-Back-N exhibits an explosion of redundant retransmissions under loss, whereas Selective Repeat only retransmits truly dropped packets.
   - `time_vs_loss.png`: Visualizes elapsed transfer time scaling across protocols.

4. **100% Data Integrity Verification:**
   - Every file transfer is verified using SHA-256 hash matching between source and received files (`sha256sum input.bin == sha256sum output.bin`).

---

## 2. Role Matrix & Module Ownership

| Member | Primary Ownership | Core Modules & Deliverables |
| :--- | :--- | :--- |
| **Dimple Mohapatra** | **Foundation, Packet Structure & File I/O** | `packet.h`, `packet.c`, `socket_utils.c`, `file_io.c`, CRC32 checksum, SHA-256 file hashing |
| **Manisha Behera** | **Stop-and-Wait & Go-Back-N ARQ** | `stop_and_wait.c`, `go_back_n.c`, sliding window logic, cumulative ACKs |
| **Raushan Kumar** | **Selective Repeat ARQ & Buffering** | `selective_repeat.c`, circular receiver buffer, selective ACKs, per-packet tracking |
| **Mayank Rana** | **Channel Emulator, Testing & Analytics** | `emulator.c`, `Makefile`, `scripts/benchmark.py`, `scripts/plot_results.py` |

---

## 3. Day-by-Day Detailed Plan

### Day 1: Packet Protocol, Checksum & UDP Loopback

#### Objectives
Establish the binary packet contract, checksum calculation, and raw UDP transport so the team has a common foundation.

#### Individual Tasks
- **Dimple Mohapatra:**
  - Create `include/packet.h` defining the packet format:
    ```c
    #define PACKET_DATA 1
    #define PACKET_ACK  2
    #define PACKET_FIN  3
    #define MAX_PAYLOAD 1024

    typedef struct {
        uint8_t  type;             // PACKET_DATA, PACKET_ACK, PACKET_FIN
        uint32_t seq_num;          // Sequence number
        uint32_t ack_num;          // Acknowledged sequence number
        uint16_t length;           // Actual length of payload
        uint32_t checksum;         // CRC32 of header + payload
        char     payload[MAX_PAYLOAD];
    } Packet;
    ```
  - Implement `src/packet.c`: Functions `compute_checksum()`, `serialize_packet()`, and `deserialize_packet()`.
- **Mayank Rana:**
  - Create the project build system (`Makefile` or `CMakeLists.txt`) with compiler flags `-Wall -Wextra -O2`.
  - Implement basic UDP socket wrappers in `src/socket_utils.c` (`create_udp_socket`, `bind_socket`).
- **Manisha Behera & Raushan Kumar:**
  - Build a temporary test script (`tests/test_packet.c`) to instantiate a packet, compute checksum, corrupt one byte, and verify that `compute_checksum()` detects the mismatch.
  - Test sending raw UDP packets between two localhost ports.

#### Day 1 Outcome
- `make` successfully compiles the packet library.
- Test executable confirms:
  1. Checksum matches on uncorrupted packets.
  2. Checksum fails when bits are intentionally flipped.
  3. UDP send/receive works cleanly on localhost.

---

### Day 2: File Chunking & Stop-and-Wait (SW) Protocol

#### Objectives
Implement real file reading/writing and the complete Stop-and-Wait ARQ protocol with fixed timeout retransmissions.

#### Individual Tasks
- **Dimple Mohapatra:**
  - Implement `src/file_io.c`:
    - `read_chunk(FILE *fp, char *buffer, int max_len)`
    - `write_chunk(FILE *fp, const char *buffer, int len)`
    - `compute_file_hash(const char *filepath, char *hash_out)` using SHA-256.
- **Manisha Behera:**
  - Implement `src/stop_and_wait.c`:
    - **Sender:** Send packet with `seq_num = 0` (or 1), configure a 200ms socket timeout (`SO_RCVTIMEO`), and block waiting for `PACKET_ACK`. If timeout fires, retransmit the packet. Once ACK arrives, toggle `seq_num` and advance to the next file chunk.
    - **Receiver:** Receive packet, check checksum. If valid and matching expected `seq_num`, write to disk, send ACK, and toggle expected sequence number. If duplicate or corrupt, re-send previous ACK.
- **Mayank Rana:**
  - Start building `src/emulator.c`: Bind proxy port; forward packets from Sender $\to$ Receiver and Receiver $\to$ Sender with a configurable drop rate (`--loss <percent>`).
- **Raushan Kumar:**
  - Assist Manisha in implementing the sender timeout handler using `select()` or `setsockopt(SO_RCVTIMEO)`.
  - Prepare a dummy 5 MB test file (`test_data/input_5mb.bin`).

#### Day 2 Outcome
- Full file transfer works using **Stop-and-Wait**.
- Sender transmits `input_5mb.bin`, Receiver writes `output_5mb.bin`.
- Running `sha256sum input_5mb.bin` and `sha256sum output_5mb.bin` produces **identical hashes**.
- Stop-and-Wait successfully recovers if packets are manually dropped.

---

### Day 3: Channel Emulator & Go-Back-N (GBN) Protocol

#### Objectives
Introduce sliding window mechanics and cumulative acknowledgements via Go-Back-N; expand emulator with loss and artificial delay.

#### Individual Tasks
- **Manisha Behera:**
  - Implement `src/go_back_n.c`:
    - **Sender:**
      - Window size $N$ (e.g., $N = 8$).
      - Maintain `base` (oldest unacknowledged packet) and `next_seq_num`.
      - Can send packets as long as `next_seq_num < base + N`.
      - Single base timer (200 ms). If ACK arrives where `ack_num >= base`, advance `base = ack_num + 1` and reset timer.
      - On timeout: Retransmit **all** packets from `base` up to `next_seq_num - 1`.
    - **Receiver:**
      - Maintain `expected_seq_num`.
      - If arriving packet is valid and `seq_num == expected_seq_num`, write to file, increment `expected_seq_num`, and send cumulative ACK.
      - If packet is out of order or corrupt, discard it and re-send cumulative ACK for `expected_seq_num - 1`.
- **Mayank Rana:**
  - Complete `src/emulator.c` with parameters:
    - `--loss <float>`: Drops packets randomly using `(rand() / (double)RAND_MAX) < loss_rate`.
    - `--delay <ms>`: Adds artificial sleep before forwarding packet.
- **Dimple Mohapatra:**
  - Add sender window ring-buffer helper in `include/buffer.h` to store in-flight packets awaiting GBN acknowledgement.
- **Raushan Kumar:**
  - Run side-by-side benchmark between Stop-and-Wait and Go-Back-N over the emulator with 0ms and 50ms delay to verify that GBN achieves higher throughput.

#### Day 3 Outcome
- Go-Back-N file transfer functions properly with window sizes $N = 4, 8, 16$.
- When a packet is dropped by the emulator, the sender times out and retransmits the full unacknowledged window.
- Receiver output file passes SHA-256 verification.

---

### Day 4: Selective Repeat (SR) ARQ & Out-of-Order Buffer

#### Objectives
Implement Selective Repeat ARQ to eliminate redundant retransmissions by buffering out-of-order packets at the receiver.

#### Individual Tasks
- **Raushan Kumar:**
  - Implement `src/selective_repeat.c`:
    - **Sender:**
      - Window size $N$ (e.g., $N = 8$).
      - Per-packet tracking array: `enum { UNACKED, ACKED } status[N]`.
      - Maintain sending timestamp for each packet; retransmit **only** individual packets whose elapsed time exceeds 200 ms.
      - When selective ACK arrives, mark that specific packet as `ACKED`. Advance window base when the lowest unacked packet is acknowledged.
    - **Receiver Buffer:**
      - Maintain a circular buffer of size $N$ with slots `[rcv_base, rcv_base + N - 1]`.
      - If packet arrives within the window: Store in buffer and send selective ACK for its `seq_num`.
      - If the arriving packet matches `rcv_base`: Flush all contiguous buffered packets to the file and slide `rcv_base`.
      - If packet is duplicate (`seq_num < rcv_base`): Re-send ACK so sender can advance.
- **Dimple Mohapatra:**
  - Assist Raushan in writing the memory-safe circular buffer for the receiver to prevent memory leaks and buffer overruns.
- **Manisha Behera:**
  - Conduct edge-case testing on Go-Back-N under high loss rates ($15\%$) to verify base recovery.
- **Mayank Rana:**
  - Add packet bit-corruption feature to `src/emulator.c` (`--corrupt <percent>`, flipping 1 random byte in payload).

#### Day 4 Outcome
- Selective Repeat transfers files correctly over the emulator.
- When packet #3 is dropped, packet #4 and #5 are buffered at the receiver without being discarded. Once packet #3 is retransmitted, all packets are flushed to disk in exact sequence.
- File hash matches 100%.

---

### Day 5: Full System Integration & Impairment Hardening

#### Objectives
Integrate SW, GBN, and SR into unified CLI binaries and harden against network impairments (loss, delay, corruption).

#### Individual Tasks
- **Mayank Rana:**
  - Finalize CLI parameter parsing in `src/sender.c` and `src/receiver.c`:
    ```bash
    ./sender --file <path> --ip <dest_ip> --port <port> --protocol <sw|gbn|sr> --window <N>
    ./receiver --port <port> --output <path> --protocol <sw|gbn|sr> --window <N>
    ./emulator --listen-port <p1> --forward-port <p2> --loss <0-100> --delay <ms>
    ```
- **Dimple Mohapatra:**
  - Standardize log format across all protocols:
    - `[SEND] Seq=12 Length=1024 Checksum=0x8A2F`
    - `[RECV] Ack=12 Valid=YES`
    - `[TIMEOUT] Packet=12 Retransmitting...`
    - `[CORRUPT] Packet=14 Discarded (Checksum mismatch)`
- **Manisha Behera & Raushan Kumar:**
  - Run stress tests for both GBN and SR under severe conditions (20% loss + 10% corruption + 20ms delay).
  - Ensure zero deadlocks: Handshake termination packet (`PACKET_FIN`) must be reliably acknowledged so programs exit gracefully.

#### Day 5 Outcome
- A single unified `sender` and `receiver` binary supports all 3 protocols via `--protocol` flag.
- The entire system runs cleanly without memory leaks or hanging threads.
- All file transfers exit with return code 0 and matched SHA-256 hashes.

---

### Day 6: Automated Benchmarking & Matplotlib Visualizations

#### Objectives
Gather performance metrics across loss rates and generate publication-quality comparison charts.

#### Individual Tasks
- **Mayank Rana:**
  - Write `scripts/benchmark.py`:
    - Automatically launches `receiver`, `emulator`, and `sender` in sequence.
    - Loops through loss rates: `0%, 5%, 10%, 15%, 20%, 25%`.
    - Repeats 3 runs per configuration and records average:
      1. Transfer Time (seconds)
      2. Goodput (KB/sec) = `File Size / Transfer Time`
      3. Retransmission Count
    - Exports metrics to `results/benchmark_data.csv`.
  - Write `scripts/plot_results.py`:
    - Reads `results/benchmark_data.csv` using Python `pandas` / `csv`.
    - Generates 3 Matplotlib plots saved to `results/`:
      - `goodput_comparison.png`
      - `retransmissions_comparison.png`
      - `transfer_time_comparison.png`
- **Dimple, Manisha, and Raushan:**
  - Monitor test runs, verify that no runs crash or hang.
  - Inspect output CSV to ensure the central project thesis holds true:
    - At 0% loss: GBN and SR have near-identical high goodput; SW is slowest.
    - At 15%+ loss: SR goodput remains resilient; GBN goodput degrades steeply due to window retransmissions.

#### Day 6 Outcome
- `benchmark_data.csv` contains verified, reproducible benchmark numbers.
- High-resolution comparison plots generated in `results/` directly validating the core project claim.

---

### Day 7: Final Code Polishing, Documentation & Viva Preparation

#### Objectives
Finalize repository, polish README, verify clean builds, and prepare all team members for technical evaluation and viva.

#### Individual Tasks
- **Mayank Rana:**
  - Update `README.md` with:
    - Step-by-step compilation and execution guide.
    - Embedded generated Matplotlib comparison graphs.
    - Summary table of benchmark results.
- **Dimple Mohapatra:**
  - Code review: Ensure consistent indentation, clean variable naming, and proper `#include` guards.
  - Verify clean compilation with `gcc -Wall -Wextra -pedantic` without warnings.
- **Manisha Behera & Raushan Kumar:**
  - Prepare presentation talking points and viva cheat sheet.
  - Rehearse live demonstration flow (running emulator at 15% loss, showing packets dropping, retransmitting, and final SHA-256 match).

#### Day 7 Outcome
- Clean, ready-to-submit GitHub repository.
- Complete documentation with reproducible commands.
- All 4 members fully prepared to answer viva questions on their specific components.

---

## 4. File & Architecture Layout

```text
Reliable Data Transfer over UDP/
├── include/
│   ├── packet.h              # Packet struct & CRC32 headers (Dimple)
│   ├── socket_utils.h        # UDP socket creation & helpers (Dimple)
│   ├── file_io.h             # File chunking & SHA256 integrity (Dimple)
│   ├── buffer.h              # Window & circular buffer structures (Dimple / Raushan)
│   └── protocols.h           # Prototypes for SW, GBN, and SR (Manisha / Raushan)
├── src/
│   ├── packet.c              # Serialization & checksum computation (Dimple)
│   ├── socket_utils.c        # Network socket functions (Dimple)
│   ├── file_io.c             # Chunk reader, writer & hashing (Dimple)
│   ├── stop_and_wait.c       # Stop-and-Wait ARQ logic (Manisha)
│   ├── go_back_n.c           # Go-Back-N ARQ logic (Manisha)
│   ├── selective_repeat.c    # Selective Repeat ARQ logic (Raushan)
│   ├── sender.c              # Unified sender executable (Mayank / All)
│   ├── receiver.c            # Unified receiver executable (Mayank / All)
│   └── emulator.c            # Loss/Delay/Corruption proxy (Mayank)
├── scripts/
│   ├── benchmark.py          # Automated experiment runner (Mayank)
│   └── plot_results.py       # Matplotlib comparative graphs (Mayank)
├── test_data/
│   └── sample_file.bin       # Test input file
├── results/
│   ├── benchmark_data.csv    # Test data across loss rates
│   ├── goodput_comparison.png
│   └── retransmissions_comparison.png
├── WORK_DIVISION.md          # Complete project sprint and assignment guide
├── README.md                 # Primary project overview
└── Makefile                  # Compilation commands
```

---

## 5. End-to-End Verification & Demo Guide

### Step 1: Build the Project
```bash
make clean && make
```
*Outputs binaries: `sender`, `receiver`, `emulator`.*

### Step 2: Create a Test File
```bash
# Generate a 2 MB test file
head -c 2097152 </dev/urandom > test_data/sample_file.bin
```

### Step 3: Run the 3-Node Topology

**Terminal 1 — Receiver:**
```bash
./receiver --port 9001 --output test_data/received_file.bin --protocol sr --window 8
```

**Terminal 2 — Channel Emulator (10% Packet Loss, 20ms Delay):**
```bash
./emulator --listen-port 8000 --forward-port 9001 --loss 10 --delay 20
```

**Terminal 3 — Sender:**
```bash
./sender --file test_data/sample_file.bin --ip 127.0.0.1 --port 8000 --protocol sr --window 8
```

### Step 4: Verify File Integrity
```bash
sha256sum test_data/sample_file.bin
sha256sum test_data/received_file.bin
```
*Both outputs must match identically.*

---

## 6. Viva / Evaluation Questions by Role

### For Dimple (Packet & Networking):
1. **Why use UDP instead of TCP for this project?**
   - *Answer:* TCP implements reliability inside the OS kernel. By using UDP, we implement reliability (ARQ, sequencing, checksums, retransmissions) at the application layer to understand and measure transport layer mechanics.
2. **Why is a checksum necessary if UDP already has one?**
   - *Answer:* Standard UDP checksums are optional on some IPv4 stacks, only 16-bit, and don't protect against simulated application-layer proxy corruptions. Our CRC32 / internet checksum guarantees end-to-end data integrity.

### For Manisha (Stop-and-Wait & Go-Back-N):
1. **Why does Go-Back-N suffer when packet loss is high?**
   - *Answer:* GBN uses cumulative ACKs. If packet $k$ is dropped, the receiver discards all subsequent packets ($k+1, k+2, \dots$), forcing the sender to retransmit the entire window from $k$ onwards, wasting bandwidth on redundant retransmissions.
2. **What is the sender window constraint for Go-Back-N with $k$-bit sequence numbers?**
   - *Answer:* Maximum window size $N \le 2^k - 1$. This prevents duplicate sequence number ambiguity between the current and subsequent window.

### For Raushan (Selective Repeat & Buffering):
1. **How does Selective Repeat prevent the redundant retransmissions of GBN?**
   - *Answer:* The receiver buffers out-of-order packets and sends individual selective ACKs. The sender tracks packet states individually and only retransmits the specific unacknowledged packet that timed out.
2. **What is the sequence number requirement for Selective Repeat?**
   - *Answer:* Maximum window size $N \le 2^{k-1}$ (i.e., sequence number space must be at least $2N$). If $N > 2^{k-1}$, the receiver cannot distinguish between a new packet and a retransmission from the previous window.

### For Mayank (Channel Emulator & Benchmarks):
1. **How does the channel emulator drop packets without modifying OS networking?**
   - *Answer:* It acts as a UDP application proxy. It receives datagrams on a listen port, rolls a random number against the loss probability, and either drops the packet or forwards it to the target destination port.
2. **What does the Goodput vs. Loss graph prove?**
   - *Answer:* At 0% loss, sliding window protocols (GBN and SR) achieve comparable throughput. As loss increases, Selective Repeat maintains significantly higher goodput because its retransmission count scales linearly with lost packets, whereas GBN's retransmissions scale with $N \times \text{loss}$.
