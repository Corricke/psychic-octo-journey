#include <stdint.h>
#include "shell.h"
#include "console.h"
#include "vga.h"
#include "timer.h"
#include "string.h"
#include "io.h"
#include "e820.h"
#include "heap.h"
#include "ata.h"
#include "fat.h"
#include "task.h"
#include "frame.h"

#define LINE_MAX 128

static void read_line(char *buf)
{
    int len = 0;
    for (;;) {
        char c = console_getc();
        if (c == '\r' || c == '\n') {
            console_putc('\n');
            buf[len] = '\0';
            return;
        }
        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                console_puts("\b \b");
            }
            continue;
        }
        if (c < ' ' || c > '~')
            continue;
        if (len < LINE_MAX - 1) {
            buf[len++] = c;
            console_putc(c);
        }
    }
}

/* Split off the first word; returns the rest of the line (or ""). */
static char *split_args(char *line)
{
    while (*line && *line != ' ')
        line++;
    if (*line) {
        *line++ = '\0';
        while (*line == ' ')
            line++;
    }
    return line;
}

static int parse_hex(const char *s, uint32_t *out)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    if (!*s)
        return -1;
    uint32_t v = 0;
    for (; *s; s++) {
        char c = *s;
        uint32_t d;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            return -1;
        v = (v << 4) | d;
    }
    *out = v;
    return 0;
}

static int parse_dec(const char *s, uint32_t *out)
{
    if (!*s)
        return -1;
    uint32_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (uint32_t)(*s - '0');
    }
    *out = v;
    return 0;
}

static void cmd_help(void)
{
    console_puts(
        "Commands:\n"
        "  help               show this list\n"
        "  about              about OctoOS\n"
        "  echo <text>        print text\n"
        "  clear              clear the screen\n"
        "  ls [path]          list a directory\n"
        "  cat <file>         print a file\n"
        "  write <file> <txt> write text to a file\n"
        "  rm <path>          remove a file or empty directory\n"
        "  mkdir <dir>        create a directory\n"
        "  cd <dir>           change directory\n"
        "  run <file> [&]     run a user program (& = background)\n"
        "  ps                 list tasks\n"
        "  mem                BIOS E820 memory map\n"
        "  heap               kernel heap statistics\n"
        "  disk               ATA drive information\n"
        "  uptime             time since boot\n"
        "  peek <hex> [len]   hex dump of physical memory\n"
        "  color <fg> <bg>    set text color (0-15)\n"
        "  reboot             reboot the machine\n"
        "  shutdown           power off (QEMU/Bochs/VirtualBox)\n");
}

static void cmd_about(void)
{
    console_puts(
        "OctoOS 0.1 -- a tiny command-line OS for x86\n"
        "Boot sector -> 32-bit protected mode -> flat C kernel.\n"
        "Console on VGA text mode and COM1 serial.\n");
}

static void print_hex_digits(uint32_t v, int digits)
{
    while (digits--) {
        uint32_t d = (v >> (digits * 4)) & 0xF;
        console_putc((char)(d < 10 ? '0' + d : 'a' + d - 10));
    }
}

static void print_hex64(uint64_t v)
{
    print_hex_digits((uint32_t)(v >> 32), 8);
    print_hex_digits((uint32_t)v, 8);
}

static void cmd_mem(void)
{
    uint32_t n = E820_COUNT;
    uint64_t usable = 0;

    if (n == 0 || n > 64) {
        console_puts("no E820 map available\n");
        return;
    }

    console_puts("base              length            type\n");
    for (uint32_t i = 0; i < n; i++) {
        volatile struct e820_entry *e = &E820_MAP[i];
        print_hex64(e->base);
        console_puts("  ");
        print_hex64(e->length);
        kprintf("  %u%s\n", e->type, e->type == 1 ? " (usable)" : "");
        if (e->type == 1)
            usable += e->length;
    }
    kprintf("usable: %u KiB\n", (uint32_t)(usable / 1024));
}

static void cmd_heap(void)
{
    struct heap_stats st;
    heap_get_stats(&st);
    if (!st.total) {
        console_puts("heap not initialized\n");
        return;
    }
    kprintf("total: %u KiB, used: %u bytes, free: %u KiB\n",
            st.total / 1024, st.used, st.free / 1024);
    kprintf("blocks: %u, largest free: %u KiB\n",
            st.blocks, st.largest_free / 1024);
}

static void cmd_disk(void)
{
    if (!ata_sectors()) {
        console_puts("no drive detected\n");
        return;
    }
    kprintf("model: %s\n", ata_model());
    kprintf("sectors: %u (%u MiB)\n",
            ata_sectors(), ata_sectors() / 2048);
}

static void cmd_run(char *args)
{
    int background = 0;
    char *flag = split_args(args);
    if (strcmp(flag, "&") == 0)
        background = 1;
    if (!*args) {
        console_puts("usage: run <file> [&]\n");
        return;
    }

    uint32_t size;
    void *image = fat_read_file(args, &size);
    if (!image)
        return;

    int pid = task_spawn_user(args, image, size);
    kfree(image);
    if (pid < 0)
        return;

    if (background) {
        kprintf("[%d] %s\n", pid, args);
        return;
    }

    struct task *t = task_by_pid(pid);
    while (*(volatile enum task_state *)&t->state != TASK_ZOMBIE)
        __asm__ volatile ("sti; hlt");
    if (t->exit_code)
        kprintf("exit code %d\n", t->exit_code);
    task_reap(t);
}

static void cmd_ps(void)
{
    struct task *tasks = task_table();
    console_puts("pid  state    name\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state == TASK_FREE)
            continue;
        kprintf("%d    %s   %s%s\n", t->pid,
                t->state == TASK_ZOMBIE ? "zombie" : "ready ",
                t->name, t == task_current() ? " *" : "");
    }
    kprintf("free frames: %u (%u KiB)\n",
            frame_free_count(), frame_free_count() * 4);
}

/* Reap finished background tasks (the foreground path reaps its own). */
static void reap_zombies(void)
{
    struct task *tasks = task_table();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_ZOMBIE) {
            kprintf("[%d] %s exited (%d)\n", tasks[i].pid, tasks[i].name,
                    tasks[i].exit_code);
            task_reap(&tasks[i]);
        }
    }
}

static void cmd_uptime(void)
{
    uint32_t t = timer_ticks();
    kprintf("up %u.%u%u s (%u ticks @ %u Hz)\n",
            t / TIMER_HZ, (t % TIMER_HZ) / 10, t % 10, t, TIMER_HZ);
}

static void cmd_peek(char *args)
{
    char *len_arg = split_args(args);
    uint32_t addr, len = 64;

    if (parse_hex(args, &addr) < 0) {
        console_puts("usage: peek <hex-addr> [len]\n");
        return;
    }
    if (*len_arg && parse_dec(len_arg, &len) < 0) {
        console_puts("usage: peek <hex-addr> [len]\n");
        return;
    }
    if (len > 4096)
        len = 4096;

    for (uint32_t off = 0; off < len; off += 16) {
        kprintf("%p  ", addr + off);
        for (uint32_t i = 0; i < 16; i++) {
            if (off + i < len) {
                uint8_t b = *(volatile uint8_t *)(addr + off + i);
                print_hex_digits(b, 2);
                console_putc(' ');
            } else {
                console_puts("   ");
            }
        }
        console_puts(" |");
        for (uint32_t i = 0; i < 16 && off + i < len; i++) {
            uint8_t b = *(volatile uint8_t *)(addr + off + i);
            console_putc((b >= ' ' && b <= '~') ? (char)b : '.');
        }
        console_puts("|\n");
    }
}

static void cmd_color(char *args)
{
    char *bg_arg = split_args(args);
    uint32_t fg, bg;

    if (parse_dec(args, &fg) < 0 || parse_dec(bg_arg, &bg) < 0 ||
        fg > 15 || bg > 15) {
        console_puts("usage: color <fg 0-15> <bg 0-15>\n");
        return;
    }
    vga_set_color((uint8_t)fg, (uint8_t)bg);
    console_puts("color set\n");
}

static void cmd_reboot(void)
{
    console_puts("rebooting...\n");
    /* Pulse the CPU reset line via the 8042 keyboard controller. */
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);
    for (;;)
        __asm__ volatile ("hlt");
}

static void cmd_shutdown(void)
{
    console_puts("shutting down...\n");
    outw(0x604, 0x2000);        /* QEMU */
    outw(0xB004, 0x2000);       /* Bochs and older QEMU */
    outw(0x4004, 0x3400);       /* VirtualBox */
    console_puts("power-off not supported here; halting.\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void shell_run(void)
{
    char line[LINE_MAX];

    for (;;) {
        reap_zombies();
        kprintf("octo:%s> ", fat_cwd());
        read_line(line);

        char *cmd = line;
        while (*cmd == ' ')
            cmd++;
        if (!*cmd)
            continue;
        char *args = split_args(cmd);

        if (strcmp(cmd, "help") == 0)
            cmd_help();
        else if (strcmp(cmd, "about") == 0)
            cmd_about();
        else if (strcmp(cmd, "echo") == 0)
            kprintf("%s\n", args);
        else if (strcmp(cmd, "clear") == 0)
            vga_clear();
        else if (strcmp(cmd, "ls") == 0)
            fat_ls(args);
        else if (strcmp(cmd, "cat") == 0) {
            if (*args)
                fat_cat(args);
            else
                console_puts("usage: cat <file>\n");
        }
        else if (strcmp(cmd, "write") == 0) {
            char *text = split_args(args);
            if (*args && *text) {
                uint32_t len = strlen(text);
                text[len] = '\n';   /* reuse the line buffer's NUL slot */
                if (fat_write_file(args, text, len + 1) == 0)
                    console_puts("ok\n");
            } else {
                console_puts("usage: write <file> <text>\n");
            }
        }
        else if (strcmp(cmd, "rm") == 0) {
            if (*args)
                fat_rm(args);
            else
                console_puts("usage: rm <path>\n");
        }
        else if (strcmp(cmd, "mkdir") == 0) {
            if (*args)
                fat_mkdir(args);
            else
                console_puts("usage: mkdir <dir>\n");
        }
        else if (strcmp(cmd, "cd") == 0)
            fat_cd(*args ? args : "/");
        else if (strcmp(cmd, "run") == 0)
            cmd_run(args);
        else if (strcmp(cmd, "ps") == 0)
            cmd_ps();
        else if (strcmp(cmd, "mem") == 0)
            cmd_mem();
        else if (strcmp(cmd, "heap") == 0)
            cmd_heap();
        else if (strcmp(cmd, "disk") == 0)
            cmd_disk();
        else if (strcmp(cmd, "uptime") == 0)
            cmd_uptime();
        else if (strcmp(cmd, "peek") == 0)
            cmd_peek(args);
        else if (strcmp(cmd, "color") == 0)
            cmd_color(args);
        else if (strcmp(cmd, "reboot") == 0)
            cmd_reboot();
        else if (strcmp(cmd, "shutdown") == 0)
            cmd_shutdown();
        else
            kprintf("unknown command: %s (try 'help')\n", cmd);
    }
}
