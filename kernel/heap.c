#include "heap.h"
#include "e820.h"
#include "console.h"

/*
 * First-fit kernel heap over the largest usable E820 region at or above
 * 1 MiB (everything below 1 MiB is the kernel image, stacks, BIOS and
 * MMIO areas). Blocks carry a header and form a singly linked list;
 * adjacent free blocks are coalesced on free.
 */

#define HEAP_MAX_SIZE  (8u * 1024 * 1024)
#define HEAP_MIN_BASE  0x100000u
#define ALIGN_UP(x)    (((x) + 7u) & ~7u)

struct block {
    uint32_t size;              /* payload size, not counting header */
    uint32_t free;
    struct block *next;
};

static struct block *heap_head;
static uint32_t heap_total;

void heap_init(void)
{
    uint64_t best_base = 0, best_len = 0;
    uint32_t n = E820_COUNT;

    for (uint32_t i = 0; i < n && i < 64; i++) {
        volatile struct e820_entry *e = &E820_MAP[i];
        if (e->type != E820_USABLE || e->base < HEAP_MIN_BASE)
            continue;
        if (e->length > best_len) {
            best_base = e->base;
            best_len = e->length;
        }
    }

    if (!best_len) {
        console_puts("heap: no usable memory above 1 MiB\n");
        return;
    }
    if (best_len > HEAP_MAX_SIZE)
        best_len = HEAP_MAX_SIZE;

    heap_head = (struct block *)(uint32_t)best_base;
    heap_total = (uint32_t)best_len;
    heap_head->size = heap_total - sizeof(struct block);
    heap_head->free = 1;
    heap_head->next = 0;
}

void *kmalloc(size_t size)
{
    if (!heap_head || size == 0)
        return 0;
    size = ALIGN_UP(size);

    for (struct block *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < size)
            continue;
        /* Split if the remainder can hold a header and a minimal block. */
        if (b->size >= size + sizeof(struct block) + 8) {
            struct block *rest =
                (struct block *)((uint8_t *)(b + 1) + size);
            rest->size = b->size - size - sizeof(struct block);
            rest->free = 1;
            rest->next = b->next;
            b->size = size;
            b->next = rest;
        }
        b->free = 0;
        return b + 1;
    }
    return 0;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    struct block *b = (struct block *)ptr - 1;
    b->free = 1;

    /* Coalesce runs of free blocks. */
    for (struct block *p = heap_head; p; p = p->next) {
        while (p->free && p->next && p->next->free) {
            p->size += sizeof(struct block) + p->next->size;
            p->next = p->next->next;
        }
    }
}

void heap_get_stats(struct heap_stats *st)
{
    st->total = heap_total;
    st->used = 0;
    st->free = 0;
    st->largest_free = 0;
    st->blocks = 0;
    for (struct block *b = heap_head; b; b = b->next) {
        st->blocks++;
        if (b->free) {
            st->free += b->size;
            if (b->size > st->largest_free)
                st->largest_free = b->size;
        } else {
            st->used += b->size;
        }
    }
}
