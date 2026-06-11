#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

/* OctoOS user-mode syscall wrappers: int 0x80, number in eax,
 * args in ebx/ecx/edx, result in eax. */

static inline int _sys(int n, int a, int b, int c)
{
    int r;
    __asm__ volatile ("int $0x80"
                      : "=a"(r)
                      : "a"(n), "b"(a), "c"(b), "d"(c)
                      : "memory");
    return r;
}

static inline void exit(int code)
{
    _sys(1, code, 0, 0);
    for (;;)
        ;
}

static inline void putc(char c)     { _sys(2, c, 0, 0); }
static inline char getc(void)       { return (char)_sys(3, 0, 0, 0); }
static inline void puts(const char *s) { _sys(4, (int)s, 0, 0); }
static inline unsigned ticks(void)  { return (unsigned)_sys(5, 0, 0, 0); }
static inline void yield(void)      { _sys(6, 0, 0, 0); }

/* File I/O. fd 0 reads the console, fds 1/2 write to it. */
#define O_RDONLY 0
#define O_WRONLY 1

static inline int open(const char *path, int mode)
{
    return _sys(7, (int)path, mode, 0);
}
static inline int close(int fd) { return _sys(8, fd, 0, 0); }
static inline int read(int fd, void *buf, int n)
{
    return _sys(9, fd, (int)buf, n);
}
static inline int write(int fd, const void *buf, int n)
{
    return _sys(10, fd, (int)buf, n);
}
static inline int getargs(char *buf, int max)
{
    return _sys(11, (int)buf, max, 0);
}

/* Small conveniences for demo programs. */
static inline int str_len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static inline void put_uint(unsigned v)
{
    char tmp[12];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i--)
        putc(tmp[i]);
}

#endif
