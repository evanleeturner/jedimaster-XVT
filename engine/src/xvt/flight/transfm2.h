#ifndef XVT_FLIGHT_TRANSFM2_H
#define XVT_FLIGHT_TRANSFM2_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_camMatR0_X;
extern int g_camMatR0_Y;
extern int g_camMatR0_Z;
extern int g_camMatR1_X;
extern int g_camMatR1_Y;
extern int g_camMatR1_Z;
extern int g_camMatR2_X;
extern int g_camMatR2_Y;
extern int g_camMatR2_Z;
extern int viewX;
extern int viewY;
extern int depthZ;
extern int32_t g_projScaleHalfInt;
extern uint8_t perspShift;
extern uint16_t g_projAspectY;

int TRANSFM2_clipobjecteyez(int x, int y, int z);
int TRANSFM2_CamMatDotRow0(int x, int y, int z);
int TRANSFM2_CamMatDotRow1(int x, int y, int z);
int TRANSFM2_CamMatDotRow2(int x, int y, int z);
int TRANSFM2_ProjectScreenX(int viewX, int viewZ);
int TRANSFM2_ProjectScreenY(int viewY, int viewZ);
int TRANSFM2_ProjectScreenXFixedPoint(int viewX, unsigned int depth);
int TRANSFM2_ProjectScreenYFixedPoint(int viewY, unsigned int depth);

#ifdef __cplusplus
}
#endif

#endif
