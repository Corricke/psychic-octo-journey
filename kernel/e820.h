#ifndef E820_H
#define E820_H

#include <stdint.h>

/* BIOS E820 memory map, collected by the boot sector into low memory. */

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed));

#define E820_USABLE 1

#define E820_COUNT (*(volatile uint32_t *)0x0500)
#define E820_MAP   ((volatile struct e820_entry *)0x0504)

#endif
