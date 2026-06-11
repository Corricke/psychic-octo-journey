#ifndef IDT_H
#define IDT_H

#include <stdint.h>

struct regs {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;  /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
};

void idt_init(void);

#endif
