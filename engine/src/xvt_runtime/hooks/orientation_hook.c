#include "xvt_runtime/hooks/orientation_hook.h"

#include "xvt_runtime/timing/flight_timing.h"

/* Player rotation ported from OpenXWA's gimbal-lock hook. */

#include <math.h>
#include <stdint.h>
#include <string.h>

#define XVT_ORIENTATION_PI 3.14159265358979323846f
#define XVT_ORIENTATION_HALF_PI (XVT_ORIENTATION_PI * 0.5f)
/* These two take 32767 units to a half turn, not the 32768 of the header's 65536-to-a-turn binary angle,
 * which the fixed-point path and the 0x8000 half-turn offsets below use. */
#define XVT_ORIENTATION_BAM_TO_RAD (XVT_ORIENTATION_PI / 32767.0f)
#define XVT_ORIENTATION_RAD_TO_BAM (32767.0f / XVT_ORIENTATION_PI)
#define XVT_ORIENTATION_GIMBAL_EPSILON 1.0e-5f

static float xvt_orientation_wrap_radians(float angle)
{
	return atan2f(sinf(angle), cosf(angle));
}

static int16_t xvt_orientation_round_angle(float angle)
{
	int value = (int)roundf(angle);

	if (value > 32767) {
		value = 32767;
	} else if (value < -32767) {
		value = -32767;
	}

	return (int16_t)value;
}

static void xvt_orientation_to_radians(struct xvt_orientation_angles angles,
				       float *pitch, float *yaw, float *roll)
{
	*yaw = xvt_orientation_wrap_radians(-(float)(int16_t)angles.yaw *
					    XVT_ORIENTATION_BAM_TO_RAD);
	*pitch = xvt_orientation_wrap_radians(
		-XVT_ORIENTATION_HALF_PI -
		(float)(int16_t)angles.pitch * XVT_ORIENTATION_BAM_TO_RAD);
	*roll = xvt_orientation_wrap_radians(-(float)(int16_t)angles.roll *
					     XVT_ORIENTATION_BAM_TO_RAD);
}

static struct xvt_orientation_angles
xvt_orientation_from_radians(float pitch, float yaw, float roll)
{
	struct xvt_orientation_angles result;

	int16_t yaw_binary_angle =
		xvt_orientation_round_angle(xvt_orientation_wrap_radians(-yaw) *
					    XVT_ORIENTATION_RAD_TO_BAM);
	int16_t pitch_binary_angle = xvt_orientation_round_angle(
		xvt_orientation_wrap_radians(-XVT_ORIENTATION_HALF_PI - pitch) *
		XVT_ORIENTATION_RAD_TO_BAM);
	int16_t roll_binary_angle = xvt_orientation_round_angle(
		xvt_orientation_wrap_radians(-roll) *
		XVT_ORIENTATION_RAD_TO_BAM);

	/* As in OpenXWA, select the equivalent Euler representation offset by
	 * half a turn in yaw and roll. */
	result.yaw = (uint16_t)((uint16_t)yaw_binary_angle + 0x8000u);
	result.pitch = (uint16_t)(uint16_t)(int16_t)-pitch_binary_angle;
	result.roll = (uint16_t)((uint16_t)roll_binary_angle + 0x8000u);
	return result;
}

/*
 * Column-major 3x3 matrix. Each column is a current body axis in world space.
 * This layout is the portable equivalent of the DirectXMath hook's m.r axes.
 */
static void xvt_orientation_rotate_local(float matrix[3][3], int axis_column,
					 float angle)
{
	if (angle == 0.0f) {
		return;
	}

	float axis_x = matrix[axis_column][0];
	float axis_y = matrix[axis_column][1];
	float axis_z = matrix[axis_column][2];
	float cosine = cosf(angle);
	float sine = sinf(angle);
	float one_minus_cosine = 1.0f - cosine;

	float rotation[3][3];
	rotation[0][0] = cosine + axis_x * axis_x * one_minus_cosine;
	rotation[0][1] = axis_x * axis_y * one_minus_cosine - axis_z * sine;
	rotation[0][2] = axis_x * axis_z * one_minus_cosine + axis_y * sine;
	rotation[1][0] = axis_y * axis_x * one_minus_cosine + axis_z * sine;
	rotation[1][1] = cosine + axis_y * axis_y * one_minus_cosine;
	rotation[1][2] = axis_y * axis_z * one_minus_cosine - axis_x * sine;
	rotation[2][0] = axis_z * axis_x * one_minus_cosine - axis_y * sine;
	rotation[2][1] = axis_z * axis_y * one_minus_cosine + axis_x * sine;
	rotation[2][2] = cosine + axis_z * axis_z * one_minus_cosine;

	float rotated[3][3];
	for (int column = 0; column < 3; ++column) {
		for (int row = 0; row < 3; ++row) {
			rotated[column][row] =
				rotation[row][0] * matrix[column][0] +
				rotation[row][1] * matrix[column][1] +
				rotation[row][2] * matrix[column][2];
		}
	}
	memcpy(matrix, rotated, sizeof(rotated));
}

static void xvt_orientation_matrix_to_quaternion(const float matrix[3][3],
						 float quaternion[4])
{
	float m00 = matrix[0][0];
	float m01 = matrix[1][0];
	float m02 = matrix[2][0];
	float m10 = matrix[0][1];
	float m11 = matrix[1][1];
	float m12 = matrix[2][1];
	float m20 = matrix[0][2];
	float m21 = matrix[1][2];
	float m22 = matrix[2][2];
	float trace = m00 + m11 + m22;
	float scale;

	if (trace > 0.0f) {
		scale = sqrtf(trace + 1.0f) * 2.0f;
		quaternion[3] = 0.25f * scale;
		quaternion[0] = (m21 - m12) / scale;
		quaternion[1] = (m02 - m20) / scale;
		quaternion[2] = (m10 - m01) / scale;
	} else if (m00 > m11 && m00 > m22) {
		scale = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
		quaternion[3] = (m21 - m12) / scale;
		quaternion[0] = 0.25f * scale;
		quaternion[1] = (m01 + m10) / scale;
		quaternion[2] = (m02 + m20) / scale;
	} else if (m11 > m22) {
		scale = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
		quaternion[3] = (m02 - m20) / scale;
		quaternion[0] = (m01 + m10) / scale;
		quaternion[1] = 0.25f * scale;
		quaternion[2] = (m12 + m21) / scale;
	} else {
		scale = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
		quaternion[3] = (m10 - m01) / scale;
		quaternion[0] = (m02 + m20) / scale;
		quaternion[1] = (m12 + m21) / scale;
		quaternion[2] = 0.25f * scale;
	}
}

static void xvt_orientation_quaternion_to_euler(const float quaternion[4],
						float *pitch, float *yaw,
						float *roll)
{
	float x = quaternion[0];
	float y = quaternion[1];
	float z = quaternion[2];
	float w = quaternion[3];
	float xx = x * x;
	float yy = y * y;
	float zz = z * z;
	/* These mRC are 1-based elements of the row-vector rotation matrix, as DirectXMath's _RC; the
	 * 0-based mRC of MatrixToQuaternion name other elements. */
	float m31 = 2.0f * x * z + 2.0f * y * w;
	float m32 = 2.0f * y * z - 2.0f * x * w;
	float m33 = 1.0f - 2.0f * xx - 2.0f * yy;
	float cos_pitch = sqrtf(m33 * m33 + m31 * m31);

	*pitch = atan2f(-m32, cos_pitch);
	if (cos_pitch > XVT_ORIENTATION_GIMBAL_EPSILON) {
		float m12 = 2.0f * x * y + 2.0f * z * w;
		float m22 = 1.0f - 2.0f * xx - 2.0f * zz;

		*yaw = atan2f(m31, m33);
		*roll = atan2f(m12, m22);
	} else {
		float m11 = 1.0f - 2.0f * yy - 2.0f * zz;
		float m21 = 2.0f * x * y - 2.0f * z * w;

		*yaw = 0.0f;
		*roll = atan2f(-m21, m11);
	}

	*pitch = xvt_orientation_wrap_radians(*pitch);
	*yaw = xvt_orientation_wrap_radians(*yaw);
	*roll = xvt_orientation_wrap_radians(*roll);
}

struct xvt_orientation_angles
xvt_orientation_apply_pitch_yaw(struct xvt_orientation_angles current,
				int pitch_delta_q16, int neg_yaw_delta_q16)
{
	if (xvt_flight_timing_is_network125()) {
		return xvt_orientation_apply_pitch_yaw_fixed(
			current, pitch_delta_q16, neg_yaw_delta_q16);
	}
	float pitch;
	float yaw;
	float roll;

	xvt_orientation_to_radians(current, &pitch, &yaw, &roll);
	float matrix[3][3] = {
		{1.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, 0.0f, 1.0f},
	};
	xvt_orientation_rotate_local(matrix, 1, yaw);
	xvt_orientation_rotate_local(matrix, 0, pitch);
	xvt_orientation_rotate_local(matrix, 2, roll);

	xvt_orientation_rotate_local(matrix, 1,
				     -(float)neg_yaw_delta_q16 *
					     XVT_ORIENTATION_BAM_TO_RAD);
	xvt_orientation_rotate_local(
		matrix, 0, (float)pitch_delta_q16 * XVT_ORIENTATION_BAM_TO_RAD);

	float quaternion[4];
	xvt_orientation_matrix_to_quaternion(matrix, quaternion);
	xvt_orientation_quaternion_to_euler(quaternion, &pitch, &yaw, &roll);
	return xvt_orientation_from_radians(pitch, yaw, roll);
}
