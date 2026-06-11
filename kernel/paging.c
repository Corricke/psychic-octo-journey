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

uint32_t paging_new_user(void)
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

    for (uint32_t i = 1024 - USER_STACK_PAGES; i < 1024; i++) {
        uint32_t f = frame_alloc();
        if (!f) {
            free_user_pages(pgdir);
            return 0;
        }
        ((uint32_t *)pt)[i] = f | PTE_U | PTE_W | PTE_P;
    }
    return pgdir;
}

uint32_t paging_user_page(uint32_t pgdir, uint32_t vaddr)
{
    if (vaddr < USER_BASE || vaddr >= USER_BASE + USER_IMAGE_MAX)
        return 0;

    uint32_t *pt = (uint32_t *)(((uint32_t *)pgdir)[USER_PDE] & ~0xFFFu);
    uint32_t idx = (vaddr - USER_BASE) / PAGE_SIZE;
    if (!(pt[idx] & PTE_P)) {
        uint32_t f = frame_alloc();
        if (!f)
            return 0;
        pt[idx] = f | PTE_U | PTE_W | PTE_P;
    }
    return pt[idx] & ~0xFFFu;
}

void paging_destroy_user(uint32_t pgdir)
{
    free_user_pages(pgdir);
}
