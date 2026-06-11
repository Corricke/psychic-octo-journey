#include "syscall.h"

/* Reads a line of input through the getc syscall and greets back. */

int main(void)
{
    char name[64];
    int n = 0;

    puts("What is your name? ");
    for (;;) {
        char c = getc();
        if (c == '\r' || c == '\n')
            break;
        if ((c == '\b' || c == 0x7F) && n > 0) {
            n--;
            puts("\b \b");
            continue;
        }
        if (c >= ' ' && c <= '~' && n < 63) {
            name[n++] = c;
            putc(c);
        }
    }
    name[n] = '\0';
    puts("\nHello, ");
    puts(n ? name : "mystery guest");
    puts("! Greetings from user space.\n");
    return 0;
}
