#include <stdio.h>
#include <string.h>
#include <assert.h>
#if defined(__has_include)
  #if __has_include("../include/socket_utils.h")
    #include "../include/socket_utils.h"
  #elif __has_include("socket_utils.h")
    #include "socket_utils.h"
  #endif
#else
  #include "socket_utils.h"
#endif

int main(void) {
    printf("=========================================\n");
    printf(" Running UDP Loopback Test (Dimple Day 1)\n");
    printf("=========================================\n\n");

    if (init_network() != 0) {
        printf("Failed to init network\n");
        return 1;
    }

    int test_port = 19876;
    SOCKET recv_sock = create_udp_socket();
    assert(recv_sock != INVALID_SOCKET);

    int bind_res = bind_udp_socket(recv_sock, test_port);
    assert(bind_res == 0);
    printf("[1] Receiver socket created and bound to port %d\n", test_port);

    SOCKET send_sock = create_udp_socket();
    assert(send_sock != INVALID_SOCKET);
    printf("[2] Sender socket created\n");

    /* Create test packet */
    Packet tx_pkt;
    const char *msg = "UDP Loopback Packet Test";
    init_packet(&tx_pkt, PACKET_DATA, 42, 0, msg, (uint16_t)strlen(msg));

    printf("[3] Sending packet...\n");
    print_packet_info("TX", &tx_pkt);
    int sent = send_packet_to(send_sock, &tx_pkt, "127.0.0.1", test_port);
    printf("Bytes sent: %d\n", sent);
    assert(sent > 0);

    /* Receive packet */
    Packet rx_pkt;
    struct sockaddr_in src_addr;
    set_socket_timeout(recv_sock, 1000); /* 1 sec timeout */
    int received = recv_packet(recv_sock, &rx_pkt, &src_addr);
    printf("Bytes received: %d\n", received);
    assert(received > 0);

    print_packet_info("RX", &rx_pkt);

    /* Verify integrity */
    int valid = verify_packet_checksum(&rx_pkt);
    printf("Checksum valid: %d (1 = SUCCESS)\n", valid);
    assert(valid == 1);
    assert(strcmp(rx_pkt.payload, msg) == 0);
    assert(rx_pkt.seq_num == 42);

    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);
    cleanup_network();

    printf("\n=========================================\n");
    printf(">>> UDP LOOPBACK TEST PASSED 100%%! <<<\n");
    printf("=========================================\n");

    return 0;
}
