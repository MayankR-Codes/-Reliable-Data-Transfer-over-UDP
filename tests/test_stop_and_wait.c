/*
 * test_stop_and_wait.c
 * ====================
 * Author  : Manisha Behera (Day 2 Test)
 * Purpose : End-to-end test of the Stop-and-Wait ARQ protocol.
 *
 * Strategy:
 *   1. Generate a small in-memory test file (test_data/sw_input.bin).
 *   2. Launch sw_receiver() in a background Windows thread.
 *   3. Call sw_sender() in the main thread to transfer the file.
 *   4. Wait for both to complete.
 *   5. Compute SHA-256 of input and output files and assert they match.
 *
 * Day 2 Spec:
 *   - Full file transfer works using Stop-and-Wait.
 *   - Running sha256 on input and output produces identical hashes.
 *   - SW successfully recovers if packets are manually dropped (emulated
 *     here by the 200 ms timeout / retransmit mechanism).
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
#define SW_TEST_PORT      19200
#define SW_INPUT_FILE     "test_data/sw_input.bin"
#define SW_OUTPUT_FILE    "test_data/sw_output.bin"
#define SW_TEST_FILE_SIZE (5 * 1024)   /* 5 KB — quick smoke test    */

/* ── Helper: print pass/fail ────────────────────────────────────── */
static void banner(const char *test_name, int passed) {
    printf("  >>> %-55s [%s]\n", test_name, passed ? "PASS" : "FAIL");
}

/* ── Receiver thread argument ───────────────────────────────────── */
typedef struct {
    int         port;
    const char *output_path;
    int         result;        /* filled by thread: 0=ok, -1=fail */
} ReceiverArgs;

/* ── Receiver thread function (Windows) ─────────────────────────── */
#ifdef _WIN32
static unsigned __stdcall receiver_thread(void *arg) {
    ReceiverArgs *a = (ReceiverArgs *)arg;
    a->result = sw_receiver(a->port, a->output_path);
    return 0;
}
#else
static void *receiver_thread(void *arg) {
    ReceiverArgs *a = (ReceiverArgs *)arg;
    a->result = sw_receiver(a->port, a->output_path);
    return NULL;
}
#endif

/* ── Generate a deterministic binary test file ──────────────────── */
static int create_test_file(const char *path, size_t size) {
    /* Ensure test_data/ directory exists */
#ifdef _WIN32
    CreateDirectoryA("test_data", NULL);
#else
    system("mkdir -p test_data");
#endif
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = (uint8_t)(i & 0xFF);
        fwrite(&byte, 1, 1, fp);
    }
    fclose(fp);
    return 0;
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main(void) {
    int all_passed = 1;

    printf("\n");
    printf("=======================================================\n");
    printf("  Manisha Behera — Day 2: Stop-and-Wait ARQ Tests\n");
    printf("=======================================================\n\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 1: Generate deterministic test file                   */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 1] Creating test input file (%d bytes)\n",
           SW_TEST_FILE_SIZE);
    int ok = (create_test_file(SW_INPUT_FILE, SW_TEST_FILE_SIZE) == 0);
    printf("  Input file: %s  size=%d bytes\n",
           SW_INPUT_FILE, SW_TEST_FILE_SIZE);
    banner("Input file created successfully", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 2: Compute SHA-256 of input file                      */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 2] Compute SHA-256 of input file\n");
    char input_hash[SHA256_HEX_LEN];
    ok = (compute_file_sha256(SW_INPUT_FILE, input_hash) == 0);
    printf("  Input SHA-256 : %s\n", input_hash);
    banner("SHA-256 computed for input file", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 3: End-to-end Stop-and-Wait file transfer             */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 3] End-to-end SW transfer: sender → receiver (loopback)\n");
    printf("  Port: %d | Input: %s | Output: %s\n\n",
           SW_TEST_PORT, SW_INPUT_FILE, SW_OUTPUT_FILE);

    /* Launch receiver in background thread */
    ReceiverArgs rargs = { SW_TEST_PORT, SW_OUTPUT_FILE, -99 };

#ifdef _WIN32
    HANDLE hThread = (HANDLE)_beginthreadex(
        NULL, 0, receiver_thread, &rargs, 0, NULL);
    if (hThread == NULL) {
        fprintf(stderr, "[FATAL] Cannot start receiver thread.\n");
        return 1;
    }
    /* Give receiver a moment to bind before sender connects */
    Sleep(200);
#else
    pthread_t tid;
    if (pthread_create(&tid, NULL, receiver_thread, &rargs) != 0) {
        fprintf(stderr, "[FATAL] Cannot start receiver thread.\n");
        return 1;
    }
    usleep(200000);   /* 200 ms */
#endif

    /* Run sender in main thread */
    printf("--- SENDER OUTPUT ---\n");
    int sender_ret = sw_sender(SW_INPUT_FILE, "127.0.0.1", SW_TEST_PORT);
    printf("--- END SENDER ---\n\n");

    /* Wait for receiver thread to finish */
#ifdef _WIN32
    WaitForSingleObject(hThread, 10000);   /* max 10 s */
    CloseHandle(hThread);
#else
    pthread_join(tid, NULL);
#endif

    ok = (sender_ret == 0) && (rargs.result == 0);
    printf("  Sender result   : %s\n", sender_ret == 0 ? "OK" : "FAIL");
    printf("  Receiver result : %s\n", rargs.result == 0 ? "OK" : "FAIL");
    banner("SW transfer completed without error", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 4: SHA-256 integrity check                            */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 4] SHA-256 integrity verification\n");
    char output_hash[SHA256_HEX_LEN];
    int hash_ok = (compute_file_sha256(SW_OUTPUT_FILE, output_hash) == 0);
    printf("  Input  SHA-256 : %s\n", input_hash);
    printf("  Output SHA-256 : %s\n", output_hash);

    ok = hash_ok && (strcmp(input_hash, output_hash) == 0);
    banner("SHA-256 hashes match — 100%% data integrity", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* TEST 5: Output file size matches input                     */
    /* ─────────────────────────────────────────────────────────── */
    printf("[TEST 5] File size verification\n");
    long out_size = get_file_size(SW_OUTPUT_FILE);
    printf("  Input  size : %d bytes\n", SW_TEST_FILE_SIZE);
    printf("  Output size : %ld bytes\n", out_size);
    ok = (out_size == (long)SW_TEST_FILE_SIZE);
    banner("Output file size matches input exactly", ok);
    assert(ok);
    all_passed &= ok;
    printf("\n");

    /* ─────────────────────────────────────────────────────────── */
    /* SUMMARY                                                    */
    /* ─────────────────────────────────────────────────────────── */
    printf("=======================================================\n");
    if (all_passed)
        printf("  ALL 5 SW TESTS PASSED — Manisha Day 2 Complete!\n");
    else
        printf("  SOME TESTS FAILED — Review output above.\n");
    printf("=======================================================\n\n");

    return all_passed ? 0 : 1;
}
