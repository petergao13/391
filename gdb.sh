#!/bin/bash
# Attach gdb-multiarch to a running debug session started by ./debug.sh.
# Run this in a second terminal while debug.sh is running.
cd "$(dirname "$0")"

if ! docker ps --format '{{.Names}}' | grep -q '^wonton-debug$'; then
    echo "Error: no running wonton-debug container found."
    echo "Start one first with: ./debug.sh"
    exit 1
fi

echo "==> Attaching gdb-multiarch to QEMU GDB server..."
docker exec -it wonton-debug gdb-multiarch \
    -ex "set architecture riscv:rv64" \
    -ex "target remote localhost:12345" \
    /work/sys/kernel.elf
