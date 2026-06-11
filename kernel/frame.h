#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

/* 4 KiB physical frame allocator for page tables and user memory. */

void frame_init(void);
uint32_t frame_alloc(void);     /* zeroed; 0 if exhausted */
void frame_free(uint32_t phys);
uint32_t frame_free_count(void);

#endif
