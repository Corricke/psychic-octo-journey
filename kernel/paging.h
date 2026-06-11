#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE 4096u

/* User virtual layout: one 4 MiB page-table window. */
#define USER_BASE      0x40000000u
#define USER_STACK_TOP 0x40400000u
#define USER_STACK_PAGES 4

#define PTE_P  0x001
#define PTE_W  0x002
#define PTE_U  0x004
#define PDE_PS 0x080

void paging_init(void);
uint32_t kernel_pgdir(void);

/* Build an address space for a user image; returns the page directory
 * physical address, or 0 on failure. */
uint32_t paging_new_user(const void *image, uint32_t size);
void paging_destroy_user(uint32_t pgdir);

#endif
