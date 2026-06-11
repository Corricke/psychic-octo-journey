#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

/* OctoOS user-mode syscall wrappers: int 0x80, number in eax,
 * args in ebx/ecx, result in eax. */

static inline int _sys(int n, int a, int b)
{
    int r;
    __asm__ volatile ("int $0x80"
                      : "=a"(r)
                      : "a"(n), "b"(a), "c"(b)
                      : "memory");
    return r;
}

static inline void exit(int code)
{
    _sys(1, code, 0);
    for (;;)
        ;
}

static inline void putc(char c)     { _sys(2, c, 0); }
static inline char getc(void)       { return (char)_sys(3, 0, 0); }
static inline void puts(const char *s) { _sys(4, (int)s, 0); }
static inline unsigned ticks(void)  { return (unsigned)_sys(5, 0, 0); }
static inline void yield(void)      { _sys(6, 0, 0); }

#endif
