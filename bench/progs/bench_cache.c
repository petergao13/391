/*! @file bench_cache.c
    @brief Block cache benchmarks
    @copyright Copyright (c) 2024-2025
*/

#include "../common/bench.h"

#define BLOCK_SIZE 512
#define CACHE_SIZE 64  // 64 blocks in cache per context.md
#define TEST_FILE "c/cachetest"

static char read_buf[BLOCK_SIZE];

// ============================================================================
// Cache Hit Rate Simulation
// ============================================================================

/**
 * @brief Measure sequential access (should have high cache hit rate)
 * @param file_size Size of file to read
 * @param iterations Number of complete read-throughs
 */
void bench_cache_sequential(unsigned long file_size, int iterations) {
    // First, create and populate test file
    _fsdelete((char*)TEST_FILE);
    _fscreate((char*)TEST_FILE);
    
    int fd = _open(-1, TEST_FILE);
    if (fd < 0) {
        printf("[BENCH] cache_sequential: failed to open %s\n", TEST_FILE);
        return;
    }
    
    // Write test data
    char write_buf[BLOCK_SIZE];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        write_buf[i] = (char)i;
    }
    
    unsigned long written = 0;
    while (written < file_size) {
        _write(fd, write_buf, BLOCK_SIZE);
        written += BLOCK_SIZE;
    }
    _close(fd);
    
    // Now measure read performance across iterations
    unsigned long total_bytes = 0;
    bench_time_t start = bench_get_time_us();
    
    for (int iter = 0; iter < iterations; iter++) {
        fd = _open(-1, TEST_FILE);
        if (fd < 0) continue;
        
        for (;;) {
            long rd = _read(fd, read_buf, BLOCK_SIZE);
            if (rd <= 0) break;
            total_bytes += rd;
        }
        _close(fd);
    }
    
    bench_time_t elapsed = bench_elapsed_us(start);
    
    // First iteration is cold, rest should be cached
    printf("[BENCH] cache_seq: %lu bytes in %lu us (x%d iterations)\n",
           total_bytes, elapsed, iterations);
    bench_report_throughput("cache_seq_total", total_bytes, elapsed);
    
    _fsdelete((char*)TEST_FILE);
}

// ============================================================================
// Cache Thrashing Benchmark
// ============================================================================

/**
 * @brief Access more blocks than cache can hold (64 blocks)
 * @param num_blocks Number of unique blocks to access
 * @param access_pattern 0=sequential, 1=random
 */
void bench_cache_thrash(int num_blocks, int access_pattern) {
    _fsdelete((char*)TEST_FILE);
    _fscreate((char*)TEST_FILE);
    
    int fd = _open(-1, TEST_FILE);
    if (fd < 0) {
        printf("[BENCH] cache_thrash: failed to create test file\n");
        return;
    }
    
    // Write file
    char write_buf[BLOCK_SIZE];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        write_buf[i] = (char)i;
    }
    for (int i = 0; i < num_blocks; i++) {
        _write(fd, write_buf, BLOCK_SIZE);
    }
    _close(fd);
    
    // Now read blocks in specified pattern
    fd = _open(-1, TEST_FILE);
    if (fd < 0) {
        _fsdelete((char*)TEST_FILE);
        return;
    }
    
    unsigned long seed = 54321;
    int accesses = num_blocks * 2;  // Each block accessed twice on average
    
    bench_time_t start = bench_get_time_us();
    
    for (int i = 0; i < accesses; i++) {
        unsigned long block;
        if (access_pattern == 0) {
            // Sequential with wraparound
            block = i % num_blocks;
        } else {
            // Random
            block = bench_rand(&seed) % num_blocks;
        }
        
        unsigned long pos = block * BLOCK_SIZE;
        _fcntl(fd, 2, (void*)pos);  // FCNTL_SETPOS
        _read(fd, read_buf, BLOCK_SIZE);
    }
    
    bench_time_t elapsed = bench_elapsed_us(start);
    _close(fd);
    
    const char* pattern_name = (access_pattern == 0) ? "seq" : "rand";
    printf("[BENCH] cache_thrash_%s_%d_blocks: %d accesses in %lu us\n",
           pattern_name, num_blocks, accesses, elapsed);
    
    _fsdelete((char*)TEST_FILE);
}

// ============================================================================
// Working Set vs Cache Size
// ============================================================================

/**
 * @brief Test performance with different working set sizes
 */
void bench_cache_working_set(void) {
    printf("[BENCH] Testing cache behavior with different working set sizes\n");
    printf("[BENCH] Cache holds %d blocks of %d bytes each\n", CACHE_SIZE, BLOCK_SIZE);
    
    // Test working sets: 16, 32, 64, 96, 128 blocks
    int working_sets[] = {16, 32, 48, 64, 80, 96, 128};
    int num_tests = sizeof(working_sets) / sizeof(working_sets[0]);
    
    for (int t = 0; t < num_tests; t++) {
        int ws = working_sets[t];
        
        _fsdelete((char*)TEST_FILE);
        _fscreate((char*)TEST_FILE);
        
        int fd = _open(-1, TEST_FILE);
        if (fd < 0) continue;
        
        char buf[BLOCK_SIZE];
        for (int i = 0; i < ws; i++) {
            _write(fd, buf, BLOCK_SIZE);
        }
        _close(fd);
        
        // Perform random accesses
        fd = _open(-1, TEST_FILE);
        if (fd < 0) continue;
        
        unsigned long seed = 99999;
        int accesses = 500;
        
        bench_time_t start = bench_get_time_us();
        
        for (int i = 0; i < accesses; i++) {
            unsigned long block = bench_rand(&seed) % ws;
            unsigned long pos = block * BLOCK_SIZE;
            _fcntl(fd, 2, (void*)pos);
            _read(fd, read_buf, BLOCK_SIZE);
        }
        
        bench_time_t elapsed = bench_elapsed_us(start);
        _close(fd);
        
        unsigned long us_per_access = elapsed / accesses;
        const char* status = (ws <= CACHE_SIZE) ? "(fits)" : "(thrash)";
        printf("[BENCH] ws_%d_blocks %s: %d accesses in %lu us (%lu us/access)\n",
               ws, status, accesses, elapsed, us_per_access);
    }
    
    _fsdelete((char*)TEST_FILE);
}

// ============================================================================
// Locality Patterns
// ============================================================================

/**
 * @brief Test temporal locality (re-accessing same blocks)
 * @param hot_blocks Number of frequently accessed blocks
 * @param cold_blocks Number of occasionally accessed blocks
 */
void bench_cache_locality(int hot_blocks, int cold_blocks) {
    int total_blocks = hot_blocks + cold_blocks;
    
    _fsdelete((char*)TEST_FILE);
    _fscreate((char*)TEST_FILE);
    
    int fd = _open(-1, TEST_FILE);
    if (fd < 0) {
        printf("[BENCH] cache_locality: failed to create file\n");
        return;
    }
    
    char buf[BLOCK_SIZE];
    for (int i = 0; i < total_blocks; i++) {
        _write(fd, buf, BLOCK_SIZE);
    }
    _close(fd);
    
    fd = _open(-1, TEST_FILE);
    if (fd < 0) {
        _fsdelete((char*)TEST_FILE);
        return;
    }
    
    // Access pattern: 80% hot blocks, 20% cold blocks
    unsigned long seed = 11111;
    int accesses = 1000;
    int hot_accesses = 0;
    int cold_accesses = 0;
    
    bench_time_t start = bench_get_time_us();
    
    for (int i = 0; i < accesses; i++) {
        unsigned long block;
        if ((bench_rand(&seed) % 100) < 80) {
            // Access hot block
            block = bench_rand(&seed) % hot_blocks;
            hot_accesses++;
        } else {
            // Access cold block
            block = hot_blocks + (bench_rand(&seed) % cold_blocks);
            cold_accesses++;
        }
        
        unsigned long pos = block * BLOCK_SIZE;
        _fcntl(fd, 2, (void*)pos);
        _read(fd, read_buf, BLOCK_SIZE);
    }
    
    bench_time_t elapsed = bench_elapsed_us(start);
    _close(fd);
    
    printf("[BENCH] cache_locality: %d hot/%d cold blocks, %d/%d accesses in %lu us\n",
           hot_blocks, cold_blocks, hot_accesses, cold_accesses, elapsed);
    
    _fsdelete((char*)TEST_FILE);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    bench_suite_start("Cache Benchmarks");

    bench_section("Sequential Access (Cold vs Warm)");
    bench_cache_sequential(32 * BLOCK_SIZE, 3);  // 32 blocks, 3 iterations

    bench_section("Cache Thrashing");
    // Within cache capacity
    bench_cache_thrash(32, 0);   // 32 blocks sequential
    bench_cache_thrash(32, 1);   // 32 blocks random
    // At cache capacity
    bench_cache_thrash(64, 0);   // 64 blocks sequential  
    bench_cache_thrash(64, 1);   // 64 blocks random
    // Beyond cache capacity
    bench_cache_thrash(128, 0);  // 128 blocks sequential
    bench_cache_thrash(128, 1);  // 128 blocks random

    bench_section("Working Set vs Cache Size");
    bench_cache_working_set();

    bench_section("Locality Patterns");
    bench_cache_locality(16, 16);   // Small hot set, small cold set
    bench_cache_locality(16, 64);   // Small hot set, large cold set
    bench_cache_locality(64, 64);   // Large hot set (cache-sized), large cold

    bench_suite_end();
    _exit();
}
