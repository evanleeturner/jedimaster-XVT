#ifndef XVT_ASSETS_MODEL_TEXTURE_H
#define XVT_ASSETS_MODEL_TEXTURE_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ModelTextureDefaultTextureData {
	uint8_t baseTexels[64]; /* The 8 by 8 texels. */
	/* 16 shades of the 256 colors as g_swPalette indices, shade by
	 * shade. */
	uint8_t indexedShadeTable[4096];
	/* The same 16 shades as RGB565 colors. */
	uint16_t rgb565ShadeTable[4096];
};

struct ModelTextureDefaultTexture {
	/* Its header, 8 by 8 with palette 256 and inlinePaletteCount 16, which
	 * RenderScene sets the first time it draws a model. */
	OptTextureData header;
	/* The texels and shades ModelTexture_BuildPalettedShadeTable writes. */
	ModelTextureDefaultTextureData data;
};

void ModelTexture_FilterHardwarePalette(uint16_t *palette);
int ModelTexture_IsHardwareFormat555(void);
void ModelTexture_BuildPalettedShadeTable(uint8_t *dst, const uint8_t *rgb24,
					  int width, int height);
#ifndef XVT_MODERN
size_t ModelTexture_LoadRgbOrTexFile(uint8_t *dst, const char *fileName);
#endif

#ifdef __cplusplus
}
#endif

#endif
