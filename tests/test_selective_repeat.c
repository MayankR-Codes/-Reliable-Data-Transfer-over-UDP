/*
 * test_selective_repeat.c
 * =======================
 * Author  : Raushan Kumar
 * Day     : 4 — Selective Repeat (SR) ARQ & Out-of-Order Buffer
 * Purpose : End-to-end tests for Selective Repeat ARQ protocol:
 *           - Lossless transfers across window sizes N=4, N=8, N=16.
 *           - Impaired transfer with 15% packet loss over emulator,
 *             verifying out-of-order buffering, gap flushing, and
 *             individual packet retransmission without window restarts.
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
#endif

#include "../include/protocols.h"
#include "../include/file_io.h"

#define SR_BASE_PORT     19400
#define SR_INPUT_FILE    "test_data/sr_input.bin"
#define SR_TEST_SIZE     (20 * 1024)   /* 20 KB = 20 packets */

static void banner(const char *label, int passed) {
    printf("  >>> %-58s [%s]\n", label, passed ? "PASS" : "FAIL");
}

typedef struct {
    int         port;
    const char *output_path;
    int         result;
} RecvArgs;

#ifdef _WIN32
static unsigned __stdcall sr_recv_thread(void *arg) {
    RecvArgs *a = (RecvArgs *)arg;
    a->result = sr_receiver(a->port, a->output_path);
    return 0;
}
#endif

static int run_sr_transfer_test(const char *in_file, const char *out_file,
                                int port, int window_size,
                                const char *expected_hash)
{
    RecvArgs r_args;
    r_args.port        = port;
    r_args.output_path = out_file;
    r_args.result      = -999;

#ifdef _WIN32
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, sr_recv_thread, &r_args, 0, NULL);
    assert(hThread != NULL);
    Sleep(300); /* Allow receiver to bind */
#endif

    int s_result = sr_sender(in_file, "127.0.0.1", port, window_size);

#ifdef _WIN32
    WaitForSingleObject(hThread, 15000);
    CloseHandle(hThread);
#endif

    char out_hash[65] = {0};
    compute_file_sha256(out_file, out_hash);
    long out_size = get_file_size(out_file);

    int transfer_ok = (s_result == 0 && r_args.result == 0);
    int hash_ok     = (strcmp(expected_hash, out_hash) == 0);
    int size_ok     = (out_size == SR_TEST_SIZE);

    printf("  Input  SHA-256 : %s\n", expected_hash);
    printf("  Output SHA-256 : %s\n", out_hash);

    banner("SR transfer completed (sender=OK, receiver=OK)", transfer_ok);
    banner("SR SHA-256 integrity (100% data integrity)", hash_ok);
    banner("SR output size matches exactly", size_ok);

    return (transfer_ok && hash_ok && size_ok);
}

/* ── Run SR transfer through emulator with simulated packet loss ──── */
static int run_sr_transfer_with_loss(const char *in_file, const char *out_file,
                                     int emu_port, int recv_port, int loss_percent,
                                     int window_size, const char *expected_hash)
{
    RecvArgs r_args;
    r_args.port        = recv_port;
    r_args.output_path = out_file;
    r_args.result      = -999;

#ifdef _WIN32
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, sr_recv_thread, &r_args, 0, NULL);
    assert(hThread != NULL);
    Sleep(300);

    /* Launch emulator proxy process */
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "emulator.exe --listen %d --recv-port %d --loss %d",
             emu_port, recv_port, loss_percent);
    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "[FATAL] Cannot start emulator for SR loss test.\n");
        return 0;
    }
    Sleep(400);
#endif

    printf("--- SR SENDER (N=%d over %d%% Loss) ---\n", window_size, loss_percent);
    int s_result = sr_sender(in_file, "127.0.0.1", emu_port, window_size);
    printf("--- END SR SENDER ---\n\n");

#ifdef _WIN32
    WaitForSingleObject(hThread, 25000);
    CloseHandle(hThread);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#endif

    char out_hash[65] = {0};
    compute_file_sha256(out_file, out_hash);
    long out_size = get_file_size(out_file);

    int transfer_ok = (s_result == 0 && r_args.result == 0);
    int hash_ok     = (strcmp(expected_hash, out_hash) == 0);
    int size_ok     = (out_size == SR_TEST_SIZE);

    printf("  Input  SHA-256 : %s\n", expected_hash);
    printf("  Output SHA-256 : %s\n", out_hash);

    char label[128];
    snprintf(label, sizeof(label), "SR %d%% loss recovery transfer completed (N=%d)",
             loss_percent, window_size);
    banner(label, transfer_ok);

    snprintf(label, sizeof(label), "SR %d%% loss SHA-256 data integrity (100%% match)",
             loss_percent);
    banner(label, hash_ok);

    snprintf(label, sizeof(label), "SR %d%% loss output size matches exactly (%ld bytes)",
             loss_percent, out_size);
    banner(label, size_ok);

    return (transfer_ok && hash_ok && size_ok);
}

int main(void) {
    printf("\n=======================================================\n");
    printf("  Selective Repeat (SR) ARQ End-to-End Tests\n");
    printf("=======================================================\n\n");

    /* TEST 1: Create test file and calculate hash */
    printf("[TEST 1] Creating test input file (%d bytes)\n", SR_TEST_SIZE);
    FILE *fp = fopen(SR_INPUT_FILE, "wb");
    assert(fp != NULL);
    for (int i = 0; i < SR_TEST_SIZE; i++) {
        fputc((i * 13 + 7) & 0xFF, fp);
    }
    fclose(fp);
    banner("Test input file created", 1);

    char base_hash[65] = {0};
    assert(compute_file_sha256(SR_INPUT_FILE, base_hash) == 0);
    printf("  Input SHA-256 : %s\n", base_hash);
    banner("Baseline SHA-256 computed", 1);
    printf("\n");

    /* TEST 2: SR Transfer N=4 */
    printf("[TEST 2] SR transfer — window N=4\n\n");
    int ok_n4 = run_sr_transfer_test(SR_INPUT_FILE, "test_data/sr_out_n4.bin",
                                     SR_BASE_PORT, 4, base_hash);
    assert(ok_n4);
    printf("\n");

    /* TEST 3: SR Transfer N=8 */
    printf("[TEST 3] SR transfer — window N=8\n\n");
    int ok_n8 = run_sr_transfer_test(SR_INPUT_FILE, "test_data/sr_out_n8.bin",
                                     SR_BASE_PORT + 1, 8, base_hash);
    assert(ok_n8);
    printf("\n");

    /* TEST 4: SR Transfer N=16 */
    printf("[TEST 4] SR transfer — window N=16\n\n");
    int ok_n16 = run_sr_transfer_test(SR_INPUT_FILE, "test_data/sr_out_n16.bin",
                                      SR_BASE_PORT + 2, 16, base_hash);
    assert(ok_n16);
    printf("\n");

    /* TEST 5: Raushan Day 4: SR Transfer over 15% Packet Loss */
    printf("[TEST 5] Raushan Day 4: SR 15%% packet loss recovery (window N=8)\n\n");
    int ok_loss = run_sr_transfer_with_loss(SR_INPUT_FILE, "test_data/sr_out_loss15.bin",
                                            19450, 19451, 15, 8, base_hash);
    assert(ok_loss);
    printf("\n");

    printf("=======================================================\n");
    printf("  ALL SELECTIVE REPEAT TESTS PASSED 100%%!\n");
    printf("=======================================================\n\n");

    return 0;
}
