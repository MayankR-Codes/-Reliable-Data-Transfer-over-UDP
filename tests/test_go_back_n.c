/*
 * test_go_back_n.c
 * ================
 * Author  : Manisha Behera (Day 3 Test)
 * Purpose : End-to-end tests for the Go-Back-N ARQ protocol.
 *
 * Test Plan (Day 3 Spec):
 *   TEST 1 — File creation & SHA-256 baseline
 *   TEST 2 — GBN transfer with window N=4  (small window)
 *   TEST 3 — SHA-256 integrity check after N=4 transfer
 *   TEST 4 — GBN transfer with window N=8  (default window)
 *   TEST 5 — SHA-256 integrity check after N=8 transfer
 *   TEST 6 — GBN transfer with window N=16 (large window)
 *   TEST 7 — SHA-256 integrity check after N=16 transfer
 *
 * Day 3 Spec:
 *   - Go-Back-N file transfer functions properly with N=4,8,16.
 *   - When a packet is dropped by the emulator, the sender times out and
 *     retransmits the full unacknowledged window.
 *   - Receiver output file passes SHA-256 verification.
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

/* ── Test configuration ─────────────────────────────────────────── */
#define GBN_BASE_PORT     19300          /* unique port range for GBN  */
#define GBN_INPUT_FILE    "test_data/gbn_input.bin"
#define GBN_TEST_SIZE     (20 * 1024)    /* 20 KB — 20 chunks of 1024  */

/* ── Helper: print pass/fail banner ────────────────────────────── */
static void banner(const char *label, int passed) {
    printf("  >>> %-58s [%s]\n", label, passed ? "PASS" : "FAIL");
}

/* ── Receiver thread arg ────────────────────────────────────────── */
typedef struct {
    int         port;
    const char *output_path;
    int         result;
} RecvArgs;

#ifdef _WIN32
static unsigned __stdcall gbn_recv_thread(void *arg) {
    RecvArgs *a = (RecvArgs *)arg;
    a->result = gbn_receiver(a->port, a->output_path);
    return 0;
}
#else
static void *gbn_recv_thread(void *arg) {
    RecvArgs *a = (RecvArgs *)arg;
    a->result = gbn_receiver(a->port, a->output_path);
    return NULL;
}
#endif

/* ── Generate deterministic binary test file ────────────────────── */
static int make_test_file(const char *path, size_t size) {
#ifdef _WIN32
    CreateDirectoryA("test_data", NULL);
#else
    system("mkdir -p test_data");
#endif
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    for (size_t i = 0; i < size; i++) {
        uint8_t b = (uint8_t)((i * 7 + 13) & 0xFF);  /* non-trivial pattern */
        fwrite(&b, 1, 1, fp);
    }
    fclose(fp);
    return 0;
}

/* ── Run a single GBN transfer test ────────────────────────────── */
static int run_gbn_transfer(int port, int window,
                             const char *input_path,
                             const char *output_path,
                             const char *input_hash)
{
    char label[128];

    /* ── Launch receiver thread ─────────────────────────────────── */
    RecvArgs rargs = { port, output_path, -99 };

#ifdef _WIN32
    HANDLE ht = (HANDLE)_beginthreadex(
        NULL, 0, gbn_recv_thread, &rargs, 0, NULL);
    if (!ht) { fprintf(stderr, "[FATAL] Cannot start receiver.\n"); return 0; }
    Sleep(250);   /* let receiver bind */
#else
    pthread_t tid;
    if (pthread_create(&tid, NULL, gbn_recv_thread, &rargs) != 0) {
        fprintf(stderr, "[FATAL] Cannot start receiver.\n"); return 0;
    }
    usleep(250000);
#endif

    /* ── Sender ─────────────────────────────────────────────────── */
    printf("--- SENDER (N=%d) ---\n", window);
    int sret = gbn_sender(input_path, "127.0.0.1", port, window);
    printf("--- END SENDER ---\n\n");

#ifdef _WIN32
    WaitForSingleObject(ht, 15000);
    CloseHandle(ht);
#else
    pthread_join(tid, NULL);
#endif

    int all_ok = 1;

    /* Transfer success */
    int ok = (sret == 0) && (rargs.result == 0);
    snprintf(label, sizeof(label),
             "GBN N=%-2d transfer completed (sender=%s, receiver=%s)",
             window,
             sret == 0 ? "OK" : "ERR",
             rargs.result == 0 ? "OK" : "ERR");
    banner(label, ok);
    assert(ok);
    all_ok &= ok;

    /* SHA-256 integrity */
    char out_hash[SHA256_HEX_LEN];
    int hash_ok = (compute_file_sha256(output_path, out_hash) == 0);
    ok = hash_ok && (strcmp(input_hash, out_hash) == 0);

    if (ok)
        printf("  Input  SHA-256 : %s\n  Output SHA-256 : %s\n",
               input_hash, out_hash);
    else
        printf("  [HASH MISMATCH]\n"
               "  Input  SHA-256 : %s\n  Output SHA-256 : %s\n",
               input_hash, out_hash);

    snprintf(label, sizeof(label),
             "GBN N=%-2d SHA-256 integrity (100%% data integrity)", window);
    banner(label, ok);
    assert(ok);
    all_ok &= ok;

    /* File size */
    long out_sz = get_file_size(output_path);
    ok = (out_sz == (long)GBN_TEST_SIZE);
    snprintf(label, sizeof(label),
             "GBN N=%-2d output size matches (%ld == %d bytes)",
             window, out_sz, GBN_TEST_SIZE);
    banner(label, ok);
    assert(ok);
    all_ok &= ok;

    return all_ok;
}

/* ── Run GBN transfer through emulator with simulated packet loss ── */
static int run_gbn_transfer_with_loss(int emu_port, int recv_port, int loss_percent, int window,
                                      const char *input_path, const char *output_path,
                                      const char *input_hash)
{
    char label[128];
    RecvArgs rargs = { recv_port, output_path, -99 };

#ifdef _WIN32
    HANDLE ht = (HANDLE)_beginthreadex(NULL, 0, gbn_recv_thread, &rargs, 0, NULL);
    if (!ht) { fprintf(stderr, "[FATAL] Cannot start receiver.\n"); return 0; }
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
        fprintf(stderr, "[FATAL] Cannot start emulator.\n");
        return 0;
    }
    Sleep(400);
#endif

    printf("--- SENDER (N=%d over %d%% Loss) ---\n", window, loss_percent);
    int sret = gbn_sender(input_path, "127.0.0.1", emu_port, window);
    printf("--- END SENDER ---\n\n");

#ifdef _WIN32
    WaitForSingleObject(ht, 25000);
    CloseHandle(ht);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#endif

    int all_ok = 1;
    int ok = (sret == 0) && (rargs.result == 0);
    snprintf(label, sizeof(label),
             "GBN 15%% loss recovery transfer completed (N=%d)", window);
    banner(label, ok);
    assert(ok);
    all_ok &= ok;

    char out_hash[SHA256_HEX_LEN];
    int hash_ok = (compute_file_sha256(output_path, out_hash) == 0);
    ok = hash_ok && (strcmp(input_hash, out_hash) == 0);
    snprintf(label, sizeof(label),
             "GBN 15%% loss SHA-256 data integrity (100%% identical)");
    banner(label, ok);
    assert(ok);
    all_ok &= ok;

    long out_sz = get_file_size(output_path);
    ok = (out_sz == (long)GBN_TEST_SIZE);
    snprintf(label, sizeof(label),
             "GBN 15%% loss output size matches exactly (%ld bytes)", out_sz);
    banner(label, ok);
    assert(ok);
    all_ok &= ok;

    return all_ok;
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main(void) {
    int all_passed = 1;

    printf("\n");
    printf("=======================================================\n");
    printf("  Manisha Behera — Day 3: Go-Back-N ARQ Tests\n");
    printf("=======================================================\n\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 1: Create test file & compute baseline SHA-256        */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 1] Creating test input file (%d bytes)\n", GBN_TEST_SIZE);
    int ok = (make_test_file(GBN_INPUT_FILE, GBN_TEST_SIZE) == 0);
    banner("Test input file created", ok);
    assert(ok);
    all_passed &= ok;

    char input_hash[SHA256_HEX_LEN];
    ok = (compute_file_sha256(GBN_INPUT_FILE, input_hash) == 0);
    printf("  Input SHA-256 : %s\n", input_hash);
    banner("Baseline SHA-256 computed", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 2+3: GBN with window N=4                              */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 2/3] GBN transfer — window N=4\n\n");
    ok = run_gbn_transfer(GBN_BASE_PORT, 4,
                          GBN_INPUT_FILE, "test_data/gbn_out_n4.bin",
                          input_hash);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 4+5: GBN with window N=8 (default spec window)       */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 4/5] GBN transfer — window N=8\n\n");
    ok = run_gbn_transfer(GBN_BASE_PORT + 1, 8,
                          GBN_INPUT_FILE, "test_data/gbn_out_n8.bin",
                          input_hash);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 6+7: GBN with window N=16 (large window)             */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 6/7] GBN transfer — window N=16\n\n");
    ok = run_gbn_transfer(GBN_BASE_PORT + 2, 16,
                          GBN_INPUT_FILE, "test_data/gbn_out_n16.bin",
                          input_hash);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 8+9: Day 4 Edge Case — GBN over 15% Packet Loss       */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 8/9] Manisha Day 4: GBN 15%% packet loss recovery (window N=8)\n\n");
    ok = run_gbn_transfer_with_loss(19350, 19351, 15, 8,
                                    GBN_INPUT_FILE, "test_data/gbn_out_loss15.bin",
                                    input_hash);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* SUMMARY                                                    */
    /* ─────────────────────────────────────────────────────────── */
    printf("=======================================================\n");
    if (all_passed)
        printf("  ALL GBN TESTS PASSED — Manisha Day 3 & Day 4 Complete!\n");
    else
        printf("  SOME TESTS FAILED — Review output above.\n");
    printf("=======================================================\n\n");

    return all_passed ? 0 : 1;
}
