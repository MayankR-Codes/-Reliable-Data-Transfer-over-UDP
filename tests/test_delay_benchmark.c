/*
 * test_delay_benchmark.c
 * =======================
 * Author  : Raushan Kumar
 * Day     : 3 — SW vs GBN Delay Comparison Benchmark
 *
 * Runs a side-by-side performance benchmark between Stop-and-Wait (SW)
 * and Go-Back-N (GBN, N=8) over the channel emulator under:
 *   - 0 ms artificial delay (baseline local RTT)
 *   - 50 ms artificial delay (high RTT channel)
 *
 * Demonstrates the fundamental networking theorem:
 *   - Under latency (50 ms delay), Stop-and-Wait goodput collapses
 *     because it must wait 1 full RTT (~100 ms) per 1 KB packet (~10 KB/s max).
 *   - Go-Back-N pipelining (N=8) keeps up to 8 packets in-flight,
 *     achieving significantly higher throughput than Stop-and-Wait.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
#else
  #include <pthread.h>
  #include <unistd.h>
  #include <sys/time.h>
#endif

#include "../include/protocols.h"
#include "../include/file_io.h"

#define BENCH_INPUT_FILE     "test_data/delay_bench_input.bin"
#define BENCH_FILE_SIZE      (20 * 1024)   /* 20 KB */

#define PORT_SW_RECV         19600
#define PORT_SW_EMU          19601

#define PORT_GBN_RECV        19610
#define PORT_GBN_EMU         19611

static long long get_time_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
#endif
}

typedef struct {
    int         port;
    const char *out_path;
    int         protocol; /* 0=SW, 1=GBN */
    int         result;
} BenchRecvArgs;

#ifdef _WIN32
static unsigned __stdcall bench_recv_thread(void *arg) {
    BenchRecvArgs *a = (BenchRecvArgs *)arg;
    if (a->protocol == 0) {
        a->result = sw_receiver(a->port, a->out_path);
    } else {
        a->result = gbn_receiver(a->port, a->out_path);
    }
    return 0;
}
#endif

static double measure_transfer(int protocol, int emu_port, int recv_port,
                               int delay_ms, const char *out_path)
{
    BenchRecvArgs rargs = { recv_port, out_path, protocol, -999 };

#ifdef _WIN32
    HANDLE ht = (HANDLE)_beginthreadex(NULL, 0, bench_recv_thread, &rargs, 0, NULL);
    assert(ht != NULL);
    Sleep(250);

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "emulator.exe --listen %d --recv-port %d --delay %d",
             emu_port, recv_port, delay_ms);
    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "[FATAL] Cannot start emulator.\n");
        return 0.0;
    }
    Sleep(350);
#endif

    long long t0 = get_time_ms();
    int sret = -1;
    if (protocol == 0) {
        sret = sw_sender(BENCH_INPUT_FILE, "127.0.0.1", emu_port);
    } else {
        sret = gbn_sender(BENCH_INPUT_FILE, "127.0.0.1", emu_port, 8);
    }
    long long elapsed_ms = get_time_ms() - t0;

#ifdef _WIN32
    WaitForSingleObject(ht, 25000);
    CloseHandle(ht);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#endif

    if (sret != 0 || rargs.result != 0 || elapsed_ms <= 0) return 0.0;

    double sec = (double)elapsed_ms / 1000.0;
    double kb = (double)BENCH_FILE_SIZE / 1024.0;
    return kb / sec; /* KB/s */
}

int main(void) {
    printf("===============================================================\n");
    printf("  Raushan Kumar — Day 3: SW vs GBN Delay Throughput Benchmark  \n");
    printf("===============================================================\n\n");

    /* Create test file */
    FILE *fp = fopen(BENCH_INPUT_FILE, "wb");
    assert(fp != NULL);
    for (int i = 0; i < BENCH_FILE_SIZE; i++) {
        fputc((i * 17 + 5) & 0xFF, fp);
    }
    fclose(fp);

    char base_hash[65] = {0};
    compute_file_sha256(BENCH_INPUT_FILE, base_hash);
    printf("Test file: %s (size=%d bytes, SHA-256=%s)\n\n",
           BENCH_INPUT_FILE, BENCH_FILE_SIZE, base_hash);

    /* 1. Stop-and-Wait with 0ms vs 50ms delay */
    printf("[PHASE 1] Benchmarking Stop-and-Wait (SW)...\n");
    double sw_0ms = measure_transfer(0, PORT_SW_EMU, PORT_SW_RECV, 0, "test_data/delay_sw_0.bin");
    printf("  SW  at  0 ms Delay : %6.2f KB/s\n", sw_0ms);

    double sw_50ms = measure_transfer(0, PORT_SW_EMU, PORT_SW_RECV, 50, "test_data/delay_sw_50.bin");
    printf("  SW  at 50 ms Delay : %6.2f KB/s\n\n", sw_50ms);

    /* 2. Go-Back-N with 0ms vs 50ms delay */
    printf("[PHASE 2] Benchmarking Go-Back-N (GBN, N=8)...\n");
    double gbn_0ms = measure_transfer(1, PORT_GBN_EMU, PORT_GBN_RECV, 0, "test_data/delay_gbn_0.bin");
    printf("  GBN at  0 ms Delay : %6.2f KB/s\n", gbn_0ms);

    double gbn_50ms = measure_transfer(1, PORT_GBN_EMU, PORT_GBN_RECV, 50, "test_data/delay_gbn_50.bin");
    printf("  GBN at 50 ms Delay : %6.2f KB/s\n\n", gbn_50ms);

    /* 3. Verification */
    printf("===============================================================\n");
    printf("  BENCHMARK RESULTS & VERIFICATION                             \n");
    printf("===============================================================\n");
    printf("  At 50 ms Network Delay:\n");
    printf("    Stop-and-Wait Goodput : %6.2f KB/s\n", sw_50ms);
    printf("    Go-Back-N (N=8)       : %6.2f KB/s (%.1fx faster than SW)\n\n",
           gbn_50ms, (sw_50ms > 0) ? (gbn_50ms / sw_50ms) : 1.0);

    int throughput_higher = (gbn_50ms > sw_50ms);
    printf("  >>> %-60s [%s]\n",
           "Go-Back-N achieves higher throughput than SW at 50ms delay",
           throughput_higher ? "PASS" : "FAIL");

    assert(throughput_higher);
    printf("===============================================================\n");
    printf(">>> ALL RAUSHAN DAY 3 DELAY BENCHMARK TESTS PASSED 100%%! <<<\n");
    printf("===============================================================\n\n");

    return 0;
}
