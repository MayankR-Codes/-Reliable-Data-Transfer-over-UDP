#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "packet.h"

#define SHA256_HEX_LEN 65 /* 64 hex characters + null terminator */

/* Check if a file exists on disk */
int file_exists(const char *filepath);

/* Get total file size in bytes (-1 on error) */
long get_file_size(const char *filepath);

/* Read up to max_len bytes from file stream into buffer.
   Returns number of bytes read (0 on EOF, -1 on error). */
int read_chunk(FILE *fp, char *buffer, size_t max_len);

/* Write len bytes from buffer into file stream.
   Returns number of bytes written (-1 on error). */
int write_chunk(FILE *fp, const char *buffer, size_t len);

/* Compute SHA-256 hash of a file as a 64-char hex string.
   hash_out must be at least 65 bytes long.
   Returns 0 on success, -1 on error. */
int compute_file_sha256(const char *filepath, char *hash_out);

/* Compare SHA-256 hashes of two files.
   Returns 1 if hashes match identically (100% data integrity),
   0 if hashes differ, -1 if an error occurred opening files. */
int compare_file_hashes(const char *file1, const char *file2);

#endif /* FILE_IO_H */
