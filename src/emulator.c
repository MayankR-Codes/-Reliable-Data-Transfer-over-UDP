#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "socket_utils.h"

#ifdef _WIN32
    #include <windows.h>
    static void sleep_ms(int ms) { if (ms > 0) Sleep((DWORD)ms); }
#else
    #include <unistd.h>
    static void sleep_ms(int ms) { if (ms > 0) usleep((useconds_t)(ms * 1000)); }
#endif

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int    listen_port      = 8000;
    int    receiver_port    = 9001;
    char   receiver_ip[64]  = "127.0.0.1";
    double loss_rate        = 0.0;  /* 0.0 – 1.0, e.g. 0.15 = 15% */
    double corrupt_rate     = 0.0;  /* DATA packet corruption rate */
    int    delay_ms         = 0;    /* one-way latency in milliseconds */

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--recv-port") == 0 && i + 1 < argc) {
            receiver_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--recv-ip") == 0 && i + 1 < argc) {
            strncpy(receiver_ip, argv[++i], sizeof(receiver_ip) - 1);
        } else if (strcmp(argv[i], "--loss") == 0 && i + 1 < argc) {
            double val = atof(argv[++i]);
            loss_rate = (val > 1.0) ? (val / 100.0) : val;
            if (loss_rate < 0.0) loss_rate = 0.0;
            if (loss_rate > 1.0) loss_rate = 1.0;
        } else if (strcmp(argv[i], "--corrupt") == 0 && i + 1 < argc) {
            double val = atof(argv[++i]);
            corrupt_rate = (val > 1.0) ? (val / 100.0) : val;
            if (corrupt_rate < 0.0) corrupt_rate = 0.0;
            if (corrupt_rate > 1.0) corrupt_rate = 1.0;
        } else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay_ms = atoi(argv[++i]);
            if (delay_ms < 0) delay_ms = 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  --listen <port>      Emulator listen port      (default: 8000)\n");
            printf("  --recv-ip <ip>       Receiver IP address       (default: 127.0.0.1)\n");
            printf("  --recv-port <port>   Receiver port             (default: 9001)\n");
            printf("  --loss <percent>     DATA packet drop rate     (e.g., 15 for 15%%)\n");
            printf("  --corrupt <percent>  DATA bit corruption rate  (e.g., 5 for 5%%)\n");
            printf("  --delay <ms>         Artificial latency in ms  (default: 0)\n");
            printf("  --help, -h           Show this help message\n");
            return 0;
        }
    }

    srand((unsigned int)time(NULL));

    printf("====================================================\n");
    printf("   Channel Emulator (Proxy) — Loss / Delay / Corrupt\n");
    printf("====================================================\n");
    printf("Listening on Port        : %d\n", listen_port);
    printf("Forwarding target (Recv) : %s:%d\n", receiver_ip, receiver_port);
    printf("DATA Packet Loss Rate    : %.1f%%\n", loss_rate * 100.0);
    printf("DATA Corrupt Rate        : %.1f%%\n", corrupt_rate * 100.0);
    printf("Network Delay            : %d ms\n", delay_ms);
    printf("====================================================\n\n");

    if (init_network() != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize network subsystem.\n");
        return 1;
    }

    SOCKET emu_sock = create_udp_socket();
    if (emu_sock == INVALID_SOCKET) { cleanup_network(); return 1; }

    if (bind_udp_socket(emu_sock, listen_port) != 0) {
        fprintf(stderr, "[ERROR] Could not bind emulator to port %d\n", listen_port);
        CLOSE_SOCKET(emu_sock);
        cleanup_network();
        return 1;
    }

    struct sockaddr_in receiver_addr;
    setup_sockaddr(&receiver_addr, receiver_ip, receiver_port);

    struct sockaddr_in sender_addr;
    int sender_known = 0;

    Packet pkt;
    struct sockaddr_in incoming_addr;

    unsigned long total_data_received  = 0;
    unsigned long total_data_forwarded = 0;
    unsigned long total_data_dropped   = 0;
    unsigned long total_data_corrupted = 0;
    unsigned long total_ack_received   = 0;
    unsigned long total_ack_forwarded  = 0;

    printf("[EMULATOR] Proxy active. Waiting for traffic...\n\n");

    while (1) {
        int bytes = recv_packet(emu_sock, &pkt, &incoming_addr);
        if (bytes <= 0) continue;

        /* Traffic from Receiver -> Forward ACK back to Sender */
        if (incoming_addr.sin_port == receiver_addr.sin_port &&
            incoming_addr.sin_addr.s_addr == receiver_addr.sin_addr.s_addr) {

            total_ack_received++;
            if (!sender_known) continue;

            if (delay_ms > 0) sleep_ms(delay_ms);

            total_ack_forwarded++;
            printf("[EMULATOR -> SENDER]   FWD ACK: Type=%d, Ack=%u\n", pkt.type, pkt.ack_num);
            send_packet(emu_sock, &pkt, &sender_addr);
        }
        /* Traffic from Sender -> Forward DATA to Receiver */
        else {
            sender_addr  = incoming_addr;
            sender_known = 1;
            total_data_received++;

            /* FIN packet always passes through cleanly to end transfer */
            if (pkt.type == PACKET_FIN) {
                printf("[EMULATOR -> RECEIVER] FWD FIN: Seq=%u\n", pkt.seq_num);
                send_packet(emu_sock, &pkt, &receiver_addr);

                printf("\n--- EMULATOR SESSION STATS ---\n");
                printf("DATA Received: %lu  Forwarded: %lu  Dropped: %lu  Corrupted: %lu\n",
                       total_data_received, total_data_forwarded, total_data_dropped, total_data_corrupted);
                printf("ACK  Received: %lu  Forwarded: %lu\n",
                       total_ack_received, total_ack_forwarded);
                printf("------------------------------\n\n");
                continue;
            }

            /* Simulate Packet Loss */
            if (loss_rate > 0.0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < loss_rate) {
                    total_data_dropped++;
                    printf("[EMULATOR DROP] >>> DROPPED DATA: Seq=%u Len=%u (dropped %lu / %.1f%%)\n",
                           pkt.seq_num, pkt.length, total_data_dropped,
                           100.0 * total_data_dropped / total_data_received);
                    continue;
                }
            }

            /* Simulate Bit Corruption */
            if (corrupt_rate > 0.0 && pkt.length > 0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < corrupt_rate) {
                    total_data_corrupted++;
                    int byte_idx = rand() % pkt.length;
                    pkt.payload[byte_idx] ^= 0xFF;  /* Flip bits */
                    printf("[EMULATOR CORRUPT] >>> CORRUPTED DATA: Seq=%u Len=%u (flipped byte %d)\n",
                           pkt.seq_num, pkt.length, byte_idx);
                }
            }

            /* Simulate Network Delay */
            if (delay_ms > 0) {
                printf("[EMULATOR DELAY] Delaying Seq=%u by %d ms\n", pkt.seq_num, delay_ms);
                sleep_ms(delay_ms);
            }

            total_data_forwarded++;
            printf("[EMULATOR -> RECEIVER] FWD DATA: Type=%d, Seq=%u, Len=%u\n",
                   pkt.type, pkt.seq_num, pkt.length);
            send_packet(emu_sock, &pkt, &receiver_addr);
        }
    }

    CLOSE_SOCKET(emu_sock);
    cleanup_network();
    return 0;
}
