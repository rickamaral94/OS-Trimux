#define _GNU_SOURCE
#include "fatgrow.h"
#include "util.h"

#include <errno.h>
#include <stdlib.h>
#include <linux/fs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR 512u
#define FAT32_MAX_CLUSTERS 0x0FFFFFF5u

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int rd(int fd, uint64_t sector, uint8_t *buf)
{
    return pread(fd, buf, SECTOR, (off_t)(sector * SECTOR)) == (ssize_t)SECTOR ? 0 : -1;
}

static int wr(int fd, uint64_t sector, const uint8_t *buf)
{
    return pwrite(fd, buf, SECTOR, (off_t)(sector * SECTOR)) == (ssize_t)SECTOR ? 0 : -1;
}

int tm_fatgrow_device_size(int fd, uint64_t *bytes)
{
    struct stat st;
    if (fstat(fd, &st) != 0)
        return -1;
    if (S_ISBLK(st.st_mode))
        return ioctl(fd, BLKGETSIZE64, bytes) == 0 ? 0 : -1;
    if (S_ISREG(st.st_mode)) {
        *bytes = (uint64_t)st.st_size;
        return 0;
    }
    return -1;
}

#define FAIL(...)                                                                                       \
    do {                                                                                                \
        if (err)                                                                                        \
            snprintf(err, errsz, __VA_ARGS__);                                                          \
        return -1;                                                                                      \
    } while (0)

int tm_fatgrow_plan(int fd, uint64_t dev_bytes, TmFatGrowPlan *plan, char *err, size_t errsz)
{
    uint8_t mbr[SECTOR], vbr[SECTOR], fsi[SECTOR], bak[SECTOR];
    memset(plan, 0, sizeof *plan);
    plan->dev_sectors = dev_bytes / SECTOR;
    if (rd(fd, 0, mbr) != 0)
        FAIL("cannot read MBR");
    if (mbr[510] != 0x55 || mbr[511] != 0xAA)
        FAIL("MBR signature missing");
    const uint8_t *pe = mbr + 446;
    for (int i = 1; i < 4; i++)
        if (pe[16 * i + 4] != 0)
            FAIL("card has more than one partition; refusing to modify it");
    if (pe[0] != 0x00 && pe[0] != 0x80)
        FAIL("invalid partition status byte");
    plan->part_type = pe[4];
    if (plan->part_type != 0x0B && plan->part_type != 0x0C)
        FAIL("partition 1 is not FAT32 (type 0x%02x)", plan->part_type);
    plan->part_start = le32(pe + 8);
    plan->part_len = le32(pe + 12);
    if (plan->part_start == 0 || (uint64_t)plan->part_start + plan->part_len > plan->dev_sectors)
        FAIL("partition table does not fit the device");

    if (rd(fd, plan->part_start, vbr) != 0)
        FAIL("cannot read boot sector");
    if (vbr[510] != 0x55 || vbr[511] != 0xAA || (vbr[0] != 0xEB && vbr[0] != 0xE9))
        FAIL("boot sector signature missing");
    uint16_t bps = le16(vbr + 11);
    uint8_t spc = vbr[13];
    uint16_t rsvd = le16(vbr + 14);
    uint8_t nfats = vbr[16];
    uint32_t fatsz = le32(vbr + 36);
    if (bps != SECTOR || spc == 0 || (spc & (spc - 1)) || rsvd == 0 || nfats != 2 ||
        le16(vbr + 17) != 0 || le16(vbr + 19) != 0 || le16(vbr + 22) != 0 || fatsz == 0)
        FAIL("boot sector is not a supported FAT32 layout");
    if (memcmp(vbr + 82, "FAT32   ", 8) != 0)
        FAIL("filesystem type is not FAT32");
    if (le32(vbr + 28) != plan->part_start)
        FAIL("hidden-sectors field does not match the partition start");
    plan->fs_total = le32(vbr + 32);
    if (plan->fs_total == 0 || plan->fs_total > plan->part_len)
        FAIL("filesystem larger than its partition");
    plan->fsinfo_sector = le16(vbr + 48);
    plan->backup_sector = le16(vbr + 50);
    if (plan->fsinfo_sector == 0 || plan->fsinfo_sector >= rsvd || plan->backup_sector == 0 ||
        plan->backup_sector + 1u >= rsvd)
        FAIL("FSInfo/backup sectors outside the reserved area");
    if (rd(fd, plan->part_start + plan->fsinfo_sector, fsi) != 0 || le32(fsi) != 0x41615252u ||
        le32(fsi + 484) != 0x61417272u || le32(fsi + 508) != 0xAA550000u)
        FAIL("FSInfo sector invalid");
    if (rd(fd, plan->part_start + plan->backup_sector, bak) != 0 || memcmp(bak, vbr, 90) != 0)
        FAIL("backup boot sector differs from the primary; run a disk check first");

    uint64_t data_start = (uint64_t)rsvd + (uint64_t)nfats * fatsz;
    uint64_t fat_clusters = (uint64_t)fatsz * SECTOR / 4u - 2u;
    if (fat_clusters > FAT32_MAX_CLUSTERS)
        fat_clusters = FAT32_MAX_CLUSTERS;
    uint64_t max_total = data_start + fat_clusters * spc;
    if (max_total > 0xFFFFFFFFull)
        max_total = 0xFFFFFFFFull;
    plan->max_total = (uint32_t)max_total;

    uint64_t avail = plan->dev_sectors - plan->part_start;
    if (avail > 0xFFFFFFFFull - plan->part_start)
        avail = 0xFFFFFFFFull - plan->part_start; /* MBR addressing limit */
    uint64_t target = avail < max_total ? avail : max_total;
    if (target <= data_start)
        FAIL("device too small");
    target = data_start + ((target - data_start) / spc) * spc; /* whole clusters only */
    if (target <= plan->fs_total || target - plan->fs_total < TM_FATGROW_MIN_GAIN_SECTORS)
        return 1;
    plan->new_total = (uint32_t)target;
    return 0;
}

int tm_fatgrow_apply(int fd, const TmFatGrowPlan *plan, char *err, size_t errsz)
{
    uint8_t mbr[SECTOR], s[SECTOR];
    if (!plan->new_total || plan->new_total <= plan->fs_total)
        FAIL("nothing to apply");
    /* 1) partition length first: a partition larger than its filesystem is valid */
    if (rd(fd, 0, mbr) != 0)
        FAIL("cannot re-read MBR");
    if (le32(mbr + 446 + 8) != plan->part_start)
        FAIL("partition table changed since planning");
    if (plan->new_total > le32(mbr + 446 + 12)) {
        put32(mbr + 446 + 12, plan->new_total);
        mbr[446 + 5] = 0xFE; /* end CHS: LBA addressing only */
        mbr[446 + 6] = 0xFF;
        mbr[446 + 7] = 0xFF;
        if (wr(fd, 0, mbr) != 0 || fsync(fd) != 0)
            FAIL("writing the partition table failed: %s", strerror(errno));
    }
    /* 2) backup boot sector, 3) primary boot sector */
    uint64_t secs[2] = {plan->part_start + plan->backup_sector, plan->part_start};
    for (int i = 0; i < 2; i++) {
        if (rd(fd, secs[i], s) != 0)
            FAIL("cannot read boot sector");
        put32(s + 32, plan->new_total);
        if (wr(fd, secs[i], s) != 0 || fsync(fd) != 0)
            FAIL("writing the boot sector failed: %s", strerror(errno));
    }
    /* 4) FSInfo free count becomes "unknown" (0xFFFFFFFF): the OS recounts */
    uint64_t fsis[2] = {plan->part_start + plan->fsinfo_sector,
                        plan->part_start + plan->backup_sector + plan->fsinfo_sector};
    for (int i = 0; i < 2; i++) {
        if (rd(fd, fsis[i], s) != 0 || le32(s) != 0x41615252u)
            continue; /* the backup FSInfo copy is optional */
        put32(s + 488, 0xFFFFFFFFu);
        if (wr(fd, fsis[i], s) != 0)
            FAIL("writing FSInfo failed: %s", strerror(errno));
    }
    if (fsync(fd) != 0)
        FAIL("fsync failed: %s", strerror(errno));
    return 0;
}

int tm_card_device(const char *sd_root, char *part, size_t ps, char *disk, size_t ds)
{
    char mounts[512];
    const char *root = getenv("TRIMUX_SYSFS_ROOT");
    snprintf(mounts, sizeof mounts, "%s/proc/mounts", root ? root : "");
    char *txt = tm_read_file(mounts, 262144, NULL);
    if (!txt)
        return -1;
    int found = 0;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char dev[256], mnt[256];
        if (sscanf(line, "%255s %255s", dev, mnt) == 2 && strcmp(mnt, sd_root) == 0) {
            tm_strlcpy(part, dev, ps);
            found = 1; /* the last mount wins, as in the kernel's view */
        }
    }
    free(txt);
    size_t n = found ? strlen(part) : 0;
    /* only /dev/mmcblkNp1 is accepted: a whole-disk filesystem cannot grow */
    if (!found || !tm_starts_with(part, "/dev/mmcblk") || n < 3 || strcmp(part + n - 2, "p1") != 0)
        return -1;
    if (tm_strlcpy(disk, part, ds) != 0)
        return -1;
    disk[n - 2] = '\0';
    return 0;
}
