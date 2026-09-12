#ifndef XVT_RENDER_RENDER_QUAD_H
#define XVT_RENDER_RENDER_QUAD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t g_explosionBillboardColorByFrame[32];

void RenderQuad_DrawModelTexture(SceneBillboardQueueEntry* quadRecord);
void RenderQuad_DrawRotatedSprite(int angle, int screenX, int screenY, uint16_t screenSize,
								  const void* textureLevel);

#ifdef __cplusplus
}
#endif

#endif
