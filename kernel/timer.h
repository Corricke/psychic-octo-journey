#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define TIMER_HZ 100

void timer_init(void);
void timer_tick(void);
uint32_t timer_ticks(void);

#endif
