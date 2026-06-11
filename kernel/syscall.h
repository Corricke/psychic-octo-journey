#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"

/* int 0x80, number in eax, args in ebx/ecx, result in eax. */
#define SYS_EXIT  1
#define SYS_PUTC  2
#define SYS_GETC  3
#define SYS_PUTS  4
#define SYS_TICKS 5
#define SYS_YIELD 6

int syscall_dispatch(struct regs *r);

#endif
