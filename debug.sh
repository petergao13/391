#!/bin/bash
# Build the OS and start QEMU with a GDB server on port 12345.
# Run ./gdb.sh in a second terminal to attach the debugger.
set -e
cd "$(dirname "$0")"

echo "==> Building Docker image (cached after first run)..."
docker build -t wonton-os . 2>&1 | tail -1

echo ""
echo "==> Building OS and starting QEMU in debug mode..."
echo "    Open a second terminal and run: ./gdb.sh"
echo "(Ctrl-A X to quit QEMU)"
echo ""

docker rm -f wonton-debug 2>/dev/null || true

docker run --rm -it \
    --platform linux/amd64 \
    -v "$(pwd):/work" \
    --name wonton-debug \
    wonton-os bash -c '
        set -e
        cd /work
        make -C usr clean all
        ./util/mkfs_ktfs sys/ktfs.raw 8M 32 \
            usr/bin/shell  \
            usr/bin/date   \
            usr/bin/echo   \
            usr/bin/cat    \
            usr/bin/ls     \
            usr/bin/wc     \
            usr/bin/touch  \
            usr/bin/rm     \
            usr/bin/xargs  \
            usr/bin/hello  \
            usr/games/trek
        make -C sys clean
        make -C sys debug
    '
