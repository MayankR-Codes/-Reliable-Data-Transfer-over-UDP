#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

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
  static long long get_time_ms(void) {
      FILETIME ft;
      GetSystemTimeAsFileTime(&ft);
      long long t = ((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
      return t / 10000LL; /* 100ns ticks -> ms */
  }
  static void start_emulator_delay(int delay_ms) {
      STARTUPINFO si; PROCESS_INFORMATION pi;
      ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
      ZeroMemory(&pi, sizeof(pi));
      char cmd[128];
      snprintf(cmd, sizeof(cmd),
               "emulator.exe --listen 8000 --recv-port 9001 --delay %d", delay_ms);
      CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
      Sleep(500);
      /* store in globals for cleanup */
      extern PROCESS_INFORMATION g_emu_pi;
      g_emu_pi = pi;
  }
  PROCESS_INFORMATION g_emu_pi;
  static void stop_emulator(void) {
      TerminateProcess(g_emu_pi.hProcess, 0);
      CloseHandle(g_emu_pi.hProcess);
      CloseHandle(g_emu_pi.hThread);
  }
#else
  #include <sys/time.h>
  static long long get_time_ms(void) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
  }
  static void start_emulator_delay(int d) { (void)d; }
  static void stop_emulator(void) {}
#endif

int main(void) {
    printf("====================================================\n");
    printf(" Testing Emulator Artificial Delay (Mayank Day 3)  \n");
    printf("====================================================\n\n");

    if (init_network() != 0) return 1;

    int configured_delay_ms = 50;  /* emulator configured with 50ms delay */
    int emu_port  = 8000;
    int recv_port = 9001;

    /* Launch emulator with 50ms delay */
    start_emulator_delay(configured_delay_ms);

    /* Setup receiver */
    SOCKET recv_sock = create_udp_socket();
    assert(bind_udp_socket(recv_sock, recv_port) == 0);
    set_socket_timeout(recv_sock, 3000);

    /* Setup sender */
    SOCKET send_sock = create_udp_socket();
    set_socket_timeout(send_sock, 3000);

    /* ---- Test 1: Measure round-trip time with delay ---- */
    printf("[TEST 1] Measuring RTT through %d ms delay emulator...\n",
           configured_delay_ms);

    Packet tx;
    init_packet(&tx, PACKET_DATA, 1, 0, "Delay Test Payload", 18);

    long long t_before = get_time_ms();
    int sent = send_packet_to(send_sock, &tx, "127.0.0.1", emu_port);
    assert(sent > 0);

    Packet rx;
    struct sockaddr_in from;
    int recvd = recv_packet(recv_sock, &rx, &from);
    long long t_after = get_time_ms();
    long long elapsed = t_after - t_before;

    assert(recvd > 0);
    assert(verify_packet_checksum(&rx) == 1);
    printf("    Elapsed one-way time  : %ld ms\n", (long)elapsed);
    printf("    Configured delay      : %d ms\n", configured_delay_ms);
    /* Verify the delay was actually applied — must be at least 80% of the delay */
    assert(elapsed >= (long long)(configured_delay_ms * 0.8));
    printf(">>> TEST 1 PASSED: Delay correctly applied by emulator!\n\n");

    /* ---- Test 2: Verify packet arrives intact (no corruption from delay) ---- */
    printf("[TEST 2] Verifying packet integrity is preserved through delay...\n");
    assert(rx.seq_num == tx.seq_num);
    assert(rx.length  == tx.length);
    assert(rx.type    == tx.type);
    printf(">>> TEST 2 PASSED: Packet arrived intact with correct fields!\n\n");

    /* ---- Test 3: Send 5 packets and check all are delayed properly ---- */
    printf("[TEST 3] Sending 5 packets, verifying all are delayed...\n");
    int delayed_count = 0;
    for (int i = 0; i < 5; i++) {
        Packet p;
        char buf[32]; snprintf(buf, sizeof(buf), "Pkt %d", i);
        init_packet(&p, PACKET_DATA, (uint32_t)(10 + i), 0, buf,
                    (uint16_t)strlen(buf));

        long long t0 = get_time_ms();
        send_packet_to(send_sock, &p, "127.0.0.1", emu_port);
        Packet pr; struct sockaddr_in fa;
        set_socket_timeout(recv_sock, 2000);
        int rb = recv_packet(recv_sock, &pr, &fa);
        long long dt = get_time_ms() - t0;
        if (rb > 0 && dt >= (long long)(configured_delay_ms * 0.8)) {
            delayed_count++;
        }
        printf("    Packet %d: elapsed=%ld ms\n", i, (long)dt);
    }
    assert(delayed_count == 5);
    printf(">>> TEST 3 PASSED: All 5 packets correctly delayed!\n\n");

    /* Send FIN */
    Packet fin; init_packet(&fin, PACKET_FIN, 9999, 0, NULL, 0);
    send_packet_to(send_sock, &fin, "127.0.0.1", emu_port);

    stop_emulator();
    CLOSE_SOCKET(send_sock);
    CLOSE_SOCKET(recv_sock);
    cleanup_network();

    printf("====================================================\n");
    printf(">>> ALL DELAY TESTS (MAYANK DAY 3) PASSED 100%%! <<<\n");
    printf("====================================================\n");
    return 0;
}
