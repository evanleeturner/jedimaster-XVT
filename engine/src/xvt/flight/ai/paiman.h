#ifndef XVT_FLIGHT_AI_PAIMAN_H
#define XVT_FLIGHT_AI_PAIMAN_H

#include <stdint.h>

#include "xvt/flight/ai/pai.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const int16_t g_ai_escort_station_offset_x_by_variable[28];
extern const int16_t g_ai_escort_station_offset_y_by_variable[28];
extern const int16_t g_ai_escort_station_offset_z_by_variable[28];
extern const int16_t g_form_pos_x[34][6];
extern const int16_t g_form_pos_y[34][6];
extern const int16_t g_form_pos_z[34][6];
extern const int16_t g_formation_divisor[34];

typedef int16_t (*ai_course_order_maneuver_proc)(void);

typedef void (*ai_maneuver_init_proc)(void);

extern ai_course_order_maneuver_proc
	g_ai_course_order_maneuver_table[AI_MANEUVER_MODE_COUNT];
extern ai_course_order_maneuver_proc g_ai_current_maneuver_proc;
extern uint16_t g_order_throttle_to_craft_throttle_speed[12];
/* The matching translation unit defines this before use; modern consumers need the declaration. */
extern uint16_t g_ai_turn_away_state_delay_by_skill[4];

void paiman_initmaneuver(void);
void paiman_initturninsidemaneuver(void);
int16_t paiman_turninsidemaneuver(void);
void paiman_update_turn_inside_heading(unsigned int fallback_obj_idx);
void paiman_initsplitsmaneuver(void);
int16_t paiman_splitsmaneuver(void);
void paiman_initimmelmannmaneuver(void);
int16_t paiman_immelmannmaneuver(void);
void paiman_initscissorsmaneuver(void);
int16_t paiman_scissorsmaneuver(void);
void paiman_initrendezvousmaneuver(void);
int16_t paiman_rendezvousmaneuver(void);
void paiman_initcruisemaneuver(void);
int16_t paiman_cruisemaneuver(void);
void paiman_advance_order_waypoint(int object_index);
void paiman_initheadtowardfullmaneuver(void);
int16_t paiman_headtowardfullmaneuver(void);
void paiman_initrunawaymaneuver(void);
int16_t paiman_runawaymaneuver(void);
void paiman_initheadonattackmaneuver(void);
int16_t paiman_headonattackmaneuver(void);
void paiman_initfollowleadermaneuver(void);
int16_t paiman_followleadermaneuver(void);
void paiman_initsetupattackmaneuver(void);
int16_t paiman_setupattackmaneuver(void);
void paiman_initattackmaneuver(void);
int16_t paiman_attackmaneuver(void);
void paiman_initzoommaneuver(void);
int16_t paiman_zoommaneuver(void);
void paiman_initdivemaneuver(void);
int16_t paiman_divemaneuver(void);
void paiman_initsplitsdivemaneuver(void);
int16_t paiman_splitsdivemaneuver(void);
void paiman_initspeedawaymaneuver(void);
int16_t paiman_speedawaymaneuver(void);
void paiman_setup_speed_away_turn(unsigned int object_idx);
void paiman_initintohyperspacemaneuver(void);
int16_t paiman_intohyperspacemaneuver(void);
void paiman_initoutofhyperspacemaneuver(void);
int16_t paiman_outofhyperspacemaneuver(void);
void paiman_initescortmaneuver(void);
int16_t paiman_escortmaneuver(void);
void paiman_initboardmaneuver(void);
int16_t paiman_boardmaneuver(void);
void paiman_transfer_object_to_ai_team(unsigned int object_idx,
				       struct craft_data *craft,
				       uint8_t owner_flag);
void paiman_initawaitboardmaneuver(void);
int16_t paiman_awaitboardmaneuver(void);
void paiman_initheadtowardmaneuver(void);
int16_t paiman_headtowardmaneuver(void);
void paiman_initturnawaymaneuver(void);
int16_t paiman_turnawaymaneuver(void);
void paiman_setupturnawaycourse(unsigned int object_idx);
void paiman_initoutofhangarmaneuver(void);
int16_t paiman_outofhangarmaneuver(void);
void paiman_initavoidstarshipmaneuver(void);
int16_t paiman_avoidstarshipmaneuver(void);
void paiman_initwaitmaneuver(void);
int16_t paiman_waitmaneuver(void);
void paiman_initdropoffmaneuver(void);
int16_t paiman_dropoffmaneuver(void);
void paiman_initkamikazemaneuver(void);
int16_t paiman_kamikazemaneuver(void);
void paiman_initavoidattackermaneuver(void);
int16_t paiman_avoidattackermaneuver(void);
void paiman_initkamikazecopymaneuver(void);
int16_t paiman_kamikazecopymaneuver(void);
void paiman_setflighttotarget(uint16_t yaw_offset, int steer_pitch);
void paiman_initcruiseandrunawaycontrols(void);
void paiman_attacktarget(int16_t yaw_offset);
void paiman_calcplanelead(int target_obj_idx);
void paiman_calcformation(void);
void paiman_setturn(int turn_step);
void paiman_setpower(int ignored_obj_idx, int throttle);
void paiman_setspeed(int obj_idx, unsigned int desired_speed);

#ifdef __cplusplus
}
#endif

#endif
