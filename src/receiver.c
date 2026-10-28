/*
 * receiver.c
 * ==========
 * Author  : Mayank Rana & Team
 * Day     : 5 — Unified CLI Binaries
 *
 * Unified CLI receiver binary that listens on a UDP port, validates
 * packet checksums, handles incoming ARQ reliability protocols (SW, GBN, SR),
 * reconstructs the file, and performs SHA-256 integrity verification.
 *
 * Usage:
 *   ./receiver --port <port> --output <path> --protocol <sw|gbn|sr> [--window <N>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocols.h"
#include "socket_utils.h"
#include "file_io.h"

static void print_usage(const char *prog) {
    printf("Reliable UDP File Receiver\n");
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  --port <port>        Local UDP port to listen on        (default: 9001)\n");
    printf("  --output <path>      Output file path to save data     (default: output_received.bin)\n");
    printf("  --protocol <proto>   ARQ protocol: sw | gbn | sr       (default: gbn)\n");
    printf("  --window <N>         Receive window size for SR        (default: 8)\n");
    printf("  --help, -h           Display this help message\n");
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int  listen_port       = 9001;
    char output_path[256]  = "output_received.bin";
    char protocol[16]      = "gbn";
    int  window_size       = 8;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            listen_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            strncpy(output_path, argv[++i], sizeof(output_path) - 1);
        } else if (strcmp(argv[i], "--protocol") == 0 && i + 1 < argc) {
            strncpy(protocol, argv[++i], sizeof(protocol) - 1);
        } else if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
            window_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    printf("====================================================\n");
    printf("        RELIABLE UDP RECEIVER — DAY 5 CLI           \n");
    printf("====================================================\n");
    printf("Listening Port  : %d\n", listen_port);
    printf("Output File     : %s\n", output_path);
    printf("Protocol        : %s\n", protocol);
    if (strcmp(protocol, "sw") != 0) {
        printf("Window Size (N) : %d\n", window_size);
    }
    printf("====================================================\n\n");

    int status = -1;

    if (strcmp(protocol, "sw") == 0) {
        status = sw_receiver(listen_port, output_path);
    } else if (strcmp(protocol, "gbn") == 0) {
        status = gbn_receiver(listen_port, output_path);
    } else if (strcmp(protocol, "sr") == 0) {
        status = sr_receiver(listen_port, output_path);
    } else {
        fprintf(stderr, "[ERROR] Unknown protocol '%s'. Valid options: sw, gbn, sr\n", protocol);
        return 1;
    }

    if (status != 0) {
        fprintf(stderr, "[ERROR] Receiver failed during transfer.\n");
        return 1;
    }

    long file_size = get_file_size(output_path);
    char sha_hex[65];
    if (compute_file_sha256(output_path, sha_hex) != 0) {
        fprintf(stderr, "[ERROR] Could not compute hash of received file.\n");
        return 1;
    }

    printf("====================================================\n");
    printf("              RECEIVER SUMMARY REPORT               \n");
    printf("====================================================\n");
    printf("Status          : SUCCESS (0)\n");
    printf("Output File     : %s\n", output_path);
    printf("File Size       : %ld bytes\n", file_size);
    printf("SHA-256 Hash    : %s\n", sha_hex);
    printf("====================================================\n");

    return 0;
}
