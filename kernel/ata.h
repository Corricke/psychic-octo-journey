#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* Primary-channel master drive, LBA28, polled PIO. */

int ata_init(void);             /* 0 on success */
int ata_read(uint32_t lba, uint32_t count, void *buf);

const char *ata_model(void);
uint32_t ata_sectors(void);

#endif
