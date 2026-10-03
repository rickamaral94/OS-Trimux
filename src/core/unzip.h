/* Minimal .zip extractor (the firmware's BusyBox has no unzip).
 *
 * Supports stored and deflated entries (the zlib inflater of stb_image),
 * checks every CRC-32, keeps the Unix permission bits (launch scripts and
 * binaries stay executable) and refuses entries that would land outside the
 * destination (absolute paths, "..", symlinks). Zip64 archives are refused. */
#ifndef TRIMUX_UNZIP_H
#define TRIMUX_UNZIP_H

#include <stddef.h>
#include <stdint.h>

uint32_t tm_crc32(uint32_t crc, const void *data, size_t len);
/* Extracts every entry of zip into dest (created). Returns the number of
 * files written, or -1 (dest may then hold a partial tree). */
int tm_unzip(const char *zip, const char *dest);

#endif
