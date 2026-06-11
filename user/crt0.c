#include "syscall.h"

int main(const char *args);

static char argbuf[64];

void _start(void)
{
    getargs(argbuf, sizeof(argbuf));
    exit(main(argbuf));
}
