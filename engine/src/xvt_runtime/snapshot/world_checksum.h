#ifndef XVT_RUNTIME_SNAPSHOT_WORLD_CHECKSUM_H
#define XVT_RUNTIME_SNAPSHOT_WORLD_CHECKSUM_H

/* World-state checksums, the regional sums behind XvtSnapshot_ChecksumImage
 * (world_state.h). XvtSnapshot_LiveChecksum is declared in world_state.h and
 * defined beside these. */

#include <stddef.h>
#include <stdint.h>

/* Fills checksums and lengths with the regional byte sums and byte lengths of
 * the world part of image, its first prefix bytes, by the region rule
 * XvtSnapshot_ChecksumImage describes; both arrays are zeroed first and unused
 * entries stay 0. Reads the live slot ranges and flight-group count to walk
 * the image. Does not validate image: callers pass one that
 * XvtSnapshot_Validate accepts for this world. */
void XvtSnapshot_ChecksumPrefix(const uint8_t *image, size_t prefix,
				unsigned checksums[16], unsigned lengths[16]);

#endif
