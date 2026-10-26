/*
 * test_packet.c
 * =============
 * Author  : Manisha Behera (Day 1 Task — Manisha & Raushan)
 * Purpose : Test packet initialization, checksum computation,
 *           single-bit corruption detection, and header-tampering
 *           detection using the CRC32 implementation in packet.c.
 *
 * Day 1 Spec:
 *   - Instantiate a packet, compute checksum, corrupt one byte,
 *     and verify that compute_checksum() detects the mismatch.
 *   - Confirm clean packets pass verification.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/packet.h"

/* ── Helper: print a pass/fail banner ─────────────────────────── */
static void result_banner(const char *test_name, int passed) {
    if (passed)
        printf("  >>> %-45s [PASS]\n", test_name);
    else
        printf("  >>> %-45s [FAIL]\n", test_name);
}

int main(void) {
    int all_passed = 1;

    printf("\n");
    printf("=======================================================\n");
    printf("  Manisha Behera — Day 1: Packet & Checksum Tests\n");
    printf("=======================================================\n\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 1: Clean packet — checksum must PASS                  */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 1] Clean packet checksum verification\n");
    Packet p1;
    const char *msg = "Hello, Reliable UDP Transport! (Manisha Day1)";
    init_packet(&p1, PACKET_DATA, 1, 0, msg, (uint16_t)strlen(msg));
    print_packet_info("TX", &p1);

    int ok = verify_packet_checksum(&p1);
    printf("  verify_packet_checksum() = %d  (expected 1)\n", ok);
    result_banner("Clean packet checksum PASS", ok == 1);
    assert(ok == 1);
    all_passed &= (ok == 1);
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 2: Single-bit payload corruption — must FAIL          */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 2] Single-bit payload corruption detection\n");
    p1.payload[4] ^= 0x01;                  /* flip 1 bit in payload */
    ok = verify_packet_checksum(&p1);
    printf("  verify after bit-flip = %d  (expected 0 = CORRUPTED)\n", ok);
    result_banner("Bit-flip corruption detected", ok == 0);
    assert(ok == 0);
    all_passed &= (ok == 0);
    p1.payload[4] ^= 0x01;                  /* restore bit */
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 3: Multi-byte payload corruption — must FAIL          */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 3] Multi-byte payload corruption detection\n");
    p1.payload[0] = 'X';
    p1.payload[1] = 'X';
    ok = verify_packet_checksum(&p1);
    printf("  verify after multi-byte tamper = %d  (expected 0)\n", ok);
    result_banner("Multi-byte corruption detected", ok == 0);
    assert(ok == 0);
    all_passed &= (ok == 0);
    /* Reinitialise to clean state for next tests */
    init_packet(&p1, PACKET_DATA, 1, 0, msg, (uint16_t)strlen(msg));
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 4: Header seq_num tampering — must FAIL               */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 4] Header seq_num tampering detection\n");
    p1.seq_num = 9999;                       /* tamper header field */
    ok = verify_packet_checksum(&p1);
    printf("  verify after seq_num tamper = %d  (expected 0)\n", ok);
    result_banner("Header seq_num tampering detected", ok == 0);
    assert(ok == 0);
    all_passed &= (ok == 0);
    init_packet(&p1, PACKET_DATA, 1, 0, msg, (uint16_t)strlen(msg));
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 5: Header type tampering — must FAIL                  */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 5] Header type field tampering detection\n");
    p1.type = PACKET_ACK;                    /* change DATA → ACK in header */
    ok = verify_packet_checksum(&p1);
    printf("  verify after type tamper = %d  (expected 0)\n", ok);
    result_banner("Header type tampering detected", ok == 0);
    assert(ok == 0);
    all_passed &= (ok == 0);
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 6: ACK packet with zero-length payload — must PASS    */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 6] ACK packet (zero payload) checksum verification\n");
    Packet ack;
    init_packet(&ack, PACKET_ACK, 0, 1, NULL, 0);
    print_packet_info("ACK", &ack);
    ok = verify_packet_checksum(&ack);
    printf("  verify ACK packet = %d  (expected 1)\n", ok);
    result_banner("Zero-payload ACK packet PASS", ok == 1);
    assert(ok == 1);
    all_passed &= (ok == 1);
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 7: FIN packet — must PASS                             */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 7] FIN packet checksum verification\n");
    Packet fin;
    init_packet(&fin, PACKET_FIN, 0, 0, NULL, 0);
    print_packet_info("FIN", &fin);
    ok = verify_packet_checksum(&fin);
    printf("  verify FIN packet = %d  (expected 1)\n", ok);
    result_banner("FIN packet PASS", ok == 1);
    assert(ok == 1);
    all_passed &= (ok == 1);
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 8: Max payload boundary                               */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 8] Max-payload (1024 bytes) packet\n");
    Packet big;
    char fill[MAX_PAYLOAD_SIZE];
    memset(fill, 0xAB, MAX_PAYLOAD_SIZE);
    init_packet(&big, PACKET_DATA, 42, 0, fill, MAX_PAYLOAD_SIZE);
    ok = verify_packet_checksum(&big);
    printf("  Max-payload length reported = %u bytes\n", big.length);
    printf("  verify = %d  (expected 1)\n", ok);
    result_banner("Max-payload packet PASS", ok == 1);
    assert(ok == 1);
    all_passed &= (ok == 1);
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* SUMMARY                                                     */
    /* ─────────────────────────────────────────────────────────── */
    printf("=======================================================\n");
    if (all_passed)
        printf("  ALL 8 TESTS PASSED — Manisha Day 1 Complete!\n");
    else
        printf("  SOME TESTS FAILED — Review output above.\n");
    printf("=======================================================\n\n");

    return all_passed ? 0 : 1;
}
