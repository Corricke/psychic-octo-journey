# OctoOS

A tiny command-line operating system for x86, written from scratch in C and
NASM assembly. It boots from a raw disk image on real hardware or in QEMU —
no GRUB, no libc, no dependencies beyond a compiler.

```
  OctoOS 0.1 -- x86 command-line operating system
  Type 'help' for a list of commands.

octo:/> mkdir docs
octo:/> write docs/note.txt saved to disk for real
ok
octo:/> cat docs/note.txt
saved to disk for real
octo:/> run bin/hello.bin
Hello from ring 3!
octo:/> run bin/ticker.bin &
[3] bin/ticker.bin
octo:/> [tick]ps
pid  state    name
1    ready    shell *
3    ready    bin/ticker.bin
octo:/> peek 0x7c00 16
0x00007c00  fa 31 c0 8e d8 8e c0 8e d0 bc 00 7c fb 88 16 d6  |.1.........|....|
```

## Design

**Boot** (`boot/boot.asm`, 512 bytes): the BIOS loads the boot sector at
`0x7C00`. It reads the kernel from disk to `0x10000` using INT 13h
extensions (LBA), queries the BIOS E820 memory map into low memory for the
kernel to use later, enables the A20 line via the fast gate, loads a flat
GDT, and far-jumps into 32-bit protected mode at the kernel entry point.
Single stage — no second-stage loader; the kernel lives in the sectors
right after the boot sector. The boot sector doubles as a real MBR: its
partition table points at a FAT16 partition starting at LBA 2048.

**Disk layout** (16 MiB image):

```
LBA 0          boot sector + MBR partition table
LBA 1-128      kernel (flat binary, max 64 KiB)
LBA 2048-      FAT16 partition (15 MiB) with the filesystem
```

**Kernel** (`kernel/`): freestanding 32-bit C linked at `0x10000` with a
small assembly entry stub that zeroes `.bss`. The kernel runs
identity-mapped in ring 0; user programs run paged in ring 3.

- **Interrupts**: full IDT, the 8259 PICs remapped to vectors 32–47, CPU
  exceptions panic with a register dump. The PIT runs at 100 Hz for
  timekeeping, and input is fully interrupt-driven — the CPU `hlt`s when
  idle.
- **Console**: output is mirrored to VGA text mode (`0xB8000`, with
  scrolling and hardware cursor) and the COM1 UART at 115200 8N1. Input
  from the PS/2 keyboard (IRQ 1, scancode set 1, shift/caps handling) and
  serial RX (IRQ 4) feeds one shared ring buffer, so the machine is equally
  usable from a monitor+keyboard or a serial terminal.
- **Heap**: first-fit `kmalloc`/`kfree` with splitting and coalescing
  over the largest usable E820 region above 1 MiB (capped at 8 MiB).
- **Storage**: polled PIO driver for the primary-master ATA drive
  (IDENTIFY, LBA28 read/write + cache flush), and a FAT16 driver on top
  that parses the MBR partition table and the BPB. Full path and
  subdirectory support with a current working directory, plus write
  operations: create/overwrite files, delete, and mkdir (both FAT copies
  kept in sync). Changes persist across reboots. No long file names —
  8.3 only.
- **Memory protection**: paging with the first 256 MiB identity-mapped
  for the kernel (4 MiB PSE pages, supervisor-only). Each process gets
  its own page directory with a 4 MiB user window at `0x40000000`.
- **Processes**: ring-3 user processes loaded as flat binaries from the
  filesystem, preemptively scheduled round-robin from the timer
  interrupt. Each task has its own kernel stack; the TSS switches stacks
  on ring transitions. A faulting user process is killed and reaped —
  the kernel keeps running.
- **Syscalls**: `int 0x80` (exit, putc, getc, puts, ticks, yield), with
  user-mode wrappers in `user/syscall.h`. The shell runs programs in the
  foreground or, with `&`, in the background.
- **Shell**: line editor with backspace, then a simple
  first-word-dispatches command loop.

| command | description |
|---|---|
| `help` | list commands |
| `about` | about OctoOS |
| `echo <text>` | print text |
| `clear` | clear the screen |
| `ls [path]` | list a directory |
| `cat <file>` | print a file |
| `write <file> <text>` | write text to a file |
| `rm <path>` | remove a file or empty directory |
| `mkdir <dir>` | create a directory |
| `cd <dir>` | change directory |
| `run <file> [&]` | run a user program, optionally in the background |
| `ps` | list tasks and free frames |
| `mem` | print the BIOS E820 memory map |
| `heap` | kernel heap statistics |
| `disk` | ATA drive model and capacity |
| `uptime` | time since boot (PIT ticks) |
| `peek <hex> [len]` | hex dump of physical memory |
| `color <fg> <bg>` | set VGA text color |
| `reboot` | reset via the 8042 controller |
| `shutdown` | ACPI-style power off on QEMU/Bochs/VirtualBox |

## Building

Requires `gcc` (with 32-bit codegen, standard on x86-64 Linux), `nasm`,
GNU `make`, `dosfstools` (`mkfs.fat`), and `mtools` (`mcopy`):

```sh
make            # produces os.img
```

## Running

```sh
make run        # QEMU, serial console on stdio (headless)
make run-vga    # QEMU with the VGA window, serial mirrored to stdio
```

On real hardware: write `os.img` to a USB stick or disk
(`dd if=os.img of=/dev/sdX`) and boot it on a machine with legacy
BIOS / CSM boot enabled. It needs a BIOS (not pure UEFI), INT 13h
extensions (any machine from this millennium), and a VGA-compatible text
mode or a serial port.

## User programs

User programs are flat binaries linked at `0x40000000` (see
`user/user.ld`), started through `user/crt0.c`, and talk to the kernel
only through `int 0x80`. The build copies them into `/BIN` on the
filesystem image: `HELLO.BIN` (hello world), `TICKER.BIN` (background
multitasking demo), `GREET.BIN` (interactive input demo).

## Layout

```
boot/boot.asm      boot sector: disk load, E820, A20, GDT, protected mode
kernel/entry.asm   entry stub: stack, .bss zeroing, call kmain
kernel/kmain.c     init order and banner
kernel/gdt.c       GDT with ring-3 segments + TSS
kernel/isr.asm     interrupt stubs (exceptions, IRQs, int 0x80)
kernel/idt.c       IDT, PIC remap, dispatch, user-fault kill
kernel/ctx.asm     context switch + ring-3 entry trampoline
kernel/task.c      task table, spawn/exit/reap, round-robin scheduler
kernel/syscall.c   int 0x80 syscall handlers
kernel/frame.c     physical 4 KiB frame allocator
kernel/paging.c    kernel identity map + per-process address spaces
kernel/heap.c      kmalloc/kfree over usable E820 memory
kernel/vga.c       VGA text mode driver
kernel/serial.c    COM1 UART driver
kernel/console.c   VGA+serial mux, input ring buffer, kprintf
kernel/keyboard.c  PS/2 keyboard (scancode set 1)
kernel/timer.c     PIT at 100 Hz
kernel/ata.c       ATA PIO driver (primary master, LBA28 read/write)
kernel/fat.c       FAT16: subdirectories, cwd, read/write/delete/mkdir
kernel/shell.c     command shell
kernel/string.c    mem*/str* routines
kernel/linker.ld   links the kernel as a flat binary at 0x10000
user/syscall.h     user-mode syscall wrappers
user/crt0.c        user program entry
user/*.c           demo programs (hello, ticker, greet)
```
