#include "fat.h"
#include "ata.h"
#include "heap.h"
#include "console.h"
#include "string.h"

#define SECTOR_SIZE 512

#define ATTR_READONLY 0x01
#define ATTR_HIDDEN   0x02
#define ATTR_VOLUME   0x08
#define ATTR_DIR      0x10
#define ATTR_LFN      0x0F

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

static struct {
    int mounted;
    uint32_t part_lba;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t data_lba;
    uint32_t root_entries;
    uint32_t sectors_per_cluster;
} fs;

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
    /* Find the first FAT16 partition in the MBR. */
    if (ata_read(0, 1, sector_buf) < 0)
        return -1;
    if (rd16(sector_buf + 510) != 0xAA55)
        return -1;

    uint32_t part_lba = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = sector_buf + 446 + i * 16;
        uint8_t type = e[4];
        if (type == 0x04 || type == 0x06 || type == 0x0E) {
            part_lba = rd32(e + 8);
            break;
        }
    }
    if (!part_lba)
        return -1;

    /* Parse the BPB in the partition's boot sector. */
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
    fs.fat_lba = part_lba + reserved;
    fs.root_lba = fs.fat_lba + (uint32_t)num_fats * fat_size;
    fs.data_lba = fs.root_lba + (root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    fs.mounted = 1;
    return 0;
}

static uint16_t fat_next_cluster(uint16_t cluster)
{
    uint32_t off = (uint32_t)cluster * 2;
    if (ata_read(fs.fat_lba + off / SECTOR_SIZE, 1, sector_buf) < 0)
        return 0xFFFF;
    return rd16(sector_buf + off % SECTOR_SIZE);
}

/* Format a raw 8.3 directory name as "NAME.EXT" into out[13]. */
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

/* Convert a user-typed name to padded 8.3 form. -1 if it doesn't fit. */
static int to_83(const char *name, uint8_t *out)
{
    memset(out, ' ', 11);
    int i = 0, n = 0;
    for (; name[i] && name[i] != '.'; i++) {
        if (n >= 8)
            return -1;
        char c = name[i];
        out[n++] = (uint8_t)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    if (name[i] == '.') {
        i++;
        n = 8;
        for (; name[i]; i++) {
            if (n >= 11)
                return -1;
            char c = name[i];
            out[n++] = (uint8_t)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        }
    }
    return 0;
}

/* Iterate root directory entries; returns 1 with *out filled when the
 * callback-free scan matches `want` (or lists when want == 0). */
static int root_scan(const uint8_t *want, struct dirent *out)
{
    uint32_t sectors = (fs.root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;

    for (uint32_t s = 0; s < sectors; s++) {
        if (ata_read(fs.root_lba + s, 1, sector_buf) < 0)
            return 0;
        for (int i = 0; i < SECTOR_SIZE / 32; i++) {
            struct dirent *d = (struct dirent *)(sector_buf + i * 32);
            if (d->name[0] == 0x00)
                return 0;       /* end of directory */
            if (d->name[0] == 0xE5 || d->attr == ATTR_LFN ||
                (d->attr & ATTR_VOLUME))
                continue;
            if (want) {
                if (memcmp(want, d->name, 11) == 0) {
                    *out = *d;
                    return 1;
                }
            } else {
                char name[13];
                format_name(d->name, name);
                if (d->attr & ATTR_DIR)
                    kprintf("%s  <DIR>\n", name);
                else
                    kprintf("%s  %u bytes\n", name, d->size);
            }
        }
    }
    return 0;
}

void fat_ls(void)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return;
    }
    root_scan(0, 0);
}

int fat_cat(const char *name)
{
    if (!fs.mounted) {
        console_puts("no filesystem mounted\n");
        return -1;
    }

    uint8_t want[11];
    struct dirent d;
    if (to_83(name, want) < 0 || !root_scan(want, &d)) {
        kprintf("no such file: %s\n", name);
        return -1;
    }
    if (d.attr & ATTR_DIR) {
        kprintf("%s is a directory\n", name);
        return -1;
    }

    uint32_t cluster_bytes = fs.sectors_per_cluster * SECTOR_SIZE;
    uint8_t *buf = kmalloc(cluster_bytes);
    if (!buf) {
        console_puts("out of memory\n");
        return -1;
    }

    uint32_t remaining = d.size;
    uint16_t cluster = d.cluster_lo;
    while (remaining && cluster >= 2 && cluster < 0xFFF8) {
        uint32_t lba = fs.data_lba +
                       (uint32_t)(cluster - 2) * fs.sectors_per_cluster;
        if (ata_read(lba, fs.sectors_per_cluster, buf) < 0) {
            console_puts("\nread error\n");
            kfree(buf);
            return -1;
        }
        uint32_t chunk = remaining < cluster_bytes ? remaining : cluster_bytes;
        for (uint32_t i = 0; i < chunk; i++) {
            char c = (char)buf[i];
            if (c == '\r')
                continue;
            console_putc(c);
        }
        remaining -= chunk;
        cluster = fat_next_cluster(cluster);
    }
    kfree(buf);
    return 0;
}
