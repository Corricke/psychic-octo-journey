#include "timer.h"
#include "io.h"

static volatile uint32_t ticks;

void timer_init(void)
{
    uint16_t divisor = 1193182 / TIMER_HZ;
    outb(0x43, 0x36);           /* channel 0, lo/hi, square wave */
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
}

void timer_tick(void)
{
    ticks++;
}

uint32_t timer_ticks(void)
{
    return ticks;
}
