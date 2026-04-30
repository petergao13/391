#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

make -C usr clean
make -C usr all

util/mkfs_ktfs sys/ktfs.raw 8M 32 \
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
exec make -C sys run
