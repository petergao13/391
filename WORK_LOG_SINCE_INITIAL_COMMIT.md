# Work log since initial commit

This document records all committed work after the repository’s first commit. The initial commit is:

- **`adff0fa`** — `initial commit`

All following changes are on branch **`lightweight-metrics`** (see [Branch layout](#branch-layout)).

---

## Summary by area

| Area | What changed |
|------|----------------|
| **`bench/`** | User-space benchmark suite: Makefile, README, wrapper script, shared `common/` library, six benchmark programs, prebuilt binaries under `bench/bin/`. |
| **Root docs** | `context.md` (project context), `PRD.md` (placeholder), `LIGHTWEIGHT_KERNEL_LOG.md` (metrics and methodology). |
| **`sys/`** | Boot-time instrumentation in `main.c`; `ktfs.raw` image updated with the bench tooling workflow. |

---

## Commits (chronological)

### 1. `280911b` — first working version

- **`bench/Makefile`** — Build rules for the benchmark programs and `bin/` outputs.
- **`bench/README.md`** — How to build and run benchmarks, program descriptions.
- **`bench/common/bench.c`**, **`bench/common/bench.h`** — Shared timing, stats, and reporting helpers for benchmarks.
- **`bench/progs/bench_*.c`** — Implementations for: syscall, proc, mem, pipe, fs, cache benchmarks.
- **`bench/bin/bench_*`** — Checked-in built executables for the six benchmarks.
- **`context.md`** — Long-form project/context notes (471 lines added).
- **`PRD.md`** — Empty placeholder file added.

### 2. `c8b0163` — added bench script

- **`bench/bench.sh`** — Script to drive building/running benchmarks (initial version included install-related steps).
- **`sys/ktfs.raw`** — Filesystem image touched as part of the bench workflow.

### 3. `e1676c7` — removed make install

- **`bench/Makefile`**, **`bench/README.md`**, **`bench/bench.sh`** — Removed `make install` from the documented and scripted flow; small cleanups.

### 4. `2c2681b` — Add boot-time instrumentation and lightweight kernel log

- **`sys/main.c`** — Records time at start of `main()` with `rdtime()`, prints elapsed µs and ticks after kernel init (before `run_init()`), using `TIMER_FREQ` for conversion.
- **`LIGHTWEIGHT_KERNEL_LOG.md`** — Documents kernel image size views, boot-time definition, QEMU measurement notes, and related methodology.

### 5. Add work log documenting changes since initial commit

- **`WORK_LOG_SINCE_INITIAL_COMMIT.md`** — This file: a single narrative of all work after `adff0fa` and how `main` vs `lightweight-metrics` are used. (Commit subject on `lightweight-metrics`: *Add work log documenting changes since initial commit*.)

---

## Files touched (full list vs initial)

From `git diff adff0fa..lightweight-metrics --name-only`:

- `WORK_LOG_SINCE_INITIAL_COMMIT.md`
- `LIGHTWEIGHT_KERNEL_LOG.md`
- `PRD.md`
- `bench/Makefile`
- `bench/README.md`
- `bench/bench.sh`
- `bench/bin/bench_cache`, `bench_fs`, `bench_mem`, `bench_pipe`, `bench_proc`, `bench_syscall`
- `bench/common/bench.c`, `bench/common/bench.h`
- `bench/progs/bench_cache.c`, `bench_fs.c`, `bench_mem.c`, `bench_pipe.c`, `bench_proc.c`, `bench_syscall.c`
- `context.md`
- `sys/ktfs.raw`
- `sys/main.c`

---

## Branch layout

- **`main`** — Intentionally kept at the **initial commit** (`adff0fa`) so the default branch stays the course baseline.
- **`lightweight-metrics`** — Contains the initial commit **plus** all commits listed above (bench suite, script/Makefile tweaks, boot instrumentation, and `LIGHTWEIGHT_KERNEL_LOG.md`).

To continue development: work on `lightweight-metrics` (or branch from it). To see the baseline tree only: `git checkout main`.

---

## This file

- **`WORK_LOG_SINCE_INITIAL_COMMIT.md`** (this document) — Maintained on `lightweight-metrics`; extend the numbered sections above when you add new commits.
