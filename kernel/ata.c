#include "ata.h"
#include "io.h"

#define ATA_DATA    0x1F0
#define ATA_ERROR   0x1F1
#define ATA_COUNT   0x1F2
#define ATA_LBA0    0x1F3
#define ATA_LBA1    0x1F4
#define ATA_LBA2    0x1F5
#define ATA_DRIVE   0x1F6
#define ATA_STATUS  0x1F7
#define ATA_CMD     0x1F7
#define ATA_CTRL    0x3F6

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_BSY  0x80

#define CMD_READ      0x20
#define CMD_WRITE     0x30
#define CMD_FLUSH     0xE7
#define CMD_IDENTIFY  0xEC

static char model[41];
static uint32_t total_sectors;

static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void out16(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static int ata_wait_ready(void)
{
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ST_BSY)
            continue;
        if (st & (ST_ERR | ST_DF))
            return -1;
        return 0;
    }
    return -1;
}

/* Wait for BSY to clear, then for DRQ (or an error). 0 on success. */
static int ata_wait_drq(void)
{
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ST_BSY)
            continue;
        if (st & (ST_ERR | ST_DF))
            return -1;
        if (st & ST_DRQ)
            return 0;
    }
    return -1;
}

int ata_init(void)
{
    outb(ATA_CTRL, 0x02);       /* nIEN: we poll, no IRQ14 */

    outb(ATA_DRIVE, 0xA0);      /* select master */
    outb(ATA_COUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_CMD, CMD_IDENTIFY);

    if (inb(ATA_STATUS) == 0)   /* no drive */
        return -1;
    if (ata_wait_drq() < 0)
        return -1;

    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(ATA_DATA);

    /* Model string: words 27-46, big-endian byte pairs. */
    for (int i = 0; i < 20; i++) {
        model[i * 2]     = (char)(id[27 + i] >> 8);
        model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    model[40] = '\0';
    for (int i = 39; i >= 0 && model[i] == ' '; i--)
        model[i] = '\0';

    total_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    return 0;
}

int ata_read(uint32_t lba, uint32_t count, void *buf)
{
    uint16_t *out = buf;

    while (count--) {
        if (lba >= (1u << 28))
            return -1;
        outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        outb(ATA_COUNT, 1);
        outb(ATA_LBA0, (uint8_t)lba);
        outb(ATA_LBA1, (uint8_t)(lba >> 8));
        outb(ATA_LBA2, (uint8_t)(lba >> 16));
        outb(ATA_CMD, CMD_READ);

        if (ata_wait_drq() < 0)
            return -1;
        for (int i = 0; i < 256; i++)
            *out++ = inw(ATA_DATA);
        lba++;
    }
    return 0;
}

int ata_write(uint32_t lba, uint32_t count, const void *buf)
{
    const uint16_t *in = buf;

    while (count--) {
        if (lba >= (1u << 28))
            return -1;
        outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        outb(ATA_COUNT, 1);
        outb(ATA_LBA0, (uint8_t)lba);
        outb(ATA_LBA1, (uint8_t)(lba >> 8));
        outb(ATA_LBA2, (uint8_t)(lba >> 16));
        outb(ATA_CMD, CMD_WRITE);

        if (ata_wait_drq() < 0)
            return -1;
        for (int i = 0; i < 256; i++)
            out16(ATA_DATA, *in++);
        if (ata_wait_ready() < 0)
            return -1;
        lba++;
    }
    outb(ATA_CMD, CMD_FLUSH);
    return ata_wait_ready();
}

const char *ata_model(void)
{
    return model;
}

uint32_t ata_sectors(void)
{
    return total_sectors;
}
