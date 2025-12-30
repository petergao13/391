### System model and execution environment

- Target architecture: **RISC-V** running under **QEMU**, kernel boots in **S-mode**; later checkpoints execute user programs in **U-mode** with traps back to S-mode.
    
- Kernel is a small Unix-like OS core (V6-inspired) with **devices + filesystem + processes + syscalls + scheduling**.
    

---

### High-level architecture (key abstractions)

**Uniform I/O (UIO) is the central unifying interface.**

- Kernel interacts with “file-like” endpoints via:
    
    - `struct uio` → has `intf` pointer to `struct uio_intf` function table
        
    - `uio_intf` contains function pointers: `close`, `read`/`fetch`, `write`/`store`, `cntl`
        
- Files, devices, listings, pipes, in-memory buffers all expose UIO endpoints.
    

**Mountpoints / filesystem multiplexer**

- `filesys.c` maps **mountpoint name → filesystem implementation**.
    
- Standard mountpoints:
    
    - `"dev"` → **devfs** (devices)
        
    - `"c"` → **ktfs** (course filesystem)
        
- `open` uses path `"mount/file"` (e.g., `"dev/uart1"`, `"c/trek"`); userspace passes a single string path, kernel parses it.
    

**Listing objects**

- If filename is `NULL` or `""`, `open` returns a **listing uio**:
    
    - filesys listing: lists mountpoints
        
    - devfs listing: provided
        
    - ktfs listing: implemented in CP3
        

---

### Device layer (MMIO / VIRTIO)

- Devices are exposed through devfs as UIO endpoints.
    
- Core devices in this MP:
    
    - **PLIC** (interrupt controller), **UART**, **RTC**, **timer**
        
    - **VIRTIO RNG** (from MP2)
        
    - **VIRTIO block device** (**vioblk**) (CP1)
        
    - **ramdisk** (CP1, for unit testing without vioblk)
        
- VIRTIO is via **MMIO registers**; vioblk has attach + ISR + storage operations.
    

---

### VIRTIO block device (vioblk) interface

Implement these (storage-style interface over virtio blk):

- `vioblk_attach(regs, irqno)`
    
- `vioblk_storage_open/close`
    
- `vioblk_storage_fetch(pos, buf, bytecnt)` and `..._store(...)`
    
- `vioblk_storage_cntl(op, arg)`
    
    - Explicit `FCNTL` case must print: `kprintf("MMAP is not a supported yet")`
        
- `vioblk_isr(irqno, aux)`
    

---

### Cache layer (between filesystem and backing storage)

A block cache sits between KTFS and its backing `struct storage` (vioblk or ramdisk).  
Required cache properties:

- **Capacity: 64 blocks total**
    
- Starts empty at boot.
    
- If you read 64 distinct blocks into an empty cache, **all 64 must remain cached** (no eviction during that fill).
    
- A block brought in by `cache_get_block()` must remain present **at least until the next** `cache_get_block()` call.
    

Required functions:

- `create_cache(struct storage *sto, struct cache **cptr)`
    
- `cache_get_block(cache, pos, void **pptr)` _(pos interpreted as block index / block-aligned position per your design)_
    
- `cache_release_block(cache, pblk, dirty)`
    
- `cache_flush(cache)` _(write back any dirty blocks if applicable)_
    

---

### KTFS filesystem (course FS)

**Block size is 512 bytes**, intentionally matching the VIRTIO device block size.

**On-disk layout**

- Superblock (block 0) + inode bitmap blocks + data bitmap blocks + inode blocks + data blocks.
    
- Superblock fields:
    
    - `block_count`
        
    - `inode_bitmap_block_count`
        
    - `bitmap_block_count`
        
    - `inode_block_count`
        
    - `root_directory_inode`
        

**Inodes**

- Store file size and block pointers:
    
    - **4 direct** data blocks
        
    - **1 indirect** block pointer (block of `uint32` data block numbers)
        
    - **2 doubly-indirect** block pointers (block of indirect block numbers → block numbers)
        

**Root directory / dentries**

- KTFS supports only a single directory (root).
    
- Directory data blocks contain **dentries**:
    
    - dentry size: **16B** = `uint16 inode` + `char name[14]`
        
    - filename max length **13 chars** (needs null terminator)
        
- Root directory inode `size = (#dentries) * 16`.
    
- Strict invariant: **dentries must be contiguous** (no holes after deletes).
    
- Strict invariant: **1:1:1 mapping** between file ↔ dentry ↔ inode; no duplicate inode numbers or filenames.
    

---

### KTFS driver: CP1 (read-only) + CP2/CP3 extensions

**CP1 KTFS functions**

- `mount_ktfs(name, cache*)`
    
- `ktfs_open(fs, name, struct uio **uioptr)`
    
- `ktfs_close(uio)`
    
- `ktfs_fetch(uio, buf, len)` _(advances file position)_
    
- `ktfs_cntl(uio, cmd, arg)`
    
- `ktfs_flush(fs)`
    

**CP1 required fcntl operations**

- `FCNTL_GETEND`, `FCNTL_GETPOS`, `FCNTL_SETPOS`
    
- Must track per-open-file `pos`; reads advance `pos` by bytes read.
    
- Explicit `FCNTL` case must print: `kprintf("MMAP is not a supported yet")`
    

**CP2 KTFS (CRUD)**

- Add:
    
    - `ktfs_store(uio, buf, len)`
        
    - `ktfs_create(fs, name)`
        
    - `ktfs_delete(fs, name)`
        
- Add `FCNTL_SETEND` (extend file size, allocate blocks as needed; support writes beyond end).
    
- Writes must persist to the filesystem image across QEMU shutdown (i.e., actually reach backing storage; beware write-back cache).
    
- **Important ordering constraint:** for writes, call `synchronize()` around **dirty cache releases** to prevent compiler reordering.
    

**CP3 KTFS listing**

- `ktfs_open` returns a **listing uio** when filename is `NULL` or `""`.
    
- Implement:
    
    - `ktfs_listing_read(uio, buf, bufsz)` _(returns next filename per read)_
        
    - `ktfs_listing_close(uio)`
        

---

### Ramdisk (for isolated testing)

- A storage device backed by the **kernel blob section** (`kimg_blob_start` → `kimg_blob_end`) to avoid vioblk dependencies.
    
- Implement:
    
    - `ramdisk_attach()`
        
    - `ramdisk_open/close`
        
    - `ramdisk_cntl(cmd, arg)` _(include same “MMAP not supported yet” behavior for FCNTL case)_
        
    - `ramdisk_fetch(pos, buf, bytecnt)`
        
- Useful for testing: cache + ktfs + ELF loader without virtio blk.
    

---

### ELF loading

- Loader should work over **any UIO endpoint** (file, ramdisk-backed uio, etc.).
    
- Only needs to interpret **program headers** (not ELF sections).
    
- **Critical constraint:** whenever calling a `uio` operation on a `uio` object, you **must call `synchronize()` before and after** the call (compiler reordering protection).
    

---

### Virtual memory (CP2+) — Sv39, demand paging, validation

- Paging scheme: **Sv39**, page tables are 3-level.
    
- Demand paging requirement:
    
    - Page faults **within user-owned VMA range** (`USER_START_VMA` to `USER_END_VMA`) → allocate page and map U-accessible.
        
    - Page faults outside user region → terminate user program.
        
- Physical page management uses a **free chunk list**:
    
    - Initially one chunk: `[heap_end (page-aligned), end_of_RAM)`
        
    - Allocation is **best-fit** over chunks (smallest chunk that satisfies `cnt` contiguous pages).
        
    - Freeing can reinsert chunks **without coalescing**.
        

Memory API functions to implement (memory.c / memory.h):

- initialization + resetting/tearing down active address spaces:
    
    - `memory_init()`
        
    - `reset_active_mspace()`
        
        - If level-0 PTE is not a leaf, panic: **“Sv48, Sv57 schemes not supported.”**
            
    - `discard_active_mspace()`
        
- physical allocation:
    
    - `alloc_phys_page`, `alloc_phys_pages(cnt)`
        
    - `free_phys_page`, `free_phys_pages(pp, cnt)`
        
    - `free_phys_page_count()`
        
- mapping:
    
    - `map_page(vma, pp, rwxug_flags)`
        
    - `map_range(vma, size, pp, rwxug_flags)`
        
    - `alloc_and_map_range(vma, size, rwxug_flags)`
        
    - `set_range_flags(vp, size, rwxug_flags)`
        
    - `unmap_and_free_range(vp, size)`
        
- validation (defensive syscalls):
    
    - `validate_vptr(vp, len, rwxug_flags)`
        
    - `validate_vstr(vs, rug_flags)`
        
- fault handler:
    
    - `handle_umode_page_fault(tfr, vma)`
        

User program mapping range for CP2+:

- Load user programs between **0x0C0000000** and **0x100000000** (update `USER_START_VMA/USER_END_VMA` accordingly).
    

---

### Process abstraction (CP2+) and thread linkage

- A **process** is a wrapper around a **kernel thread** plus:
    
    - `pid`
        
    - associated kernel `tid`
        
    - memory space identifier (mspace / page table root)
        
    - per-process UIO table (FDs for terminal/files/devices/buffers)
        
- Constraints:
    
    - Init process PID is **0**.
        
    - Max **16 concurrent processes**.
        
    - In CP2, all processes share the **main memory space**; in CP3, multiple memory spaces exist (after fork).
        

Process API (process.c):

- `procmgr_init()` (provided)
    
- `process_exec(exeio, argc, argv)`
    
    - Must `sfence_vma()` at start (TLB flush).
        
    - Unmap old user mappings (belonging to other processes).
        
    - Load executable from `exeio` into mapped pages; **close the executable uio after load**.
        
    - Transition thread into U-mode (typically via assembly helper in `trap.s`).
        
    - If `elf_load` returns:
        
        - `-EIO` → print **“The given UIO is invalid”** then exit
            
        - `-EBADFMT` → print **“Not a RISCV Executable”** then exit
            
- `process_exit()` cleans:
    
    - memory space
        
    - open UIO interfaces
        
    - associated kernel thread
        

Thread library additions (thread.c / thread.h):

- Add `struct process *proc` to thread struct.
    
- Implement:
    
    - `thread_process(tid)`
        
    - `running_thread_process()`
        
    - `thread_set_process(tid, proc)`
        

---

### Traps, syscalls, and privilege transitions

- U-mode traps enter `smode_trap_entry_from_umode` (trap.s):
    
    - Save registers into trap frame
        
    - Dispatch to exception vs interrupt
        
    - Restore registers and return with `sret`
        
- `handle_umode_exception` (excp.c):
    
    - Gracefully handle **page faults** and **ecall**
        
    - On unhandled exception or error: **terminate current process**
        
- All syscalls must defend against invalid user pointers using `validate_vptr/validate_vstr`.
    
- **All syscalls should start with `sfence_vma()`** when switching to kernel space (TLB hygiene).
    
- System calls required in CP2 include: `exit, print, open, close, read, write, fcntl, exec, wait, usleep, fscreate, fsdelete, uiodup`, plus dispatch (`handle_syscall`, `syscall`).
    

---

### CP3: fork, preemption, pipes, and shell

**Fork**

- Syscall: `sysfork(const struct trap_frame *tfr)`
    
- Implement:
    
    - `process_fork(tfr)`
        
    - `fork_func(done_cond, tfr)`
        
    - `clone_active_mspace()` (memory.c)
        
- Multiple memory spaces: must **switch active mspace on context switch** (thread.c updates).
    
- Kernel stacks: allocate stack pages via `alloc_phys_page()` (not `kmalloc`), free via `free_phys_page()`.
    

**Preemptive multitasking**

- Preempt **only when running in U-mode** (in `handle_umode_interrupt`).
    
- Timer ISR must set up a **periodic alarm** to trigger scheduling preemption.
    

**Pipes**

- Provide `create_pipe(struct uio **wptr, struct uio **rptr)` in `uio.c`:
    
    - separate read-end/write-end uios sharing one buffer
        
- Add syscall `syspipe(int *wfdptr, int *rfdptr)` (syscall number **20**).
    

**Shell (usr/progs/shell.c)**  
Minimum required behaviors:

- Fork/exec of commands
    
- Argument parsing to `(argc, argv)`; ignore repeated/leading/trailing spaces
    
- Default FD setup: `0=STDIN`, `1=STDOUT`, `2=CONSOLE_OUT` all to console; errors to FD 2
    
- Redirection:
    
    - `< file` opens file into FD 0
        
    - `> file` opens/creates file into FD 1; overwrite contents
        
- Piping:
    
    - `cmd1 | cmd2` uses `syspipe`, multiple forks, sets cmd1 stdout to pipe write-end and cmd2 stdin to pipe read-end
        
- Command name mapping: if command has **no `/`**, prepend `"c/"` before open (e.g., `trek` → `c/trek`).
    

**User utilities (usr/progs/)**

- Implement: `date, echo, cat, ls, wc, touch, rm, xargs`
    
- File arguments are assumed to be **absolute paths** (e.g., `c/file`).