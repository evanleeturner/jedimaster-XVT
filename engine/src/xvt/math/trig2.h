#ifndef XVT_MATH_TRIG2_H
#define XVT_MATH_TRIG2_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const float g_q16_angle_to_radians_scale;
extern const float g_trig_q15_output_scale;
extern uint16_t g_sin_table[513];
extern uint16_t g_hypot_excess_q16_table[257];
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
extern uint16_t trig2_pitch;

int16_t trig2_getsignedsin(int16_t angle_q16);
int16_t trig2_calcsinemagnitude(int16_t angle);
int16_t trig2_w_arcsin(int16_t sin_q15);
int16_t trig2_w_arccos(int16_t cos_q15);
int16_t trig2_arccos(int16_t cos_q15);
int16_t trig2_arcsin(int16_t sin_q15);
unsigned int trig2_sinewordmult(int16_t value, int16_t angle);
int trig2_sinedwordmult(int value, uint16_t angle);
int16_t trig2_getsignedcos(int16_t angle_q16);
unsigned int trig2_cosinewordmult(uint16_t value, int16_t angle);
int trig2_cosinedwordmult(int value, uint16_t angle);
void trig2_update_cartesian_offsets(void);
void trig2_movexyz(uint16_t distance, int16_t yaw, uint16_t pitch);
void trig2_ctop(int dx, int dy, int dz);
void trig2_ctop2dim(int dx, int dy);
int trig2_calcangleplanedistance(int magnitude_a, int magnitude_b);
int16_t trig2_calcarctan_core(int adjacent, int opposite, int16_t *out_angle,
			      int16_t *out_ratio_index);
int16_t trig2_arctan(int y, int x);

#ifdef __cplusplus
}
#endif

#endif
