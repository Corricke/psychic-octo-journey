#include "paging.h"
#include "frame.h"
#include "string.h"

/*
 * The kernel runs identity-mapped: the first 256 MiB are covered with
 * 4 MiB PSE pages, supervisor-only. User processes get a copy of those
 * mappings plus one 4 KiB page table mapping their 4 MiB window at
 * USER_BASE (code/data from the bottom, stack at the top).
 */

#define IDENTITY_PDES 64        /* 64 * 4 MiB = 256 MiB */
#define USER_PDE      (USER_BASE >> 22)

static uint32_t kdir;           /* physical (== virtual, identity) */

void paging_init(void)
{
    kdir = frame_alloc();
    uint32_t *dir = (uint32_t *)kdir;
    for (uint32_t i = 0; i < IDENTITY_PDES; i++)
        dir[i] = (i << 22) | PDE_PS | PTE_W | PTE_P;

    uint32_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10;                /* PSE */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));

    __asm__ volatile ("mov %0, %%cr3" : : "r"(kdir));

    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;          /* PG */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

uint32_t kernel_pgdir(void)
{
    return kdir;
}

static void free_user_pages(uint32_t pgdir)
{
    uint32_t *dir = (uint32_t *)pgdir;
    if (dir[USER_PDE] & PTE_P) {
        uint32_t *pt = (uint32_t *)(dir[USER_PDE] & ~0xFFFu);
        for (int i = 0; i < 1024; i++)
            if (pt[i] & PTE_P)
                frame_free(pt[i] & ~0xFFFu);
        frame_free((uint32_t)pt);
    }
    frame_free(pgdir);
}

uint32_t paging_new_user(const void *image, uint32_t size)
{
    uint32_t pgdir = frame_alloc();
    if (!pgdir)
        return 0;
    memcpy((void *)pgdir, (void *)kdir, PAGE_SIZE);

    uint32_t pt = frame_alloc();
    if (!pt) {
        frame_free(pgdir);
        return 0;
    }
    ((uint32_t *)pgdir)[USER_PDE] = pt | PTE_U | PTE_W | PTE_P;

    /* Code/data pages: the image plus headroom for .bss and a brk-less
     * static heap. Frames come zeroed from the allocator. */
    uint32_t image_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE + 16;
    uint32_t stack_first = 1024 - USER_STACK_PAGES;
    if (image_pages >= stack_first)
        goto fail;

    const uint8_t *src = image;
    for (uint32_t i = 0; i < image_pages; i++) {
        uint32_t f = frame_alloc();
        if (!f)
            goto fail;
        ((uint32_t *)pt)[i] = f | PTE_U | PTE_W | PTE_P;
        if (size > i * PAGE_SIZE) {
            uint32_t chunk = size - i * PAGE_SIZE;
            if (chunk > PAGE_SIZE)
                chunk = PAGE_SIZE;
            memcpy((void *)f, src + i * PAGE_SIZE, chunk);
        }
    }

    for (uint32_t i = stack_first; i < 1024; i++) {
        uint32_t f = frame_alloc();
        if (!f)
            goto fail;
        ((uint32_t *)pt)[i] = f | PTE_U | PTE_W | PTE_P;
    }
    return pgdir;

fail:
    free_user_pages(pgdir);
    return 0;
}

void paging_destroy_user(uint32_t pgdir)
{
    free_user_pages(pgdir);
}
