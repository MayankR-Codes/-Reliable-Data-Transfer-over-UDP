/*
 * go_back_n.c
 * ===========
 * Author  : Manisha Behera
 * Day     : 3 — Channel Emulator & Go-Back-N (GBN) Protocol
 *
 * Implements the complete Go-Back-N ARQ protocol:
 *
 *   gbn_sender()
 *     - Sliding window of size N (1 ≤ N ≤ MAX_WINDOW_SIZE).
 *     - Maintains `base` (oldest unacked seq) and `next_seq_num`.
 *     - Sends packets freely while next_seq_num < base + N (window open).
 *     - Stores every sent packet in the WindowBuffer (buffer.h) so they
 *       can be retransmitted without re-reading the file.
 *     - Single base timer: 200 ms non-blocking poll via select().
 *     - On timeout → retransmit ALL packets in [base, next_seq_num).
 *     - Cumulative ACK ack_num → advances base to ack_num + 1,
 *       resets timer if more packets are still in-flight.
 *     - Terminates with PACKET_FIN + FIN-ACK handshake.
 *
 *   gbn_receiver()
 *     - Maintains `expected_seq_num`.
 *     - In-order valid packet → write chunk to disk, send cumulative ACK,
 *       increment expected_seq_num.
 *     - Out-of-order or corrupt packet → discard, re-send ACK for
 *       (expected_seq_num - 1) so sender knows the last good delivery.
 *     - Terminates cleanly on PACKET_FIN.
 *
 * Fixed retransmission timeout : GBN_TIMEOUT_MS  (200 ms)
 * Max consecutive timeout count: GBN_MAX_TIMEOUTS (100, ≈ 20 s)
 */

#include "protocols.h"
#include "buffer.h"
#include "log_utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <winsock2.h>   /* select(), timeval — already via socket_utils */
#else
  #include <sys/select.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif

/* ── Tuning constants ─────────────────────────────────────────────── */
#define GBN_TIMEOUT_MS    200   /* Single base-timer value (ms)         */
#define GBN_MAX_TIMEOUTS  100   /* Abort after this many consecutive TOs */

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

/*
 * now_ms() — monotonic wall-clock in milliseconds.
 * Used to implement the base timer without a background thread.
 */
static long long now_ms(void) {
#ifdef _WIN32
    /* GetTickCount() returns DWORD (wraps ~49 days) — fine for 200 ms timers */
    return (long long)(DWORD)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
#endif
}

/*
 * recv_ack_nonblocking() — poll socket for an ACK packet with a
 * deadline-based timeout.  Returns bytes received, 0 on timeout,
 * negative on error.
 *
 * deadline_ms: absolute time (now_ms()) after which we stop waiting.
 */
static int recv_ack_nonblocking(SOCKET sockfd, Packet *ack_out,
                                struct sockaddr_in *from,
                                long long deadline_ms)
{
    long long remaining = deadline_ms - now_ms();
    if (remaining <= 0) return 0;   /* already past deadline */

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);

    struct timeval tv;
    tv.tv_sec  = (long)(remaining / 1000);
    tv.tv_usec = (long)((remaining % 1000) * 1000);

    int sel = select((int)(sockfd + 1), &rfds, NULL, NULL, &tv);
    if (sel <= 0) return 0;   /* timeout or error */

    return recv_packet(sockfd, ack_out, from);
}

/* ================================================================== */
/*  GBN SENDER                                                         */
/* ================================================================== */

int gbn_sender(const char *filepath, const char *dest_ip,
               int dest_port, int window_size)
{
    if (!filepath || !dest_ip) return -1;
    if (window_size < 1 || window_size > MAX_WINDOW_SIZE) {
        fprintf(stderr,
                "[GBN-SENDER][ERROR] Invalid window size %d "
                "(1 – %d allowed).\n", window_size, MAX_WINDOW_SIZE);
        return -1;
    }

    /* ── Open source file ──────────────────────────────────────── */
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "[GBN-SENDER][ERROR] Cannot open: %s\n", filepath);
        return -1;
    }
    long file_size = get_file_size(filepath);
    printf("[GBN-SENDER] File        : %s  (%ld bytes)\n",
           filepath, file_size);
    printf("[GBN-SENDER] Destination : %s:%d\n", dest_ip, dest_port);
    printf("[GBN-SENDER] Window size : N=%d\n", window_size);
    printf("[GBN-SENDER] Timeout     : %d ms\n\n", GBN_TIMEOUT_MS);

    /* ── Network setup ─────────────────────────────────────────── */
    if (init_network() != 0) { fclose(fp); return -1; }

    SOCKET sockfd = create_udp_socket();
    if (sockfd == INVALID_SOCKET) {
        fclose(fp); cleanup_network(); return -1;
    }
    /* No SO_RCVTIMEO — we use select() with a computed deadline */

    struct sockaddr_in dest_addr;
    setup_sockaddr(&dest_addr, dest_ip, dest_port);

    /* ── Window buffer (Dimple's buffer.h) ─────────────────────── */
    WindowBuffer wb;
    wbuf_init(&wb, window_size);

    /* ── Pre-load all file chunks into a local packet array ───── */
    /*    GBN must retransmit without re-reading file on timeout.  */
    /*    We store packets in the WindowBuffer slots directly.      */

    /* Build entire sequence of DATA packets first */
#define GBN_MAX_PKTS 65536
    Packet *all_pkts = (Packet *)malloc(sizeof(Packet) * GBN_MAX_PKTS);
    if (!all_pkts) {
        fprintf(stderr, "[GBN-SENDER][ERROR] malloc failed.\n");
        fclose(fp); CLOSE_SOCKET(sockfd); cleanup_network(); return -1;
    }

    int total_chunks = 0;
    char chunk_buf[MAX_PAYLOAD_SIZE];

    while (total_chunks < GBN_MAX_PKTS) {
        int n = read_chunk(fp, chunk_buf, MAX_PAYLOAD_SIZE);
        if (n < 0) {
            fprintf(stderr, "[GBN-SENDER][ERROR] File read error.\n");
            free(all_pkts); fclose(fp);
            CLOSE_SOCKET(sockfd); cleanup_network(); return -1;
        }
        if (n == 0) break;   /* EOF */
        init_packet(&all_pkts[total_chunks], PACKET_DATA,
                    (uint32_t)total_chunks, 0,
                    chunk_buf, (uint16_t)n);
        total_chunks++;
    }
    fclose(fp);
    printf("[GBN-SENDER] Loaded %d chunk(s) into memory.\n\n", total_chunks);

    /* ── GBN Sliding Window Transfer ───────────────────────────── */
    uint32_t base         = 0;          /* oldest unACKed seq      */
    uint32_t next_seq     = 0;          /* next seq to send        */
    long     retransmits  = 0;
    int      consec_tos   = 0;          /* consecutive timeouts    */
    int      ret          = 0;
    int      fin_sent     = 0;
    int      fin_acked    = 0;

    long long timer_start = now_ms();   /* base timer start        */

    while (!fin_acked) {

        /* ── (A) Fill window: send new packets ─────────────────── */
        while ((next_seq < base + (uint32_t)window_size) &&
               (next_seq < (uint32_t)total_chunks)) {

            if (base == next_seq) {
                timer_start = now_ms();
            }

            Packet *p = &all_pkts[next_seq];
            p->seq_num  = next_seq;
            p->checksum = 0;
            p->checksum = compute_packet_checksum(p);

            int sent = send_packet(sockfd, p, &dest_addr);
            if (sent < 0) {
                fprintf(stderr,
                        "[GBN-SENDER][ERROR] sendto() failed at seq=%u.\n",
                        next_seq);
                ret = -1; goto done;
            }
            LOG_SEND(p->seq_num, p->length, p->checksum);
            next_seq++;
        }

        /* ── (B) Send FIN if all data chunks are out ────────────── */
        if (!fin_sent && base >= (uint32_t)total_chunks) {
            Packet fin_pkt;
            init_packet(&fin_pkt, PACKET_FIN, next_seq, 0, NULL, 0);
            send_packet(sockfd, &fin_pkt, &dest_addr);
            LOG_SEND_FIN(next_seq);
            fin_sent = 1;
        }

        /* ── (C) Poll for ACKs until timer expires ──────────────── */
        long long deadline = timer_start + GBN_TIMEOUT_MS;

        Packet ack_pkt;
        struct sockaddr_in from;
        int rlen = recv_ack_nonblocking(sockfd, &ack_pkt, &from, deadline);

        if (rlen > 0) {
            /* Received something — validate */
            if (!verify_packet_checksum(&ack_pkt)) {
                LOG_CORRUPT(ack_pkt.ack_num, "Checksum mismatch");
                /* Don't reset timer; let it fire naturally */
                continue;
            }

            if (ack_pkt.type == PACKET_ACK) {
                uint32_t acked = ack_pkt.ack_num;
                LOG_RECV_ACK(acked, 1);

                /* FIN-ACK? */
                if (fin_sent && acked == next_seq) {
                    printf("\n[GBN-SENDER] FIN acknowledged. Transfer complete.\n");
                    fin_acked = 1;
                    consec_tos = 0;
                    break;
                }

                /* Cumulative ACK: advance base */
                if (acked >= base && acked < next_seq) {
                    base = acked + 1;
                    consec_tos = 0;
                    /* Reset timer only if still packets in-flight */
                    if (base < next_seq || fin_sent)
                        timer_start = now_ms();
                }
            }
            continue;
        }

        /* ── (D) Timer fired — retransmit entire window ─────────── */
        consec_tos++;
        if (consec_tos > GBN_MAX_TIMEOUTS) {
            fprintf(stderr,
                    "[GBN-SENDER][ERROR] %d consecutive timeouts. Aborting.\n",
                    GBN_MAX_TIMEOUTS);
            ret = -1; break;
        }

        LOG_TIMEOUT(base);
        printf("[TIMEOUT] base=%u next=%u — Retransmitting window "
               "[%u .. %u]\n", base, next_seq, base, next_seq - 1);

        /* Retransmit all [base .. next_seq) */
        for (uint32_t s = base; s < next_seq; s++) {
            Packet *p = &all_pkts[s];
            send_packet(sockfd, p, &dest_addr);
            printf("[RETX]   Seq=%-5u\n", s);
            retransmits++;
        }

        /* Retransmit FIN too if it was sent */
        if (fin_sent && base >= (uint32_t)total_chunks) {
            Packet fin_pkt;
            init_packet(&fin_pkt, PACKET_FIN, next_seq, 0, NULL, 0);
            send_packet(sockfd, &fin_pkt, &dest_addr);
            printf("[RETX]   FIN  Seq=%-5u\n", next_seq);
        }

        timer_start = now_ms();   /* reset base timer after retransmit */
    }

done:
    /* ── Statistics ─────────────────────────────────────────────── */
    printf("\n[GBN-SENDER] ── Transfer Statistics ──────────────────\n");
    printf("  Total chunks    : %d\n",   total_chunks);
    printf("  Bytes sent      : %ld\n",  file_size);
    printf("  Retransmits     : %ld\n",  retransmits);
    printf("  Consec. timeouts: %d\n",   consec_tos);
    printf("  Status          : %s\n",   ret == 0 ? "SUCCESS" : "FAILED");
    printf("[GBN-SENDER] ───────────────────────────────────────\n\n");

    free(all_pkts);
    CLOSE_SOCKET(sockfd);
    cleanup_network();
    return ret;
}

/* ================================================================== */
/*  GBN RECEIVER                                                       */
/* ================================================================== */

int gbn_receiver(int listen_port, const char *output_path)
{
    if (!output_path) return -1;

    printf("[GBN-RECEIVER] Listening on port %d\n", listen_port);
    printf("[GBN-RECEIVER] Output file: %s\n\n", output_path);

    /* ── Open output file ──────────────────────────────────────── */
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "[GBN-RECEIVER][ERROR] Cannot open output: %s\n",
                output_path);
        return -1;
    }

    /* ── Network setup ─────────────────────────────────────────── */
    if (init_network() != 0) { fclose(fp); return -1; }

    SOCKET sockfd = create_udp_socket();
    if (sockfd == INVALID_SOCKET) {
        fclose(fp); cleanup_network(); return -1;
    }

    if (bind_udp_socket(sockfd, listen_port) != 0) {
        fprintf(stderr,
                "[GBN-RECEIVER][ERROR] Bind failed on port %d\n",
                listen_port);
        CLOSE_SOCKET(sockfd); fclose(fp); cleanup_network(); return -1;
    }

    /* ── Receive loop ───────────────────────────────────────────── */
    uint32_t expected_seq = 0;
    long     bytes_recv   = 0;
    long     discarded    = 0;
    long     corrupted    = 0;
    int      chunks_recv  = 0;
    int      ret          = 0;

    struct sockaddr_in sender_addr;

    while (1) {
        Packet pkt;
        int rlen = recv_packet(sockfd, &pkt, &sender_addr);
        if (rlen <= 0) continue;   /* keep waiting */

        /* ── Checksum validation ────────────────────────────────── */
        if (!verify_packet_checksum(&pkt)) {
            corrupted++;
            LOG_CORRUPT(pkt.seq_num, "Checksum mismatch");
            /* Re-ACK the last good cumulative sequence */
            if (expected_seq > 0) {
                Packet ack;
                init_packet(&ack, PACKET_ACK, 0, expected_seq - 1, NULL, 0);
                send_packet(sockfd, &ack, &sender_addr);
                LOG_SEND_ACK(expected_seq - 1);
            }
            continue;
        }

        /* ── FIN packet ─────────────────────────────────────────── */
        if (pkt.type == PACKET_FIN) {
            printf("[RECV] FIN Seq=%-5u — Sending FIN-ACK\n", pkt.seq_num);
            Packet fin_ack;
            init_packet(&fin_ack, PACKET_ACK, 0, pkt.seq_num, NULL, 0);
            send_packet(sockfd, &fin_ack, &sender_addr);
            LOG_SEND_ACK(pkt.seq_num);
            break;
        }

        /* ── DATA packet ────────────────────────────────────────── */
        if (pkt.type == PACKET_DATA) {

            if (pkt.seq_num == expected_seq) {
                /* ── In-order: write and ACK ────────────────────── */
                int written = write_chunk(fp, pkt.payload, pkt.length);
                if (written < 0) {
                    fprintf(stderr,
                            "[GBN-RECEIVER][ERROR] File write error.\n");
                    ret = -1; break;
                }

                bytes_recv += pkt.length;
                chunks_recv++;
                LOG_RECV_DATA(pkt.seq_num, pkt.length, 1);

                Packet ack;
                init_packet(&ack, PACKET_ACK, 0, expected_seq, NULL, 0);
                send_packet(sockfd, &ack, &sender_addr);
                LOG_SEND_ACK(expected_seq);

                expected_seq++;

            } else {
                /* ── Out-of-order: discard, re-ACK last good ───── */
                discarded++;
                printf("[DISCARD] Seq=%-5u (expected %u) — Re-ACK=%u\n",
                       pkt.seq_num, expected_seq,
                       expected_seq > 0 ? expected_seq - 1 : 0);

                Packet ack;
                uint32_t last_good = (expected_seq > 0)
                                     ? expected_seq - 1 : 0;
                init_packet(&ack, PACKET_ACK, 0, last_good, NULL, 0);
                send_packet(sockfd, &ack, &sender_addr);
                LOG_SEND_ACK(last_good);
            }
        }
    }

    /* ── Statistics ─────────────────────────────────────────────── */
    printf("\n[GBN-RECEIVER] ── Transfer Statistics ────────────────\n");
    printf("  Chunks received : %d\n",   chunks_recv);
    printf("  Bytes received  : %ld\n",  bytes_recv);
    printf("  Out-of-order    : %ld\n",  discarded);
    printf("  Corrupted pkts  : %ld\n",  corrupted);
    printf("  Status          : %s\n",   ret == 0 ? "SUCCESS" : "FAILED");
    printf("[GBN-RECEIVER] ───────────────────────────────────────\n\n");

    fflush(fp);
    fclose(fp);
    CLOSE_SOCKET(sockfd);
    cleanup_network();
    return ret;
}
