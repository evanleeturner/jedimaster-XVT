#ifndef XVT_MATH_TRIG2_H
#define XVT_MATH_TRIG2_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const float g_q16AngleToRadiansScale;
extern const float g_trigQ15OutputScale;
extern uint16_t g_sinTable[513];
extern uint16_t g_squarerootable[257];
extern int trig2_xmovedist;
extern int trig2_xoffset;
extern int trig2_ymovedist;
extern uint16_t trig2_phi;
extern int trig2_polardistance;
extern int trig2_rho;
extern uint16_t trig2_xyangle;
extern int trig2_yoffset;
extern int trig2_zmovedist;
extern int trig2_zoffset;
extern uint16_t trig2_theta;
extern uint16_t pitchQ16;

int16_t trig2_getsignedsin(int16_t angleQ16);
int16_t trig2_calcsineofangle(int16_t angle);
int16_t trig2_w_arcsin(int16_t sinQ15);
int16_t trig2_w_arccos(int16_t cosQ15);
int16_t trig2_arccos(int16_t cosQ15);
int16_t trig2_arcsin(int16_t sinQ15);
unsigned int trig2_sinewordmult(int16_t arg1, int16_t arg2);
int trig2_sinedwordmult(int value, uint16_t angle);
int16_t trig2_getsignedcos(int16_t angleQ16);
unsigned int trig2_cosinewordmult(uint16_t value, int16_t angle);
int trig2_cosinedwordmult(int value, uint16_t angle);
void trig2_UpdateCartesianOffsets(void);
void trig2_movexyz(uint16_t distance, int16_t yaw, uint16_t pitch);
void trig2_ctop(int dx, int dy, int dz);
void trig2_ctop2dim(int dx, int dy);
int trig2_calcangleplanedistance(int magnitudeA, int magnitudeB);
int16_t trig2_calcarctan_core(int a, int b, int16_t* outRatio, int16_t* outAngle);
int16_t trig2_arctan(int y, int x);

#ifdef __cplusplus
}
#endif

#endif
