#include "xvt_runtime/snapshot/render_camera.h"

#include "xvt/flight/transfm2.h"
#include <math.h>
#include <string.h>

/* OpenXWA's render-only camera shadow. The source tag prevents a temporary
 * or independently written Q15 camera from borrowing another view's basis. */
typedef struct RenderCamera {
	double rows[9];
	int32_t source[9];
	int valid;
} RenderCamera;

static RenderCamera g_camera, g_savedViewport;

static void CopySource(int32_t rows[9]) {
	const int32_t source[9] = { g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z, g_camMatR1_X, g_camMatR1_Y,
								g_camMatR1_Z, g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z };
	memcpy(rows, source, sizeof source);
}

/* Mirror of FVIEW_calcrotatemove, using the same full-turn angle convention. */
static void RotMove(double m[9], int16_t pitch, int16_t yaw) {
	const double scale = 6.283185307179586 / 65536.0;
	double a = (double)(uint16_t)(0xc000 - pitch) * scale;
	double b = (double)(uint16_t)(int16_t)-yaw * scale;
	double cb = cos(b), sb = sin(b), ca = cos(a), sa = sin(a);
	m[0] = cb;
	m[1] = sb;
	m[2] = 0;
	m[3] = -sb * sa;
	m[4] = cb * sa;
	m[5] = -ca;
	m[6] = -sb * ca;
	m[7] = cb * ca;
	m[8] = sa;
}

/* OpenXWA's double-precision mirror of FVIEW_transformaxes. */
static void RotateAxes(double m[9], double ax, double ay, double az, int16_t angle) {
	if (!angle)
		return;
	const double scale = 6.283185307179586 / 65536.0;
	double c = cos((double)(uint16_t)angle * scale);
	double s = sin((double)(uint16_t)angle * scale);
	double omc = 1 - c;
	double r00 = c + omc * ax * ax;
	double r01 = az * s + omc * ay * ax;
	double r02 = -ay * s + omc * az * ax;
	double r10 = -az * s + omc * ay * ax;
	double r11 = c + omc * ay * ay;
	double r12 = ax * s + omc * az * ay;
	double r20 = ay * s + omc * az * ax;
	double r21 = -ax * s + omc * az * ay;
	double r22 = c + omc * az * az;
	for (int i = 0; i < 9; i += 3) {
		double x = m[i], y = m[i + 1], z = m[i + 2];
		m[i] = r20 * z + r10 * y + r00 * x;
		m[i + 1] = r21 * z + r11 * y + r01 * x;
		m[i + 2] = r22 * z + r12 * y + r02 * x;
	}
}

void XvtRenderCamera_Build(int16_t roll, int16_t pitch, int16_t yaw, int16_t angle_d, int16_t aim_x,
						   int16_t aim_y) {
	double* m = g_camera.rows;
	RotMove(m, pitch, yaw);
	RotateAxes(m, m[3], m[4], m[5], angle_d);
	RotateAxes(m, m[6], m[7], m[8], roll);
	for (int i = 3; i < 9; ++i)
		m[i] = -m[i];
	/* FVIEW saves the yaw axis before applying the HUD pitch offset. */
	double axis[3] = { m[3], m[4], m[5] };
	RotateAxes(m, m[0], m[1], m[2], aim_x);
	RotateAxes(m, axis[0], axis[1], axis[2], aim_y);
	CopySource(g_camera.source);
	g_camera.valid = 1;
}

void XvtRenderCamera_CopyRows(float rows[9]) {
	int32_t source[9];
	CopySource(source);
	int precise = g_camera.valid && !memcmp(source, g_camera.source, sizeof source);
	for (int i = 0; i < 9; ++i)
		rows[i] = precise ? (float)g_camera.rows[i] : source[i] * (1.0f / 32768.0f);
}

void XvtRenderCamera_SaveViewport(void) { g_savedViewport = g_camera; }

void XvtRenderCamera_RestoreViewport(void) {
	int32_t source[9];
	g_camera = g_savedViewport;
	CopySource(source);
	if (memcmp(source, g_camera.source, sizeof source))
		g_camera.valid = 0;
}
