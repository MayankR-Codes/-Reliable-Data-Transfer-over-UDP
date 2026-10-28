#ifndef BUFFER_H
#define BUFFER_H

/*
 * buffer.h — Sender & Receiver Window Buffers
 *
 * Owned by: Dimple Mohapatra (Day 3 & Day 4)
 *
 * Components:
 *   1. WindowBuffer (Sender Ring Buffer):
 *      - Stores in-flight unacknowledged packets for GBN and SR.
 *      - Manages base and next_seq_num.
 *      - Provides cumulative ACK (GBN) and selective ACK (SR).
 *
 *   2. RecvBuffer (Receiver Circular Buffer — Day 4 with Raushan):
 *      - Memory-safe out-of-order packet buffering for Selective Repeat.
 *      - Bounds-checked slot storage indexed by (seq_num % MAX_WINDOW_SIZE).
 *      - Contiguous chunk flushing to disk when base advances.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "packet.h"

#define MAX_WINDOW_SIZE 32

#define SLOT_EMPTY  0
#define SLOT_SENT   1
#define SLOT_ACKED  2

/* ── Sender Ring Buffer Slot ────────────────────────────────────── */
typedef struct {
    Packet    pkt;
    int       status;         /* SLOT_EMPTY | SLOT_SENT | SLOT_ACKED */
    uint32_t  seq_num;
    long long sent_time_ms;
} WindowSlot;

/* ── Sender Ring Buffer ─────────────────────────────────────────── */
typedef struct {
    WindowSlot slots[MAX_WINDOW_SIZE];
    uint32_t   base;
    uint32_t   next_seq_num;
    int        window_size;
    int        count;
} WindowBuffer;

/* ── Receiver Circular Buffer Slot ──────────────────────────────── */
typedef struct {
    Packet pkt;
    int    received;          /* 0 = slot free/unwritten, 1 = received & buffered */
} RecvSlot;

/* ── Receiver Circular Buffer (Selective Repeat Out-of-Order) ───── */
typedef struct {
    RecvSlot slots[MAX_WINDOW_SIZE];
    uint32_t rcv_base;
    int      window_size;
} RecvBuffer;

/* =========================================================================
   Sender WindowBuffer Functions
   ========================================================================= */

static inline int wbuf_init(WindowBuffer *wb, int window_size) {
    if (!wb || window_size < 1 || window_size > MAX_WINDOW_SIZE) return -1;
    memset(wb, 0, sizeof(WindowBuffer));
    wb->window_size = window_size;
    wb->base        = 0;
    wb->next_seq_num = 0;
    wb->count        = 0;
    for (int i = 0; i < MAX_WINDOW_SIZE; i++) {
        wb->slots[i].status = SLOT_EMPTY;
    }
    return 0;
}

static inline int wbuf_is_full(const WindowBuffer *wb) {
    if (!wb) return 1;
    return ((wb->next_seq_num - wb->base) >= (uint32_t)wb->window_size);
}

static inline int wbuf_is_empty(const WindowBuffer *wb) {
    if (!wb) return 1;
    return (wb->next_seq_num == wb->base);
}

static inline int wbuf_in_flight(const WindowBuffer *wb) {
    if (!wb) return 0;
    return (int)(wb->next_seq_num - wb->base);
}

static inline int wbuf_push(WindowBuffer *wb, const Packet *pkt, long long now_ms) {
    if (!wb || !pkt) return -1;
    if (wbuf_is_full(wb)) return -1;

    uint32_t seq = wb->next_seq_num;
    int idx = (int)(seq % (uint32_t)MAX_WINDOW_SIZE);

    wb->slots[idx].pkt          = *pkt;
    wb->slots[idx].pkt.seq_num  = seq;
    wb->slots[idx].seq_num      = seq;
    wb->slots[idx].status       = SLOT_SENT;
    wb->slots[idx].sent_time_ms = now_ms;

    wb->next_seq_num++;
    wb->count++;
    return 0;
}

static inline int wbuf_ack(WindowBuffer *wb, uint32_t ack_seq_num) {
    if (!wb) return -1;
    uint32_t in_flight = wb->next_seq_num - wb->base;
    uint32_t diff = ack_seq_num - wb->base;
    if (diff >= in_flight) return -1;

    for (uint32_t i = 0; i <= diff; i++) {
        uint32_t s = wb->base + i;
        int idx = (int)(s % (uint32_t)MAX_WINDOW_SIZE);
        wb->slots[idx].status = SLOT_ACKED;
    }

    while (wb->base != wb->next_seq_num) {
        int idx = (int)(wb->base % (uint32_t)MAX_WINDOW_SIZE);
        if (wb->slots[idx].status != SLOT_ACKED) break;
        wb->slots[idx].status = SLOT_EMPTY;
        wb->base++;
        wb->count--;
    }
    return (int)wb->base;
}

static inline int wbuf_selective_ack(WindowBuffer *wb, uint32_t seq_num) {
    if (!wb) return -1;
    uint32_t in_flight = wb->next_seq_num - wb->base;
    uint32_t diff = seq_num - wb->base;
    if (diff >= in_flight) return -1;
    int idx = (int)(seq_num % (uint32_t)MAX_WINDOW_SIZE);
    wb->slots[idx].status = SLOT_ACKED;

    while (wb->base != wb->next_seq_num) {
        int bidx = (int)(wb->base % (uint32_t)MAX_WINDOW_SIZE);
        if (wb->slots[bidx].status != SLOT_ACKED) break;
        wb->slots[bidx].status = SLOT_EMPTY;
        wb->base++;
        wb->count--;
    }
    return 0;
}

static inline WindowSlot *wbuf_get_slot(WindowBuffer *wb, uint32_t seq_num) {
    if (!wb) return NULL;
    uint32_t in_flight = wb->next_seq_num - wb->base;
    uint32_t diff = seq_num - wb->base;
    if (diff >= in_flight) return NULL;
    int idx = (int)(seq_num % (uint32_t)MAX_WINDOW_SIZE);
    return &wb->slots[idx];
}

static inline void wbuf_clear_slot(WindowBuffer *wb, uint32_t seq_num) {
    if (!wb) return;
    int idx = (int)(seq_num % (uint32_t)MAX_WINDOW_SIZE);
    wb->slots[idx].status = SLOT_EMPTY;
}

static inline void wbuf_reset(WindowBuffer *wb) {
    if (!wb) return;
    memset(wb, 0, sizeof(WindowBuffer));
}

/* =========================================================================
   Receiver RecvBuffer Functions (Selective Repeat Out-of-Order Buffer)
   ========================================================================= */

static inline int rbuf_init(RecvBuffer *rb, int window_size) {
    if (!rb || window_size < 1 || window_size > MAX_WINDOW_SIZE) return -1;
    memset(rb, 0, sizeof(RecvBuffer));
    rb->window_size = window_size;
    rb->rcv_base    = 0;
    return 0;
}

static inline int rbuf_is_in_window(const RecvBuffer *rb, uint32_t seq_num) {
    if (!rb) return 0;
    uint32_t diff = seq_num - rb->rcv_base;
    return (diff < (uint32_t)rb->window_size);
}

static inline int rbuf_put(RecvBuffer *rb, const Packet *pkt) {
    if (!rb || !pkt) return -1;
    if (!rbuf_is_in_window(rb, pkt->seq_num)) return -1;

    int idx = (int)(pkt->seq_num % (uint32_t)MAX_WINDOW_SIZE);
    rb->slots[idx].pkt      = *pkt;
    rb->slots[idx].received = 1;
    return 0;
}

static inline int rbuf_is_buffered(const RecvBuffer *rb, uint32_t seq_num) {
    if (!rb) return 0;
    int idx = (int)(seq_num % (uint32_t)MAX_WINDOW_SIZE);
    return rb->slots[idx].received;
}

static inline int rbuf_flush_contiguous(RecvBuffer *rb, FILE *fp,
                                       unsigned long *bytes_written,
                                       unsigned long *chunks_written) {
    if (!rb || !fp) return -1;
    int flushed = 0;
    while (rb->slots[(int)(rb->rcv_base % (uint32_t)MAX_WINDOW_SIZE)].received) {
        int idx = (int)(rb->rcv_base % (uint32_t)MAX_WINDOW_SIZE);
        Packet *p = &rb->slots[idx].pkt;
        size_t n = fwrite(p->payload, 1, p->length, fp);
        if (bytes_written)  *bytes_written  += n;
        if (chunks_written) *chunks_written += 1;
        rb->slots[idx].received = 0;
        rb->rcv_base++;
        flushed++;
    }
    return flushed;
}

static inline void rbuf_reset(RecvBuffer *rb) {
    if (!rb) return;
    memset(rb, 0, sizeof(RecvBuffer));
}

#endif /* BUFFER_H */
