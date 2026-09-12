#include "xvt/math/math2.h"

#include "xvt/flight/hud/hud.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"

// GLOBAL: XVT 0x51C4C0
RadarEllipseClampLimit g_radarEllipseClampMode19Preset[37] = {
	{ 0, 18 },  { 1, 18 },  { 2, 18 },  { 3, 18 },  { 4, 17 },  { 5, 17 },  { 6, 17 },  { 7, 17 },
	{ 8, 16 },  { 9, 16 },  { 10, 16 }, { 10, 15 }, { 11, 15 }, { 12, 15 }, { 12, 14 }, { 13, 14 },
	{ 14, 14 }, { 14, 13 }, { 15, 13 }, { 15, 12 }, { 16, 12 }, { 16, 11 }, { 17, 11 }, { 17, 10 },
	{ 18, 10 }, { 18, 9 },  { 18, 8 },  { 19, 8 },  { 19, 7 },  { 19, 6 },  { 20, 6 },  { 20, 5 },
	{ 21, 4 },  { 21, 3 },  { 21, 2 },  { 21, 1 },  { 21, 0 },
};
// GLOBAL: XVT 0x51C510
RadarEllipseClampLimit g_radarEllipseClampTable[37] = {
	{ 0, 18 },  { 1, 18 },  { 2, 18 },  { 3, 18 },  { 4, 17 },  { 5, 17 },  { 6, 17 },  { 7, 17 },
	{ 8, 16 },  { 9, 16 },  { 10, 16 }, { 10, 15 }, { 11, 15 }, { 12, 15 }, { 12, 14 }, { 13, 14 },
	{ 14, 14 }, { 14, 13 }, { 15, 13 }, { 15, 12 }, { 16, 12 }, { 16, 11 }, { 17, 11 }, { 17, 10 },
	{ 18, 10 }, { 18, 9 },  { 18, 8 },  { 19, 8 },  { 19, 7 },  { 19, 6 },  { 20, 6 },  { 20, 5 },
	{ 21, 4 },  { 21, 3 },  { 21, 2 },  { 21, 1 },  { 21, 0 },
};
// GLOBAL: XVT 0x51C55C
int g_radarEllipseClampCachedResolutionMode = FLIGHT_RESOLUTION_320X240;
// GLOBAL: XVT 0xA08C78
int16_t radary = 0;
// GLOBAL: XVT 0xA08C7C
int16_t radarx = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x425AE0
int MATH2_ABoverC32(int a, int b, int c) {
	uint64_t product;
	int negative;

	negative = 0;
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

	if (negative != 0) {
		product = (uint64_t)(uint32_t)b * (uint32_t)a;
		if ((uint32_t)(product >> 32) >= (uint32_t)c)
			a = INT32_MAX;
		else
			a = (int)(uint32_t)(product / (uint32_t)c);
		a = (int)(0u - (uint32_t)a);
		return a;
	}

	product = (uint64_t)(uint32_t)b * (uint32_t)a;
	if ((uint32_t)(product >> 32) >= (uint32_t)c)
		a = INT32_MAX;
	else
		a = (int)(uint32_t)(product / (uint32_t)c);
	return a;
}

// FUNCTION: XVT 0x425B70
unsigned int MATH2_fraction(uint16_t value, uint16_t fracQ16) {
	unsigned int product;
	unsigned int result;

	result = value;
	if (fracQ16 != 0xffffu) {
		product = (unsigned int)value * fracQ16;
		result = product + 0x8000u;
		result >>= 16;
	}

	return result;
}

// FUNCTION: XVT 0x425BA0
unsigned int MATH2_longfraction(unsigned int value, uint16_t fracQ16) {
	if (fracQ16 == 0xffffu) {
		return value;
	}
	return (((value & 0xffffu) * fracQ16) >> 16) + ((value >> 16) * fracQ16);
}

// FUNCTION: XVT 0x425C20
uint16_t MATH2_divide(uint16_t numerator, uint16_t denominator) {
	if (numerator == denominator)
		return 0xffffu;
	if (denominator == 0)
		return 0;
	if (numerator < denominator)
		return ((uint32_t)numerator << 16) / (uint32_t)denominator;
	return 0xffffu;
}

// FUNCTION: XVT 0x425C60
unsigned int MATH2_percentage(unsigned int numerator, unsigned int denominator) {
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

// FUNCTION: XVT 0x425D80
unsigned int MATH2_mphconvert(int16_t speed, uint16_t divisor) {
	unsigned int ticksPerUnit;
	unsigned int scaledValue;
	unsigned int result;

	scaledValue = 4660 * speed + 128;
	scaledValue >>= 8;
	ticksPerUnit = divisor;
	result = scaledValue / ticksPerUnit;
	if ((scaledValue & ticksPerUnit) > (scaledValue >> 1)) {
		result++;
	}
	return result;
}

// FUNCTION: XVT 0x425E30
int16_t MATH2_getradarcoord(int a1, int a2, int a3) {
	int angle;
	int tableIndex;
	int projectedY;
	int projectedX;
	int16_t arctanValues[2];
	uint16_t angleDivisor;
	uint8_t shift;

	if (g_flightResolutionMode != g_radarEllipseClampCachedResolutionMode) {
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
			uint8_t yLimit;
			int remaining;

			tableIndex = 0;
			remaining = 37;
			do {
				yLimit = g_radarEllipseClampMode19Preset[tableIndex].yLimit;
				g_radarEllipseClampTable[tableIndex].xLimit =
					g_radarEllipseClampMode19Preset[tableIndex].xLimit;
				g_radarEllipseClampTable[tableIndex].yLimit = yLimit;
				++tableIndex;
				--remaining;
			} while (remaining != 0);
		} else if (g_flightResolutionMode == FLIGHT_RESOLUTION_480X360) {
			tableIndex = 0;
			angle = 0;
			do {
				g_radarEllipseClampTable[tableIndex].xLimit = (uint8_t)trig2_sinewordmult(30, (int16_t)angle);
				g_radarEllipseClampTable[tableIndex].yLimit =
					(uint8_t)trig2_cosinewordmult(30, (int16_t)angle);
				++tableIndex;
				angle += 443;
			} while (angle < 0x4000);
		} else {
			tableIndex = 0;
			angle = 0;
			do {
				g_radarEllipseClampTable[tableIndex].xLimit = (uint8_t)trig2_sinewordmult(44, (int16_t)angle);
				g_radarEllipseClampTable[tableIndex].yLimit =
					(uint8_t)trig2_cosinewordmult(44, (int16_t)angle);
				++tableIndex;
				angle += 443;
			} while (angle < 0x4000);
		}
		g_radarEllipseClampCachedResolutionMode = g_flightResolutionMode;
	}

	projectedY = a2;
	projectedX = a1;
	if (a1 < 0) {
		projectedX = (int)(0u - (uint32_t)a1);
	}
#ifdef XVT_MODERN
	shift = (uint8_t)(perspShift - 5);
	projectedX = (int)((uint32_t)projectedX << (shift & 31u));
#else
	shift = perspShift - 5;
	projectedX <<= shift;
#endif
	if (a3 != 0) {
		projectedX /= a3;
	}
	if (projectedX > INT16_MAX) {
		projectedX = INT16_MAX;
	}

	if (a2 < 0) {
		projectedY = (int)(0u - (uint32_t)a2);
	}
#ifdef XVT_MODERN
	projectedY = (int)((uint32_t)projectedY << (shift & 31u));
#else
	projectedY <<= shift;
#endif
	if (a3 != 0) {
		projectedY /= a3;
	}
	if (projectedY > INT16_MAX) {
		projectedY = INT16_MAX;
	}

	radarx = (int16_t)projectedX;
	radary = (int16_t)projectedY;
	trig2_calcarctan_core(projectedX, projectedY, &arctanValues[1], arctanValues);
	arctanValues[1] = (int16_t)-arctanValues[1];
	angleDivisor = 443;
	arctanValues[1] += 0x4000;
	arctanValues[1] = (uint16_t)arctanValues[1] / angleDivisor;
	tableIndex = (uint16_t)arctanValues[1];

	arctanValues[0] = g_radarEllipseClampTable[tableIndex].xLimit;
	if (radarx > (int)(uint16_t)arctanValues[0]) {
		radarx = arctanValues[0];
	}
	if (a1 < 0) {
		radarx = (int16_t)-radarx;
	}

	arctanValues[0] = g_radarEllipseClampTable[tableIndex].yLimit;
	if (radary > (int)(uint16_t)arctanValues[0]) {
		radary = arctanValues[0];
	}
	if (a2 < 0) {
		radary = (int16_t)-radary;
	}
	return radary;
}
