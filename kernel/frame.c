#include "frame.h"
#include "e820.h"
#include "heap.h"
#include "string.h"

/*
 * Frames come from the same large usable region the heap lives in,
 * starting right after the heap's 8 MiB and capped so everything stays
 * inside the kernel's identity-mapped range.
 */

#define HEAP_RESERVED  (8u * 1024 * 1024)   /* keep in sync with heap.c */
#define FRAME_SIZE     4096u
#define FRAMES_MAX     16384u               /* up to 64 MiB of frames */
#define IDENTITY_LIMIT (256u * 1024 * 1024) /* must match paging_init */

static uint32_t base;
static uint32_t count;
static uint8_t bitmap[FRAMES_MAX / 8];
static uint32_t free_frames;

void frame_init(void)
{
    uint64_t best_base = 0, best_len = 0;
    uint32_t n = E820_COUNT;

    for (uint32_t i = 0; i < n && i < 64; i++) {
        volatile struct e820_entry *e = &E820_MAP[i];
        if (e->type != E820_USABLE || e->base < 0x100000)
            continue;
        if (e->length > best_len) {
            best_base = e->base;
            best_len = e->length;
        }
    }
    if (best_len <= HEAP_RESERVED)
        return;

    uint64_t start = best_base + HEAP_RESERVED;
    uint64_t end = best_base + best_len;
    if (end > IDENTITY_LIMIT)
        end = IDENTITY_LIMIT;
    if (start >= end)
        return;

    base = (uint32_t)((start + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1));
    count = (uint32_t)((end - base) / FRAME_SIZE);
    if (count > FRAMES_MAX)
        count = FRAMES_MAX;
    free_frames = count;
}

uint32_t frame_alloc(void)
{
    for (uint32_t i = 0; i < count; i++) {
        if (!(bitmap[i / 8] & (1u << (i % 8)))) {
            bitmap[i / 8] |= (uint8_t)(1u << (i % 8));
            free_frames--;
            uint32_t phys = base + i * FRAME_SIZE;
            memset((void *)phys, 0, FRAME_SIZE);
            return phys;
        }
    }
    return 0;
}

void frame_free(uint32_t phys)
{
    if (phys < base)
        return;
    uint32_t i = (phys - base) / FRAME_SIZE;
    if (i >= count || !(bitmap[i / 8] & (1u << (i % 8))))
        return;
    bitmap[i / 8] &= (uint8_t)~(1u << (i % 8));
    free_frames++;
}

uint32_t frame_free_count(void)
{
    return free_frames;
}
