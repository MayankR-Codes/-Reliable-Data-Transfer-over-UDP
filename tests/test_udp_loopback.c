/*
 * test_udp_loopback.c
 * ===================
 * Author  : Manisha Behera (Day 1 Task — Manisha & Raushan)
 * Purpose : Test raw UDP send/receive between two localhost ports,
 *           verifying that the packet arrives intact with matching
 *           checksum, sequence number, and payload content.
 *
 * Day 1 Spec:
 *   - Test sending raw UDP packets between two localhost ports.
 *   - Verify that the packet roundtrip preserves data integrity.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/socket_utils.h"

/* ── Helper: print a pass/fail banner ─────────────────────────── */
static void result_banner(const char *test_name, int passed) {
    if (passed)
        printf("  >>> %-50s [PASS]\n", test_name);
    else
        printf("  >>> %-50s [FAIL]\n", test_name);
}

int main(void) {
    int all_passed = 1;

    printf("\n");
    printf("=======================================================\n");
    printf("  Manisha Behera — Day 1: UDP Loopback Tests\n");
    printf("=======================================================\n\n");

    /* Initialise Winsock (no-op on Linux) */
    if (init_network() != 0) {
        fprintf(stderr, "[FATAL] Network initialisation failed.\n");
        return 1;
    }

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 1: Create sockets and bind receiver                   */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 1] Create UDP sockets and bind receiver to port 19900\n");
    int test_port = 19900;

    SOCKET recv_sock = create_udp_socket();
    int ok = (recv_sock != INVALID_SOCKET);
    result_banner("Receiver socket created", ok);
    assert(ok);
    all_passed &= ok;

    ok = (bind_udp_socket(recv_sock, test_port) == 0);
    result_banner("Receiver socket bound to port 19900", ok);
    assert(ok);
    all_passed &= ok;

    /* Set a 2-second receive timeout so the test doesn't hang */
    set_socket_timeout(recv_sock, 2000);

    SOCKET send_sock = create_udp_socket();
    ok = (send_sock != INVALID_SOCKET);
    result_banner("Sender socket created", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 2: Send a DATA packet over loopback                   */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 2] Send DATA packet over loopback (127.0.0.1:%d)\n", test_port);
    Packet tx_pkt;
    const char *msg = "Manisha Day1 UDP loopback test packet";
    init_packet(&tx_pkt, PACKET_DATA, 7, 0, msg, (uint16_t)strlen(msg));
    print_packet_info("TX", &tx_pkt);

    int sent = send_packet_to(send_sock, &tx_pkt, "127.0.0.1", test_port);
    printf("  Bytes sent: %d  (expected > 0)\n", sent);
    ok = (sent > 0);
    result_banner("Packet sent successfully", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 3: Receive the packet                                 */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 3] Receive packet on loopback socket\n");
    Packet rx_pkt;
    struct sockaddr_in src_addr;
    int received = recv_packet(recv_sock, &rx_pkt, &src_addr);
    printf("  Bytes received: %d  (expected > 0)\n", received);
    ok = (received > 0);
    result_banner("Packet received successfully", ok);
    assert(ok);
    all_passed &= ok;
    print_packet_info("RX", &rx_pkt);
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 4: Checksum integrity after loopback                  */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 4] CRC32 checksum integrity after loopback\n");
    ok = verify_packet_checksum(&rx_pkt);
    printf("  verify_packet_checksum() = %d  (expected 1)\n", ok);
    result_banner("Checksum valid after loopback", ok == 1);
    assert(ok == 1);
    all_passed &= (ok == 1);
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 5: Sequence number preserved                          */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 5] Sequence number preserved across loopback\n");
    printf("  rx_pkt.seq_num = %u  (expected 7)\n", rx_pkt.seq_num);
    ok = (rx_pkt.seq_num == 7);
    result_banner("Sequence number matches", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 6: Payload content preserved                          */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 6] Payload content preserved across loopback\n");
    ok = (memcmp(rx_pkt.payload, msg, strlen(msg)) == 0);
    printf("  Payload match: %s\n", ok ? "YES" : "NO");
    result_banner("Payload content matches exactly", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 7: Send an ACK back to confirm bidirectional UDP      */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 7] Bidirectional — send ACK back and receive it\n");

    /* Bind sender to a known port so we can receive on it */
    int ack_port = 19901;
    SOCKET ack_recv_sock = create_udp_socket();
    assert(ack_recv_sock != INVALID_SOCKET);
    assert(bind_udp_socket(ack_recv_sock, ack_port) == 0);
    set_socket_timeout(ack_recv_sock, 2000);

    Packet ack_tx;
    init_packet(&ack_tx, PACKET_ACK, 0, rx_pkt.seq_num, NULL, 0);
    print_packet_info("ACK-TX", &ack_tx);

    sent = send_packet_to(recv_sock, &ack_tx, "127.0.0.1", ack_port);
    printf("  ACK bytes sent: %d\n", sent);
    assert(sent > 0);

    Packet ack_rx;
    struct sockaddr_in ack_src;
    received = recv_packet(ack_recv_sock, &ack_rx, &ack_src);
    printf("  ACK bytes received: %d\n", received);
    ok = (received > 0) &&
         (ack_rx.type == PACKET_ACK) &&
         (ack_rx.ack_num == 7) &&
         (verify_packet_checksum(&ack_rx) == 1);
    print_packet_info("ACK-RX", &ack_rx);
    result_banner("Bidirectional ACK roundtrip valid", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ── Cleanup ────────────────────────────────────────────── */
    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);
    CLOSE_SOCKET(ack_recv_sock);
    cleanup_network();

    /* ── Summary ────────────────────────────────────────────── */
    printf("=======================================================\n");
    if (all_passed)
        printf("  ALL 7 UDP LOOPBACK TESTS PASSED — Manisha Day 1!\n");
    else
        printf("  SOME TESTS FAILED — Review output above.\n");
    printf("=======================================================\n\n");

    return all_passed ? 0 : 1;
}
