# Lightweight Kernel Log (ECE 391 Team Wonton Kernel)

This file tracks **lightweight-ness metrics** (kernel image size + boot time) for this kernel, and documents what we did to measure them so far.

## What we measured (current workspace)

### Kernel image size

**Primary artifact**: `sys/kernel.elf`

- **On-disk size (debug build)**: **365 KiB** (`372,464` bytes)
- **Runtime-relevant footprint (sections)**: **103,672 bytes (~101.2 KiB)**
  - `.text + .rodata` (“text” reported by `size`): `78,056` bytes
  - `.data`: `9,056` bytes
  - `.bss`: `16,560` bytes

**Other useful “image size” views**

- **Stripped ELF (debug removed)**: `113 KiB` (still loads the same runtime sections; just removes `.debug_*`)
- **Flat binary (no ELF/debug headers)**: `90,976` bytes (~88.9 KiB)

### Boot time (QEMU virt)

We added a small boot-time print to the kernel and measured:

- **Boot time to first user program (`shell`)**: **3,961 µs (~4.0 ms)** on QEMU `virt`

**Definition of “boot time” used here**

Time from entering `main()` in `sys/main.c` until just before `process_exec("shell")` (the first user-mode program) is invoked.

## What we changed in the kernel

### Boot-time instrumentation

We added a timer-based print in `sys/main.c` using the kernel’s time CSR:

- `rdtime()` provides tick count
- conversion to microseconds uses `TIMER_FREQ` (Hz)

The output format is:

```
[BOOT] kernel init: <us> us (<ticks> ticks @ <TIMER_FREQ> Hz)
```

## How to reproduce (copy/paste)

### Build the kernel

```bash
cd /home/gaoyujia/Desktop/391/sys
make clean
make all
```

### Measure kernel size (ELF sections)

```bash
cd /home/gaoyujia/Desktop/391
ls -lh sys/kernel.elf
riscv64-unknown-elf-size sys/kernel.elf
riscv64-unknown-elf-size -A sys/kernel.elf
```

### Measure “stripped” and “flat binary” sizes

```bash
cd /home/gaoyujia/Desktop/391
riscv64-unknown-elf-objcopy --strip-debug sys/kernel.elf /tmp/kernel.stripped.elf
ls -lh /tmp/kernel.stripped.elf

riscv64-unknown-elf-objcopy -O binary sys/kernel.elf /tmp/kernel.bin
ls -lh /tmp/kernel.bin
```

### Measure boot time (capture the boot line and exit)

```bash
cd /home/gaoyujia/Desktop/391/sys
timeout 6s make run
```

You should see something like:

```
[BOOT] kernel init: 3961 us (39860 ticks @ 10000000 Hz)
```

## Notes / caveats

- These numbers are from **QEMU**, not real hardware, but they are still very useful for comparing kernel changes over time.
- `sys/kernel.elf` is currently built **with debug info**; that inflates on-disk size. The section-based numbers (`text/data/bss`) reflect what the kernel actually needs at runtime.
- The filesystem image `sys/ktfs.raw` is **8 MiB** by construction (not a kernel size metric).

## Next metrics to add (optional)

- Track boot time across commits / milestones (e.g., “MP1/MP2/MP3”).
- Add a second metric for “time to first shell prompt” if the shell prints a reliable banner/prompt line we can detect.
