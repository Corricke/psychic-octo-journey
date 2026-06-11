; OctoOS boot sector
;
; Loaded by the BIOS at 0000:7C00. Loads the kernel from disk to 0x10000
; using INT 13h extensions, collects the E820 memory map for the kernel,
; enables the A20 line, and jumps to 32-bit protected mode.

[org 0x7C00]
[bits 16]

KERNEL_LBA     equ 1            ; kernel starts in the sector after us
KERNEL_SECTORS equ 64           ; 32 KiB, kept in sync with the Makefile
KERNEL_SEG     equ 0x1000       ; physical 0x10000
E820_COUNT     equ 0x0500       ; u32 entry count
E820_MAP       equ 0x0504       ; 24-byte entries follow

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    ; Load the kernel with an extended read (DAP below).
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    call do_e820

    ; Enable A20 via the fast gate.
    in al, 0x92
    or al, 2
    out 0x92, al

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_start

disk_error:
    mov si, msg_err
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .print
.halt:
    hlt
    jmp .halt

; Query the BIOS E820 memory map into E820_MAP, count into E820_COUNT.
do_e820:
    xor ebx, ebx
    xor bp, bp                  ; entry counter
    mov di, E820_MAP
.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150         ; 'SMAP'
    mov ecx, 24
    mov dword [es:di + 20], 1   ; ACPI 3.x: assume entry is valid
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    inc bp
    add di, 24
    test ebx, ebx
    jnz .loop
.done:
    movzx eax, bp
    mov [E820_COUNT], eax
    ret

[bits 32]
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp 0x08:0x10000

[bits 16]
gdt:
    dq 0                        ; null descriptor
    dq 0x00CF9A000000FFFF       ; 0x08: flat 4 GiB code
    dq 0x00CF92000000FFFF       ; 0x10: flat 4 GiB data
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

dap:                            ; disk address packet for INT 13h AH=42h
    db 16, 0
    dw KERNEL_SECTORS
    dw 0x0000, KERNEL_SEG       ; destination offset:segment
    dq KERNEL_LBA

boot_drive: db 0
msg_err:    db "OctoOS: disk read error", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
