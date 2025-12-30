/*! @file bench_mem.c
    @brief Memory benchmarks (page faults, allocation patterns)
    @copyright Copyright (c) 2024-2025
*/

#include "../common/bench.h"

// User memory region from context.md
#define USER_MEM_START 0x0C0000000UL
#define USER_MEM_END   0x100000000UL
#define PAGE_SIZE      4096

// ============================================================================
// Page Fault Latency Benchmark
// ============================================================================

/**
 * @brief Measure demand paging latency by touching new pages
 * @param num_pages Number of pages to touch
 */
void bench_page_fault_latency(int num_pages) {
    struct bench_stats stats;
    bench_stats_init(&stats);

    // We'll touch pages by writing to them
    // Note: This assumes pages are not pre-faulted
    // Start from a high address to avoid stepping on our own code/data
    volatile char* base = (volatile char*)0x0E0000000UL;

    for (int i = 0; i < num_pages; i++) {
        volatile char* page = base + (i * PAGE_SIZE);
        
        bench_time_t start = bench_get_time_us();
        *page = (char)i;  // Touch page - triggers page fault if not mapped
        bench_time_t elapsed = bench_elapsed_us(start);
        
        // First access should include page fault, subsequent might be faster
        bench_stats_add(&stats, elapsed);
    }

    bench_report_latency("page_fault", &stats);
}

// ============================================================================
// Memory Access Pattern Benchmarks
// ============================================================================

/**
 * @brief Sequential memory access benchmark
 * @param size Total bytes to access
 */
void bench_mem_sequential(unsigned long size) {
    volatile char* base = (volatile char*)0x0E0000000UL;
    
    bench_time_t start = bench_get_time_us();
    
    // Write sequential pattern
    for (unsigned long i = 0; i < size; i++) {
        base[i] = (char)i;
    }
    
    bench_time_t elapsed = bench_elapsed_us(start);
    bench_report_throughput("mem_seq_write", size, elapsed);

    // Read back
    start = bench_get_time_us();
    volatile char sum = 0;
    for (unsigned long i = 0; i < size; i++) {
        sum += base[i];
    }
    elapsed = bench_elapsed_us(start);
    bench_report_throughput("mem_seq_read", size, elapsed);
    
    // Use sum to prevent optimization
    (void)sum;
}

/**
 * @brief Strided memory access (tests TLB behavior)
 * @param num_accesses Number of accesses
 * @param stride Bytes between accesses
 */
void bench_mem_strided(int num_accesses, unsigned long stride) {
    volatile char* base = (volatile char*)0x0E0000000UL;
    
    bench_time_t start = bench_get_time_us();
    
    for (int i = 0; i < num_accesses; i++) {
        base[i * stride] = (char)i;
    }
    
    bench_time_t elapsed = bench_elapsed_us(start);
    
    unsigned long span = num_accesses * stride;
    printf("[BENCH] mem_stride_%lu: %d accesses spanning %lu bytes in %lu us\n",
           stride, num_accesses, span, elapsed);
}

/**
 * @brief Random memory access
 * @param region_size Size of region to access randomly
 * @param num_accesses Number of random accesses
 */
void bench_mem_random(unsigned long region_size, int num_accesses) {
    volatile char* base = (volatile char*)0x0E0000000UL;
    unsigned long seed = 42;
    
    // First, touch all pages to avoid including page fault latency
    for (unsigned long i = 0; i < region_size; i += PAGE_SIZE) {
        base[i] = 0;
    }
    
    bench_time_t start = bench_get_time_us();
    
    volatile char sum = 0;
    for (int i = 0; i < num_accesses; i++) {
        unsigned long offset = bench_rand(&seed) % region_size;
        sum += base[offset];
    }
    
    bench_time_t elapsed = bench_elapsed_us(start);
    
    bench_report_ops("mem_random_read", num_accesses, elapsed);
    (void)sum;
}

// ============================================================================
// Memory Bandwidth Benchmark
// ============================================================================

/**
 * @brief Measure memory bandwidth with large block copies
 * @param block_size Size of each copy operation
 * @param iterations Number of copy operations
 */
void bench_mem_bandwidth(unsigned long block_size, int iterations) {
    volatile char* src = (volatile char*)0x0E0000000UL;
    volatile char* dst = (volatile char*)0x0E0000000UL + (2 * 1024 * 1024);  // 2MB offset
    
    // Initialize source
    for (unsigned long i = 0; i < block_size; i++) {
        src[i] = (char)i;
    }
    
    bench_time_t start = bench_get_time_us();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (unsigned long i = 0; i < block_size; i++) {
            dst[i] = src[i];
        }
    }
    
    bench_time_t elapsed = bench_elapsed_us(start);
    unsigned long total_bytes = block_size * iterations * 2;  // Read + write
    bench_report_throughput("mem_copy", total_bytes, elapsed);
}

// ============================================================================
// Stack Usage Benchmark
// ============================================================================

// Recursive function to measure stack depth
static volatile int stack_depth_counter = 0;

static void recurse_stack(int depth, int max_depth) {
    volatile char local_buf[256];  // Use some stack space
    // Fill buffer to ensure it's actually allocated on stack
    for (int i = 0; i < 256; i++) {
        local_buf[i] = (char)(depth + i);
    }
    stack_depth_counter = depth;
    // Read back to prevent optimization
    volatile char check = local_buf[depth % 256];
    (void)check;
    
    if (depth < max_depth) {
        recurse_stack(depth + 1, max_depth);
    }
}

/**
 * @brief Test stack growth and page fault handling
 * @param max_depth Maximum recursion depth
 */
void bench_stack_growth(int max_depth) {
    bench_time_t start = bench_get_time_us();
    
    recurse_stack(0, max_depth);
    
    bench_time_t elapsed = bench_elapsed_us(start);
    
    printf("[BENCH] stack_growth: depth %d in %lu us (~%lu bytes)\n",
           stack_depth_counter, elapsed, (unsigned long)(stack_depth_counter * 256));
}

// ============================================================================
// Working Set Size Detection
// ============================================================================

/**
 * @brief Detect effective working set size through timing
 * @param max_pages Maximum pages to test
 */
void bench_working_set(int max_pages) {
    printf("[BENCH] working_set: testing 1 to %d pages\n", max_pages);
    
    volatile char* base = (volatile char*)0x0E0000000UL;
    unsigned long seed = 12345;
    
    // Pre-touch all pages
    for (int p = 0; p < max_pages; p++) {
        base[p * PAGE_SIZE] = 0;
    }
    
    int test_sizes[] = {1, 2, 4, 8, 16, 32, 64, 128};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    for (int s = 0; s < num_sizes && test_sizes[s] <= max_pages; s++) {
        int working_set = test_sizes[s];
        unsigned long region_size = working_set * PAGE_SIZE;
        
        // Random accesses within working set
        bench_time_t start = bench_get_time_us();
        
        for (int i = 0; i < 10000; i++) {
            unsigned long offset = bench_rand(&seed) % region_size;
            base[offset]++;
        }
        
        bench_time_t elapsed = bench_elapsed_us(start);
        unsigned long ns_per_access = (elapsed * 1000) / 10000;
        
        printf("[BENCH] ws_%d_pages: 10000 accesses in %lu us (%lu ns/access)\n",
               working_set, elapsed, ns_per_access);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    bench_suite_start("Memory Benchmarks");

    bench_section("Page Fault Latency");
    bench_page_fault_latency(16);

    bench_section("Sequential Access");
    bench_mem_sequential(64 * 1024);  // 64 KB

    bench_section("Strided Access");
    bench_mem_strided(100, PAGE_SIZE);      // Page-sized stride
    bench_mem_strided(100, PAGE_SIZE / 4);  // Quarter page stride

    bench_section("Random Access");
    bench_mem_random(64 * 1024, 1000);

    bench_section("Memory Bandwidth");
    bench_mem_bandwidth(4096, 100);

    bench_section("Stack Growth");
    bench_stack_growth(50);

    bench_section("Working Set Analysis");
    bench_working_set(64);

    bench_suite_end();
    _exit();
}
