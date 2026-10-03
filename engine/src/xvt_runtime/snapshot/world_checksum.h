#ifndef XVT_RUNTIME_SNAPSHOT_WORLD_CHECKSUM_H
#define XVT_RUNTIME_SNAPSHOT_WORLD_CHECKSUM_H

/* World-state checksums, the regional sums behind xvt_snapshot_checksum_image
 * (world_state.h). xvt_snapshot_live_checksum is declared in world_state.h and
 * defined beside these. */

#include <stddef.h>
#include <stdint.h>

/* Fills checksums and lengths with the regional byte sums and byte lengths of
 * the world part of image, its first prefix bytes, by the region rule
 * xvt_snapshot_checksum_image describes; both arrays are zeroed first and unused
 * entries stay 0. Reads the live slot ranges and flight-group count to walk
 * the image. Does not validate image: callers pass one that
 * xvt_snapshot_validate accepts for this world. */
void xvt_snapshot_checksum_prefix(const uint8_t *image, size_t prefix,
				  unsigned checksums[16], unsigned lengths[16]);

#endif
