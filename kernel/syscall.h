#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"

/* int 0x80, number in eax, args in ebx/ecx/edx, result in eax. */
#define SYS_EXIT    1
#define SYS_PUTC    2
#define SYS_GETC    3
#define SYS_PUTS    4
#define SYS_TICKS   5
#define SYS_YIELD   6
#define SYS_OPEN    7
#define SYS_CLOSE   8
#define SYS_READ    9
#define SYS_WRITE   10
#define SYS_GETARGS 11

int syscall_dispatch(struct regs *r);

#endif
