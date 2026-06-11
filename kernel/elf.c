#include "elf.h"
#include "paging.h"
#include "string.h"

struct elf32_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

#define PT_LOAD 1

/* Copy one PT_LOAD segment into the address space page by page.
 * Frames come zeroed, which covers the p_memsz > p_filesz (.bss) tail. */
static int load_segment(uint32_t pgdir, const uint8_t *image,
                        const struct elf32_phdr *ph)
{
    for (uint32_t va = ph->p_vaddr & ~(PAGE_SIZE - 1);
         va < ph->p_vaddr + ph->p_memsz; va += PAGE_SIZE) {
        uint32_t frame = paging_user_page(pgdir, va);
        if (!frame)
            return -1;

        /* Intersection of this page with the segment's file data. */
        uint32_t start = va > ph->p_vaddr ? va : ph->p_vaddr;
        uint32_t fend = ph->p_vaddr + ph->p_filesz;
        uint32_t end = va + PAGE_SIZE < fend ? va + PAGE_SIZE : fend;
        if (start < end)
            memcpy((uint8_t *)frame + (start - va),
                   image + ph->p_offset + (start - ph->p_vaddr),
                   end - start);
    }
    return 0;
}

int elf_load(uint32_t pgdir, const void *image, uint32_t size,
             uint32_t *entry)
{
    const uint8_t *img = image;
    const struct elf32_ehdr *eh = image;

    if (size < sizeof(*eh) ||
        eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 1 ||      /* ELFCLASS32 */
        eh->e_ident[5] != 1 ||      /* little-endian */
        eh->e_type != 2 ||          /* ET_EXEC */
        eh->e_machine != 3)         /* EM_386 */
        return -1;

    if (eh->e_phentsize < sizeof(struct elf32_phdr) || eh->e_phnum == 0 ||
        eh->e_phoff + (uint32_t)eh->e_phnum * eh->e_phentsize > size)
        return -1;

    for (int i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr *ph = (const struct elf32_phdr *)
            (img + eh->e_phoff + (uint32_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_filesz > ph->p_memsz ||
            ph->p_offset + ph->p_filesz > size ||
            ph->p_vaddr < USER_BASE ||
            ph->p_vaddr + ph->p_memsz < ph->p_vaddr ||
            ph->p_vaddr + ph->p_memsz > USER_BASE + USER_IMAGE_MAX)
            return -1;
        if (load_segment(pgdir, img, ph) < 0)
            return -1;
    }

    if (eh->e_entry < USER_BASE || eh->e_entry >= USER_BASE + USER_IMAGE_MAX)
        return -1;
    *entry = eh->e_entry;
    return 0;
}
