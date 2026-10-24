#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include "packet.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define CLOSE_SOCKET(s) close(s)
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
#endif

/* Initialize network subsystem (WSAStartup on Windows) */
int init_network(void);

/* Clean up network subsystem (WSACleanup on Windows) */
void cleanup_network(void);

/* Create a standard UDP socket */
SOCKET create_udp_socket(void);

/* Bind a UDP socket to a local port */
int bind_udp_socket(SOCKET sockfd, int port);

/* Set socket receive timeout in milliseconds */
int set_socket_timeout(SOCKET sockfd, int timeout_ms);

/* Send a Packet struct to a target IPv4 address and port */
int send_packet_to(SOCKET sockfd, const Packet *pkt, const char *dest_ip, int dest_port);

/* Send a Packet struct to an existing sockaddr_in address */
int send_packet(SOCKET sockfd, const Packet *pkt, const struct sockaddr_in *dest_addr);

/* Receive a Packet struct from a UDP socket */
int recv_packet(SOCKET sockfd, Packet *pkt, struct sockaddr_in *src_addr);

/* Helper to fill a sockaddr_in structure */
void setup_sockaddr(struct sockaddr_in *addr, const char *ip, int port);

#endif /* SOCKET_UTILS_H */
