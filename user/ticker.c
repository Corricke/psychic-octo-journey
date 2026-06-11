#include "syscall.h"

/* Prints a tick mark every half second, five times. Run it in the
 * background (`run ticker.bin &`) to watch it interleave with the
 * shell. */

int main(const char *args)
{
    (void)args;
    for (int i = 0; i < 5; i++) {
        unsigned t0 = ticks();
        while (ticks() - t0 < 50)
            yield();
        puts("[tick]");
    }
    puts("\nticker done\n");
    return 0;
}
