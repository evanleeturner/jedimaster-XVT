#ifndef XVT_FLIGHT_FLIGHT_OBJECT_H
#define XVT_FLIGHT_FLIGHT_OBJECT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t g_billboard_texture_sequence_index;
extern int16_t *g_billboard_texture_frame_sequence;
extern uint16_t g_local_debris_recycle_slot_cursor;

void flight_object_update_special_behavior(void);
void flight_object_advance_texture_frame_sequence(unsigned int object_idx);
void flight_object_update_player_hyperspace_transition(int player_idx);
void flight_object_recycle_local_debris_near_player(void);

#ifdef __cplusplus
}
#endif

#endif
