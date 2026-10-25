#include <stdio.h>
#include <string.h>
#include <assert.h>
#if defined(__has_include)
  #if __has_include("../include/socket_utils.h")
    #include "../include/socket_utils.h"
  #elif __has_include("socket_utils.h")
    #include "socket_utils.h"
  #endif
#else
  #include "socket_utils.h"
#endif

#ifdef _WIN32
#include <windows.h>
static PROCESS_INFORMATION emu_pi;
static int started_local_emulator = 0;

static void try_launch_emulator(void) {
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&emu_pi, sizeof(emu_pi));

    char cmd[] = "emulator.exe --listen 8000 --recv-port 9001";
    if (CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &emu_pi)) {
        started_local_emulator = 1;
        Sleep(400); /* Wait for emulator process to bind port 8000 */
    }
}

static void stop_local_emulator(void) {
    if (started_local_emulator) {
        TerminateProcess(emu_pi.hProcess, 0);
        CloseHandle(emu_pi.hProcess);
        CloseHandle(emu_pi.hThread);
        started_local_emulator = 0;
    }
}
#else
static void try_launch_emulator(void) {}
static void stop_local_emulator(void) {}
#endif

int main(void) {
    printf("====================================================\n");
    printf(" Testing 3-Node Chain: Sender -> Emulator -> Recv   \n");
    printf("====================================================\n\n");

    if (init_network() != 0) return 1;

    int emu_port = 8000;
    int recv_port = 9001;

    /* 1. Setup Receiver */
    SOCKET recv_sock = create_udp_socket();
    assert(bind_udp_socket(recv_sock, recv_port) == 0);
    set_socket_timeout(recv_sock, 250);

    /* 2. Setup Sender */
    SOCKET send_sock = create_udp_socket();
    set_socket_timeout(send_sock, 1000);

    /* 3. Send DATA packet to Emulator (port 8000) */
    Packet data_pkt;
    const char *payload_text = "Mayank Emulator Day 1 Test Payload";
    init_packet(&data_pkt, PACKET_DATA, 1, 0, payload_text, (uint16_t)strlen(payload_text));

    printf("[1] Sender transmitting packet to Emulator (Port %d)...\n", emu_port);
    Packet recv_pkt;
    struct sockaddr_in emu_addr;
    int recvd = -1;

    for (int attempt = 0; attempt < 20 && recvd <= 0; attempt++) {
        /* If emulator is not yet detected after first couple tries, try auto-launching it */
        if (attempt == 1 && !started_local_emulator) {
            try_launch_emulator();
        }

        int sent = send_packet_to(send_sock, &data_pkt, "127.0.0.1", emu_port);
        if (sent <= 0) {
            fprintf(stderr, "Failed to send packet to emulator (socket error %d).\n",
                    WSAGetLastError());
            stop_local_emulator();
            CLOSE_SOCKET(send_sock);
            CLOSE_SOCKET(recv_sock);
            cleanup_network();
            return 1;
        }
        recvd = recv_packet(recv_sock, &recv_pkt, &emu_addr);
    }

    if (recvd <= 0) {
        fprintf(stderr,
                "Timed out waiting for the emulator on port %d. "
                "Start emulator.exe before running this test.\n",
                emu_port);
        stop_local_emulator();
        CLOSE_SOCKET(send_sock);
        CLOSE_SOCKET(recv_sock);
        cleanup_network();
        return 1;
    }

    printf("[2] Receiver got the forwarded packet on Port %d.\n", recv_port);
    assert(verify_packet_checksum(&recv_pkt) == 1);
    assert(strcmp(recv_pkt.payload, payload_text) == 0);
    printf(">>> Receiver successfully received packet forwarded by Emulator!\n");

    /* 4. Receiver sends ACK back to Emulator */
    Packet ack_pkt;
    init_packet(&ack_pkt, PACKET_ACK, 0, recv_pkt.seq_num, NULL, 0);
    printf("[3] Receiver sending ACK=%d back to Emulator...\n", ack_pkt.ack_num);
    send_packet(recv_sock, &ack_pkt, &emu_addr);

    /* 5. Sender receives ACK forwarded by Emulator */
    Packet sender_ack;
    struct sockaddr_in emu_ret_addr;
    printf("[4] Sender waiting for returning ACK...\n");
    int ack_bytes = -1;
    int ack_error = 0;
    for (int attempt = 0; attempt < 25 && ack_bytes <= 0; attempt++) {
        ack_bytes = recv_packet(send_sock, &sender_ack, &emu_ret_addr);
        if (ack_bytes <= 0) {
            ack_error = WSAGetLastError();
            if (ack_error != WSAECONNRESET && ack_error != WSAETIMEDOUT) {
                break;
            }
        }
    }

    if (ack_bytes <= 0) {
        fprintf(stderr, "Failed to receive ACK from emulator (socket error %d).\n", ack_error);
        stop_local_emulator();
        CLOSE_SOCKET(send_sock);
        CLOSE_SOCKET(recv_sock);
        cleanup_network();
        return 1;
    }

    assert(verify_packet_checksum(&sender_ack) == 1);
    assert(sender_ack.type == PACKET_ACK);
    assert(sender_ack.ack_num == 1);
    printf(">>> Sender received verified ACK from Emulator!\n");

    /* Cleanup sockets and processes */
    stop_local_emulator();
    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);
    cleanup_network();

    printf("\n====================================================\n");
    printf(">>> 3-NODE CHAIN (SENDER-EMULATOR-RECEIVER) PASSED! <<<\n");
    printf("====================================================\n");

    return 0;
}
