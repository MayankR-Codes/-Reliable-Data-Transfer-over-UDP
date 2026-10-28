#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "socket_utils.h"

/* =========================================================
   Platform-portable millisecond sleep
   ========================================================= */
#ifdef _WIN32
    #include <windows.h>
    static void sleep_ms(int ms) { if (ms > 0) Sleep((DWORD)ms); }
#else
    #include <unistd.h>
    static void sleep_ms(int ms) { if (ms > 0) usleep((useconds_t)(ms * 1000)); }
#endif

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int listen_port          = 8000;
    int receiver_port        = 9001;
    char receiver_ip[64]     = "127.0.0.1";
    double loss_rate         = 0.0;  /* 0.0 – 1.0, e.g. 0.10 = 10%  */
    double loss_ack_rate     = 0.0;  /* optional independent ACK loss */
    double corrupt_rate      = 0.0;  /* DATA packet payload corruption rate */
    double corrupt_ack_rate  = 0.0;  /* optional ACK corruption rate */
    int    delay_ms          = 0;    /* one-way delay in milliseconds  */
    int    delay_jitter      = 0;    /* ±jitter added to delay_ms      */
    unsigned int random_seed = (unsigned int)time(NULL);
    int seed_provided = 0;

    /* ---- Parse command-line arguments ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--recv-port") == 0 && i + 1 < argc) {
            receiver_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--recv-ip") == 0 && i + 1 < argc) {
            strncpy(receiver_ip, argv[++i], sizeof(receiver_ip) - 1);
        } else if (strcmp(argv[i], "--loss") == 0 && i + 1 < argc) {
            double val = atof(argv[++i]);
            loss_rate = (val > 1.0) ? (val / 100.0) : val;
            if (loss_rate < 0.0) loss_rate = 0.0;
            if (loss_rate > 1.0) loss_rate = 1.0;
        } else if (strcmp(argv[i], "--loss-ack") == 0 && i + 1 < argc) {
            double val = atof(argv[++i]);
            loss_ack_rate = (val > 1.0) ? (val / 100.0) : val;
            if (loss_ack_rate < 0.0) loss_ack_rate = 0.0;
            if (loss_ack_rate > 1.0) loss_ack_rate = 1.0;
        } else if (strcmp(argv[i], "--corrupt") == 0 && i + 1 < argc) {
            double val = atof(argv[++i]);
            corrupt_rate = (val > 1.0) ? (val / 100.0) : val;
            if (corrupt_rate < 0.0) corrupt_rate = 0.0;
            if (corrupt_rate > 1.0) corrupt_rate = 1.0;
        } else if (strcmp(argv[i], "--corrupt-ack") == 0 && i + 1 < argc) {
            double val = atof(argv[++i]);
            corrupt_ack_rate = (val > 1.0) ? (val / 100.0) : val;
            if (corrupt_ack_rate < 0.0) corrupt_ack_rate = 0.0;
            if (corrupt_ack_rate > 1.0) corrupt_ack_rate = 1.0;
        } else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay_ms = atoi(argv[++i]);
            if (delay_ms < 0) delay_ms = 0;
        } else if (strcmp(argv[i], "--jitter") == 0 && i + 1 < argc) {
            delay_jitter = atoi(argv[++i]);
            if (delay_jitter < 0) delay_jitter = 0;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            random_seed = (unsigned int)atoi(argv[++i]);
            seed_provided = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  --listen <port>      Emulator listen port        (default: 8000)\n");
            printf("  --recv-ip <ip>       Receiver IP address         (default: 127.0.0.1)\n");
            printf("  --recv-port <port>   Receiver port               (default: 9001)\n");
            printf("  --loss <percent>     DATA drop rate, e.g. 10 or 0.10 for 10%%\n");
            printf("  --loss-ack <percent> ACK drop rate               (default: 0%%)\n");
            printf("  --corrupt <percent>  DATA corruption rate        (default: 0%%)\n");
            printf("  --corrupt-ack <pct>  ACK corruption rate         (default: 0%%)\n");
            printf("  --delay <ms>         Artificial one-way delay in ms (default: 0)\n");
            printf("  --jitter <ms>        Random delay variation ±ms  (default: 0)\n");
            printf("  --seed <number>      RNG seed for reproducible experiments\n");
            printf("  --help, -h           Show this help\n");
            return 0;
        }
    }

    srand(random_seed);

    /* ---- Banner ---- */
    printf("====================================================\n");
    printf("   Channel Emulator (Proxy) — Day 4: Loss/Delay/Corrupt\n");
    printf("====================================================\n");
    printf("Listening on Port        : %d\n", listen_port);
    printf("Forwarding target (Recv) : %s:%d\n", receiver_ip, receiver_port);
    printf("DATA Packet Loss Rate    : %.1f%%\n", loss_rate * 100.0);
    if (loss_ack_rate > 0.0)
        printf("ACK Packet Loss Rate     : %.1f%%\n", loss_ack_rate * 100.0);
    if (corrupt_rate > 0.0)
        printf("DATA Packet Corrupt Rate : %.1f%%\n", corrupt_rate * 100.0);
    if (corrupt_ack_rate > 0.0)
        printf("ACK Packet Corrupt Rate  : %.1f%%\n", corrupt_ack_rate * 100.0);
    printf("Network Delay            : %d ms", delay_ms);
    if (delay_jitter > 0) printf(" ±%d ms jitter", delay_jitter);
    printf("\n");
    printf("Random Seed              : %u%s\n",
           random_seed, seed_provided ? " (manual)" : " (auto)");
    printf("====================================================\n\n");

    if (init_network() != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize network subsystem.\n");
        return 1;
    }

    SOCKET emu_sock = create_udp_socket();
    if (emu_sock == INVALID_SOCKET) { cleanup_network(); return 1; }

    if (bind_udp_socket(emu_sock, listen_port) != 0) {
        fprintf(stderr, "[ERROR] Could not bind emulator to port %d\n", listen_port);
        CLOSE_SOCKET(emu_sock);
        cleanup_network();
        return 1;
    }

    struct sockaddr_in receiver_addr;
    setup_sockaddr(&receiver_addr, receiver_ip, receiver_port);

    struct sockaddr_in sender_addr;
    int sender_known = 0;

    Packet pkt;
    struct sockaddr_in incoming_addr;

    /* ---- Metrics ---- */
    unsigned long total_data_received  = 0;
    unsigned long total_data_forwarded = 0;
    unsigned long total_data_dropped   = 0;
    unsigned long total_data_corrupted = 0;
    unsigned long total_ack_received   = 0;
    unsigned long total_ack_forwarded  = 0;
    unsigned long total_ack_dropped    = 0;
    unsigned long total_ack_corrupted  = 0;

    printf("[EMULATOR] Proxy active. Waiting for traffic...\n\n");

    while (1) {
        int bytes = recv_packet(emu_sock, &pkt, &incoming_addr);
        if (bytes <= 0) continue;

        /* Verify checksum on incoming packet */
        if (!verify_packet_checksum(&pkt)) {
            printf("[EMULATOR WARNING] Corrupted packet received! "
                   "Type=%d Checksum=0x%08X\n", pkt.type, pkt.checksum);
        }

        /* ---- Traffic from Receiver -> forward ACK to Sender ---- */
        if (incoming_addr.sin_port        == receiver_addr.sin_port &&
            incoming_addr.sin_addr.s_addr == receiver_addr.sin_addr.s_addr) {

            total_ack_received++;

            if (!sender_known) {
                printf("[EMULATOR WARNING] ACK arrived before any DATA — "
                       "sender address unknown yet.\n");
                continue;
            }

            /* ACK drop */
            if (loss_ack_rate > 0.0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < loss_ack_rate) {
                    total_ack_dropped++;
                    printf("[EMULATOR DROP] >>> DROPPED ACK: Ack=%u "
                           "(total dropped: %lu)\n",
                           pkt.ack_num, total_ack_dropped);
                    continue;
                }
            }

            /* ACK corruption */
            if (corrupt_ack_rate > 0.0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < corrupt_ack_rate) {
                    total_ack_corrupted++;
                    /* Invert checksum so recipient rejects ACK */
                    pkt.checksum ^= 0xFFFFFFFF;
                    printf("[EMULATOR CORRUPT] >>> CORRUPTED ACK: Ack=%u "
                           "(total corrupted: %lu)\n",
                           pkt.ack_num, total_ack_corrupted);
                }
            }

            /* Apply delay before forwarding ACK */
            if (delay_ms > 0 || delay_jitter > 0) {
                int actual = delay_ms;
                if (delay_jitter > 0) {
                    int var = (rand() % (2 * delay_jitter + 1)) - delay_jitter;
                    actual += var;
                    if (actual < 0) actual = 0;
                }
                if (actual > 0) sleep_ms(actual);
            }

            total_ack_forwarded++;
            printf("[EMULATOR -> SENDER]   FWD ACK: Type=%d, Ack=%u\n",
                   pkt.type, pkt.ack_num);
            send_packet(emu_sock, &pkt, &sender_addr);

        } else {
            /* ---- Traffic from Sender -> forward DATA to Receiver ---- */
            sender_addr  = incoming_addr;
            sender_known = 1;
            total_data_received++;

            /* FIN always passes through uncorrupted so transfers terminate cleanly */
            if (pkt.type == PACKET_FIN) {
                printf("[EMULATOR -> RECEIVER] FWD FIN: Seq=%u\n", pkt.seq_num);
                send_packet(emu_sock, &pkt, &receiver_addr);

                printf("\n--- EMULATOR SESSION STATS ---\n");
                printf("DATA  Received: %lu  Forwarded: %lu  Dropped: %lu  Corrupted: %lu"
                       "  (%.1f%% loss, %.1f%% corrupt)\n",
                       total_data_received, total_data_forwarded,
                       total_data_dropped, total_data_corrupted,
                       total_data_received > 0
                           ? 100.0 * total_data_dropped / total_data_received
                           : 0.0,
                       total_data_received > 0
                           ? 100.0 * total_data_corrupted / total_data_received
                           : 0.0);
                printf("ACK   Received: %lu  Forwarded: %lu  Dropped: %lu  Corrupted: %lu\n",
                       total_ack_received, total_ack_forwarded,
                       total_ack_dropped, total_ack_corrupted);
                printf("Delay setting : %d ms", delay_ms);
                if (delay_jitter > 0) printf(" ±%d ms jitter", delay_jitter);
                printf("\n------------------------------\n\n");
                continue;
            }

            /* DATA drop */
            if (loss_rate > 0.0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < loss_rate) {
                    total_data_dropped++;
                    printf("[EMULATOR DROP] >>> DROPPED DATA: Seq=%u Len=%u "
                           "(dropped %lu / %.1f%%)\n",
                           pkt.seq_num, pkt.length, total_data_dropped,
                           100.0 * total_data_dropped / total_data_received);
                    continue;
                }
            }

            /* DATA bit-corruption: flip 1 random byte in payload */
            if (corrupt_rate > 0.0 && pkt.length > 0) {
                double r = (double)rand() / (double)RAND_MAX;
                if (r < corrupt_rate) {
                    total_data_corrupted++;
                    int byte_idx = rand() % pkt.length;
                    pkt.payload[byte_idx] ^= 0xFF;  /* Invert all 8 bits in payload byte */
                    printf("[EMULATOR CORRUPT] >>> CORRUPTED DATA: Seq=%u Len=%u "
                           "(flipped payload byte %d, total corrupted: %lu)\n",
                           pkt.seq_num, pkt.length, byte_idx, total_data_corrupted);
                }
            }

            /* Apply delay before forwarding DATA */
            if (delay_ms > 0 || delay_jitter > 0) {
                int actual = delay_ms;
                if (delay_jitter > 0) {
                    int var = (rand() % (2 * delay_jitter + 1)) - delay_jitter;
                    actual += var;
                    if (actual < 0) actual = 0;
                }
                if (actual > 0) {
                    printf("[EMULATOR DELAY] Delaying Seq=%u by %d ms\n",
                           pkt.seq_num, actual);
                    sleep_ms(actual);
                }
            }

            total_data_forwarded++;
            printf("[EMULATOR -> RECEIVER] FWD DATA: Type=%d, Seq=%u, Len=%u\n",
                   pkt.type, pkt.seq_num, pkt.length);
            send_packet(emu_sock, &pkt, &receiver_addr);
        }
    }

    CLOSE_SOCKET(emu_sock);
    cleanup_network();
    return 0;
}
