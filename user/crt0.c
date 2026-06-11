#include "syscall.h"

int main(void);

/* Placed at the very start of the binary; the kernel jumps to
 * USER_BASE, which is here. */
__attribute__((section(".entry"), used))
void _start(void)
{
    exit(main());
}
