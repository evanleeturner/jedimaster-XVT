#include "xvt_runtime/hooks/orientation_hook.h"

/* Q30 body axes and Q32 binary angles keep simulation independent of libm,
 * floating-point contraction and the host rounding mode. One turn is 2^32. */
static const uint32_t g_cordic_angles[31] = {
	0x20000000u, 0x12e4051eu, 0x09fb385bu, 0x051111d4u, 0x028b0d43u,
	0x0145d7e1u, 0x00a2f61eu, 0x00517c55u, 0x0028be53u, 0x00145f2fu,
	0x000a2f98u, 0x000517ccu, 0x00028be6u, 0x000145f3u, 0x0000a2fau,
	0x0000517du, 0x000028beu, 0x0000145fu, 0x00000a30u, 0x00000518u,
	0x0000028cu, 0x00000146u, 0x000000a3u, 0x00000051u, 0x00000029u,
	0x00000014u, 0x0000000au, 0x00000005u, 0x00000003u, 0x00000001u,
	0x00000001u};

static void sin_cos(uint16_t angle, int64_t *sine, int64_t *cosine)
{
	const int64_t unit = INT64_C(1) << 30;
	/* Exact cardinal axes avoid introducing artificial motion at a pole. */
	if (!(angle & 0x3fff)) {
		*sine = angle == 0x4000 ? unit : angle == 0xc000 ? -unit : 0;
		*cosine = angle == 0 ? unit : angle == 0x8000 ? -unit : 0;
		return;
	}
	int64_t phase = (int64_t)angle * 65536;
	if (phase >= (INT64_C(1) << 31)) {
		phase -= INT64_C(1) << 32;
	}
	int sign = 1;
	/* unit doubles as a quarter turn here: phase is a Q32 angle, one turn being 2^32. */
	if (phase > unit) {
		phase -= INT64_C(1) << 31;
		sign = -1;
	} else if (phase < -unit) {
		phase += INT64_C(1) << 31;
		sign = -1;
	}
	int64_t x = 652032874;
	int64_t y = 0;
	for (unsigned i = 0; i < 31; ++i) {
		int direction = phase >= 0 ? 1 : -1;
		int64_t divisor = INT64_C(1) << i;
		int64_t next_x = x - direction * (y / divisor);
		y += direction * (x / divisor);
		x = next_x;
		phase -= direction * (int64_t)g_cordic_angles[i];
	}
	*sine = sign * y;
	*cosine = sign * x;
}

static int64_t fixed_atan2(int64_t y, int64_t x)
{
	if (!y) {
		return x < 0 ? INT64_C(1) << 31 : 0;
	}
	if (!x) {
		return y > 0 ? INT64_C(1) << 30 : -(INT64_C(1) << 30);
	}
	int64_t phase = 0;
	if (x < 0) {
		phase = y > 0 ? INT64_C(1) << 31 : -(INT64_C(1) << 31);
		x = -x;
		y = -y;
	}
	for (unsigned i = 0; i < 31 && y; ++i) {
		int direction = y > 0 ? 1 : -1;
		int64_t divisor = INT64_C(1) << i;
		int64_t next_x = x + direction * (y / divisor);
		y -= direction * (x / divisor);
		x = next_x;
		phase += direction * (int64_t)g_cordic_angles[i];
	}
	return phase;
}

static uint64_t integer_sqrt(uint64_t value)
{
	uint64_t bit = UINT64_C(1) << 62;
	while (bit > value) {
		bit >>= 2;
	}
	uint64_t root = 0;
	while (bit) {
		if (value >= root + bit) {
			value -= root + bit;
			root = (root >> 1) + bit;
		} else {
			root >>= 1;
		}
		bit >>= 2;
	}
	return root;
}

static int64_t round_q30(int64_t value)
{
	const int64_t half = INT64_C(1) << 29;
	const int64_t unit = INT64_C(1) << 30;
	return value >= 0 ? (value + half) / unit : -((-value + half) / unit);
}

static uint16_t round_angle(int64_t angle)
{
	return (uint16_t)(angle >= 0 ? (angle + 32768) / 65536
				     : -((-angle + 32768) / 65536));
}

static void rotate_local(int64_t matrix[3][3], unsigned axis, uint16_t angle)
{
	if (!angle) {
		return;
	}
	int64_t sine;
	int64_t cosine;
	sin_cos(angle, &sine, &cosine);
	unsigned a = (axis + 1) % 3;
	unsigned b = (axis + 2) % 3;
	for (unsigned row = 0; row < 3; ++row) {
		int64_t x = matrix[a][row];
		int64_t y = matrix[b][row];
		matrix[a][row] = round_q30(cosine * x + sine * y);
		matrix[b][row] = round_q30(cosine * y - sine * x);
	}
}

struct xvt_orientation_angles
xvt_orientation_apply_pitch_yaw_fixed(struct xvt_orientation_angles current,
				      int pitch_delta_q16,
				      int neg_yaw_delta_q16)
{
	if (!(uint16_t)pitch_delta_q16 && !(uint16_t)neg_yaw_delta_q16) {
		return current;
	}
	const int64_t unit = INT64_C(1) << 30;
	int64_t matrix[3][3] = {{unit, 0, 0}, {0, unit, 0}, {0, 0, unit}};
	rotate_local(matrix, 1, (uint16_t)(0u - current.yaw));
	rotate_local(matrix, 0, (uint16_t)(0xc000u - current.pitch));
	rotate_local(matrix, 2, (uint16_t)(0u - current.roll));
	rotate_local(matrix, 1, (uint16_t)(0u - (uint16_t)neg_yaw_delta_q16));
	rotate_local(matrix, 0, (uint16_t)pitch_delta_q16);
	/* Extract the same Y-X-Z Euler convention directly from the body axes.
	 * A quaternion round trip adds no rotation and is unnecessary here. */
	int64_t horizontal =
		(int64_t)integer_sqrt((uint64_t)(matrix[2][2] * matrix[2][2]) +
				      (uint64_t)(matrix[2][0] * matrix[2][0]));
	int64_t pitch = fixed_atan2(-matrix[2][1], horizontal);
	int64_t yaw;
	int64_t roll;
	if (horizontal > 10737) {
		yaw = fixed_atan2(matrix[2][0], matrix[2][2]);
		roll = fixed_atan2(matrix[0][1], matrix[1][1]);
	} else {
		yaw = 0;
		roll = fixed_atan2(-matrix[1][0], matrix[0][0]);
	}
	struct xvt_orientation_angles result = {
		(uint16_t)(0x8000u - round_angle(yaw)),
		(uint16_t)(0x4000u + round_angle(pitch)),
		(uint16_t)(0x8000u - round_angle(roll))};
	return result;
}
