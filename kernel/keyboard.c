#include <stdint.h>
#include "keyboard.h"
#include "io.h"
#include "console.h"

/* PS/2 keyboard, scancode set 1. Translated characters are pushed into
 * the console input buffer. */

static const char map_lower[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ',
};

static const char map_upper[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ',
};

static int shift, caps, ext;

void keyboard_irq(void)
{
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {           /* extended prefix */
        ext = 1;
        return;
    }
    if (ext) {
        ext = 0;
        switch (sc) {           /* make codes only; releases ignored */
        case 0x48: console_input((char)KEY_UP); break;
        case 0x50: console_input((char)KEY_DOWN); break;
        case 0x4B: console_input((char)KEY_LEFT); break;
        case 0x4D: console_input((char)KEY_RIGHT); break;
        }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; return; }
    if (sc == 0x3A) { caps = !caps; return; }
    if (sc & 0x80)              /* other key release */
        return;

    char c = shift ? map_upper[sc] : map_lower[sc];
    if (!c)
        return;
    if (caps && !shift && c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    else if (caps && shift && c >= 'A' && c <= 'Z')
        c = (char)(c - 'A' + 'a');
    console_input(c);
}
