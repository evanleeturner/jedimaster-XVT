#ifndef XVT_UTIL_GAME_RAND_H
#define XVT_UTIL_GAME_RAND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t g_game_rand_value_state;
extern int16_t g_game_rand_feedback_state;
extern uint16_t g_game_rand2_value_state;
extern uint16_t g_game_rand2_feedback_state;

int16_t game_rand(void);
uint16_t game_rand2(void);
uint16_t game_rand_range(uint16_t modulus);

#ifdef __cplusplus
}
#endif

#endif
