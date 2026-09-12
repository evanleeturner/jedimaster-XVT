#include "xvt/util/game_rand.h"

// GLOBAL: XVT 0x555C80
uint16_t g_gameRand2ValueState = 0;
// GLOBAL: XVT 0x555C84
int16_t g_gameRandStateA = 0;
// GLOBAL: XVT 0x9D113C
int16_t g_gameRandStateB = 0;
// GLOBAL: XVT 0x9D77A8
uint16_t g_gameRand2FeedbackState = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x425CB0
int16_t GameRand(void) {
	int16_t result;
	int16_t iterations;
	int carryOut;
	int seedSign;
	uint16_t high;

	result = g_gameRandStateA;
	iterations = 16;
	do {
		high = (uint8_t)(g_gameRandStateB >> 8);
		carryOut = (((uint16_t)(high ^ (uint16_t)((uint8_t)g_gameRandStateB * 2))) & 0x80) != 0;
		seedSign = (high & 0x80) != 0;
		g_gameRandStateB = (int16_t)(2 * g_gameRandStateB + carryOut);
		result = (int16_t)(2 * result + seedSign);
	} while (--iterations != 0);

	g_gameRandStateA = result;
	return result;
}

// FUNCTION: XVT 0x425D10
uint16_t GameRand2(void) {
	uint16_t result;
	int16_t iterations;
	unsigned char carryOut;
	unsigned char seedSign;

	result = g_gameRand2ValueState;
	iterations = 16;
	do {
		uint16_t feedback;

		feedback = (uint8_t)(g_gameRand2FeedbackState >> 8) ^ ((uint8_t)g_gameRand2FeedbackState * 2);
		carryOut = (feedback & 0x80) != 0;
		seedSign = (g_gameRand2FeedbackState & 0x8000) != 0;
		g_gameRand2FeedbackState = (uint16_t)(2 * g_gameRand2FeedbackState + carryOut);
		result = (uint16_t)(2 * result + seedSign);
	} while (--iterations != 0);
	g_gameRand2ValueState = result;
	return result;
}

// FUNCTION: XVT 0x459B10
uint16_t GameRandRange(uint16_t modulus) {
	uint16_t value;

	if (modulus == 0) {
		return 0;
	}

	value = (uint16_t)GameRand();
	return (uint16_t)(value - (value / modulus) * modulus);
}
