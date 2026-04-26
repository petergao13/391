FROM --platform=linux/amd64 debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-riscv64-unknown-elf \
    binutils-riscv64-unknown-elf \
    qemu-system-misc \
    make \
    build-essential \
    git \
    file \
    gdb-multiarch \
    && rm -rf /var/lib/apt/lists/*

# The Bookworm bare-metal toolchain ships without newlib/libc headers.
# Provide the macOS/BSD-internal headers that the kernel source #includes
# so the project compiles without modification.
RUN INCDIR=/usr/lib/gcc/riscv64-unknown-elf/12.2.0/include && \
    mkdir -p "$INCDIR/sys" && \
    # sys/_intsup.h — macOS internal; stdint.h already supplies everything
    printf '#ifndef _SYS_INTSUP_H\n#define _SYS_INTSUP_H\n#endif\n' \
        > "$INCDIR/sys/_intsup.h" && \
    # sys/_types.h — macOS internal primitive types; all already in stddef.h
    printf '#ifndef _SYS__TYPES_H\n#define _SYS__TYPES_H\n#include <stddef.h>\n#endif\n' \
        > "$INCDIR/sys/_types.h" && \
    # sys/types.h — minimal POSIX subset used by the kernel
    printf '#ifndef _SYS_TYPES_H\n#define _SYS_TYPES_H\n#include <stddef.h>\n#include <sys/_types.h>\ntypedef long ssize_t;\ntypedef long off_t;\ntypedef unsigned int mode_t;\ntypedef unsigned int uid_t;\ntypedef unsigned int gid_t;\n#endif\n' \
        > "$INCDIR/sys/types.h"

WORKDIR /work
CMD ["/bin/bash"]
