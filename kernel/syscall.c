#include "syscall.h"
#include "console.h"
#include "timer.h"
#include "task.h"
#include "paging.h"
#include "file.h"
#include "string.h"

static int user_range(uint32_t ptr, uint32_t len)
{
    return ptr >= USER_BASE && len <= USER_STACK_TOP - ptr;
}

/* fd 0: console. Blocks for the first byte, then drains what's pending. */
static int read_console(char *buf, uint32_t len)
{
    if (len == 0)
        return 0;
    uint32_t got = 0;
    buf[got++] = console_getc();
    int c;
    while (got < len && (c = console_trygetc()) >= 0)
        buf[got++] = (char)c;
    return (int)got;
}

int syscall_dispatch(struct regs *r)
{
    int pid = task_current()->pid;

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
        if (!user_range(r->ebx, 1))
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
    case SYS_OPEN: {
        if (!user_range(r->ebx, 1))
            return -1;
        const char *path = (const char *)r->ebx;
        if (strlen(path) > 80)
            return -1;
        return file_open(pid, path, (int)r->ecx);
    }
    case SYS_CLOSE:
        return file_close(pid, (int)r->ebx);
    case SYS_READ: {
        if (!user_range(r->ecx, r->edx))
            return -1;
        int fd = (int)r->ebx;
        if (fd == 0)
            return read_console((char *)r->ecx, r->edx);
        return file_read(pid, fd, (void *)r->ecx, r->edx);
    }
    case SYS_WRITE: {
        if (!user_range(r->ecx, r->edx))
            return -1;
        int fd = (int)r->ebx;
        if (fd == 1 || fd == 2) {
            const char *s = (const char *)r->ecx;
            for (uint32_t i = 0; i < r->edx; i++)
                console_putc(s[i]);
            return (int)r->edx;
        }
        return file_write(pid, fd, (const void *)r->ecx, r->edx);
    }
    case SYS_GETARGS: {
        if (!user_range(r->ebx, r->ecx))
            return -1;
        const char *args = task_current()->args;
        uint32_t len = strlen(args);
        if (len + 1 > r->ecx)
            len = r->ecx ? r->ecx - 1 : 0;
        memcpy((void *)r->ebx, args, len);
        ((char *)r->ebx)[len] = '\0';
        return (int)len;
    }
    default:
        return -1;
    }
}
