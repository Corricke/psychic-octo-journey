#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
int serial_data_ready(void);
char serial_read(void);

#endif
