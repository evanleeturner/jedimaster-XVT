#ifndef XVT_FLIGHT_FLIGHT_VIEW_H
#define XVT_FLIGHT_FLIGHT_VIEW_H

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FlightViewportSaveState {
	uint16_t viewportX;
	uint16_t pad02;
	uint16_t viewportY;
	uint16_t pad06;
	int camMatR0_X;
	int camMatR1_X;
	int camMatR0_Y;
	int camMatR1_Y;
	int camMatR2_X;
	int camMatR0_Z;
	int camMatR1_Z;
	int camMatR2_Y;
	int camMatR2_Z;
	uint16_t baseOffset;
	uint16_t pad2E;
	uint16_t height;
	uint16_t pad32;
	uint16_t width;
	uint16_t pad36;
};

extern int g_camRelWorldX;
extern int g_camRelWorldY;
extern int g_camRelWorldZ;
extern uint16_t g_flightInitialTextureCacheFlushPending;

struct FlightViewScale {
	int value;
	int scale;
};

static __inline int FlightView_ScaleQ15(struct FlightViewScale operation) {
	operation.value = (int)(((int64_t)operation.value * operation.scale) >> 15);
	return operation.value;
}

HRESULT FlightView_CompositeMaskedSoftwareSurface(void);
extern int g_currentObjectBoundsExtent;
int16_t FlightView_RotateViewByInput(int angleQ16, int rollAngleQ16, int playerIdx);
void FlightView_UpdatePlayerCamera(int playerIdx);
void FlightView_Render(void);
int FlightView_ComputeObjectViewPosition(uint16_t objectIdx);
int FlightView_IsObjectSphereVisible(int objectIdx, unsigned int sphereRadius);
int FlightView_CullWorldSphereToViewport(int worldX, int worldY, int worldZ, int sphereRadius);
void FlightView_RenderStartupFrame(void);
void FlightView_RenderFrame(void);

#ifdef __cplusplus
}
#endif

#endif
