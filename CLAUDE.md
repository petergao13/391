# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

A RISC-V 64-bit teaching OS kernel ("Team Wonton Kernel") that boots in QEMU with a user-mode shell, a block-cached filesystem (KTFS), and UNIX-style user programs. See [context.md](context.md) for the full agent-oriented reference (failure modes, sanity commands, file-by-file layout).

## Platform requirement

Builds and boots **only on x86-64 Linux** (verified: Ubuntu). Requires `riscv64-unknown-elf-gcc`, `qemu-system-riscv64`, and `screen`. The FS tools `util/mkfs_ktfs` / `util/unmkfs_ktfs` are prebuilt x86-64 Linux ELFs (no source in repo) — on macOS/ARM hosts, use an x86-64 Linux container. This repo may be open on a macOS machine; do not expect builds to succeed locally.

## Commands

```bash
./mkfs-image.sh              # Full flow: build usr/, pack sys/ktfs.raw, build sys/, boot QEMU

make -C usr                  # Build user programs into usr/bin/
make -C sys                  # Build sys/kernel.elf only
make -C sys run              # Boot kernel in QEMU (needs sys/ktfs.raw to exist)
make -C sys debug            # Boot halted with gdb server on tcp::12345

make -C sys run-test         # Kernel test build: links sys/tests/test_main.c +
make -C sys debug-test       #   TEST_SUITE_OBJS (see sys/Makefile) instead of main.c

# Pack/inspect a filesystem image manually
util/mkfs_ktfs sys/ktfs.raw 8M 32 usr/bin/* usr/games/* usr/games/dtextc.dat
util/unmkfs_ktfs /tmp/ktfs_inspect sys/ktfs.raw   # unpack image to inspect contents
```

There is no lint step. Kernel builds with `-Werror=implicit-function-declaration`. Per-subsystem debug output is enabled by uncommenting `-D<SUBSYS>_DEBUG/-D<SUBSYS>_TRACE` CFLAGS lines in `sys/Makefile`.

**Two-terminal run workflow:** QEMU's stdout (serial0) shows boot logs and prints `char device redirected to /dev/pts/N (label serial1)`. The interactive shell (`LUMON OS>`) lives on that PTY — attach from a second terminal with `screen /dev/pts/N`. Exit QEMU with `Ctrl-a x`; detach screen with `Ctrl-a d`.

## Architecture

Two independently built worlds, glued together by the KTFS image:

- **`sys/`** — bare-metal kernel (rv64imazicsr, `-bios none`). Boot: `start.s` → `main.c`, which inits devices/FS and execs `shell` as PID 1. Trap path: `trap.c`/`excp.c`/`intr.c` → `syscall.c` dispatch. `process.c` (fork/exec) sits on `thread.c` + `thrasm.s` (context switch). `memory.c` is the page allocator.
- **`usr/`** — freestanding user programs in `usr/progs/`, linked against a mini-libc (`string.c`, `heap.c`, `uio.c`, `syscall.S`) via `umode.ld`, output to `usr/bin/`. To add a program: create `usr/progs/<name>.c`, add `<name>` to `ALL_TARGETS` in `usr/Makefile`, and add `usr/bin/<name>` to the `mkfs_ktfs` list in `mkfs-image.sh`.

**Storage stack** (top to bottom): `filesys.c` (generic mount/lookup) → `ktfs.c` (inodes, bitmaps, direct/indirect/double-indirect blocks; on-disk structs in `fsimpl.h`) → `cache.c` (refcounted, pin-aware block cache) → `dev/vioblk.c` (virtio block) → `dev/virtio.c` (virtio-mmio transport). `device.c` manages device registration; other drivers in `sys/dev/` (uart, rtc, viorng, ramdisk).

**Name resolution in the shell** (`usr/progs/shell.c`): a bare command `foo` resolves to `c/foo` (KTFS namespace — `mkfs_ktfs` stores files by basename only); `dev/` is the device namespace (`dev/uart1`, `dev/rtc0`); `//` is the listing namespace used by `ls`. The shell opens `dev/uart1` as its console — this depends on the `-serial pty` line in `sys/Makefile`'s QEMUOPTS.

## Invariants (do not break)

- Every successful `cache_get_block` needs a matching `cache_release_block` on every path, or eviction eventually fails with `-EBUSY`.
- `memory.c`'s free list must stay sorted by physical address — adjacent-chunk coalescing depends on it; violations cause silent corruption.
- `main.c` execs `shell` at boot, so `usr/bin/shell` must stay in the `mkfs_ktfs` file list in `mkfs-image.sh`; `zork` additionally needs `usr/games/dtextc.dat` in the image.
