#include "syscall.h"

/* wc <file>: count lines, words, and bytes. */

static char buf[512];

int main(const char *args)
{
    if (!*args) {
        puts("usage: run bin/wc.elf <file>\n");
        return 1;
    }

    int fd = open(args, O_RDONLY);
    if (fd < 0)
        return 1;

    unsigned lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n')
                lines++;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    close(fd);

    put_uint(lines);
    puts(" lines, ");
    put_uint(words);
    puts(" words, ");
    put_uint(bytes);
    puts(" bytes\n");
    return 0;
}
