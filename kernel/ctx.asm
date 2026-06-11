; Context switch and the ring-3 entry trampoline.

[bits 32]

section .text

; void ctx_switch(uint32_t *old_esp, uint32_t new_esp)
; Saves callee-saved state on the current stack, stores ESP through
; old_esp, switches to new_esp and unwinds the same frame there.
global ctx_switch
ctx_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    push ebp
    push ebx
    push esi
    push edi
    pushfd
    mov [eax], esp
    mov esp, edx
    popfd
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; First entry into a new user task: the kernel stack was crafted so that
; ctx_switch "returns" here with an iret frame (eip/cs/eflags/esp/ss)
; on top. Load user data segments and drop to ring 3.
global user_entry
user_entry:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretd
