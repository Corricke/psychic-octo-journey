#include "syscall.h"

/* cp <src> <dst>: copy a file through the read/write syscalls. */

static char buf[512];

int main(const char *args)
{
    /* Split "src dst" in place-ish: find the space. */
    static char src[64];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < 63) {
        src[i] = args[i];
        i++;
    }
    src[i] = '\0';
    while (args[i] == ' ')
        i++;
    const char *dst = args + i;

    if (!src[0] || !*dst) {
        puts("usage: run bin/cp.elf <src> <dst>\n");
        return 1;
    }

    int in = open(src, O_RDONLY);
    if (in < 0)
        return 1;
    int out = open(dst, O_WRONLY);
    if (out < 0) {
        close(in);
        puts("cannot open destination\n");
        return 1;
    }

    unsigned total = 0;
    int n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, n) != n) {
            puts("write error\n");
            close(in);
            close(out);
            return 1;
        }
        total += (unsigned)n;
    }
    close(in);
    if (close(out) < 0) {
        puts("write error on close\n");
        return 1;
    }
    put_uint(total);
    puts(" bytes copied\n");
    return 0;
}
