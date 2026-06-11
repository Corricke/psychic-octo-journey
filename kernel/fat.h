#ifndef FAT_H
#define FAT_H

#include <stdint.h>

/*
 * FAT16 driver for the first FAT partition in the MBR. Supports
 * subdirectories and a current working directory; paths use '/' and may
 * be absolute or relative. Names are plain 8.3 (no long file names).
 */

int fat_mount(void);            /* 0 on success */

int fat_ls(const char *path);   /* "" lists the cwd */
int fat_cat(const char *path);
void *fat_read_file(const char *path, uint32_t *size); /* kmalloc'd */

int fat_write_file(const char *path, const void *data, uint32_t size);
int fat_rm(const char *path);
int fat_mkdir(const char *path);

int fat_cd(const char *path);
const char *fat_cwd(void);      /* e.g. "/", "/DOCS" */

#endif
