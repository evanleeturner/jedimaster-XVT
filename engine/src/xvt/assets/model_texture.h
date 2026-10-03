#ifndef XVT_ASSETS_MODEL_TEXTURE_H
#define XVT_ASSETS_MODEL_TEXTURE_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct model_texture_default_texture_data {
	uint8_t base_texels[64]; /* The 8 by 8 texels. */
	/* 16 shades of the 256 colors as g_sw_palette indices, shade by
	 * shade. */
	uint8_t indexed_shade_table[4096];
	/* The same 16 shades as RGB565 colors. */
	uint16_t rgb565_shade_table[4096];
};

struct model_texture_default_texture {
	/* Its header, 8 by 8 with palette 256 and inline_palette_count 16, which
	 * RenderScene sets the first time it draws a model. */
	struct opt_texture_data header;
	/* The texels and shades model_texture_build_paletted_shade_table writes. */
	struct model_texture_default_texture_data data;
};

void model_texture_filter_hardware_palette(uint16_t *palette);
int model_texture_is_hardware_format555(void);
void model_texture_build_paletted_shade_table(uint8_t *dst,
					      const uint8_t *rgb24, int width,
					      int height);
#ifndef XVT_MODERN
size_t model_texture_load_rgb_or_tex_file(uint8_t *dst, const char *file_name);
#endif

#ifdef __cplusplus
}
#endif

#endif
