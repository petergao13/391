/*! @file bench_pipe.c
    @brief Pipe benchmarks
    @copyright Copyright (c) 2024-2025
*/

#include "../common/bench.h"

#define PIPE_BUF_SIZE 512
#define LARGE_TRANSFER (32 * 1024)  // 32 KB
#define PING_PONG_ITERATIONS 100

static char pipe_buf[PIPE_BUF_SIZE];

// ============================================================================
// Pipe Throughput Benchmark
// ============================================================================

/**
 * @brief Measure pipe write/read throughput
 * @param total_bytes Total bytes to transfer
 * @param chunk_size Size of each write/read operation
 */
void bench_pipe_throughput(unsigned long total_bytes, unsigned long chunk_size) {
    int wfd, rfd;
    
    if (_pipe(&wfd, &rfd) < 0) {
        printf("[BENCH] pipe_throughput: failed to create pipe\n");
        return;
    }

    // Fill buffer with pattern
    for (unsigned long i = 0; i < chunk_size && i < PIPE_BUF_SIZE; i++) {
        pipe_buf[i] = (char)(i & 0xFF);
    }

    int pid = _fork();
    if (pid == 0) {
        // Child: writer
        _close(rfd);  // Close read end
        
        unsigned long bytes_written = 0;
        while (bytes_written < total_bytes) {
            unsigned long to_write = chunk_size;
            if (bytes_written + to_write > total_bytes) {
                to_write = total_bytes - bytes_written;
            }
            long written = _write(wfd, pipe_buf, to_write);
            if (written <= 0) break;
            bytes_written += written;
        }
        
        _close(wfd);
        _exit();
    } else if (pid > 0) {
        // Parent: reader
        _close(wfd);  // Close write end

        unsigned long bytes_read = 0;
        bench_time_t start = bench_get_time_us();

        while (bytes_read < total_bytes) {
            long rd = _read(rfd, pipe_buf, chunk_size);
            if (rd <= 0) break;
            bytes_read += rd;
        }

        bench_time_t elapsed = bench_elapsed_us(start);
        _close(rfd);
        _wait(pid);

        bench_report_throughput("pipe_throughput", bytes_read, elapsed);
    }
}

// ============================================================================
// Pipe Latency Benchmark (Ping-Pong)
// ============================================================================

/**
 * @brief Measure pipe round-trip latency
 * @param iterations Number of ping-pong iterations
 */
void bench_pipe_latency(int iterations) {
    int wfd1, rfd1;  // Parent writes, child reads
    int wfd2, rfd2;  // Child writes, parent reads
    
    if (_pipe(&wfd1, &rfd1) < 0) {
        printf("[BENCH] pipe_latency: failed to create pipe 1\n");
        return;
    }
    if (_pipe(&wfd2, &rfd2) < 0) {
        printf("[BENCH] pipe_latency: failed to create pipe 2\n");
        _close(wfd1);
        _close(rfd1);
        return;
    }

    int pid = _fork();
    if (pid == 0) {
        // Child: receive ping, send pong
        char buf[1];
        _close(wfd1);
        _close(rfd2);
        
        for (int i = 0; i < iterations; i++) {
            _read(rfd1, buf, 1);   // Receive ping
            _write(wfd2, buf, 1);  // Send pong
        }
        
        _close(rfd1);
        _close(wfd2);
        _exit();
    } else if (pid > 0) {
        // Parent: send ping, receive pong
        char buf[1] = {'P'};
        _close(rfd1);
        _close(wfd2);

        struct bench_stats stats;
        bench_stats_init(&stats);

        for (int i = 0; i < iterations; i++) {
            bench_time_t start = bench_get_time_us();
            _write(wfd1, buf, 1);  // Send ping
            _read(rfd2, buf, 1);   // Receive pong
            bench_stats_add(&stats, bench_elapsed_us(start));
        }

        _close(wfd1);
        _close(rfd2);
        _wait(pid);

        bench_report_latency("pipe_roundtrip", &stats);
    }
}

// ============================================================================
// Pipe Create/Destroy Benchmark
// ============================================================================

/**
 * @brief Measure pipe creation and destruction latency
 * @param iterations Number of create/destroy cycles
 */
void bench_pipe_create_destroy(int iterations) {
    struct bench_stats create_stats, destroy_stats;
    bench_stats_init(&create_stats);
    bench_stats_init(&destroy_stats);

    for (int i = 0; i < iterations; i++) {
        int wfd, rfd;
        
        bench_time_t start = bench_get_time_us();
        int ret = _pipe(&wfd, &rfd);
        bench_stats_add(&create_stats, bench_elapsed_us(start));

        if (ret >= 0) {
            start = bench_get_time_us();
            _close(wfd);
            _close(rfd);
            bench_stats_add(&destroy_stats, bench_elapsed_us(start));
        }
    }

    bench_report_latency("pipe_create", &create_stats);
    bench_report_latency("pipe_destroy", &destroy_stats);
}

// ============================================================================
// Multi-Stage Pipeline Benchmark
// ============================================================================

/**
 * @brief Measure multi-stage pipeline throughput
 * @param stages Number of pipeline stages
 * @param bytes_per_stage Bytes to pass through pipeline
 */
void bench_pipeline_stages(int stages, unsigned long bytes_per_stage) {
    if (stages < 2 || stages > 8) {
        printf("[BENCH] pipeline: stages must be 2-8\n");
        return;
    }

    // Create pipes between stages
    int pipes[8][2];  // [stage][0=write, 1=read]
    for (int i = 0; i < stages - 1; i++) {
        if (_pipe(&pipes[i][0], &pipes[i][1]) < 0) {
            printf("[BENCH] pipeline: failed to create pipe %d\n", i);
            // Cleanup already created pipes
            for (int j = 0; j < i; j++) {
                _close(pipes[j][0]);
                _close(pipes[j][1]);
            }
            return;
        }
    }

    bench_time_t start = bench_get_time_us();

    // Fork intermediate stages (all but first and last)
    int pids[8];
    int num_children = 0;
    
    for (int stage = 1; stage < stages - 1; stage++) {
        int pid = _fork();
        if (pid == 0) {
            // Child: read from previous pipe, write to next pipe
            // Close unused pipe ends
            for (int i = 0; i < stages - 1; i++) {
                if (i != stage - 1) _close(pipes[i][1]);  // Close read ends we don't use
                if (i != stage) _close(pipes[i][0]);      // Close write ends we don't use
            }
            
            // Pass data through
            char buf[PIPE_BUF_SIZE];
            for (;;) {
                long rd = _read(pipes[stage-1][1], buf, PIPE_BUF_SIZE);
                if (rd <= 0) break;
                _write(pipes[stage][0], buf, rd);
            }
            
            _close(pipes[stage-1][1]);
            _close(pipes[stage][0]);
            _exit();
        } else if (pid > 0) {
            pids[num_children++] = pid;
        }
    }

    // Fork the last stage (reader)
    int pid = _fork();
    if (pid == 0) {
        // Last stage: just read and discard
        char buf[PIPE_BUF_SIZE];
        // Close all write ends and unused read ends
        for (int i = 0; i < stages - 1; i++) {
            _close(pipes[i][0]);
            if (i != stages - 2) _close(pipes[i][1]);
        }
        
        unsigned long total = 0;
        for (;;) {
            long rd = _read(pipes[stages-2][1], buf, PIPE_BUF_SIZE);
            if (rd <= 0) break;
            total += rd;
        }
        _close(pipes[stages-2][1]);
        _exit();
    } else if (pid > 0) {
        pids[num_children++] = pid;
    }

    // First stage (parent): write data
    // Close all read ends and unused write ends
    for (int i = 0; i < stages - 1; i++) {
        _close(pipes[i][1]);
        if (i != 0) _close(pipes[i][0]);
    }

    // Write data
    unsigned long written = 0;
    while (written < bytes_per_stage) {
        unsigned long to_write = PIPE_BUF_SIZE;
        if (written + to_write > bytes_per_stage) {
            to_write = bytes_per_stage - written;
        }
        long w = _write(pipes[0][0], pipe_buf, to_write);
        if (w <= 0) break;
        written += w;
    }
    _close(pipes[0][0]);

    // Wait for all children
    for (int i = 0; i < num_children; i++) {
        _wait(pids[i]);
    }

    bench_time_t elapsed = bench_elapsed_us(start);
    printf("[BENCH] pipeline_%d_stages: %lu bytes in %lu us\n", stages, bytes_per_stage, elapsed);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    bench_suite_start("Pipe Benchmarks");

    bench_section("Pipe Creation/Destruction");
    bench_pipe_create_destroy(50);

    bench_section("Pipe Latency");
    bench_pipe_latency(PING_PONG_ITERATIONS);

    bench_section("Pipe Throughput");
    bench_pipe_throughput(LARGE_TRANSFER, PIPE_BUF_SIZE);

    bench_section("Multi-Stage Pipeline");
    bench_pipeline_stages(3, 8192);

    bench_suite_end();
    _exit();
}
