# =====================================================================
# Stage 1: build patched QEMU 9.0.2
#
# The UIUC ECE 391 kernel expects the RISC-V virt machine to expose 8
# NS16550 UARTs (0x10000000..0x10000700) and to put the goldfish RTC on
# IRQ 9. Stock QEMU only maps a single UART at 0x10000000, so the kernel
# faults at 0x10000101 (UART1 IER) the moment it tries to initialise
# UART1. Imports/qemu.patch — the official course patch — extends the
# virt machine's memory map and IRQ assignments to match.
# =====================================================================
FROM --platform=linux/amd64 debian:bookworm-slim AS qemu-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        git \
        ca-certificates \
        build-essential \
        ninja-build \
        meson \
        pkg-config \
        python3 \
        python3-venv \
        flex \
        bison \
        libglib2.0-dev \
        libpixman-1-dev \
        libslirp-dev \
    && rm -rf /var/lib/apt/lists/*

COPY Imports/qemu.patch /tmp/qemu.patch

RUN git clone --depth 1 --branch v9.0.2 https://github.com/qemu/qemu /tmp/qemu \
    && cd /tmp/qemu \
    && patch -p0 < /tmp/qemu.patch \
    && ./configure --prefix=/opt/qemu \
        --target-list=riscv64-softmmu \
        --enable-system \
        --enable-slirp \
        --disable-docs \
        --disable-tools \
        --disable-werror \
    && make -j"$(nproc)" \
    && make install \
    && rm -rf /tmp/qemu /tmp/qemu.patch

# =====================================================================
# Stage 2: final dev image
# =====================================================================
FROM --platform=linux/amd64 debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-riscv64-unknown-elf \
        binutils-riscv64-unknown-elf \
        make \
        build-essential \
        git \
        file \
        gdb-multiarch \
        libglib2.0-0 \
        libpixman-1-0 \
        libslirp0 \
    && rm -rf /var/lib/apt/lists/*

# Pull the patched QEMU in from stage 1.
COPY --from=qemu-builder /opt/qemu /opt/qemu
ENV PATH="/opt/qemu/bin:${PATH}"

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
