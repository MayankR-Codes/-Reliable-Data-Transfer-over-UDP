/*
 * protocols.h
 * ===========
 * Authors : Manisha Behera (SW, GBN) — Raushan Kumar (SR)
 * Purpose : Declares the public API for all three ARQ protocol
 *           implementations: Stop-and-Wait, Go-Back-N, Selective Repeat.
 *
 * Each protocol exposes two symmetric functions:
 *   <proto>_sender()   — reads file, sends packets, handles retransmission
 *   <proto>_receiver() — receives packets, writes file, sends ACKs
 *
 * Return conventions:
 *   0   = transfer completed successfully
 *  -1   = fatal error (socket failure, file I/O error, etc.)
 */

#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include "socket_utils.h"   /* SOCKET, sockaddr_in, send/recv helpers  */
#include "file_io.h"        /* read_chunk, write_chunk, compute_sha256  */

/* ===================================================================== */
/*  Stop-and-Wait (SW) ARQ  — Manisha Behera, Day 2                      */
/* ===================================================================== */

/*
 * sw_sender — Stop-and-Wait sender.
 *
 * Opens 'filepath', reads it in MAX_PAYLOAD_SIZE chunks, and sends each
 * chunk as a PACKET_DATA datagram to (dest_ip, dest_port).
 * Uses a 200 ms socket receive-timeout (SO_RCVTIMEO) to detect loss;
 * retransmits the same packet until a valid PACKET_ACK for the current
 * seq_num is received.  seq_num toggles between 0 and 1 (1-bit SW).
 * Terminates the transfer by sending PACKET_FIN and awaiting its ACK.
 *
 * Parameters:
 *   filepath   — path to the input file
 *   dest_ip    — receiver's IP address (dotted-decimal string)
 *   dest_port  — receiver's UDP port number
 *
 * Returns: 0 on success, -1 on error.
 */
int sw_sender(const char *filepath, const char *dest_ip, int dest_port);

/*
 * sw_receiver — Stop-and-Wait receiver.
 *
 * Binds to listen_port, receives PACKET_DATA datagrams, validates CRC32,
 * writes valid in-order chunks to 'output_path', and replies with
 * PACKET_ACK.  Duplicates (wrong seq_num) or corrupted packets trigger
 * a re-ACK of the last successfully received seq_num.
 * Terminates cleanly when PACKET_FIN is received.
 *
 * Parameters:
 *   listen_port — local UDP port to bind
 *   output_path — path to write the received file
 *
 * Returns: 0 on success, -1 on error.
 */
int sw_receiver(int listen_port, const char *output_path);

/* ===================================================================== */
/*  Go-Back-N (GBN) ARQ — Manisha Behera, Day 3                          */
/* ===================================================================== */

/*
 * gbn_sender — Go-Back-N sender.
 *
 * Sliding window of size 'window_size' (≤ MAX_WINDOW_SIZE).
 * Maintains base and next_seq_num; keeps a single base timer (200 ms).
 * On timeout: retransmits ALL packets in [base, next_seq_num).
 * Cumulative ACKs advance the base.
 *
 * Parameters:
 *   filepath    — path to the input file
 *   dest_ip     — receiver's IP address
 *   dest_port   — receiver's UDP port
 *   window_size — sliding window size N (1 ≤ N ≤ MAX_WINDOW_SIZE)
 *
 * Returns: 0 on success, -1 on error.
 */
int gbn_sender(const char *filepath, const char *dest_ip,
               int dest_port, int window_size);

/*
 * gbn_receiver — Go-Back-N receiver.
 *
 * Maintains expected_seq_num.  Accepts only the next in-order packet;
 * discards out-of-order or corrupt packets and re-ACKs the last
 * cumulative sequence number received.
 *
 * Parameters:
 *   listen_port — local UDP port to bind
 *   output_path — path to write the received file
 *
 * Returns: 0 on success, -1 on error.
 */
int gbn_receiver(int listen_port, const char *output_path);

/* ===================================================================== */
/*  Selective Repeat (SR) ARQ — Raushan Kumar, Day 4                      */
/* ===================================================================== */

/*
 * sr_sender — Selective Repeat sender.
 *
 * Per-packet 200 ms timeout; retransmits ONLY the individual packet
 * that timed out.  Selective ACKs mark individual slots in the window
 * buffer.  Window advances when the base packet is acknowledged.
 *
 * Parameters:
 *   filepath    — path to the input file
 *   dest_ip     — receiver's IP address
 *   dest_port   — receiver's UDP port
 *   window_size — sliding window size N (1 ≤ N ≤ MAX_WINDOW_SIZE/2)
 *
 * Returns: 0 on success, -1 on error.
 */
int sr_sender(const char *filepath, const char *dest_ip,
              int dest_port, int window_size);

/*
 * sr_receiver — Selective Repeat receiver.
 *
 * Maintains a circular receive buffer of size window_size.  Buffers
 * out-of-order packets and sends a selective ACK for each.  When the
 * base slot fills, flushes all contiguous buffered packets to disk.
 *
 * Parameters:
 *   listen_port — local UDP port to bind
 *   output_path — path to write the received file
 *
 * Returns: 0 on success, -1 on error.
 */
int sr_receiver(int listen_port, const char *output_path);

#endif /* PROTOCOLS_H */
