#include "xvt/math/trig2.h"

#include <math.h>

/* Radians per angle unit: 2 pi over 65536, to float precision. Read by
 * trig2_getsignedsin and trig2_getsignedcos. */
// GLOBAL: XVT 0x51815C
const float g_q16_angle_to_radians_scale = 0.000095873722f;

/* 32767, the scale trig2_getsignedsin and trig2_getsignedcos multiply their
 * result by. */
// GLOBAL: XVT 0x518160
const float g_trig_q15_output_scale = 32767.0f;

/* The sine over a half circle in 512 steps of 64 angle units: entry i is
 * within 1 of sin(i * pi / 512) * 65535, rising to 65534 at entry 256 and back
 * to 0 at 512. Nothing writes it. */
// GLOBAL: XVT 0x524520
uint16_t g_sin_table[513] = {
	0,     402,   804,   1206,  1608,  2010,  2412,	 2814,	3216,  3617,
	4019,  4420,  4821,  5222,  5623,  6023,  6424,	 6824,	7224,  7623,
	8022,  8421,  8820,  9218,  9616,  10014, 10411, 10808, 11204, 11600,
	11996, 12391, 12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534,
	15924, 16314, 16703, 17091, 17479, 17867, 18253, 18639, 19024, 19409,
	19792, 20175, 20557, 20939, 21320, 21699, 22078, 22457, 22834, 23210,
	23586, 23961, 24335, 24708, 25080, 25451, 25821, 26190, 26558, 26925,
	27291, 27656, 28020, 28383, 28745, 29106, 29466, 29824, 30182, 30538,
	30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347, 33692, 34037,
	34380, 34721, 35062, 35401, 35738, 36075, 36410, 36744, 37076, 37407,
	37736, 38064, 38391, 38716, 39040, 39362, 39683, 40002, 40320, 40636,
	40951, 41264, 41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713,
	44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056, 46341, 46624,
	46906, 47186, 47464, 47741, 48015, 48288, 48559, 48828, 49095, 49361,
	49624, 49886, 50146, 50404, 50660, 50914, 51166, 51417, 51665, 51911,
	52156, 52398, 52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267,
	54491, 54714, 54934, 55152, 55368, 55582, 55794, 56004, 56212, 56418,
	56621, 56823, 57022, 57219, 57414, 57607, 57798, 57986, 58172, 58356,
	58538, 58718, 58896, 59071, 59244, 59415, 59583, 59750, 59914, 60075,
	60235, 60392, 60547, 60700, 60851, 60999, 61145, 61288, 61429, 61568,
	61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596, 62714, 62830,
	62943, 63054, 63162, 63268, 63372, 63473, 63572, 63668, 63763, 63854,
	63944, 64031, 64115, 64197, 64277, 64354, 64429, 64501, 64571, 64639,
	64704, 64766, 64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180,
	65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436, 65457, 65476,
	65492, 65505, 65516, 65525, 65531, 65533, 65534, 65533, 65531, 65525,
	65516, 65505, 65492, 65476, 65457, 65436, 65413, 65387, 65358, 65328,
	65294, 65259, 65220, 65180, 65137, 65091, 65043, 64993, 64940, 64884,
	64827, 64766, 64704, 64639, 64571, 64501, 64429, 64354, 64277, 64197,
	64115, 64031, 63944, 63854, 63763, 63668, 63572, 63473, 63372, 63268,
	63162, 63054, 62943, 62830, 62714, 62596, 62476, 62353, 62228, 62101,
	61971, 61839, 61705, 61568, 61429, 61288, 61145, 60999, 60851, 60700,
	60547, 60392, 60235, 60075, 59914, 59750, 59583, 59415, 59244, 59071,
	58896, 58718, 58538, 58356, 58172, 57986, 57798, 57607, 57414, 57219,
	57022, 56823, 56621, 56418, 56212, 56004, 55794, 55582, 55368, 55152,
	54934, 54714, 54491, 54267, 54040, 53812, 53581, 53349, 53114, 52878,
	52639, 52398, 52156, 51911, 51665, 51417, 51166, 50914, 50660, 50404,
	50146, 49886, 49624, 49361, 49095, 48828, 48559, 48288, 48015, 47741,
	47464, 47186, 46906, 46624, 46341, 46056, 45769, 45480, 45190, 44898,
	44604, 44308, 44011, 43713, 43412, 43110, 42806, 42501, 42194, 41886,
	41576, 41264, 40951, 40636, 40320, 40002, 39683, 39362, 39040, 38716,
	38391, 38064, 37736, 37407, 37076, 36744, 36410, 36075, 35738, 35401,
	35062, 34721, 34380, 34037, 33692, 33347, 33000, 32652, 32303, 31952,
	31600, 31248, 30893, 30538, 30182, 29824, 29466, 29106, 28745, 28383,
	28020, 27656, 27291, 26925, 26558, 26190, 25821, 25451, 25080, 24708,
	24335, 23961, 23586, 23210, 22834, 22457, 22078, 21699, 21320, 20939,
	20557, 20175, 19792, 19409, 19024, 18639, 18253, 17867, 17479, 17091,
	16703, 16314, 15924, 15534, 15143, 14751, 14359, 13966, 13573, 13180,
	12785, 12391, 11996, 11600, 11204, 10808, 10411, 10014, 9616,  9218,
	8820,  8421,  8022,  7623,  7224,  6824,  6424,	 6023,	5623,  5222,
	4821,  4420,  4019,  3617,  3216,  2814,  2412,	 2010,	1608,  1206,
	804,   402,   0,
};

/* Entry i is within 1 of atan(i / 256) in angle units, rising to 8192, an
 * eighth of a circle, at entry 256; entry 257 repeats 8192 so that
 * interpolating at 256 adds nothing, and the last two are 0. Nothing writes
 * it. */
// GLOBAL: XVT 0x524C28
static int16_t g_arctantable[260] = {
	0,    41,   81,	  122,	163,  204,  244,  285,	326,  367,  407,  448,
	489,  529,  570,  610,	651,  692,  732,  773,	813,  854,  894,  935,
	975,  1015, 1056, 1096, 1136, 1177, 1217, 1257, 1297, 1337, 1377, 1417,
	1457, 1497, 1537, 1577, 1617, 1656, 1696, 1736, 1775, 1815, 1854, 1894,
	1933, 1973, 2012, 2051, 2090, 2129, 2168, 2207, 2246, 2285, 2324, 2363,
	2401, 2440, 2478, 2517, 2555, 2593, 2632, 2670, 2708, 2746, 2784, 2822,
	2860, 2897, 2935, 2973, 3010, 3047, 3085, 3122, 3159, 3196, 3233, 3270,
	3307, 3344, 3380, 3417, 3453, 3490, 3526, 3562, 3598, 3634, 3670, 3706,
	3742, 3778, 3813, 3849, 3884, 3920, 3955, 3990, 4025, 4060, 4095, 4129,
	4164, 4199, 4233, 4267, 4302, 4336, 4370, 4404, 4438, 4471, 4505, 4539,
	4572, 4605, 4639, 4672, 4705, 4738, 4771, 4803, 4836, 4869, 4901, 4933,
	4966, 4998, 5030, 5062, 5093, 5125, 5157, 5188, 5220, 5251, 5282, 5313,
	5344, 5375, 5406, 5437, 5467, 5498, 5528, 5558, 5589, 5619, 5649, 5679,
	5708, 5738, 5768, 5797, 5826, 5856, 5885, 5914, 5943, 5972, 6000, 6029,
	6057, 6086, 6114, 6142, 6171, 6199, 6226, 6254, 6282, 6310, 6337, 6365,
	6392, 6419, 6446, 6473, 6500, 6527, 6554, 6580, 6607, 6633, 6660, 6686,
	6712, 6738, 6764, 6790, 6815, 6841, 6867, 6892, 6917, 6943, 6968, 6993,
	7018, 7043, 7067, 7092, 7117, 7141, 7166, 7190, 7214, 7238, 7262, 7286,
	7310, 7334, 7358, 7381, 7405, 7428, 7451, 7474, 7498, 7521, 7544, 7566,
	7589, 7612, 7634, 7657, 7679, 7702, 7724, 7746, 7768, 7790, 7812, 7834,
	7856, 7877, 7899, 7920, 7942, 7963, 7984, 8005, 8026, 8047, 8068, 8089,
	8110, 8130, 8151, 8172, 8192, 8192, 0,	  0,
};

/* Entry i is within 1 of (sqrt(1 + (i / 256)^2) - 1) * 65536: what the
 * hypotenuse adds to the longer leg when the shorter is i / 256 of it, as a
 * fraction of 65536. Nothing writes it. */
// GLOBAL: XVT 0x524E30
uint16_t g_hypot_excess_q16_table[257] = {
	0,     0,     2,     4,	    8,	   12,	  18,	 24,	32,    40,
	50,    60,    72,    84,    98,	   112,	  128,	 144,	162,   180,
	200,   220,   242,   264,   287,   312,	  337,	 363,	391,   419,
	448,   479,   510,   542,   575,   610,	  645,	 681,	718,   756,
	795,   835,   876,   918,   961,   1005,  1050,	 1095,	1142,  1190,
	1238,  1288,  1338,  1390,  1442,  1495,  1550,	 1605,	1661,  1718,
	1776,  1835,  1895,  1955,  2017,  2079,  2143,	 2207,	2273,  2339,
	2406,  2474,  2543,  2612,  2683,  2755,  2827,	 2900,	2974,  3049,
	3125,  3202,  3280,  3358,  3438,  3518,  3599,	 3681,	3764,  3847,
	3932,  4017,  4103,  4190,  4278,  4367,  4456,	 4547,	4638,  4730,
	4822,  4916,  5010,  5105,  5201,  5298,  5396,	 5494,	5593,  5693,
	5794,  5895,  5997,  6100,  6204,  6309,  6414,	 6520,	6627,  6734,
	6843,  6952,  7061,  7172,  7283,  7395,  7508,	 7621,	7735,  7850,
	7966,  8082,  8199,  8317,  8435,  8554,  8674,	 8794,	8915,  9037,
	9160,  9283,  9407,  9531,  9656,  9782,  9909,	 10036, 10164, 10292,
	10421, 10551, 10681, 10812, 10944, 11076, 11209, 11343, 11477, 11612,
	11747, 11883, 12019, 12157, 12294, 12433, 12572, 12711, 12852, 12992,
	13134, 13276, 13418, 13561, 13705, 13849, 13994, 14139, 14285, 14431,
	14578, 14726, 14874, 15022, 15171, 15321, 15471, 15622, 15773, 15925,
	16077, 16230, 16384, 16537, 16692, 16847, 17002, 17158, 17314, 17471,
	17629, 17786, 17945, 18104, 18263, 18423, 18583, 18744, 18905, 19066,
	19229, 19391, 19554, 19718, 19882, 20046, 20211, 20376, 20542, 20708,
	20875, 21042, 21209, 21377, 21546, 21714, 21884, 22053, 22223, 22394,
	22565, 22736, 22908, 23080, 23252, 23425, 23599, 23772, 23946, 24121,
	24296, 24471, 24647, 24823, 24999, 25176, 25353, 25531, 25709, 25887,
	26066, 26245, 26424, 26604, 26784, 26964, 27146,
};

/* World units an object moves along x this step;
 * object_add_trig_move_delta_and_clamp_world_position adds it to the object's
 * position. Many functions write it, chiefly trig2_movexyz and
 * object_update_lifetime_and_movement, and xvt_flight_integration_move. */
// GLOBAL: XVT 0x9A20B0
int trig2_xmovedist = 0;

/* x of the last conversion in this file: trig2_ctop stores the magnitude of
 * dx; trig2_update_cartesian_offsets stores its x. Only this file reads it. */
// GLOBAL: XVT 0x9A8E14
int trig2_xoffset = 0;

/* The hypotenuse trig2_calcangleplanedistance last computed: after trig2_ctop,
 * the 3D distance. Read widely after trig2_ctop. Many functions write it,
 * chiefly trig2_calcangleplanedistance; callers such as flight_update_timers,
 * laser_fireturretslot and hud_draw_cmd_target_details scale it in place. */
// GLOBAL: XVT 0x9A8C00
int trig2_polardistance = 0;

/* Heading of the last trig2_ctop: 0x4000 minus the angle from +x toward +y, so
 * 0 along +y and 0x4000 along +x. That function writes it, and
 * mission_spawn_flight_group_wave_craft sets or turns it before calling
 * trig2_movexyz. */
// GLOBAL: XVT 0x9A8070
uint16_t trig2_xyangle = 0;

/* World units an object moves along y this step; written and read as
 * trig2_xmovedist is. */
// GLOBAL: XVT 0x9CD26C
int trig2_ymovedist = 0;

/* Angle from +z that trig2_update_cartesian_offsets converts. Only
 * trig2_movexyz writes it, from its pitch. */
// GLOBAL: XVT 0x9D12B0
uint16_t trig2_phi = 0;

/* Distance trig2_update_cartesian_offsets converts. Only trig2_movexyz writes
 * it, from its distance. */
// GLOBAL: XVT 0x9D6824
int trig2_rho = 0;

/* y of the last conversion in this file: trig2_ctop stores the magnitude of
 * dy; trig2_update_cartesian_offsets stores its y. Only this file reads it. */
// GLOBAL: XVT 0x9E9648
int trig2_yoffset = 0;

/* 1 when the last trig2_calcarctan_core swapped its legs because opposite
 * was over adjacent, compared as signed numbers; else 0. Only that function
 * writes and reads it. */
// GLOBAL: XVT 0x9EC460
static int16_t trig2_legs_swapped = 0;

/* 1 when the x given to the last trig2_ctop or trig2_arctan was negative, else
 * 0; 0xFFFF until the first. Only those write and read it. */
// GLOBAL: XVT 0x9ECC46
static uint16_t trig2_signx = UINT16_MAX;

/* 1 when the y given to the last trig2_ctop or trig2_arctan was negative, else
 * 0; 0xFFFF until the first. Only those write and read it. */
// GLOBAL: XVT 0x9ECC48
static uint16_t trig2_signy = UINT16_MAX;

/* 1 when the dz given to the last trig2_ctop was negative, else 0. Only
 * trig2_ctop writes and reads it. */
// GLOBAL: XVT 0x9ECC50
static uint16_t trig2_signz = 0;

/* World units an object moves along z this step; written and read as
 * trig2_xmovedist is. */
// GLOBAL: XVT 0x9FD438
int trig2_zmovedist = 0;

/* z of the last conversion in this file: trig2_ctop stores the magnitude of
 * dz; trig2_update_cartesian_offsets stores its z, after using it to hold the
 * distance in the x-y plane. Only this file reads it. */
// GLOBAL: XVT 0xA00520
int trig2_zoffset = 0;

/* The larger of the two legs given to the last trig2_calcarctan_core,
 * compared as signed numbers; adjacent when they are equal. Only that
 * function writes it; trig2_calcangleplanedistance scales it into the
 * hypotenuse. */
// GLOBAL: XVT 0xA00740
static int trig2_larger_leg = 0;

/* Angle from +x toward +y that trig2_update_cartesian_offsets converts. Only
 * trig2_movexyz writes it, as 0x4000 minus its yaw. */
// GLOBAL: XVT 0xA07C5C
uint16_t trig2_theta = 0;

/* Pitch of the last trig2_ctop: the angle from +z, 0 straight along +z,
 * 0x4000 level and 0x8000 along -z. Written by trig2_ctop and by
 * mission_spawn_flight_group_wave_craft. */
// GLOBAL: XVT 0xA080F0
uint16_t trig2_pitch = 0;

/* The angle, 0 to 0x4000, that trig2_calcangleplanedistance last took from
 * trig2_calcarctan_core; only that function writes it. */
// GLOBAL: XVT 0xA080FE
static int16_t trig2_angleplane = 0;

/* Returns the sine of an angle of 65,536 units to the circle, times 32767,
 * truncated toward zero; computed in floating point. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x46A3C0
int16_t trig2_getsignedsin(int16_t angle_q16)
{
	return (int16_t)(sin(angle_q16 * g_q16_angle_to_radians_scale) *
			 g_trig_q15_output_scale);
}

/* Returns the sine's magnitude: g_sin_table interpolated between its entries,
 * 0 to 65534 where 65536 would be 1, as a 16-bit pattern to read unsigned.
 * An angle from 0x8000 up gives the same as one 0x8000 less. */
// FUNCTION: XVT 0x46A3F0
int16_t trig2_calcsinemagnitude(int16_t angle)
{
	uint16_t table_index = ((uint16_t)angle >> 6) & 0x1FFu;
	uint16_t base = g_sin_table[table_index];
	uint16_t delta_magnitude =
		(uint16_t)(g_sin_table[table_index + 1] - base);
	int16_t signed_delta = (int16_t)delta_magnitude;
	if (signed_delta < 0) {
		delta_magnitude = (uint16_t)-delta_magnitude;
	}

	uint32_t interpolation = ((uint32_t)(uint16_t)((uint16_t)angle << 10) *
				  delta_magnitude) >>
				 16;
	if (signed_delta < 0) {
		interpolation = 0u - interpolation;
	}

	return (int16_t)(base + interpolation);
}

/* Returns trig2_arccos(cos_q15). */
// FUNCTION: XVT 0x46A460
int16_t trig2_w_arccos(int16_t cos_q15) { return trig2_arccos(cos_q15); }

/* Returns the arccosine of cos_q15 (32768 standing for 1) in angle units, 0 to
 * 0x8000. Searches the falling half of g_sin_table from entry 256 for the
 * first entry at or under twice the magnitude and interpolates; when twice
 * the magnitude is under every entry searched (a magnitude under 201), it
 * divides by entry 511 instead. A
 * negative input returns 0x8000 minus the result for its magnitude. Only
 * trig2_w_arccos calls this. */
// FUNCTION: XVT 0x46A470
int16_t trig2_arccos(int16_t cos_q15)
{
	int16_t table_index = 256;
	uint16_t target = (uint16_t)cos_q15;
	if (cos_q15 < 0) {
		target = (uint16_t)-target;
	}
	target = (uint16_t)(target + target);
	int16_t remaining_steps = 256;
	uint16_t interpolation;
	uint16_t angle;
	do {
		if (g_sin_table[table_index] <= target) {
			--remaining_steps;
			uint16_t base = g_sin_table[table_index - 1];
			uint16_t delta = (uint16_t)(target - base);
			if (delta != 0) {
				uint16_t span =
					(uint16_t)(g_sin_table[table_index -
							       2] -
						   base);
				/* Keep the shifted 32-bit pattern signed for division. */
				interpolation =
					(uint16_t)((uint16_t)((int32_t)((uint32_t)
										delta
									<< 16) /
							      (int32_t)span) >>
						   8);
			} else {
				interpolation = 0;
			}
			angle = (int16_t)(-1 - remaining_steps);
			angle = (int16_t)((uint16_t)angle << 8);
			angle = (int16_t)(angle - interpolation);
			angle = (int16_t)((uint16_t)angle >> 2);
			if (cos_q15 < 0) {
				angle = (uint16_t)-angle;
				angle += 0x8000u;
			}
			return angle;
		}
		--remaining_steps;
		++table_index;
	} while (remaining_steps > 0);

	uint16_t divisor = g_sin_table[table_index - 1];
	interpolation = (uint16_t)((int32_t)((uint32_t)target << 16) /
				   (int32_t)divisor);
	interpolation >>= 8;
	interpolation = (uint16_t)-interpolation;
	if (interpolation == 0) {
		angle = 0x4000;
	} else {
		angle = (int16_t)(interpolation >> 2);
	}
	if (cos_q15 < 0) {
		angle = (uint16_t)-angle;
		angle += 0x8000u;
	}

	return angle;
}

/* Returns (magnitude of value * sine + 0x8000) >> 16, sine being the
 * g_sin_table entry for angle without interpolation. When the sign of value
 * and the 0x8000 bit of angle differ, the sum is negated before the shift: the
 * low 16 bits then hold a negative value, rounded down, and the high 16 bits
 * are 0, so 1000 at 0xC000 gives 0xFC17, -1001 read as 16 bits. */
// FUNCTION: XVT 0x46A5F0
unsigned int trig2_sinewordmult(int16_t value, int16_t angle)
{
	/* sine first holds value's sign bit (0x8000 or 0); it takes the table sine below. */
	uint16_t sine = (uint16_t)value & 0x8000u;
	if (sine != 0) {
		value = (int16_t)-value;
	}
	uint16_t sign_difference = sine ^ ((uint16_t)angle & 0x8000u);
	sine = g_sin_table[((uint16_t)angle >> 6) & 0x1FFu];
	uint32_t product = (uint32_t)sine * (uint16_t)value + 0x8000u;
	if (sign_difference != 0) {
		product = 0u - product;
	}

	return product >> 16;
}

/* Returns value times the g_sin_table entry for angle (no interpolation) over
 * 65536, computed on the magnitude as (high 16 bits) * entry plus
 * ((low 16 bits) * entry + 0x8000) >> 16, and negated when the sign of value
 * and the 0x8000 bit of angle differ. Does not check that the sum fits in 32
 * bits. */
// FUNCTION: XVT 0x46A650
int trig2_sinedwordmult(int value, uint16_t angle)
{
	int16_t sign = 0;
	if (value < 0) {
		sign = 0x8000u;
		value = (int)(0u - (uint32_t)value);
	}
	sign ^= (uint16_t)angle;
	uint16_t table_value =
		g_sin_table[(((uint16_t)angle >> 5) & 0x3FEu) >> 1];
	uint32_t low_product =
		((table_value * (uint32_t)(uint16_t)value) + 0x8000u) >> 16;
	value = (int)((uint32_t)value >> 16);
	value = (int)((uint32_t)value * table_value + low_product);
	if ((sign & 0x8000u) != 0) {
		return -value;
	}
	return value;
}

/* Returns the cosine of an angle of 65,536 units to the circle, times 32767,
 * truncated toward zero; computed in floating point. */
// FUNCTION: XVT 0x46A6D0
int16_t trig2_getsignedcos(int16_t angle_q16)
{
	return (int16_t)(cos(angle_q16 * g_q16_angle_to_radians_scale) *
			 g_trig_q15_output_scale);
}

/* trig2_sinewordmult for the angle plus 0x4000: value times the cosine, in
 * the same form. */
// FUNCTION: XVT 0x46A700
unsigned int trig2_cosinewordmult(uint16_t value, int16_t angle)
{
	/* cosine first holds value's sign bit (0x8000 or 0); it takes the table cosine below. */
	uint16_t cosine = value & 0x8000u;
	if (cosine != 0) {
		value = (uint16_t)(0u - value);
	}
	angle = (int16_t)(angle + 0x4000);
	uint16_t sign_difference = cosine ^ ((uint16_t)angle & 0x8000u);
	cosine = g_sin_table[((uint16_t)angle >> 6) & 0x1FFu];
	uint32_t product = (uint32_t)cosine * value + 0x8000u;
	if (sign_difference != 0) {
		product = 0u - product;
	}

	return product >> 16;
}

/* trig2_sinedwordmult for the angle plus 0x4000: value times the cosine, in
 * the same form. */
// FUNCTION: XVT 0x46A760
int trig2_cosinedwordmult(int value, uint16_t angle)
{
	uint16_t sign = 0;
	uint32_t magnitude = (uint32_t)value;
	if (value < 0) {
		sign = (int16_t)0x8000u;
		magnitude = 0u - magnitude;
	}
	uint16_t shifted_angle = (uint16_t)angle;
	shifted_angle += 0x4000u;
	sign ^= (int16_t)shifted_angle;
	shifted_angle >>= 5;
	sign &= (int16_t)0x8000u;
	shifted_angle &= 0x3FEu;
	shifted_angle >>= 1;
	uint16_t table_value = g_sin_table[shifted_angle];
	uint32_t low_product =
		((table_value * (uint32_t)(uint16_t)magnitude) + 0x8000u) >> 16;
	magnitude >>= 16;
	magnitude *= table_value;
	int result = (int)(magnitude + low_product);
	if (sign != 0) {
		return -result;
	}
	return result;
}

/* Converts trig2_rho, trig2_phi and trig2_theta to x, y and z: sets
 * trig2_zoffset to trig2_rho * cos(trig2_phi), and trig2_xoffset and
 * trig2_yoffset to trig2_rho * sin(trig2_phi) times the cosine and sine of
 * trig2_theta, through trig2_sinedwordmult and trig2_cosinedwordmult. Only
 * trig2_movexyz calls this. */
// FUNCTION: XVT 0x46A7C0
void trig2_update_cartesian_offsets(void)
{
	trig2_zoffset = trig2_sinedwordmult(trig2_rho, trig2_phi);
	trig2_xoffset = trig2_cosinedwordmult(trig2_zoffset, trig2_theta);
	trig2_yoffset = trig2_sinedwordmult(trig2_zoffset, trig2_theta);
	trig2_zoffset = trig2_cosinedwordmult(trig2_rho, trig2_phi);
}

/* Sets trig2_xmovedist, trig2_ymovedist and trig2_zmovedist to the offset of
 * distance along yaw and pitch, the inverse of trig2_ctop: trig2_theta is
 * 0x4000 minus yaw, trig2_phi is pitch (0 along +z, 0x4000 level). Also
 * sets trig2_rho and the three offsets of trig2_update_cartesian_offsets. */
// FUNCTION: XVT 0x46A870
void trig2_movexyz(uint16_t distance, int16_t yaw, uint16_t pitch)
{
	trig2_theta = (uint16_t)(0x4000 - yaw);
	trig2_rho = distance;
	trig2_phi = pitch;
	trig2_update_cartesian_offsets();
	trig2_xmovedist = trig2_xoffset;
	trig2_ymovedist = trig2_yoffset;
	trig2_zmovedist = trig2_zoffset;
}

/* Converts an offset to heading, pitch and distance: trig2_xyangle (0 along
 * +y, 0x4000 along +x), trig2_pitch (0 along +z, 0x4000 level, 0x8000 along
 * -z) and trig2_polardistance. Also stores the magnitudes in trig2_xoffset,
 * trig2_yoffset and trig2_zoffset and the signs in trig2_signx, trig2_signy
 * and trig2_signz, and leaves trig2_angleplane, trig2_larger_leg and
 * trig2_legs_swapped from its second trig2_calcangleplanedistance. */
// FUNCTION: XVT 0x46A8D0
void trig2_ctop(int dx, int dy, int dz)
{
	int magnitude_x = dx;
	if (magnitude_x < 0) {
		magnitude_x = -magnitude_x;
		trig2_signx = 1;
	} else {
		trig2_signx = 0;
	}
	int magnitude_y = dy;
	trig2_xoffset = magnitude_x;
	if (magnitude_y < 0) {
		magnitude_y = -magnitude_y;
		trig2_signy = 1;
	} else {
		trig2_signy = 0;
	}
	int magnitude_z = dz;
	trig2_yoffset = magnitude_y;
	if (magnitude_z < 0) {
		magnitude_z = -magnitude_z;
		trig2_signz = 1;
	} else {
		trig2_signz = 0;
	}
	trig2_zoffset = magnitude_z;

	trig2_calcangleplanedistance(magnitude_x, magnitude_y);
	int16_t angle = trig2_angleplane;
	trig2_xyangle = angle;
	if (trig2_signy != 0) {
		angle = (int16_t)-trig2_angleplane;
	}
	if (trig2_signx != 0) {
		angle = (int16_t)(0x8000u - (uint16_t)angle);
	}
	trig2_xyangle = angle;
	trig2_xyangle = (int16_t)(0x4000 - angle);

	trig2_calcangleplanedistance(trig2_polardistance, trig2_zoffset);
	angle = trig2_angleplane;
	trig2_pitch = angle;
	if (trig2_signz != 0) {
		angle = (int16_t)-trig2_angleplane;
	}
	trig2_pitch = angle;
	trig2_pitch = (int16_t)(0x4000 - angle);
}

/* Returns the hypotenuse of two non-negative legs and stores it in
 * trig2_polardistance: the larger leg plus the larger leg times the
 * g_hypot_excess_q16_table entry for their ratio in 256ths (the fraction left
 * out), over 65536, with the low 16 bits rounded. Stores the angle of
 * magnitude_b over magnitude_a, 0 to 0x4000, in trig2_angleplane; through
 * trig2_calcarctan_core also sets trig2_larger_leg and trig2_legs_swapped. */
// FUNCTION: XVT 0x46AA70
int trig2_calcangleplanedistance(int magnitude_a, int magnitude_b)
{
	int16_t angle;
	int16_t ratio_index;

	trig2_calcarctan_core(magnitude_a, magnitude_b, &angle, &ratio_index);
	trig2_angleplane = angle;
	uint32_t scale = g_hypot_excess_q16_table[(uint16_t)ratio_index];
	uint32_t divisor = (uint32_t)trig2_larger_leg;
	uint32_t high_product = (divisor >> 16) * scale;
	uint32_t low_product = (divisor & 0xFFFFu) * scale;
	low_product += 0x8000u;
	low_product >>= 16;
	uint16_t rounded_low_product = (uint16_t)low_product;
	trig2_polardistance =
		(int)(divisor + high_product + rounded_low_product);
	return trig2_polardistance;
}

/* Returns the angle whose tangent is opposite over adjacent, 0 to 0x4000, for
 * non-negative legs, and stores it in *out_angle. Divides the smaller leg by
 * the larger to 16 bits (after shifting both up 8 bits once or twice while
 * the larger is under 0x1000000) and interpolates g_arctantable between
 * entries at the ratio's high byte, which it stores in *out_ratio_index (256
 * for equal legs); when opposite is the larger it returns 0x4000 minus that
 * angle. Sets trig2_legs_swapped and trig2_larger_leg. Two zero legs give
 * 0x2000. */
// FUNCTION: XVT 0x46AAF0
int16_t trig2_calcarctan_core(int adjacent, int opposite, int16_t *out_angle,
			      int16_t *out_ratio_index)
{
	uint32_t numerator = (uint32_t)opposite;
	uint32_t divisor = (uint32_t)adjacent;
	uint32_t fraction = 0;
	trig2_legs_swapped = 0;
	if (numerator == divisor) {
		numerator = 256;
		trig2_larger_leg = (int)divisor;
	} else {
		if ((int32_t)numerator >= (int32_t)divisor) {
			trig2_legs_swapped = 1;
			uint32_t swap = numerator;
			numerator = divisor;
			divisor = swap;
		}
		trig2_larger_leg = (int)divisor;
		if (divisor != 0) {
			if ((divisor & 0xFF000000u) == 0) {
				divisor <<= 8;
				numerator <<= 8;
				if ((divisor & 0xFF000000u) == 0) {
					divisor <<= 8;
					numerator <<= 8;
				}
			}
			if (numerator == divisor) {
				fraction = numerator >> 16;
				numerator = 256;
			} else {
				divisor >>= 16;
				numerator = (uint16_t)(numerator / divisor);
				fraction = (numerator & 0xFFu) << 8;
				numerator = (uint32_t)((int32_t)numerator >> 8);
			}
		} else {
			fraction = numerator >> 16;
			numerator = 0;
		}
	}

	*out_ratio_index = (int16_t)numerator;
	*out_angle = g_arctantable[(uint16_t)numerator + 1];
	uint16_t table_delta =
		(uint16_t)(*out_angle -
			   g_arctantable[(uint16_t)*out_ratio_index]);
	*out_angle = (int16_t)table_delta;
	uint32_t interpolation = ((fraction & 0xFF00u) * table_delta) >> 16;
	*out_angle = (int16_t)interpolation;
	int16_t result = (int16_t)(interpolation +
				   g_arctantable[(uint16_t)*out_ratio_index]);
	*out_angle = result;
	if (trig2_legs_swapped != 0) {
		result = (int16_t)-result;
		*out_angle = result;
		result = (int16_t)(result + 0x4000);
		*out_angle = result;
	}
	return result;
}

/* Returns the angle from +x toward +y of the point (x, y), as int16 in angle
 * units (atan2): the first-quadrant angle from trig2_calcarctan_core,
 * negated when y is negative and taken from 0x8000 when x is. Sets
 * trig2_signx, trig2_signy, trig2_legs_swapped and trig2_larger_leg. (0, 0) gives
 * 0x2000. */
// FUNCTION: XVT 0x46ABF0
int16_t trig2_arctan(int y, int x)
{
	int magnitude_y = y;
	if (magnitude_y < 0) {
		magnitude_y = -magnitude_y;
		trig2_signy = 1;
	} else {
		trig2_signy = 0;
	}
	int magnitude_x = x;
	if (magnitude_x < 0) {
		magnitude_x = -magnitude_x;
		trig2_signx = 1;
	} else {
		trig2_signx = 0;
	}
	int16_t ratio_index;
	int16_t angle;
	trig2_calcarctan_core(magnitude_x, magnitude_y, &angle, &ratio_index);
	if (trig2_signy != 0) {
		angle = (int16_t)-angle;
	}
	if (trig2_signx != 0) {
		angle = (int16_t)(0x8000u - (uint16_t)angle);
	}
	return angle;
}
