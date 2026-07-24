#!/usr/bin/env bash
# macOS equivalent of mkfs-image.sh for CA-interview prep drills.
#   - usr/ and sys/ build natively (homebrew riscv64-unknown-elf-gcc)
#   - util/mkfs_ktfs (x86-64 Linux ELF) runs in an amd64 Docker container
#   - QEMU is the class-patched 9.0.2 build (8-UART virt machine)
# Usage: ./mkfs-image-mac.sh [debug]     ("debug" boots halted w/ gdb on :12345)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

QEMU_BIN="${ECE391_QEMU:-$HOME/.local/ece391-qemu/bin/qemu-system-riscv64}"
[ -x "$QEMU_BIN" ] || { echo "class QEMU not found at $QEMU_BIN" >&2; exit 1; }

# Docker daemon (needed only for mkfs_ktfs)
docker info >/dev/null 2>&1 || {
  echo "starting Docker Desktop..."
  open -a Docker
  for _ in $(seq 1 30); do docker info >/dev/null 2>&1 && break; sleep 2; done
  docker info >/dev/null 2>&1 || { echo "Docker daemon failed to start" >&2; exit 1; }
}

make -C usr clean
make -C usr all

docker run --rm --platform linux/amd64 -v "$ROOT":/w -w /w ubuntu:24.04 \
  ./util/mkfs_ktfs sys/ktfs.raw 8M 32 \
  usr/bin/shell \
  usr/bin/date \
  usr/bin/echo \
  usr/bin/cat \
  usr/bin/ls \
  usr/bin/wc \
  usr/bin/touch \
  usr/bin/rm \
  usr/bin/xargs \
  usr/bin/hello \
  usr/games/trek \
  usr/games/rogue \
  usr/games/zork \
  usr/games/dtextc.dat

make -C sys clean
exec make -C sys "${1:-run}" QEMU="$QEMU_BIN"
