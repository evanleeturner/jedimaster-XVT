#include "xvt/util/game_rand.h"

/* The value game_rand2 last returned. Only game_rand2 reads or writes it, and
 * each call shifts all its old bits out unused, so it never changes a
 * result. */
// GLOBAL: XVT 0x555C80
uint16_t g_game_rand2_value_state = 0;
/* The value game_rand last returned. Only game_rand reads or writes it, and each
 * call shifts all its old bits out unused, so it never changes a result. */
// GLOBAL: XVT 0x555C84
int16_t g_game_rand_value_state = 0;
/* State of the game's main random generator, a 16-bit shift register that
 * game_rand returns and steps. Flight start seeds it from
 * g_game_config.random_seed when g_active_flight_player_count is not 1, else from
 * timeGetTime XOR 0xBEEF (flight_main_loop in the original build,
 * xvt_flight_loading_globals in the modern one). Many functions write it, chiefly
 * game_rand; mission_init, mission_spawn_flight_group_static_objects,
 * pai_update_all_craft_ai and collide_damagecraft swap another seed in for a while
 * and put it back. The saved world state carries it and the world checksum
 * includes it. */
// GLOBAL: XVT 0x9D113C
int16_t g_game_rand_feedback_state = 0;
/* State of the second random generator, stepped by game_rand2 the same way.
 * Flight start seeds it with timeGetTime plus g_game_rand_feedback_state
 * (flight_main_loop in the original build, xvt_flight_loading_globals in the
 * modern one); after that only game_rand2 writes it. The saved world state and
 * the world checksum leave it out. */
// GLOBAL: XVT 0x9D77A8
uint16_t g_game_rand2_feedback_state = 0;

/* Returns g_game_rand_feedback_state as it was on entry and steps that 16-bit
 * shift register 16 times: each step shifts it left one place and feeds in the
 * XOR of its 0x8000 and 0x40 bits at the bottom. Also stores the result in
 * g_game_rand_value_state. A state of 0 stays 0, and every call then returns 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x425CB0
int16_t game_rand(void)
{
	int16_t result;
	int16_t iterations;
	int carry_out;
	int seed_sign;
	uint16_t high;

	result = g_game_rand_value_state;
	iterations = 16;
	do {
		high = (uint8_t)(g_game_rand_feedback_state >> 8);
		carry_out =
			(((uint16_t)(high ^
				     (uint16_t)((uint8_t)
							g_game_rand_feedback_state *
						2))) &
			 0x80) != 0;
		seed_sign = (high & 0x80) != 0;
		g_game_rand_feedback_state =
			(int16_t)(2 * g_game_rand_feedback_state + carry_out);
		result = (int16_t)(2 * result + seed_sign);
	} while (--iterations != 0);

	g_game_rand_value_state = result;
	return result;
}

/* The same generator as game_rand on its own state: returns
 * g_game_rand2_feedback_state as it was on entry, steps it 16 times and stores the
 * result in g_game_rand2_value_state. Its callers choose sounds and the flight
 * music, which leaves g_game_rand_feedback_state alone. */
// FUNCTION: XVT 0x425D10
uint16_t game_rand2(void)
{
	uint16_t result;
	int16_t iterations;
	unsigned char carry_out;
	unsigned char seed_sign;

	result = g_game_rand2_value_state;
	iterations = 16;
	do {
		uint16_t feedback;

		feedback = (uint8_t)(g_game_rand2_feedback_state >> 8) ^
			   ((uint8_t)g_game_rand2_feedback_state * 2);
		carry_out = (feedback & 0x80) != 0;
		seed_sign = (g_game_rand2_feedback_state & 0x8000) != 0;
		g_game_rand2_feedback_state =
			(uint16_t)(2 * g_game_rand2_feedback_state + carry_out);
		result = (uint16_t)(2 * result + seed_sign);
	} while (--iterations != 0);
	g_game_rand2_value_state = result;
	return result;
}

/* Returns game_rand's value, taken as unsigned 16-bit, modulo modulus: 0 to
 * modulus - 1. Returns 0 without calling game_rand when modulus is 0. */
// FUNCTION: XVT 0x459B10
uint16_t game_rand_range(uint16_t modulus)
{
	uint16_t value;

	if (modulus == 0) {
		return 0;
	}

	value = (uint16_t)game_rand();
	return (uint16_t)(value - (value / modulus) * modulus);
}
