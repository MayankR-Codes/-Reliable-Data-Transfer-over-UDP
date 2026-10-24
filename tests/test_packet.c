#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "packet.h"

int main(void) {
    printf("=========================================\n");
    printf("   Running Dimple's Day 1 Packet Tests   \n");
    printf("=========================================\n\n");

    /* TEST 1: Initialization and clean packet checksum */
    Packet p1;
    const char *test_msg = "Hello, Reliable UDP Transport!";
    init_packet(&p1, PACKET_DATA, 100, 0, test_msg, (uint16_t)strlen(test_msg));

    print_packet_info("TEST 1", &p1);
    printf("Expected verification: 1 (PASS)\n");
    int valid = verify_packet_checksum(&p1);
    printf("Actual verification  : %d\n", valid);
    assert(valid == 1);
    printf(">>> TEST 1 PASSED: Clean packet verified successfully!\n\n");

    /* TEST 2: Single-bit corruption detection */
    printf("Simulating bit corruption in payload...\n");
    p1.payload[5] ^= 0x01; /* Flip 1 bit */
    valid = verify_packet_checksum(&p1);
    printf("Verification after bit corruption: %d (0 = Detected Corruption)\n", valid);
    assert(valid == 0);
    printf(">>> TEST 2 PASSED: Bit corruption detected and rejected!\n\n");

    /* TEST 3: Header tampering detection (tamper seq_num) */
    p1.payload[5] ^= 0x01; /* Restore payload */
    assert(verify_packet_checksum(&p1) == 1); /* Should be valid again */

    printf("Simulating sequence number tampering in header...\n");
    p1.seq_num = 999; /* Change sequence number */
    valid = verify_packet_checksum(&p1);
    printf("Verification after header tampering: %d\n", valid);
    assert(valid == 0);
    printf(">>> TEST 3 PASSED: Header tampering detected and rejected!\n\n");

    /* TEST 4: ACK packet with 0 payload */
    Packet ack_pkt;
    init_packet(&ack_pkt, PACKET_ACK, 0, 100, NULL, 0);
    print_packet_info("TEST 4", &ack_pkt);
    assert(verify_packet_checksum(&ack_pkt) == 1);
    printf(">>> TEST 4 PASSED: Empty-payload ACK packet verified successfully!\n\n");

    printf("=========================================\n");
    printf(" ALL TESTS PASSED SUCCESSFULLY! (DAY 1)  \n");
    printf("=========================================\n");

    return 0;
}
