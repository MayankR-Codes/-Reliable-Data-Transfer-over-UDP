#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(__has_include)
  #if __has_include("../include/file_io.h")
    #include "../include/file_io.h"
  #elif __has_include("file_io.h")
    #include "file_io.h"
  #endif
#else
  #include "file_io.h"
#endif

int main(void) {
    printf("====================================================\n");
    printf("   Running Dimple's Day 2 File I/O & SHA-256 Tests  \n");
    printf("====================================================\n\n");

    const char *src_path = "test_data_in.bin";
    const char *dst_path = "test_data_out.bin";

    /* 1. Generate a 12 KB sample binary file (larger than 1 packet) */
    size_t test_size = 12 * 1024 + 321; /* 12,609 bytes */
    FILE *fp_src = fopen(src_path, "wb");
    assert(fp_src != NULL);

    for (size_t i = 0; i < test_size; i++) {
        uint8_t byte = (uint8_t)((i * 37 + 13) & 0xFF);
        fputc(byte, fp_src);
    }
    fclose(fp_src);

    /* Test get_file_size */
    long measured_size = get_file_size(src_path);
    printf("[TEST 1] Source file created. Size: %ld bytes\n", measured_size);
    assert(measured_size == (long)test_size);
    printf(">>> TEST 1 PASSED: File size measured correctly!\n\n");

    /* 2. Compute SHA-256 hash of the source file */
    char src_hash[SHA256_HEX_LEN];
    assert(compute_file_sha256(src_path, src_hash) == 0);
    printf("[TEST 2] Source file SHA-256:\n         %s\n", src_hash);
    assert(strlen(src_hash) == 64);
    printf(">>> TEST 2 PASSED: SHA-256 hash generated!\n\n");

    /* 3. Read chunk by chunk and write to destination file */
    FILE *fp_in = fopen(src_path, "rb");
    FILE *fp_out = fopen(dst_path, "wb");
    assert(fp_in != NULL && fp_out != NULL);

    char chunk_buf[MAX_PAYLOAD_SIZE];
    int bytes_read;
    int chunk_count = 0;
    long total_copied = 0;

    while ((bytes_read = read_chunk(fp_in, chunk_buf, sizeof(chunk_buf))) > 0) {
        int bytes_written = write_chunk(fp_out, chunk_buf, (size_t)bytes_read);
        assert(bytes_written == bytes_read);
        total_copied += bytes_written;
        chunk_count++;
    }

    fclose(fp_in);
    fclose(fp_out);

    printf("[TEST 3] Chunking transfer complete.\n");
    printf("         Chunks copied : %d\n", chunk_count);
    printf("         Total bytes   : %ld\n", total_copied);
    assert(total_copied == (long)test_size);
    printf(">>> TEST 3 PASSED: All chunks transferred intact!\n\n");

    /* 4. Compare SHA-256 hashes of source and destination */
    char dst_hash[SHA256_HEX_LEN];
    assert(compute_file_sha256(dst_path, dst_hash) == 0);
    printf("[TEST 4] Destination file SHA-256:\n         %s\n", dst_hash);
    assert(strcmp(src_hash, dst_hash) == 0);
    assert(compare_file_hashes(src_path, dst_path) == 1);
    printf(">>> TEST 4 PASSED: SHA-256 hashes match 100%% identically!\n\n");

    /* 5. Test corruption detection: Mutate 1 single byte in destination file */
    printf("[TEST 5] Mutating 1 byte in destination file to test integrity detection...\n");
    FILE *fp_corrupt = fopen(dst_path, "r+b");
    assert(fp_corrupt != NULL);
    fseek(fp_corrupt, 500, SEEK_SET);
    int original = fgetc(fp_corrupt);
    fseek(fp_corrupt, 500, SEEK_SET);
    fputc(original ^ 0x01, fp_corrupt); /* Invert 1 bit */
    fclose(fp_corrupt);

    char corrupted_hash[SHA256_HEX_LEN];
    assert(compute_file_sha256(dst_path, corrupted_hash) == 0);
    printf("         Corrupted file SHA-256:\n         %s\n", corrupted_hash);
    assert(strcmp(src_hash, corrupted_hash) != 0);
    assert(compare_file_hashes(src_path, dst_path) == 0);
    printf(">>> TEST 5 PASSED: SHA-256 successfully rejected corrupted file!\n\n");

    /* Clean up temporary test files */
    remove(src_path);
    remove(dst_path);

    printf("====================================================\n");
    printf(">>> ALL DIMPLE DAY 2 TESTS PASSED 100%%! <<<\n");
    printf("====================================================\n");

    return 0;
}
