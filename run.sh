#!/bin/bash
# Build the OS and run it in QEMU.
# Press Ctrl-A X to quit QEMU, Ctrl-A C to toggle the QEMU monitor.
set -e
cd "$(dirname "$0")"

echo "==> Building Docker image (cached after first run)..."
docker build -t wonton-os . 2>&1 | tail -1

echo ""
echo "==> Building OS and launching QEMU..."
echo "(Ctrl-A X to quit, Ctrl-A C to toggle QEMU monitor)"
echo ""

docker run --rm -it \
    --platform linux/amd64 \
    -v "$(pwd):/work" \
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
        make -C sys run
    '
