#include <stdarg.h>
#include "console.h"
#include "vga.h"
#include "serial.h"
#include "string.h"

#define INBUF_SIZE 256

static volatile char inbuf[INBUF_SIZE];
static volatile uint8_t in_head, in_tail;

void console_init(void)
{
    vga_init();
    serial_init();
}

void console_putc(char c)
{
    /* Keep each character atomic across preemption. */
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));
    vga_putc(c);
    serial_putc(c);
    __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory", "cc");
}

void console_puts(const char *s)
{
    while (*s)
        console_putc(*s++);
}

void console_input(char c)
{
    uint8_t next = (uint8_t)(in_head + 1);
    if (next == in_tail)
        return;                 /* buffer full, drop */
    inbuf[in_head] = c;
    in_head = next;
}

int console_trygetc(void)
{
    if (in_head == in_tail)
        return -1;
    uint8_t c = (uint8_t)inbuf[in_tail];
    in_tail++;
    return c;
}

char console_getc(void)
{
    /* May be entered with interrupts off (syscall gate); make sure
     * they're on while we wait or the hlt would never wake. */
    while (in_head == in_tail)
        __asm__ volatile ("sti; hlt");
    char c = inbuf[in_tail];
    in_tail++;
    return c;
}

static void print_uint(uint32_t v, unsigned base, int min_digits)
{
    char buf[16];
    int i = 0;
    do {
        unsigned d = v % base;
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    while (i < min_digits)
        buf[i++] = '0';
    while (i--)
        console_putc(buf[i]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            console_putc(*fmt);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            console_puts(s ? s : "(null)");
            break;
        }
        case 'c':
            console_putc((char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                console_putc('-');
                v = -v;
            }
            print_uint((uint32_t)v, 10, 1);
            break;
        }
        case 'u':
            print_uint(va_arg(ap, uint32_t), 10, 1);
            break;
        case 'x':
            print_uint(va_arg(ap, uint32_t), 16, 1);
            break;
        case 'p':
            console_puts("0x");
            print_uint(va_arg(ap, uint32_t), 16, 8);
            break;
        case '%':
            console_putc('%');
            break;
        case '\0':
            va_end(ap);
            return;
        default:
            console_putc('%');
            console_putc(*fmt);
            break;
        }
    }
    va_end(ap);
}
