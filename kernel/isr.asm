; Interrupt stubs. CPU exceptions 0-31 and remapped PIC IRQs 32-47 all
; funnel through isr_common into the C dispatcher.

[bits 32]

section .text

extern isr_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0                ; fake error code to keep the frame uniform
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

%assign i 32
%rep 16
ISR_NOERR i
%assign i i+1
%endrep

ISR_NOERR 128                   ; syscall

isr_common:
    pusha
    push esp                    ; struct regs *
    call isr_handler
    add esp, 4
    popa
    add esp, 8                  ; drop int_no and err_code
    iretd
