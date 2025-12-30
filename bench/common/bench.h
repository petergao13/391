/*! @file bench.h
    @brief Benchmark timing and reporting utilities
    @copyright Copyright (c) 2024-2025
*/

#ifndef _BENCH_H_
#define _BENCH_H_

#include "../../usr/syscall.h"
#include "../../usr/string.h"
#include "../../usr/uio.h"

// ============================================================================
// Timing Types and Constants
// ============================================================================

typedef unsigned long bench_time_t;

// Timer frequency (from QEMU virt machine - 10 MHz)
#define TIMER_FREQ 10000000UL

// ============================================================================
// Timing Functions
// ============================================================================

/**
 * @brief Get current time in microseconds
 * @return Current timestamp in microseconds
 */
bench_time_t bench_get_time_us(void);

/**
 * @brief Calculate elapsed time in microseconds
 * @param start Start timestamp from bench_get_time_us()
 * @return Elapsed microseconds since start
 */
bench_time_t bench_elapsed_us(bench_time_t start);

// ============================================================================
// Statistics Collection
// ============================================================================

struct bench_stats {
    unsigned long count;      // Number of samples
    unsigned long total_us;   // Total time in microseconds
    unsigned long min_us;     // Minimum latency
    unsigned long max_us;     // Maximum latency
};

/**
 * @brief Initialize statistics structure
 * @param s Pointer to stats structure
 */
void bench_stats_init(struct bench_stats* s);

/**
 * @brief Add a sample to statistics
 * @param s Pointer to stats structure
 * @param elapsed_us Elapsed time for this sample
 */
void bench_stats_add(struct bench_stats* s, bench_time_t elapsed_us);

/**
 * @brief Print statistics summary
 * @param name Benchmark name
 * @param s Pointer to stats structure
 */
void bench_stats_print(const char* name, struct bench_stats* s);

// ============================================================================
// Reporting Functions
// ============================================================================

/**
 * @brief Report operations per second
 * @param name Benchmark name
 * @param ops Number of operations completed
 * @param elapsed_us Total elapsed time in microseconds
 */
void bench_report_ops(const char* name, unsigned long ops, bench_time_t elapsed_us);

/**
 * @brief Report throughput in bytes/second
 * @param name Benchmark name  
 * @param bytes Number of bytes transferred
 * @param elapsed_us Total elapsed time in microseconds
 */
void bench_report_throughput(const char* name, unsigned long bytes, bench_time_t elapsed_us);

/**
 * @brief Report latency statistics
 * @param name Benchmark name
 * @param s Pointer to stats structure
 */
void bench_report_latency(const char* name, struct bench_stats* s);

// ============================================================================
// Utility Macros
// ============================================================================

/**
 * @brief Repeat a benchmark and collect statistics
 * @param name Benchmark name for reporting
 * @param iterations Number of iterations
 * @param code Code block to benchmark
 */
#define BENCH_REPEAT(name, iterations, code) do { \
    struct bench_stats _bench_s; \
    bench_stats_init(&_bench_s); \
    for (int _bench_i = 0; _bench_i < (iterations); _bench_i++) { \
        bench_time_t _bench_start = bench_get_time_us(); \
        { code; } \
        bench_stats_add(&_bench_s, bench_elapsed_us(_bench_start)); \
    } \
    bench_report_latency(name, &_bench_s); \
} while(0)

/**
 * @brief Time a single operation
 * @param code Code block to time
 * @param result_var Variable to store elapsed microseconds
 */
#define BENCH_TIME(code, result_var) do { \
    bench_time_t _bench_start = bench_get_time_us(); \
    { code; } \
    result_var = bench_elapsed_us(_bench_start); \
} while(0)

// ============================================================================
// Test Helpers
// ============================================================================

/**
 * @brief Print a section header
 * @param section Section name
 */
void bench_section(const char* section);

/**
 * @brief Print benchmark suite header
 * @param suite Suite name
 */
void bench_suite_start(const char* suite);

/**
 * @brief Print benchmark suite footer
 */
void bench_suite_end(void);

/**
 * @brief Simple memory fill for buffer initialization
 * @param buf Buffer to fill
 * @param val Value to fill with
 * @param len Length in bytes
 */
void bench_memset(void* buf, int val, unsigned long len);

/**
 * @brief Generate pseudo-random number (simple LCG)
 * @param seed Pointer to seed (updated in place)
 * @return Pseudo-random value
 */
unsigned long bench_rand(unsigned long* seed);

#endif // _BENCH_H_
