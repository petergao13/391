# Makefile for 391 Kernel Benchmark Suite
#
# Usage:
#   make          - Build all benchmarks
#   make clean    - Remove build artifacts
#   make install  - Copy binaries to ../usr/bin/
#
# The benchmarks link against the user library from ../usr/

TOOLPREFIX = riscv64-unknown-elf-

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)as
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# Directories
USR_DIR = ../usr
COMMON_DIR = common
PROGS_DIR = progs
BIN_DIR = bin

# Compiler flags (matching usr/Makefile)
CFLAGS = -Wall -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64g -mabi=lp64d
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -I$(USR_DIR) -I.
CFLAGS += -DUMODE

# Linker script (from usr/)
ULIB_LD = $(USR_DIR)/umode.ld

# User library objects (build in usr/ first)
ULIB_OBJS = \
	$(USR_DIR)/start.o \
	$(USR_DIR)/uio.o \
	$(USR_DIR)/string.o \
	$(USR_DIR)/syscall.o \
	$(USR_DIR)/heap.o

# Benchmark common library
BENCH_COMMON = $(COMMON_DIR)/bench.o

# Benchmark programs
BENCH_TARGETS = \
	bench_fs \
	bench_proc \
	bench_pipe \
	bench_syscall \
	bench_mem \
	bench_cache

# Default target
all: $(BIN_DIR) $(addprefix $(BIN_DIR)/, $(BENCH_TARGETS))

# Create bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build common library
$(COMMON_DIR)/%.o: $(COMMON_DIR)/%.c $(COMMON_DIR)/bench.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Build benchmark programs
$(PROGS_DIR)/%.o: $(PROGS_DIR)/%.c $(COMMON_DIR)/bench.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Link benchmark binaries
$(BIN_DIR)/%: $(PROGS_DIR)/%.o $(BENCH_COMMON) $(ULIB_OBJS) | $(BIN_DIR)
	$(LD) -T $(ULIB_LD) -o $@ $^

# Install to usr/bin for inclusion in filesystem
install: all
	cp $(BIN_DIR)/* $(USR_DIR)/bin/

# Clean build artifacts
clean:
	rm -rf $(BIN_DIR)
	rm -f $(COMMON_DIR)/*.o
	rm -f $(PROGS_DIR)/*.o

# Make sure user library is built first
$(ULIB_OBJS):
	$(MAKE) -C $(USR_DIR) $(notdir $@)

# Phony targets
.PHONY: all clean install

# Dependencies
$(PROGS_DIR)/bench_fs.o: $(COMMON_DIR)/bench.h
$(PROGS_DIR)/bench_proc.o: $(COMMON_DIR)/bench.h
$(PROGS_DIR)/bench_pipe.o: $(COMMON_DIR)/bench.h
$(PROGS_DIR)/bench_syscall.o: $(COMMON_DIR)/bench.h
$(PROGS_DIR)/bench_mem.o: $(COMMON_DIR)/bench.h
$(PROGS_DIR)/bench_cache.o: $(COMMON_DIR)/bench.h
