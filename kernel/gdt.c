#include "gdt.h"
#include "string.h"

/* Replaces the minimal boot-sector GDT with one that also has ring-3
 * code/data segments and a TSS (needed for ring 3 -> ring 0 interrupt
 * stack switching). */

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss {
    uint32_t prev, esp0;
    uint16_t ss0, pad0;
    uint32_t unused[23];
    uint16_t trap, iomap;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr gdtp;
static struct tss tss;

static void gdt_set(int n, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran)
{
    gdt[n].limit_lo = (uint16_t)limit;
    gdt[n].base_lo = (uint16_t)base;
    gdt[n].base_mid = (uint8_t)(base >> 16);
    gdt[n].access = access;
    gdt[n].gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[n].base_hi = (uint8_t)(base >> 24);
}

void tss_set_esp0(uint32_t esp0)
{
    tss.esp0 = esp0;
}

void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC0);     /* kernel code */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);     /* kernel data */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC0);     /* user code */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC0);     /* user data */

    memset(&tss, 0, sizeof(tss));
    tss.ss0 = SEL_KDATA;
    tss.esp0 = 0x90000;
    tss.iomap = sizeof(tss);
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint32_t)gdt;

    __asm__ volatile (
        "lgdt %0\n"
        "ljmp %1, $1f\n"
        "1:\n"
        "mov %2, %%eax\n"
        "mov %%eax, %%ds\n"
        "mov %%eax, %%es\n"
        "mov %%eax, %%fs\n"
        "mov %%eax, %%gs\n"
        "mov %%eax, %%ss\n"
        "mov %3, %%eax\n"
        "ltr %%ax\n"
        : : "m"(gdtp), "i"(SEL_KCODE), "i"(SEL_KDATA), "i"(SEL_TSS)
        : "eax", "memory");
}
