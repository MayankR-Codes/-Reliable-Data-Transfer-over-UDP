# Reliable Data Transfer over UDP

A from-scratch reliable file-transfer system built on top of UDP. The project demonstrates how transport protocols can provide correct, ordered, and verifiable delivery even when the underlying network loses, duplicates, reorders, delays, or corrupts packets.

## Overview

UDP is fast and lightweight, but it does not provide delivery guarantees. This project adds those guarantees at the application layer through three ARQ (Automatic Repeat reQuest) protocols:

- **Stop-and-Wait ARQ** - sends one packet at a time and waits for its acknowledgement.
- **Go-Back-N ARQ** - allows a window of packets in flight and retransmits from the first missing packet after a loss.
- **Selective Repeat ARQ** - acknowledges and retransmits only the packets that are missing or damaged.

A configurable channel emulator sits between the sender and receiver so the protocols can be tested under controlled network impairments.

## Core Features

- Reliable file transfer over POSIX UDP sockets
- Ordered delivery with sequence numbers and acknowledgements
- Packet checksums for corruption detection
- Retransmission timers and timeout handling
- Adaptive retransmission timeout (RTO)
- Karn's algorithm to avoid using ambiguous RTT samples
- Configurable packet loss, duplication, reordering, corruption, and delay
- File integrity verification using hash comparison
- Experiment data collection and comparative plots with Python and Matplotlib

## System Design

```text
						 +-------------------+
						 |   Channel Emulator |
						 | loss | delay | ... |
						 +---------+---------+
								   |
+----------------+       UDP      |      +----------------+
|     Sender     +----------------------->+    Receiver    |
| ARQ + timers   |                         | ARQ + buffering|
+----------------+<-----------------------+----------------+
				 acknowledgements / control
```

Each data packet contains the information needed for validation and recovery, including a packet type, sequence number, payload length, checksum, and payload. Receivers discard corrupted packets, buffer valid out-of-order packets where the protocol permits it, and acknowledge received data. The sender uses acknowledgements and timer events to decide when retransmission is required.

## Protocols

| Protocol | Sender window | Recovery behavior | Expected trade-off |
| --- | ---: | --- | --- |
| Stop-and-Wait | 1 | Retransmit the current packet | Simple, but low goodput on delayed links |
| Go-Back-N | Configurable | Retransmit the missing packet and all later unacknowledged packets | Better link utilization, but loss causes redundant retransmissions |
| Selective Repeat | Configurable | Retransmit only missing or corrupted packets | More buffering and logic, but efficient under loss |

All protocols will use the same packet size, file size, checksum mechanism, and test environment so their performance can be compared fairly.

## Channel Emulator

The emulator will expose configurable impairment parameters, including:

- Packet-loss probability
- Packet-duplication probability
- Packet-reordering probability
- Packet-corruption probability
- Network delay and delay variation

Impairments will first be tested independently and then in combination. This makes it easier to isolate protocol bugs and understand how each condition affects performance.

## Performance Claim

> **Claim:** As packet loss increases, Selective Repeat will achieve better goodput than Go-Back-N and Stop-and-Wait.

The experiment will vary:

- Packet-loss rate
- Sender/receiver window size
- Packet-reordering probability

The following variables will remain constant during comparable runs:

- Packet size
- Input file size
- Hardware and runtime environment

We will measure:

- **Goodput** - successfully delivered application data per unit of time
- **Retransmissions** - total packets resent after timeout or negative evidence
- **Transfer time** - elapsed time from the first transmitted packet to verified completion

Results will be exported for analysis and presented with comparative Python/Matplotlib graphs. Every completed transfer will also be checked by comparing the receiver's file hash with the source file hash.

## Technology Stack

- **C** - protocol implementation, packet handling, timers, buffering, and socket control
- **POSIX UDP sockets** - network communication
- **Standard C libraries** - file I/O, memory management, timing, and utilities
- **Hashing library** - end-to-end file integrity verification
- **Python and Matplotlib** - experiment automation, analysis, and visualization

C was selected to provide low-level control over datagrams, packet layouts, retransmission state, timers, and receive buffers while keeping the protocol implementation close to the underlying network behavior.

## Development Roadmap

1. Define the packet format and checksum validation rules.
2. Implement basic UDP sender and receiver communication.
3. Add Stop-and-Wait with fixed retransmission timeouts.
4. Add Go-Back-N with cumulative acknowledgements and a sliding window.
5. Add Selective Repeat with receiver buffering and per-packet recovery.
6. Add the channel emulator and test each impairment independently.
7. Add adaptive RTO estimation and Karn's algorithm.
8. Add hash verification, experiment logging, and plotting scripts.
9. Run controlled comparisons and document the results.

Fixed timeouts will be used first to reduce debugging complexity. Adaptive RTO will be introduced only after the basic retransmission behavior is stable.

## Team Responsibilities

| Contributor | Responsibility |
| --- | --- |
| Dimple Mohapatra | UDP communication, packet structure, and checksum |
| Manisha Behera | Stop-and-Wait and Go-Back-N implementations |
| Raushan Kumar | Selective Repeat, timers, and adaptive RTO |
| Mayank Rana | Channel emulator, experiments, and visualization |
| All members | Integration, testing, documentation, and viva preparation |

## Risk Management

| Risk | Mitigation |
| --- | --- |
| Timer and retransmission bugs | Begin with fixed timeouts, then add adaptive RTO after protocol behavior is verified |
| Difficulty isolating network impairments | Test loss, duplication, reordering, corruption, and delay separately before combining them |
| Incorrect or incomplete transfers | Validate checksums per packet and compare complete-file hashes after every test |
| Unfair performance comparisons | Keep packet size, file size, hardware, and experiment conditions consistent |

## Project Status

This repository contains the project specification and will be developed incrementally according to the roadmap above.
