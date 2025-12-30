/*! @file bench_syscall.c
    @brief Syscall latency benchmarks
    @copyright Copyright (c) 2024-2025
*/

#include "../common/bench.h"

#define SYSCALL_ITERATIONS 1000
#define BUF_SIZE 64

static char io_buf[BUF_SIZE];

// ============================================================================
// Individual Syscall Latency Benchmarks
// ============================================================================

/**
 * @brief Measure usleep syscall overhead (minimal sleep)
 */
void bench_syscall_usleep(int iterations) {
    BENCH_REPEAT("syscall_usleep(1)", iterations, {
        _usleep(1);  // 1 microsecond - measures syscall overhead
    });
}

/**
 * @brief Measure open/close syscall pair
 */
void bench_syscall_open_close(int iterations) {
    BENCH_REPEAT("syscall_open_close", iterations, {
        int fd = _open(-1, "c/echo");
        if (fd >= 0) _close(fd);
    });
}

/**
 * @brief Measure read syscall on console (non-blocking)
 */
void bench_syscall_read(int iterations) {
    // Open a file for reading
    int fd = _open(-1, "c/echo");
    if (fd < 0) {
        printf("[BENCH] syscall_read: failed to open file\n");
        return;
    }

    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        // Reset position to start
        _fcntl(fd, 2, (void*)0);  // FCNTL_SETPOS = 2
        
        bench_time_t start = bench_get_time_us();
        _read(fd, io_buf, BUF_SIZE);
        bench_stats_add(&stats, bench_elapsed_us(start));
    }

    _close(fd);
    bench_report_latency("syscall_read", &stats);
}

/**
 * @brief Measure write syscall
 */
void bench_syscall_write(int iterations) {
    // Create a test file
    _fsdelete("c/benchwr");
    _fscreate("c/benchwr");
    int fd = _open(-1, "c/benchwr");
    if (fd < 0) {
        printf("[BENCH] syscall_write: failed to open file\n");
        return;
    }

    // Fill buffer
    for (int i = 0; i < BUF_SIZE; i++) {
        io_buf[i] = 'A';
    }

    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        _write(fd, io_buf, BUF_SIZE);
        bench_stats_add(&stats, bench_elapsed_us(start));
    }

    _close(fd);
    _fsdelete("c/benchwr");
    bench_report_latency("syscall_write", &stats);
}

/**
 * @brief Measure fcntl syscall (GETPOS)
 */
void bench_syscall_fcntl(int iterations) {
    int fd = _open(-1, "c/echo");
    if (fd < 0) {
        printf("[BENCH] syscall_fcntl: failed to open file\n");
        return;
    }

    BENCH_REPEAT("syscall_fcntl_getpos", iterations, {
        _fcntl(fd, 1, NULL);  // FCNTL_GETPOS = 1
    });

    _close(fd);
}

/**
 * @brief Measure pipe syscall
 */
static void pipe_test_body(void) {
    int wfd, rfd;
    if (_pipe(&wfd, &rfd) >= 0) {
        _close(wfd);
        _close(rfd);
    }
}

void bench_syscall_pipe(int iterations) {
    BENCH_REPEAT("syscall_pipe", iterations, pipe_test_body());
}

/**
 * @brief Measure uiodup syscall
 */
void bench_syscall_uiodup(int iterations) {
    int fd = _open(-1, "c/echo");
    if (fd < 0) {
        printf("[BENCH] syscall_uiodup: failed to open file\n");
        return;
    }

    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        int newfd = _uiodup(fd, -1);  // Duplicate to any available fd
        bench_stats_add(&stats, bench_elapsed_us(start));
        
        if (newfd >= 0) _close(newfd);
    }

    _close(fd);
    bench_report_latency("syscall_uiodup", &stats);
}

/**
 * @brief Measure fork syscall only (not including child execution)
 */
void bench_syscall_fork(int iterations) {
    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        bench_time_t start = bench_get_time_us();
        int pid = _fork();
        bench_time_t elapsed = bench_elapsed_us(start);
        
        if (pid == 0) {
            // Child exits immediately
            _exit();
        } else if (pid > 0) {
            bench_stats_add(&stats, elapsed);
            _wait(pid);
        }
    }

    bench_report_latency("syscall_fork", &stats);
}

/**
 * @brief Measure wait syscall when child already exited
 */
void bench_syscall_wait(int iterations) {
    struct bench_stats stats;
    bench_stats_init(&stats);

    for (int i = 0; i < iterations; i++) {
        int pid = _fork();
        if (pid == 0) {
            _exit();
        } else if (pid > 0) {
            _usleep(1000);  // Let child exit first
            
            bench_time_t start = bench_get_time_us();
            _wait(pid);
            bench_stats_add(&stats, bench_elapsed_us(start));
        }
    }

    bench_report_latency("syscall_wait", &stats);
}

// ============================================================================
// Syscall Throughput
// ============================================================================

/**
 * @brief Measure raw syscall throughput
 */
void bench_syscall_throughput(void) {
    int iterations = 10000;
    
    bench_time_t start = bench_get_time_us();
    for (int i = 0; i < iterations; i++) {
        _usleep(0);  // Minimal syscall - just enter/exit kernel
    }
    bench_time_t elapsed = bench_elapsed_us(start);
    
    bench_report_ops("syscall_throughput", iterations, elapsed);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    bench_suite_start("Syscall Latency Benchmarks");

    bench_section("Minimal Syscalls");
    bench_syscall_usleep(SYSCALL_ITERATIONS);

    bench_section("File Descriptor Syscalls");
    bench_syscall_open_close(SYSCALL_ITERATIONS / 10);
    bench_syscall_fcntl(SYSCALL_ITERATIONS);
    bench_syscall_uiodup(SYSCALL_ITERATIONS / 10);

    bench_section("I/O Syscalls");
    bench_syscall_read(SYSCALL_ITERATIONS);
    bench_syscall_write(SYSCALL_ITERATIONS / 10);

    bench_section("IPC Syscalls");
    bench_syscall_pipe(SYSCALL_ITERATIONS / 10);

    bench_section("Process Syscalls");
    bench_syscall_fork(20);
    bench_syscall_wait(20);

    bench_section("Throughput");
    bench_syscall_throughput();

    bench_suite_end();
    _exit();
}
