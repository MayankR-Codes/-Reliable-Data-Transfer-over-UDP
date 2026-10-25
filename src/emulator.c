#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "socket_utils.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int listen_port = 8000;
    int receiver_port = 9001;
    char receiver_ip[64] = "127.0.0.1";
    double loss_rate = 0.0;       /* 0.0 to 1.0 (e.g. 0.10 for 10%) */
    double loss_ack_rate = 0.0;   /* Optional loss for ACKs */
    unsigned int random_seed = (unsigned int)time(NULL);
    int seed_provided = 0;

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
        } else if (strcmp(argv[i], "--loss-ack") == 0 && i + 1 < argc) {
            double val = atof(argv[++i]);
            loss_ack_rate = (val > 1.0) ? (val / 100.0) : val;
            if (loss_ack_rate < 0.0) loss_ack_rate = 0.0;
            if (loss_ack_rate > 1.0) loss_ack_rate = 1.0;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            random_seed = (unsigned int)atoi(argv[++i]);
            seed_provided = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  --listen <port>      Emulator listen port (default: 8000)\n");
            printf("  --recv-ip <ip>       Receiver IP address (default: 127.0.0.1)\n");
            printf("  --recv-port <port>   Receiver port (default: 9001)\n");
            printf("  --loss <percent>     Packet drop rate for DATA (e.g., 10 or 0.10 for 10%%)\n");
            printf("  --loss-ack <percent> Packet drop rate for ACKs (default: 0%%)\n");
            printf("  --seed <number>      Random number generator seed (for reproducible tests)\n");
            printf("  --help, -h           Show this help message\n");
            return 0;
        }
    }

    srand(random_seed);

    printf("====================================================\n");
    printf("   Channel Emulator (Proxy) — Day 2: Packet Loss    \n");
    printf("====================================================\n");
    printf("Listening on Port        : %d\n", listen_port);
    printf("Forwarding target (Recv) : %s:%d\n", receiver_ip, receiver_port);
    printf("DATA Packet Loss Rate    : %.1f%%\n", loss_rate * 100.0);
    if (loss_ack_rate > 0.0) {
        printf("ACK Packet Loss Rate     : %.1f%%\n", loss_ack_rate * 100.0);
    }
    printf("Random Seed              : %u%s\n", random_seed, seed_provided ? " (manual)" : " (auto)");
    printf("====================================================\n\n");

    if (init_network() != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize network subsystem.\n");
        return 1;
    }

    SOCKET emu_sock = create_udp_socket();
    if (emu_sock == INVALID_SOCKET) {
        cleanup_network();
        return 1;
    }

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

    /* Metrics and statistics */
    unsigned long total_data_received = 0;
    unsigned long total_data_forwarded = 0;
    unsigned long total_data_dropped = 0;
    unsigned long total_ack_received = 0;
    unsigned long total_ack_forwarded = 0;
    unsigned long total_ack_dropped = 0;

    printf("[EMULATOR] Proxy active. Waiting for traffic...\n\n");

    while (1) {
        int bytes = recv_packet(emu_sock, &pkt, &incoming_addr);
        if (bytes <= 0) {
            continue;
        }

        /* Verify packet integrity */
        int valid = verify_packet_checksum(&pkt);
        if (!valid) {
            printf("[EMULATOR WARNING] Received corrupted packet! Type=%d, Checksum=0x%08X\n",
                   pkt.type, pkt.checksum);
        }

        /* Determine packet origin: Receiver or Sender */
        if (incoming_addr.sin_port == receiver_addr.sin_port &&
            incoming_addr.sin_addr.s_addr == receiver_addr.sin_addr.s_addr) {

            /* Traffic from Receiver -> Forward to Sender */
            total_ack_received++;

            if (!sender_known) {
                printf("[EMULATOR WARNING] Received ACK from Receiver but Sender address is unknown.\n");
                continue;
            }

            /* Check for ACK drop */
            if (loss_ack_rate > 0.0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < loss_ack_rate) {
                    total_ack_dropped++;
                    printf("[EMULATOR DROP] >>> DROPPED ACK: Type=%d, Ack=%u (Total Dropped: %lu)\n",
                           pkt.type, pkt.ack_num, total_ack_dropped);
                    continue; /* Drop ACK */
                }
            }

            total_ack_forwarded++;
            printf("[EMULATOR -> SENDER]   FWD ACK: Type=%d, Ack=%u\n", pkt.type, pkt.ack_num);
            send_packet(emu_sock, &pkt, &sender_addr);

        } else {
            /* Traffic from Sender -> Forward to Receiver */
            sender_addr = incoming_addr;
            sender_known = 1;
            total_data_received++;

            /* Always allow FIN packets through cleanly so transfers terminate reliably */
            if (pkt.type == PACKET_FIN) {
                printf("[EMULATOR -> RECEIVER] FWD FIN: Type=FIN, Seq=%u\n", pkt.seq_num);
                send_packet(emu_sock, &pkt, &receiver_addr);
                printf("\n--- EMULATOR STATS ---\n");
                printf("DATA Received: %lu, Forwarded: %lu, Dropped: %lu (Actual Loss: %.1f%%)\n",
                       total_data_received, total_data_forwarded, total_data_dropped,
                       total_data_received > 0 ? (100.0 * total_data_dropped / total_data_received) : 0.0);
                printf("ACK  Received: %lu, Forwarded: %lu, Dropped: %lu\n",
                       total_ack_received, total_ack_forwarded, total_ack_dropped);
                printf("----------------------\n\n");
                continue;
            }

            /* Roll random number for DATA packet drop */
            if (loss_rate > 0.0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < loss_rate) {
                    total_data_dropped++;
                    printf("[EMULATOR DROP] >>> DROPPED DATA: Seq=%u, Len=%u (Total Dropped: %lu/%.1f%%)\n",
                           pkt.seq_num, pkt.length, total_data_dropped,
                           (100.0 * total_data_dropped / total_data_received));
                    continue; /* Packet is dropped! */
                }
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
