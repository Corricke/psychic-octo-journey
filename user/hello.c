#include "syscall.h"

int main(const char *args)
{
    (void)args;
    puts("Hello from ring 3!\n");
    return 0;
}
