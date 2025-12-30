/*! @file bench.c
    @brief Benchmark timing and reporting utilities implementation
    @copyright Copyright (c) 2024-2025
*/

#include "bench.h"

// ============================================================================
// RTC/Timer Access for Timing
// ============================================================================

// QEMU virt machine RTC base address
#define RTC_BASE 0x00101000UL

// Read the RTC time register
// Note: This reads the low 32-bit time value. For benchmarks this is sufficient.
static inline unsigned long read_rtc_time(void) {
    volatile unsigned long* rtc = (volatile unsigned long*)RTC_BASE;
    return rtc[0];  // Time low register
}

// Alternative: Use RDTIME instruction (reads mtime CSR from user mode)
static inline unsigned long read_cycle(void) {
    unsigned long val;
    __asm__ volatile ("rdtime %0" : "=r"(val));
    return val;
}

bench_time_t bench_get_time_us(void) {
    // Use RDTIME which gives us timer ticks at TIMER_FREQ (10 MHz)
    // Convert to microseconds: ticks / (TIMER_FREQ / 1000000) = ticks / 10
    return read_cycle() / 10;
}

bench_time_t bench_elapsed_us(bench_time_t start) {
    bench_time_t now = bench_get_time_us();
    // Handle potential wraparound (unlikely for benchmarks)
    if (now >= start) {
        return now - start;
    }
    return now + (0xFFFFFFFFFFFFFFFFUL - start) + 1;
}

// ============================================================================
// Statistics Implementation
// ============================================================================

void bench_stats_init(struct bench_stats* s) {
    s->count = 0;
    s->total_us = 0;
    s->min_us = 0xFFFFFFFFFFFFFFFFUL;
    s->max_us = 0;
}

void bench_stats_add(struct bench_stats* s, bench_time_t elapsed_us) {
    s->count++;
    s->total_us += elapsed_us;
    if (elapsed_us < s->min_us) {
        s->min_us = elapsed_us;
    }
    if (elapsed_us > s->max_us) {
        s->max_us = elapsed_us;
    }
}

void bench_stats_print(const char* name, struct bench_stats* s) {
    if (s->count == 0) {
        printf("[BENCH] %s: no samples\n", name);
        return;
    }
    unsigned long avg_us = s->total_us / s->count;
    printf("[BENCH] %s: %lu ops, avg=%lu us, min=%lu us, max=%lu us\n",
           name, s->count, avg_us, s->min_us, s->max_us);
}

// ============================================================================
// Reporting Implementation
// ============================================================================

void bench_report_ops(const char* name, unsigned long ops, bench_time_t elapsed_us) {
    if (elapsed_us == 0) elapsed_us = 1;  // Avoid division by zero
    // ops/sec = ops * 1000000 / elapsed_us
    unsigned long ops_per_sec = (ops * 1000000UL) / elapsed_us;
    printf("[BENCH] %s: %lu ops in %lu us (%lu ops/s)\n",
           name, ops, elapsed_us, ops_per_sec);
}

void bench_report_throughput(const char* name, unsigned long bytes, bench_time_t elapsed_us) {
    if (elapsed_us == 0) elapsed_us = 1;  // Avoid division by zero
    // bytes/sec = bytes * 1000000 / elapsed_us
    unsigned long bytes_per_sec = (bytes * 1000000UL) / elapsed_us;
    // Convert to KB/s for readability
    unsigned long kb_per_sec = bytes_per_sec / 1024;
    printf("[BENCH] %s: %lu bytes in %lu us (%lu KB/s)\n",
           name, bytes, elapsed_us, kb_per_sec);
}

void bench_report_latency(const char* name, struct bench_stats* s) {
    bench_stats_print(name, s);
}

// ============================================================================
// Utility Implementation
// ============================================================================

void bench_section(const char* section) {
    printf("\n--- %s ---\n", section);
}

void bench_suite_start(const char* suite) {
    printf("\n");
    printf("========================================\n");
    printf("  %s\n", suite);
    printf("========================================\n");
}

void bench_suite_end(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Benchmark Complete\n");
    printf("========================================\n");
}

void bench_memset(void* buf, int val, unsigned long len) {
    unsigned char* p = (unsigned char*)buf;
    for (unsigned long i = 0; i < len; i++) {
        p[i] = (unsigned char)val;
    }
}

unsigned long bench_rand(unsigned long* seed) {
    // Simple Linear Congruential Generator
    // Parameters from Numerical Recipes
    *seed = (*seed * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
    return *seed;
}
