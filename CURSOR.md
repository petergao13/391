# Cursor Context

## Project Overview
- RISC-V teaching OS project with kernel code in `sys/` and user programs in `usr/`.
- User binaries are built into `usr/bin/` and packed into the KTFS image used by the kernel.

## Common Build and Run Flow
- Build user programs: `make -C usr`
- Build kernel: `make -C sys`
- Full image + run flow: `./mkfs-image.sh`

## Important Paths
- Kernel sources: `sys/`
- User program sources: `usr/progs/`
- User binary output: `usr/bin/`
- KTFS image: `sys/ktfs.raw`
- Filesystem tooling: `util/mkfs_ktfs`, `util/unmkfs_ktfs`

## Notes
- `mkfs-image.sh` is the root helper script to run the standard workflow.
- Shell and core user utilities live in `usr/progs/`.
