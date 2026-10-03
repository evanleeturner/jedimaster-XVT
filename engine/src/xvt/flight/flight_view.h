#ifndef XVT_FLIGHT_FLIGHT_VIEW_H
#define XVT_FLIGHT_FLIGHT_VIEW_H

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The viewport and camera matrix PushFlightViewport saves in
 * g_savedFlightViewport before it sets a new viewport, and PopFlightViewport
 * puts back. Those functions use the copy of this struct that flight_sw.c
 * defines for itself, with the same fields; no file that includes this header
 * uses this one. */
struct FlightViewportSaveState {
	uint16_t viewportX;  /* g_flightVpX: the viewport's left column. */
	uint16_t pad02;	     /* Never read or written. */
	uint16_t viewportY;  /* g_flightVpY: the viewport's top row. */
	uint16_t pad06;	     /* Never read or written. */
	int camMatR0_X;	     /* g_camMatR0_X. */
	int camMatR1_X;	     /* g_camMatR1_X. */
	int camMatR0_Y;	     /* g_camMatR0_Y. */
	int camMatR1_Y;	     /* g_camMatR1_Y. */
	int camMatR2_X;	     /* g_camMatR2_X. */
	int camMatR0_Z;	     /* g_camMatR0_Z. */
	int camMatR1_Z;	     /* g_camMatR1_Z. */
	int camMatR2_Y;	     /* g_camMatR2_Y. */
	int camMatR2_Z;	     /* g_camMatR2_Z. */
	uint16_t baseOffset; /* g_flightVpBaseOffset, cut to 16 bits. */
	uint16_t pad2E;	     /* Never read or written. */
	uint16_t height;     /* g_flightVpHeight. */
	uint16_t pad32;	     /* Never read or written. */
	uint16_t width;	     /* g_flightVpWidth. */
	uint16_t pad36;	     /* Never read or written. */
};

extern int g_camRelWorldX;
extern int g_camRelWorldY;
extern int g_camRelWorldZ;
extern uint16_t g_flightInitialTextureCacheFlushPending;

/* Argument of FlightView_ScaleQ15, which nothing calls. */
struct FlightViewScale {
	int value; /* Number to scale. */
	int scale; /* Q15 factor (32,768 is 1.0). */
};

static __inline int FlightView_ScaleQ15(struct FlightViewScale operation)
{
	operation.value =
		(int)(((int64_t)operation.value * operation.scale) >> 15);
	return operation.value;
}

HRESULT FlightView_CompositeMaskedSoftwareSurface(void);
extern int g_currentObjectBoundsExtent;
int16_t FlightView_RotateViewByInput(int pitchStep, int yawOrRollStep,
				     int playerIdx);
void FlightView_UpdatePlayerCamera(int playerIdx);
void FlightView_Render(void);
int FlightView_ComputeObjectViewPosition(uint16_t objectIdx);
int FlightView_ProjectAndTestSphereVisible(int objectIdx,
					   unsigned int sphereRadius);
int FlightView_CullWorldSphereToViewport(int worldX, int worldY, int worldZ,
					 int sphereRadius);
void FlightView_RenderStartupFrame(void);
void FlightView_RenderFrame(void);

#ifdef __cplusplus
}
#endif

#endif
