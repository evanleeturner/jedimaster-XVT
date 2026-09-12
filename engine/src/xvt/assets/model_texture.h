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
	uint8_t baseTexels[64];
	uint8_t indexedShadeTable[4096];
	uint16_t rgb565ShadeTable[4096];
};

struct ModelTextureDefaultTexture {
	OptTextureData header;
	ModelTextureDefaultTextureData data;
};

void ModelTexture_FilterHardwarePalette(uint16_t* palette);
int ModelTexture_IsHardwareFormat555(void);
void ModelTexture_BuildPalettedShadeTable(uint8_t* dst, const uint8_t* rgb24, int width, int height);
#ifndef XVT_MODERN
size_t ModelTexture_LoadRgbOrTexFile(uint8_t* dst, const char* fileName);
#endif

#ifdef __cplusplus
}
#endif

#endif
