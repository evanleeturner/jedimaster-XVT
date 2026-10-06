#include "xvt/flight/ai/paiman.h"

#include <string.h>

#include "xvt/assets/model_bounds.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

/* Side offset, in world units along the escorted craft's side axis, of each
 * escort station, indexed by the escort order's variable1: with the Y and Z
 * tables, entries 0 to 26 form a three by three by three grid 3,072 apart and
 * entry 27 is 0 on all three axes. paiman_escortmaneuver, its only reader,
 * multiplies the offset by 16 when the escorted craft's type has a
 * max_bounds_extent of 3,000 or more. */
// GLOBAL: XVT 0x527768
const int16_t g_ai_escort_station_offset_x_by_variable[28] = {
	-3072, 0,     3072,  -3072, 0,	   3072,  -3072, 0,    3072,  -3072,
	0,     3072,  -3072, 0,	    3072,  -3072, 0,	 3072, -3072, 0,
	3072,  -3072, 0,     3072,  -3072, 0,	  3072,	 0};
/* Offset along the escorted craft's up axis of each escort station, indexed by
 * the escort order's variable1; see g_ai_escort_station_offset_x_by_variable. */
// GLOBAL: XVT 0x5277A0
const int16_t g_ai_escort_station_offset_y_by_variable[28] = {
	3072,  3072,  3072,  3072,  3072,  3072,  3072,	 3072, 3072,  0,
	0,     0,     0,     0,	    0,	   0,	  0,	 0,    -3072, -3072,
	-3072, -3072, -3072, -3072, -3072, -3072, -3072, 0};
/* Offset along the escorted craft's forward axis of each escort station,
 * indexed by the escort order's variable1; see
 * g_ai_escort_station_offset_x_by_variable. */
// GLOBAL: XVT 0x5277D8
const int16_t g_ai_escort_station_offset_z_by_variable[28] = {
	3072, 3072, 3072, 0, 0,	    0,	   -3072, -3072, -3072, 3072,
	3072, 3072, 0,	  0, 0,	    -3072, -3072, -3072, 3072,	3072,
	3072, 0,    0,	  0, -3072, -3072, -3072, 0};

/* Side offset, in model bound sizes, of each place in each of the 34
 * formations, by craft_ordinal 0 to 5. paiman_calcformation,
 * mission_init_flight_group_object_slot and mission_resolve_formation_slot_world_loc
 * read it. */
// GLOBAL: XVT 0x527970
const int16_t g_form_pos_x[34][6] = {
	{0, 1, -1, 2, -2, 3},	  {0, 1, -2, -3, 2, 3},
	{0, 0, 0, 0, 0, 0},	  {0, 1, -1, 2, -2, 3},
	{1, 2, 3, 4, 5, 6},	  {-1, -2, -3, -4, -5, -6},
	{0, -1, 0, -1, 0, -1},	  {0, 1, -1, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 1, -1, 1, -1, 0},
	{0, 1, -1, 2, -2, 3},	  {0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 1, 2, 3, 4, 5},
	{-1, -2, -3, -4, -5, -6}, {0, 0, 1, 2, 3, 4},
	{1, 2, 3, 4, 5, 6},	  {0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 1, -1, 2, -2, 3},
	{0, 1, -1, 2, -2, 3},	  {-1, 1, -1, 1, -1, 1},
	{0, 0, 0, 0, 0, 0},	  {-1, 1, -1, 1, -1, 1},
	{0, 0, 0, -1, 1, 0},	  {0, 1, -1, 0, 0, 0},
	{0, 2, -3, 3, -2, 0},	  {0, 0, 0, 0, 0, 0},
	{0, 2, -3, 3, -2, 0},	  {-1, 1, -2, 2, -1, 1},
	{0, 0, 0, 0, 0, 0},	  {-1, 1, -2, 2, -1, 1},
};
/* Forward offset, in model bound sizes, of each place in each of the 34
 * formations, by craft_ordinal 0 to 5; see g_form_pos_x. */
// GLOBAL: XVT 0x527B08
const int16_t g_form_pos_y[34][6] = {
	{0, -1, -1, -2, -2, -3},  {3, 2, 1, 0, -1, -2},
	{-1, -2, -3, -4, -5, -6}, {0, 0, 0, 0, 0, 0},
	{0, -1, -2, -3, -4, -5},  {0, -1, -2, -3, -4, -5},
	{0, 0, -1, -1, -2, -2},	  {1, 0, 0, -1, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 0, 0, 0, 0, -1},
	{1, 2, 2, 3, 3, 4},	  {0, -1, -1, -2, -2, -3},
	{1, 2, 2, 3, 3, 4},	  {0, 1, 2, 3, 4, 5},
	{0, 0, 0, 0, 0, 0},	  {0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 1, 2, 3, 4, 5},
	{-1, -2, -3, -4, -5, -6}, {0, -1, -2, -3, -4, -5},
	{0, -1, -2, -3, -4, -5},  {0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {2, 2, 0, 0, -2, -2},
	{2, 2, 0, 0, -2, -2},	  {0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, -1},	  {0, 0, 0, 0, 1, -1},
	{3, -3, 1, 1, -3, 0},	  {3, -3, 1, 1, -3, 0},
	{0, 0, 0, 0, 0, 0},	  {2, 2, 0, 0, -2, -2},
	{2, 2, 0, 0, -2, -2},	  {0, 0, 0, 0, 0, 0},
};
/* Up offset, in model bound sizes, of each place in each of the 34 formations,
 * by craft_ordinal 0 to 5; see g_form_pos_x. */
// GLOBAL: XVT 0x527CA0
const int16_t g_form_pos_z[34][6] = {
	{0, 0, 0, 0, 0, 0},	  {0, 1, -1, -2, 2, 3},
	{0, 0, 0, 0, 0, 0},	  {0, 0, 0, 0, 0, 0},
	{0, 1, 2, 3, 4, 5},	  {0, 1, 2, 3, 4, 5},
	{0, 0, 0, 0, 0, 0},	  {0, 0, 0, 0, 1, -1},
	{0, 1, 2, 3, 4, 5},	  {0, 1, 1, -1, -1, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 1, -1, 2, -2, 3},
	{0, 1, -1, 2, -2, 3},	  {0, 0, 0, 0, 0, 0},
	{-1, -2, -3, -4, -5, -6}, {0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 1, 2, 3, 4, 5},
	{1, 2, 3, 4, 5, 6},	  {0, 1, 2, 3, 4, 5},
	{-1, -2, -3, -4, -5, -6}, {0, 1, 1, 2, 2, 3},
	{0, -1, -1, -2, -2, -3},  {0, 0, 0, 0, 0, 0},
	{1, -1, 1, -1, 1, -1},	  {2, 2, 0, 0, -2, -2},
	{0, 1, -1, 0, 0, 0},	  {1, 0, 0, -1, 0, 0},
	{0, 0, 0, 0, 0, 0},	  {0, 2, -3, 3, -2, 0},
	{3, -3, 1, 1, -3, 0},	  {0, 0, 0, 0, 0, 0},
	{1, -1, 2, -2, 1, -1},	  {2, 2, 0, 0, -2, -2},
};
/* What each formation's offsets are divided by: 3 for formations 1 and 28 to
 * 30, 2 for 31 to 33, else 1. Read by paiman_calcformation and
 * mission_init_flight_group_object_slot. */
// GLOBAL: XVT 0x527E38
const int16_t g_formation_divisor[34] = {1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
					 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
					 1, 1, 1, 1, 3, 3, 3, 2, 2, 2};

/* Simulated seconds, by skill tier 0 to 2, between the course updates of the
 * turn-away and turn-inside maneuvers; the readers multiply by
 * SIMULATION_TICKS_PER_SECOND. Entry 3 is 0. Nothing writes it. */
// GLOBAL: XVT 0x527760
uint16_t g_ai_turn_away_state_delay_by_skill[4] = {5, 3, 1, 0};

/* Throttle, as a fraction of 65,535, for each mission order throttle setting 0
 * to 10, in tenths; entry 11 is 0. The cruise and rendezvous maneuvers,
 * mission_init_flight_group_object_slot and player_unbind_from_current_craft read it.
 * Nothing writes it. */
// GLOBAL: XVT 0x527810
uint16_t g_order_throttle_to_craft_throttle_speed[12] = {
	0,     6553,  13108, 19662, 26216, 32768,
	39322, 45876, 52430, 58984, 65535, 0};

/* Simulated seconds, by the flight group's AI level, between the weaves of the
 * avoid-attacker maneuver: 8 down to 1, then 0. */
// GLOBAL: XVT 0x527950
static const uint16_t g_ai_avoid_attacker_delay_seconds_by_group_ai[8] = {
	8, 6, 5, 3, 2, 1, 0, 0};

/* A fraction of a simulated second, by the flight group's AI level, added to
 * g_ai_avoid_attacker_delay_seconds_by_group_ai; every entry is 0, so it adds
 * nothing. */
// GLOBAL: XVT 0x527960
static const uint16_t g_ai_avoid_attacker_delay_frac_q16_by_group_ai[8] = {
	0, 0, 0, 0, 0, 0, 0, 0};

/* The speed paiman_outofhyperspacemaneuver gives an arriving craft for each
 * maneuver_phase value: 3,600 for 0 to 5, 1,800 for 6 to 8, 900 for 9 and 10, 0
 * for 11. It raises maneuver_phase before it reads, from 1 up to 10, so entries
 * 0 and 11 are never read. */
// GLOBAL: XVT 0x527938
static const uint16_t g_ai_hyperspace_arrival_speed_by_phase[12] = {
	3600, 3600, 3600, 3600, 3600, 3600, 1800, 1800, 1800, 900, 900, 0,
};

/* The step function of each maneuver mode, which paiorder_updatecourseorder
 * calls on each think; it returns nonzero when the maneuver is done. Mode 0
 * runs paiorder_nullhandler; rocket attack shares the attack function, stop
 * shares await-board, and evasive shares splits-dive. Nothing writes it. */
// GLOBAL: XVT 0x527828
ai_course_order_maneuver_proc
	g_ai_course_order_maneuver_table[AI_MANEUVER_MODE_COUNT] = {
		paiorder_nullhandler,
		paiman_turninsidemaneuver,
		paiman_splitsmaneuver,
		paiman_immelmannmaneuver,
		paiman_scissorsmaneuver,
		paiman_rendezvousmaneuver,
		paiman_cruisemaneuver,
		paiman_headtowardfullmaneuver,
		paiman_runawaymaneuver,
		paiman_headonattackmaneuver,
		paiman_followleadermaneuver,
		paiman_setupattackmaneuver,
		paiman_attackmaneuver,
		paiman_zoommaneuver,
		paiman_divemaneuver,
		paiman_splitsdivemaneuver,
		paiman_speedawaymaneuver,
		paiman_escortmaneuver,
		paiman_boardmaneuver,
		paiman_awaitboardmaneuver,
		paiman_headtowardmaneuver,
		paiman_intohyperspacemaneuver,
		paiman_outofhyperspacemaneuver,
		paiman_attackmaneuver,
		paiman_turnawaymaneuver,
		paiman_awaitboardmaneuver,
		paiman_outofhangarmaneuver,
		paiman_splitsdivemaneuver,
		paiman_avoidstarshipmaneuver,
		paiman_waitmaneuver,
		paiman_dropoffmaneuver,
		paiman_kamikazemaneuver,
		paiman_avoidattackermaneuver,
		paiman_kamikazecopymaneuver,
};

/* The start function of each maneuver mode, which paiman_initmaneuver calls.
 * Mode 0 calls paiorder_nullhandler through this void function type; the pairs
 * that share a step function share a start function too. Nothing writes it. */
// GLOBAL: XVT 0x5278B0
static ai_maneuver_init_proc g_maneuver_init_table[AI_MANEUVER_MODE_COUNT] = {
	(ai_maneuver_init_proc)paiorder_nullhandler,
	paiman_initturninsidemaneuver,
	paiman_initsplitsmaneuver,
	paiman_initimmelmannmaneuver,
	paiman_initscissorsmaneuver,
	paiman_initrendezvousmaneuver,
	paiman_initcruisemaneuver,
	paiman_initheadtowardfullmaneuver,
	paiman_initrunawaymaneuver,
	paiman_initheadonattackmaneuver,
	paiman_initfollowleadermaneuver,
	paiman_initsetupattackmaneuver,
	paiman_initattackmaneuver,
	paiman_initzoommaneuver,
	paiman_initdivemaneuver,
	paiman_initsplitsdivemaneuver,
	paiman_initspeedawaymaneuver,
	paiman_initescortmaneuver,
	paiman_initboardmaneuver,
	paiman_initawaitboardmaneuver,
	paiman_initheadtowardmaneuver,
	paiman_initintohyperspacemaneuver,
	paiman_initoutofhyperspacemaneuver,
	paiman_initattackmaneuver,
	paiman_initturnawaymaneuver,
	paiman_initawaitboardmaneuver,
	paiman_initoutofhangarmaneuver,
	paiman_initsplitsdivemaneuver,
	paiman_initavoidstarshipmaneuver,
	paiman_initwaitmaneuver,
	paiman_initdropoffmaneuver,
	paiman_initkamikazemaneuver,
	paiman_initavoidattackermaneuver,
	paiman_initkamikazecopymaneuver,
};

/* The step function paiorder_updatecourseorder picked last; only it writes and
 * calls it. */
// GLOBAL: XVT 0x9993F4
ai_course_order_maneuver_proc g_ai_current_maneuver_proc;

/* The start function paiman_initmaneuver picked last; only it writes and calls
 * it. */
// GLOBAL: XVT 0x9993F8
static ai_maneuver_init_proc g_ai_current_maneuver_init_proc = 0;

/* Starts the craft's maneuver_mode. Clears its push, hits_this_maneuver,
 * warheads_fired_this_maneuver and warhead_lock_ticks, sets commanded_speed to five
 * times the current order's speed, roll_state to 4 and maneuver_phase to 0, then
 * runs the mode's start function from g_maneuver_init_table through
 * g_ai_current_maneuver_init_proc. Works on g_cur_craft and g_pai_context. Does not
 * check maneuver_mode against the table's 34 entries. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x49F360
void paiman_initmaneuver(void)
{
	g_cur_craft->push_accum_x = 0;
	g_cur_craft->push_accum_y = g_cur_craft->push_accum_x;
	g_cur_craft->push_accum_z = g_cur_craft->push_accum_y;
	g_cur_craft->ai_flight.hits_this_maneuver = 0;
	g_cur_craft->ai_flight.warheads_fired_this_maneuver = 0;
	g_cur_craft->warhead_lock_ticks = 0;
	g_cur_craft->commanded_speed =
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.speed;
	g_cur_craft->commanded_speed *= 5;
	g_cur_craft->ai_flight.roll_state = 4;
	g_pai_context.controller->maneuver_phase = 0;
	if (g_flight_sim_side_effects_suppressed == 0 &&
	    g_pai_context.controller->maneuver_mode >= AI_MANEUVER_MODE_COUNT) {
		XVT_LOG_WARN(
			"ai.maneuver_invalid object=%d maneuver=%d plan=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.controller->maneuver_mode,
			(int)g_pai_context.controller->running_plan_id);
	}
	g_ai_current_maneuver_init_proc =
		g_maneuver_init_table[g_pai_context.controller->maneuver_mode];
	g_ai_current_maneuver_init_proc();
	XVT_LOG_DEBUG(
		"ai.maneuver_started object=%d maneuver=%d plan=%d order=%d target=%d phase=%d timer=%d step_timer=%d heading=%u pitch=%u roll=%u throttle=%u speed=%d match=%d predicted=%d",
		(int)g_pai_context.object_index,
		(int)g_pai_context.controller->maneuver_mode,
		(int)g_pai_context.controller->running_plan_id,
		(int)g_pai_context.order_slot,
		(int)g_pai_context.controller->target_obj_idx,
		(int)g_pai_context.controller->maneuver_phase,
		g_pai_context.controller->maneuver_timer,
		(int)g_pai_context.controller->secondary_maneuver_timer,
		(unsigned)g_pai_context.controller->target_xy_angle,
		(unsigned)g_pai_context.controller->target_z_angle,
		(unsigned)g_pai_context.controller->target_roll,
		(unsigned)g_cur_craft->throttle_speed,
		(int)g_cur_craft->commanded_speed,
		(int)(g_pai_context.craft == g_cur_craft),
		g_flight_sim_side_effects_suppressed);
}

/* Starts turn inside: picks a side at random in maneuver_phase, sets the first
 * heading with paiman_update_turn_inside_heading, and runs it for 10 to 17 times
 * SIMULATION_TICKS_PER_SECOND ticks. */
// FUNCTION: XVT 0x49F450
void paiman_initturninsidemaneuver(void)
{
	g_pai_context.controller->maneuver_phase = game_rand() & 1;
	paiman_update_turn_inside_heading(g_pai_context.object_index);
	g_pai_context.controller->maneuver_timer =
		SIMULATION_TICKS_PER_SECOND * ((game_rand() & 7) + 10);
}

/* Returns 1 once the maneuver timer has run out, else 0; each time
 * secondary_maneuver_timer runs out it sets a new heading with
 * paiman_update_turn_inside_heading. */
// FUNCTION: XVT 0x49F4A0
int16_t paiman_turninsidemaneuver(void)
{
	if (g_pai_context.controller->maneuver_timer == 0) {
		return 1;
	}
	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		paiman_update_turn_inside_heading(g_pai_context.object_index);
	}
	return 0;
}

/* Turns the craft a quarter turn off its attacker's yaw, or off the yaw of the
 * fallback object when it has no attacker: minus 0x4000 for an odd
 * maneuver_phase, plus for an even one. The turn step is half the effective
 * skill plus 0x8000. Sets secondary_maneuver_timer to the tier's
 * g_ai_turn_away_state_delay_by_skill seconds in ticks. */
// FUNCTION: XVT 0x49F4E0
void paiman_update_turn_inside_heading(unsigned int fallback_obj_idx)
{
	uint16_t yaw;

	if (g_cur_craft->last_attacker_obj_idx != UINT16_MAX) {
		if ((g_pai_context.controller->maneuver_phase & 1) != 0) {
			yaw = g_object_table[g_cur_craft->last_attacker_obj_idx]
				      .yaw -
			      0x4000;
		} else {
			yaw = g_object_table[g_cur_craft->last_attacker_obj_idx]
				      .yaw +
			      0x4000;
		}
	} else {
		if ((g_pai_context.controller->maneuver_phase & 1) != 0) {
			yaw = g_object_table[fallback_obj_idx].yaw - 0x4000;
		} else {
			yaw = g_object_table[fallback_obj_idx].yaw + 0x4000;
		}
	}
	g_pai_context.controller->target_xy_angle = yaw;
	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	unsigned int turn_step = effective_skill >> 1;
	turn_step += 0x8000;
	paiman_setturn((uint16_t)turn_step);
	g_pai_context.controller->secondary_maneuver_timer =
		SIMULATION_TICKS_PER_SECOND *
		g_ai_turn_away_state_delay_by_skill[g_pai_context.skill_tier];
}

/* Starts splits: no turn, and a pitch through a loop at full step with
 * pitch_state 2 until it comes back to target_z_angle 0x4000. It sets target_roll
 * to 0x8000 and roll_step to full but leaves roll_state at 4 from
 * paiman_initmaneuver, so no roll starts. */
// FUNCTION: XVT 0x49F5C0
void paiman_initsplitsmaneuver(void)
{
	g_cur_craft->ai_flight.roll_step = 0xFFFFu;
	g_pai_context.controller->target_roll = 0x8000;
	g_cur_craft->ai_flight.turn_state = 0;
	g_cur_craft->ai_flight.pitch_through_loop = 1;
	g_pai_context.controller->target_z_angle = 0x4000;
	g_cur_craft->ai_flight.pitch_state = 2;
	g_cur_craft->ai_flight.pitch_step_scale = 0xFFFFu;
}

/* Returns 1 once the roll and the pitch are done (roll_state 4, pitch_state 3),
 * else 0. */
// FUNCTION: XVT 0x49F620
int16_t paiman_splitsmaneuver(void)
{
	return g_cur_craft->ai_flight.roll_state == 4 &&
	       g_cur_craft->ai_flight.pitch_state == 3;
}

/* Starts the Immelmann: full throttle, a roll to 0 at full step, no turn, and a
 * pitch to target_z_angle 0x4000 at full step without a loop, with pitch_state set
 * by which side of 0x4000 the pitch is on (3 when on it). Sets the maneuver
 * timer to 0. */
// FUNCTION: XVT 0x49F650
void paiman_initimmelmannmaneuver(void)
{
	paiman_setpower(g_pai_context.object_index, 0xFFFFu);
	g_cur_craft->ai_flight.roll_state = 1;
	g_cur_craft->ai_flight.roll_step = 0xFFFFu;
	g_pai_context.controller->target_roll = 0;
	g_cur_craft->ai_flight.turn_state = 0;
	g_pai_context.controller->target_z_angle = 0x4000;
	g_cur_craft->ai_flight.pitch_step_scale = 0xFFFFu;
	g_cur_craft->ai_flight.pitch_through_loop = 0;
	uint16_t pitch = g_cur_craft->pitch;
	if (pitch < 0x4000u) {
		g_cur_craft->ai_flight.pitch_state = 2;
	} else if (pitch > 0x4000u) {
		g_cur_craft->ai_flight.pitch_state = 1;
	} else {
		g_cur_craft->ai_flight.pitch_state = 3;
	}
	g_pai_context.controller->maneuver_timer = 0;
}

/* Steps the Immelmann by maneuver_phase; returns 1 at 2 once the roll and pitch
 * are done, else 0. At 0, once the pitch is at 0x4000, it starts a pitch
 * through a loop with pitch_state 1 back to 0x4000 and moves to 1; at 1, once
 * that ends, a roll to 0 at full step, no turn, and 2. */
// FUNCTION: XVT 0x49F700
int16_t paiman_immelmannmaneuver(void)
{
	int maneuver_phase = g_pai_context.controller->maneuver_phase;
	switch (maneuver_phase) {
	case 0:
		if (g_cur_craft->ai_flight.pitch_state == 3) {
			g_cur_craft->ai_flight.pitch_state = 1;
			g_cur_craft->ai_flight.pitch_step_scale = -1;
			g_cur_craft->ai_flight.pitch_through_loop = 1;
			g_pai_context.controller->target_z_angle = 0x4000;
			g_pai_context.controller->maneuver_phase = 1;
		}
		break;
	case 1:
		if (g_cur_craft->ai_flight.pitch_state == 3) {
			g_cur_craft->ai_flight.roll_state = 1;
			g_cur_craft->ai_flight.roll_step = -1;
			g_pai_context.controller->target_roll = 0;
			g_cur_craft->ai_flight.turn_state = 0;
			g_pai_context.controller->maneuver_phase = 2;
		}
		break;
	case 2:
		if (g_cur_craft->ai_flight.roll_state == 4 &&
		    g_cur_craft->ai_flight.pitch_state == 3) {
			return 1;
		}
		break;
	default:
		break;
	}
	return 0;
}

/* Starts scissors: a turn to half a circle off the attacker's yaw, or its own
 * when it has none, at half the effective skill plus 0x8000; a roll that does
 * not stop (roll_state 3) at full step, its way set by a random target_roll; a
 * maneuver timer of 10 to 17 times SIMULATION_TICKS_PER_SECOND and a secondary
 * timer of 472 ticks. */
// FUNCTION: XVT 0x49F7E0
void paiman_initscissorsmaneuver(void)
{
	int object_index;

	uint16_t last_attacker_obj_idx = g_cur_craft->last_attacker_obj_idx;
	if (last_attacker_obj_idx != UINT16_MAX) {
		object_index = last_attacker_obj_idx;
	} else {
		object_index = g_pai_context.object_index;
	}
	g_pai_context.controller->target_xy_angle =
		g_object_table[object_index].yaw + 0x8000;
	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	unsigned int turn_step = (effective_skill >> 1) + 0x8000;
	paiman_setturn((uint16_t)turn_step);
	g_cur_craft->ai_flight.roll_state = 3;
	g_cur_craft->ai_flight.roll_step = -1;
	g_pai_context.controller->target_roll = (uint16_t)game_rand();
	g_pai_context.controller->maneuver_timer =
		SIMULATION_TICKS_PER_SECOND * ((game_rand() & 7) + 10);
	g_pai_context.controller->secondary_maneuver_timer = 472;
}

/* Returns 1, setting roll_state 4, once the maneuver timer has run out; else 0.
 * Each time secondary_maneuver_timer runs out it turns again by half a circle and
 * flips the roll's way, and sets the timer to 472 plus up to 236 ticks at
 * random; a flight group of AI level 3 or more then fires a flare on one draw
 * in four when it has one and cm_fire_cooldown_timer is 0. */
// FUNCTION: XVT 0x49F8B0
int16_t paiman_scissorsmaneuver(void)
{
	if (g_pai_context.controller->maneuver_timer == 0) {
		g_cur_craft->ai_flight.roll_state = 4;
		return 1;
	}
	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		g_cur_craft->ai_flight.turn_state = 1;
		g_pai_context.controller->target_xy_angle += 0x8000;
		g_pai_context.controller->target_roll ^= 0x8000u;
		g_pai_context.controller->secondary_maneuver_timer =
			math2_fraction((uint16_t)game_rand(), 0xEC) + 472;
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.group_ai >= 3 &&
		    (game_rand() & 3) == 3 &&
		    g_cur_craft->cm_type_id == COUNTERMEASURE_TYPE_FLARE &&
		    g_cur_craft->cm_ammo_count != 0 &&
		    g_cur_craft->cm_fire_cooldown_timer == 0) {
			laser_createcountermeasureprojectile(
				g_pai_context.object_index,
				COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
		}
	}
	return 0;
}

/* Starts rendezvous: steers at the aim point with paiman_setflighttotarget and
 * sets the throttle from the order's throttle setting, full when that gives
 * 0. */
// FUNCTION: XVT 0x49F9A0
void paiman_initrendezvousmaneuver(void)
{
	paiman_setflighttotarget(0, 1);
	uint16_t throttle = g_order_throttle_to_craft_throttle_speed
		[g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			 .fg.orders[g_pai_context.order_slot]
			 .throttle];
	if (throttle == 0) {
		throttle = UINT16_MAX;
	}
	paiman_setpower(g_pai_context.object_index, throttle);
}

/* Steers at the aim point and sets the throttle as
 * paiman_initrendezvousmaneuver does; returns 0. */
// FUNCTION: XVT 0x49FA10
int16_t paiman_rendezvousmaneuver(void)
{
	paiman_setflighttotarget(0, 1);
	uint16_t throttle = g_order_throttle_to_craft_throttle_speed
		[g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			 .fg.orders[g_pai_context.order_slot]
			 .throttle];
	if (throttle == 0) {
		throttle = UINT16_MAX;
	}
	paiman_setpower(g_pai_context.object_index, throttle);
	return 0;
}

/* Starts cruise: levels the craft with paiman_initcruiseandrunawaycontrols,
 * steers at the aim point when its roll is below 0x8000, sets the throttle from
 * the order's throttle setting and secondary_maneuver_timer to
 * SIMULATION_TICKS_PER_SECOND. */
// FUNCTION: XVT 0x49FA80
void paiman_initcruisemaneuver(void)
{
	paiman_initcruiseandrunawaycontrols();
	if (g_object_table[g_pai_context.object_index].roll < 0x8000) {
		paiman_setflighttotarget(0, 1);
	}
	paiman_setpower(
		g_pai_context.object_index,
		g_order_throttle_to_craft_throttle_speed
			[g_mission_flight_groups
				 [g_pai_context.craft_flight_group_index]
					 .fg.orders[g_pai_context.order_slot]
					 .throttle]);
	g_pai_context.controller->secondary_maneuver_timer =
		SIMULATION_TICKS_PER_SECOND;
}

/* Flies the order's waypoints; returns 0 on every path. Within 0x1000 world
 * units of the aim point (0x2000 for a starship) it moves to the next waypoint
 * with paiman_advance_order_waypoint; a starship or freighter whose waypoint_index
 * came back unchanged then stops: turn done, throttle 0, and a push onto the
 * aim point. Each time secondary_maneuver_timer runs out, when neither climbing
 * nor diving and more than 512 off the aim point in Z, it sets a pitch toward
 * it, climb_state 1 and throttle 0xC000, which the paiman_setflighttotarget call
 * right after and the throttle below replace; it steers at the aim point,
 * resets the timer, and rolls level once its turn is done. The throttle is the
 * order's setting; a starship's is 0 while it turns more than 0x1000 off
 * course. Sets the trig2_ globals. */
// FUNCTION: XVT 0x49FB20
int16_t paiman_cruisemaneuver(void)
{
	pai_calc_angles_to_aim_point();
	if (trig2_polardistance <
	    (g_object_table[g_pai_context.object_index].genus_id == 4
		     ? 0x2000
		     : 0x1000)) {
		uint8_t waypoint_index =
			g_pai_context.controller->waypoint_index;
		paiman_advance_order_waypoint(g_pai_context.object_index);
		uint8_t genus_id =
			g_object_table[g_pai_context.object_index].genus_id;
		if ((genus_id == 4 || genus_id == 3) &&
		    g_pai_context.controller->waypoint_index ==
			    waypoint_index) {
			g_cur_craft->ai_flight.turn_state = 3;
			paiman_setpower(g_pai_context.object_index, 0);
			g_cur_craft->push_accum_x =
				g_pai_context.controller->aim_point_x -
				g_object_table[g_pai_context.object_index]
					.world_x;
			g_cur_craft->push_accum_y =
				g_pai_context.controller->aim_point_y -
				g_object_table[g_pai_context.object_index]
					.world_y;
			g_cur_craft->push_accum_z =
				g_pai_context.controller->aim_point_z -
				g_object_table[g_pai_context.object_index]
					.world_z;
			return 0;
		}
	}

	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		if (g_cur_craft->ai_flight.dive_state != 1 &&
		    g_cur_craft->ai_flight.climb_state != 1) {
			int z_distance =
				g_pai_context.controller->aim_point_z -
				g_object_table[g_pai_context.object_index]
					.world_z;
			if (z_distance < 0) {
				z_distance = -z_distance;
			}
			if (z_distance > 512) {
				g_pai_context.controller->target_z_angle =
					(uint16_t)trig2_pitch;
				if (g_pai_context.controller->target_z_angle <=
				    g_cur_craft->pitch) {
					g_cur_craft->ai_flight.pitch_state = 1;
				} else {
					g_cur_craft->ai_flight.pitch_state = 2;
				}
				g_cur_craft->ai_flight.climb_state = 1;
				paiman_setpower(g_pai_context.object_index,
						0xC000);
			}
		}

		paiman_setflighttotarget(0, 1);
		g_pai_context.controller->secondary_maneuver_timer =
			SIMULATION_TICKS_PER_SECOND;
		if (g_cur_craft->ai_flight.turn_state == 3 &&
		    g_object_table[g_pai_context.object_index].roll != 0) {
			g_cur_craft->ai_flight.roll_state = 1;
			g_cur_craft->ai_flight.roll_step = UINT16_MAX;
			g_pai_context.controller->target_roll = 0;
		}
	}

	uint16_t throttle_index =
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.throttle;
	struct object_record *object =
		&g_object_table[g_pai_context.object_index];
	if (object->genus_id != 4) {
		paiman_setpower(g_pai_context.object_index,
				g_order_throttle_to_craft_throttle_speed
					[throttle_index]);
		return 0;
	}

	uint16_t yaw_difference =
		(uint16_t)(object->yaw -
			   g_pai_context.controller->target_xy_angle);
	if (yaw_difference >= 0x8000) {
		yaw_difference = (uint16_t)(0u - yaw_difference);
	}
	if (g_cur_craft->ai_flight.turn_state == 2 &&
	    yaw_difference >= 0x1000) {
		paiman_setpower(g_pai_context.object_index, 0);
		return 0;
	}

	paiman_setpower(
		g_pai_context.object_index,
		g_order_throttle_to_craft_throttle_speed[throttle_index]);
	return 0;
}

/* Moves the craft to its next waypoint, mission point 4 to 11, and targets it:
 * past 11 or at a point not enabled it goes back to 4, and on formldr1pln,
 * formevadeldr1pln or starshipformpln that adds 1 to the current order slot's
 * goal_progress. Clears the target's signature and live flag and sets the aim
 * point. The argument is ignored. */
// FUNCTION: XVT 0x49FE90
void paiman_advance_order_waypoint(int object_index)
{
	(void)object_index;

	uint8_t current_plan_id = g_pai_context.controller->current_plan_id;
	uint8_t waypoint_index = ++g_pai_context.controller->waypoint_index;
	if (waypoint_index > 11 ||
	    g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			    .fg.mission_point_enabled[waypoint_index] == 0) {
		g_pai_context.controller->waypoint_index = 4;
		if (strcmp(g_plan_table[current_plan_id].name, "formldr1pln") ==
			    0 ||
		    strcmp(g_plan_table[current_plan_id].name,
			   "formevadeldr1pln") == 0 ||
		    strcmp(g_plan_table[current_plan_id].name,
			   "starshipformpln") == 0) {
			++g_pai_context.controller->order_progress
				  .goal_progress[g_pai_context.order_slot];
		}
	}
	g_pai_context.controller->target_obj_idx =
		g_pai_context.controller->waypoint_index + 0x8000;
	g_pai_context.controller->target_signature = 0;
	g_pai_context.controller->has_live_target = 0;
	pai_update_aim_point_from_order_target();
	if (g_pai_context.controller->waypoint_index + 1 != waypoint_index) {
		XVT_LOG_DEBUG(
			"ai.waypoint_reached object=%d plan=%d waypoint=%d progress=%d predicted=%d",
			(int)g_pai_context.object_index, (int)current_plan_id,
			(int)g_pai_context.controller->waypoint_index,
			(int)g_pai_context.controller->order_progress
				.goal_progress[g_pai_context.order_slot],
			g_flight_sim_side_effects_suppressed);
	}
}

/* Starts head toward at full throttle: steers at the aim point and sets
 * secondary_maneuver_timer to SIMULATION_TICKS_PER_SECOND. */
// FUNCTION: XVT 0x49FF70
void paiman_initheadtowardfullmaneuver(void)
{
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
	g_pai_context.controller->secondary_maneuver_timer =
		SIMULATION_TICKS_PER_SECOND;
}

/* Each time secondary_maneuver_timer runs out, moves the aim point to the target
 * and steers at it at full throttle, then resets the timer. Returns 0. */
// FUNCTION: XVT 0x49FFA0
int16_t paiman_headtowardfullmaneuver(void)
{
	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		pai_update_aim_point_from_order_target();
		paiman_setflighttotarget(0, 1);
		paiman_setpower(g_pai_context.object_index, UINT16_MAX);
		g_pai_context.controller->secondary_maneuver_timer =
			SIMULATION_TICKS_PER_SECOND;
	}
	return 0;
}

/* Starts run away: levels the craft with paiman_initcruiseandrunawaycontrols
 * and, when its roll is below 0x8000, turns to half a circle off the heading to
 * the aim point. */
// FUNCTION: XVT 0x49FFF0
void paiman_initrunawaymaneuver(void)
{
	paiman_initcruiseandrunawaycontrols();
	if (g_object_table[g_pai_context.object_index].roll < 0x8000) {
		paiman_setflighttotarget(0x8000, 1);
	}
}

/* Turns to half a circle off the heading to the aim point, the pitch still
 * following the aim point, at full throttle; returns 0. */
// FUNCTION: XVT 0x4A0030
int16_t paiman_runawaymaneuver(void)
{
	paiman_setflighttotarget(0x8000, 1);
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
	return 0;
}

/* Starts the head-on attack: targets the last attacker, with its signature, and
 * sets has_live_target to 1 even when there is none (target 0xFFFF); steers at
 * the aim point at full throttle for 1,888 ticks. */
// FUNCTION: XVT 0x4A0060
void paiman_initheadonattackmaneuver(void)
{
	g_pai_context.controller->target_obj_idx =
		g_cur_craft->last_attacker_obj_idx;
	if (g_cur_craft->last_attacker_obj_idx != UINT16_MAX) {
		g_pai_context.controller->target_signature =
			g_object_table[g_cur_craft->last_attacker_obj_idx]
				.object_signature;
	} else {
		g_pai_context.controller->target_signature = 0;
	}
	g_pai_context.controller->has_live_target = 1;
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
	g_pai_context.controller->maneuver_timer = 1888;
}

/* Returns 1 once the maneuver timer has run out; else steers at the aim point
 * at full throttle and returns 0. */
// FUNCTION: XVT 0x4A00F0
int16_t paiman_headonattackmaneuver(void)
{
	if (g_pai_context.controller->maneuver_timer == 0) {
		return 1;
	}
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
	return 0;
}

/* Does nothing. */
// FUNCTION: XVT 0x4A0130
void paiman_initfollowleadermaneuver(void) {}

/* Keeps a follower with its leader; returns 0 on every path. Farther than
 * 0x10000 world units from the leader (327,680 for a starship) it flies at the
 * leader's position at full throttle with no push. Otherwise it takes the
 * leader's heading, or the heading the leader is turning to, at an eighth of
 * the effective skill plus 0x4000; matches a player leader's speed by moving
 * its throttle 50 per unit of speed difference, or copies an AI leader's
 * throttle; matches the leader's pitch and, when the leader has no
 * impact_obj_idx, its roll, snapping to them within 0x400. Then it pushes toward
 * its formation place with paiman_calcformation, unless its leader is a
 * player's craft at speed 10 or less, which clears the push. Sets the trig2_
 * globals. */
// FUNCTION: XVT 0x4A0140
int16_t paiman_followleadermaneuver(void)
{
	uint16_t leader_object_idx = (uint8_t)g_cur_craft->leader_obj_idx;

	pai_object_ref_direction_to_object_ref((uint16_t)leader_object_idx,
					       g_pai_context.object_index);
	struct object_record *object =
		&g_object_table[g_pai_context.object_index];
	if ((object->genus_id != CRAFT_GENUS_STARSHIP &&
	     trig2_polardistance > 0x10000) ||
	    trig2_polardistance > 327680) {
		g_pai_context.controller->aim_point_x =
			g_object_table[leader_object_idx].world_x;
		g_pai_context.controller->aim_point_y =
			g_object_table[leader_object_idx].world_y;
		g_pai_context.controller->aim_point_z =
			g_object_table[leader_object_idx].world_z;
		paiman_setflighttotarget(0, 1);
		paiman_setpower(g_pai_context.object_index, UINT16_MAX);
		g_cur_craft->push_accum_x = 0;
		g_cur_craft->push_accum_y = 0;
		g_cur_craft->push_accum_z = 0;
		return 0;
	}

	uint16_t effective_skill_value;
	if (g_pai_context.leader_or_self_craft->ai_flight.turn_state == 2) {
		g_pai_context.controller->target_xy_angle =
			g_pai_context.leader_or_self_craft->ai_controller
				.target_xy_angle;
		effective_skill_value =
			pai_get_effective_skill_value(g_cur_craft);
		paiman_setturn((effective_skill_value >> 3) + 0x4000);
	} else {
		int16_t yaw = g_object_table[leader_object_idx].yaw;
		if (object->yaw != yaw) {
			g_pai_context.controller->target_xy_angle = yaw;
			effective_skill_value =
				pai_get_effective_skill_value(g_cur_craft);
			paiman_setturn((effective_skill_value >> 3) + 0x4000);
		}
	}

	{
		struct object_record *leader =
			&g_object_table[leader_object_idx];
		if (leader->player_owner_idx != -1) {
			uint16_t object_index = g_pai_context.object_index;
			uint16_t speed = leader->mobj->speed;
			uint16_t object_speed =
				g_object_table[object_index].mobj->speed;
			if (speed > object_speed) {
				uint16_t throttle_speed =
					g_cur_craft->throttle_speed;
				paiman_setpower(
					object_index,
					throttle_speed +
						(uint16_t)(50 *
							   (speed -
							    object_speed)));
				if (g_cur_craft->throttle_speed <
				    throttle_speed) {
					paiman_setpower(
						g_pai_context.object_index,
						UINT16_MAX);
				}
			} else if (speed < object_speed) {
				uint16_t throttle_speed =
					g_cur_craft->throttle_speed;
				paiman_setpower(
					object_index,
					throttle_speed -
						(uint16_t)(50 * (object_speed -
								 speed)));
				if (g_cur_craft->throttle_speed >
				    throttle_speed) {
					paiman_setpower(
						g_pai_context.object_index, 0);
				}
			}
		} else {
			paiman_setpower(g_pai_context.object_index,
					g_pai_context.leader_or_self_craft
						->throttle_speed);
		}
	}

	uint16_t target_angle;
	uint16_t angle_difference;
	{
		uint16_t *craft_pitch = &g_cur_craft->pitch;

		target_angle = g_pai_context.leader_or_self_craft->pitch;
		angle_difference = *craft_pitch - target_angle;
		if (angle_difference >= 0x8000) {
			angle_difference = (uint16_t)-angle_difference;
		}
		if (angle_difference < 0x400) {
			*craft_pitch = target_angle;
			g_cur_craft->ai_flight.pitch_state = 0;
		} else {
			g_pai_context.controller->target_z_angle = target_angle;
			if (g_cur_craft->pitch >=
			    g_pai_context.controller->target_z_angle) {
				g_cur_craft->ai_flight.pitch_state = 1;
			} else {
				g_cur_craft->ai_flight.pitch_state = 2;
			}
			g_cur_craft->ai_flight.pitch_step_scale = UINT16_MAX;
			g_cur_craft->ai_flight.pitch_through_loop = 0;
		}
	}

	if (g_pai_context.leader_or_self_craft->ai_flight.impact_obj_idx ==
	    UINT16_MAX) {
		uint16_t *object_roll =
			&g_object_table[g_pai_context.object_index].roll;

		target_angle =
			g_object_table[g_pai_context.leader_object_index].roll;
		angle_difference = *object_roll - target_angle;
		if (angle_difference >= 0x8000) {
			angle_difference = (uint16_t)-angle_difference;
		}
		if (angle_difference < 0x400) {
			*object_roll = target_angle;
			g_object_table[g_pai_context.object_index]
				.mobj->orient_matrix_dirty = 1;
			g_cur_craft->ai_flight.roll_state = 0;
		} else {
			g_pai_context.controller->target_roll = target_angle;
			g_cur_craft->ai_flight.roll_step = UINT16_MAX;
			g_cur_craft->ai_flight.roll_state = 1;
		}
	}

	object = &g_object_table[(uint8_t)g_cur_craft->leader_obj_idx];
	if (object->player_owner_idx == -1) {
		paiman_calcformation();
	} else if (object->mobj->speed > 10) {
		paiman_calcformation();
	} else {
		g_cur_craft->push_accum_x = 0;
		g_cur_craft->push_accum_y = 0;
		g_cur_craft->push_accum_z = 0;
	}
	return 0;
}

/* Starts setup attack: full throttle, and steers at the target with
 * paiman_attacktarget. */
// FUNCTION: XVT 0x4A0550
void paiman_initsetupattackmaneuver(void)
{
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
	paiman_attacktarget(0);
}

/* Steers at the target with paiman_attacktarget, at half throttle while the
 * craft's yaw is 0x3000 to 0xD000 off that heading, else full. Returns 0. */
// FUNCTION: XVT 0x4A0580
int16_t paiman_setupattackmaneuver(void)
{
	paiman_attacktarget(0);
	uint16_t object_index = g_pai_context.object_index;
	uint16_t yaw_difference = g_object_table[object_index].yaw -
				  g_pai_context.controller->target_xy_angle;
	if (yaw_difference >= 0x3000 && yaw_difference <= 0xD000) {
		paiman_setpower(object_index, 0x8000);
		return 0;
	}
	paiman_setpower(object_index, UINT16_MAX);
	return 0;
}

/* Starts attack and rocket attack: full throttle, and steers at the target with
 * paiman_attacktarget. */
// FUNCTION: XVT 0x4A05F0
void paiman_initattackmaneuver(void)
{
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
	paiman_attacktarget(0);
}

/* Runs attack and rocket attack; returns 1 when the break-off turn
 * (maneuver_phase 1) has timed out, else 0. Clears the push each think. Before
 * that it works out a break-off distance: 5,120 for a target outside the craft
 * slots; for a freighter, starship or platform 0x2000, 0xA000 for type 54,
 * 0x8000 more for types 90 and 91, doubled when the bearing to it lies outside
 * 0x2800 to 0x5800 off its yaw; for other craft 1,536 to 5,120 by the flight
 * group's AI level, doubled when the craft's yaw or pitch is more than 0x4000
 * off the target's. The hits it takes before breaking off are the model's
 * reaction_threshold, halved with the front shield below an eighth, or 1 at
 * system_damage_hull_threshold, both only for a craft with shields. While farther
 * than that distance and below that many hits it attacks: paiman_attacktarget,
 * throttle 0xC000 in rocket attack, and in attack a throttle by bearing,
 * distance and, from AI level 4, the target's speed; a craft with nonzero
 * craft_ordinal then pushes away from the wingman with the lowest craft_ordinal
 * below its own within a rough 1,000, and returns. Closer or hit enough, it
 * clears warheads_fired_this_maneuver and warhead_lock_ticks; unless a starfighter
 * in ai_flight.threat_obj_idx has hit it enough, it breaks off, setting
 * maneuver_phase 1: a random turn of 0x3000 to 0x6FFF either way, a random
 * pitch, full throttle, for 20 to 27 times SIMULATION_TICKS_PER_SECOND against
 * a freighter, starship or platform, else 2 to 5. Else it records that threat
 * as its attacker and starts a maneuver from the under-attack choices at full
 * throttle: a front and side choice when its yaw or pitch is 0x3000 or more off
 * the threat's, else a rear choice after firing a flare when it can. */
// FUNCTION: XVT 0x4A0620
int16_t paiman_attackmaneuver(void)
{
	g_cur_craft->push_accum_x = 0;
	g_cur_craft->push_accum_y = g_cur_craft->push_accum_x;
	g_cur_craft->push_accum_z = g_cur_craft->push_accum_y;
	switch (g_pai_context.controller->maneuver_phase) {
	case 1:
		if (g_pai_context.controller->maneuver_timer == 0) {
			return 1;
		}
		break;
	case 0: {

		pai_object_ref_direction_to_object_ref(
			g_pai_context.object_index,
			g_pai_context.controller->target_obj_idx);
		unsigned int target_distance = trig2_polardistance;
		int break_off_distance = 5120;
		if (g_active_region_craft_object_slot_end <=
		    g_pai_context.controller->target_obj_idx) {
			break_off_distance = 0x1400;
		} else {
			struct object_record *target =
				&g_object_table[g_pai_context.controller
							->target_obj_idx];
			if (target->genus_id == CRAFT_GENUS_STARSHIP ||
			    target->genus_id == CRAFT_GENUS_PLATFORM ||
			    target->genus_id == CRAFT_GENUS_FREIGHTER) {
				break_off_distance = 0x2000;
				if (target->object_type == 54) {
					break_off_distance = 0xA000;
				}
				if (target->object_type == 91) {
					break_off_distance += 0x8000;
				}
				if (target->object_type == 90) {
					break_off_distance += 0x8000;
				}
				uint16_t yaw_difference =
					(uint16_t)(trig2_xyangle - target->yaw);
				if (yaw_difference >= 0x8000) {
					yaw_difference =
						(uint16_t)-yaw_difference;
				}
				if (yaw_difference < 0x2800 ||
				    yaw_difference > 0x5800) {
					break_off_distance *= 2;
				}
			} else {
				uint8_t group_ai =
					g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.fg.group_ai;

				break_off_distance = group_ai == 5   ? 1536
						     : group_ai == 4 ? 2304
						     : group_ai == 3 ? 3072
						     : group_ai == 2 ? 4096
								     : 5120;
				struct object_record *own =
					&g_object_table[g_pai_context
								.object_index];
				uint16_t yaw_difference =
					(uint16_t)(own->yaw - target->yaw);
				if (yaw_difference >= 0x8000) {
					yaw_difference =
						(uint16_t)-yaw_difference;
				}
				uint16_t pitch_difference =
					(uint16_t)(own->pitch - target->pitch);
				if (pitch_difference >= 0x8000) {
					pitch_difference =
						(uint16_t)-pitch_difference;
				}
				const int max_angle = 0x4000;
				if (pitch_difference > max_angle ||
				    yaw_difference > max_angle) {
					break_off_distance *= 2;
				}
			}
		}

		uint16_t hits_before_break_off =
			g_model_defs[g_cur_craft->model_index]
				.reaction_threshold;
		if ((g_cur_craft->system_flags &
		     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
			if (craft_get_object_max_shield(
				    g_pai_context.object_index) /
				    8 >
			    g_cur_craft->shield_energy[0]) {
				hits_before_break_off >>= 1;
			}
			if (g_cur_craft->hull_damage >=
			    g_cur_craft->system_damage_hull_threshold) {
				hits_before_break_off = 1;
			}
		}
		const int full_throttle = UINT16_MAX;
		if (break_off_distance <= trig2_polardistance &&
		    g_cur_craft->ai_flight.hits_this_maneuver <
			    hits_before_break_off) {
			paiman_attacktarget(0);
			if (g_pai_context.controller->maneuver_mode ==
			    AI_MANEUVER_MODE_ATTACK) {
				uint16_t own_yaw =
					g_object_table[g_pai_context
							       .object_index]
						.yaw;
				uint16_t yaw_difference =
					(uint16_t)(own_yaw -
						   g_pai_context.controller
							   ->target_xy_angle);

				const int rear_angle = 0xD000;
				if (yaw_difference >= 0x3000 &&
				    yaw_difference <= rear_angle) {
					paiman_setpower(
						g_pai_context.object_index,
						43690);
				} else {
					uint16_t target_object_index =
						g_pai_context.controller
							->target_obj_idx;

					if (target_distance > 0x8000) {
						paiman_setpower(
							g_pai_context
								.object_index,
							full_throttle);
					} else {
						struct object_record *target =
							&g_object_table
								[target_object_index];

						if (target->mobj == NULL) {
							paiman_setpower(
								g_pai_context
									.object_index,
								0x8000);
						} else if (
							g_mission_flight_groups
								[g_pai_context
									 .craft_flight_group_index]
									.fg
									.group_ai >=
							4) {
							uint16_t target_yaw_difference =
								(uint16_t)(own_yaw -
									   target->yaw);

							if (target_yaw_difference >=
								    0x3000 &&
							    target_yaw_difference <=
								    rear_angle) {
								paiman_setpower(
									g_pai_context
										.object_index,
									full_throttle);
							} else {
								uint16_t speed =
									target->mobj
										->speed;

								const int attack_distance =
									0x4000;
								speed +=
									target_distance >
											attack_distance
										? 20
										: 0;
								uint16_t max_speed =
									g_cur_craft
										->ai_flight
										.max_speed_cache;

								if (speed >=
								    max_speed) {
									paiman_setpower(
										g_pai_context
											.object_index,
										full_throttle);
								} else {
									uint16_t throttle = math2_ratio_q16(
										speed,
										max_speed);

									paiman_setpower(
										g_pai_context
											.object_index,
										throttle);
								}
							}
						} else {
							paiman_setpower(
								g_pai_context
									.object_index,
								full_throttle);
						}
					}
				}
			} else {
				paiman_setpower(g_pai_context.object_index,
						0xC000);
			}
			if (g_cur_craft->craft_ordinal != 0) {
				int nearest_wingman = -1;
				uint8_t nearest_wave = UINT8_MAX;

				for (unsigned int object_index =
					     g_active_region_object_slot_start;
				     (unsigned int)
					     g_active_region_craft_object_slot_end >
				     object_index;
				     ++object_index) {
					if (g_pai_context.object_index !=
						    object_index &&
					    g_object_table[object_index]
							    .object_type != 0 &&
					    g_object_table[object_index]
							    .flight_group_idx ==
						    g_pai_context
							    .craft_flight_group_index) {
						struct craft_data *wingman =
							g_object_table
								[object_index]
									.mobj
									->p_craft;
						if (g_cur_craft->craft_ordinal >
						    wingman->craft_ordinal) {
							pai_object_ref_update_rough_distance(
								g_pai_context
									.object_index,
								object_index);
							if (g_last_rough_distance <
								    1000 &&
							    nearest_wave >
								    wingman->craft_ordinal) {
								nearest_wingman =
									object_index;
								nearest_wave =
									wingman->craft_ordinal;
							}
						}
					}
				}
				if (nearest_wingman != -1) {
					g_cur_craft->push_accum_x =
						g_object_table
							[g_pai_context
								 .object_index]
								.world_x -
						g_object_table[nearest_wingman]
							.world_x;
					g_cur_craft->push_accum_y =
						g_object_table
							[g_pai_context
								 .object_index]
								.world_y -
						g_object_table[nearest_wingman]
							.world_y;
					g_cur_craft->push_accum_z =
						2 *
						(g_object_table
							 [g_pai_context
								  .object_index]
								 .world_z -
						 g_object_table[nearest_wingman]
							 .world_z);
					return 0;
				}
			}
			break;
		}

		g_cur_craft->ai_flight.warheads_fired_this_maneuver = 0;
		g_cur_craft->warhead_lock_ticks = 0;
		if (g_cur_craft->ai_flight.hits_this_maneuver <
			    hits_before_break_off ||
		    g_cur_craft->ai_flight.threat_obj_idx == UINT16_MAX ||
		    g_object_table[g_cur_craft->ai_flight.threat_obj_idx]
				    .genus_id != 0) {
			int16_t yaw_offset =
				(int16_t)((game_rand() & 0x3FFF) + 12288);
			if ((uint16_t)game_rand() >= 0x8000) {
				yaw_offset = (int16_t)-yaw_offset;
			}
			g_pai_context.controller->target_xy_angle =
				(uint16_t)(g_object_table[g_pai_context
								  .object_index]
						   .yaw +
					   yaw_offset);
			paiman_setturn(
				(pai_get_effective_skill_value(g_cur_craft) >>
				 1) +
				0x8000);
			g_pai_context.controller->target_z_angle =
				(uint16_t)(game_rand() & 0x7FFF);
			g_cur_craft->ai_flight.climb_state = 0;
			g_cur_craft->ai_flight.dive_state = 0;
			if (g_pai_context.controller->target_z_angle <=
			    g_cur_craft->pitch) {
				g_cur_craft->ai_flight.pitch_state = 1;
			} else {
				g_cur_craft->ai_flight.pitch_state = 2;
			}
			g_cur_craft->ai_flight.pitch_through_loop = 0;
			g_cur_craft->ai_flight.pitch_step_scale = UINT16_MAX;
			paiman_setpower(g_pai_context.object_index,
					full_throttle);
			if (g_active_region_craft_object_slot_end <=
			    g_pai_context.controller->target_obj_idx) {
				g_pai_context.controller->maneuver_timer =
					SIMULATION_TICKS_PER_SECOND *
					((game_rand() & 3) + 2);
			} else if (g_object_table[g_pai_context.controller
							  ->target_obj_idx]
						   .genus_id ==
					   CRAFT_GENUS_STARSHIP ||
				   g_object_table[g_pai_context.controller
							  ->target_obj_idx]
						   .genus_id ==
					   CRAFT_GENUS_PLATFORM ||
				   g_object_table[g_pai_context.controller
							  ->target_obj_idx]
						   .genus_id ==
					   CRAFT_GENUS_FREIGHTER) {
				g_pai_context.controller->maneuver_timer =
					SIMULATION_TICKS_PER_SECOND *
					((game_rand() & 7) + 20);
			} else {
				g_pai_context.controller->maneuver_timer =
					SIMULATION_TICKS_PER_SECOND *
					((game_rand() & 3) + 2);
			}
			g_pai_context.controller->maneuver_phase = 1;
			XVT_LOG_DEBUG(
				"ai.attack_broke_off object=%d target=%d distance=%u limit=%d hits=%d max_hits=%d heading=%u pitch=%u timer=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.controller->target_obj_idx,
				target_distance, break_off_distance,
				(int)g_cur_craft->ai_flight.hits_this_maneuver,
				(int)hits_before_break_off,
				(unsigned)g_pai_context.controller
					->target_xy_angle,
				(unsigned)g_pai_context.controller
					->target_z_angle,
				g_pai_context.controller->maneuver_timer,
				g_flight_sim_side_effects_suppressed);
			return 0;
		}

		{
			struct object_record *threat =
				&g_object_table[g_cur_craft->ai_flight
							.threat_obj_idx];

			uint16_t yaw_difference =
				(uint16_t)(g_object_table[g_pai_context
								  .object_index]
						   .yaw -
					   threat->yaw);
			if (yaw_difference >= 0x8000) {
				yaw_difference = (uint16_t)-yaw_difference;
			}
			uint16_t pitch_difference =
				(uint16_t)(g_object_table[g_pai_context
								  .object_index]
						   .pitch -
					   threat->pitch);
			if (pitch_difference >= 0x8000) {
				pitch_difference = (uint16_t)-pitch_difference;
			}
			if (yaw_difference >= 0x3000 ||
			    pitch_difference >= 0x3000) {
				g_pai_context.controller->maneuver_mode =
					g_ai_under_attack_front_side_maneuver_choices
						[game_rand() & 3];
			} else {
				g_pai_context.controller->maneuver_mode =
					g_ai_under_attack_rear_maneuver_choices
						[game_rand() & 7];
				if (g_cur_craft->cm_type_id ==
					    COUNTERMEASURE_TYPE_FLARE &&
				    g_cur_craft->cm_ammo_count != 0 &&
				    g_cur_craft->cm_fire_cooldown_timer == 0) {
					laser_createcountermeasureprojectile(
						g_pai_context.object_index,
						155);
				}
			}
			g_cur_craft->last_attacker_obj_idx =
				g_cur_craft->ai_flight.threat_obj_idx;
			XVT_LOG_DEBUG(
				"ai.attack_abandoned object=%d target=%d threat=%d hits=%d max_hits=%d yaw_gap=%u pitch_gap=%u maneuver=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.controller->target_obj_idx,
				(int)g_cur_craft->ai_flight.threat_obj_idx,
				(int)g_cur_craft->ai_flight.hits_this_maneuver,
				(int)hits_before_break_off,
				(unsigned)yaw_difference,
				(unsigned)pitch_difference,
				(int)g_pai_context.controller->maneuver_mode,
				g_flight_sim_side_effects_suppressed);
			paiman_initmaneuver();
			paiman_setpower(g_pai_context.object_index,
					full_throttle);
		}
		return 0;
	}
	}
	return 0;
}

/* Starts zoom: full throttle, a roll that does not stop (roll_state 3) its way
 * set at random, a pitch at full step to 0x1001 to 0x2000 without a loop, no
 * climb or dive, for 3 to 6 times SIMULATION_TICKS_PER_SECOND. */
// FUNCTION: XVT 0x4A0E00
void paiman_initzoommaneuver(void)
{
	paiman_setpower(g_pai_context.object_index, 0xFFFF);
	g_cur_craft->ai_flight.roll_state = 3;
	g_cur_craft->ai_flight.roll_step = 0xFFFFu;
	g_pai_context.controller->target_roll = game_rand();
	g_pai_context.controller->target_z_angle =
		0x2000 - (game_rand() & 0x0FFF);
	g_pai_context.controller->maneuver_timer =
		SIMULATION_TICKS_PER_SECOND * ((game_rand() & 3) + 3);
	g_cur_craft->ai_flight.climb_state = 0;
	g_cur_craft->ai_flight.dive_state = 0;
	uint16_t pitch = g_cur_craft->pitch;
	if (g_pai_context.controller->target_z_angle <= pitch) {
		g_cur_craft->ai_flight.pitch_state = 1;
	} else {
		g_cur_craft->ai_flight.pitch_state = 2;
	}
	g_cur_craft->ai_flight.pitch_through_loop = 0;
	g_cur_craft->ai_flight.pitch_step_scale = 0xFFFFu;
}

/* Returns 1 once the maneuver timer has run out, else 0. */
// FUNCTION: XVT 0x4A0EF0
int16_t paiman_zoommaneuver(void)
{
	return g_pai_context.controller->maneuver_timer == 0;
}

/* Starts dive: full throttle, a pitch at full step to 0x5800 to 0x67FF without
 * a loop, no climb, for 1,180 ticks. */
// FUNCTION: XVT 0x4A0F00
void paiman_initdivemaneuver(void)
{
	paiman_setpower(g_pai_context.object_index, 0xFFFF);
	g_pai_context.controller->target_z_angle =
		(game_rand() & 0x0FFF) + 0x5800;
	g_cur_craft->ai_flight.climb_state = 0;
	if (g_pai_context.controller->target_z_angle <= g_cur_craft->pitch) {
		g_cur_craft->ai_flight.pitch_state = 1;
	} else {
		g_cur_craft->ai_flight.pitch_state = 2;
	}
	g_cur_craft->ai_flight.pitch_through_loop = 0;
	g_cur_craft->ai_flight.pitch_step_scale = 0xFFFFu;
	g_pai_context.controller->maneuver_timer = 1180;
}

/* Returns 1 once the maneuver timer has run out, else 0. */
// FUNCTION: XVT 0x4A0F90
int16_t paiman_divemaneuver(void)
{
	return g_pai_context.controller->maneuver_timer == 0;
}

/* Starts splits dive, also used for evasive: a roll to 0x8000 at full step, no
 * turn, and a pitch through a loop with pitch_state 2 to 0x4000 to 0x7FFF, its
 * step half the effective skill plus 0x8000. */
// FUNCTION: XVT 0x4A0FA0
void paiman_initsplitsdivemaneuver(void)
{
	g_cur_craft->ai_flight.roll_state = 1;
	g_cur_craft->ai_flight.roll_step = 0xFFFFu;
	g_pai_context.controller->target_roll = 0x8000u;
	g_cur_craft->ai_flight.turn_state = 0;
	g_cur_craft->ai_flight.pitch_through_loop = 1;
	g_pai_context.controller->target_z_angle =
		(game_rand() & 0x3FFF) + 0x4000;
	g_cur_craft->ai_flight.pitch_state = 2;
	g_cur_craft->ai_flight.pitch_step_scale =
		(pai_get_effective_skill_value(g_cur_craft) >> 1) + 0x8000;
}

/* Returns 1 once the roll and the pitch are done (roll_state 4, pitch_state 3),
 * else 0. */
// FUNCTION: XVT 0x4A1030
int16_t paiman_splitsdivemaneuver(void)
{
	return g_cur_craft->ai_flight.roll_state == 4 &&
	       g_cur_craft->ai_flight.pitch_state == 3;
}

/* Starts speed away: full throttle for 4,720 ticks, and the first weave from
 * paiman_setup_speed_away_turn, which replaces the target_xy_angle this function
 * sets just before. */
// FUNCTION: XVT 0x4A1060
void paiman_initspeedawaymaneuver(void)
{
	paiman_setpower(g_pai_context.object_index, 0xFFFF);
	g_pai_context.controller->maneuver_timer = 4720;
	uint16_t yaw = g_object_table[g_pai_context.object_index].yaw;
	uint16_t random_offset = game_rand();
	random_offset &= 0xFF;
	yaw += random_offset;
	g_pai_context.controller->target_xy_angle = yaw;
	paiman_setup_speed_away_turn(g_pai_context.object_index);
}

/* Returns 1 once the maneuver timer has run out, else 0. Each time
 * secondary_maneuver_timer runs out it starts the next weave with
 * paiman_setup_speed_away_turn, which replaces the negated target_xy_angle this
 * function sets just before. */
// FUNCTION: XVT 0x4A10D0
int16_t paiman_speedawaymaneuver(void)
{
	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		g_pai_context.controller->target_xy_angle =
			-g_pai_context.controller->target_xy_angle;
		paiman_setup_speed_away_turn(g_pai_context.object_index);
	}
	return g_pai_context.controller->maneuver_timer == 0;
}

/* Starts one weave of speed away: a Z push of 50 to 81 world units and a turn
 * of 384 to 639 angle units off the object's yaw, both negated when the craft's
 * Z push is 0 or more; turn step half the effective skill plus 0x8000; next
 * weave in 118 ticks. The push is held in 16 unsigned bits, so the negated one
 * is stored as 65,536 minus it, a large push up. */
// FUNCTION: XVT 0x4A1110
void paiman_setup_speed_away_turn(unsigned int object_idx)
{
	uint16_t push_z = (game_rand() & 0x1F) + 50;
	int16_t yaw_offset = game_rand() & 0xFF;
	yaw_offset += 384;
	if (g_cur_craft->push_accum_z >= 0) {
		push_z = -push_z;
		yaw_offset = -yaw_offset;
	}
	g_cur_craft->push_accum_z = push_z;
	g_pai_context.controller->target_xy_angle =
		g_object_table[object_idx].yaw + yaw_offset;
	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	unsigned int turn_step = effective_skill >> 1;
	turn_step += 0x8000;
	paiman_setturn(turn_step);
	g_pai_context.controller->secondary_maneuver_timer = 118;
}

/* Starts into hyperspace: steers at the aim point at full throttle, with
 * maneuver_phase 0. */
// FUNCTION: XVT 0x4A11B0
void paiman_initintohyperspacemaneuver(void)
{
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
	g_pai_context.controller->maneuver_phase = 0;
}

/* Takes the craft out through hyperspace; returns 0 on every path. At
 * maneuver_phase 0 it steers at the aim point at full throttle and, within
 * 0x4000 world units, marks the craft entering hyperspace, stops its roll,
 * pitch and turn, and moves to 1 with a maneuver timer of 1,652 ticks and a
 * second one of 944. At 1, once its speed reaches 3,600
 * (flight_update_craft_steering_and_speed speeds it up), it counts the departure in
 * g_mission_fg_stats and the team goals as its flags call for, emits message
 * 0x87, records the left-region outcome and removes the craft, and the object
 * it carries likewise. */
// FUNCTION: XVT 0x4A11E0
int16_t paiman_intohyperspacemaneuver(void)
{
	switch (g_pai_context.controller->maneuver_phase) {
	case 0:
		paiman_setflighttotarget(0, 1);
		if (trig2_polardistance < 0x4000) {
			g_cur_craft->object_kind =
				CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
			g_cur_craft->ai_flight.roll_state = 0;
			g_cur_craft->ai_flight.pitch_state = 0;
			g_cur_craft->ai_flight.turn_state = 0;
			g_pai_context.controller->maneuver_phase = 1;
			g_pai_context.controller->secondary_maneuver_timer =
				944;
			g_pai_context.controller->maneuver_timer = 1652;
			XVT_LOG_DEBUG(
				"ai.hyperspace_entering object=%d fg=%d distance=%d plan=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				trig2_polardistance,
				(int)g_pai_context.controller->running_plan_id,
				g_flight_sim_side_effects_suppressed);
		}
		paiman_setpower(g_pai_context.object_index, 0xFFFF);
		return 0;
	case 1:
		break;
	default:
		return 0;
	}

	g_cur_craft->object_kind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
	if (g_object_table[g_pai_context.object_index].mobj->speed >= 0xE10) {
		uint16_t flight_group_idx;
		int special_cargo;
		if (g_cur_craft->captured_by_flight_group == 0 &&
		    g_pai_context.controller->skipped_to_order4 == 0 &&
		    (g_cur_craft->ai_flight.go_home_flag != 0 ||
		     (g_cur_craft->ai_flight.mission_aborted_flag == 0 &&
		      g_cur_craft->ai_flight.depart_timer_flag == 0))) {
			flight_group_idx =
				g_pai_context.craft_flight_group_index;
			++g_mission_fg_stats[flight_group_idx]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_DEPARTED];
			special_cargo = 0;
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft ==
			    g_cur_craft->craft_ordinal) {
				special_cargo = 1;
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DEPARTED] =
					1;
			}
			mission_apply_team_goal_score_all_enabled_teams(
				12, g_pai_context.craft_flight_group_index,
				special_cargo);
		}
		if (g_cur_craft->captured_by_flight_group == 0 &&
		    g_pai_context.controller->skipped_to_order4 == 1 &&
		    g_cur_craft->ai_flight.mission_aborted_flag == 0 &&
		    g_cur_craft->ai_flight.depart_timer_flag == 0) {
			flight_group_idx =
				g_pai_context.craft_flight_group_index;
			++g_mission_fg_stats[flight_group_idx].outcome_count
				  [FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE];
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft ==
			    g_cur_craft->craft_ordinal) {
				g_mission_fg_stats[flight_group_idx].special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE] =
					1;
			}
		}
		unsigned int team;
		unsigned int other_team;
		if (g_cur_craft->captured_by_flight_group != 0) {
			team = g_object_table[g_pai_context.object_index]
				       .mobj->team;
			++g_mission_fg_stats[g_pai_context
						     .craft_flight_group_index]
				  .team_captured_departed_count[team];
			special_cargo = 0;
			if (g_mission_flight_groups
				    [g_pai_context.craft_flight_group_index]
					    .fg.special_cargo_craft ==
			    g_cur_craft->craft_ordinal) {
				++g_mission_fg_stats
					  [g_pai_context
						   .craft_flight_group_index]
						  .team_special_cargo_captured_departed
							  [team];
				special_cargo = 1;
			}
			mission_apply_team_goal_score_for_team(
				44, g_pai_context.craft_flight_group_index,
				special_cargo, (uint8_t)team);
			for (other_team = 0; other_team < 10; ++other_team) {
				if (other_team != team &&
				    g_mission_flight_groups
						    [g_pai_context
							     .craft_flight_group_index]
							    .fg.team !=
					    other_team) {
					++g_mission_fg_stats
						  [g_pai_context
							   .craft_flight_group_index]
							  .team_uncaptured_lost
								  [other_team];
					if (g_mission_flight_groups
						    [g_pai_context
							     .craft_flight_group_index]
							    .fg
							    .special_cargo_craft ==
					    g_cur_craft->craft_ordinal) {
						g_mission_fg_stats[g_pai_context
									   .craft_flight_group_index]
							.team_special_cargo_uncaptured_lost
								[other_team] =
							1;
					}
				}
			}
		}
		XVT_LOG_DEBUG(
			"ai.departure_counted object=%d fg=%d departed=%u incomplete=%u home=%d aborted=%d departing=%d skipped=%d captured=%d team_departed=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.craft_flight_group_index,
			(unsigned)g_mission_fg_stats
				[g_pai_context.craft_flight_group_index]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_DEPARTED],
			(unsigned)g_mission_fg_stats[g_pai_context
							     .craft_flight_group_index]
				.outcome_count
					[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE],
			(int)g_cur_craft->ai_flight.go_home_flag,
			(int)g_cur_craft->ai_flight.mission_aborted_flag,
			(int)g_cur_craft->ai_flight.depart_timer_flag,
			(int)g_pai_context.controller->skipped_to_order4,
			(int)g_cur_craft->captured_by_flight_group,
			(int)g_mission_fg_stats
				[g_pai_context.craft_flight_group_index]
					.team_captured_departed_count
						[g_object_table
							 [g_pai_context
								  .object_index]
								 .mobj->team],
			g_flight_sim_side_effects_suppressed);
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"ai.hyperspace_departed object=%d fg=%d craft=%d captured=%d carried=%d tick=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)g_object_table[g_pai_context.object_index]
					.object_type,
				g_cur_craft->captured_by_flight_group != 0,
				g_cur_craft->carried_object_index == UINT16_MAX
					? -1
					: (int)g_cur_craft
						  ->carried_object_index,
				g_game_time);
		}
		msg_emit_craft_message(g_pai_context.object_index, g_cur_craft,
				       0x87);
		mission_record_craft_outcome(
			g_pai_context.object_index,
			g_pai_context.craft_flight_group_index,
			FLIGHT_GROUP_OUTCOME_LEFT_REGION);
		g_object_table[g_pai_context.object_index].object_type = 0;
		craft_free_linked_objects(g_cur_craft);
		{
			uint16_t carried_object_idx =
				g_cur_craft->carried_object_index;

			if (carried_object_idx != UINT16_MAX) {
				if (g_object_table[carried_object_idx].mobj !=
				    NULL) {
					uint16_t carried_group_idx =
						g_object_table[carried_object_idx]
							.flight_group_idx;
					mission_record_craft_outcome(
						carried_object_idx,
						carried_group_idx,
						FLIGHT_GROUP_OUTCOME_LEFT_REGION);
					struct craft_data *carried_craft =
						g_object_table
							[carried_object_idx]
								.mobj->p_craft;
					if (carried_craft
						    ->captured_by_flight_group !=
					    0) {
						team = g_object_table
							       [carried_object_idx]
								       .mobj
								       ->team;
						++g_mission_fg_stats
							  [carried_group_idx]
								  .team_captured_departed_count
									  [team];
						special_cargo = 0;
						if (g_mission_flight_groups
							    [carried_group_idx]
								    .fg
								    .special_cargo_craft ==
						    carried_craft
							    ->craft_ordinal) {
							++g_mission_fg_stats[carried_group_idx]
								  .team_special_cargo_captured_departed
									  [team];
							special_cargo = 1;
						}
						mission_apply_team_goal_score_for_team(
							44, carried_group_idx,
							special_cargo,
							(uint8_t)team);
						uint8_t special_cargo_craft;
						for (other_team = 0;
						     other_team < 10;
						     ++other_team) {
							if (other_team !=
								    team &&
							    g_mission_flight_groups[carried_group_idx]
									    .fg
									    .team !=
								    other_team) {
								uint8_t other_team_count =
									g_mission_fg_stats[carried_group_idx]
										.team_uncaptured_lost
											[other_team];
								special_cargo_craft =
									g_mission_flight_groups
										[carried_group_idx]
											.fg
											.special_cargo_craft;
								++other_team_count;
								g_mission_fg_stats[carried_group_idx]
									.team_uncaptured_lost
										[other_team] =
									other_team_count;
								if (special_cargo_craft ==
								    carried_craft
									    ->craft_ordinal) {
									g_mission_fg_stats[carried_group_idx]
										.team_special_cargo_uncaptured_lost
											[other_team] =
										1;
								}
							}
						}
						special_cargo_craft =
							g_mission_flight_groups
								[carried_group_idx]
									.fg
									.special_cargo_craft;
						++g_mission_fg_stats[carried_group_idx]
							  .outcome_count
								  [FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION];
						if (special_cargo_craft ==
						    carried_craft
							    ->craft_ordinal) {
							g_mission_fg_stats[carried_group_idx]
								.special_cargo_outcome
									[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION] =
								1;
						}
					} else {
						++g_mission_fg_stats[carried_group_idx]
							  .outcome_count
								  [FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION];
						special_cargo = 0;
						if (g_mission_flight_groups
							    [carried_group_idx]
								    .fg
								    .special_cargo_craft ==
						    carried_craft
							    ->craft_ordinal) {
							special_cargo = 1;
							g_mission_fg_stats[carried_group_idx]
								.special_cargo_outcome
									[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION] =
								1;
						}
						mission_apply_team_goal_score_all_enabled_teams(
							46, carried_group_idx,
							special_cargo);
					}
					XVT_LOG_DEBUG(
						"ai.cargo_departed object=%d carried=%d fg=%d captured=%d delivered=%u undelivered=%u predicted=%d",
						(int)g_pai_context.object_index,
						(int)carried_object_idx,
						(int)carried_group_idx,
						(int)carried_craft
							->captured_by_flight_group,
						(unsigned)g_mission_fg_stats
							[carried_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION],
						(unsigned)g_mission_fg_stats
							[carried_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION],
						g_flight_sim_side_effects_suppressed);
					g_object_table[carried_object_idx]
						.object_type = 0;
					craft_free_linked_objects(
						carried_craft);
				}
			}
		}
	}
	return 0;
}

/* Starts the arrival from hyperspace: marks the craft arriving, sets its speed
 * to 3,600 and maneuver_phase 0, targets the flight group's current mission
 * point with the aim point there, and sets a maneuver timer of 2,596 ticks and
 * a secondary one of SIMULATION_TICKS_PER_SECOND. Keeps think_interval in
 * docked_target_signatures[0] and thinks every 59 ticks meanwhile. */
// FUNCTION: XVT 0x4A1750
void paiman_initoutofhyperspacemaneuver(void)
{
	g_cur_craft->object_kind = CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE;
	g_object_table[g_pai_context.object_index].mobj->speed = 3600;
	g_pai_context.controller->maneuver_phase = 0;
	g_pai_context.controller->secondary_maneuver_timer =
		SIMULATION_TICKS_PER_SECOND;
	g_pai_context.controller->target_obj_idx = 0x8000;
	g_pai_context.controller->target_signature = 0;
	g_pai_context.controller->has_live_target = 0;
	pai_update_aim_point_from_order_target();
	g_pai_context.controller->maneuver_timer = 2596;
	/* While the craft arrives from hyperspace, the first slot of the
	 * docked-target signature list keeps its think interval; the arrival
	 * maneuver puts it back when it ends. */
	g_cur_craft->ai_flight.docked_target_signatures[0] =
		(uint16_t)g_pai_context.controller->think_interval;
	g_pai_context.controller->think_interval = 59;
}

/* Slows an arriving craft and returns 1 when it has arrived, else 0. Each time
 * secondary_maneuver_timer runs out it raises maneuver_phase by 1, up to 10, and
 * sets the speed from g_ai_hyperspace_arrival_speed_by_phase. A craft with no leader
 * has arrived within 4,096 world units of its aim point or when the maneuver
 * timer runs out; a follower, once its leader no longer runs
 * outofhyperspacepln. On arrival it writes the plan id of the craft's first
 * order, leader or follower, into byte 3 of the shared outofhyperspacepln plan
 * data, the plan its first order switches to; restores think_interval; marks the
 * craft active; clears its target; and sets its speed to 0 on nullpln or a
 * stationary plan, else 250. */
// FUNCTION: XVT 0x4A17F0
int16_t paiman_outofhyperspacemaneuver(void)
{
	struct ai_controller *leader_controller =
		&g_pai_context.leader_or_self_craft->ai_controller;
	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		++g_pai_context.controller->maneuver_phase;
		if (g_pai_context.controller->maneuver_phase > 10) {
			g_pai_context.controller->maneuver_phase = 10;
		}
		g_object_table[g_pai_context.object_index].mobj->speed =
			g_ai_hyperspace_arrival_speed_by_phase
				[g_pai_context.controller->maneuver_phase];
		g_pai_context.controller->secondary_maneuver_timer =
			SIMULATION_TICKS_PER_SECOND;
	}

	uint8_t reached = 0;
	if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
		trig2_ctop(g_pai_context.controller->aim_point_x -
				   g_object_table[g_pai_context.object_index]
					   .world_x,
			   g_pai_context.controller->aim_point_y -
				   g_object_table[g_pai_context.object_index]
					   .world_y,
			   g_pai_context.controller->aim_point_z -
				   g_object_table[g_pai_context.object_index]
					   .world_z);
		if (trig2_polardistance < 4096 ||
		    g_pai_context.controller->maneuver_timer == 0) {
			reached = 1;
		}
	} else if (strcmp(g_plan_table[leader_controller->running_plan_id].name,
			  "outofhyperspacepln") != 0) {
		reached = 1;
	}

	if (reached != 0) {
		uint16_t order =
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[0]
					.order;
		uint8_t plan_id;
		if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
			plan_id = g_builtin_plan_id_by_name_index
				[g_order_leader_builtin_plan_name_index[order]];
		} else {
			plan_id = g_builtin_plan_id_by_name_index
				[g_order_follower_builtin_plan_name_index
					 [order]];
		}

		uint8_t *out_of_hyperspace_plan =
			pai_getplandataptrbyname("outofhyperspacepln");
		out_of_hyperspace_plan[3] = plan_id;
		g_pai_context.controller->think_interval =
			g_cur_craft->ai_flight.docked_target_signatures[0];
		g_cur_craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
		g_pai_context.controller->target_obj_idx = UINT16_MAX;
		g_pai_context.controller->target_signature = 0;
		g_pai_context.controller->has_live_target = 0;

		if (strcmp(g_plan_table[plan_id].name, "nullpln") == 0 ||
		    strcmp(g_plan_table[plan_id].name, "stationaryldrpln") ==
			    0 ||
		    strcmp(g_plan_table[plan_id].name, "stationaryflwpln") ==
			    0) {
			g_object_table[g_pai_context.object_index].mobj->speed =
				0;
			XVT_LOG_DEBUG(
				"ai.hyperspace_arrived object=%d fg=%d leader=%d plan=%d think=%d speed=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				g_cur_craft->leader_obj_idx == UINT8_MAX
					? -1
					: (int)g_cur_craft->leader_obj_idx,
				(int)plan_id,
				g_pai_context.controller->think_interval,
				(int)g_object_table[g_pai_context.object_index]
					.mobj->speed,
				g_flight_sim_side_effects_suppressed);
			return 1;
		}

		g_object_table[g_pai_context.object_index].mobj->speed = 250;
		XVT_LOG_DEBUG(
			"ai.hyperspace_arrived object=%d fg=%d leader=%d plan=%d think=%d speed=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)g_pai_context.craft_flight_group_index,
			g_cur_craft->leader_obj_idx == UINT8_MAX
				? -1
				: (int)g_cur_craft->leader_obj_idx,
			(int)plan_id, g_pai_context.controller->think_interval,
			(int)g_object_table[g_pai_context.object_index]
				.mobj->speed,
			g_flight_sim_side_effects_suppressed);
		return 1;
	}

	return 0;
}

/* Does nothing. */
// FUNCTION: XVT 0x4A1A40
void paiman_initescortmaneuver(void) {}

/* Keeps the craft at its escort station; returns 0 on every path. The escorted
 * craft is the first in the active region's craft slots of flight group
 * escort_target_fg with no leader; one found in slot 255 counts as none. With
 * none, it steers at the aim point at half throttle. Farther than 0x8000 from
 * the escorted craft (0x20000 when its type has a max_bounds_extent of 3,000 or
 * more) or with the escorted craft disabled, it flies at it, a quarter turn off
 * when disabled, at full throttle beyond 0x10000 else 0x4000, with no push.
 * Otherwise it matches the escorted craft's heading, pitch and roll as
 * paiman_followleadermaneuver does, and its speed by moving the throttle 50 per
 * unit of speed difference, and pushes toward the station
 * g_aiEscortStationOffset*ByVariable[variable1] names, times 16 for a large
 * escorted craft. The push is cleared instead when object 255 is a player's
 * craft at speed 10 or less: it reads the escorted craft's leader index, which
 * is always 255. Does not check variable1 below 28. Sets the trig2_ globals. */
// FUNCTION: XVT 0x4A1A50
int16_t paiman_escortmaneuver(void)
{
	uint16_t escort_target_fg = g_pai_context.controller->escort_target_fg;
	uint16_t object_index = g_pai_context.object_index;

	uint16_t target_idx = UINT8_MAX;
	for (uint16_t object_idx = (uint16_t)g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type != 0) {
			struct craft_data *candidate_craft =
				g_object_table[object_idx].mobj->p_craft;
			if (g_object_table[object_idx].flight_group_idx ==
				    escort_target_fg &&
			    candidate_craft->leader_obj_idx == UINT8_MAX) {
				target_idx = object_idx;
				break;
			}
		}
	}
	if (target_idx != UINT8_MAX) {
		pai_object_ref_direction_to_object_ref(target_idx,
						       object_index);
		struct craft_data *target_craft =
			g_object_table[target_idx].mobj->p_craft;
		struct ai_controller *target_controller =
			&target_craft->ai_controller;
		unsigned int escort_range =
			g_object_type_table[g_object_table[target_idx]
						    .object_type]
						.max_bounds_extent < 3000
				? 0x8000
				: 0x20000;
		if ((unsigned int)trig2_polardistance > escort_range ||
		    target_craft->working_subsystems == 0) {
			g_pai_context.controller->aim_point_x =
				g_object_table[target_idx].world_x;
			g_pai_context.controller->aim_point_y =
				g_object_table[target_idx].world_y;
			g_pai_context.controller->aim_point_z =
				g_object_table[target_idx].world_z;
			if (target_craft->working_subsystems == 0) {
				paiman_setflighttotarget(0x4000, 1);
			} else {
				paiman_setflighttotarget(0, 1);
			}
			if (trig2_polardistance > 0x10000) {
				paiman_setpower(object_index, UINT16_MAX);
			} else {
				paiman_setpower(object_index, 0x4000);
			}
			g_cur_craft->push_accum_x = 0;
			g_cur_craft->push_accum_y = 0;
			g_cur_craft->push_accum_z = 0;
			return 0;
		}
		if (target_craft->ai_flight.turn_state == 2) {
			g_pai_context.controller->target_xy_angle =
				target_controller->target_xy_angle;
			paiman_setturn(
				(pai_get_effective_skill_value(g_cur_craft) >>
				 3) +
				0x4000);
		} else if (g_object_table[object_index].yaw !=
			   g_object_table[target_idx].yaw) {
			g_pai_context.controller->target_xy_angle =
				g_object_table[target_idx].yaw;
			paiman_setturn(
				(pai_get_effective_skill_value(g_cur_craft) >>
				 3) +
				0x4000);
		}
		if (g_object_table[target_idx].mobj->speed >
		    g_object_table[object_index].mobj->speed) {
			uint16_t throttle = g_cur_craft->throttle_speed;
			paiman_setpower(
				object_index,
				throttle +
					(uint16_t)(50 *
						   (g_object_table[target_idx]
							    .mobj->speed -
						    g_object_table[object_index]
							    .mobj->speed)));
			if (g_cur_craft->throttle_speed < throttle) {
				paiman_setpower(object_index, UINT16_MAX);
			}
		} else if (g_object_table[target_idx].mobj->speed <
			   g_object_table[object_index].mobj->speed) {
			uint16_t throttle = g_cur_craft->throttle_speed;
			paiman_setpower(
				object_index,
				throttle -
					(uint16_t)(50 *
						   (g_object_table[object_index]
							    .mobj->speed -
						    g_object_table[target_idx]
							    .mobj->speed)));
			if (g_cur_craft->throttle_speed > throttle) {
				paiman_setpower(object_index, 0);
			}
		}
		{
			uint16_t pitch = target_craft->pitch;
			uint16_t pitch_difference = g_cur_craft->pitch - pitch;
			if (pitch_difference >= 0x8000u) {
				pitch_difference = -pitch_difference;
			}
			if (pitch_difference < 0x400u) {
				g_cur_craft->pitch = pitch;
				g_cur_craft->ai_flight.pitch_state = 0;
			} else {
				g_pai_context.controller->target_z_angle =
					pitch;
				if (g_cur_craft->pitch >=
				    g_pai_context.controller->target_z_angle) {
					g_cur_craft->ai_flight.pitch_state = 1;
				} else {
					g_cur_craft->ai_flight.pitch_state = 2;
				}
				g_cur_craft->ai_flight.pitch_step_scale =
					UINT16_MAX;
				g_cur_craft->ai_flight.pitch_through_loop = 0;
			}
		}
		{
			uint16_t roll = g_object_table[target_idx].roll;
			uint16_t roll_difference =
				g_object_table[object_index].roll - roll;
			if (roll_difference >= 0x8000u) {
				roll_difference = -roll_difference;
			}
			if (roll_difference < 0x400u) {
				g_object_table[object_index].roll = roll;
				g_object_table[object_index]
					.mobj->orient_matrix_dirty = 1;
				g_cur_craft->ai_flight.roll_state = 0;
			} else {
				g_pai_context.controller->target_roll = roll;
				g_cur_craft->ai_flight.roll_step = UINT16_MAX;
				g_cur_craft->ai_flight.roll_state = 1;
			}
		}
		int variable1 =
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[g_pai_context.order_slot]
					.variable1;
		if (variable1 >= 28) {
			XVT_LOG_DEBUG(
				"ai.escort_station_invalid object=%d fg=%d order=%d station=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)g_pai_context.order_slot, variable1,
				g_flight_sim_side_effects_suppressed);
		}
		pai_calcrotatedpoint(
			&g_object_table[target_idx],
			g_ai_escort_station_offset_x_by_variable[variable1],
			g_ai_escort_station_offset_y_by_variable[variable1],
			g_ai_escort_station_offset_z_by_variable[variable1]);
		if (g_object_type_table[g_object_table[target_idx].object_type]
			    .max_bounds_extent >= 3000) {
			g_rotated_x *= 16;
			g_rotated_y *= 16;
			g_rotated_z *= 16;
		}
		{
			uint16_t leader_idx =
				(uint8_t)target_craft->leader_obj_idx;
			if (g_object_table[leader_idx].player_owner_idx == -1 ||
			    g_object_table[leader_idx].mobj->speed > 10) {
				g_cur_craft->push_accum_x =
					g_object_table[target_idx].world_x -
					g_object_table[g_pai_context
							       .object_index]
						.world_x +
					g_rotated_x;
				g_cur_craft->push_accum_y =
					g_object_table[target_idx].world_y -
					g_object_table[g_pai_context
							       .object_index]
						.world_y +
					g_rotated_y;
				g_cur_craft->push_accum_z =
					g_object_table[target_idx].world_z -
					g_object_table[g_pai_context
							       .object_index]
						.world_z +
					g_rotated_z;
			} else {
				g_cur_craft->push_accum_x = 0;
				g_cur_craft->push_accum_y = 0;
				g_cur_craft->push_accum_z = 0;
			}
		}
	} else {
		paiman_setflighttotarget(0, 1);
		paiman_setpower(g_pai_context.object_index, 0x8000);
	}
	return 0;
}

/* Starts boarding at maneuver_phase 0. */
// FUNCTION: XVT 0x4A2010
void paiman_initboardmaneuver(void)
{
	g_pai_context.controller->maneuver_phase = 0;
}

/* Boards target_obj_idx, an object or a mission point, in four stages kept in
 * maneuver_phase; returns 1 when the last stage times out, after clearing the
 * target, else 0. Stage 0 flies to an approach point by the target's docking
 * point, worked out from both models' dock offsets, or 2,048 higher in Z for a
 * mission point: half throttle beyond 0x2000 (the full throttle it sets beyond
 * 0x4000 is replaced at once), a quarter beyond 2,048. There, while a player's
 * target moves, it waits, and when that player is the local one it asks, with a
 * warning sound, for the throttle to be set to 0 whenever that message is not
 * already queued; then throttle 0 and stage 1. Stage 1 pushes the craft onto
 * the docking point and turns it to the target's orientation at half step;
 * within 16 world units it moves to stage 2 for 1,180 ticks times the order's
 * variable1, counts its group's FLIGHT_GROUP_OUTCOME_FAILED_MISSION and the
 * target group's FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION once each, emits "has
 * docked with" and the voice lines. In stage 2, on boardtogivepln with a
 * player's target, it reloads the target one round per slot and repairs one
 * system every 472 ticks, keeping the timer at 1,416 while it does. When the
 * timer runs out it does the plan's work: give, take or swap the special cargo
 * name, capture (the target joins its team and flies home or stays), destroy
 * (the target starts selfdestroypln), pick up (it carries the target), contact,
 * or repair. It then counts the target inspected by its team when their IFF
 * match, adds 1 to goal_progress, records the target's signature in its docked
 * list, counts the docked and boarded outcomes, and goes to stage 3 for 2,360
 * ticks. Stage 3 pushes the craft to a point 0x4000 along its own up axis from
 * the target, or, for a mission point, 500 in Z. */
// FUNCTION: XVT 0x4A2020
int16_t paiman_boardmaneuver(void)
{
	enum {
		BOARD_PHASE_APPROACH = 0,
		BOARD_PHASE_ALIGN = 1,
		BOARD_PHASE_TRANSFER = 2,
		BOARD_PHASE_SEPARATE = 3,
		DOCKING_SMALL_CRAFT_GENUS_LIMIT = 2,
		DOCKING_UP_FALLBACK = 0x7000,
		MISSION_POINT_APPROACH_Z_OFFSET = 2048,
		MISSION_POINT_DOCK_Z_OFFSET = 128,
		APPROACH_FULL_POWER_DISTANCE = 0x4000,
		APPROACH_HALF_POWER_DISTANCE = 0x2000,
		APPROACH_DOCK_DISTANCE = 2048,
		THROTTLE_QUARTER = 0x4000,
		THROTTLE_HALF = 0x8000,
		READY_MESSAGE_SLOT_COUNT = 10,
		ALIGNMENT_STEP = 0x8000,
		DOCKING_ALIGNMENT_DISTANCE = 16,
		DOCKING_DURATION_PER_ORDER_UNIT = 1180,
		RELOAD_STEP_TICKS = 472,
		RELOAD_CONTINUE_DURATION = 1416,
		SEPARATION_DURATION = 2360,
		SEPARATION_MISSION_POINT_PUSH = 500,
		MINIMUM_VOICE_ORDER_TIME = 2,
		SPECIAL_CARGO_NAME_LENGTH = 16,
		TEAM_COUNT = 10,
		HUD_FEATURE_COUNT = 13,
		MAX_OBJECT_SIGNATURE_COUNT = 10,
		WARHEAD_LAUNCHER_SECONDARY = 1,
		SECONDARY_WARHEAD_TYPE = 5,
		MINIMUM_WARHEAD_COUNT = 1,
		MAXIMUM_WARHEAD_COUNT = 99,
		FULL_WEAPON_CHARGE = 127,
		SYSTEM_HEALTH_FULL = 100,
		CAPTURE_OWNER_FLAG = 0x80,
		SELF_DESTRUCT_RANDOM_MASK = 0x0F,
		SELF_DESTRUCT_RANDOM_BASE = 15,
		HUD_TARGET_INVALIDATED = -3,
	};

	uint16_t target_object_index = g_pai_context.controller->target_obj_idx;
	uint16_t variable1 =
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.variable1;
	struct mobile_object *target_mobile_object =
		g_object_table[target_object_index].mobj;
	uint8_t *target_flight_group_index_ptr =
		&g_object_table[target_object_index].flight_group_idx;
	struct craft_data *target_craft = NULL;
	struct ai_controller *target_controller = NULL;
	uint16_t target_model_index = UINT8_MAX;
	uint16_t target_flight_group_index = *target_flight_group_index_ptr;
	uint16_t target_signature =
		g_object_table[target_object_index].object_signature;

	if (target_mobile_object != NULL) {
		target_craft = target_mobile_object->p_craft;
		target_controller = &target_craft->ai_controller;
		target_model_index = target_craft->model_index;
	}

	switch (g_pai_context.controller->maneuver_phase) {
	case BOARD_PHASE_APPROACH: {
		if (target_mobile_object != NULL) {
			int16_t initial_up_offset;

			if (g_object_table[target_object_index].genus_id <
			    DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
				initial_up_offset =
					g_model_defs[target_model_index]
						.dock_from_up[0] -
					g_model_defs[g_cur_craft->model_index]
						.dock_to_up[0];
			} else if (g_object_table[g_pai_context.object_index]
					   .genus_id <
				   DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
				initial_up_offset =
					g_model_defs[target_model_index]
						.dock_from_up[1] +
					g_model_defs[target_model_index]
						.dock_from_up[0] -
					g_model_defs[g_cur_craft->model_index]
						.dock_to_up[1];
			} else {
				initial_up_offset =
					2 * g_model_defs[target_model_index]
							.dock_from_up[1] -
					g_model_defs[g_cur_craft->model_index]
						.dock_to_up[1];
			}
			int16_t additional_up_offset =
				g_model_defs[target_model_index]
					.dock_from_up[1] -
				g_model_defs[g_cur_craft->model_index]
					.dock_to_up[1];
			int16_t approach_up_offset = additional_up_offset +
						     additional_up_offset +
						     initial_up_offset;
			if (approach_up_offset < 0) {
				approach_up_offset = DOCKING_UP_FALLBACK;
			}
			pai_calcrotatedpoint(
				&g_object_table[target_object_index], 0,
				approach_up_offset,
				g_model_defs[target_model_index].dock_forward);
			g_pai_context.controller->aim_point_x =
				g_rotated_x +
				g_object_table[target_object_index].world_x;
			g_pai_context.controller->aim_point_y =
				g_rotated_y +
				g_object_table[target_object_index].world_y;
			g_pai_context.controller->aim_point_z =
				g_rotated_z +
				g_object_table[target_object_index].world_z;
		} else {
			mission_resolve_object_or_mission_point_world_loc(
				target_object_index, 0);
			g_pai_context.controller->aim_point_x = g_world_loc_x;
			g_pai_context.controller->aim_point_y = g_world_loc_y;
			g_pai_context.controller->aim_point_z =
				g_world_loc_z + MISSION_POINT_APPROACH_Z_OFFSET;
		}

		paiman_setflighttotarget(0, 1);
		if (trig2_polardistance > APPROACH_FULL_POWER_DISTANCE) {
			paiman_setpower(g_pai_context.object_index, UINT16_MAX);
		}
		if (trig2_polardistance > APPROACH_HALF_POWER_DISTANCE) {
			paiman_setpower(g_pai_context.object_index,
					THROTTLE_HALF);
			return 0;
		}
		if (trig2_polardistance > APPROACH_DOCK_DISTANCE) {
			paiman_setpower(g_pai_context.object_index,
					THROTTLE_QUARTER);
			return 0;
		}
		{
			int target_player_index =
				g_object_table[target_object_index]
					.player_owner_idx;

			if (target_player_index != -1 &&
			    g_object_table[target_object_index].mobj != NULL &&
			    g_object_table[target_object_index].mobj->speed !=
				    0) {
				if (target_player_index != g_local_player) {
					return 0;
				}
				unsigned int message_slot;
				for (message_slot = 0;
				     message_slot < READY_MESSAGE_SLOT_COUNT;
				     ++message_slot) {
					if (g_ready_message_pane_queue
						    [message_slot]
							    .state_or_message_id ==
					    IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU) {
						break;
					}
				}
				if (message_slot < READY_MESSAGE_SLOT_COUNT) {
					return 0;
				}
				g_msg_sender_iff =
					g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.fg.iff;
				msg_emit_in_flight_message(
					IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU,
					g_local_player);
				fsfx_play_sound(FLIGHT_SOUND_GENERAL_WARNING,
						-1, g_local_player);
				XVT_LOG_DEBUG(
					"ai.board_asked_stop object=%d target=%d slot=%d predicted=%d",
					(int)g_pai_context.object_index,
					(int)target_object_index,
					g_local_player,
					g_flight_sim_side_effects_suppressed);
				return 0;
			}
		}
		paiman_setpower(g_pai_context.object_index, 0);
		g_pai_context.controller->maneuver_phase = BOARD_PHASE_ALIGN;
		XVT_LOG_DEBUG(
			"ai.board_approached object=%d target=%d plan=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)target_object_index,
			(int)g_pai_context.controller->current_plan_id,
			g_flight_sim_side_effects_suppressed);
		return 0;
	}

	case BOARD_PHASE_ALIGN: {
		int push_x;
		int push_y;
		int push_z;
		uint16_t target_roll;
		uint16_t target_yaw;
		uint16_t target_pitch;

		if (target_mobile_object != NULL) {
			int16_t docking_up_offset;

			if (g_object_table[target_object_index].genus_id <
			    DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
				docking_up_offset =
					g_model_defs[target_model_index]
						.dock_from_up[0] -
					g_model_defs[g_cur_craft->model_index]
						.dock_to_up[0];
			} else if (g_object_table[g_pai_context.object_index]
					   .genus_id <
				   DOCKING_SMALL_CRAFT_GENUS_LIMIT) {
				docking_up_offset =
					g_model_defs[target_model_index]
						.dock_from_up[0] -
					g_model_defs[g_cur_craft->model_index]
						.dock_to_up[1];
			} else {
				docking_up_offset =
					g_model_defs[target_model_index]
						.dock_from_up[1] -
					g_model_defs[g_cur_craft->model_index]
						.dock_to_up[1];
			}
			pai_calcrotatedpoint(
				&g_object_table[target_object_index], 0,
				docking_up_offset,
				g_model_defs[target_model_index].dock_forward);
			g_cur_craft->push_accum_x =
				g_rotated_x +
				g_object_table[target_object_index].world_x -
				g_object_table[g_pai_context.object_index]
					.world_x;
			push_x = g_cur_craft->push_accum_x;
			g_cur_craft->push_accum_y =
				g_rotated_y +
				g_object_table[target_object_index].world_y -
				g_object_table[g_pai_context.object_index]
					.world_y;
			push_y = g_cur_craft->push_accum_y;
			g_cur_craft->push_accum_z =
				g_rotated_z +
				g_object_table[target_object_index].world_z -
				g_object_table[g_pai_context.object_index]
					.world_z;
			push_z = g_cur_craft->push_accum_z;
			target_roll = g_object_table[target_object_index].roll;
			target_yaw = g_object_table[target_object_index].yaw;
			target_pitch =
				g_object_table[target_object_index].pitch;
		} else {
			mission_resolve_object_or_mission_point_world_loc(
				target_object_index, 0);
			g_cur_craft->push_accum_x =
				g_world_loc_x -
				g_object_table[g_pai_context.object_index]
					.world_x;
			push_x = g_cur_craft->push_accum_x;
			g_cur_craft->push_accum_y =
				g_world_loc_y -
				g_object_table[g_pai_context.object_index]
					.world_y;
			push_y = g_cur_craft->push_accum_y;
			g_cur_craft->push_accum_z =
				g_world_loc_z -
				g_object_table[g_pai_context.object_index]
					.world_z +
				MISSION_POINT_DOCK_Z_OFFSET;
			push_z = g_cur_craft->push_accum_z;
			target_roll = 0;
			target_yaw = 0;
			target_pitch = 0x4000;
		}
		if (g_object_table[g_pai_context.object_index].roll !=
		    target_roll) {
			g_cur_craft->ai_flight.roll_state = 1;
			g_cur_craft->ai_flight.roll_step = ALIGNMENT_STEP;
			g_pai_context.controller->target_roll = target_roll;
		}
		if (g_object_table[g_pai_context.object_index].yaw !=
		    target_yaw) {
			g_cur_craft->ai_flight.turn_state = 2;
			g_cur_craft->ai_flight.turn_step =
				(int16_t)ALIGNMENT_STEP;
			g_pai_context.controller->target_xy_angle = target_yaw;
		}
		if (g_object_table[g_pai_context.object_index].pitch !=
		    g_object_table[target_object_index].pitch) {
			g_pai_context.controller->target_z_angle = target_pitch;
			g_cur_craft->ai_flight.pitch_step_scale =
				ALIGNMENT_STEP;
			g_cur_craft->ai_flight.pitch_through_loop = 0;
			g_cur_craft->ai_flight.pitch_state =
				g_pai_context.controller->target_z_angle >
						g_cur_craft->pitch
					? 2
					: 1;
		}
		if (push_x < 0) {
			push_x = -push_x;
		}
		if (push_y < 0) {
			push_y = -push_y;
		}
		if (push_z < 0) {
			push_z = -push_z;
		}
		if (push_x + push_y + push_z >= DOCKING_ALIGNMENT_DISTANCE) {
			return 0;
		}

		g_cur_craft->push_accum_x = 0;
		g_cur_craft->push_accum_y = 0;
		g_cur_craft->push_accum_z = 0;
		g_pai_context.controller->maneuver_phase = BOARD_PHASE_TRANSFER;
		g_pai_context.controller->maneuver_timer =
			DOCKING_DURATION_PER_ORDER_UNIT * variable1;
		g_pai_context.controller->secondary_maneuver_timer =
			SIMULATION_TICKS_PER_SECOND;
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"ai.boarding_started object=%d fg=%d target=%d target_fg=%d plan=%d timer=%d tick=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)target_object_index,
				target_object_index >= 0x8000
					? -1
					: (int)target_flight_group_index,
				(int)g_pai_context.controller->current_plan_id,
				g_pai_context.controller->maneuver_timer,
				g_game_time);
		}
		if (g_object_table[g_pai_context.object_index].mobj != NULL &&
		    g_cur_craft->ai_flight.docking_accounting_done == 0) {
			uint16_t flight_group_index =
				g_pai_context.craft_flight_group_index;

			g_cur_craft->ai_flight.docking_accounting_done = 1;
			++g_mission_fg_stats[flight_group_index].outcome_count
				  [FLIGHT_GROUP_OUTCOME_FAILED_MISSION];
			if (g_mission_flight_groups[flight_group_index]
				    .fg.special_cargo_craft ==
			    g_cur_craft->craft_ordinal) {
				g_mission_fg_stats[flight_group_index].special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_FAILED_MISSION] =
					1;
			}
		}
		if (target_mobile_object != NULL &&
		    target_craft->ai_flight.boarded_accounting_done == 0) {
			target_craft->ai_flight.boarded_accounting_done = 1;
			++g_mission_fg_stats[target_flight_group_index].outcome_count
				  [FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION];
			if (g_mission_flight_groups[target_flight_group_index]
				    .fg.special_cargo_craft ==
			    target_craft->craft_ordinal) {
				g_mission_fg_stats[target_flight_group_index].special_cargo_outcome
					[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION] =
					1;
			}
		}
		XVT_LOG_DEBUG(
			"ai.docking_counted object=%d target=%d failed=%d completed=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)target_object_index,
			(int)g_mission_fg_stats[g_pai_context
							.craft_flight_group_index]
				.outcome_count
					[FLIGHT_GROUP_OUTCOME_FAILED_MISSION],
			target_craft != NULL
				? (int)g_mission_fg_stats[target_flight_group_index]
					  .outcome_count
						  [FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION]
				: -1,
			g_flight_sim_side_effects_suppressed);
		msg_format_object_name(g_pai_context.object_index, 1,
				       g_flight_text_scratch_buffer);
		msg_add_message_ptr(0, g_flight_text_scratch_buffer);
		msg_format_object_name(target_object_index, 1,
				       g_flight_secondary_object_name_buffer);
		msg_add_message_ptr(1, g_flight_secondary_object_name_buffer);
		g_msg_sender_iff =
			(uint8_t)g_object_table[g_pai_context.object_index]
				.mobj->iff;
		msg_emit_in_flight_message(IFMSG_234_ARG_HAS_DOCKED_WITH_ARG,
					   g_local_player);
		if (variable1 >= MINIMUM_VOICE_ORDER_TIME) {
			const char *plan_name =
				g_plan_table[g_pai_context.controller
						     ->current_plan_id]
					.name;

			if (strcmp(plan_name, "boardtogivepln") == 0 ||
			    strcmp(plan_name, "boardtoexchangepln") == 0 ||
			    strcmp(plan_name, "boardtocontactpln") == 0 ||
			    strcmp(plan_name, "boardtopickuppln") == 0 ||
			    strcmp(plan_name, "boardtorepairpln") == 0) {
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_STATUS,
					TACTICAL_MSG_BOARDING_STARTED_FRIENDLY,
					g_pai_context.object_index, UINT16_MAX);
			} else if (strcmp(plan_name, "boardtocapturepln") ==
					   0 ||
				   strcmp(plan_name, "boardtotakepln") == 0 ||
				   strcmp(plan_name, "boardtodestroypln") ==
					   0) {
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_STATUS,
					TACTICAL_MSG_BOARDING_STARTED_HOSTILE,
					g_pai_context.object_index, UINT16_MAX);
			}
		}
		fsfx_speak_tactical_officer_event(
			TACTICAL_VOICE_STATUS,
			TACTICAL_MSG_BOARDING_STARTED_TARGET,
			target_object_index, UINT16_MAX);
		return 0;
	}

	case BOARD_PHASE_TRANSFER: {
		const char *plan_name =
			g_plan_table[g_pai_context.controller->current_plan_id]
				.name;

		if (g_pai_context.controller->maneuver_timer != 0) {
			if (strcmp(plan_name, "boardtogivepln") != 0 ||
			    g_object_table[target_object_index]
					    .player_owner_idx == -1 ||
			    g_pai_context.controller
					    ->secondary_maneuver_timer != 0 ||
			    target_craft == NULL) {
				return 0;
			}
			uint16_t reloaded_or_repaired = 0;
			for (uint16_t launcher_index = 0;
			     launcher_index <
			     target_craft->warhead_launcher_count;
			     ++launcher_index) {
				if (target_craft->warhead_slot_type_ids
					    [launcher_index] == 0) {
					continue;
				}
				uint16_t last_weapon_slot =
					g_model_defs[target_model_index]
						.warhead_launcher_last_slot
							[launcher_index];
				for (uint16_t weapon_slot_index =
					     g_model_defs[target_model_index]
						     .warhead_launcher_first_slot
							     [launcher_index];
				     weapon_slot_index <= last_weapon_slot;
				     ++weapon_slot_index) {
					uint16_t warhead =
						g_mission_flight_groups
							[g_object_table[target_object_index]
								 .flight_group_idx]
								.fg.warhead;

					if (launcher_index ==
					    WARHEAD_LAUNCHER_SECONDARY) {
						warhead =
							SECONDARY_WARHEAD_TYPE;
					}
					uint16_t desired_count = math2_fraction(
						g_model_defs[target_model_index]
							.warhead_launcher_capacity
								[launcher_index],
						g_warhead_ammo_fraction_q16
							[warhead]);
					if (desired_count == 0) {
						desired_count =
							MINIMUM_WARHEAD_COUNT;
					}
					uint8_t flight_group_status =
						g_mission_flight_groups
							[g_object_table[target_object_index]
								 .flight_group_idx]
								.fg.status1;
					if (flight_group_status == 1) {
						desired_count *= 2;
					} else if (flight_group_status == 2) {
						desired_count >>= 1;
					}
					if (desired_count == 0) {
						desired_count =
							MINIMUM_WARHEAD_COUNT;
					}
					if (desired_count >
					    MAXIMUM_WARHEAD_COUNT) {
						desired_count =
							MAXIMUM_WARHEAD_COUNT;
					}
					if (target_craft
						    ->weapon_slots
							    [weapon_slot_index]
						    .ammo_count <
					    desired_count) {
						reloaded_or_repaired = 1;
						++target_craft
							  ->weapon_slots
								  [weapon_slot_index]
							  .ammo_count;
					}
					target_craft
						->weapon_slots
							[weapon_slot_index]
						.laser_charge =
						FULL_WEAPON_CHARGE;
				}
			}
			if (target_craft->cm_type_id !=
			    COUNTERMEASURE_TYPE_NONE) {
				target_craft->cm_ammo_count =
					g_model_defs[target_model_index]
						.countermeasure_count;
			}
			{
				uint16_t subsystem_mask =
					CRAFT_SUBSYSTEM_FLAG_SHIELDS;

				for (uint16_t system_index = 0;
				     system_index < CRAFT_SUBSYSTEM_COUNT;
				     ++system_index) {
					if ((target_craft->system_flags &
					     subsystem_mask) != 0 &&
					    (target_craft->working_subsystems &
					     subsystem_mask) == 0) {
						reloaded_or_repaired = 1;
						target_craft
							->working_subsystems |=
							subsystem_mask;
						break;
					}
					subsystem_mask *= 2;
				}
			}
			g_pai_context.controller->secondary_maneuver_timer =
				RELOAD_STEP_TICKS;
			if (reloaded_or_repaired != 0) {
				g_pai_context.controller->maneuver_timer =
					RELOAD_CONTINUE_DURATION;
			}
			XVT_LOG_DEBUG(
				"ai.reload_step object=%d target=%d slot=%d reloaded=%d timer=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)target_object_index,
				g_object_table[target_object_index]
					.player_owner_idx,
				(int)reloaded_or_repaired,
				g_pai_context.controller->maneuver_timer,
				g_flight_sim_side_effects_suppressed);
			return 0;
		}

		if (strcmp(plan_name, "boardtogivepln") == 0) {
			if (target_mobile_object != NULL) {
				for (uint16_t cargo_index = 0;
				     cargo_index <
				     sizeof(target_craft->special_cargo_name);
				     ++cargo_index) {
					target_craft->special_cargo_name
						[cargo_index] =
						g_cur_craft->special_cargo_name
							[cargo_index];
				}
				target_craft->boarding_state = 2;
			}
			if (g_mission_flight_groups
				    [g_pai_context.craft_flight_group_index]
					    .fg
					    .orders[g_pai_context.controller
							    ->current_order_slot]
					    .variable2 <=
			    g_pai_context.controller->order_progress.goal_progress
					    [g_pai_context.controller
						     ->current_order_slot] +
				    1) {
				g_cur_craft->special_cargo_name[0] = 0;
			}
			g_cur_craft->boarding_state = 1;
			if (g_object_table[target_object_index]
					    .player_owner_idx != -1 &&
			    target_craft != NULL) {
				if (g_object_table[target_object_index]
					    .player_owner_idx ==
				    g_local_player) {
					int player_object_index =
						g_players[g_local_player]
							.object_index;
					int can_restore_hud = 0;

					if (player_object_index != -1) {
						uint8_t player_object_type =
							g_object_table
								[player_object_index]
									.object_type;

						can_restore_hud =
							player_object_type ==
								CRAFT_SPECIES_X_WING ||
							player_object_type ==
								CRAFT_SPECIES_Y_WING ||
							player_object_type ==
								CRAFT_SPECIES_A_WING ||
							player_object_type ==
								CRAFT_SPECIES_Z_95_HEADHUNTER ||
							player_object_type ==
								CRAFT_SPECIES_B_WING;
					}
					if (can_restore_hud) {
						uint16_t feature_mask = 1;

						for (int feature_index = 0;
						     feature_index <
						     HUD_FEATURE_COUNT;
						     ++feature_index) {
							if ((target_craft
								     ->damage_stats
								     .installed_hud_feature_mask &
							     feature_mask) !=
								    0 &&
							    (target_craft
								     ->damage_stats
								     .active_hud_feature_mask &
							     feature_mask) ==
								    0) {
								target_craft
									->damage_stats
									.active_hud_feature_mask |=
									feature_mask;
							}
							feature_mask *= 2;
						}
						XVT_LOG_DEBUG(
							"ai.hud_features_restored object=%d target=%d slot=%d mask=%04x predicted=%d",
							(int)g_pai_context
								.object_index,
							(int)target_object_index,
							g_local_player,
							(unsigned)target_craft
								->damage_stats
								.active_hud_feature_mask,
							g_flight_sim_side_effects_suppressed);
						flight_surface_lock();
						hud_rebuild_display_for_view_state(
							g_players[g_local_player]
								.view_state
								.hud_state_live,
							g_local_player);
						flight_surface_unlock();
					}
				}
				{
					for (uint16_t system_index = 0;
					     system_index <
					     CRAFT_SUBSYSTEM_COUNT;
					     ++system_index) {
						target_craft
							->system_display_slot_by_system
								[system_index] =
							(uint8_t)system_index;
						target_craft->system_health
							[system_index] =
							SYSTEM_HEALTH_FULL;
						target_craft
							->system_repair_seconds
								[system_index] =
							0;
					}
				}
			}
			msg_emit_craft_message(
				g_pai_context.object_index, g_cur_craft,
				IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS,
				TACTICAL_MSG_TRANSFER_COMPLETE,
				g_pai_context.object_index, UINT16_MAX);
		} else if (strcmp(plan_name, "boardtotakepln") == 0) {
			if (target_mobile_object != NULL) {
				for (uint16_t cargo_index = 0;
				     cargo_index <
				     sizeof(target_craft->special_cargo_name);
				     ++cargo_index) {
					g_cur_craft->special_cargo_name
						[cargo_index] =
						target_craft->special_cargo_name
							[cargo_index];
				}
				target_craft->special_cargo_name[0] = 0;
				target_craft->boarding_state = 1;
			}
			g_cur_craft->boarding_state = 2;
			msg_emit_craft_message(
				g_pai_context.object_index, g_cur_craft,
				IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS,
				TACTICAL_MSG_BOARDING_COMPLETE,
				g_pai_context.object_index, UINT16_MAX);
		} else if (strcmp(plan_name, "boardtoexchangepln") == 0) {
			if (target_mobile_object != NULL) {
				for (uint16_t cargo_index = 0;
				     cargo_index <
				     sizeof(target_craft->special_cargo_name);
				     ++cargo_index) {
					char cargo_byte =
						g_cur_craft->special_cargo_name
							[cargo_index];

					g_cur_craft->special_cargo_name
						[cargo_index] =
						target_craft->special_cargo_name
							[cargo_index];
					target_craft->special_cargo_name
						[cargo_index] = cargo_byte;
				}
				target_craft->boarding_state = 2;
			}
			g_cur_craft->boarding_state = 2;
			msg_emit_craft_message(
				g_pai_context.object_index, g_cur_craft,
				IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
		} else if (strcmp(plan_name, "boardtocapturepln") == 0) {
			if (target_mobile_object != NULL) {
				paiman_transfer_object_to_ai_team(
					target_object_index, target_craft,
					CAPTURE_OWNER_FLAG);
				if (target_craft->captured_by_flight_group !=
				    0) {
					if (target_craft->ai_flight
						    .max_speed_cache != 0) {
						target_controller
							->running_plan_id =
							pai_find_plan_id_by_name_or_zero(
								"flyhomeevadepln");
					} else {
						target_controller
							->running_plan_id =
							pai_find_plan_id_by_name_or_zero(
								"stationaryldrpln");
					}
					msg_emit_craft_message(
						target_object_index,
						target_craft,
						IFMSG_139_HAS_BEEN_CAPTURED);
				} else {
					int order =
						g_mission_flight_groups
							[target_flight_group_index]
								.fg.orders[0]
								.order;
					uint8_t plan_name_index =
						target_craft->leader_obj_idx ==
								UINT8_MAX
							? g_order_leader_builtin_plan_name_index
								  [order]
							: g_order_follower_builtin_plan_name_index
								  [order];

					target_controller->running_plan_id =
						g_builtin_plan_id_by_name_index
							[plan_name_index];
				}
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"ai.craft_captured object=%d target=%d team=%d captured=%d plan=%d tick=%d",
						(int)g_pai_context.object_index,
						(int)target_object_index,
						(int)target_mobile_object->team,
						(int)target_craft
							->captured_by_flight_group,
						(int)target_controller
							->running_plan_id,
						g_game_time);
				}
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_STATUS,
					TACTICAL_MSG_BOARDING_COMPLETE,
					g_pai_context.object_index, UINT16_MAX);
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_STATUS,
					TACTICAL_MSG_CAPTURED_TARGET,
					target_object_index, UINT16_MAX);
				struct pai_context saved_context =
					g_pai_context;
				struct craft_data *saved_craft = g_cur_craft;
				g_cur_craft = target_craft;
				pai_setupcraftcontext(target_object_index);
				pai_apply_running_plan_target_and_maneuver(
					target_object_index);
				g_cur_craft = saved_craft;
				g_pai_context = saved_context;
			}
		} else if (strcmp(plan_name, "boardtodestroypln") == 0) {
			if (target_mobile_object != NULL) {
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"battle.killing_blow victim=%d by=%d attacker=%d weapon=\"boarding\" type=%d tick=%d",
						(int)target_object_index,
						g_object_table
							[g_pai_context
								 .object_index]
								.player_owner_idx,
						(int)g_pai_context.object_index,
						(int)g_object_table
							[g_pai_context
								 .object_index]
								.object_type,
						g_game_time);
				}
				mission_credit_destruction_damage_contributors(
					g_pai_context.object_index,
					target_object_index);
				if (target_mobile_object->lifetime_timer != 0) {
					target_mobile_object->lifetime_timer =
						SIMULATION_TICKS_PER_SECOND *
						((game_rand() &
						  SELF_DESTRUCT_RANDOM_MASK) +
						 SELF_DESTRUCT_RANDOM_BASE);
				}
				target_controller->running_plan_id =
					pai_find_plan_id_by_name_or_zero(
						"selfdestroypln");
				XVT_LOG_DEBUG(
					"ai.target_sabotaged object=%d target=%d plan=%d life=%u predicted=%d",
					(int)g_pai_context.object_index,
					(int)target_object_index,
					(int)target_controller->running_plan_id,
					(unsigned)target_mobile_object
						->lifetime_timer,
					g_flight_sim_side_effects_suppressed);
				struct pai_context saved_context =
					g_pai_context;
				struct craft_data *saved_craft = g_cur_craft;
				g_cur_craft = target_craft;
				pai_setupcraftcontext(target_object_index);
				pai_apply_running_plan_target_and_maneuver(
					target_object_index);
				g_cur_craft = saved_craft;
				g_pai_context = saved_context;
			}
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS,
				TACTICAL_MSG_BOARDING_COMPLETE,
				g_pai_context.object_index, UINT16_MAX);
		} else if (strcmp(plan_name, "boardtopickuppln") == 0) {
			if (target_mobile_object != NULL) {
				paiman_transfer_object_to_ai_team(
					target_object_index, target_craft,
					CAPTURE_OWNER_FLAG);
				g_cur_craft->carried_object_index =
					target_object_index;
				target_craft->carrier_obj_idx =
					g_pai_context.object_index;
				target_craft->working_subsystems = 0;
			} else {
				++g_mission_fg_stats[*target_flight_group_index_ptr]
					  .outcome_count
						  [FLIGHT_GROUP_OUTCOME_CAPTURED];
				g_object_table[target_object_index]
					.object_type = 0;
				XVT_LOG_DEBUG(
					"ai.object_picked_up object=%d target=%d fg=%d count=%u predicted=%d",
					(int)g_pai_context.object_index,
					(int)target_object_index,
					(int)*target_flight_group_index_ptr,
					(unsigned)g_mission_fg_stats
						[*target_flight_group_index_ptr]
							.outcome_count
								[FLIGHT_GROUP_OUTCOME_CAPTURED],
					g_flight_sim_side_effects_suppressed);
			}
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS,
				TACTICAL_MSG_TRANSFER_COMPLETE,
				g_pai_context.object_index, UINT16_MAX);
		} else if (strcmp(plan_name, "boardtocontactpln") == 0) {
			if (target_mobile_object != NULL) {
				target_craft->boarding_state = 2;
			}
			msg_emit_craft_message(
				g_pai_context.object_index, g_cur_craft,
				IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS,
				TACTICAL_MSG_BOARDING_COMPLETE,
				g_pai_context.object_index, UINT16_MAX);
		} else if (strcmp(plan_name, "boardtorepairpln") == 0) {
			if (target_mobile_object != NULL) {
				target_craft->working_subsystems =
					target_craft->system_flags;
				target_craft->subsystem_damage = 0;
				target_craft->object_kind =
					CRAFT_OBJECT_KIND_ACTIVE;
				target_craft->boarding_state = 3;
			}
			msg_emit_craft_message(
				g_pai_context.object_index, g_cur_craft,
				IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE);
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS,
				TACTICAL_MSG_BOARDING_COMPLETE,
				g_pai_context.object_index, UINT16_MAX);
			fsfx_speak_tactical_officer_event(
				TACTICAL_VOICE_STATUS,
				TACTICAL_MSG_REPAIRED_TARGET,
				target_object_index, UINT16_MAX);
		}

		if (target_mobile_object != NULL) {
			struct mobile_object *boarding_mobile_object =
				g_object_table[g_pai_context.object_index].mobj;

			if (boarding_mobile_object->iff ==
			    target_mobile_object->iff) {
				uint8_t *team_identified_order =
					&target_craft->identified_order_by_team
						 [boarding_mobile_object->team];

				if (*team_identified_order == 0) {
					uint16_t highest_identified_order = 0;

					for (uint16_t team_index = 0;
					     team_index < TEAM_COUNT;
					     ++team_index) {
						if (highest_identified_order <
						    target_craft->identified_order_by_team
							    [team_index]) {
							highest_identified_order =
								target_craft->identified_order_by_team
									[team_index];
						}
					}
					*team_identified_order =
						(uint8_t)(highest_identified_order +
							  1);
					++g_mission_fg_stats[*target_flight_group_index_ptr]
						  .outcome_count
							  [FLIGHT_GROUP_OUTCOME_INSPECTED];
					if (g_mission_flight_groups
						    [*target_flight_group_index_ptr]
							    .fg
							    .special_cargo_craft ==
					    target_craft->craft_ordinal) {
						g_mission_fg_stats[*target_flight_group_index_ptr]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_INSPECTED] =
							1;
					}
				}
			} else {
				fsfx_play_sound(FLIGHT_SOUND_GENERAL_WARNING,
						-1, g_local_player);
			}
		}
		++g_pai_context.controller->order_progress.goal_progress
			  [g_pai_context.controller->current_order_slot];
		g_cur_craft->ai_flight.docked_target_signatures
			[g_cur_craft->ai_flight.docked_target_count++] =
			target_signature;
		if (g_cur_craft->ai_flight.docked_target_count >=
		    MAX_OBJECT_SIGNATURE_COUNT) {
			--g_cur_craft->ai_flight.docked_target_count;
		}
		if (g_cur_craft->ai_flight.docked_target_count == 1) {
			uint16_t flight_group_index =
				g_pai_context.craft_flight_group_index;

			++g_mission_fg_stats[flight_group_index]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_DOCKED];
			if (g_mission_flight_groups[flight_group_index]
				    .fg.special_cargo_craft ==
			    g_cur_craft->craft_ordinal) {
				g_mission_fg_stats[flight_group_index]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_DOCKED] =
					1;
			}
		}
		if (target_mobile_object != NULL) {
			++target_craft->ai_flight.times_boarded;
			++g_mission_fg_stats[target_flight_group_index]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_BOARDED];
			if (g_mission_flight_groups[target_flight_group_index]
				    .fg.special_cargo_craft ==
			    target_craft->craft_ordinal) {
				g_mission_fg_stats[target_flight_group_index]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_BOARDED] =
					1;
			}
		}
		g_pai_context.controller->maneuver_phase = BOARD_PHASE_SEPARATE;
		g_pai_context.controller->maneuver_timer = SEPARATION_DURATION;
		if (g_players[g_local_player].current_target_object_idx ==
		    target_object_index) {
			g_hud_cached_target_object_idx = HUD_TARGET_INVALIDATED;
		}
		if (g_object_table[target_object_index].player_owner_idx !=
		    -1) {
			g_pai_context.controller->candidate_target_idx =
				UINT16_MAX;
		}
		XVT_LOG_DEBUG(
			"ai.boarding_done object=%d target=%d plan=%d state=%d target_state=%d captured=%d target_plan=%d carried=%d identified=%d progress=%d docked=%d docked_count=%u boarded=%d boarded_count=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)target_object_index,
			(int)g_pai_context.controller->current_plan_id,
			(int)g_cur_craft->boarding_state,
			target_craft != NULL ? (int)target_craft->boarding_state
					     : -1,
			target_craft != NULL
				? (int)target_craft->captured_by_flight_group
				: -1,
			target_craft != NULL ? (int)target_craft->ai_controller
						       .running_plan_id
					     : -1,
			g_cur_craft->carried_object_index == UINT16_MAX
				? -1
				: (int)g_cur_craft->carried_object_index,
			target_craft != NULL
				? (int)target_craft->identified_order_by_team
					  [g_object_table[g_pai_context
								  .object_index]
						   .mobj->team]
				: -1,
			(int)g_pai_context.controller->order_progress
				.goal_progress[g_pai_context.controller
						       ->current_order_slot],
			(int)g_cur_craft->ai_flight.docked_target_count,
			(unsigned)g_mission_fg_stats
				[g_pai_context.craft_flight_group_index]
					.outcome_count
						[FLIGHT_GROUP_OUTCOME_DOCKED],
			target_craft != NULL
				? (int)target_craft->ai_flight.times_boarded
				: -1,
			target_mobile_object != NULL
				? (int)g_mission_fg_stats[target_flight_group_index]
					  .outcome_count
						  [FLIGHT_GROUP_OUTCOME_BOARDED]
				: -1,
			g_flight_sim_side_effects_suppressed);
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"ai.boarding_finished object=%d fg=%d target=%d plan=%d progress=%d tick=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)target_object_index,
				(int)g_pai_context.controller->current_plan_id,
				(int)g_pai_context.controller->order_progress
					.goal_progress
						[g_pai_context.controller
							 ->current_order_slot],
				g_game_time);
		}
		return 0;
	}

	case BOARD_PHASE_SEPARATE:
		if (g_pai_context.controller->maneuver_timer == 0) {
			XVT_LOG_DEBUG(
				"ai.boarding_separated object=%d target=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.controller->target_obj_idx,
				g_flight_sim_side_effects_suppressed);
			g_pai_context.controller->target_obj_idx = UINT16_MAX;
			g_pai_context.controller->target_signature = 0;
			g_pai_context.controller->has_live_target = 0;
			return 1;
		}
		if (target_mobile_object != NULL) {
			pai_calcrotatedpoint(
				&g_object_table[g_pai_context.object_index], 0,
				0x4000, 0);
			g_cur_craft->push_accum_x =
				g_rotated_x +
				g_object_table[target_object_index].world_x -
				g_object_table[g_pai_context.object_index]
					.world_x;
			g_cur_craft->push_accum_y =
				g_rotated_y +
				g_object_table[target_object_index].world_y -
				g_object_table[g_pai_context.object_index]
					.world_y;
			g_cur_craft->push_accum_z =
				g_rotated_z +
				g_object_table[target_object_index].world_z -
				g_object_table[g_pai_context.object_index]
					.world_z;
		} else {
			g_cur_craft->push_accum_x = 0;
			g_cur_craft->push_accum_y = 0;
			g_cur_craft->push_accum_z =
				SEPARATION_MISSION_POINT_PUSH;
		}
		return 0;

	default:
		return 0;
	}
}

/* Gives an object the IFF and team of the craft in g_pai_context, restores its
 * working subsystems, clears its subsystem damage and last attacker, and marks
 * it active. When the teams differed it updates the capture counts: taken back
 * by its own flight group's team, it undoes the FLIGHT_GROUP_OUTCOME_CAPTURED
 * count and clears captured_by_flight_group; else it counts the capture for the
 * new team, moving it from an earlier captor's team, and sets
 * captured_by_flight_group to owner_flag with the capturing flight group. */
// FUNCTION: XVT 0x4A3750
void paiman_transfer_object_to_ai_team(unsigned int object_idx,
				       struct craft_data *craft,
				       uint8_t owner_flag)
{
	struct object_record *object = &g_object_table[object_idx];
	int flight_group_idx = object->flight_group_idx;
	struct mobile_object **current_mobile_object_ptr =
		&g_object_table[g_pai_context.object_index].mobj;
	uint8_t object_team = object->mobj->team;
	if ((*current_mobile_object_ptr)->team != object_team) {
		uint8_t mission_team =
			g_mission_flight_groups[flight_group_idx].fg.team;
		if ((*current_mobile_object_ptr)->team == mission_team) {
			g_mission_fg_stats[flight_group_idx]
				.outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED]--;
			if (g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft ==
			    craft->craft_ordinal) {
				g_mission_fg_stats[flight_group_idx]
					.special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_CAPTURED] =
					0;
			}
			craft->captured_by_flight_group = 0;
		} else {
			if (object_team == mission_team) {
				g_mission_fg_stats[flight_group_idx].outcome_count
					[FLIGHT_GROUP_OUTCOME_CAPTURED]++;
				if (g_mission_flight_groups[flight_group_idx]
					    .fg.special_cargo_craft ==
				    craft->craft_ordinal) {
					g_mission_fg_stats[flight_group_idx].special_cargo_outcome
						[FLIGHT_GROUP_OUTCOME_CAPTURED] =
						1;
				}
			} else {
				g_flight_mission_state.runtime.team_fg_inspected_captured_counts
					[1]
					[g_mission_flight_groups
						 [craft->captured_by_flight_group &
						  0x7F]
							 .fg.team]
					[flight_group_idx]--;
			}
			g_flight_mission_state.runtime
				.team_fg_inspected_captured_counts
					[1][(*current_mobile_object_ptr)->team]
					[flight_group_idx]++;
			craft->captured_by_flight_group =
				owner_flag |
				(uint8_t)g_pai_context.craft_flight_group_index;
		}
	}

	g_object_table[object_idx].mobj->iff =
		g_object_table[g_pai_context.object_index].mobj->iff;
	g_object_table[object_idx].mobj->team =
		g_object_table[g_pai_context.object_index].mobj->team;
	craft->working_subsystems = craft->system_flags;
	craft->subsystem_damage = 0;
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->last_attacker_obj_idx = UINT16_MAX;
	XVT_LOG_DEBUG(
		"ai.craft_changed_side object=%d target=%d fg=%d from=%d team=%d iff=%d captured=%d captured_count=%u team_count=%u predicted=%d",
		(int)g_pai_context.object_index, (int)object_idx,
		flight_group_idx, (int)object_team,
		(int)g_object_table[object_idx].mobj->team,
		(int)g_object_table[object_idx].mobj->iff,
		(int)craft->captured_by_flight_group,
		(unsigned)g_mission_fg_stats[flight_group_idx]
			.outcome_count[FLIGHT_GROUP_OUTCOME_CAPTURED],
		(unsigned)g_flight_mission_state.runtime
			.team_fg_inspected_captured_counts
				[1][g_object_table[object_idx].mobj->team]
				[flight_group_idx],
		g_flight_sim_side_effects_suppressed);
}

/* Starts await board, also used for stop: stops the roll, pitch and turn and
 * sets throttle 0. */
// FUNCTION: XVT 0x4A3920
void paiman_initawaitboardmaneuver(void)
{
	g_cur_craft->ai_flight.roll_state = 0;
	g_cur_craft->ai_flight.pitch_state = 0;
	g_cur_craft->ai_flight.turn_state = 0;
	paiman_setpower(g_pai_context.object_index, 0);
}

/* Keeps the craft stopped as paiman_initawaitboardmaneuver does; returns 0. */
// FUNCTION: XVT 0x4A3960
int16_t paiman_awaitboardmaneuver(void)
{
	g_cur_craft->ai_flight.roll_state = 0;
	g_cur_craft->ai_flight.pitch_state = 0;
	g_cur_craft->ai_flight.turn_state = 0;
	paiman_setpower(g_pai_context.object_index, 0);
	return 0;
}

/* Starts head toward: steers at the aim point. */
// FUNCTION: XVT 0x4A39A0
void paiman_initheadtowardmaneuver(void) { paiman_setflighttotarget(0, 1); }

/* Steers at the aim point and rolls level at full step; returns 0. */
// FUNCTION: XVT 0x4A39B0
int16_t paiman_headtowardmaneuver(void)
{
	paiman_setflighttotarget(0, 1);
	g_cur_craft->ai_flight.roll_state = 1;
	g_cur_craft->ai_flight.roll_step = UINT16_MAX;
	g_pai_context.controller->target_roll = 0;
	return 0;
}

/* Starts turn away: sets the course with paiman_setupturnawaycourse and runs
 * for 3,540 ticks. */
// FUNCTION: XVT 0x4A39F0
void paiman_initturnawaymaneuver(void)
{
	paiman_setupturnawaycourse(g_pai_context.object_index);
	g_pai_context.controller->maneuver_timer = 3540;
}

/* Returns 1 once the maneuver timer has run out, else 0; each time
 * secondary_maneuver_timer runs out it sets the course again. */
// FUNCTION: XVT 0x4A3A10
int16_t paiman_turnawaymaneuver(void)
{
	if (g_pai_context.controller->maneuver_timer == 0) {
		return 1;
	}
	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		paiman_setupturnawaycourse(g_pai_context.object_index);
	}
	return 0;
}

/* Turns the craft to its attacker's yaw, or half a circle off the object's own
 * yaw when it has no attacker, at half the effective skill plus 0x8000, and
 * sets secondary_maneuver_timer to the tier's g_ai_turn_away_state_delay_by_skill
 * seconds in ticks. */
// FUNCTION: XVT 0x4A3A50
void paiman_setupturnawaycourse(unsigned int object_idx)
{
	uint16_t yaw;

	if (g_cur_craft->last_attacker_obj_idx != UINT16_MAX) {
		yaw = g_object_table[g_cur_craft->last_attacker_obj_idx].yaw;
	} else {
		yaw = g_object_table[object_idx].yaw + 0x8000;
	}
	g_pai_context.controller->target_xy_angle = yaw;
	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	unsigned int turn_step = effective_skill >> 1;
	turn_step += 0x8000;
	paiman_setturn(turn_step);
	g_pai_context.controller->secondary_maneuver_timer =
		g_ai_turn_away_state_delay_by_skill[g_pai_context.skill_tier] *
		SIMULATION_TICKS_PER_SECOND;
}

/* Starts out of hangar: a maneuver timer of 2,360 ticks. */
// FUNCTION: XVT 0x4A3AF0
void paiman_initoutofhangarmaneuver(void)
{
	g_pai_context.controller->maneuver_timer = 2360;
}

/* Returns 1 once the maneuver timer has run out, else 0. At that point it gives
 * the whole flight group the mission's formation and spacing, and writes the
 * plan id of the craft's first order, leader or follower, into byte 3 of the
 * shared exithangarpln plan data, the plan its first order switches to. */
// FUNCTION: XVT 0x4A3B00
int16_t paiman_outofhangarmaneuver(void)
{
	int maneuver_timer = g_pai_context.controller->maneuver_timer;
	if (maneuver_timer == 0) {
		uint16_t flight_group_index =
			g_pai_context.craft_flight_group_index;
		uint16_t order = g_mission_flight_groups[flight_group_index]
					 .fg.orders[0]
					 .order;
		uint8_t plan_id;
		if (g_cur_craft->leader_obj_idx == UINT8_MAX) {
			plan_id = g_builtin_plan_id_by_name_index
				[g_order_leader_builtin_plan_name_index[order]];
		} else {
			plan_id = g_builtin_plan_id_by_name_index
				[g_order_follower_builtin_plan_name_index
					 [order]];
		}
		pai_set_flight_group_formation(
			flight_group_index,
			g_mission_flight_groups[flight_group_index]
				.fg.formation,
			g_mission_flight_groups[flight_group_index]
				.fg.formation_spacing);
		uint8_t *exit_hangar_plan =
			pai_getplandataptrbyname("exithangarpln");
		exit_hangar_plan[3] = plan_id;
		XVT_LOG_DEBUG(
			"ai.hangar_exited object=%d fg=%d formation=%d spacing=%d plan=%d predicted=%d",
			(int)g_pai_context.object_index,
			(int)flight_group_index,
			(int)g_mission_flight_groups[flight_group_index]
				.fg.formation,
			(int)g_mission_flight_groups[flight_group_index]
				.fg.formation_spacing,
			(int)plan_id, g_flight_sim_side_effects_suppressed);
		return 1;
	}

	return 0;
}

/* Starts avoid starship toward the target_xy_angle and target_z_angle the order
 * set: a turn at half the effective skill plus 0x8000, a pitch at full step
 * without a loop, and secondary_maneuver_timer at 15 to 22 times
 * SIMULATION_TICKS_PER_SECOND. */
// FUNCTION: XVT 0x4A3BB0
void paiman_initavoidstarshipmaneuver(void)
{
	g_pai_context.controller->secondary_maneuver_timer =
		((game_rand() & 7) + 15) * SIMULATION_TICKS_PER_SECOND;
	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	unsigned int turn_step = effective_skill >> 1;
	turn_step += 0x8000;
	paiman_setturn(turn_step);
	g_cur_craft->ai_flight.pitch_step_scale = UINT16_MAX;
	g_cur_craft->ai_flight.pitch_through_loop = 0;
	if (g_pai_context.controller->target_z_angle <= g_cur_craft->pitch) {
		g_cur_craft->ai_flight.pitch_state = 1;
	} else {
		g_cur_craft->ai_flight.pitch_state = 2;
	}
}

/* Returns 0; paiorder_avoidstarshiporder ends this maneuver. */
// FUNCTION: XVT 0x4A3C40
int16_t paiman_avoidstarshipmaneuver(void) { return 0; }

/* Starts wait: a maneuver timer of the order's variable1 times 1,180 ticks,
 * roll, pitch and turn stopped, throttle 0. */
// FUNCTION: XVT 0x4A3C50
void paiman_initwaitmaneuver(void)
{
	g_pai_context.controller->maneuver_timer =
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.variable1;
	g_pai_context.controller->maneuver_timer *= 1180;
	g_cur_craft->ai_flight.roll_state = 0;
	g_cur_craft->ai_flight.pitch_state = 0;
	g_cur_craft->ai_flight.turn_state = 0;
	paiman_setpower(g_pai_context.object_index, 0);
}

/* Returns 0; pai_is_plan_complete_for_order_slot ends waitpln on the timer. */
// FUNCTION: XVT 0x4A3CF0
int16_t paiman_waitmaneuver(void) { return 0; }

/* Starts drop off: stops the roll, pitch and turn and sets throttle 0. */
// FUNCTION: XVT 0x4A3D00
void paiman_initdropoffmaneuver(void)
{
	g_cur_craft->ai_flight.roll_state = 0;
	g_cur_craft->ai_flight.pitch_state = 0;
	g_cur_craft->ai_flight.turn_state = 0;
	paiman_setpower(g_pai_context.object_index, 0);
}

/* Delivers flight group variable2 minus 1 craft by craft; returns 0 on every
 * path. At maneuver_phase 0 it pushes the craft to the formation place
 * waypoint_index names, as mission_resolve_formation_slot_world_loc places it around
 * the last craft of that group with no leader, plus in Z the negated minimum Z
 * of the craft's own model bounds, turning to face it when more than 256 away
 * in X plus Y. Within 32 it starts that group's arrival for the place with
 * mission_start_flight_group_arrival, setting g_current_flight_group_idx and
 * g_spawn_leader_obj_idx, and goes to 1 for 1,180 ticks with a push of 1,500 in Z.
 * At 1, once the timer runs out, it goes back to 0 at the next place. Copies
 * waypoint_index into the order slot's goal_progress each think. With no leader
 * in that group it passes 255 as the basis, where
 * mission_resolve_formation_slot_world_loc tests for 0xFFFF. */
// FUNCTION: XVT 0x4A3D40
int16_t paiman_dropoffmaneuver(void)
{
	enum {
		TURN_DISTANCE_THRESHOLD = 256,
		ARRIVAL_DISTANCE_THRESHOLD = 32,
		TURN_STATE_ACTIVE = 2,
		FULL_TURN_STEP = INT16_MIN,
		DROPOFF_WAIT_TICKS = 1180,
		DROPOFF_PUSH_DISTANCE = 1500,
	};

	if (g_pai_context.controller->maneuver_phase == 0) {
		uint16_t formation_slot_index =
			g_pai_context.controller->waypoint_index;
		uint16_t destination_flight_group_index =
			(uint16_t)(g_mission_flight_groups
					   [g_pai_context
						    .craft_flight_group_index]
						   .fg
						   .orders[g_pai_context
								   .order_slot]
						   .variable2 -
				   1);
		if ((int)destination_flight_group_index >=
		    g_mission_header.num_flight_groups) {
			XVT_LOG_DEBUG(
				"ai.dropoff_group_invalid object=%d fg=%d order=%d group=%d groups=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)g_pai_context.order_slot,
				(int)g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg
						.orders[g_pai_context
								.order_slot]
						.variable2,
				(int)g_mission_header.num_flight_groups,
				g_flight_sim_side_effects_suppressed);
		}
		uint16_t basis_object_index = UINT8_MAX;

		for (uint16_t object_index =
			     (uint16_t)g_active_region_object_slot_start;
		     (int)object_index < g_active_region_craft_object_slot_end;
		     ++object_index) {
			struct object_record *object =
				&g_object_table[object_index];

			if (object->object_type != 0 &&
			    object->flight_group_idx ==
				    destination_flight_group_index &&
			    object->mobj->p_craft->leader_obj_idx ==
				    UINT8_MAX) {
				basis_object_index = object_index;
			}
		}

		mission_resolve_formation_slot_world_loc(
			destination_flight_group_index, formation_slot_index,
			basis_object_index);
		int min_z = -model_bounds_get_min_z(
			g_object_table[g_pai_context.object_index].object_type);
		g_cur_craft->push_accum_x =
			g_world_loc_x -
			g_object_table[g_pai_context.object_index].world_x;
		int distance_x = g_cur_craft->push_accum_x;
		g_cur_craft->push_accum_y =
			g_world_loc_y -
			g_object_table[g_pai_context.object_index].world_y;
		int distance_y = g_cur_craft->push_accum_y;
		g_cur_craft->push_accum_z =
			g_world_loc_z -
			g_object_table[g_pai_context.object_index].world_z;
		g_cur_craft->push_accum_z += min_z;
		int distance_z = g_cur_craft->push_accum_z;
		trig2_ctop(distance_x, distance_y, distance_z);

		if (distance_x < 0) {
			distance_x = -distance_x;
		}
		if (distance_y < 0) {
			distance_y = -distance_y;
		}
		if (distance_z < 0) {
			distance_z = -distance_z;
		}

		if (distance_x + distance_y > TURN_DISTANCE_THRESHOLD &&
		    g_object_table[g_pai_context.object_index].yaw !=
			    (uint16_t)trig2_xyangle) {
			g_cur_craft->ai_flight.turn_state = TURN_STATE_ACTIVE;
			g_cur_craft->ai_flight.turn_step = FULL_TURN_STEP;
			g_pai_context.controller->target_xy_angle =
				(uint16_t)trig2_xyangle;
		}
		if (distance_x + distance_y + distance_z <
		    ARRIVAL_DISTANCE_THRESHOLD) {
			struct pai_context saved_context = g_pai_context;

			g_current_flight_group_idx =
				destination_flight_group_index;
			struct craft_data *saved_craft = g_cur_craft;
			g_spawn_leader_obj_idx = (uint8_t)basis_object_index;
			mission_start_flight_group_arrival(
				formation_slot_index);
			g_cur_craft = saved_craft;
			g_pai_context = saved_context;
			++g_pai_context.controller->maneuver_phase;
			g_pai_context.controller->maneuver_timer =
				DROPOFF_WAIT_TICKS;
			g_cur_craft->push_accum_z = DROPOFF_PUSH_DISTANCE;
			XVT_LOG_DEBUG(
				"ai.dropoff_delivered object=%d fg=%d place=%d basis=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)destination_flight_group_index,
				(int)formation_slot_index,
				(int)basis_object_index,
				g_flight_sim_side_effects_suppressed);
		}
	} else if (g_pai_context.controller->maneuver_timer == 0) {
		g_pai_context.controller->maneuver_phase = 0;
		++g_pai_context.controller->waypoint_index;
		XVT_LOG_DEBUG("ai.dropoff_next object=%d place=%d predicted=%d",
			      (int)g_pai_context.object_index,
			      (int)g_pai_context.controller->waypoint_index,
			      g_flight_sim_side_effects_suppressed);
	}

	g_pai_context.controller->order_progress
		.goal_progress[g_pai_context.order_slot] =
		g_pai_context.controller->waypoint_index;
	return 0;
}

/* Starts kamikaze: moves the aim point to the target and steers at it at full
 * throttle. */
// FUNCTION: XVT 0x4A4050
void paiman_initkamikazemaneuver(void)
{
	pai_update_aim_point_from_order_target();
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
}

/* Moves the aim point to the target and steers at it; returns 0. */
// FUNCTION: XVT 0x4A4080
int16_t paiman_kamikazemaneuver(void)
{
	pai_update_aim_point_from_order_target();
	paiman_setflighttotarget(0, 1);
	return 0;
}

/* Starts avoid attacker: a quarter turn either way at random at half the
 * effective skill plus 0x8000, a pitch change of 0x3000 (plus from a pitch
 * below 0x4000, else minus), a roll that does not stop, its way random, for 10
 * to 17 times SIMULATION_TICKS_PER_SECOND, and the first weave after the flight
 * group AI level's delay. */
// FUNCTION: XVT 0x4A40A0
void paiman_initavoidattackermaneuver(void)
{
	g_pai_context.controller->maneuver_phase = game_rand() & 1;
	if (g_pai_context.controller->maneuver_phase) {
		g_pai_context.controller->target_xy_angle =
			(uint16_t)(g_object_table[g_pai_context.object_index]
					   .yaw +
				   0x4000u);
	} else {
		g_pai_context.controller->target_xy_angle =
			(uint16_t)(g_object_table[g_pai_context.object_index]
					   .yaw -
				   0x4000u);
	}

	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	unsigned int turn_step = effective_skill >> 1;
	turn_step += 0x8000u;
	paiman_setturn(turn_step);

	uint16_t pitch = g_object_table[g_pai_context.object_index].pitch;
	if (pitch < 0x4000u) {
		g_pai_context.controller->target_z_angle =
			(uint16_t)(pitch + 0x3000u);
		g_cur_craft->ai_flight.pitch_state = 2;
	} else {
		g_pai_context.controller->target_z_angle =
			(uint16_t)(pitch - 0x3000u);
		g_cur_craft->ai_flight.pitch_state = 1;
	}

	g_cur_craft->ai_flight.pitch_step_scale = UINT16_MAX;
	g_cur_craft->ai_flight.pitch_through_loop = 0;
	g_cur_craft->ai_flight.roll_state = 3;
	g_cur_craft->ai_flight.roll_step = UINT16_MAX;
	g_pai_context.controller->target_roll = (uint16_t)game_rand();
	g_pai_context.controller->maneuver_timer =
		SIMULATION_TICKS_PER_SECOND * ((game_rand() & 7) + 10);

	uint16_t group_ai =
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.group_ai;
	g_pai_context.controller->secondary_maneuver_timer =
		(int16_t)((uint16_t)math2_fraction(
				  g_ai_avoid_attacker_delay_frac_q16_by_group_ai
					  [group_ai],
				  0x00ECu) +
			  236u * g_ai_avoid_attacker_delay_seconds_by_group_ai
					  [group_ai]);
}

/* Returns 1, setting roll_state 4, once the maneuver timer has run out; else 0.
 * Each time secondary_maneuver_timer runs out it weaves the other way: a turn of
 * 0x3000 to 0x3FFF off its yaw, a flipped roll, a pitch change of 0x2000 to
 * 0x2FFF (plus from below 0x4000, else minus), and the next weave after the AI
 * level's delay plus up to 236 ticks; from AI level 3 it fires a flare on one
 * draw in eight when it has one and cm_fire_cooldown_timer is 0. */
// FUNCTION: XVT 0x4A4260
int16_t paiman_avoidattackermaneuver(void)
{
	if (g_pai_context.controller->maneuver_timer == 0) {
		g_cur_craft->ai_flight.roll_state = 4;
		return 1;
	}
	if (g_pai_context.controller->secondary_maneuver_timer == 0) {
		uint16_t random_angle = (uint16_t)(game_rand() & 0x0FFF);
		g_pai_context.controller->maneuver_phase ^= 1;
		if (g_pai_context.controller->maneuver_phase != 0) {
			g_pai_context.controller->target_xy_angle =
				(uint16_t)(g_object_table[g_pai_context
								  .object_index]
						   .yaw +
					   random_angle + 12288);
		} else {
			g_pai_context.controller->target_xy_angle =
				(uint16_t)(g_object_table[g_pai_context
								  .object_index]
						   .yaw -
					   random_angle - 12288);
		}
		paiman_setturn(
			(uint16_t)(pai_get_effective_skill_value(g_cur_craft) >>
				   1) +
			0x8000u);
		g_pai_context.controller->target_roll ^= 0x8000u;
		random_angle = (uint16_t)(game_rand() & 0x0FFF);
		int16_t pitch =
			g_object_table[g_pai_context.object_index].pitch;
		if ((uint16_t)pitch < 0x4000u) {
			g_pai_context.controller->target_z_angle =
				(uint16_t)(pitch + random_angle + 0x2000);
			g_cur_craft->ai_flight.pitch_state = 2;
		} else {
			g_pai_context.controller->target_z_angle =
				(uint16_t)(pitch - random_angle - 0x2000);
			g_cur_craft->ai_flight.pitch_state = 1;
		}
		uint16_t group_ai =
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.group_ai;
		g_pai_context.controller->secondary_maneuver_timer =
			(uint16_t)(SIMULATION_TICKS_PER_SECOND *
					   g_ai_avoid_attacker_delay_seconds_by_group_ai
						   [group_ai] +
				   math2_fraction(
					   g_ai_avoid_attacker_delay_frac_q16_by_group_ai
						   [group_ai],
					   0xEC));
		g_pai_context.controller->secondary_maneuver_timer +=
			math2_fraction((uint16_t)game_rand(), 0xEC);
		if (group_ai >= 3 && (game_rand() & 7) == 7 &&
		    g_cur_craft->cm_type_id == COUNTERMEASURE_TYPE_FLARE &&
		    g_cur_craft->cm_ammo_count != 0 &&
		    g_cur_craft->cm_fire_cooldown_timer == 0) {
			laser_createcountermeasureprojectile(
				g_pai_context.object_index,
				COUNTERMEASURE_PROJECTILE_OBJECT_TYPE);
		}
	}
	return 0;
}

/* Starts what paiman_initkamikazemaneuver starts: moves the aim point to the
 * target and steers at it at full throttle. */
// FUNCTION: XVT 0x4A4480
void paiman_initkamikazecopymaneuver(void)
{
	pai_update_aim_point_from_order_target();
	paiman_setflighttotarget(0, 1);
	paiman_setpower(g_pai_context.object_index, UINT16_MAX);
}

/* Does what paiman_kamikazemaneuver does: moves the aim point to the target and
 * steers at it; returns 0. */
// FUNCTION: XVT 0x4A44B0
int16_t paiman_kamikazecopymaneuver(void)
{
	pai_update_aim_point_from_order_target();
	paiman_setflighttotarget(0, 1);
	return 0;
}

/* Steers the craft at its aim point from its live position: target_xy_angle is
 * the heading there plus yaw_offset, with a turn step of half the effective
 * skill plus 0x4000. With steer_pitch nonzero it also pitches at full step
 * toward the pitch there, without a loop, and stops any climb or dive. Sets the
 * trig2_ globals; callers read trig2_polardistance as the distance to the aim
 * point. */
// FUNCTION: XVT 0x4A44D0
void paiman_setflighttotarget(uint16_t yaw_offset, int steer_pitch)
{
	int aim_x = g_pai_context.controller->aim_point_x;
	int aim_y = g_pai_context.controller->aim_point_y;
	int aim_z = g_pai_context.controller->aim_point_z;
	int world_x = g_object_table[g_pai_context.object_index].world_x;
	int world_y = g_object_table[g_pai_context.object_index].world_y;
	int world_z = g_object_table[g_pai_context.object_index].world_z;
	aim_x -= world_x;
	aim_y -= world_y;
	aim_z -= world_z;
	trig2_ctop(aim_x, aim_y, aim_z);
	g_pai_context.controller->target_xy_angle =
		(uint16_t)(yaw_offset + trig2_xyangle);
	uint16_t effective_skill = pai_get_effective_skill_value(g_cur_craft);
	effective_skill >>= 1;
	unsigned int turn_step = effective_skill;
	turn_step += 0x4000;
	paiman_setturn(turn_step);
	int update_pitch = steer_pitch;
	if (update_pitch != 0) {
		g_pai_context.controller->target_z_angle =
			(uint16_t)trig2_pitch;
		g_cur_craft->ai_flight.pitch_step_scale = UINT16_MAX;
		g_cur_craft->ai_flight.pitch_through_loop = 0;
		uint16_t pitch = g_cur_craft->pitch;
		if (pitch >= g_pai_context.controller->target_z_angle) {
			g_cur_craft->ai_flight.pitch_state = 1;
		} else {
			g_cur_craft->ai_flight.pitch_state = 2;
		}
		g_cur_craft->ai_flight.climb_state = 0;
		g_cur_craft->ai_flight.dive_state = 0;
	}
}

/* Levels the craft: no climb or dive, a pitch at full step to 0x4000 without a
 * loop (pitch_state by which side it is on, 3 when on it), a roll to 0 at full
 * step, and no turn. */
// FUNCTION: XVT 0x4A45E0
void paiman_initcruiseandrunawaycontrols(void)
{
	g_cur_craft->ai_flight.dive_state = 0;
	g_cur_craft->ai_flight.climb_state = 0;
	g_pai_context.controller->target_z_angle = 0x4000;
	g_cur_craft->ai_flight.pitch_step_scale = UINT16_MAX;
	g_cur_craft->ai_flight.pitch_through_loop = 0;
	uint16_t pitch = g_cur_craft->pitch;
	if (pitch < 0x4000) {
		g_cur_craft->ai_flight.pitch_state = 2;
	} else if (pitch > 0x4000) {
		g_cur_craft->ai_flight.pitch_state = 1;
	} else {
		g_cur_craft->ai_flight.pitch_state = 3;
	}
	g_cur_craft->ai_flight.roll_state = 1;
	g_cur_craft->ai_flight.roll_step = UINT16_MAX;
	g_pai_context.controller->target_roll = 0;
	g_cur_craft->ai_flight.turn_state = 0;
}

/* Steers the craft at its target: at the target's position in rocket attack or
 * for a target with no mobile object, else at the point ahead of it that
 * paiman_calcplanelead works out; target_xy_angle is the heading there plus
 * yaw_offset. It turns at half the effective skill plus 0x8000, with roll_state 2
 * only when 0x2000 or more off course or within 0x10000 (nothing sets roll_state
 * 2); stops a roll that does not stop; and when the pitch differs, pitches
 * toward it at full step at full throttle, ending any climb, dive or loop. Does
 * not check that target_obj_idx names an object. Sets the trig2_ globals. */
// FUNCTION: XVT 0x4A4690
void paiman_attacktarget(int16_t yaw_offset)
{
	unsigned int object_index = g_pai_context.object_index;
	uint16_t target_object_index;
	if (g_pai_context.controller->maneuver_mode ==
		    AI_MANEUVER_MODE_ROCKET_ATTACK ||
	    (target_object_index = g_pai_context.controller->target_obj_idx,
	     g_object_table[target_object_index].mobj == NULL)) {
		pai_update_aim_point_from_order_target();
	} else {
		paiman_calcplanelead(target_object_index);
	}
	int delta_x = g_pai_context.controller->aim_point_x -
		      g_object_table[object_index].world_x;
	int delta_y = g_pai_context.controller->aim_point_y -
		      g_object_table[object_index].world_y;
	int delta_z = g_pai_context.controller->aim_point_z -
		      g_object_table[object_index].world_z;
	trig2_ctop(delta_x, delta_y, delta_z);
	g_pai_context.controller->target_xy_angle = yaw_offset + trig2_xyangle;
	uint16_t effective_skill;
	if (g_cur_craft->ai_flight.roll_state == 2) {
		uint16_t yaw_difference =
			g_object_table[object_index].yaw -
			g_pai_context.controller->target_xy_angle;
		if (yaw_difference > 0x8000) {
			yaw_difference = (uint16_t)(0u - yaw_difference);
		}
		if (yaw_difference >= 0x2000 || trig2_polardistance < 0x10000) {
			effective_skill =
				pai_get_effective_skill_value(g_cur_craft);
			paiman_setturn((unsigned int)(effective_skill >> 1) +
				       0x8000);
		}
	} else {
		effective_skill = pai_get_effective_skill_value(g_cur_craft);
		paiman_setturn((unsigned int)(effective_skill >> 1) + 0x8000);
	}
	if (g_cur_craft->ai_flight.roll_state == 3) {
		g_cur_craft->ai_flight.roll_state = 0;
	}
	if (g_cur_craft->pitch != (uint16_t)trig2_pitch) {
		g_pai_context.controller->target_z_angle =
			(uint16_t)trig2_pitch;
		g_cur_craft->ai_flight.pitch_step_scale = UINT16_MAX;
		if (g_pai_context.controller->target_z_angle <=
		    g_cur_craft->pitch) {
			g_cur_craft->ai_flight.pitch_state = 1;
		} else {
			g_cur_craft->ai_flight.pitch_state = 2;
		}
		paiman_setpower(object_index, UINT16_MAX);
		g_cur_craft->ai_flight.climb_state = 0;
		g_cur_craft->ai_flight.dive_state = 0;
		g_cur_craft->ai_flight.pitch_through_loop = 0;
	}
}

/* Sets the aim point where the target will be when a shot reaches it: the
 * target's position plus its last frame's movement times a number of frames
 * (xvt_reference_motion_axis_displacement with unlocked timing). For a target
 * that is not moving that number is 0; else it is the frames the shot needs to
 * cover trig2_polardistance at the closing speed, scaled by the effective
 * skill. The closing speed adds the shot's speed (ion laser on disableldr1pln
 * with a live target, else the craft's first laser) and the craft's, then takes
 * off the target's speed times the cosine of the heading difference, or adds it
 * when the headings differ by a quarter turn or more. For a moving target it
 * sets the trig2_ globals. */
// FUNCTION: XVT 0x4A4840
void paiman_calcplanelead(int target_obj_idx)
{
	uint16_t lead_frames;

	if (g_object_table[target_obj_idx].mobj->speed == 0) {
		lead_frames = 0;
	} else {
		uint16_t object_index = g_pai_context.object_index;
		pai_object_ref_direction_to_object_ref(
			object_index, g_pai_context.controller->target_obj_idx);
		uint16_t projectile_type;
		if (strcmp(g_plan_table[g_pai_context.controller
						->current_plan_id]
				   .name,
			   "disableldr1pln") == 0 &&
		    g_pai_context.controller->has_live_target == 1) {
			projectile_type = PROJECTILE_OBJECT_TYPE_ION_LASER;
		} else if (g_cur_craft->laser_state.projectile_type_id[0] >
			   PROJECTILE_OBJECT_TYPE_FIRST) {
			projectile_type =
				g_cur_craft->laser_state.projectile_type_id[0];
		} else {
			projectile_type = PROJECTILE_OBJECT_TYPE_FIRST;
		}
		uint16_t projectile_speed =
			g_projectile_type_data
				.speed[projectile_type -
				       PROJECTILE_OBJECT_TYPE_FIRST];
		uint16_t combined_speed =
			projectile_speed +
			g_object_table[object_index].mobj->speed;
		uint16_t yaw_difference = g_object_table[target_obj_idx].yaw -
					  g_object_table[object_index].yaw;
		uint16_t target_speed =
			g_object_table[target_obj_idx].mobj->speed;
		if (yaw_difference >= 0x8000) {
			yaw_difference = (uint16_t)(0u - yaw_difference);
		}
		if (yaw_difference < 0x4000) {
			combined_speed -= trig2_cosinewordmult(target_speed,
							       yaw_difference);
		} else {
			combined_speed += trig2_cosinewordmult(target_speed,
							       yaw_difference);
		}
		uint16_t closing_units_per_second =
			combined_speed / 5u + 18 * combined_speed;
		if (closing_units_per_second == 0) {
			closing_units_per_second = 19;
		}
		uint16_t travel_frames =
			g_sim_steps_per_second *
			(trig2_polardistance / closing_units_per_second);
		uint16_t effective_skill =
			pai_get_effective_skill_value(g_cur_craft);
		lead_frames = math2_fraction(travel_frames, effective_skill);
	}
	struct mobile_object *target_mobile =
		g_object_table[target_obj_idx].mobj;
	int delta_y;
	delta_y = (xvt_flight_timing_is_unlocked()
			   ? xvt_reference_motion_axis_displacement(
				     target_obj_idx, 1)
			   : (g_object_table[target_obj_idx].world_y -
			      target_mobile->prev_world_y));
	int delta_z;
	delta_z = (xvt_flight_timing_is_unlocked()
			   ? xvt_reference_motion_axis_displacement(
				     target_obj_idx, 2)
			   : (g_object_table[target_obj_idx].world_z -
			      target_mobile->prev_world_z));
	g_pai_context.controller->aim_point_x =
		g_object_table[target_obj_idx].world_x +
		lead_frames *
			((xvt_flight_timing_is_unlocked()
				  ? xvt_reference_motion_axis_displacement(
					    target_obj_idx, 0)
				  : (g_object_table[target_obj_idx].world_x -
				     target_mobile->prev_world_x)));
	g_pai_context.controller->aim_point_y =
		g_object_table[target_obj_idx].world_y + lead_frames * delta_y;
	g_pai_context.controller->aim_point_z =
		g_object_table[target_obj_idx].world_z + lead_frames * delta_z;
}

/* Pushes the craft toward its formation place around its leader: the offsets
 * g_form_pos_x, g_form_pos_y and g_form_pos_z give for its craft_ordinal, less those
 * of place 0, times the leader's separation plus 1 and the model's bound sizes
 * (half a bound more at separation 0), divided by g_formation_divisor, turned to
 * the leader's axes and shifted by the model's bound_size_shift. Does not check
 * formation_type below 34 or craft_ordinal below 6. */
// FUNCTION: XVT 0x4A4A10
void paiman_calcformation(void)
{
	uint16_t model_index = g_cur_craft->model_index;
	int16_t bound_size_y = g_model_defs[model_index].bound_size_y;
	int16_t bound_size_x = g_model_defs[model_index].bound_size_x;
	int formation_type = g_cur_craft->ai_flight.formation_type;
	int16_t bound_size_z = g_model_defs[model_index].bound_size_z;
	int16_t separation =
		g_pai_context.leader_or_self_craft->ai_flight.separation + 1;
	int formation_index = g_cur_craft->craft_ordinal;
	int16_t offset_x = g_form_pos_x[formation_type][formation_index] -
			   g_form_pos_x[formation_type][0];
	int16_t offset_y = g_form_pos_y[formation_type][formation_index] -
			   g_form_pos_y[formation_type][0];
	int16_t offset_z = g_form_pos_z[formation_type][formation_index] -
			   g_form_pos_z[formation_type][0];
	int16_t displacement_x =
		(int16_t)(separation * offset_x) * bound_size_x;
	int16_t displacement_y =
		(int16_t)(separation * offset_y) * bound_size_y;
	int16_t displacement_z =
		(int16_t)(separation * offset_z) * bound_size_z;
	if (separation == 1) {
		displacement_x += offset_x * (bound_size_x / 2);
		displacement_z += offset_z * (bound_size_z / 2);
		displacement_y += offset_y * (bound_size_y / 2);
	}
	int16_t divisor = g_formation_divisor[formation_type];
	if (divisor != 1) {
		displacement_x /= divisor;
		displacement_y /= divisor;
		displacement_z /= divisor;
	}
	pai_calcrotatedpoint(&g_object_table[g_pai_context.leader_object_index],
			     displacement_x, displacement_z, displacement_y);
	uint16_t bound_size_shift = g_model_defs[model_index].bound_size_shift;
	if (bound_size_shift != 0) {
		g_rotated_x <<= bound_size_shift;
		g_rotated_y <<= bound_size_shift;
		g_rotated_z <<= bound_size_shift;
	}
	g_cur_craft->push_accum_x =
		g_rotated_x +
		g_object_table[g_pai_context.leader_object_index].world_x -
		g_object_table[g_pai_context.object_index].world_x;
	g_cur_craft->push_accum_y =
		g_rotated_y +
		g_object_table[g_pai_context.leader_object_index].world_y -
		g_object_table[g_pai_context.object_index].world_y;
	g_cur_craft->push_accum_z =
		g_rotated_z +
		g_object_table[g_pai_context.leader_object_index].world_z -
		g_object_table[g_pai_context.object_index].world_z;
}

/* Starts a turn to target_xy_angle: within 0x300 of it, snaps the yaw there,
 * marks the move vector and axes for rebuilding and sets turn_state 3; else sets
 * turn_state 2 with turn_step, cut to 16 bits. */
// FUNCTION: XVT 0x4A4CB0
void paiman_setturn(int turn_step)
{
	uint16_t *yaw = &g_object_table[g_pai_context.object_index].yaw;
	uint16_t target_yaw = g_pai_context.controller->target_xy_angle;
	uint16_t yaw_difference = *yaw - target_yaw;
	if (yaw_difference >= 0x8000u) {
		yaw_difference = -yaw_difference;
	}
	if (yaw_difference <= 0x300u) {
		*yaw = target_yaw;
		g_object_table[g_pai_context.object_index]
			.mobj->orient_matrix_dirty = 1;
		struct mobile_object *mobile_object =
			g_object_table[g_pai_context.object_index].mobj;
		mobile_object->move_vector_dirty =
			mobile_object->orient_matrix_dirty;
		g_cur_craft->ai_flight.turn_state = 3;
	} else {
		g_cur_craft->ai_flight.turn_state = 2;
		g_cur_craft->ai_flight.turn_step = turn_step;
	}
}

/* Sets g_cur_craft's throttle, a fraction of 65,535. The object index is
 * ignored. */
// FUNCTION: XVT 0x4A4D70
void paiman_setpower(int ignored_obj_idx, int throttle)
{
	(void)ignored_obj_idx;
	g_cur_craft->throttle_speed = throttle;
}

/* Sets the throttle that gives desired_speed. The full speed is the object's
 * max_speed_cache, raised by an eighth for each step the sum of its shield, beam
 * and laser recharge levels falls below 6, or lowered by an eighth for each
 * step above; full throttle when desired_speed reaches it, else desired_speed
 * over it. The throttle goes to g_cur_craft through paiman_setpower, not to the
 * object's craft. */
// FUNCTION: XVT 0x4A4D90
void paiman_setspeed(int obj_idx, unsigned int desired_speed)
{
	struct craft_data *craft = g_object_table[obj_idx].mobj->p_craft;
	uint16_t power_delta =
		(uint16_t)(6 - (uint8_t)craft->shield_recharge_level -
			   (uint8_t)craft->beam_recharge_level -
			   (uint8_t)craft->laser_recharge_level);
	int16_t max_speed_cache;
	uint16_t adjusted_max_speed;
	if (power_delta >= 0x8000u) {
		power_delta = (uint16_t)-power_delta;
		power_delta <<= 13;
		max_speed_cache = craft->ai_flight.max_speed_cache;
		adjusted_max_speed =
			(uint16_t)(max_speed_cache -
				   (uint16_t)math2_fraction(
					   power_delta,
					   (uint16_t)max_speed_cache));
	} else {
		power_delta <<= 13;
		max_speed_cache = craft->ai_flight.max_speed_cache;
		adjusted_max_speed =
			(uint16_t)(max_speed_cache +
				   (uint16_t)math2_fraction(
					   power_delta,
					   (uint16_t)max_speed_cache));
	}
	if (adjusted_max_speed <= desired_speed) {
		paiman_setpower(obj_idx, 0xFFFF);
	} else {
		paiman_setpower(obj_idx,
				math2_ratio_q16((uint16_t)desired_speed,
						adjusted_max_speed));
	}
}
