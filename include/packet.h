#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stddef.h>

#define PACKET_DATA 1
#define PACKET_ACK  2
#define PACKET_FIN  3

#define MAX_PAYLOAD_SIZE 1024

#pragma pack(push, 1)
typedef struct {
    uint8_t  type;                        /* PACKET_DATA, PACKET_ACK, PACKET_FIN */
    uint32_t seq_num;                     /* Sequence number */
    uint32_t ack_num;                     /* Acknowledgement number */
    uint16_t length;                      /* Actual bytes in payload (0 - 1024) */
    uint32_t checksum;                    /* CRC32 checksum over header + payload */
    char     payload[MAX_PAYLOAD_SIZE];   /* Chunk data */
} Packet;
#pragma pack(pop)

/* Calculate CRC32 of an arbitrary buffer */
uint32_t crc32_calculate(const uint8_t *data, size_t length);

/* Compute CRC32 checksum for a packet (header fields + payload length) */
uint32_t compute_packet_checksum(Packet *pkt);

/* Verify whether packet's checksum field matches its content */
int verify_packet_checksum(const Packet *pkt);

/* Helper to initialize a packet with clean fields */
void init_packet(Packet *pkt, uint8_t type, uint32_t seq_num, uint32_t ack_num, 
                 const char *payload_data, uint16_t payload_len);

/* Logging helper to format and print packet details */
void print_packet_info(const char *prefix, const Packet *pkt);

#endif /* PACKET_H */
