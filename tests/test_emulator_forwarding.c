#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "socket_utils.h"

int main(void) {
    printf("====================================================\n");
    printf(" Testing 3-Node Chain: Sender -> Emulator -> Recv   \n");
    printf("====================================================\n\n");

    if (init_network() != 0) return 1;

    int emu_port = 8000;
    int recv_port = 9001;

    /* 1. Setup Receiver */
    SOCKET recv_sock = create_udp_socket();
    assert(bind_udp_socket(recv_sock, recv_port) == 0);
    set_socket_timeout(recv_sock, 1000);

    /* 2. Setup Sender */
    SOCKET send_sock = create_udp_socket();
    set_socket_timeout(send_sock, 1000);

    /* 3. Send DATA packet to Emulator (port 8000) */
    Packet data_pkt;
    const char *payload_text = "Mayank Emulator Day 1 Test Payload";
    init_packet(&data_pkt, PACKET_DATA, 1, 0, payload_text, (uint16_t)strlen(payload_text));

    printf("[1] Sender transmitting packet to Emulator (Port %d)...\n", emu_port);
    int sent = send_packet_to(send_sock, &data_pkt, "127.0.0.1", emu_port);
    assert(sent > 0);

    /* 4. Receiver should receive the packet forwarded by Emulator */
    Packet recv_pkt;
    struct sockaddr_in emu_addr;
    printf("[2] Receiver waiting on Port %d...\n", recv_port);
    int recvd = recv_packet(recv_sock, &recv_pkt, &emu_addr);
    printf("Debug: recvd = %d, WSAGetLastError = %d\n", recvd, WSAGetLastError());
    assert(recvd > 0);
    assert(verify_packet_checksum(&recv_pkt) == 1);
    assert(strcmp(recv_pkt.payload, payload_text) == 0);
    printf(">>> Receiver successfully received packet forwarded by Emulator!\n");

    /* 5. Receiver sends ACK back to Emulator */
    Packet ack_pkt;
    init_packet(&ack_pkt, PACKET_ACK, 0, recv_pkt.seq_num, NULL, 0);
    printf("[3] Receiver sending ACK=%d back to Emulator...\n", ack_pkt.ack_num);
    send_packet(recv_sock, &ack_pkt, &emu_addr);

    /* 6. Sender receives ACK forwarded by Emulator */
    Packet sender_ack;
    struct sockaddr_in emu_ret_addr;
    printf("[4] Sender waiting for returning ACK...\n");
    int ack_bytes = recv_packet(send_sock, &sender_ack, &emu_ret_addr);
    assert(ack_bytes > 0);
    assert(verify_packet_checksum(&sender_ack) == 1);
    assert(sender_ack.type == PACKET_ACK);
    assert(sender_ack.ack_num == 1);
    printf(">>> Sender received verified ACK from Emulator!\n");

    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);
    cleanup_network();

    printf("\n====================================================\n");
    printf(">>> 3-NODE CHAIN (SENDER-EMULATOR-RECEIVER) PASSED! <<<\n");
    printf("====================================================\n");

    return 0;
}
