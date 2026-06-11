#include "file.h"
#include "fat.h"
#include "heap.h"
#include "console.h"
#include "string.h"

#define MAX_FILES  16
#define FD_BASE    3
#define WRITE_CAP  (64u * 1024)
#define PATH_MAX   96

struct file {
    int used;
    int pid;
    int mode;
    char path[PATH_MAX];
    uint8_t *buf;
    uint32_t size;              /* valid bytes in buf */
    uint32_t pos;               /* read cursor */
};

static struct file files[MAX_FILES];

/* Resolve against the cwd at open time so a later close writes to the
 * right place even if the shell has cd'd elsewhere meanwhile. */
static int abs_path(const char *path, char *out)
{
    uint32_t n = 0;
    if (path[0] != '/') {
        const char *cwd = fat_cwd();
        n = strlen(cwd);
        memcpy(out, cwd, n);
        if (n > 1)
            out[n++] = '/';
    }
    uint32_t len = strlen(path);
    if (n + len + 1 > PATH_MAX)
        return -1;
    memcpy(out + n, path, len + 1);
    return 0;
}

static struct file *get(int pid, int fd)
{
    int i = fd - FD_BASE;
    if (i < 0 || i >= MAX_FILES || !files[i].used || files[i].pid != pid)
        return 0;
    return &files[i];
}

int file_open(int pid, const char *path, int mode)
{
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    struct file *f = &files[slot];
    memset(f, 0, sizeof(*f));
    if (abs_path(path, f->path) < 0)
        return -1;

    if (mode == O_RDONLY) {
        f->buf = fat_read_file(f->path, &f->size);
        if (!f->buf)
            return -1;
    } else if (mode == O_WRONLY) {
        f->buf = kmalloc(WRITE_CAP);
        if (!f->buf)
            return -1;
    } else {
        return -1;
    }

    f->used = 1;
    f->pid = pid;
    f->mode = mode;
    return slot + FD_BASE;
}

int file_close(int pid, int fd)
{
    struct file *f = get(pid, fd);
    if (!f)
        return -1;

    int rc = 0;
    if (f->mode == O_WRONLY)
        rc = fat_write_file(f->path, f->buf, f->size);
    kfree(f->buf);
    f->used = 0;
    return rc;
}

int file_read(int pid, int fd, void *dst, uint32_t n)
{
    struct file *f = get(pid, fd);
    if (!f || f->mode != O_RDONLY)
        return -1;

    uint32_t left = f->size - f->pos;
    if (n > left)
        n = left;
    memcpy(dst, f->buf + f->pos, n);
    f->pos += n;
    return (int)n;
}

int file_write(int pid, int fd, const void *src, uint32_t n)
{
    struct file *f = get(pid, fd);
    if (!f || f->mode != O_WRONLY)
        return -1;

    if (f->size + n > WRITE_CAP)
        return -1;
    memcpy(f->buf + f->size, src, n);
    f->size += n;
    return (int)n;
}

void file_close_all(int pid)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].used && files[i].pid == pid)
            file_close(pid, i + FD_BASE);
}
