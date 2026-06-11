#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

struct heap_stats {
    uint32_t total;
    uint32_t used;
    uint32_t free;
    uint32_t largest_free;
    uint32_t blocks;
};

void heap_get_stats(struct heap_stats *st);

#endif
