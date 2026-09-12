#ifndef XVT_UTIL_GAME_RAND_H
#define XVT_UTIL_GAME_RAND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t g_gameRandStateA;
extern int16_t g_gameRandStateB;
extern uint16_t g_gameRand2ValueState;
extern uint16_t g_gameRand2FeedbackState;

int16_t GameRand(void);
uint16_t GameRand2(void);
uint16_t GameRandRange(uint16_t modulus);

#ifdef __cplusplus
}
#endif

#endif
