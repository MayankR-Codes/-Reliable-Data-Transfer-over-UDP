#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "socket_utils.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int listen_port = 8000;
    int receiver_port = 9001;
    char receiver_ip[64] = "127.0.0.1";

    /* Parse basic command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--recv-port") == 0 && i + 1 < argc) {
            receiver_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--recv-ip") == 0 && i + 1 < argc) {
            strncpy(receiver_ip, argv[++i], sizeof(receiver_ip) - 1);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--listen <port>] [--recv-ip <ip>] [--recv-port <port>]\n", argv[0]);
            return 0;
        }
    }

    printf("====================================================\n");
    printf("   Channel Emulator (Proxy) — Day 1 Skeleton        \n");
    printf("====================================================\n");
    printf("Listening on Port        : %d\n", listen_port);
    printf("Forwarding target (Recv) : %s:%d\n", receiver_ip, receiver_port);
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

    printf("[EMULATOR] Proxy active. Waiting for traffic...\n\n");

    while (1) {
        int bytes = recv_packet(emu_sock, &pkt, &incoming_addr);
        if (bytes <= 0) {
            continue;
        }

        /* Check packet integrity */
        int valid = verify_packet_checksum(&pkt);
        if (!valid) {
            printf("[EMULATOR WARNING] Received corrupt packet! Type=%d, Checksum=0x%08X\n",
                   pkt.type, pkt.checksum);
        }

        /* Determine if the packet is from the Receiver or Sender */
        if (incoming_addr.sin_port == receiver_addr.sin_port &&
            incoming_addr.sin_addr.s_addr == receiver_addr.sin_addr.s_addr) {
            /* Packet is an ACK returning from the Receiver -> Forward to Sender */
            if (sender_known) {
                printf("[EMULATOR -> SENDER]   FWD ACK: Type=%d, Ack=%u\n", pkt.type, pkt.ack_num);
                send_packet(emu_sock, &pkt, &sender_addr);
            } else {
                printf("[EMULATOR WARNING] Received ACK from Receiver but Sender address is unknown yet.\n");
            }
        } else {
            /* Packet is DATA coming from the Sender -> Forward to Receiver */
            sender_addr = incoming_addr;
            sender_known = 1;

            printf("[EMULATOR -> RECEIVER] FWD DATA: Type=%d, Seq=%u, Len=%u\n",
                   pkt.type, pkt.seq_num, pkt.length);
            send_packet(emu_sock, &pkt, &receiver_addr);
        }
    }

    CLOSE_SOCKET(emu_sock);
    cleanup_network();
    return 0;
}
