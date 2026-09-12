#ifndef XVT_FLIGHT_FVIEW_H
#define XVT_FLIGHT_FVIEW_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void FVIEW_BuildCameraOrient(int16_t viewRoll, int16_t viewPitch, int16_t viewYaw, int16_t viewAngleD,
							 int16_t hudAimX, int16_t hudAimY, ObjectRecord* objRecord);
int FVIEW_SetObjectTransform(int16_t roll, int16_t pitch, int16_t yaw, int16_t rollOffset,
							 ObjectRecord* objRecord);
void FVIEW_calcrotatemove(int16_t angleA, int16_t angleB, ObjectRecord* objRecord);
void FVIEW_calcrotateorient(int16_t angleA, int16_t angleQ16, ObjectRecord* objRecord);
int FVIEW_ComputeObjectViewMatrix(void);
void FVIEW_transformaxes(int axisX_Q15, int axisY_Q15, int axisZ_Q15, int16_t angleQ16);

extern int g_fviewForwardX_Q15;
extern int g_fviewForwardY_Q15;
extern int g_fviewForwardZ_Q15;
extern int g_fviewSideX_Q15;
extern int g_fviewSideY_Q15;
extern int g_fviewSideZ_Q15;
extern int g_fviewUpX_Q15;
extern int g_fviewUpY_Q15;
extern int g_fviewUpZ_Q15;

#ifdef __cplusplus
}
#endif

#endif
