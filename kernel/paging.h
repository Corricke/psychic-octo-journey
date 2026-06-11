#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE 4096u

/* User virtual layout: one 4 MiB page-table window. Code/data grow up
 * from USER_BASE, the stack sits at the top of the window. */
#define USER_BASE      0x40000000u
#define USER_STACK_TOP 0x40400000u
#define USER_STACK_PAGES 4
#define USER_IMAGE_MAX (0x400000u - USER_STACK_PAGES * PAGE_SIZE)

#define PTE_P  0x001
#define PTE_W  0x002
#define PTE_U  0x004
#define PDE_PS 0x080

void paging_init(void);
uint32_t kernel_pgdir(void);

/* Create an empty user address space (stack pages mapped); 0 on failure. */
uint32_t paging_new_user(void);
/* Ensure a user page is mapped; returns the backing frame's physical
 * address (zeroed when fresh), or 0 on failure. */
uint32_t paging_user_page(uint32_t pgdir, uint32_t vaddr);
void paging_destroy_user(uint32_t pgdir);

#endif
