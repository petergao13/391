# 391 Field Kit — one page

## Build & run
| Command | What it does |
|---|---|
| `./mkfs-image.sh` (EWS) / `./mkfs-image-mac.sh` (Mac) | build user progs → pack ktfs.raw → build kernel → boot QEMU |
| `make -C sys run` | boot QEMU (kernel + existing ktfs.raw) |
| `make -C sys debug` | boot **frozen**, waiting for gdb on :12345 |
| `screen /dev/pts/N` | attach to the shell console (QEMU prints the N, label `serial1`) |
| `Ctrl-a d` / `Ctrl-a x` | detach screen / kill QEMU |

## gdb — the ten I actually use
Start: `riscv64-unknown-elf-gdb sys/kernel.elf` then `target remote :12345` (attaching **freezes** the kernel — that's normal).

| Command | What it does |
|---|---|
| `Ctrl-C` | freeze a running kernel NOW — first move on any hang |
| `bt` | backtrace: where am I, who called me |
| `b file.c:123` / `b funcname` | breakpoint (then `c` to continue until it hits) |
| `watch expr` | stop the instant a variable changes — memory-corruption killer |
| `n` / `s` | step one line: over calls / into calls |
| `si` | step one *assembly instruction* (asm-level debugging) |
| `finish` | run until current function returns; prints its return value |
| `p expr` / `p/x expr` | print anything, C syntax works: `p thrtab[0]->state`, `p/x $a0` |
| `p *(struct condition *)0x8001be90` | **cast an opaque pointer** to see what it really is (structs here have `name` fields!) |
| `x/4i $sepc` · `x/8gx addr` · `x/s addr` | raw memory: as instructions / hex words / string |

Container-of (member ptr → whole struct), the drill move:
`p *(struct uart_serial *)(PTR - (unsigned long)&((struct uart_serial *)0)->MEMBER)`

After any fault: `info registers scause sepc stval` — *why*, *where*, *what address*. (`scause` bit 63 set = interrupt, clear = exception.)

## Hang playbook (memorize this paragraph)
Reproduce → attach gdb → `bt`. **In idle thread?** Then everyone's blocked: `p thrtab` → find `THREAD_WAITING` thread → `p *thrtab[i]` → cast its `wait_cond` → the condition's **name tells you what never happened**. Then check state *before* adding breakpoints (e.g. is the ring buffer empty or full?) — that splits "event never arrived" from "waiter never woken." Panic instead? Read file:line, then `scause/sepc/stval`. Narrate hypotheses; one experiment at a time; make the student read their own code aloud.
