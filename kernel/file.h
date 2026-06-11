#ifndef FILE_H
#define FILE_H

#include <stdint.h>

/*
 * File descriptors for user processes. fds 0/1/2 are the console;
 * fds >= 3 are FAT files. Reads buffer the whole file at open; writes
 * accumulate in memory and hit the disk at close. Descriptors are
 * tracked per pid and force-closed when a process is reaped.
 */

#define O_RDONLY 0
#define O_WRONLY 1              /* create or truncate */

int file_open(int pid, const char *path, int mode);
int file_close(int pid, int fd);
int file_read(int pid, int fd, void *dst, uint32_t n);
int file_write(int pid, int fd, const void *src, uint32_t n);
void file_close_all(int pid);

#endif
