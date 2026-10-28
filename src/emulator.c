#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "socket_utils.h"

/* =========================================================
   Platform-portable millisecond clock
   ========================================================= */
#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/time.h>
#endif

static long long emu_now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
#endif
}

/* =========================================================
   Asynchronous Packet Delay Queue
   Simulates true propagation delay for pipelined traffic
   ========================================================= */
#define QUEUE_MAX 1024

typedef struct {
    Packet             pkt;
    struct sockaddr_in dest;
    long long          send_at_ms;
    int                is_ack;
    int                is_fin;
} DelayedPkt;

static DelayedPkt pkt_queue[QUEUE_MAX];
static int q_head  = 0;
static int q_tail  = 0;
static int q_count = 0;

static void q_push(const Packet *p, const struct sockaddr_in *dest,
                   long long send_at, int is_ack, int is_fin) {
    if (q_count >= QUEUE_MAX) return; /* Drop if queue full */
    pkt_queue[q_tail].pkt        = *p;
    pkt_queue[q_tail].dest       = *dest;
    pkt_queue[q_tail].send_at_ms = send_at;
    pkt_queue[q_tail].is_ack     = is_ack;
    pkt_queue[q_tail].is_fin     = is_fin;
    q_tail = (q_tail + 1) % QUEUE_MAX;
    q_count++;
}

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

    long long last_data_send_at = 0;
    long long last_ack_send_at  = 0;

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
        /* 1. Flush any queued packets whose scheduled departure time has arrived */
        while (q_count > 0 && emu_now_ms() >= pkt_queue[q_head].send_at_ms) {
            DelayedPkt *dp = &pkt_queue[q_head];
            if (dp->is_ack) {
                total_ack_forwarded++;
                printf("[EMULATOR -> SENDER]   FWD ACK: Type=%d, Ack=%u\n",
                       dp->pkt.type, dp->pkt.ack_num);
                send_packet(emu_sock, &dp->pkt, &dp->dest);
            } else {
                total_data_forwarded++;
                if (dp->is_fin) {
                    printf("[EMULATOR -> RECEIVER] FWD FIN: Seq=%u\n", dp->pkt.seq_num);
                    send_packet(emu_sock, &dp->pkt, &dp->dest);

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
                } else {
                    printf("[EMULATOR -> RECEIVER] FWD DATA: Type=%d, Seq=%u, Len=%u\n",
                           dp->pkt.type, dp->pkt.seq_num, dp->pkt.length);
                    send_packet(emu_sock, &dp->pkt, &dp->dest);
                }
            }
            q_head = (q_head + 1) % QUEUE_MAX;
            q_count--;
        }

        /* 2. Wait for incoming packet or until next queued packet is ready to send */
        struct timeval tv;
        struct timeval *tvp = &tv;
        if (q_count > 0) {
            long long wait_ms = pkt_queue[q_head].send_at_ms - emu_now_ms();
            if (wait_ms < 0) wait_ms = 0;
            tv.tv_sec  = (long)(wait_ms / 1000);
            tv.tv_usec = (long)((wait_ms % 1000) * 1000);
        } else {
            /* No packets in flight; poll socket up to 50ms */
            tv.tv_sec  = 0;
            tv.tv_usec = 50000;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(emu_sock, &rfds);
        int sel = select((int)(emu_sock + 1), &rfds, NULL, NULL, tvp);
        if (sel <= 0) {
            /* Timeout or no packet ready; re-check queue in next iteration */
            continue;
        }

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

            /* Calculate departure time */
            int actual = delay_ms;
            if (delay_jitter > 0) {
                int var = (rand() % (2 * delay_jitter + 1)) - delay_jitter;
                actual += var;
                if (actual < 0) actual = 0;
            }

            long long now = emu_now_ms();
            long long send_at = now + actual;
            if (send_at < last_ack_send_at) send_at = last_ack_send_at;
            last_ack_send_at = send_at;

            if (send_at <= now) {
                total_ack_forwarded++;
                printf("[EMULATOR -> SENDER]   FWD ACK: Type=%d, Ack=%u\n",
                       pkt.type, pkt.ack_num);
                send_packet(emu_sock, &pkt, &sender_addr);
            } else {
                q_push(&pkt, &sender_addr, send_at, 1, 0);
            }

        } else {
            /* ---- Traffic from Sender -> forward DATA to Receiver ---- */
            sender_addr  = incoming_addr;
            sender_known = 1;
            total_data_received++;

            int is_fin = (pkt.type == PACKET_FIN);

            if (!is_fin) {
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
            }

            /* Calculate departure time */
            int actual = delay_ms;
            if (delay_jitter > 0) {
                int var = (rand() % (2 * delay_jitter + 1)) - delay_jitter;
                actual += var;
                if (actual < 0) actual = 0;
            }

            long long now = emu_now_ms();
            long long send_at = now + actual;
            if (send_at < last_data_send_at) send_at = last_data_send_at;
            last_data_send_at = send_at;

            if (send_at <= now) {
                total_data_forwarded++;
                if (is_fin) {
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
                } else {
                    printf("[EMULATOR -> RECEIVER] FWD DATA: Type=%d, Seq=%u, Len=%u\n",
                           pkt.type, pkt.seq_num, pkt.length);
                    send_packet(emu_sock, &pkt, &receiver_addr);
                }
            } else {
                printf("[EMULATOR DELAY] Queued Seq=%u for departure in %d ms\n",
                       pkt.seq_num, actual);
                q_push(&pkt, &receiver_addr, send_at, 0, is_fin);
            }
        }
    }

    CLOSE_SOCKET(emu_sock);
    cleanup_network();
    return 0;
}
