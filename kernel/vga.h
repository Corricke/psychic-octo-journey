#ifndef VGA_H
#define VGA_H

#include <stdint.h>

#define VGA_COLS 80
#define VGA_ROWS 25

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_set_color(uint8_t fg, uint8_t bg);

#endif
