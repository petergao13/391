# Agent Context: Team Wonton Kernel

This file is written for an AI coding agent (or any new contributor) who needs to
navigate this project and get it running **on a Linux development machine**.

## Verified platform (only one known to work)

| Component | Version |
|-----------|---------|
| Host OS | Ubuntu Linux, kernel `6.17.0-1017-oem`, x86-64 |
| Toolchain | `riscv64-unknown-elf-gcc 14.2.0 (g04696df09)` |
| QEMU | `qemu-system-riscv64 9.0.2` |
| `screen` | `4.09.01` |
| `make` | GNU Make (any recent version) |

**Do not assume macOS, Windows/WSL, or ARM Linux works.** The filesystem tooling in
`util/` is shipped as a prebuilt x86-64 Linux ELF, so an agent working on a
non-x86-64 host must either:
- run the project inside an x86-64 Linux container, or
- rebuild `util/mkfs_ktfs` / `util/unmkfs_ktfs` from source for that host (sources
  are not currently in this repo).

Confirm the util binaries before first build:
```bash
file util/mkfs_ktfs util/unmkfs_ktfs
# Expected: "ELF 64-bit LSB executable, x86-64, ..."
```

## Project layout (authoritative)

```
.
├── mkfs-image.sh          # one-command: build usr, pack ktfs.raw, build sys, run QEMU
├── README.md              # human-facing docs (recruiter-friendly)
├── context.md             # this file
├── CURSOR.md              # short Cursor workspace hints
├── util/
│   ├── mkfs_ktfs          # x86-64 Linux ELF. Packs host files into sys/ktfs.raw.
│   └── unmkfs_ktfs        # x86-64 Linux ELF. Unpacks a ktfs.raw into a directory.
├── sys/                   # Kernel (RISC-V 64, bare metal)
│   ├── Makefile           # Builds kernel.elf and launches QEMU.
│   ├── kernel.ld          # Kernel linker script.
│   ├── start.s            # Kernel entry point.
│   ├── main.c             # Kernel init + boots init process (shell).
│   ├── memory.c/.h        # Physical/virtual memory manager; free list with
│   │                      # address-sorted insertion and adjacent-chunk coalescing.
│   ├── cache.c/.h         # Block cache on top of a storage device. Refcount /
│   │                      # pin-aware LRU-ish eviction.
│   ├── ktfs.c/.h          # KTFS filesystem (inodes, bitmaps, direct/indirect/
│   │                      # double-indirect blocks, directory entries).
│   ├── filesys.c/.h       # Generic FS mount and lookup layer.
│   ├── fsimpl.h           # KTFS on-disk structs (superblock, inode, etc.).
│   ├── device.c/.h        # Device manager.
│   ├── dev/
│   │   ├── uart.c         # 16550-style UART driver.
│   │   ├── rtc.c          # virtio RTC driver (used by date).
│   │   ├── virtio.c       # virtio-mmio transport.
│   │   ├── vioblk.c       # virtio block device driver (backs KTFS).
│   │   ├── viorng.c       # virtio RNG driver.
│   │   └── ramdisk.c      # In-memory block device.
│   ├── thread.c/.h, thrasm.s  # Threads + context switch.
│   ├── process.c/.h       # User processes, fork, exec.
│   ├── syscall.c          # System call dispatch.
│   ├── intr.c, excp.c, trap.c, plic.c, timer.c
│   ├── heap0.c, misc.c, error.c, string.c, console.c, uio.c
│   └── ktfs.raw           # Generated filesystem image (produced by mkfs-image.sh).
└── usr/                   # User space (RISC-V 64)
    ├── Makefile           # Builds every program under progs/ into usr/bin/.
    ├── umode.ld, no_umode.ld  # Linker scripts for user programs.
    ├── start.s            # User program entry stub (calls main, then _exit).
    ├── syscall.S / syscall.h  # Syscall wrappers.
    ├── string.c/.h        # printf, dprintf, snprintf, strcmp, etc.
    ├── heap.c/.h          # User malloc/free.
    ├── uio.c/.h           # Buffered I/O helpers.
    ├── shell.h            # Shell-wide constants (FIN '<', FOUT '>', PIPE '|',
    │                      # STDIN 0, STDOUT 1, CONSOLEOUT 2).
    ├── progs/
    │   ├── shell.c        # The interactive shell. Maps STDIN/STDOUT to
    │   │                  # dev/uart1 (CONSOLEOUT). Parses pipelines and
    │   │                  # redirection, forks + execs each stage.
    │   ├── hello.c, echo.c, cat.c, ls.c, wc.c, date.c, touch.c, rm.c, xargs.c
    │   └── test.c, test1.c, test2.c, test3.c     # Kernel stress tests.
    ├── bin/               # Output of `make -C usr` (built RISC-V ELFs).
    └── games/
        ├── trek           # RISC-V ELF (BSD Star Trek).
        ├── rogue          # RISC-V ELF (Rogue).
        ├── zork           # RISC-V ELF (Zork).
        └── dtextc.dat     # Data file required by zork.
```

## How the shell finds programs (important when debugging exec)

- `usr/progs/shell.c`'s `dup_resolve_prog(word)` rule:
  - If `word` contains a `/`, use it as-is.
  - Otherwise, prefix with `c/` (the KTFS namespace).
- `util/mkfs_ktfs` stores files in KTFS **by basename only**. So
  `usr/games/trek` becomes filename `trek`, and the shell resolves the user
  typing `trek` to `c/trek`, which maps to the KTFS entry `trek`.
- Device namespace is `dev/` (e.g. `dev/uart1`, `dev/rtc0`).
- Listing namespace is `//` (used by `ls`).

If the shell reports "Cannot open specified file", verify that the binary was
packed into `sys/ktfs.raw`:
```bash
mkdir -p /tmp/ktfs_inspect
util/unmkfs_ktfs /tmp/ktfs_inspect sys/ktfs.raw
ls /tmp/ktfs_inspect
```

## Build + boot (the only workflow you should teach a new user)

```bash
./mkfs-image.sh
```
What it does, in order:
1. `make -C usr clean && make -C usr all` — builds every user program into
   `usr/bin/` using `riscv64-unknown-elf-gcc`.
2. `util/mkfs_ktfs sys/ktfs.raw 8M 32 usr/bin/* usr/games/*` — creates an 8 MB
   filesystem image with 32 inodes and packs the listed files.
3. `make -C sys clean && exec make -C sys run` — builds `sys/kernel.elf` and
   launches it under QEMU with the flags shown below.

The QEMU command `make -C sys run` executes (defined in `sys/Makefile`):
```
qemu-system-riscv64 \
  -global virtio-mmio.force-legacy=false \
  -machine virt -bios none -nographic \
  -serial mon:stdio \          # boot logs + QEMU monitor on Terminal A
  -serial pty \                # shell console exposed as /dev/pts/N
  -object rng-random,filename=/dev/urandom,id=rng0 \
  -device virtio-rng-device,rng=rng0 \
  -drive file=ktfs.raw,id=blk0,if=none,format=raw,readonly=false \
  -device virtio-blk-device,drive=blk0 \
  -monitor pty \
  -m 8M -kernel kernel.elf
```

## Two-terminal workflow (must communicate to any new user)

The kernel uses two UARTs. The shell does **not** talk to Terminal A. It talks
to Terminal B, which you attach via `screen`.

1. **Terminal A** runs `./mkfs-image.sh` and stays open. Somewhere in the boot
   output, QEMU prints a line like:
   ```
   char device redirected to /dev/pts/<N> (label serial1)
   ```
   The number `<N>` changes every boot.
2. **Terminal B** attaches to that PTY:
   ```bash
   screen /dev/pts/<N>
   ```
   Press Enter once to see `Starting 391 Shell` and `LUMON OS>`.
3. In Terminal B you can now run any user program or game by typing its name
   (no path, no args required for simple cases):
   - User programs: `hello`, `date`, `echo ...`, `ls`, `cat <file>`,
     `wc <file>`, `touch <file>`, `rm <file>`, `echo ... | xargs ...`.
   - Games: `trek`, `rogue`, `zork`.

Exiting:
- QEMU monitor escape is `Ctrl-a x` (from Terminal A). `Ctrl-a c` to enter the
  monitor, `quit` to exit.
- Detach `screen` without killing QEMU: `Ctrl-a d`. Reattach: `screen -r`.

## Common failure modes and their cause

| Symptom | Likely cause / fix |
|---------|--------------------|
| `riscv64-unknown-elf-gcc: command not found` | Install `gcc-riscv64-unknown-elf`, `binutils-riscv64-unknown-elf`. |
| `qemu-system-riscv64: command not found` | Install `qemu-system-misc` (Debian/Ubuntu). |
| `util/mkfs_ktfs: cannot execute` | Host is not x86-64 Linux. Use an x86-64 Linux container or rebuild from source. |
| Script prints nothing after `make -C sys run` | QEMU booted fine but logs are sparse. The PTY line is the important one; look for `(label serial1)`. |
| `screen: /dev/pts/<N>: Permission denied` | Try `sudo screen /dev/pts/<N>`, or check ownership. |
| Shell prints "Cannot open specified file" for a game | Game binary not packed into `sys/ktfs.raw`. Re-run `./mkfs-image.sh`. |
| Zork errors on startup | `dtextc.dat` is missing from the image. Confirm the list in `mkfs-image.sh`. |
| Kernel reaches a `panic("ktfs: ...")` | Probable cache or block-device failure. Check recent changes to `sys/cache.c` or `sys/ktfs.c`. |

## Invariants an agent must not break

- `util/mkfs_ktfs` invocation in `mkfs-image.sh` must keep `shell` **before**
  any file whose absence would be fatal at boot; `main.c` execs `shell` as PID
  1.
- `usr/progs/shell.c` explicitly opens `dev/uart1` as `CONSOLEOUT`. If you
  remove `-serial pty` from `sys/Makefile`, the shell will have no console.
- Cache blocks are pinned via refcount in `cache_get_block`. Any new caller of
  `cache_get_block` **must** have a matching `cache_release_block` on every
  success path, or eviction will eventually fail with `-EBUSY`.
- `memory.c`'s free list is kept sorted by physical address so coalescing is
  valid. Inserts that break that invariant will cause silent corruption.

## Where to look first when an agent is asked to fix something

- "Shell crashes / pipes hang" → `usr/progs/shell.c`.
- "`ls`/`cat`/`wc` broken" → `usr/progs/<name>.c`.
- "File not found / FS corruption" → `sys/ktfs.c`, then `sys/cache.c`.
- "Out of memory / crash on fork" → `sys/memory.c` (page allocator, coalescing).
- "Interrupts / timer / exceptions" → `sys/intr.c`, `sys/excp.c`, `sys/timer.c`,
  `sys/trap.c`, `sys/plic.c`.
- "Syscalls" → `sys/syscall.c` + `usr/syscall.S` + `usr/syscall.h`.
- "Process / exec / fork" → `sys/process.c`.
- "Boot" → `sys/start.s` → `sys/main.c`.
- "QEMU invocation" → `sys/Makefile` (`QEMUOPTS` and `run:` target).

## Quick sanity commands an agent can run on a fresh clone

```bash
# 1. Confirm tools.
riscv64-unknown-elf-gcc --version
qemu-system-riscv64 --version
screen --version
file util/mkfs_ktfs util/unmkfs_ktfs

# 2. Build everything without booting.
make -C usr clean && make -C usr
make -C sys clean && make -C sys

# 3. Pack a test image without booting QEMU.
util/mkfs_ktfs sys/ktfs.raw 8M 32 \
  usr/bin/shell usr/bin/ls usr/bin/cat \
  usr/games/trek usr/games/rogue usr/games/zork usr/games/dtextc.dat

# 4. Inspect what's inside the image.
rm -rf /tmp/ktfs_inspect && mkdir -p /tmp/ktfs_inspect
util/unmkfs_ktfs /tmp/ktfs_inspect sys/ktfs.raw
ls /tmp/ktfs_inspect

# 5. Boot (you will need a second terminal and `screen /dev/pts/<N>`).
./mkfs-image.sh
```
