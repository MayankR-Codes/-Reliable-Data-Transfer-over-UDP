#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

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
  static PROCESS_INFORMATION g_emu_pi;

  static void start_emulator_corrupt(int corrupt_percent, int emu_port, int recv_port, unsigned int seed) {
      STARTUPINFO si;
      ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
      ZeroMemory(&g_emu_pi, sizeof(g_emu_pi));
      char cmd[256];
      snprintf(cmd, sizeof(cmd),
               "emulator.exe --listen %d --recv-port %d --corrupt %d --seed %u",
               emu_port, recv_port, corrupt_percent, seed);
      CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &g_emu_pi);
      Sleep(400); /* Allow emulator socket to bind */
  }

  static void stop_emulator(void) {
      if (g_emu_pi.hProcess) {
          TerminateProcess(g_emu_pi.hProcess, 0);
          WaitForSingleObject(g_emu_pi.hProcess, 1000);
          CloseHandle(g_emu_pi.hProcess);
          CloseHandle(g_emu_pi.hThread);
          g_emu_pi.hProcess = NULL;
          Sleep(200);
      }
  }
#else
  #include <unistd.h>
  static void start_emulator_corrupt(int c, int ep, int rp, unsigned int s) {
      (void)c; (void)ep; (void)rp; (void)s;
  }
  static void stop_emulator(void) {}
#endif

int main(void) {
    printf("====================================================\n");
    printf(" Testing Emulator Packet Corruption (Mayank Day 4)  \n");
    printf("====================================================\n\n");

    if (init_network() != 0) return 1;

    int emu_port  = 8004;
    int recv_port = 9004;

    /* ---------------------------------------------------------------
       TEST 1: 100% Corruption rate — every DATA packet payload flipped
       --------------------------------------------------------------- */
    printf("[TEST 1] Testing 100%% corruption rate (--corrupt 100)...\n");
    start_emulator_corrupt(100, emu_port, recv_port, 42);

    SOCKET recv_sock = create_udp_socket();
    assert(bind_udp_socket(recv_sock, recv_port) == 0);
    set_socket_timeout(recv_sock, 2000);

    SOCKET send_sock = create_udp_socket();
    set_socket_timeout(send_sock, 2000);

    int corrupt_detected = 0;
    int total_test1_pkts = 5;

    for (int i = 0; i < total_test1_pkts; i++) {
        Packet tx;
        char buf[64];
        snprintf(buf, sizeof(buf), "Payload-Message-For-Corruption-Test-%d", i);
        init_packet(&tx, PACKET_DATA, (uint32_t)i, 0, buf, (uint16_t)strlen(buf));

        send_packet_to(send_sock, &tx, "127.0.0.1", emu_port);

        Packet rx;
        struct sockaddr_in from;
        int bytes = recv_packet(recv_sock, &rx, &from);
        assert(bytes > 0);

        /* Checksum must fail because payload byte was inverted */
        int valid = verify_packet_checksum(&rx);
        if (!valid) {
            corrupt_detected++;
        }
        printf("  Packet %d: Checksum Valid=%s (Expected: NO)\n",
               i, valid ? "YES" : "NO");
    }

    assert(corrupt_detected == total_test1_pkts);
    printf(">>> TEST 1 PASSED: 100%% of DATA packets correctly corrupted!\n\n");

    /* ---------------------------------------------------------------
       TEST 2: Verify exactly 1 payload byte is altered
       --------------------------------------------------------------- */
    printf("[TEST 2] Verifying bit-flip payload modification...\n");
    Packet tx2;
    const char *orig_text = "ABCDEFGHIJ0123456789abcdefghij";
    init_packet(&tx2, PACKET_DATA, 100, 0, orig_text, (uint16_t)strlen(orig_text));
    send_packet_to(send_sock, &tx2, "127.0.0.1", emu_port);

    Packet rx2;
    struct sockaddr_in from2;
    int b2 = recv_packet(recv_sock, &rx2, &from2);
    assert(b2 > 0);

    int diff_bytes = 0;
    int diff_idx = -1;
    for (int i = 0; i < tx2.length; i++) {
        if (tx2.payload[i] != rx2.payload[i]) {
            diff_bytes++;
            diff_idx = i;
        }
    }
    assert(diff_bytes == 1);
    assert(diff_idx >= 0);
    /* Verify byte was inverted with 0xFF */
    unsigned char expected_byte = (unsigned char)tx2.payload[diff_idx] ^ 0xFF;
    unsigned char actual_byte   = (unsigned char)rx2.payload[diff_idx];
    assert(expected_byte == actual_byte);
    printf("  Original byte at index %d: 0x%02X ('%c')\n",
           diff_idx, (unsigned char)tx2.payload[diff_idx], tx2.payload[diff_idx]);
    printf("  Corrupted byte at index %d: 0x%02X\n",
           diff_idx, actual_byte);
    printf(">>> TEST 2 PASSED: Exactly 1 payload byte inverted via 0xFF!\n\n");

    /* ---------------------------------------------------------------
       TEST 3: FIN packet is NEVER corrupted
       --------------------------------------------------------------- */
    printf("[TEST 3] Verifying PACKET_FIN is exempt from corruption...\n");
    Packet fin;
    init_packet(&fin, PACKET_FIN, 9999, 0, NULL, 0);
    send_packet_to(send_sock, &fin, "127.0.0.1", emu_port);

    Packet rx_fin;
    struct sockaddr_in from_fin;
    int b_fin = recv_packet(recv_sock, &rx_fin, &from_fin);
    assert(b_fin > 0);
    assert(rx_fin.type == PACKET_FIN);
    assert(verify_packet_checksum(&rx_fin) == 1);
    printf("  FIN Packet arrived with Valid Checksum = YES\n");
    printf(">>> TEST 3 PASSED: FIN packet bypassed corruption untouched!\n\n");

    stop_emulator();
    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);

    /* ---------------------------------------------------------------
       TEST 4: Statistical corruption (e.g. 30% corruption)
       --------------------------------------------------------------- */
    printf("[TEST 4] Testing statistical corruption (--corrupt 30)...\n");
    emu_port  = 8005;
    recv_port = 9005;
    start_emulator_corrupt(30, emu_port, recv_port, 999);

    recv_sock = create_udp_socket();
    assert(bind_udp_socket(recv_sock, recv_port) == 0);
    set_socket_timeout(recv_sock, 2000);

    send_sock = create_udp_socket();
    set_socket_timeout(send_sock, 2000);

    int total_pkts = 50;
    int corrupted_count = 0;
    int valid_count = 0;

    for (int i = 0; i < total_pkts; i++) {
        Packet p;
        char buf[32];
        snprintf(buf, sizeof(buf), "StatData-%d", i);
        init_packet(&p, PACKET_DATA, (uint32_t)i, 0, buf, (uint16_t)strlen(buf));

        send_packet_to(send_sock, &p, "127.0.0.1", emu_port);

        Packet r;
        struct sockaddr_in f;
        int r_bytes = recv_packet(recv_sock, &r, &f);
        assert(r_bytes > 0);

        if (verify_packet_checksum(&r)) {
            valid_count++;
        } else {
            corrupted_count++;
        }
    }

    double observed_rate = (double)corrupted_count / (double)total_pkts * 100.0;
    printf("  Total Sent     : %d\n", total_pkts);
    printf("  Valid (clean)  : %d\n", valid_count);
    printf("  Corrupted pkts : %d\n", corrupted_count);
    printf("  Measured rate  : %.1f%% (Configured: 30.0%%)\n", observed_rate);

    /* For 50 samples with p=0.30, verify corruption actually occurred in reasonable range [10%, 50%] */
    assert(corrupted_count >= 5);
    assert(corrupted_count <= 25);
    assert(valid_count + corrupted_count == total_pkts);
    printf(">>> TEST 4 PASSED: Statistical corruption rate verified!\n\n");

    /* Terminate emulator with FIN */
    init_packet(&fin, PACKET_FIN, 9999, 0, NULL, 0);
    send_packet_to(send_sock, &fin, "127.0.0.1", emu_port);

    stop_emulator();
    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);
    cleanup_network();

    printf("====================================================\n");
    printf(">>> ALL CORRUPTION TESTS (MAYANK DAY 4) PASSED 100%%! <<<\n");
    printf("====================================================\n");
    return 0;
}
