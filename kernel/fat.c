#include "fat.h"
#include "ata.h"
#include "heap.h"
#include "console.h"
#include "string.h"

#define SECTOR_SIZE 512

#define ATTR_VOLUME 0x08
#define ATTR_DIR    0x10
#define ATTR_LFN    0x0F

#define CLUSTER_FREE 0x0000
#define CLUSTER_EOC  0xFFFF
#define IS_EOC(c)    ((c) >= 0xFFF8)

struct dirent {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  reserved[8];
    uint16_t cluster_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed));

/* Where a directory entry lives on disk, so it can be rewritten. */
struct loc {
    uint32_t lba;
    uint32_t off;
};

static struct {
    int mounted;
    uint32_t part_lba;
    uint32_t fat_lba;
    uint32_t fat_sectors;
    uint32_t num_fats;
    uint32_t root_lba;
    uint32_t root_sectors;
    uint32_t data_lba;
    uint32_t root_entries;
    uint32_t sectors_per_cluster;
    uint32_t max_cluster;       /* highest valid cluster number */
} fs;

/* cluster 0 stands for the root directory throughout this driver */
static uint16_t cwd_cluster;
static char cwd_path[128] = "/";

static uint8_t sector_buf[SECTOR_SIZE];

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fat_mount(void)
{
    if (ata_read(0, 1, sector_buf) < 0)
        return -1;
    if (rd16(sector_buf + 510) != 0xAA55)
        return -1;

    uint32_t part_lba = 0, part_sectors = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = sector_buf + 446 + i * 16;
        uint8_t type = e[4];
        if (type == 0x04 || type == 0x06 || type == 0x0E) {
            part_lba = rd32(e + 8);
            part_sectors = rd32(e + 12);
            break;
        }
    }
    if (!part_lba)
        return -1;

    if (ata_read(part_lba, 1, sector_buf) < 0)
        return -1;
    uint16_t bytes_per_sector = rd16(sector_buf + 11);
    uint8_t  spc              = sector_buf[13];
    uint16_t reserved         = rd16(sector_buf + 14);
    uint8_t  num_fats         = sector_buf[16];
    uint16_t root_entries     = rd16(sector_buf + 17);
    uint16_t fat_size         = rd16(sector_buf + 22);

    if (bytes_per_sector != SECTOR_SIZE || !spc || !num_fats || !fat_size)
        return -1;

    fs.part_lba = part_lba;
    fs.sectors_per_cluster = spc;
    fs.root_entries = root_entries;
    fs.num_fats = num_fats;
    fs.fat_sectors = fat_size;
    fs.fat_lba = part_lba + reserved;
    fs.root_lba = fs.fat_lba + (uint32_t)num_fats * fat_size;
    fs.root_sectors = (root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    fs.data_lba = fs.root_lba + fs.root_sectors;
    fs.max_cluster = 1 + (part_sectors - (fs.data_lba - part_lba)) / spc;
    fs.mounted = 1;
    cwd_cluster = 0;
    cwd_path[0] = '/';
    cwd_path[1] = '\0';
    return 0;
}

/* ---- FAT (cluster chain) access ------------------------------------- */

static uint16_t fat_get(uint16_t cluster)
{
    uint32_t off = (uint32_t)cluster * 2;
    if (ata_read(fs.fat_lba + off / SECTOR_SIZE, 1, sector_buf) < 0)
        return CLUSTER_EOC;
    return rd16(sector_buf + off % SECTOR_SIZE);
}

static int fat_set(uint16_t cluster, uint16_t value)
{
    uint32_t off = (uint32_t)cluster * 2;
    uint32_t sec = off / SECTOR_SIZE;

    for (uint32_t f = 0; f < fs.num_fats; f++) {
        uint32_t lba = fs.fat_lba + f * fs.fat_sectors + sec;
        if (ata_read(lba, 1, sector_buf) < 0)
            return -1;
        sector_buf[off % SECTOR_SIZE] = (uint8_t)value;
        sector_buf[off % SECTOR_SIZE + 1] = (uint8_t)(value >> 8);
        if (ata_write(lba, 1, sector_buf) < 0)
            return -1;
    }
    return 0;
}

static uint16_t cluster_alloc(void)
{
    for (uint16_t c = 2; c <= fs.max_cluster; c++) {
        if (fat_get(c) == CLUSTER_FREE) {
            if (fat_set(c, CLUSTER_EOC) < 0)
                return 0;
            return c;
        }
    }
    return 0;                   /* disk full */
}

static void chain_free(uint16_t cluster)
{
    while (cluster >= 2 && !IS_EOC(cluster)) {
        uint16_t next = fat_get(cluster);
        fat_set(cluster, CLUSTER_FREE);
        cluster = next;
    }
}

static uint32_t cluster_lba(uint16_t cluster)
{
    return fs.data_lba + (uint32_t)(cluster - 2) * fs.sectors_per_cluster;
}

/* ---- directory iteration --------------------------------------------- */

/* Iterates 32-byte entries of a directory: the fixed root area when
 * cluster == 0, a cluster chain otherwise. */
struct dir_iter {
    uint16_t cluster;           /* current cluster (0 = root area) */
    uint32_t sector;            /* sector index within root/cluster */
    uint32_t entry;             /* entry index within sector */
    int done;
};

static void dir_begin(struct dir_iter *it, uint16_t dir_cluster)
{
    it->cluster = dir_cluster;
    it->sector = 0;
    it->entry = 0;
    it->done = 0;
}

/* Advance to the next raw entry. Returns 0 at end of directory.
 * Fills *d and *where (the entry's disk location). */
static int dir_next(struct dir_iter *it, struct dirent *d, struct loc *where)
{
    if (it->done)
        return 0;

    for (;;) {
        uint32_t lba;
        if (it->cluster == 0) {
            if (it->sector >= fs.root_sectors)
                return 0;
            lba = fs.root_lba + it->sector;
        } else {
            if (it->sector >= fs.sectors_per_cluster) {
                uint16_t next = fat_get(it->cluster);
                if (IS_EOC(next))
                    return 0;
                it->cluster = next;
                it->sector = 0;
            }
            lba = cluster_lba(it->cluster) + it->sector;
        }

        if (ata_read(lba, 1, sector_buf) < 0)
            return 0;
        while (it->entry < SECTOR_SIZE / 32) {
            struct dirent *e =
                (struct dirent *)(sector_buf + it->entry * 32);
            where->lba = lba;
            where->off = it->entry * 32;
            it->entry++;
            *d = *e;
            return 1;
        }
        it->entry = 0;
        it->sector++;
    }
}

static int entry_in_use(const struct dirent *d)
{
    return d->name[0] != 0x00 && d->name[0] != 0xE5 &&
           d->attr != ATTR_LFN && !(d->attr & ATTR_VOLUME);
}

/* ---- names and paths -------------------------------------------------- */

static void format_name(const uint8_t *raw, char *out)
{
    int n = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++)
        out[n++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++)
            out[n++] = (char)raw[i];
    }
    out[n] = '\0';
}

/* Convert one path component (length len) to padded 8.3 form. */
static int to_83(const char *name, int len, uint8_t *out)
{
    memset(out, ' ', 11);
    if (len == 1 && name[0] == '.') {
        out[0] = '.';
        return 0;
    }
    if (len == 2 && name[0] == '.' && name[1] == '.') {
        out[0] = out[1] = '.';
        return 0;
    }

    int i = 0, n = 0;
    for (; i < len && name[i] != '.'; i++) {
        if (n >= 8)
            return -1;
        char c = name[i];
        out[n++] = (uint8_t)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    if (i < len && name[i] == '.') {
        i++;
        n = 8;
        for (; i < len; i++) {
            if (n >= 11 || name[i] == '.')
                return -1;
            char c = name[i];
            out[n++] = (uint8_t)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        }
    }
    return 0;
}

/* Look up one 8.3 name in a directory. Returns 1 if found. */
static int dir_find(uint16_t dir_cluster, const uint8_t *name83,
                    struct dirent *out, struct loc *where)
{
    /* "." and ".." don't exist as entries in the root directory. */
    if (dir_cluster == 0 && name83[0] == '.') {
        memset(out, 0, sizeof(*out));
        out->attr = ATTR_DIR;
        out->cluster_lo = 0;
        return 1;
    }

    struct dir_iter it;
    struct dirent d;
    struct loc l;
    dir_begin(&it, dir_cluster);
    while (dir_next(&it, &d, &l)) {
        if (d.name[0] == 0x00)
            break;
        if (!entry_in_use(&d))
            continue;
        if (memcmp(d.name, name83, 11) == 0) {
            *out = d;
            if (where)
                *where = l;
            return 1;
        }
    }
    return 0;
}

/*
 * Resolve a path. With parent_of set, resolves to the containing
 * directory and copies the final component into last83; otherwise
 * resolves the full path. Returns the directory/file via *out
 * (for the root directory, a synthetic DIR entry with cluster 0).
 * Returns 0 on success.
 */
static int resolve(const char *path, int parent_of,
                   struct dirent *out, struct loc *where, uint8_t *last83)
{
    uint16_t dir = cwd_cluster;
    if (*path == '/') {
        dir = 0;
        path++;
    }

    memset(out, 0, sizeof(*out));
    out->attr = ATTR_DIR;
    out->cluster_lo = dir;

    while (*path) {
        int len = 0;
        while (path[len] && path[len] != '/')
            len++;

        uint8_t name83[11];
        if (to_83(path, len, name83) < 0)
            return -1;

        const char *rest = path + len;
        while (*rest == '/')
            rest++;

        if (parent_of && !*rest) {
            /* `out` currently holds the parent directory. */
            memcpy(last83, name83, 11);
            return 0;
        }

        if (!(out->attr & ATTR_DIR))
            return -1;          /* path component is a file */
        if (!dir_find(out->cluster_lo, name83, out, where))
            return -1;
        path = rest;
    }
    return parent_of ? -1 : 0;
}

/* ---- read side --------------------------------------------------------- */

int fat_ls(const char *path)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return -1;
    }

    struct dirent d;
    if (resolve(path, 0, &d, 0, 0) < 0 || !(d.attr & ATTR_DIR)) {
        kprintf("not a directory: %s\n", *path ? path : "?");
        return -1;
    }

    struct dir_iter it;
    struct loc l;
    dir_begin(&it, d.cluster_lo);
    while (dir_next(&it, &d, &l)) {
        if (d.name[0] == 0x00)
            break;
        if (!entry_in_use(&d))
            continue;
        char name[13];
        format_name(d.name, name);
        if (d.attr & ATTR_DIR)
            kprintf("%s  <DIR>\n", name);
        else
            kprintf("%s  %u bytes\n", name, d.size);
    }
    return 0;
}

/* Walk a file's clusters, invoking fn(chunk, len, arg) for each piece. */
static int file_stream(const struct dirent *d,
                       void (*fn)(const uint8_t *, uint32_t, void *),
                       void *arg)
{
    uint32_t cluster_bytes = fs.sectors_per_cluster * SECTOR_SIZE;
    uint8_t *buf = kmalloc(cluster_bytes);
    if (!buf)
        return -1;

    uint32_t remaining = d->size;
    uint16_t cluster = d->cluster_lo;
    while (remaining && cluster >= 2 && !IS_EOC(cluster)) {
        if (ata_read(cluster_lba(cluster), fs.sectors_per_cluster, buf) < 0) {
            kfree(buf);
            return -1;
        }
        uint32_t chunk = remaining < cluster_bytes ? remaining : cluster_bytes;
        fn(buf, chunk, arg);
        remaining -= chunk;
        cluster = fat_get(cluster);
    }
    kfree(buf);
    return remaining ? -1 : 0;
}

static void stream_print(const uint8_t *buf, uint32_t len, void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < len; i++)
        if (buf[i] != '\r')
            console_putc((char)buf[i]);
}

static void stream_copy(const uint8_t *buf, uint32_t len, void *arg)
{
    uint8_t **dst = arg;
    memcpy(*dst, buf, len);
    *dst += len;
}

static int lookup_file(const char *path, struct dirent *d)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return -1;
    }
    if (resolve(path, 0, d, 0, 0) < 0) {
        kprintf("no such file: %s\n", path);
        return -1;
    }
    if (d->attr & ATTR_DIR) {
        kprintf("%s is a directory\n", path);
        return -1;
    }
    return 0;
}

int fat_cat(const char *path)
{
    struct dirent d;
    if (lookup_file(path, &d) < 0)
        return -1;
    if (file_stream(&d, stream_print, 0) < 0) {
        console_puts("\nread error\n");
        return -1;
    }
    return 0;
}

void *fat_read_file(const char *path, uint32_t *size)
{
    struct dirent d;
    if (lookup_file(path, &d) < 0)
        return 0;

    uint8_t *buf = kmalloc(d.size ? d.size : 1);
    if (!buf) {
        console_puts("out of memory\n");
        return 0;
    }
    uint8_t *cursor = buf;
    if (file_stream(&d, stream_copy, &cursor) < 0) {
        console_puts("read error\n");
        kfree(buf);
        return 0;
    }
    *size = d.size;
    return buf;
}

/* ---- write side --------------------------------------------------------- */

static int loc_update(const struct loc *l, const struct dirent *d)
{
    if (ata_read(l->lba, 1, sector_buf) < 0)
        return -1;
    memcpy(sector_buf + l->off, d, 32);
    return ata_write(l->lba, 1, sector_buf);
}

/* Find a reusable slot (free or end-of-directory) in a directory. */
static int dir_free_slot(uint16_t dir_cluster, struct loc *where)
{
    struct dir_iter it;
    struct dirent d;
    dir_begin(&it, dir_cluster);
    while (dir_next(&it, &d, where)) {
        if (d.name[0] == 0x00 || d.name[0] == 0xE5)
            return 0;
    }
    return -1;                  /* directory full */
}

static int cluster_zero(uint16_t cluster)
{
    memset(sector_buf, 0, SECTOR_SIZE);
    for (uint32_t s = 0; s < fs.sectors_per_cluster; s++)
        if (ata_write(cluster_lba(cluster) + s, 1, sector_buf) < 0)
            return -1;
    return 0;
}

/* Allocate a chain and write `size` bytes of data into it.
 * Returns the first cluster, or 0 (with chain freed) on failure. */
static uint16_t chain_write(const void *data, uint32_t size)
{
    if (!size)
        return 0;

    uint32_t cluster_bytes = fs.sectors_per_cluster * SECTOR_SIZE;
    uint8_t *buf = kmalloc(cluster_bytes);
    if (!buf)
        return 0;

    const uint8_t *src = data;
    uint16_t first = 0, prev = 0;
    while (size) {
        uint16_t c = cluster_alloc();
        if (!c)
            goto fail;
        if (!first)
            first = c;
        if (prev && fat_set(prev, c) < 0)
            goto fail;

        uint32_t chunk = size < cluster_bytes ? size : cluster_bytes;
        memset(buf, 0, cluster_bytes);
        memcpy(buf, src, chunk);
        if (ata_write(cluster_lba(c), fs.sectors_per_cluster, buf) < 0)
            goto fail;
        src += chunk;
        size -= chunk;
        prev = c;
    }
    kfree(buf);
    return first;

fail:
    if (first)
        chain_free(first);
    kfree(buf);
    return 0;
}

int fat_write_file(const char *path, const void *data, uint32_t size)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return -1;
    }

    struct dirent parent, d;
    struct loc where;
    uint8_t name83[11];
    if (resolve(path, 1, &parent, 0, name83) < 0 ||
        !(parent.attr & ATTR_DIR) || name83[0] == '.') {
        kprintf("bad path: %s\n", path);
        return -1;
    }

    int exists = dir_find(parent.cluster_lo, name83, &d, &where);
    if (exists) {
        if (d.attr & ATTR_DIR) {
            kprintf("%s is a directory\n", path);
            return -1;
        }
        if (d.cluster_lo >= 2)
            chain_free(d.cluster_lo);
    } else {
        if (dir_free_slot(parent.cluster_lo, &where) < 0) {
            console_puts("directory full\n");
            return -1;
        }
        memset(&d, 0, sizeof(d));
        memcpy(d.name, name83, 11);
    }

    uint16_t first = chain_write(data, size);
    if (size && !first) {
        console_puts("disk full or write error\n");
        return -1;
    }
    d.cluster_lo = first;
    d.cluster_hi = 0;
    d.size = size;
    return loc_update(&where, &d);
}

int fat_rm(const char *path)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return -1;
    }

    struct dirent d;
    struct loc where = {0, 0};
    if (resolve(path, 0, &d, &where, 0) < 0) {
        kprintf("no such file: %s\n", path);
        return -1;
    }
    if (where.lba == 0 || d.name[0] == '.') {
        kprintf("cannot remove %s\n", path);
        return -1;
    }
    if (d.attr & ATTR_DIR) {
        /* Allow removing empty directories only. */
        struct dir_iter it;
        struct dirent e;
        struct loc l;
        dir_begin(&it, d.cluster_lo);
        while (dir_next(&it, &e, &l)) {
            if (e.name[0] == 0x00)
                break;
            if (entry_in_use(&e) && e.name[0] != '.') {
                kprintf("directory not empty: %s\n", path);
                return -1;
            }
        }
    }

    if (d.cluster_lo >= 2)
        chain_free(d.cluster_lo);
    d.name[0] = 0xE5;
    return loc_update(&where, &d);
}

int fat_mkdir(const char *path)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return -1;
    }

    struct dirent parent, d;
    struct loc where;
    uint8_t name83[11];
    if (resolve(path, 1, &parent, 0, name83) < 0 ||
        !(parent.attr & ATTR_DIR) || name83[0] == '.') {
        kprintf("bad path: %s\n", path);
        return -1;
    }
    if (dir_find(parent.cluster_lo, name83, &d, 0)) {
        kprintf("already exists: %s\n", path);
        return -1;
    }
    if (dir_free_slot(parent.cluster_lo, &where) < 0) {
        console_puts("directory full\n");
        return -1;
    }

    uint16_t c = cluster_alloc();
    if (!c || cluster_zero(c) < 0) {
        console_puts("disk full or write error\n");
        return -1;
    }

    /* "." and ".." entries in the new directory's first sector. */
    memset(sector_buf, 0, SECTOR_SIZE);
    struct dirent *dot = (struct dirent *)sector_buf;
    memset(dot->name, ' ', 11);
    dot->name[0] = '.';
    dot->attr = ATTR_DIR;
    dot->cluster_lo = c;
    struct dirent *dotdot = dot + 1;
    memset(dotdot->name, ' ', 11);
    dotdot->name[0] = dotdot->name[1] = '.';
    dotdot->attr = ATTR_DIR;
    dotdot->cluster_lo = parent.cluster_lo;
    if (ata_write(cluster_lba(c), 1, sector_buf) < 0)
        return -1;

    memset(&d, 0, sizeof(d));
    memcpy(d.name, name83, 11);
    d.attr = ATTR_DIR;
    d.cluster_lo = c;
    return loc_update(&where, &d);
}

/* ---- cwd ----------------------------------------------------------------- */

/* Rebuild cwd_path from a path argument, handling "." and "..". */
static void cwd_apply(const char *path)
{
    char next[128];
    int n = 0;

    if (*path != '/') {
        for (n = 0; cwd_path[n]; n++)
            next[n] = cwd_path[n];
    } else {
        next[n++] = '/';
        while (*path == '/')
            path++;
    }

    while (*path) {
        int len = 0;
        while (path[len] && path[len] != '/')
            len++;

        if (len == 1 && path[0] == '.') {
            /* nothing */
        } else if (len == 2 && path[0] == '.' && path[1] == '.') {
            while (n > 1 && next[n - 1] != '/')
                n--;
            if (n > 1)
                n--;            /* drop the slash too */
        } else {
            if (n > 1)
                next[n++] = '/';
            for (int i = 0; i < len && n < 126; i++) {
                char c = path[i];
                next[n++] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
            }
        }
        path += len;
        while (*path == '/')
            path++;
    }
    if (n == 0)
        next[n++] = '/';
    next[n] = '\0';
    memcpy(cwd_path, next, (uint32_t)n + 1);
}

int fat_cd(const char *path)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return -1;
    }

    struct dirent d;
    if (resolve(path, 0, &d, 0, 0) < 0 || !(d.attr & ATTR_DIR)) {
        kprintf("not a directory: %s\n", path);
        return -1;
    }
    cwd_cluster = d.cluster_lo;
    cwd_apply(path);
    return 0;
}

const char *fat_cwd(void)
{
    return cwd_path;
}
