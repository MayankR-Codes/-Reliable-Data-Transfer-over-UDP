#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#endif

#if defined(__has_include)
  #if __has_include("../include/buffer.h")
    #include "../include/buffer.h"
  #elif __has_include("buffer.h")
    #include "buffer.h"
  #endif
#else
  #include "buffer.h"
#endif

/* Simple ms timestamp for tests */
static long long now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
#endif
}

int main(void) {
    printf("====================================================\n");
    printf("  Running Dimple's Day 3 Window Buffer Tests        \n");
    printf("====================================================\n\n");

    WindowBuffer wb;

    /* ------------------------------------------------------------------ */
    /* TEST 1: Init with window_size = 4 */
    /* ------------------------------------------------------------------ */
    printf("[TEST 1] Initialize ring buffer with window_size = 4...\n");
    assert(wbuf_init(&wb, 4) == 0);
    assert(wb.window_size == 4);
    assert(wb.base == 0);
    assert(wb.next_seq_num == 0);
    assert(wbuf_is_empty(&wb) == 1);
    assert(wbuf_is_full(&wb)  == 0);
    assert(wbuf_in_flight(&wb) == 0);
    printf(">>> TEST 1 PASSED: Buffer initialized correctly!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 2: Push 4 packets (fill window completely) */
    /* ------------------------------------------------------------------ */
    printf("[TEST 2] Pushing 4 packets to fill window...\n");
    for (int i = 0; i < 4; i++) {
        Packet p;
        char payload[32];
        snprintf(payload, sizeof(payload), "DataPkt-%d", i);
        init_packet(&p, PACKET_DATA, (uint32_t)i, 0, payload,
                    (uint16_t)strlen(payload));
        int res = wbuf_push(&wb, &p, now_ms());
        assert(res == 0);
        printf("  Pushed seq=%d  base=%u  next_seq=%u  in_flight=%d\n",
               i, wb.base, wb.next_seq_num, wbuf_in_flight(&wb));
    }
    assert(wbuf_is_full(&wb)   == 1);
    assert(wbuf_is_empty(&wb)  == 0);
    assert(wbuf_in_flight(&wb) == 4);
    /* Window full: 5th push must fail */
    Packet overflow;
    init_packet(&overflow, PACKET_DATA, 99, 0, "OVERFLOW", 8);
    assert(wbuf_push(&wb, &overflow, now_ms()) == -1);
    printf(">>> TEST 2 PASSED: Window fills correctly; overflow push rejected!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 3: Cumulative ACK for seq=1 — base must slide to 2 */
    /* ------------------------------------------------------------------ */
    printf("[TEST 3] Cumulative ACK seq=1 (GBN style)...\n");
    int new_base = wbuf_ack(&wb, 1);
    printf("  After ack(1): base=%u  next_seq=%u  in_flight=%d\n",
           wb.base, wb.next_seq_num, wbuf_in_flight(&wb));
    assert(new_base == 2);
    assert(wb.base == 2);
    assert(wbuf_in_flight(&wb) == 2);
    /* Slots 0 and 1 should now be EMPTY */
    assert(wb.slots[0 % MAX_WINDOW_SIZE].status == SLOT_EMPTY);
    assert(wb.slots[1 % MAX_WINDOW_SIZE].status == SLOT_EMPTY);
    /* Slots 2 and 3 still SENT */
    assert(wb.slots[2 % MAX_WINDOW_SIZE].status == SLOT_SENT);
    assert(wb.slots[3 % MAX_WINDOW_SIZE].status == SLOT_SENT);
    printf(">>> TEST 3 PASSED: Cumulative ACK advances base correctly!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 4: Push 2 more packets into the freed slots */
    /* ------------------------------------------------------------------ */
    printf("[TEST 4] Pushing 2 more packets after window slide...\n");
    for (int i = 4; i < 6; i++) {
        Packet p;
        char payload[32];
        snprintf(payload, sizeof(payload), "NewPkt-%d", i);
        init_packet(&p, PACKET_DATA, (uint32_t)i, 0, payload,
                    (uint16_t)strlen(payload));
        int res = wbuf_push(&wb, &p, now_ms());
        assert(res == 0);
        printf("  Pushed seq=%d  base=%u  next_seq=%u  in_flight=%d\n",
               i, wb.base, wb.next_seq_num, wbuf_in_flight(&wb));
    }
    assert(wbuf_is_full(&wb)   == 1);
    assert(wbuf_in_flight(&wb) == 4);
    printf(">>> TEST 4 PASSED: Sliding window reuse works correctly!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 5: wbuf_get_slot — retrieve in-flight packet for retransmit */
    /* ------------------------------------------------------------------ */
    printf("[TEST 5] Retrieving in-flight packet at seq=3 for retransmission...\n");
    WindowSlot *slot = wbuf_get_slot(&wb, 3);
    assert(slot != NULL);
    assert(slot->status  == SLOT_SENT);
    assert(slot->seq_num == 3);
    assert(verify_packet_checksum(&slot->pkt) == 1);
    printf("  Slot seq=%u  status=SENT  checksum=0x%08X  payload='%.*s'\n",
           slot->seq_num, slot->pkt.checksum,
           slot->pkt.length, slot->pkt.payload);
    /* Out-of-window request must return NULL */
    assert(wbuf_get_slot(&wb, 0) == NULL);  /* Already acked */
    assert(wbuf_get_slot(&wb, 99) == NULL); /* Way out of range */
    printf(">>> TEST 5 PASSED: Slot retrieval correct; out-of-window returns NULL!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 6: Selective ACK — ack seq=5 without advancing base past seq=2 */
    /* ------------------------------------------------------------------ */
    printf("[TEST 6] Selective ACK seq=5 (SR style — only mark, don't slide base)...\n");
    uint32_t base_before = wb.base;
    int sr_rc = wbuf_selective_ack(&wb, 5);
    assert(sr_rc == 0);
    printf("  After selective_ack(5): base=%u (was %u)  in_flight=%d\n",
           wb.base, base_before, wbuf_in_flight(&wb));
    /* base=2 because seq=2 is still SENT (gap), so base cannot slide yet */
    assert(wb.base == 2);
    assert(wb.slots[5 % MAX_WINDOW_SIZE].status == SLOT_ACKED);
    assert(wb.slots[2 % MAX_WINDOW_SIZE].status == SLOT_SENT);
    printf(">>> TEST 6 PASSED: Selective ACK marks correctly without sliding base!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 7: ACK seq=2 — should trigger chain slide past seq=2 */
    /* ------------------------------------------------------------------ */
    printf("[TEST 7] ACK seq=2 — base should now slide past 2, 3 is still SENT...\n");
    wbuf_ack(&wb, 2);
    printf("  After ack(2): base=%u  in_flight=%d\n",
           wb.base, wbuf_in_flight(&wb));
    /* seq 2 acked -> base becomes 3. seq 3 is still SENT so stops there. */
    assert(wb.base == 3);
    printf(">>> TEST 7 PASSED: ACK chain slides base correctly!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 8: Reset clears everything */
    /* ------------------------------------------------------------------ */
    printf("[TEST 8] Reset buffer...\n");
    wbuf_reset(&wb);
    assert(wb.base == 0);
    assert(wb.next_seq_num == 0);
    assert(wb.count == 0);
    assert(wbuf_is_empty(&wb) == 1);
    printf(">>> TEST 8 PASSED: Buffer reset correctly!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 9: Invalid init parameters are rejected */
    /* ------------------------------------------------------------------ */
    printf("[TEST 9] Invalid init parameters rejected...\n");
    assert(wbuf_init(&wb, 0) == -1);
    assert(wbuf_init(&wb, MAX_WINDOW_SIZE + 1) == -1);
    assert(wbuf_init(NULL, 4) == -1);
    printf(">>> TEST 9 PASSED: Invalid parameters rejected!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 10: Large sequence rollover — wraps ring correctly */
    /* ------------------------------------------------------------------ */
    printf("[TEST 10] Ring buffer wraps correctly for large sequence numbers...\n");
    assert(wbuf_init(&wb, 8) == 0);
    /* Fast-forward base and next_seq_num near UINT32_MAX to simulate rollover */
    wb.base         = 0xFFFFFFF8u;
    wb.next_seq_num = 0xFFFFFFF8u;
    for (int i = 0; i < 8; i++) {
        Packet p;
        char buf[16];
        snprintf(buf, sizeof(buf), "W%d", i);
        init_packet(&p, PACKET_DATA, wb.next_seq_num, 0, buf,
                    (uint16_t)strlen(buf));
        int r = wbuf_push(&wb, &p, now_ms());
        assert(r == 0);
    }
    assert(wbuf_is_full(&wb) == 1);
    /* ACK first 4 packets (from 0xFFFFFFF8 through 0xFFFFFFFB) */
    int ack_ret = wbuf_ack(&wb, 0xFFFFFFFBu);
    assert(ack_ret == (int)0xFFFFFFFCu);
    assert(wb.base == 0xFFFFFFFCu);
    assert(wbuf_in_flight(&wb) == 4);
    printf("  Seq-rollover base=0x%08X  next_seq=0x%08X  in_flight=%d\n",
           wb.base, wb.next_seq_num, wbuf_in_flight(&wb));
    printf(">>> TEST 10 PASSED: Ring buffer handles large sequence numbers!\n\n");

    /* ================================================================== */
    /*  Dimple's Day 4 RecvBuffer Tests (Selective Repeat Out-of-Order)   */
    /* ================================================================== */
    printf("====================================================\n");
    printf("  Running Dimple's Day 4 RecvBuffer Tests           \n");
    printf("====================================================\n\n");

    RecvBuffer rb;

    /* ------------------------------------------------------------------ */
    /* TEST 11: RecvBuffer init and parameter validation */
    /* ------------------------------------------------------------------ */
    printf("[TEST 11] Initialize RecvBuffer and validate window boundaries...\n");
    assert(rbuf_init(&rb, 4) == 0);
    assert(rb.window_size == 4);
    assert(rb.rcv_base == 0);
    assert(rbuf_init(NULL, 4) == -1);
    assert(rbuf_init(&rb, 0) == -1);
    assert(rbuf_init(&rb, MAX_WINDOW_SIZE + 1) == -1);

    /* Within window: [0, 3] */
    assert(rbuf_is_in_window(&rb, 0) == 1);
    assert(rbuf_is_in_window(&rb, 1) == 1);
    assert(rbuf_is_in_window(&rb, 2) == 1);
    assert(rbuf_is_in_window(&rb, 3) == 1);
    assert(rbuf_is_in_window(&rb, 4) == 0);
    assert(rbuf_is_buffered(&rb, 0) == 0);
    printf(">>> TEST 11 PASSED: RecvBuffer initialized & window bounds verified!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 12: RecvBuffer out-of-order storage & bounds checking */
    /* ------------------------------------------------------------------ */
    printf("[TEST 12] Storing out-of-order packets (gap at seq=0)...\n");
    Packet p1, p2, p_out;
    init_packet(&p1, PACKET_DATA, 1, 0, "Chunk-1", 7);
    init_packet(&p2, PACKET_DATA, 2, 0, "Chunk-2", 7);
    init_packet(&p_out, PACKET_DATA, 5, 0, "Chunk-5", 7);

    assert(rbuf_put(&rb, &p2) == 0);
    assert(rbuf_put(&rb, &p1) == 0);
    assert(rbuf_put(&rb, &p_out) == -1); /* Seq 5 is outside window [0..3] */

    assert(rbuf_is_buffered(&rb, 0) == 0);
    assert(rbuf_is_buffered(&rb, 1) == 1);
    assert(rbuf_is_buffered(&rb, 2) == 1);
    assert(rbuf_is_buffered(&rb, 3) == 0);
    printf("  Buffered: seq=1 (yes), seq=2 (yes), seq=0 (no - gap)\n");
    printf(">>> TEST 12 PASSED: Out-of-order storage and rejection work correctly!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 13: RecvBuffer contiguous flush on gap fill */
    /* ------------------------------------------------------------------ */
    printf("[TEST 13] Flushing with gap vs flushing after gap is filled...\n");
    FILE *tfp = fopen("test_data/rbuf_test.tmp", "wb");
    assert(tfp != NULL);

    unsigned long bytes_written = 0;
    unsigned long chunks_written = 0;

    /* Base is 0, slot 0 is empty -> flush must return 0 */
    int flushed = rbuf_flush_contiguous(&rb, tfp, &bytes_written, &chunks_written);
    assert(flushed == 0);
    assert(rb.rcv_base == 0);
    printf("  Gap at seq=0: flushed %d chunks (expected 0)\n", flushed);

    /* Fill gap by inserting seq=0 */
    Packet p0;
    init_packet(&p0, PACKET_DATA, 0, 0, "Chunk-0", 7);
    assert(rbuf_put(&rb, &p0) == 0);
    assert(rbuf_is_buffered(&rb, 0) == 1);

    /* Now flush should contiguous-flush 0, 1, 2 and advance rcv_base to 3 */
    flushed = rbuf_flush_contiguous(&rb, tfp, &bytes_written, &chunks_written);
    printf("  After inserting seq=0: flushed %d chunks, rcv_base=%u\n", flushed, rb.rcv_base);
    assert(flushed == 3);
    assert(rb.rcv_base == 3);
    assert(chunks_written == 3);
    assert(bytes_written == 21);
    assert(rbuf_is_buffered(&rb, 0) == 0); /* Cleared after flush */
    assert(rbuf_is_buffered(&rb, 1) == 0);
    assert(rbuf_is_buffered(&rb, 2) == 0);
    fclose(tfp);

    /* Verify file content */
    tfp = fopen("test_data/rbuf_test.tmp", "rb");
    assert(tfp != NULL);
    char readback[32] = {0};
    size_t nr = fread(readback, 1, sizeof(readback), tfp);
    assert(nr == 21);
    assert(memcmp(readback, "Chunk-0Chunk-1Chunk-2", 21) == 0);
    fclose(tfp);
    remove("test_data/rbuf_test.tmp");
    printf(">>> TEST 13 PASSED: Contiguous flushing flushes exactly in-order sequence!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 14: Duplicate handling and out-of-window packet rejection */
    /* ------------------------------------------------------------------ */
    printf("[TEST 14] Duplicate packet and stale packet handling...\n");
    /* rcv_base is 3, window is 4 -> window is [3..6] */
    Packet p3;
    init_packet(&p3, PACKET_DATA, 3, 0, "Chunk-3", 7);
    assert(rbuf_put(&rb, &p3) == 0);
    /* Duplicate write to slot 3 should succeed */
    assert(rbuf_put(&rb, &p3) == 0);

    /* Stale packet seq=1 (< rcv_base=3) must be rejected */
    assert(rbuf_is_in_window(&rb, 1) == 0);
    assert(rbuf_put(&rb, &p1) == -1);

    /* Advance rcv_base past 3 */
    tfp = fopen("test_data/rbuf_test.tmp", "wb");
    assert(tfp != NULL);
    flushed = rbuf_flush_contiguous(&rb, tfp, NULL, NULL);
    assert(flushed == 1);
    assert(rb.rcv_base == 4);
    fclose(tfp);
    remove("test_data/rbuf_test.tmp");
    printf(">>> TEST 14 PASSED: Duplicate & stale packet handling verified!\n\n");

    /* ------------------------------------------------------------------ */
    /* TEST 15: RecvBuffer reset & large sequence rollover */
    /* ------------------------------------------------------------------ */
    printf("[TEST 15] RecvBuffer reset and sequence rollover handling...\n");
    rbuf_reset(&rb);
    assert(rb.rcv_base == 0);
    assert(rb.window_size == 0);

    assert(rbuf_init(&rb, 4) == 0);
    rb.rcv_base = 0xFFFFFFFEu; /* Near 32-bit wrap */
    assert(rbuf_is_in_window(&rb, 0xFFFFFFFEu) == 1);
    assert(rbuf_is_in_window(&rb, 0xFFFFFFFFu) == 1);
    assert(rbuf_is_in_window(&rb, 0x00000000u) == 1);
    assert(rbuf_is_in_window(&rb, 0x00000001u) == 1);
    assert(rbuf_is_in_window(&rb, 0x00000002u) == 0);

    Packet wrap0, wrap1;
    init_packet(&wrap0, PACKET_DATA, 0xFFFFFFFEu, 0, "WrapA", 5);
    init_packet(&wrap1, PACKET_DATA, 0xFFFFFFFFu, 0, "WrapB", 5);
    assert(rbuf_put(&rb, &wrap0) == 0);
    assert(rbuf_put(&rb, &wrap1) == 0);

    tfp = fopen("test_data/rbuf_test.tmp", "wb");
    assert(tfp != NULL);
    flushed = rbuf_flush_contiguous(&rb, tfp, NULL, NULL);
    assert(flushed == 2);
    assert(rb.rcv_base == 0x00000000u); /* Successfully rolled over! */
    fclose(tfp);
    remove("test_data/rbuf_test.tmp");
    printf(">>> TEST 15 PASSED: Rollover and reset verified!\n\n");

    printf("====================================================\n");
    printf(">>> ALL DIMPLE DAY 3 & DAY 4 TESTS PASSED 100%%! <<<\n");
    printf("====================================================\n");
    return 0;
}
