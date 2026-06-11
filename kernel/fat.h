#ifndef FAT_H
#define FAT_H

#include <stdint.h>

/* Read-only FAT16 driver for the first FAT partition in the MBR.
 * Root directory only — no subdirectory traversal yet. */

int fat_mount(void);            /* 0 on success */
void fat_ls(void);
int fat_cat(const char *name);  /* 0 on success */

#endif
