#include "socket_utils.h"
#include <stdio.h>
#include <string.h>

#define PACKET_HEADER_SIZE 15

int init_network(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return -1;
    }
#endif
    return 0;
}

void cleanup_network(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

SOCKET create_udp_socket(void) {
    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET) {
        perror("[ERROR] Failed to create UDP socket");
    }
    return sockfd;
}

int bind_udp_socket(SOCKET sockfd, int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        perror("[ERROR] Failed to bind socket");
        return -1;
    }
    return 0;
}

int set_socket_timeout(SOCKET sockfd, int timeout_ms) {
#ifdef _WIN32
    DWORD timeout = (DWORD)timeout_ms;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0) {
        perror("[ERROR] Failed to set socket receive timeout");
        return -1;
    }
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)) < 0) {
        perror("[ERROR] Failed to set socket receive timeout");
        return -1;
    }
#endif
    return 0;
}

void setup_sockaddr(struct sockaddr_in *addr, const char *ip, int port) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);
    if (ip && strlen(ip) > 0) {
        addr->sin_addr.s_addr = inet_addr(ip);
    } else {
        addr->sin_addr.s_addr = htonl(INADDR_ANY);
    }
}

int send_packet(SOCKET sockfd, const Packet *pkt, const struct sockaddr_in *dest_addr) {
    if (!pkt || !dest_addr) return -1;
    /* Total size to send: 15-byte header + length of payload */
    size_t send_size = PACKET_HEADER_SIZE + pkt->length;
    int bytes_sent = sendto(sockfd, (const char *)pkt, (int)send_size, 0,
                            (const struct sockaddr *)dest_addr, sizeof(*dest_addr));
    return bytes_sent;
}

int send_packet_to(SOCKET sockfd, const Packet *pkt, const char *dest_ip, int dest_port) {
    struct sockaddr_in dest;
    setup_sockaddr(&dest, dest_ip, dest_port);
    return send_packet(sockfd, pkt, &dest);
}

int recv_packet(SOCKET sockfd, Packet *pkt, struct sockaddr_in *src_addr) {
    if (!pkt) return -1;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    
    memset(pkt, 0, sizeof(Packet));
    int bytes_received = recvfrom(sockfd, (char *)pkt, sizeof(Packet), 0,
                                  (struct sockaddr *)src_addr, &addr_len);
    return bytes_received;
}
