/* Growing the card's FAT32 partition after flashing the image.
 *
 * The TriMux image is built with FAT tables large enough for a 1 TiB card
 * (see scripts/make_image.py), so growing the filesystem only requires
 * metadata updates: the MBR partition length, TotSec32 in the boot sector and
 * its backup, and invalidating the FSInfo free-cluster hint. No FAT or data
 * cluster is moved or rewritten. Writes are ordered so an interruption leaves
 * a valid (if not fully grown) filesystem: partition first, then boot sectors.
 */
#ifndef TRIMUX_FATGROW_H
#define TRIMUX_FATGROW_H

#include <stddef.h>
#include <stdint.h>

#define TM_FATGROW_MIN_GAIN_SECTORS 131072u /* 64 MiB: below this, do nothing */

typedef struct {
    uint64_t dev_sectors;
    uint32_t part_start, part_len;
    uint8_t part_type;
    uint32_t fs_total;
    uint32_t max_total; /* limit imposed by the FAT size */
    uint32_t new_total; /* 0 when no growth is possible/needed */
    uint16_t backup_sector, fsinfo_sector;
} TmFatGrowPlan;

/* Inspects an image or block device. Returns 0 when growth is possible,
 * 1 when nothing needs to be done, -1 on any validation error (err filled). */
int tm_fatgrow_plan(int fd, uint64_t dev_bytes, TmFatGrowPlan *plan, char *err, size_t errsz);
/* Applies a plan produced by tm_fatgrow_plan on the same fd. */
int tm_fatgrow_apply(int fd, const TmFatGrowPlan *plan, char *err, size_t errsz);
/* Finds the partition mounted at sd_root (must be /dev/mmcblkNp1) and its
 * disk (/dev/mmcblkN) from /proc/mounts. Returns 0 on success. */
int tm_card_device(const char *sd_root, char *part, size_t ps, char *disk, size_t ds);
/* Size of a regular file or block device. */
int tm_fatgrow_device_size(int fd, uint64_t *bytes);

#endif
