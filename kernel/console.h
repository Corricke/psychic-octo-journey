#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/*
 * The console multiplexes output to the VGA text buffer and COM1, and
 * draws input from a single ring buffer fed by the keyboard and serial
 * RX interrupt handlers.
 */

/* Special keys are delivered in-band as bytes above the ASCII range
 * (the PS/2 driver emits them directly; serial arrives as ESC [ A..D
 * and is translated by the line editor). */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
void kprintf(const char *fmt, ...);

void console_input(char c);     /* called from IRQ handlers */
char console_getc(void);        /* blocks, hlt-ing between interrupts */
int console_trygetc(void);      /* -1 if no input pending */

#endif
