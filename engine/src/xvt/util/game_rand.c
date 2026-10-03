#include "xvt/util/game_rand.h"

/* The value GameRand2 last returned. Only GameRand2 reads or writes it, and
 * each call shifts all its old bits out unused, so it never changes a
 * result. */
// GLOBAL: XVT 0x555C80
uint16_t g_gameRand2ValueState = 0;
/* The value GameRand last returned. Only GameRand reads or writes it, and each
 * call shifts all its old bits out unused, so it never changes a result. */
// GLOBAL: XVT 0x555C84
int16_t g_gameRandValueState = 0;
/* State of the game's main random generator, a 16-bit shift register that
 * GameRand returns and steps. Flight start seeds it from
 * g_gameConfig.randomSeed when g_activeFlightPlayerCount is not 1, else from
 * timeGetTime XOR 0xBEEF (Flight_MainLoop in the original build,
 * XvtFlightLoading_Globals in the modern one). Many functions write it, chiefly
 * GameRand; Mission_Init, Mission_SpawnFlightGroupStaticObjects,
 * pai_UpdateAllCraftAI and collide_damagecraft swap another seed in for a while
 * and put it back. The saved world state carries it and the world checksum
 * includes it. */
// GLOBAL: XVT 0x9D113C
int16_t g_gameRandFeedbackState = 0;
/* State of the second random generator, stepped by GameRand2 the same way.
 * Flight start seeds it with timeGetTime plus g_gameRandFeedbackState
 * (Flight_MainLoop in the original build, XvtFlightLoading_Globals in the
 * modern one); after that only GameRand2 writes it. The saved world state and
 * the world checksum leave it out. */
// GLOBAL: XVT 0x9D77A8
uint16_t g_gameRand2FeedbackState = 0;

/* Returns g_gameRandFeedbackState as it was on entry and steps that 16-bit
 * shift register 16 times: each step shifts it left one place and feeds in the
 * XOR of its 0x8000 and 0x40 bits at the bottom. Also stores the result in
 * g_gameRandValueState. A state of 0 stays 0, and every call then returns 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x425CB0
int16_t GameRand(void)
{
	int16_t result;
	int16_t iterations;
	int carryOut;
	int seedSign;
	uint16_t high;

	result = g_gameRandValueState;
	iterations = 16;
	do {
		high = (uint8_t)(g_gameRandFeedbackState >> 8);
		carryOut =
			(((uint16_t)(high ^
				     (uint16_t)((uint8_t)
							g_gameRandFeedbackState *
						2))) &
			 0x80) != 0;
		seedSign = (high & 0x80) != 0;
		g_gameRandFeedbackState =
			(int16_t)(2 * g_gameRandFeedbackState + carryOut);
		result = (int16_t)(2 * result + seedSign);
	} while (--iterations != 0);

	g_gameRandValueState = result;
	return result;
}

/* The same generator as GameRand on its own state: returns
 * g_gameRand2FeedbackState as it was on entry, steps it 16 times and stores the
 * result in g_gameRand2ValueState. Its callers choose sounds and the flight
 * music, which leaves g_gameRandFeedbackState alone. */
// FUNCTION: XVT 0x425D10
uint16_t GameRand2(void)
{
	uint16_t result;
	int16_t iterations;
	unsigned char carryOut;
	unsigned char seedSign;

	result = g_gameRand2ValueState;
	iterations = 16;
	do {
		uint16_t feedback;

		feedback = (uint8_t)(g_gameRand2FeedbackState >> 8) ^
			   ((uint8_t)g_gameRand2FeedbackState * 2);
		carryOut = (feedback & 0x80) != 0;
		seedSign = (g_gameRand2FeedbackState & 0x8000) != 0;
		g_gameRand2FeedbackState =
			(uint16_t)(2 * g_gameRand2FeedbackState + carryOut);
		result = (uint16_t)(2 * result + seedSign);
	} while (--iterations != 0);
	g_gameRand2ValueState = result;
	return result;
}

/* Returns GameRand's value, taken as unsigned 16-bit, modulo modulus: 0 to
 * modulus - 1. Returns 0 without calling GameRand when modulus is 0. */
// FUNCTION: XVT 0x459B10
uint16_t GameRandRange(uint16_t modulus)
{
	uint16_t value;

	if (modulus == 0) {
		return 0;
	}

	value = (uint16_t)GameRand();
	return (uint16_t)(value - (value / modulus) * modulus);
}
