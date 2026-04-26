# Team Wonton Kernel

## Members
- ethanl10
- dk56 (danielkim11)
- peterg4 (peterg4a)

---

## Running the OS

### On macOS (Docker)

Docker is required. Start Docker Desktop, then:

```bash
./run.sh        # build and launch the OS in QEMU
./debug.sh      # build and launch with GDB server on :12345
./gdb.sh        # attach gdb-multiarch (second terminal, while debug.sh is running)
./shell.sh      # open a bash shell inside the build container
```

The scripts build a `linux/amd64` container (Debian Bookworm) with the full
RISC-V toolchain and QEMU. All build artifacts stay in the project directory.

Press **Ctrl-A X** to quit QEMU. Press **Ctrl-A C** to toggle the QEMU monitor.

### On Linux (native)

Install the toolchain:

```bash
sudo apt-get install gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
    qemu-system-misc make
```

Build and run:

```bash
make -C usr clean all

./util/mkfs_ktfs sys/ktfs.raw 8M 32 \
    usr/bin/shell usr/bin/date usr/bin/echo usr/bin/cat \
    usr/bin/ls usr/bin/wc usr/bin/touch usr/bin/rm \
    usr/bin/xargs usr/bin/hello usr/games/trek

make -C sys clean run
```

For a debug build (GDB server on :12345):

```bash
make -C sys debug
# in a second terminal:
gdb-multiarch -ex "set architecture riscv:rv64" \
              -ex "target remote localhost:12345" \
              sys/kernel.elf
```
