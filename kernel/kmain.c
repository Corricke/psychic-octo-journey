#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "timer.h"
#include "heap.h"
#include "frame.h"
#include "paging.h"
#include "task.h"
#include "ata.h"
#include "fat.h"
#include "shell.h"

void kmain(void)
{
    console_init();
    gdt_init();
    timer_init();
    idt_init();
    heap_init();
    frame_init();
    paging_init();
    task_init();

    console_puts(
        "\n"
        "  OctoOS 0.1 -- x86 command-line operating system\n"
        "  Type 'help' for a list of commands.\n"
        "\n");

    if (ata_init() < 0)
        console_puts("ata: no drive on primary master\n");
    else if (fat_mount() < 0)
        console_puts("fat: no FAT16 partition found\n");

    shell_run();
}
