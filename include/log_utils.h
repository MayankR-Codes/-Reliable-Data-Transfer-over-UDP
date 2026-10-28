#ifndef LOG_UTILS_H
#define LOG_UTILS_H

/*
 * log_utils.h — Standardized Logging Macros
 *
 * Owned by: Dimple Mohapatra (Day 5)
 *
 * Standardized log format across all ARQ protocols (SW, GBN, SR):
 *   [SEND] Seq=12 Length=1024 Checksum=0x8A2F
 *   [RECV] Ack=12 Valid=YES
 *   [TIMEOUT] Packet=12 Retransmitting...
 *   [CORRUPT] Packet=14 Discarded (Checksum mismatch)
 */

#include <stdio.h>
#include <stdint.h>

#define LOG_SEND(seq, len, checksum) \
    printf("[SEND] Seq=%-5u Length=%-4u Checksum=0x%08X\n", \
           (unsigned int)(seq), (unsigned int)(len), (unsigned int)(checksum))

#define LOG_SEND_ACK(ack) \
    printf("[SEND] Ack=%-5u\n", (unsigned int)(ack))

#define LOG_SEND_FIN(seq) \
    printf("[SEND] FIN  Seq=%-5u\n", (unsigned int)(seq))

#define LOG_RECV_DATA(seq, len, valid) \
    printf("[RECV] Seq=%-5u Length=%-4u Valid=%s\n", \
           (unsigned int)(seq), (unsigned int)(len), (valid) ? "YES" : "NO")

#define LOG_RECV_ACK(ack, valid) \
    printf("[RECV] Ack=%-5u Valid=%s\n", \
           (unsigned int)(ack), (valid) ? "YES" : "NO")

#define LOG_TIMEOUT(seq) \
    printf("[TIMEOUT] Packet=%-5u Retransmitting...\n", (unsigned int)(seq))

#define LOG_TIMEOUT_ATTEMPT(seq, attempt) \
    printf("[TIMEOUT] Packet=%-5u Retransmitting... (attempt %d)\n", \
           (unsigned int)(seq), (int)(attempt))

#define LOG_CORRUPT(seq, reason) \
    printf("[CORRUPT] Packet=%-5u Discarded (%s)\n", \
           (unsigned int)(seq), (reason))

#endif /* LOG_UTILS_H */
