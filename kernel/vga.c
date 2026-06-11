#include "vga.h"
#include "io.h"
#include "string.h"

static volatile uint16_t *const vmem = (uint16_t *)0xB8000;
static int cur_row, cur_col;
static uint8_t attr = 0x07;     /* light grey on black */

static void move_cursor(void)
{
    uint16_t pos = (uint16_t)(cur_row * VGA_COLS + cur_col);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)pos);
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    attr = (uint8_t)((bg << 4) | (fg & 0x0F));
}

void vga_clear(void)
{
    uint16_t blank = (uint16_t)(attr << 8) | ' ';
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vmem[i] = blank;
    cur_row = 0;
    cur_col = 0;
    move_cursor();
}

static void scroll(void)
{
    uint16_t blank = (uint16_t)(attr << 8) | ' ';
    memmove((void *)vmem, (const void *)(vmem + VGA_COLS),
            (VGA_ROWS - 1) * VGA_COLS * 2);
    for (int i = 0; i < VGA_COLS; i++)
        vmem[(VGA_ROWS - 1) * VGA_COLS + i] = blank;
    cur_row = VGA_ROWS - 1;
}

void vga_putc(char c)
{
    switch (c) {
    case '\n':
        cur_col = 0;
        cur_row++;
        break;
    case '\r':
        cur_col = 0;
        break;
    case '\b':
        if (cur_col > 0) {
            cur_col--;
            vmem[cur_row * VGA_COLS + cur_col] = (uint16_t)(attr << 8) | ' ';
        }
        break;
    case '\t':
        cur_col = (cur_col + 8) & ~7;
        break;
    default:
        vmem[cur_row * VGA_COLS + cur_col] = (uint16_t)(attr << 8) | (uint8_t)c;
        cur_col++;
        break;
    }

    if (cur_col >= VGA_COLS) {
        cur_col = 0;
        cur_row++;
    }
    if (cur_row >= VGA_ROWS)
        scroll();
    move_cursor();
}

void vga_init(void)
{
    vga_clear();
}
