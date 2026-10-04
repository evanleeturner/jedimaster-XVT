#ifndef XVT_UNDITHER_H
#define XVT_UNDITHER_H
#include "aeron/asset/decode_types.h"
/* Palette-aware RGB filtering. Coverage and the output alpha remain unchanged. */
/* Writes the RGB of every covered pixel of image into rgba (4 bytes per pixel,
 * row-major): a weighted average of the pixel's own palette color (weight 8)
 * and its 8 edge-clamped neighbors, uncovered ones skipped. A neighbor weighs 7
 * when it has the pixel's index; otherwise 8, 6, 1 or 0 as the nearest third
 * palette color to the pair's midpoint lies at least twice, at least once, at
 * least two thirds, or less than two thirds of the pair's own midpoint distance
 * away (squared RGB distances). Uncovered pixels and every alpha byte are left
 * as they are. Returns 0, writing nothing, for a NULL argument, an image
 * without indices or coverage, a zero width or height, a palette_count of 0 or
 * over 256, a covered index at or past palette_count, or a failed
 * allocation. */
int xvt_undither_apply(const AeronIndexedFrame *image,
		       const uint8_t palette[256][4], unsigned palette_count,
		       uint8_t *rgba);
#endif
