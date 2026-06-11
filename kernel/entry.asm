; Kernel entry point. The boot sector far-jumps here at 0x10000 in
; 32-bit protected mode with flat segments already loaded.

[bits 32]

section .entry

global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    mov esp, 0x90000
    cld

    ; The image is loaded verbatim from disk, so .bss must be zeroed here.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kmain
.hang:
    cli
    hlt
    jmp .hang
