/*
 * stop_and_wait.c
 * ===============
 * Authors : Manisha Behera & Raushan Kumar
 * Day     : 2 — File Chunking & Stop-and-Wait (SW) Protocol
 *
 * Implements the complete Stop-and-Wait ARQ protocol:
 *
 *   sw_sender()   — reads any file in 1024-byte chunks, sends each
 *                   chunk as a PACKET_DATA datagram, waits for an ACK
 *                   with a 200 ms socket timeout (SO_RCVTIMEO / select()),
 *                   and retransmits on timeout.  seq_num alternates 0 ↔ 1
 *                   (1-bit SW). Closes the session with PACKET_FIN.
 *
 *   sw_receiver() — listens for PACKET_DATA, validates CRC32,
 *                   writes valid in-order chunks to the output file,
 *                   and sends PACKET_ACK.  Duplicates / corrupt pkts
 *                   trigger a re-ACK of the last good seq_num.
 *                   Exits cleanly on PACKET_FIN.
 *
 * Fixed retransmission timeout: SW_TIMEOUT_MS  (200 ms)
 * Maximum retransmit attempts : SW_MAX_RETRIES (50, ~10 s total)
 */

#include "protocols.h"
#include "log_utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Tuning constants ────────────────────────────────────────────── */
#define SW_TIMEOUT_MS   200   /* Socket receive-timeout in milliseconds */
#define SW_MAX_RETRIES   50   /* Max retransmissions per packet          */

/* ================================================================== */
/*  SENDER                                                             */
/* ================================================================== */

int sw_sender(const char *filepath, const char *dest_ip, int dest_port)
{
    if (!filepath || !dest_ip) return -1;

    /* ── Open source file ─────────────────────────────────────── */
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "[SW-SENDER][ERROR] Cannot open file: %s\n", filepath);
        return -1;
    }

    long file_size = get_file_size(filepath);
    printf("[SW-SENDER] File : %s  (%ld bytes)\n", filepath, file_size);
    printf("[SW-SENDER] Dest : %s:%d\n", dest_ip, dest_port);
    printf("[SW-SENDER] Timeout : %d ms | MaxRetries : %d\n\n",
           SW_TIMEOUT_MS, SW_MAX_RETRIES);

    /* ── Create UDP socket ────────────────────────────────────── */
    if (init_network() != 0) { fclose(fp); return -1; }

    SOCKET sockfd = create_udp_socket();
    if (sockfd == INVALID_SOCKET) { fclose(fp); cleanup_network(); return -1; }

    /* Apply 200 ms receive timeout so recvfrom() returns on loss */
    if (set_socket_timeout(sockfd, SW_TIMEOUT_MS) != 0) {
        CLOSE_SOCKET(sockfd); fclose(fp); cleanup_network(); return -1;
    }

    struct sockaddr_in dest_addr;
    setup_sockaddr(&dest_addr, dest_ip, dest_port);

    /* ── Transfer loop ────────────────────────────────────────── */
    uint32_t seq_num       = 0;   /* toggles 0 ↔ 1                  */
    long     bytes_sent    = 0;
    long     retransmits   = 0;
    int      chunk_count   = 0;
    int      ret           = 0;

    char chunk_buf[MAX_PAYLOAD_SIZE];

    while (1) {
        /* Read next chunk from file */
        int chunk_len = read_chunk(fp, chunk_buf, MAX_PAYLOAD_SIZE);
        if (chunk_len < 0) {
            fprintf(stderr, "[SW-SENDER][ERROR] File read error.\n");
            ret = -1; break;
        }

        /* Build DATA packet (chunk_len == 0 means EOF — send FIN) */
        Packet pkt;
        if (chunk_len == 0) {
            /* End of file — send FIN */
            init_packet(&pkt, PACKET_FIN, seq_num, 0, NULL, 0);
        } else {
            init_packet(&pkt, PACKET_DATA, seq_num, 0,
                        chunk_buf, (uint16_t)chunk_len);
        }

        /* ── ARQ retransmission loop ──────────────────────────── */
        int tries = 0;
        int acked = 0;

        while (!acked && tries < SW_MAX_RETRIES) {

            /* Send packet */
            int sent = send_packet(sockfd, &pkt, &dest_addr);
            if (sent < 0) {
                fprintf(stderr, "[SW-SENDER][ERROR] sendto() failed.\n");
                ret = -1; goto done;
            }

            if (pkt.type == PACKET_DATA) {
                if (tries == 0)
                    LOG_SEND(pkt.seq_num, pkt.length, pkt.checksum);
                else
                    LOG_TIMEOUT_ATTEMPT(pkt.seq_num, tries + 1);
            } else {
                LOG_SEND_FIN(pkt.seq_num);
            }

            /* Wait for ACK */
            Packet ack_pkt;
            struct sockaddr_in from_addr;
            int recv_len = recv_packet(sockfd, &ack_pkt, &from_addr);

            if (recv_len <= 0) {
                /* Timeout or error — retransmit */
                tries++;
                if (tries < SW_MAX_RETRIES) retransmits++;
                continue;
            }

            /* Validate ACK */
            if (!verify_packet_checksum(&ack_pkt)) {
                LOG_CORRUPT(ack_pkt.ack_num, "Checksum mismatch");
                tries++;
                retransmits++;
                continue;
            }

            if (ack_pkt.type == PACKET_ACK && ack_pkt.ack_num == seq_num) {
                /* ── Correct ACK ──────────────────────────────── */
                LOG_RECV_ACK(ack_pkt.ack_num, 1);
                acked = 1;
            } else {
                /* Stale / duplicate ACK — retransmit */
                printf("[RECV] Stale ACK=%-4u (expected %u), retransmitting.\n",
                       ack_pkt.ack_num, seq_num);
                tries++;
                retransmits++;
            }
        }

        if (!acked) {
            fprintf(stderr,
                    "[SW-SENDER][ERROR] Packet Seq=%u unacknowledged after "
                    "%d retries. Aborting.\n", seq_num, SW_MAX_RETRIES);
            ret = -1; break;
        }

        /* Check if we just finished with FIN */
        if (pkt.type == PACKET_FIN) {
            printf("\n[SW-SENDER] FIN acknowledged. Transfer complete.\n");
            break;
        }

        /* Advance to next chunk */
        bytes_sent += chunk_len;
        chunk_count++;
        seq_num ^= 1;   /* toggle 0 ↔ 1 */
    }

done:
    /* ── Statistics ───────────────────────────────────────────── */
    printf("\n[SW-SENDER] ── Transfer Statistics ───────────────────\n");
    printf("  Chunks sent   : %d\n", chunk_count);
    printf("  Bytes sent    : %ld\n", bytes_sent);
    printf("  Retransmits   : %ld\n", retransmits);
    printf("  Status        : %s\n", ret == 0 ? "SUCCESS" : "FAILED");
    printf("[SW-SENDER] ─────────────────────────────────────────\n\n");

    fclose(fp);
    CLOSE_SOCKET(sockfd);
    cleanup_network();
    return ret;
}

/* ================================================================== */
/*  RECEIVER                                                           */
/* ================================================================== */

int sw_receiver(int listen_port, const char *output_path)
{
    if (!output_path) return -1;

    printf("[SW-RECEIVER] Listening on port %d\n", listen_port);
    printf("[SW-RECEIVER] Output file: %s\n\n", output_path);

    /* ── Open output file ─────────────────────────────────────── */
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "[SW-RECEIVER][ERROR] Cannot open output: %s\n",
                output_path);
        return -1;
    }

    /* ── Create & bind UDP socket ─────────────────────────────── */
    if (init_network() != 0) { fclose(fp); return -1; }

    SOCKET sockfd = create_udp_socket();
    if (sockfd == INVALID_SOCKET) {
        fclose(fp); cleanup_network(); return -1;
    }

    if (bind_udp_socket(sockfd, listen_port) != 0) {
        fprintf(stderr, "[SW-RECEIVER][ERROR] Bind failed on port %d\n",
                listen_port);
        CLOSE_SOCKET(sockfd); fclose(fp); cleanup_network(); return -1;
    }

    /* ── Receive loop ─────────────────────────────────────────── */
    uint32_t expected_seq = 0;    /* toggles 0 ↔ 1              */
    long     bytes_recv   = 0;
    long     duplicates   = 0;
    long     corrupted    = 0;
    int      chunks_recv  = 0;
    int      ret          = 0;

    struct sockaddr_in sender_addr;

    while (1) {
        Packet pkt;
        int recv_len = recv_packet(sockfd, &pkt, &sender_addr);

        if (recv_len <= 0) {
            /* No data yet — keep waiting (no timeout on receiver) */
            continue;
        }

        /* ── Checksum validation ──────────────────────────────── */
        if (!verify_packet_checksum(&pkt)) {
            corrupted++;
            LOG_CORRUPT(pkt.seq_num, "Checksum mismatch");

            /* Re-ACK the last successfully received packet */
            Packet ack;
            uint32_t last_good = expected_seq ^ 1;   /* previous seq */
            init_packet(&ack, PACKET_ACK, 0, last_good, NULL, 0);
            send_packet(sockfd, &ack, &sender_addr);
            LOG_SEND_ACK(last_good);
            continue;
        }

        /* ── FIN packet: transfer complete ────────────────────── */
        if (pkt.type == PACKET_FIN) {
            printf("[RECV] FIN Seq=%-4u — Sending FIN-ACK\n", pkt.seq_num);
            Packet fin_ack;
            init_packet(&fin_ack, PACKET_ACK, 0, pkt.seq_num, NULL, 0);
            send_packet(sockfd, &fin_ack, &sender_addr);
            LOG_SEND_ACK(pkt.seq_num);
            break;
        }

        /* ── DATA packet ──────────────────────────────────────── */
        if (pkt.type == PACKET_DATA) {

            if (pkt.seq_num == expected_seq) {
                /* In-order packet: write to disk, send ACK */
                int written = write_chunk(fp, pkt.payload, pkt.length);
                if (written < 0) {
                    fprintf(stderr,
                            "[SW-RECEIVER][ERROR] File write error.\n");
                    ret = -1; break;
                }

                bytes_recv += pkt.length;
                chunks_recv++;
                LOG_RECV_DATA(pkt.seq_num, pkt.length, 1);

                Packet ack;
                init_packet(&ack, PACKET_ACK, 0, pkt.seq_num, NULL, 0);
                send_packet(sockfd, &ack, &sender_addr);
                LOG_SEND_ACK(pkt.seq_num);

                expected_seq ^= 1;   /* toggle expected: 0 ↔ 1 */

            } else {
                /* Duplicate / out-of-order: re-ACK last good seq */
                duplicates++;
                uint32_t last_good = expected_seq ^ 1;
                printf("[RECV] Duplicate Seq=%-4u (expected %u) — Re-ACK %u\n",
                       pkt.seq_num, expected_seq, last_good);

                Packet ack;
                init_packet(&ack, PACKET_ACK, 0, last_good, NULL, 0);
                send_packet(sockfd, &ack, &sender_addr);
                LOG_SEND_ACK(last_good);
            }
        }
    }

    /* ── Statistics ───────────────────────────────────────────── */
    printf("\n[SW-RECEIVER] ── Transfer Statistics ─────────────────\n");
    printf("  Chunks received : %d\n", chunks_recv);
    printf("  Bytes received  : %ld\n", bytes_recv);
    printf("  Duplicates      : %ld\n", duplicates);
    printf("  Corrupted pkts  : %ld\n", corrupted);
    printf("  Status          : %s\n", ret == 0 ? "SUCCESS" : "FAILED");
    printf("[SW-RECEIVER] ───────────────────────────────────────\n\n");

    fflush(fp);
    fclose(fp);
    CLOSE_SOCKET(sockfd);
    cleanup_network();
    return ret;
}
