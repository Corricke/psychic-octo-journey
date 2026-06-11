#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/*
 * The console multiplexes output to the VGA text buffer and COM1, and
 * draws input from a single ring buffer fed by the keyboard and serial
 * RX interrupt handlers.
 */

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
void kprintf(const char *fmt, ...);

void console_input(char c);     /* called from IRQ handlers */
char console_getc(void);        /* blocks, hlt-ing between interrupts */

#endif
