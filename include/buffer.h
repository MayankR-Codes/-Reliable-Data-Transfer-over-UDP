#ifndef BUFFER_H
#define BUFFER_H

/*
 * buffer.h — Sender Window Ring Buffer
 *
 * Owned by: Dimple Mohapatra (Day 3)
 *
 * Purpose:
 *   Provides a fixed-size circular (ring) buffer that the Go-Back-N sender
 *   uses to store in-flight packets that have been transmitted but not yet
 *   acknowledged. This allows the sender to quickly retransmit all unacked
 *   packets from [base .. next_seq_num - 1] on timeout.
 *
 *   The buffer also supports Selective Repeat (Day 4 - Raushan) by storing
 *   per-slot metadata (status, retransmit timestamp).
 *
 * Design:
 *   - Ring buffer of size MAX_WINDOW_SIZE (configurable at compile time).
 *   - Slots are indexed by (seq_num % MAX_WINDOW_SIZE).
 *   - Each slot holds:
 *       - A copy of the Packet (ready to retransmit without re-reading file).
 *       - Status: SLOT_EMPTY, SLOT_SENT (unacked), SLOT_ACKED.
 *       - Timestamp of when it was last sent (for per-packet timeout in SR).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "packet.h"

/* Maximum sender/receiver window size. Sequence number space must be
   >= 2 * MAX_WINDOW_SIZE for Selective Repeat safety. */
#define MAX_WINDOW_SIZE 32

/* Per-slot state flags */
#define SLOT_EMPTY  0  /* Slot is free — no packet stored here       */
#define SLOT_SENT   1  /* Packet has been sent, awaiting ACK          */
#define SLOT_ACKED  2  /* ACK received — slot can be advanced/freed   */

/* A single ring-buffer slot */
typedef struct {
    Packet   pkt;            /* Full copy of the packet for retransmission  */
    int      status;         /* SLOT_EMPTY | SLOT_SENT | SLOT_ACKED         */
    uint32_t seq_num;        /* Sequence number stored in this slot         */
    long long sent_time_ms;  /* Wall-clock ms when packet was last sent     */
} WindowSlot;

/* The sender window ring buffer */
typedef struct {
    WindowSlot slots[MAX_WINDOW_SIZE]; /* Circular array of packet slots    */
    uint32_t   base;                   /* Oldest unacked sequence number     */
    uint32_t   next_seq_num;           /* Next sequence number to assign     */
    int        window_size;            /* Configured window size (N <= MAX)  */
    int        count;                  /* Number of currently in-flight pkts */
} WindowBuffer;

/* =========================================================================
   Function Prototypes (static inline — header-only implementation below)
   ========================================================================= */

/* Initialize the ring buffer with a given window size (1 <= N <= MAX_WINDOW_SIZE).
   Returns 0 on success, -1 if window_size is out of range. */
static inline int  wbuf_init(WindowBuffer *wb, int window_size);

/* Returns 1 if the window is full (next_seq_num - base >= window_size), else 0. */
static inline int  wbuf_is_full(const WindowBuffer *wb);

/* Returns 1 if there are no in-flight packets (base == next_seq_num), else 0. */
static inline int  wbuf_is_empty(const WindowBuffer *wb);

/* Number of packets currently in-flight (sent but unacked). */
static inline int  wbuf_in_flight(const WindowBuffer *wb);

/* Store a packet in the next available slot and mark it SLOT_SENT.
   Stamps sent_time_ms with the provided timestamp (ms since epoch or tick).
   Returns 0 on success, or -1 if the window is full or arguments are NULL. */
static inline int  wbuf_push(WindowBuffer *wb, const Packet *pkt, long long now_ms);

/* Mark a sequence number as SLOT_ACKED and advance base past all
   consecutive leading ACKed slots (cumulative ACK for GBN).
   Returns the new value of base, or -1 if seq_num is out of window. */
static inline int  wbuf_ack(WindowBuffer *wb, uint32_t ack_seq_num);

/* Selective-ACK: mark only the specific seq_num as SLOT_ACKED without
   advancing base past unacked gaps (used by Selective Repeat — Raushan Day 4).
   Returns 0 on success, -1 if not found. */
static inline int  wbuf_selective_ack(WindowBuffer *wb, uint32_t seq_num);

/* Retrieve a pointer to the slot for seq_num, or NULL if not in window. */
static inline WindowSlot *wbuf_get_slot(WindowBuffer *wb, uint32_t seq_num);

/* Reset a slot back to SLOT_EMPTY (called after base advances). */
static inline void wbuf_clear_slot(WindowBuffer *wb, uint32_t seq_num);

/* Reset the entire buffer (e.g., on connection reset). */
static inline void wbuf_reset(WindowBuffer *wb);

/* =========================================================================
   Inline Implementations (header-only for simplicity across modules)
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

    wb->slots[idx].pkt          = *pkt;        /* Deep copy the full packet */
    wb->slots[idx].pkt.seq_num  = seq;
    wb->slots[idx].seq_num      = seq;
    wb->slots[idx].status       = SLOT_SENT;
    wb->slots[idx].sent_time_ms = now_ms;

    wb->next_seq_num++;
    wb->count++;
    return 0;   /* 0 = success, -1 = full/error */
}

static inline int wbuf_ack(WindowBuffer *wb, uint32_t ack_seq_num) {
    if (!wb) return -1;
    uint32_t in_flight = wb->next_seq_num - wb->base;
    uint32_t diff = ack_seq_num - wb->base;
    /* ack_seq_num must be within [base, next_seq_num) */
    if (diff >= in_flight) return -1;

    /* Mark this slot and all prior as ACKED */
    for (uint32_t i = 0; i <= diff; i++) {
        uint32_t s = wb->base + i;
        int idx = (int)(s % (uint32_t)MAX_WINDOW_SIZE);
        wb->slots[idx].status = SLOT_ACKED;
    }

    /* Slide base forward past all consecutive leading ACKED slots */
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

    /* Advance base past leading ACKED slots */
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

#endif /* BUFFER_H */
