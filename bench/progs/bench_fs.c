/*! @file bench_fs.c
    @brief Filesystem benchmarks
    @copyright Copyright (c) 2024-2025
*/

#include "../common/bench.h"

#define TEST_FILE "c/benchtest"
#define BLOCK_SIZE 512
#define LARGE_FILE_SIZE (64 * 1024)  // 64 KB
#define SMALL_ITERS 100
#define LARGE_ITERS 10

static char write_buf[BLOCK_SIZE];
static char read_buf[BLOCK_SIZE];

// ============================================================================
// Sequential Write Benchmark
// ============================================================================

/**
 * @brief Measure sequential write throughput
 * @param path File path to write to
 * @param total_bytes Total bytes to write
 * @param block_size Size of each write operation
 */
void bench_seq_write(const char* path, unsigned long total_bytes, unsigned long block_size) {
    // Create the test file
    int ret = _fscreate((char*)path);
    if (ret < 0) {
        printf("[BENCH] seq_write: failed to create file %s\n", path);
        return;
    }

    int fd = _open(1, path);  // Open for writing
    if (fd < 0) {
        printf("[BENCH] seq_write: failed to open file %s\n", path);
        return;
    }

    // Fill write buffer with pattern
    for (unsigned long i = 0; i < block_size; i++) {
        write_buf[i] = (char)(i & 0xFF);
    }

    unsigned long bytes_written = 0;
    bench_time_t start = bench_get_time_us();

    while (bytes_written < total_bytes) {
        unsigned long to_write = block_size;
        if (bytes_written + to_write > total_bytes) {
            to_write = total_bytes - bytes_written;
        }
        long written = _write(fd, write_buf, to_write);
        if (written <= 0) {
            printf("[BENCH] seq_write: write error at %lu bytes\n", bytes_written);
            break;
        }
        bytes_written += written;
    }

    bench_time_t elapsed = bench_elapsed_us(start);
    _close(fd);

    bench_report_throughput("seq_write", bytes_written, elapsed);
}

// ============================================================================
// Sequential Read Benchmark
// ============================================================================

/**
 * @brief Measure sequential read throughput
 * @param path File path to read from
 * @param block_size Size of each read operation
 */
void bench_seq_read(const char* path, unsigned long block_size) {
    int fd = _open(0, path);  // Open for reading
    if (fd < 0) {
        printf("[BENCH] seq_read: failed to open file %s\n", path);
        return;
    }

    unsigned long bytes_read = 0;
    bench_time_t start = bench_get_time_us();

    for (;;) {
        long rd = _read(fd, read_buf, block_size);
        if (rd <= 0) break;
        bytes_read += rd;
    }

    bench_time_t elapsed = bench_elapsed_us(start);
    _close(fd);

    bench_report_throughput("seq_read", bytes_read, elapsed);
}

// ============================================================================
// Random Read Benchmark (tests cache effectiveness)
// ============================================================================

/**
 * @brief Measure random read performance
 * @param path File path to read from
 * @param file_size Size of the file
 * @param num_reads Number of random reads to perform
 */
void bench_random_read(const char* path, unsigned long file_size, int num_reads) {
    int fd = _open(0, path);
    if (fd < 0) {
        printf("[BENCH] random_read: failed to open file %s\n", path);
        return;
    }

    unsigned long seed = 12345;
    unsigned long bytes_read = 0;
    bench_time_t start = bench_get_time_us();

    for (int i = 0; i < num_reads; i++) {
        // Generate random position (block-aligned)
        unsigned long pos = (bench_rand(&seed) % (file_size / BLOCK_SIZE)) * BLOCK_SIZE;
        
        // Seek to position
        _fcntl(fd, 2, (void*)pos);  // FCNTL_SETPOS = 2
        
        // Read one block
        long rd = _read(fd, read_buf, BLOCK_SIZE);
        if (rd > 0) bytes_read += rd;
    }

    bench_time_t elapsed = bench_elapsed_us(start);
    _close(fd);

    bench_report_ops("random_read", num_reads, elapsed);
    printf("[BENCH] random_read: %lu total bytes\n", bytes_read);
}

// ============================================================================
// File Create/Delete Benchmark
// ============================================================================

/**
 * @brief Measure file creation and deletion throughput
 * @param num_files Number of files to create/delete
 */
void bench_file_create_delete(int num_files) {
    char filename[32];
    struct bench_stats create_stats, delete_stats;
    bench_stats_init(&create_stats);
    bench_stats_init(&delete_stats);

    for (int i = 0; i < num_files; i++) {
        // Generate filename
        snprintf(filename, sizeof(filename), "c/bench%d", i);

        // Time file creation
        bench_time_t start = bench_get_time_us();
        int ret = _fscreate(filename);
        bench_stats_add(&create_stats, bench_elapsed_us(start));
        
        if (ret < 0) {
            printf("[BENCH] file_create: failed to create %s\n", filename);
        }
    }

    // Now delete all files
    for (int i = 0; i < num_files; i++) {
        snprintf(filename, sizeof(filename), "c/bench%d", i);

        bench_time_t start = bench_get_time_us();
        int ret = _fsdelete(filename);
        bench_stats_add(&delete_stats, bench_elapsed_us(start));
        
        if (ret < 0) {
            printf("[BENCH] file_delete: failed to delete %s\n", filename);
        }
    }

    bench_report_latency("file_create", &create_stats);
    bench_report_latency("file_delete", &delete_stats);
}

// ============================================================================
// Open/Close Latency Benchmark
// ============================================================================

/**
 * @brief Measure file open and close latency
 * @param path File path to open
 * @param iterations Number of open/close cycles
 */
void bench_open_close(const char* path, int iterations) {
    struct bench_stats open_stats, close_stats;
    bench_stats_init(&open_stats);
    bench_stats_init(&close_stats);

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        int fd = _open(-1, path);  // -1 = allocate any fd
        bench_stats_add(&open_stats, bench_elapsed_us(start));

        if (fd >= 0) {
            start = bench_get_time_us();
            _close(fd);
            bench_stats_add(&close_stats, bench_elapsed_us(start));
        }
    }

    bench_report_latency("file_open", &open_stats);
    bench_report_latency("file_close", &close_stats);
}

// ============================================================================
// Directory Listing Benchmark
// ============================================================================

/**
 * @brief Measure directory listing performance
 * @param path Directory path (e.g., "c" for ktfs root)
 * @param iterations Number of listing iterations
 */
void bench_ls(const char* path, int iterations) {
    char entry[64];
    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        
        // Open directory listing
        int fd = _open(-1, path);
        if (fd >= 0) {
            // Read all entries
            while (_read(fd, entry, sizeof(entry)) > 0) {
                // Just consume entries
            }
            _close(fd);
        }
        
        bench_stats_add(&stats, bench_elapsed_us(start));
    }

    bench_report_latency("dir_listing", &stats);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    bench_suite_start("Filesystem Benchmarks");

    // Ensure we have a clean test file
    _fsdelete((char*)TEST_FILE);

    bench_section("Write Performance");
    bench_seq_write(TEST_FILE, LARGE_FILE_SIZE, BLOCK_SIZE);

    bench_section("Read Performance");
    bench_seq_read(TEST_FILE, BLOCK_SIZE);

    bench_section("Random Access");
    bench_random_read(TEST_FILE, LARGE_FILE_SIZE, 50);

    bench_section("Metadata Operations");
    bench_file_create_delete(10);
    bench_open_close(TEST_FILE, SMALL_ITERS);

    bench_section("Directory Operations");
    bench_ls("c", 10);

    // Cleanup
    _fsdelete((char*)TEST_FILE);

    bench_suite_end();
    _exit();
}
