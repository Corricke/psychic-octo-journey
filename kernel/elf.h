#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* Minimal ELF32 executable loader for user processes. */

int elf_load(uint32_t pgdir, const void *image, uint32_t size,
             uint32_t *entry);

#endif
