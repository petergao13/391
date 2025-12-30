/*! @file bench_proc.c
    @brief Process benchmarks (fork, exec, wait)
    @copyright Copyright (c) 2024-2025
*/

#include "../common/bench.h"

#define FORK_ITERATIONS 20
#define EXEC_ITERATIONS 5

// ============================================================================
// Fork Latency Benchmark
// ============================================================================

/**
 * @brief Measure fork() latency
 * @param iterations Number of fork operations to measure
 */
void bench_fork_latency(int iterations) {
    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        int pid = _fork();
        bench_time_t elapsed = bench_elapsed_us(start);

        if (pid == 0) {
            // Child process - exit immediately
            _exit();
        } else if (pid > 0) {
            // Parent process
            bench_stats_add(&stats, elapsed);
            // Wait for child to exit
            _wait(pid);
        } else {
            printf("[BENCH] fork failed with error %d\n", pid);
        }
    }

    bench_report_latency("fork", &stats);
}

// ============================================================================
// Fork + Exit Latency (Round-trip)
// ============================================================================

/**
 * @brief Measure fork + child exit + wait round-trip time
 * @param iterations Number of iterations
 */
void bench_fork_exit_roundtrip(int iterations) {
    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        
        int pid = _fork();
        if (pid == 0) {
            // Child - exit immediately
            _exit();
        } else if (pid > 0) {
            // Parent - wait for child
            _wait(pid);
            bench_stats_add(&stats, bench_elapsed_us(start));
        }
    }

    bench_report_latency("fork_exit_roundtrip", &stats);
}

// ============================================================================
// Fork + Exec Latency
// ============================================================================

/**
 * @brief Measure fork + exec latency
 * @param prog_path Path to program to exec
 * @param iterations Number of iterations
 */
void bench_fork_exec(const char* prog_path, int iterations) {
    struct bench_stats stats;
    bench_stats_init(&stats);
    char* argv[] = { (char*)prog_path, NULL };

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        
        int pid = _fork();
        if (pid == 0) {
            // Child - exec the program
            int fd = _open(-1, prog_path);
            if (fd >= 0) {
                _exec(fd, 1, argv);
            }
            // If exec fails, exit
            _exit();
        } else if (pid > 0) {
            // Parent - wait for child to complete
            _wait(pid);
            bench_stats_add(&stats, bench_elapsed_us(start));
        }
    }

    bench_report_latency("fork_exec", &stats);
}

// ============================================================================
// Wait Latency
// ============================================================================

/**
 * @brief Measure wait() latency when child has already exited
 * @param iterations Number of iterations
 */
void bench_wait_latency(int iterations) {
    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        int pid = _fork();
        if (pid == 0) {
            // Child - exit immediately
            _exit();
        } else if (pid > 0) {
            // Small delay to let child exit first
            _usleep(1000);  // 1ms
            
            bench_time_t start = bench_get_time_us();
            _wait(pid);
            bench_stats_add(&stats, bench_elapsed_us(start));
        }
    }

    bench_report_latency("wait_after_exit", &stats);
}

// ============================================================================
// Process Creation Throughput
// ============================================================================

/**
 * @brief Measure maximum process creation rate
 * @param num_processes Number of processes to create
 */
void bench_process_throughput(int num_processes) {
    int pids[16];  // Max concurrent processes
    int count = 0;

    bench_time_t start = bench_get_time_us();

    // Create processes (up to limit)
    for (int i = 0; i < num_processes && i < 16; i++) {
        int pid = _fork();
        if (pid == 0) {
            // Child - just exit after small delay
            _usleep(10000);  // 10ms
            _exit();
        } else if (pid > 0) {
            pids[count++] = pid;
        }
    }

    // Wait for all children
    for (int i = 0; i < count; i++) {
        _wait(pids[i]);
    }

    bench_time_t elapsed = bench_elapsed_us(start);
    bench_report_ops("process_throughput", count, elapsed);
}

// ============================================================================
// Context Switch Estimation
// ============================================================================

/**
 * @brief Estimate context switch overhead using ping-pong between processes
 * @param ping_pongs Number of context switches to measure
 */
void bench_context_switch(int ping_pongs) {
    int wfd, rfd;
    int wfd2, rfd2;
    
    // Create two pipes for bidirectional communication
    if (_pipe(&wfd, &rfd) < 0) {
        printf("[BENCH] context_switch: failed to create pipe 1\n");
        return;
    }
    if (_pipe(&wfd2, &rfd2) < 0) {
        printf("[BENCH] context_switch: failed to create pipe 2\n");
        _close(wfd);
        _close(rfd);
        return;
    }

    int pid = _fork();
    if (pid == 0) {
        // Child: read from pipe1, write to pipe2
        char buf[1];
        _close(wfd);   // Close write end of pipe1
        _close(rfd2);  // Close read end of pipe2
        
        for (int i = 0; i < ping_pongs; i++) {
            _read(rfd, buf, 1);
            _write(wfd2, buf, 1);
        }
        
        _close(rfd);
        _close(wfd2);
        _exit();
    } else if (pid > 0) {
        // Parent: write to pipe1, read from pipe2
        char buf[1] = {'x'};
        _close(rfd);   // Close read end of pipe1
        _close(wfd2);  // Close write end of pipe2

        bench_time_t start = bench_get_time_us();
        
        for (int i = 0; i < ping_pongs; i++) {
            _write(wfd, buf, 1);
            _read(rfd2, buf, 1);
        }
        
        bench_time_t elapsed = bench_elapsed_us(start);
        
        _close(wfd);
        _close(rfd2);
        _wait(pid);

        // Each ping-pong is 2 context switches (parent->child, child->parent)
        unsigned long switches = ping_pongs * 2;
        unsigned long us_per_switch = elapsed / switches;
        printf("[BENCH] context_switch: %lu switches in %lu us (~%lu us/switch)\n",
               switches, elapsed, us_per_switch);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    bench_suite_start("Process Benchmarks");

    bench_section("Fork Latency");
    bench_fork_latency(FORK_ITERATIONS);

    bench_section("Fork + Exit Round-trip");
    bench_fork_exit_roundtrip(FORK_ITERATIONS);

    bench_section("Fork + Exec");
    // Use a simple program like echo
    bench_fork_exec("c/echo", EXEC_ITERATIONS);

    bench_section("Wait Latency");
    bench_wait_latency(FORK_ITERATIONS);

    bench_section("Process Throughput");
    bench_process_throughput(10);

    bench_section("Context Switch Estimation");
    bench_context_switch(100);

    bench_suite_end();
    _exit();
}
