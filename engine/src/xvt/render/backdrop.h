#ifndef XVT_RENDER_BACKDROP_H
#define XVT_RENDER_BACKDROP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t g_backdropCamR0XSteps[16];
extern int32_t g_backdropCamR0YSteps[16];
extern int32_t g_backdropCamR0ZSteps[16];
extern int32_t g_backdropCamR1XSteps[16];
extern int32_t g_backdropCamR1YSteps[16];
extern int32_t g_backdropCamR1ZSteps[16];
extern int32_t g_backdropCamR2XSteps[16];
extern int32_t g_backdropCamR2YSteps[16];
extern int32_t g_backdropCamR2ZSteps[16];
extern uint8_t g_backdropModelTypes[64];
extern uint8_t g_backdropPackedDirections[64];
extern uint16_t g_backdropPositiveYCount;
extern uint16_t g_backdropNegativeYCount;
extern uint16_t g_backdropPositiveZCount;
extern uint16_t g_backdropNegativeZCount;
extern uint16_t g_backdropPositiveXCount;
extern uint16_t g_backdropNegativeXCount;

void Backdrop_DrawModelTexQuadAtScreen(int modelType, int screenX, int screenY, int angle);
void Backdrop_RenderCurrentRegion(void);
void Backdrop_ProjectAndDrawScreenQuad(int viewX, int viewY, int viewZ, int angle, int backdropIndex);
void Backdrop_GenerateDefaultRecords(void);

#ifdef __cplusplus
}
#endif

#endif
