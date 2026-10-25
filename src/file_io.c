#include "file_io.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/*                      Self-Contained SHA-256 Algorithm                      */
/* ========================================================================= */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} SHA256_CTX;

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z)     (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z)    (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)        (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x)        (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x)       (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x)       (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    for (; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        if ((ctx->count % 64) == 0)
            sha256_transform(ctx, ctx->buffer);
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    size_t i = ctx->count % 64;

    /* Pad with 0x80 */
    ctx->buffer[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->buffer[i++] = 0x00;
        sha256_transform(ctx, ctx->buffer);
        memset(ctx->buffer, 0, 56);
    } else {
        while (i < 56) ctx->buffer[i++] = 0x00;
    }

    /* Append length in bits */
    uint64_t total_bits = ctx->count * 8;
    for (int j = 7; j >= 0; j--) {
        ctx->buffer[56 + (7 - j)] = (uint8_t)(total_bits >> (j * 8));
    }
    sha256_transform(ctx, ctx->buffer);

    for (int j = 0; j < 4; ++j) {
        hash[j]      = (ctx->state[0] >> (24 - j * 8)) & 0x000000ff;
        hash[j + 4]  = (ctx->state[1] >> (24 - j * 8)) & 0x000000ff;
        hash[j + 8]  = (ctx->state[2] >> (24 - j * 8)) & 0x000000ff;
        hash[j + 12] = (ctx->state[3] >> (24 - j * 8)) & 0x000000ff;
        hash[j + 16] = (ctx->state[4] >> (24 - j * 8)) & 0x000000ff;
        hash[j + 20] = (ctx->state[5] >> (24 - j * 8)) & 0x000000ff;
        hash[j + 24] = (ctx->state[6] >> (24 - j * 8)) & 0x000000ff;
        hash[j + 28] = (ctx->state[7] >> (24 - j * 8)) & 0x000000ff;
    }
}

/* ========================================================================= */
/*                          File I/O Helper Functions                         */
/* ========================================================================= */

int file_exists(const char *filepath) {
    if (!filepath) return 0;
    FILE *fp = fopen(filepath, "rb");
    if (fp) {
        fclose(fp);
        return 1;
    }
    return 0;
}

long get_file_size(const char *filepath) {
    if (!filepath) return -1;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size;
}

int read_chunk(FILE *fp, char *buffer, size_t max_len) {
    if (!fp || !buffer || max_len == 0) return -1;
    size_t bytes_read = fread(buffer, 1, max_len, fp);
    if (bytes_read == 0) {
        if (ferror(fp)) return -1; /* Read error */
        return 0;                 /* EOF */
    }
    return (int)bytes_read;
}

int write_chunk(FILE *fp, const char *buffer, size_t len) {
    if (!fp || !buffer) return -1;
    if (len == 0) return 0;
    size_t bytes_written = fwrite(buffer, 1, len, fp);
    if (bytes_written != len) return -1;
    return (int)bytes_written;
}

int compute_file_sha256(const char *filepath, char *hash_out) {
    if (!filepath || !hash_out) return -1;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    SHA256_CTX ctx;
    sha256_init(&ctx);

    uint8_t buffer[4096];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        sha256_update(&ctx, buffer, bytes_read);
    }

    if (ferror(fp)) {
        fclose(fp);
        return -1;
    }

    fclose(fp);

    uint8_t hash[32];
    sha256_final(&ctx, hash);

    for (int i = 0; i < 32; i++) {
        sprintf(hash_out + (i * 2), "%02x", hash[i]);
    }
    hash_out[64] = '\0';
    return 0;
}

int compare_file_hashes(const char *file1, const char *file2) {
    char hash1[SHA256_HEX_LEN];
    char hash2[SHA256_HEX_LEN];

    if (compute_file_sha256(file1, hash1) != 0) return -1;
    if (compute_file_sha256(file2, hash2) != 0) return -1;

    return (strcmp(hash1, hash2) == 0) ? 1 : 0;
}
