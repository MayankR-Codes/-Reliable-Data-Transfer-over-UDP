#include "packet.h"
#include <stdio.h>
#include <string.h>

#define PACKET_HEADER_SIZE 15 /* 1 + 4 + 4 + 2 + 4 */

static uint32_t crc32_table[256];
static int table_computed = 0;

static void generate_crc32_table(void) {
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = polynomial ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc32_table[i] = c;
    }
    table_computed = 1;
}

uint32_t crc32_calculate(const uint8_t *data, size_t length) {
    if (!table_computed) {
        generate_crc32_table();
    }
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t compute_packet_checksum(Packet *pkt) {
    uint32_t old_checksum = pkt->checksum;
    pkt->checksum = 0;

    /* Total bytes to hash: the 15-byte header + actual payload length */
    size_t total_bytes = PACKET_HEADER_SIZE + pkt->length;
    uint32_t calculated = crc32_calculate((const uint8_t *)pkt, total_bytes);

    pkt->checksum = old_checksum;
    return calculated;
}

int verify_packet_checksum(const Packet *pkt) {
    Packet temp;
    memcpy(&temp, pkt, sizeof(Packet));
    temp.checksum = 0;

    size_t total_bytes = PACKET_HEADER_SIZE + temp.length;
    uint32_t expected = crc32_calculate((const uint8_t *)&temp, total_bytes);

    return (expected == pkt->checksum);
}

void init_packet(Packet *pkt, uint8_t type, uint32_t seq_num, uint32_t ack_num, 
                 const char *payload_data, uint16_t payload_len) {
    if (!pkt) return;
    memset(pkt, 0, sizeof(Packet));

    pkt->type = type;
    pkt->seq_num = seq_num;
    pkt->ack_num = ack_num;
    pkt->length = (payload_len > MAX_PAYLOAD_SIZE) ? MAX_PAYLOAD_SIZE : payload_len;

    if (payload_data && pkt->length > 0) {
        memcpy(pkt->payload, payload_data, pkt->length);
    }

    pkt->checksum = compute_packet_checksum(pkt);
}

void print_packet_info(const char *prefix, const Packet *pkt) {
    if (!pkt) return;
    const char *type_str = "UNKNOWN";
    if (pkt->type == PACKET_DATA) type_str = "DATA";
    else if (pkt->type == PACKET_ACK) type_str = "ACK";
    else if (pkt->type == PACKET_FIN) type_str = "FIN";

    printf("[%s] Type=%s Seq=%u Ack=%u Len=%u Checksum=0x%08X\n",
           prefix ? prefix : "PKT",
           type_str,
           pkt->seq_num,
           pkt->ack_num,
           pkt->length,
           pkt->checksum);
}
