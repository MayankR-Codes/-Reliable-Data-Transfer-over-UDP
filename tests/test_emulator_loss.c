#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "socket_utils.h"

int main(void) {
    printf("====================================================\n");
    printf(" Testing Emulator Packet Loss Simulation (Day 2)    \n");
    printf("====================================================\n\n");

    if (init_network() != 0) return 1;

    int emu_port = 8000;
    int recv_port = 9001;
    int num_packets = 50;

    /* 1. Setup Receiver */
    SOCKET recv_sock = create_udp_socket();
    assert(bind_udp_socket(recv_sock, recv_port) == 0);
    set_socket_timeout(recv_sock, 1500); /* 1.5s timeout */

    /* 2. Setup Sender */
    SOCKET send_sock = create_udp_socket();
    set_socket_timeout(send_sock, 1000);

    printf("[1] Sending %d DATA packets to Emulator (Port %d)...\n", num_packets, emu_port);

    int received_count = 0;
    char payload_buf[64];

    for (int i = 0; i < num_packets; i++) {
        Packet tx;
        snprintf(payload_buf, sizeof(payload_buf), "Packet index #%d", i);
        init_packet(&tx, PACKET_DATA, (uint32_t)i, 0, payload_buf, (uint16_t)strlen(payload_buf));

        send_packet_to(send_sock, &tx, "127.0.0.1", emu_port);

        /* Try receiving at the receiver with a short timeout */
        Packet rx;
        struct sockaddr_in from_addr;
        set_socket_timeout(recv_sock, 100); /* 100ms wait per packet */
        int bytes = recv_packet(recv_sock, &rx, &from_addr);
        if (bytes > 0 && verify_packet_checksum(&rx)) {
            received_count++;
        }
    }

    printf("\n[2] Results Summary:\n");
    printf("    Total Packets Sent     : %d\n", num_packets);
    printf("    Total Packets Delivered: %d\n", received_count);
    printf("    Total Packets Dropped  : %d\n", num_packets - received_count);
    printf("    Measured Loss Rate     : %.1f%%\n\n", 
           (100.0 * (num_packets - received_count)) / num_packets);

    /* Verify that loss occurred but not 100% loss */
    assert(received_count < num_packets);
    assert(received_count > 0);

    /* 3. Send FIN packet and ensure it arrives */
    Packet fin_tx;
    init_packet(&fin_tx, PACKET_FIN, 9999, 0, NULL, 0);
    send_packet_to(send_sock, &fin_tx, "127.0.0.1", emu_port);

    Packet fin_rx;
    struct sockaddr_in from_addr;
    set_socket_timeout(recv_sock, 1000);
    int fin_bytes = recv_packet(recv_sock, &fin_rx, &from_addr);
    assert(fin_bytes > 0);
    assert(fin_rx.type == PACKET_FIN);
    printf("[3] FIN packet successfully passed through emulator!\n");

    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);
    cleanup_network();

    printf("\n====================================================\n");
    printf(">>> EMULATOR LOSS TEST (DAY 2) PASSED SUCCESSFULLY! <<<\n");
    printf("====================================================\n");

    return 0;
}
