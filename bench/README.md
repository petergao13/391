# 391 Kernel Benchmark Suite

A comprehensive benchmarking framework for the ECE 391 RISC-V kernel.

## Overview

This benchmark suite measures performance of key kernel subsystems:

- **Filesystem (KTFS)** - Sequential/random I/O, metadata operations
- **Processes** - Fork, exec, wait latency and throughput
- **Pipes** - IPC throughput and latency
- **Syscalls** - Individual syscall overhead
- **Memory** - Page fault handling, access patterns
- **Cache** - Block cache effectiveness

## Directory Structure

```
bench/
├── Makefile              # Build system
├── README.md             # This file
├── common/
│   ├── bench.h           # Benchmark utilities header
│   └── bench.c           # Timing and reporting functions
├── progs/
│   ├── bench_fs.c        # Filesystem benchmarks
│   ├── bench_proc.c      # Process benchmarks
│   ├── bench_pipe.c      # Pipe benchmarks
│   ├── bench_syscall.c   # Syscall latency benchmarks
│   ├── bench_mem.c       # Memory benchmarks
│   └── bench_cache.c     # Block cache benchmarks
└── bin/                  # Built binaries (created by make)
```

## Building

### Prerequisites

1. RISC-V toolchain (`riscv64-unknown-elf-gcc`)
2. User library built in `../usr/`

### Build Commands

```bash
# Build all benchmarks
cd bench
make

# Install to usr/bin/ for filesystem inclusion
make install

# Clean build artifacts
make clean
```

### Adding to KTFS Image

After building, add the benchmarks to your filesystem image:

```bash
cd ../util
./mkfs_ktfs ../sys/ktfs.raw \
    ../usr/bin/shell \
    ../usr/bin/echo \
    ../bench/bin/bench_fs \
    ../bench/bin/bench_proc \
    ../bench/bin/bench_pipe \
    ../bench/bin/bench_syscall \
    ../bench/bin/bench_mem \
    ../bench/bin/bench_cache
```

Or use `make install` to copy binaries to `usr/bin/` and include them in your normal build.

## Running Benchmarks

From the 391 shell:

```bash
# Run individual benchmarks
bench_fs
bench_proc
bench_pipe
bench_syscall
bench_mem
bench_cache
```

## Output Format

All benchmarks output results in a consistent format:

```
[BENCH] <name>: <measurement>
```

Examples:
```
[BENCH] seq_write: 65536 bytes in 45230 us (1413 KB/s)
[BENCH] fork: 20 ops, avg=1523 us, min=1401 us, max=1892 us
[BENCH] syscall_usleep(1): 1000 ops, avg=12 us, min=10 us, max=45 us
```

## Benchmark Descriptions

### bench_fs - Filesystem Benchmarks

| Test | Description |
|------|-------------|
| `seq_write` | Sequential write throughput |
| `seq_read` | Sequential read throughput |
| `random_read` | Random block reads (tests cache) |
| `file_create` | File creation latency |
| `file_delete` | File deletion latency |
| `file_open` | File open latency |
| `file_close` | File close latency |
| `dir_listing` | Directory listing performance |

### bench_proc - Process Benchmarks

| Test | Description |
|------|-------------|
| `fork` | Fork syscall latency |
| `fork_exit_roundtrip` | Fork + exit + wait cycle |
| `fork_exec` | Fork + exec latency |
| `wait_after_exit` | Wait when child already exited |
| `process_throughput` | Max process creation rate |
| `context_switch` | Estimated context switch time |

### bench_pipe - Pipe Benchmarks

| Test | Description |
|------|-------------|
| `pipe_create` | Pipe creation latency |
| `pipe_destroy` | Pipe close latency |
| `pipe_throughput` | Large data transfer rate |
| `pipe_roundtrip` | Small message ping-pong latency |
| `pipeline_N_stages` | Multi-process pipeline |

### bench_syscall - Syscall Latency

| Test | Description |
|------|-------------|
| `syscall_usleep(1)` | Minimal syscall overhead |
| `syscall_open_close` | File descriptor operations |
| `syscall_read` | Read syscall |
| `syscall_write` | Write syscall |
| `syscall_fcntl_getpos` | Fcntl operations |
| `syscall_pipe` | Pipe creation |
| `syscall_uiodup` | FD duplication |
| `syscall_fork` | Fork overhead |
| `syscall_wait` | Wait overhead |
| `syscall_throughput` | Raw syscalls/second |

### bench_mem - Memory Benchmarks

| Test | Description |
|------|-------------|
| `page_fault` | Demand paging latency |
| `mem_seq_write` | Sequential memory write |
| `mem_seq_read` | Sequential memory read |
| `mem_stride_N` | Strided access (TLB behavior) |
| `mem_random_read` | Random memory access |
| `mem_copy` | Memory bandwidth |
| `stack_growth` | Stack page fault handling |
| `ws_N_pages` | Working set behavior |

### bench_cache - Block Cache Benchmarks

| Test | Description |
|------|-------------|
| `cache_seq` | Sequential access (warm cache) |
| `cache_thrash_seq_N` | N-block sequential thrashing |
| `cache_thrash_rand_N` | N-block random thrashing |
| `ws_N_blocks` | Working set vs 64-block cache |
| `cache_locality` | Hot/cold access patterns |

## Timing Implementation

The benchmarks use the RISC-V `RDTIME` instruction which reads the `mtime` CSR. On QEMU virt machine, this runs at 10 MHz, giving microsecond resolution.

```c
// From common/bench.c
static inline unsigned long read_cycle(void) {
    unsigned long val;
    __asm__ volatile ("rdtime %0" : "=r"(val));
    return val;
}
```

## Interpreting Results

### Baseline Expectations

These are rough expectations for QEMU performance (actual hardware would differ):

| Metric | Typical Range |
|--------|---------------|
| Syscall overhead | 5-50 μs |
| Fork latency | 500-5000 μs |
| Pipe throughput | 1-10 MB/s |
| Page fault | 10-100 μs |
| Context switch | 50-500 μs |

### Cache Behavior

The block cache holds 64 blocks (32 KB with 512-byte blocks). Watch for:

- Working sets ≤64 blocks: should show good cache performance
- Working sets >64 blocks: expect increased latency from evictions

## Extending the Suite

### Adding a New Benchmark

1. Create `progs/bench_<name>.c`
2. Include `../common/bench.h`
3. Implement `int main(void)` with benchmark logic
4. Add to `BENCH_TARGETS` in Makefile

### Using the Benchmark API

```c
#include "../common/bench.h"

void my_benchmark(void) {
    // Method 1: Manual timing
    bench_time_t start = bench_get_time_us();
    // ... do work ...
    bench_time_t elapsed = bench_elapsed_us(start);
    bench_report_ops("my_test", num_ops, elapsed);

    // Method 2: BENCH_REPEAT macro
    BENCH_REPEAT("repeated_test", 100, {
        // code to benchmark
    });

    // Method 3: Statistics collection
    struct bench_stats stats;
    bench_stats_init(&stats);
    for (int i = 0; i < N; i++) {
        bench_time_t t;
        BENCH_TIME({ /* code */ }, t);
        bench_stats_add(&stats, t);
    }
    bench_report_latency("stats_test", &stats);
}
```

## Troubleshooting

### "failed to open file"
- Ensure the test file path exists (`c/...` for KTFS)
- Check that KTFS is mounted correctly

### Very high latencies
- Check if page faults are being triggered
- Verify cache is working (sequential re-reads should be fast)

### Benchmark hangs
- Pipe benchmarks require working fork/wait
- Check for deadlocks in IPC tests

## License

Part of ECE 391 coursework. See main project LICENSE.
