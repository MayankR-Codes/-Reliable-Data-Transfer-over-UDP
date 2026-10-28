/*
 * sender.c
 * ========
 * Author  : Mayank Rana & Team
 * Day     : 5 — Unified CLI Binaries
 *
 * Unified CLI sender binary that transmits files over UDP using any of
 * the three ARQ reliability protocols (SW, GBN, SR).
 *
 * Usage:
 *   ./sender --file <path> --ip <dest_ip> --port <port> --protocol <sw|gbn|sr> [--window <N>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "protocols.h"
#include "socket_utils.h"
#include "file_io.h"

#ifdef _WIN32
  #include <windows.h>
  static long long get_time_ms(void) {
      return (long long)GetTickCount();
  }
#else
  #include <sys/time.h>
  static long long get_time_ms(void) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
  }
#endif

static void print_usage(const char *prog) {
    printf("Reliable UDP File Sender\n");
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  --file <path>        Path to input file to send        (REQUIRED)\n");
    printf("  --ip <ip>            Destination IP (receiver/proxy)   (default: 127.0.0.1)\n");
    printf("  --port <port>        Destination UDP port              (default: 8000)\n");
    printf("  --protocol <proto>   ARQ protocol: sw | gbn | sr       (default: gbn)\n");
    printf("  --window <N>         Window size for GBN / SR          (default: 8)\n");
    printf("  --help, -h           Display this help message\n");
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    char filepath[256] = "";
    char dest_ip[64]   = "127.0.0.1";
    int  dest_port     = 8000;
    char protocol[16]  = "gbn";
    int  window_size   = 8;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            strncpy(filepath, argv[++i], sizeof(filepath) - 1);
        } else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            strncpy(dest_ip, argv[++i], sizeof(dest_ip) - 1);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            dest_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--protocol") == 0 && i + 1 < argc) {
            strncpy(protocol, argv[++i], sizeof(protocol) - 1);
        } else if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
            window_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (strlen(filepath) == 0) {
        fprintf(stderr, "[ERROR] Missing required option: --file <path>\n\n");
        print_usage(argv[0]);
        return 1;
    }

    long file_size = get_file_size(filepath);
    if (file_size < 0) {
        fprintf(stderr, "[ERROR] Cannot access file: %s\n", filepath);
        return 1;
    }

    char sha_hex[65];
    if (compute_file_sha256(filepath, sha_hex) != 0) {
        fprintf(stderr, "[ERROR] Failed to compute SHA-256 for: %s\n", filepath);
        return 1;
    }

    printf("====================================================\n");
    printf("         RELIABLE UDP SENDER — DAY 5 CLI            \n");
    printf("====================================================\n");
    printf("Source File     : %s (%ld bytes)\n", filepath, file_size);
    printf("Source SHA-256  : %s\n", sha_hex);
    printf("Target Address  : %s:%d\n", dest_ip, dest_port);
    printf("Protocol        : %s\n", protocol);
    if (strcmp(protocol, "sw") != 0) {
        printf("Window Size (N) : %d\n", window_size);
    }
    printf("====================================================\n\n");

    long long t_start = get_time_ms();
    int status = -1;

    if (strcmp(protocol, "sw") == 0) {
        status = sw_sender(filepath, dest_ip, dest_port);
    } else if (strcmp(protocol, "gbn") == 0) {
        status = gbn_sender(filepath, dest_ip, dest_port, window_size);
    } else if (strcmp(protocol, "sr") == 0) {
        status = sr_sender(filepath, dest_ip, dest_port, window_size);
    } else {
        fprintf(stderr, "[ERROR] Unknown protocol '%s'. Valid options: sw, gbn, sr\n", protocol);
        return 1;
    }

    long long t_end = get_time_ms();
    double elapsed_sec = (double)(t_end - t_start) / 1000.0;
    if (elapsed_sec <= 0.0) elapsed_sec = 0.001;
    double goodput_kbps = ((double)file_size / 1024.0) / elapsed_sec;

    printf("====================================================\n");
    printf("               SENDER SUMMARY REPORT                \n");
    printf("====================================================\n");
    printf("Status          : %s\n", (status == 0) ? "SUCCESS (0)" : "FAILED (-1)");
    printf("Elapsed Time    : %.3f seconds\n", elapsed_sec);
    printf("Payload Sent    : %ld bytes\n", file_size);
    printf("Goodput         : %.2f KB/s\n", goodput_kbps);
    printf("SHA-256 Hash    : %s\n", sha_hex);
    printf("====================================================\n");

    return (status == 0) ? 0 : 1;
}
