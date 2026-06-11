#include "idt.h"
#include "io.h"
#include "console.h"
#include "timer.h"
#include "keyboard.h"
#include "serial.h"

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

#define DECL_ISR(n) extern void isr##n(void);
#define ISR_LIST \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) \
    X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) \
    X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) \
    X(46) X(47)

#define X(n) DECL_ISR(n)
ISR_LIST
#undef X

static void idt_set(int n, void (*handler)(void))
{
    uint32_t base = (uint32_t)handler;
    idt[n].base_lo = base & 0xFFFF;
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;        /* present, ring 0, 32-bit interrupt gate */
    idt[n].base_hi = (base >> 16) & 0xFFFF;
}

/* Remap the 8259 PICs so IRQs 0-15 land on vectors 32-47. */
static void pic_remap(void)
{
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();    /* master offset 32 */
    outb(0xA1, 0x28); io_wait();    /* slave offset 40 */
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    /* Unmask only the timer (0), keyboard (1), cascade (2), COM1 (4). */
    outb(0x21, (uint8_t)~0x17);
    outb(0xA1, 0xFF);
}

static const char *exception_names[32] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow",
    "bound range", "invalid opcode", "device not available",
    "double fault", "coprocessor overrun", "invalid TSS",
    "segment not present", "stack fault", "general protection",
    "page fault", "reserved", "x87 FP", "alignment check",
    "machine check", "SIMD FP", "virtualization", "control protection",
    "reserved", "reserved", "reserved", "reserved", "reserved",
    "reserved", "hypervisor", "VMM communication", "security", "reserved",
};

void isr_handler(struct regs *r)
{
    if (r->int_no < 32) {
        kprintf("\nPANIC: exception %u (%s), err=%x, eip=%p\n",
                r->int_no, exception_names[r->int_no], r->err_code, r->eip);
        for (;;)
            __asm__ volatile ("cli; hlt");
    }

    switch (r->int_no) {
    case 32:
        timer_tick();
        break;
    case 33:
        keyboard_irq();
        break;
    case 36:
        while (serial_data_ready())
            console_input(serial_read());
        break;
    default:
        break;
    }

    if (r->int_no >= 40)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void idt_init(void)
{
    pic_remap();

#define X(n) idt_set(n, isr##n);
    ISR_LIST
#undef X

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(idtp));
    __asm__ volatile ("sti");
}
