#include "syscall.h"
#include "console.h"
#include "timer.h"
#include "task.h"
#include "paging.h"

static int user_range(uint32_t ptr)
{
    return ptr >= USER_BASE && ptr < USER_STACK_TOP;
}

int syscall_dispatch(struct regs *r)
{
    switch (r->eax) {
    case SYS_EXIT:
        task_exit((int)r->ebx);     /* does not return */
        return 0;
    case SYS_PUTC:
        console_putc((char)r->ebx);
        return 0;
    case SYS_GETC:
        return console_getc();
    case SYS_PUTS: {
        if (!user_range(r->ebx))
            return -1;
        const char *s = (const char *)r->ebx;
        for (int i = 0; i < 4096 && s[i]; i++)
            console_putc(s[i]);
        return 0;
    }
    case SYS_TICKS:
        return (int)timer_ticks();
    case SYS_YIELD:
        schedule();                 /* IF is off in the int gate */
        return 0;
    default:
        return -1;
    }
}
