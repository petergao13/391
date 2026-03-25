#!/usr/bin/env bash
cd usr
make clean && make all


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
  usr/games/trek

cd ../sys
make clean && make run
