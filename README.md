# Team Wonton Kernel

A custom RISC-V operating system kernel with a user-mode shell, a block-cached filesystem (KTFS), a suite of UNIX-style user programs, and three classic terminal games (`trek`, `rogue`, `zork`). The kernel boots inside QEMU and exposes an interactive shell over a virtual serial port.

## Members
- ethanl10
- dk56 (danielkim11)
- peterg4 (peterg4a)

---

## Features
- RISC-V 64-bit kernel booted via QEMU (`-machine virt`, no BIOS).
- User-mode shell (`usr/progs/shell.c`) with:
  - Command execution, `|` pipes, `<` input redirection, `>` output redirection.
- Block-cached filesystem (KTFS) on a virtio block device, with a pin/refcount cache and adjacent-chunk coalescing in the page allocator.
- User programs: `ls`, `cat`, `echo`, `wc`, `date`, `touch`, `rm`, `xargs`, `hello`.
- Three pre-built games: `trek`, `rogue`, `zork`.

---

## Prerequisites

You need a RISC-V bare-metal GCC toolchain and `qemu-system-riscv64`.

### macOS (Homebrew)
```bash
brew tap riscv-software-src/riscv
brew install riscv-tools qemu screen
```
The toolchain must provide `riscv64-unknown-elf-gcc`, `riscv64-unknown-elf-as`, `riscv64-unknown-elf-ld`, `riscv64-unknown-elf-objcopy`, and `riscv64-unknown-elf-objdump`.

### Ubuntu / Debian
```bash
sudo apt update
sudo apt install -y \
  build-essential make git \
  gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
  qemu-system-misc screen
```

### Verify
```bash
riscv64-unknown-elf-gcc --version
qemu-system-riscv64 --version
screen --version
```

---

## Build and Boot (the one command a recruiter runs)

From the project root:
```bash
./mkfs-image.sh
```

This script will:
1. Build every user program in `usr/` into `usr/bin/`.
2. Pack `usr/bin/*` and all games in `usr/games/` into the filesystem image `sys/ktfs.raw` using `util/mkfs_ktfs`.
3. Build the kernel in `sys/` into `sys/kernel.elf`.
4. Launch the kernel in QEMU (`make -C sys run`).

When QEMU starts you will see the kernel boot logs and, somewhere in the output, a line like:
```
char device redirected to /dev/pts/N (label serial1)
```
Take note of that path (`/dev/pts/N`). The interactive shell runs on that virtual serial port, not on the main QEMU terminal.

---

## Connect to the Shell (two-terminal workflow)

The kernel uses two serial ports:
- `serial0` = QEMU monitor + boot logs (what you see in Terminal A).
- `serial1` = the shell console, exposed as a PTY (`/dev/pts/N`).

So you need a **second terminal**:

1. Keep Terminal A running `./mkfs-image.sh` (this is QEMU with the kernel booted).
2. Open Terminal B (split pane / new tab / new window).
3. In Terminal B, attach to the shell using the PTY from Terminal A:
   ```bash
   screen /dev/pts/N
   ```
   Replace `N` with the number QEMU printed.
4. Press **Enter** once. You should see:
   ```
   Starting 391 Shell
   LUMON OS>
   ```
5. Type a command name and press Enter (examples below).

---

## Running User Programs

Inside the shell (Terminal B), type the program name and press Enter.

| Command | What it does | Example |
|--------|--------------|---------|
| `hello` | Print a greeting | `hello` |
| `date` | Print the current date/time (reads the virtual RTC) | `date` |
| `echo` | Print arguments to the screen | `echo hello world` |
| `ls` | List files in the filesystem | `ls` |
| `cat` | Print the contents of a file | `cat hello` |
| `wc` | Line / word / byte count of a file | `wc hello` |
| `touch` | Create an empty file | `touch notes.txt` |
| `rm` | Delete a file | `rm notes.txt` |
| `xargs` | Run a command with args read from stdin | `echo hello | xargs echo` |

You can also use pipes and redirection inside the shell, e.g.:
```
echo hello world > notes.txt
cat notes.txt | wc
```

---

## Running the 3 Games

Inside the shell (Terminal B):

| Game | Command | How to exit |
|------|---------|-------------|
| Star Trek | `trek` | Type `quit` at the game prompt |
| Rogue | `rogue` | Press `Q` then `y` |
| Zork | `zork` | Type `quit` then `y` |

Notes:
- `zork` uses the data file `dtextc.dat`, which is packed into the filesystem automatically by `mkfs-image.sh`.
- All three games are interactive text-mode programs; the terminal running `screen /dev/pts/N` is where you play them.

---

## Exiting QEMU

- To quit the kernel / QEMU cleanly: in **Terminal A**, press **`Ctrl-a`** then **`x`** (this is the QEMU monitor escape sequence enabled by `-serial mon:stdio`).
- To just detach from the shell terminal without killing QEMU: in **Terminal B**, press **`Ctrl-a`** then **`d`** (screen detach). Reattach with `screen -r`.

---

## What the QEMU Command Looks Like

The kernel is launched via `make -C sys run`, which runs roughly:

```
qemu-system-riscv64 \
  -global virtio-mmio.force-legacy=false \
  -machine virt -bios none -nographic \
  -serial mon:stdio \
  -serial pty \
  -object rng-random,filename=/dev/urandom,id=rng0 \
  -device virtio-rng-device,rng=rng0 \
  -drive file=ktfs.raw,id=blk0,if=none,format=raw,readonly=false \
  -device virtio-blk-device,drive=blk0 \
  -monitor pty \
  -m 8M \
  -kernel kernel.elf
```

What each piece is for:
- `-machine virt -bios none`: Generic RISC-V virtual machine, no firmware (the kernel is the firmware).
- `-nographic`: No GUI window; everything is on serial.
- `-serial mon:stdio`: Boot logs + QEMU monitor on the terminal running the command.
- `-serial pty`: Second UART exposed as a host PTY (`/dev/pts/N`) — this is the shell console.
- `-drive file=ktfs.raw, -device virtio-blk-device`: Exposes `sys/ktfs.raw` as a virtio block device (the filesystem the kernel mounts).
- `-device virtio-rng-device`: Random number generator for the kernel.
- `-m 8M`: 8 MB of RAM.

---

## Project Structure

```
.
├── mkfs-image.sh      # one-command build + pack + boot
├── sys/               # kernel sources and Makefile
│   ├── kernel.ld      # kernel linker script
│   ├── ktfs.raw       # generated filesystem image (after build)
│   └── ...
├── usr/               # user-space sources
│   ├── progs/         # source for user programs and the shell
│   ├── games/         # prebuilt game binaries (trek, rogue, zork) + dtextc.dat
│   └── bin/           # built user programs (after build)
├── util/              # filesystem tooling (mkfs_ktfs, unmkfs_ktfs)
└── README.md
```

---

## Troubleshooting

- **`screen: /dev/pts/N: Permission denied`** — use `sudo screen /dev/pts/N`, or run the kernel as your own user and re-check the pts number QEMU printed.
- **`riscv64-unknown-elf-gcc: command not found`** — the toolchain is not installed or not on your PATH. Re-check Prerequisites.
- **QEMU exits immediately / no PTY printed** — make sure `./mkfs-image.sh` completed without errors before the `make -C sys run` step; the FS image must exist at `sys/ktfs.raw`.
- **Apple Silicon Macs** — `qemu-system-riscv64` runs natively (it is an emulator, not a VM), so Rosetta is not required.
