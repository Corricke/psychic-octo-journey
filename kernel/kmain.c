#include "console.h"
#include "idt.h"
#include "timer.h"
#include "shell.h"

void kmain(void)
{
    console_init();
    timer_init();
    idt_init();

    console_puts(
        "\n"
        "  OctoOS 0.1 -- x86 command-line operating system\n"
        "  Type 'help' for a list of commands.\n"
        "\n");

    shell_run();
}
