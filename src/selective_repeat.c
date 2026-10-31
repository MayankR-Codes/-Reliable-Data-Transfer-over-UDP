/*
 * selective_repeat.c
 * ==================
 * Authors : Raushan Kumar & Team
 * Day     : 4 — Selective Repeat (SR) ARQ & Out-of-Order Buffer
 *
 * Implements the Selective Repeat ARQ protocol:
 *
 *   sr_sender()
 *     - Sliding window of size N (N <= MAX_WINDOW_SIZE / 2).
 *     - Per-packet tracking: each slot maintains its own 200 ms timeout timer.
 *     - Retransmits ONLY individual packets whose elapsed time exceeds 200 ms.
 *     - On receiving selective ACK(k), marks packet k as ACKED.
 *     - Window base advances when the lowest unacknowledged packet is ACKed.
 *     - Terminates via PACKET_FIN handshake.
 *
 *   sr_receiver()
 *     - Maintains a circular receiver buffer of size N.
 *     - Packets arriving within [rcv_base, rcv_base + N) are buffered and
 *       individually ACKed.
 *     - When packet at rcv_base arrives, flushes contiguous buffered chunks
 *       to file and slides rcv_base forward.
 *     - Duplicate packets (seq < rcv_base) are re-ACKed so sender advances.
 *     - Terminates cleanly on PACKET_FIN.
 */

#include "protocols.h"
#include "buffer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <sys/select.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif

#define SR_TIMEOUT_MS    200   /* Per-packet retransmission timeout (ms) */
#define SR_MAX_TIMEOUTS  100   /* Max consecutive idle timeouts          */

static long long sr_now_ms(void) {
#ifdef _WIN32
    return (long long)(DWORD)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
#endif
}

static int sr_recv_nonblocking(SOCKET sockfd, Packet *pkt_out,
                               struct sockaddr_in *from, int wait_ms)
{
    if (wait_ms < 0) wait_ms = 0;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);

    struct timeval tv;
    tv.tv_sec  = wait_ms / 1000;
    tv.tv_usec = (wait_ms % 1000) * 1000;

    int sel = select((int)(sockfd + 1), &rfds, NULL, NULL, &tv);
    if (sel <= 0) return 0;

    return recv_packet(sockfd, pkt_out, from);
}

/* ================================================================== */
/*  SR SENDER                                                         */
/* ================================================================== */

int sr_sender(const char *filepath, const char *dest_ip,
              int dest_port, int window_size)
{
    if (!filepath || !dest_ip) return -1;
    if (window_size < 1) window_size = 8;
    if (window_size > MAX_WINDOW_SIZE / 2) window_size = MAX_WINDOW_SIZE / 2;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "[SR-SENDER][ERROR] Cannot open file: %s\n", filepath);
        return -1;
    }

    long file_size = get_file_size(filepath);
    printf("[SR-SENDER] File        : %s  (%ld bytes)\n", filepath, file_size);
    printf("[SR-SENDER] Destination : %s:%d\n", dest_ip, dest_port);
    printf("[SR-SENDER] Window size : N=%d\n", window_size);
    printf("[SR-SENDER] Timeout     : %d ms (per-packet)\n\n", SR_TIMEOUT_MS);

    /* Read all chunks into memory */
    size_t cap = 64;
    size_t total_chunks = 0;
    Packet *chunks = (Packet *)malloc(cap * sizeof(Packet));
    if (!chunks) { fclose(fp); return -1; }

    char chunk_buf[MAX_PAYLOAD_SIZE];
    int bytes_read = 0;
    uint32_t seq = 0;

    while ((bytes_read = read_chunk(fp, chunk_buf, MAX_PAYLOAD_SIZE)) > 0) {
        if (total_chunks >= cap) {
            cap *= 2;
            Packet *tmp = (Packet *)realloc(chunks, cap * sizeof(Packet));
            if (!tmp) { free(chunks); fclose(fp); return -1; }
            chunks = tmp;
        }
        init_packet(&chunks[total_chunks], PACKET_DATA, seq, 0,
                    chunk_buf, (uint16_t)bytes_read);
        total_chunks++;
        seq++;
    }
    fclose(fp);

    printf("[SR-SENDER] Loaded %lu chunk(s) into memory.\n\n",
           (unsigned long)total_chunks);

    if (init_network() != 0) { free(chunks); return -1; }

    SOCKET sockfd = create_udp_socket();
    if (sockfd == INVALID_SOCKET) {
        cleanup_network();
        free(chunks);
        return -1;
    }

    struct sockaddr_in dest_addr;
    setup_sockaddr(&dest_addr, dest_ip, dest_port);

    WindowBuffer wb;
    wbuf_init(&wb, window_size);

    uint32_t next_to_send = 0;
    unsigned long retransmits = 0;
    int consecutive_idle = 0;

    /* Main transmission loop */
    while (wb.base < total_chunks) {
        /* 1. Send packets while window is not full and data remains */
        while (!wbuf_is_full(&wb) && next_to_send < total_chunks) {
            long long now = sr_now_ms();
            wbuf_push(&wb, &chunks[next_to_send], now);
            send_packet(sockfd, &chunks[next_to_send], &dest_addr);
            printf("[SEND] Seq=%-5u Length=%-4u Checksum=0x%08X\n",
                   chunks[next_to_send].seq_num,
                   chunks[next_to_send].length,
                   chunks[next_to_send].checksum);
            next_to_send++;
        }

        /* 2. Check per-packet timeouts for all in-flight slots */
        long long current_time = sr_now_ms();
        for (uint32_t s = wb.base; s < wb.next_seq_num; s++) {
            WindowSlot *slot = wbuf_get_slot(&wb, s);
            if (slot && slot->status == SLOT_SENT) {
                if (current_time - slot->sent_time_ms >= SR_TIMEOUT_MS) {
                    /* Retransmit ONLY this individual timed-out packet */
                    slot->sent_time_ms = current_time;
                    retransmits++;
                    printf("[TIMEOUT] Packet=%-5u Retransmitting...\n", slot->seq_num);
                    send_packet(sockfd, &slot->pkt, &dest_addr);
                }
            }
        }

        /* 3. Non-blocking poll for incoming ACKs */
        Packet ack_pkt;
        struct sockaddr_in from_addr;
        int recvd = sr_recv_nonblocking(sockfd, &ack_pkt, &from_addr, 10);

        if (recvd > 0) {
            consecutive_idle = 0;
            if (!verify_packet_checksum(&ack_pkt)) {
                printf("[CORRUPT] Packet=%-5u Discarded (Checksum mismatch)\n", ack_pkt.ack_num);
                continue;
            }
            if (ack_pkt.type == PACKET_ACK) {
                uint32_t ack_num = ack_pkt.ack_num;
                printf("[RECV] Ack=%-5u Valid=YES\n", ack_num);
                wbuf_selective_ack(&wb, ack_num);
            }
        } else {
            consecutive_idle++;
            if (consecutive_idle >= SR_MAX_TIMEOUTS * 20) {
                fprintf(stderr, "[SR-SENDER][ERROR] Exceeded max idle attempts.\n");
                break;
            }
        }
    }

    /* 4. FIN Handshake */
    Packet fin_pkt, fin_ack;
    init_packet(&fin_pkt, PACKET_FIN, (uint32_t)total_chunks, 0, NULL, 0);

    set_socket_timeout(sockfd, 200);
    for (int attempt = 0; attempt < 20; attempt++) {
        printf("[SEND] FIN  Seq=%-5u\n", fin_pkt.seq_num);
        send_packet(sockfd, &fin_pkt, &dest_addr);

        struct sockaddr_in from_addr;
        int b = recv_packet(sockfd, &fin_ack, &from_addr);
        if (b > 0 && verify_packet_checksum(&fin_ack) && fin_ack.type == PACKET_ACK) {
            printf("[RECV] Ack=%-5u Valid=YES\n", fin_ack.ack_num);
            printf("\n[SR-SENDER] FIN acknowledged. Transfer complete.\n");
            break;
        }
    }

    printf("\n[SR-SENDER] ── Transfer Statistics ──────────────────\n");
    printf("  Total chunks    : %lu\n", (unsigned long)total_chunks);
    printf("  Bytes sent      : %ld\n", file_size);
    printf("  Retransmissions : %lu\n", retransmits);
    printf("  Status          : %s\n", (wb.base >= total_chunks) ? "SUCCESS" : "INCOMPLETE");
    printf("[SR-SENDER] ─────────────────────────────────────────\n\n");

    CLOSE_SOCKET(sockfd);
    cleanup_network();
    free(chunks);
    return (wb.base >= total_chunks) ? 0 : -1;
}

/* ================================================================== */
/*  SR RECEIVER                                                       */
/* ================================================================== */

int sr_receiver(int listen_port, const char *output_path)
{
    if (!output_path) return -1;

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "[SR-RECEIVER][ERROR] Cannot open output file: %s\n", output_path);
        return -1;
    }

    if (init_network() != 0) { fclose(fp); return -1; }

    SOCKET sockfd = create_udp_socket();
    if (sockfd == INVALID_SOCKET) {
        cleanup_network();
        fclose(fp);
        return -1;
    }

    if (bind_udp_socket(sockfd, listen_port) != 0) {
        fprintf(stderr, "[SR-RECEIVER][ERROR] Cannot bind to port %d\n", listen_port);
        CLOSE_SOCKET(sockfd);
        cleanup_network();
        fclose(fp);
        return -1;
    }

    printf("[SR-RECEIVER] Listening on port %d\n", listen_port);
    printf("[SR-RECEIVER] Output file: %s\n\n", output_path);

    int window_size = 16;
    RecvBuffer rbuf;
    rbuf_init(&rbuf, window_size);

    unsigned long chunks_written = 0;
    unsigned long bytes_written = 0;
    unsigned long corrupt_pkts = 0;
    unsigned long out_of_order = 0;

    Packet rx_pkt;
    struct sockaddr_in sender_addr;

    while (1) {
        int bytes = recv_packet(sockfd, &rx_pkt, &sender_addr);
        if (bytes <= 0) continue;

        /* Corrupted packet detection */
        if (!verify_packet_checksum(&rx_pkt)) {
            corrupt_pkts++;
            printf("[CORRUPT] Packet=%-5u Discarded (Checksum mismatch)\n", rx_pkt.seq_num);
            continue;
        }

        /* FIN Handshake */
        if (rx_pkt.type == PACKET_FIN) {
            printf("[RECV] FIN Seq=%u    — Sending FIN-ACK\n", rx_pkt.seq_num);
            Packet fin_ack;
            init_packet(&fin_ack, PACKET_ACK, 0, rx_pkt.seq_num, NULL, 0);
            send_packet(sockfd, &fin_ack, &sender_addr);
            printf("[SEND] Ack=%-5u\n", rx_pkt.seq_num);
            break;
        }

        if (rx_pkt.type != PACKET_DATA) continue;

        uint32_t seq = rx_pkt.seq_num;

        /* Case 1: Packet within receive window [rcv_base, rcv_base + window_size) */
        if (rbuf_is_in_window(&rbuf, seq)) {
            printf("[RECV] Seq=%-5u Length=%-4u Valid=YES\n", seq, rx_pkt.length);
            if (seq != rbuf.rcv_base && !rbuf_is_buffered(&rbuf, seq)) {
                out_of_order++;
            }

            /* Store in memory-safe circular buffer */
            rbuf_put(&rbuf, &rx_pkt);

            /* Send selective ACK for received packet */
            Packet ack;
            init_packet(&ack, PACKET_ACK, 0, seq, NULL, 0);
            send_packet(sockfd, &ack, &sender_addr);
            printf("[SEND] Ack=%-5u\n", seq);

            /* Flush contiguous packets if base slot is filled */
            rbuf_flush_contiguous(&rbuf, fp, &bytes_written, &chunks_written);
        }
        /* Case 2: Duplicate packet already acknowledged (seq < rcv_base) */
        else if ((int32_t)(seq - rbuf.rcv_base) < 0 && (rbuf.rcv_base - seq) <= (uint32_t)window_size) {
            /* Re-send ACK so sender can advance its window */
            Packet ack;
            init_packet(&ack, PACKET_ACK, 0, seq, NULL, 0);
            send_packet(sockfd, &ack, &sender_addr);
            printf("[SEND] Ack=%-5u\n", seq);
        }
    }

    fflush(fp);
    fclose(fp);

    printf("\n[SR-RECEIVER] ── Transfer Statistics ────────────────\n");
    printf("  Chunks received : %lu\n", chunks_written);
    printf("  Bytes received  : %lu\n", bytes_written);
    printf("  Out-of-order    : %lu\n", out_of_order);
    printf("  Corrupted pkts  : %lu\n", corrupt_pkts);
    printf("  Status          : SUCCESS\n");
    printf("[SR-RECEIVER] ───────────────────────────────────────\n\n");

    CLOSE_SOCKET(sockfd);
    cleanup_network();
    return 0;
}
