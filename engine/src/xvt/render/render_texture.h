#ifndef XVT_RENDER_RENDER_TEXTURE_H
#define XVT_RENDER_RENDER_TEXTURE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t render_texture_decode_scratch[65536];

extern int g_render_texture_cache_cursor;

struct std3d_tex_cache_node *
render_texture_find_or_allocate_cache_entry(const void *cache_key);
struct std3d_tex_cache_node *
render_texture_get_or_create_bitmap(int width, int height, uint16_t *palette,
				    const uint8_t *pixels, int rle_format);
struct std3d_tex_cache_node *render_texture_get_or_create_opaque(
	int width, int height, const uint16_t *palette, const uint8_t *pixels);
struct std3d_tex_cache_node *
render_texture_get_or_create_color_key(int width, int height, uint16_t *palette,
				       const uint8_t *pixels);

#ifdef __cplusplus
}
#endif

#endif
