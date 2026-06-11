# OctoOS

A tiny command-line operating system for x86, written from scratch in C and
NASM assembly. It boots from a raw disk image on real hardware or in QEMU —
no GRUB, no libc, no dependencies beyond a compiler.

```
  OctoOS 0.1 -- x86 command-line operating system
  Type 'help' for a list of commands.

octo> ls
README.MD  3685 bytes
HELLO.TXT  34 bytes
LOREM.TXT  38392 bytes
octo> cat hello.txt
Hello from the OctoOS filesystem!
octo> mem
base              length            type
0000000000000000  000000000009fc00  1 (usable)
0000000000100000  0000000007ee0000  1 (usable)
...
usable: 130559 KiB
octo> peek 0x7c00 32
0x00007c00  fa 31 c0 8e d8 8e c0 8e d0 bc 00 7c fb 88 16 d6  |.1.........|....|
0x00007c10  7c be c6 7c b4 42 8a 16 d6 7c cd 13 72 1e e8 2c  ||..|.B...|..r..,|
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
LBA 1-64       kernel (flat binary, max 32 KiB)
LBA 2048-      FAT16 partition (15 MiB) with the filesystem
```

**Kernel** (`kernel/`): freestanding 32-bit C linked at `0x10000` with a
small assembly entry stub that zeroes `.bss`. It runs with flat segments,
no paging, everything in ring 0 — deliberately minimal.

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
  (IDENTIFY + LBA28 reads), and a read-only FAT16 driver on top that
  parses the MBR partition table and the BPB, walks cluster chains, and
  serves the root directory.
- **Shell**: line editor with backspace, then a simple
  first-word-dispatches command loop.

| command | description |
|---|---|
| `help` | list commands |
| `about` | about OctoOS |
| `echo <text>` | print text |
| `clear` | clear the screen |
| `ls` | list files in the FAT16 root directory |
| `cat <file>` | print a file |
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

## Layout

```
boot/boot.asm      boot sector: disk load, E820, A20, GDT, protected mode
kernel/entry.asm   entry stub: stack, .bss zeroing, call kmain
kernel/kmain.c     init order and banner
kernel/isr.asm     interrupt stubs
kernel/idt.c       IDT, PIC remap, dispatch
kernel/vga.c       VGA text mode driver
kernel/serial.c    COM1 UART driver
kernel/console.c   VGA+serial mux, input ring buffer, kprintf
kernel/keyboard.c  PS/2 keyboard (scancode set 1)
kernel/timer.c     PIT at 100 Hz
kernel/heap.c      kmalloc/kfree over usable E820 memory
kernel/ata.c       ATA PIO driver (primary master, LBA28)
kernel/fat.c       read-only FAT16 (MBR + BPB parse, root dir, cat)
kernel/shell.c     command shell
kernel/string.c    mem*/str* routines
kernel/linker.ld   links the kernel as a flat binary at 0x10000
```
