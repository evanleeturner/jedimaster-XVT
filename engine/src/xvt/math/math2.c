#include "xvt/math/math2.h"

#include "xvt/flight/hud/hud.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"

/* The radar's blip limits at 320x240: per 443-unit step of angle from
 * vertical (entry 0) toward level (entry 36), the largest sideways and
 * vertical offsets in pixels. math2_getradarcoord copies it into
 * g_radar_ellipse_clamp_table in that mode; nothing writes it. */
// GLOBAL: XVT 0x51C4C0
struct radar_ellipse_clamp_limit g_radar_ellipse_clamp320x240_preset[37] = {
	{0, 18},  {1, 18},  {2, 18},  {3, 18},	{4, 17},  {5, 17},  {6, 17},
	{7, 17},  {8, 16},  {9, 16},  {10, 16}, {10, 15}, {11, 15}, {12, 15},
	{12, 14}, {13, 14}, {14, 14}, {14, 13}, {15, 13}, {15, 12}, {16, 12},
	{16, 11}, {17, 11}, {17, 10}, {18, 10}, {18, 9},  {18, 8},  {19, 8},
	{19, 7},  {19, 6},  {20, 6},  {20, 5},	{21, 4},  {21, 3},  {21, 2},
	{21, 1},  {21, 0},
};
/* The blip limits math2_getradarcoord clamps to, in the same 443-unit steps,
 * built for the resolution in g_radar_ellipse_clamp_cached_resolution_mode;
 * starts as the 320x240 values. Only math2_getradarcoord writes it. */
// GLOBAL: XVT 0x51C510
struct radar_ellipse_clamp_limit g_radar_ellipse_clamp_table[37] = {
	{0, 18},  {1, 18},  {2, 18},  {3, 18},	{4, 17},  {5, 17},  {6, 17},
	{7, 17},  {8, 16},  {9, 16},  {10, 16}, {10, 15}, {11, 15}, {12, 15},
	{12, 14}, {13, 14}, {14, 14}, {14, 13}, {15, 13}, {15, 12}, {16, 12},
	{16, 11}, {17, 11}, {17, 10}, {18, 10}, {18, 9},  {18, 8},  {19, 8},
	{19, 7},  {19, 6},  {20, 6},  {20, 5},	{21, 4},  {21, 3},  {21, 2},
	{21, 1},  {21, 0},
};
/* The g_flight_resolution_mode g_radar_ellipse_clamp_table was last built for;
 * starts at FLIGHT_RESOLUTION_320X240 to match the table's first values. Only
 * math2_getradarcoord writes it. */
// GLOBAL: XVT 0x51C55C
int g_radar_ellipse_clamp_cached_resolution_mode = FLIGHT_RESOLUTION_320X240;
/* The blip's vertical offset in pixels that math2_getradarcoord last
 * computed, negative when its up argument was. hud_add_blip_to_radar then adds
 * the radar's screen position and raises a negative result to 0. */
// GLOBAL: XVT 0xA08C78
int16_t radary = 0;
/* The blip's sideways offset in pixels that math2_getradarcoord last
 * computed, negative when its side argument was. hud_add_blip_to_radar then adds
 * the radar's screen position. */
// GLOBAL: XVT 0xA08C7C
int16_t radarx = 0;

/* Returns a * b / c from a 64-bit product of the magnitudes, truncated toward
 * zero, negated when one or three of a, b and c are negative. When the high 32
 * bits of the product are c's magnitude or more (a quotient of 2^32 or more, or
 * c of 0) it returns 0x7FFFFFFF, or its negation. Does not check for a quotient
 * from 0x80000000 to 0xFFFFFFFF, which comes back with its sign flipped. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x425AE0
int math2_ab_over_c32(int a, int b, int c)
{
	int negative = 0;
	if (a < 0) {
		negative = 1;
		a = (int)(0u - (uint32_t)a);
	}
	if (b < 0) {
		b = (int)(0u - (uint32_t)b);
		negative = !negative;
	}
	if (c < 0) {
		c = (int)(0u - (uint32_t)c);
		negative = !negative;
	}

	uint64_t product;
	if (negative != 0) {
		product = (uint64_t)(uint32_t)b * (uint32_t)a;
		if ((uint32_t)(product >> 32) >= (uint32_t)c) {
			a = INT32_MAX;
		} else {
			a = (int)(uint32_t)(product / (uint32_t)c);
		}
		a = (int)(0u - (uint32_t)a);
		return a;
	}

	product = (uint64_t)(uint32_t)b * (uint32_t)a;
	if ((uint32_t)(product >> 32) >= (uint32_t)c) {
		a = INT32_MAX;
	} else {
		a = (int)(uint32_t)(product / (uint32_t)c);
	}
	return a;
}

/* Scales value by frac_q16 over 65536: (value * frac_q16 + 0x8000) >> 16.
 * A frac_q16 of 0xFFFF counts as a whole and returns value. */
// FUNCTION: XVT 0x425B70
unsigned int math2_fraction(uint16_t value, uint16_t frac_q16)
{
	unsigned int result = value;
	if (frac_q16 != 0xffffu) {
		unsigned int product = (unsigned int)value * frac_q16;
		result = product + 0x8000u;
		result >>= 16;
	}

	return result;
}

/* Scales a 32-bit value by frac_q16 over 65536, in two 16-bit halves:
 * ((value & 0xFFFF) * frac_q16 >> 16) + (value >> 16) * frac_q16, truncated.
 * A frac_q16 of 0xFFFF counts as a whole and returns value. */
// FUNCTION: XVT 0x425BA0
unsigned int math2_longfraction(unsigned int value, uint16_t frac_q16)
{
	if (frac_q16 == 0xffffu) {
		return value;
	}
	return (((value & 0xffffu) * frac_q16) >> 16) +
	       ((value >> 16) * frac_q16);
}

/* Returns numerator over denominator as a fraction of 65536:
 * (numerator << 16) / denominator when numerator is the smaller; 0 when
 * denominator is 0 and numerator is not; else 0xFFFF. */
// FUNCTION: XVT 0x425C20
uint16_t math2_ratio_q16(uint16_t numerator, uint16_t denominator)
{
	if (numerator == denominator) {
		return 0xffffu;
	}
	if (denominator == 0) {
		return 0;
	}
	if (numerator < denominator) {
		return ((uint32_t)numerator << 16) / (uint32_t)denominator;
	}
	return 0xffffu;
}

/* Returns numerator over denominator as a fraction of 65536, 0xFFFF when
 * denominator is 0 or not over numerator. Otherwise halves both until both
 * are 0xFFFF or under, then returns (numerator << 16) / denominator. Halving
 * can make the two equal, and then it returns 0x10000 (0x20000 over 0x20001
 * does), which a caller that keeps 16 bits reads as 0. */
// FUNCTION: XVT 0x425C60
unsigned int math2_longratio_q16(unsigned int numerator,
				 unsigned int denominator)
{
	if (numerator == denominator) {
		return 0xFFFF;
	}
	if (denominator == 0) {
		return 0xFFFF;
	}
	if (denominator <= numerator) {
		return 0xFFFF;
	}

	while (numerator > 0xFFFF || denominator > 0xFFFF) {
		numerator >>= 1;
		denominator >>= 1;
	}

	return (numerator << 16) / denominator;
}

/* Converts a speed to a per-step distance: scaled = (4660 * speed + 128) >> 8,
 * divided by divisor (callers pass g_sim_steps_per_second), plus 1 when
 * (scaled & divisor) is over scaled >> 1. Does not check for a 0 divisor or
 * a negative speed. */
// FUNCTION: XVT 0x425D80
unsigned int math2_mphconvert(int16_t speed, uint16_t divisor)
{
	unsigned int scaled_value = 4660 * speed + 128;
	scaled_value >>= 8;
	unsigned int frames_per_second = divisor;
	unsigned int result = scaled_value / frames_per_second;
	if ((scaled_value & frames_per_second) > (scaled_value >> 1)) {
		result++;
	}
	return result;
}

/* Places one radar blip and returns radary. Scales side and up, made
 * positive, left by g_perspective_shift - 5 bits, divides each by forward
 * unless it is 0, and caps each at 0x7FFF. Then clamps them to the entry of
 * g_radar_ellipse_clamp_table for their direction (0x4000 minus
 * trig2_calcarctan_core's angle, over 443), puts back the signs of side and
 * up, and stores them in radarx and radary. First rebuilds the table when
 * g_flight_resolution_mode differs from
 * g_radar_ellipse_clamp_cached_resolution_mode: a copy of the 320x240 preset in
 * that mode, else 30 (480x360) or 44 (any other mode) through
 * trig2_sinewordmult for x and trig2_cosinewordmult for y at each step's
 * angle. trig2_calcarctan_core also sets trig2_legs_swapped and trig2_larger_leg.
 * Does not check that forward is positive. */
// FUNCTION: XVT 0x425E30
int16_t math2_getradarcoord(int side, int up, int forward)
{
	int table_index;

	if (g_flight_resolution_mode !=
	    g_radar_ellipse_clamp_cached_resolution_mode) {
		int angle;
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240) {
			table_index = 0;
			int remaining = 37;
			do {
				uint8_t y_limit =
					g_radar_ellipse_clamp320x240_preset
						[table_index]
							.y_limit;
				g_radar_ellipse_clamp_table[table_index]
					.x_limit =
					g_radar_ellipse_clamp320x240_preset
						[table_index]
							.x_limit;
				g_radar_ellipse_clamp_table[table_index]
					.y_limit = y_limit;
				++table_index;
				--remaining;
			} while (remaining != 0);
		} else if (g_flight_resolution_mode ==
			   FLIGHT_RESOLUTION_480X360) {
			table_index = 0;
			angle = 0;
			do {
				g_radar_ellipse_clamp_table[table_index]
					.x_limit = (uint8_t)trig2_sinewordmult(
					30, (int16_t)angle);
				g_radar_ellipse_clamp_table[table_index]
					.y_limit =
					(uint8_t)trig2_cosinewordmult(
						30, (int16_t)angle);
				++table_index;
				angle += 443;
			} while (angle < 0x4000);
		} else {
			table_index = 0;
			angle = 0;
			do {
				g_radar_ellipse_clamp_table[table_index]
					.x_limit = (uint8_t)trig2_sinewordmult(
					44, (int16_t)angle);
				g_radar_ellipse_clamp_table[table_index]
					.y_limit =
					(uint8_t)trig2_cosinewordmult(
						44, (int16_t)angle);
				++table_index;
				angle += 443;
			} while (angle < 0x4000);
		}
		g_radar_ellipse_clamp_cached_resolution_mode =
			g_flight_resolution_mode;
	}

	int projected_y = up;
	int projected_x = side;
	if (side < 0) {
		projected_x = (int)(0u - (uint32_t)side);
	}
	uint8_t shift;
	shift = (uint8_t)(g_perspective_shift - 5);
	projected_x = (int)((uint32_t)projected_x << (shift & 31u));
	if (forward != 0) {
		projected_x /= forward;
	}
	if (projected_x > INT16_MAX) {
		projected_x = INT16_MAX;
	}

	if (up < 0) {
		projected_y = (int)(0u - (uint32_t)up);
	}
	projected_y = (int)((uint32_t)projected_y << (shift & 31u));
	if (forward != 0) {
		projected_y /= forward;
	}
	if (projected_y > INT16_MAX) {
		projected_y = INT16_MAX;
	}

	radarx = (int16_t)projected_x;
	radary = (int16_t)projected_y;
	int16_t arctan_values[2];
	trig2_calcarctan_core(projected_x, projected_y, &arctan_values[1],
			      arctan_values);
	arctan_values[1] = (int16_t)-arctan_values[1];
	uint16_t angle_divisor = 443;
	arctan_values[1] += 0x4000;
	arctan_values[1] = (uint16_t)arctan_values[1] / angle_divisor;
	table_index = (uint16_t)arctan_values[1];

	arctan_values[0] = g_radar_ellipse_clamp_table[table_index].x_limit;
	if (radarx > (int)(uint16_t)arctan_values[0]) {
		radarx = arctan_values[0];
	}
	if (side < 0) {
		radarx = (int16_t)-radarx;
	}

	arctan_values[0] = g_radar_ellipse_clamp_table[table_index].y_limit;
	if (radary > (int)(uint16_t)arctan_values[0]) {
		radary = arctan_values[0];
	}
	if (up < 0) {
		radary = (int16_t)-radary;
	}
	return radary;
}
