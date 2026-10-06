#include "xvt/flight/ai/pai.h"

#include <limits.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/flight/ai/pai_targetability.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/log/log.h"

/* The loaded AI plans; a plan id is an index into it. Each entry holds the
 * plan's name, whether the plan text defined it, and where its bytes start in
 * g_plan_order_data. pai_loadplans clears it and fills it from the .plo file, or
 * pai_compile_plans_from_text fills it from the .pln text. The world state saves
 * and restores it: flight_save_world_state and flight_restore_world_state in the
 * original build, xvt_snapshot_encode and xvt_snapshot_decode_prefix in the modern
 * one. */
// GLOBAL: XVT 0x9D1320
struct pai_plan_record g_plan_table[256];

/* The craft the AI is thinking for and values taken from it, shared by the
 * plan, order and maneuver code. pai_setupcraftcontext fills it for one craft;
 * the order and target search functions then change some of its fields. Many
 * functions write it, chiefly pai_setupcraftcontext. */
// GLOBAL: XVT 0x9A73B0
struct pai_context g_pai_context;
/* 1 once paiorder_completegootherorder or paiorder_orderswitchorder has tested
 * the flight group's skip-to-order-4 trigger pair during the current
 * pai_process_plan pass and found it false; both skip the test while it is 1.
 * pai_process_plan sets it to 0 when a pass starts and when a pass switches the
 * plan. */
// GLOBAL: XVT 0x9A8C28
int g_pai_skip_to_order4_checked = 0;
/* The last rough distance a range test worked out, in world units; callers read
 * it right after the call that sets it. pai_object_ref_update_rough_distance and
 * pai_is_object_within_range_of_craft take the larger of the X and Y offsets plus
 * half the smaller, then add half the Z offset when that sum is the larger,
 * else halve the sum and add the Z offset. Many functions write it, chiefly
 * those two. */
// GLOBAL: XVT 0x9A1FF8
int g_last_rough_distance = 0;
/* AI skill as a fraction of 65,536 for each flight group AI level:
 * mission_init_flight_group_object_slot copies the entry for g_spawn_group_ai into a
 * new craft's ai_skill. Levels 4 and 5 hold 0xFFFF, which math2_fraction treats
 * as a whole; entries 6 and 7 are 0. pai_is_object_targetable_near_craft and
 * pai_is_object_within_skill_range_of_craft index it by the skill tier, 0 to 2,
 * instead. Nothing writes it. */
// GLOBAL: XVT 0x524100
uint16_t g_ai_skill_value_q16_by_level[8] = {0x0000, 0x4000, 0x8000, 0xC000,
					     0xFFFF, 0xFFFF, 0x0000, 0x0000};
/* Ticks between two AI thinks of a craft for each flight group AI level, from
 * 708 down to 29; mission_init_flight_group_object_slot copies the entry into the
 * craft's think_interval. Entries 6 and 7 are 0. */
// GLOBAL: XVT 0x524110
const uint16_t g_ai_think_interval_by_skill[8] = {
	708, 472, SIMULATION_TICKS_PER_SECOND, 118, 59, 29, 0, 0};
/* For each plan id from 0 to 73, the index in g_str_in_flight_messages of the
 * status line a craft running that plan reports; flight_process_player_actions
 * and the HUD's target displays read it. */
// GLOBAL: XVT 0x5272F8
const uint8_t
	g_plan_report_message_id_by_plan_id[PAI_PLAN_REPORT_MESSAGE_COUNT] = {
		159, 159, 159, 183, 178, 186, 183, 178, 183, 178, 183, 178, 162,
		164, 166, 163, 162, 165, 165, 167, 164, 166, 165, 165, 168, 169,
		164, 166, 165, 169, 164, 166, 165, 170, 173, 170, 170, 171, 170,
		172, 170, 174, 189, 189, 174, 175, 174, 176, 177, 187, 181, 179,
		180, 182, 181, 159, 178, 183, 183, 184, 185, 161, 161, 183, 183,
		187, 188, 190, 170, 162, 191, 191, 191, 0,
};
/* The 75 plan names the code refers to by number, in that order, ended by an
 * empty name. Only pai_cache_builtin_plan_ids reads it. */
// GLOBAL: XVT 0x525150
const char *const g_builtin_plan_name_table[76] = {
	"nullpln",
	"stationaryldrpln",
	"stationaryflwpln",
	"formldr1pln",
	"formflw1pln",
	"formevadeldr1pln",
	"formevadeflw1pln",
	"capldr1pln",
	"capescortersldr1pln",
	"caprespondldr1pln",
	"capldr2pln",
	"capldr3pln",
	"capldr4pln",
	"capldr5pln",
	"capflw1pln",
	"capflw2pln",
	"capflw3pln",
	"capflw4pln",
	"capflw5pln",
	"disableldr1pln",
	"escortldr1pln",
	"escortldr2pln",
	"escortldr3pln",
	"escortldr4pln",
	"escortflw1pln",
	"escortflw2pln",
	"escortflw3pln",
	"escortflw4pln",
	"boardtogivepln",
	"boardtotakepln",
	"boardtoexchangepln",
	"boardtocapturepln",
	"boardtodestroypln",
	"boardtopickuppln",
	"boardtocontactpln",
	"board2pln",
	"board3pln",
	"dropoffldr1pln",
	"dropoffldr2pln",
	"rendezvous1pln",
	"rendezvous2pln",
	"rendezvousflw1pln",
	"disabledpln",
	"waitforboardpln",
	"craftwaitforgopln",
	"flyhomepln",
	"followhomepln",
	"flyhomeevadepln",
	"followhomeevadepln",
	"enterhangarpln",
	"exithangarpln",
	"intohyperspacepln",
	"outofhyperspacepln",
	"starshipintohyperpln",
	"starshipfollowhomepln",
	"starshipstatpln",
	"starshipformpln",
	"starshipfollowpln",
	"starshipwaitreturnpln",
	"starshipwaitcreatepln",
	"starshipprotectpln",
	"starshipescortpln",
	"starshipattackpln",
	"starshipdisablepln",
	"starshipwaitforgopln",
	"variablepln",
	"waitpln",
	"selfdestroypln",
	"boardtorepairpln",
	"capfreeldr1pln",
	"kamikaze1pln",
	"kamikaze2pln",
	"kamikaze3pln",
	"playercontrolledpln",
	"",
};
/* The target tokens of the plan text and the byte each compiles to, ended by an
 * empty name. NOTARGET compiles to 255, as NULLTARGET does. Only
 * pai_find_target_token_index and pai_compile_plans_from_text read it. */
// GLOBAL: XVT 0x525280
static struct pai_plan_token_def g_pai_target_token_defs[10] = {
	{"LOCATARGET", 249},
	{"LOCBTARGET", 250},
	{"ABORTTARGET", AI_TARGET_ABORT},
	{"NORMALTARGET", 252},
	{"PRIMARYTARGET", 253},
	{"HOMETARGET", 254},
	{"NULLTARGET", 255},
	{"NOTARGET", -1},
	{"0x80", 128},
	{"", 0},
};
/* The maneuver tokens of the plan text and the maneuver mode each compiles to,
 * ended by an empty name. No token names AI_MANEUVER_MODE_AVOID_ATTACKER or
 * AI_MANEUVER_MODE_KAMIKAZE_COPY. Only pai_find_maneuver_token_index and
 * pai_compile_plans_from_text read it. */
// GLOBAL: XVT 0x5255B8
static struct pai_plan_token_def g_pai_maneuver_token_defs[33] = {
	{"NULLMANR", AI_MANEUVER_MODE_NULL},
	{"TURNINSIDEMANR", AI_MANEUVER_MODE_TURN_INSIDE},
	{"SPLITSMANR", AI_MANEUVER_MODE_SPLITS},
	{"IMMELMANNMANR", AI_MANEUVER_MODE_IMMELMANN},
	{"SCISSORSMANR", AI_MANEUVER_MODE_SCISSORS},
	{"RENDEZVOUSMANR", AI_MANEUVER_MODE_RENDEZVOUS},
	{"CRUISEMANR", AI_MANEUVER_MODE_CRUISE},
	{"HEADTOWARDFULLMANR", AI_MANEUVER_MODE_HEAD_TOWARD_FULL},
	{"RUNAWAYMANR", AI_MANEUVER_MODE_RUN_AWAY},
	{"HEADONATTACKMANR", AI_MANEUVER_MODE_HEAD_ON_ATTACK},
	{"FOLLOWLEADERMANR", AI_MANEUVER_MODE_FOLLOW_LEADER},
	{"SETUPATTACKMANR", AI_MANEUVER_MODE_SETUP_ATTACK},
	{"ATTACKMANR", AI_MANEUVER_MODE_ATTACK},
	{"ZOOMMANR", AI_MANEUVER_MODE_ZOOM},
	{"DIVEMANR", AI_MANEUVER_MODE_DIVE},
	{"SPLITSDIVEMANR", AI_MANEUVER_MODE_SPLITS_DIVE},
	{"SPEEDAWAYMANR", AI_MANEUVER_MODE_SPEED_AWAY},
	{"ESCORTMANR", AI_MANEUVER_MODE_ESCORT},
	{"BOARDMANR", AI_MANEUVER_MODE_BOARD},
	{"AWAITBOARDMANR", AI_MANEUVER_MODE_AWAIT_BOARD},
	{"HEADTOWARDMANR", AI_MANEUVER_MODE_HEAD_TOWARD},
	{"INTOHYPERSPACEMANR", AI_MANEUVER_MODE_INTO_HYPERSPACE},
	{"OUTOFHYPERSPACEMANR", AI_MANEUVER_MODE_OUT_OF_HYPERSPACE},
	{"ROCKETATTACKMANR", AI_MANEUVER_MODE_ROCKET_ATTACK},
	{"TURNAWAYMANR", AI_MANEUVER_MODE_TURN_AWAY},
	{"STOPMANR", AI_MANEUVER_MODE_STOP},
	{"OUTOFHANGARMANR", AI_MANEUVER_MODE_OUT_OF_HANGAR},
	{"EVASIVEMANR", AI_MANEUVER_MODE_EVASIVE},
	{"AVOIDSTARSHIPMANR", AI_MANEUVER_MODE_AVOID_STARSHIP},
	{"WAITMANR", AI_MANEUVER_MODE_WAIT},
	{"DROPOFFMANR", AI_MANEUVER_MODE_DROPOFF},
	{"KAMIKAZEMANR", AI_MANEUVER_MODE_KAMIKAZE},
	{"", 0},
};
/* The order tokens of the plan text and the g_order_table index each compiles
 * to, 0 to 47, ended by an empty name; NULLORDR (0) ends a plan. Only
 * pai_find_order_token_index and pai_compile_plans_from_text read it. */
// GLOBAL: XVT 0x526050
static struct pai_plan_token_def g_pai_order_token_defs[49] = {
	{"NULLORDR", 0},
	{"UPDATECOURSEORDR", 1},
	{"UNDERATTACKORDR", 2},
	{"STILLATTACKORDR", 3},
	{"FLYHOMEORDR", 4},
	{"FIGHTERSHOOTORDR", 5},
	{"GUNNERSELFDEFENSEORDR", 6},
	{"GUNNEROFFENSEORDR", 7},
	{"MISSILEDEFENSEORDR", 8},
	{"SCANFORTARGETORDR", 9},
	{"WAITRUNORDR", 10},
	{"BREAKOFFORDR", 11},
	{"LEADERDEADORDR", 12},
	{"COVERLEADERORDR", 13},
	{"FOLLOWLEADATKORDR", 14},
	{"ABORTATKORDR", 15},
	{"ONTAILORDR", 16},
	{"ALWAYSORDR", 17},
	{"CHECKESCORTORDR", 18},
	{"LEADERGOHOMEORDR", 19},
	{"HYPERSPACEORDR", 20},
	{"ENTERHANGARORDR", 21},
	{"MOTHERSHIPORDR", 22},
	{"ESCORTTARGETORDR", 23},
	{"LOOKFORDISABLEORDR", 24},
	{"ABORTBOARDORDR", 25},
	{"RETURNBOARDORDR", 26},
	{"AWAITBOARDORDR", 27},
	{"MAKEDISABLEDORDR", 28},
	{"NEARTARGETORDR", 29},
	{"ROCKETSONBOARDORDR", 30},
	{"AVOIDHITORDR", 31},
	{"WAITFORALLRETURNORDR", 32},
	{"WAITFORALLCREATEORDR", 33},
	{"EVASIVEORDR", 34},
	{"NEWTARGETORDR", 35},
	{"AVOIDSTARSHIPORDR", 36},
	{"CHECKHYPERORDR", 37},
	{"STOPGOHOMEORDR", 38},
	{"COMPLETEGOHOMEORDR", 39},
	{"COMPLETEGOOTHERORDR", 40},
	{"COMPLETEFOLLOWORDR", 41},
	{"WAITGOOTHERORDR", 42},
	{"ORDERSWITCHORDR", 43},
	{"KILLSELFORDR", 44},
	{"DROPOFFDESTORDR", 45},
	{"ABORTMOTHERWAITORDR", 46},
	{"PLAYERINPUTORDR", 47},
	{"", 0},
};
/* For each plan id, where the plan's bytes start in g_plan_order_data. Written by
 * pai_compile_plans_from_text and by pai_loadplans, which first zeroes only its
 * first 256 bytes, not the whole array. */
// GLOBAL: XVT 0xA07CF0
uint8_t *g_plan_data_ptrs[256];
/* The compiled plans, back to back. Each plan is a target byte, a maneuver
 * byte, then pairs of an order id and the plan id to switch to when that order
 * fires, ended by order 0. pai_compile_plans_from_text writes it from the .pln
 * text; pai_loadplans reads it from the .plo file. The world checksum covers it
 * in both builds. */
// GLOBAL: XVT 0x9A8E40
uint8_t g_plan_order_data[0x20000] = {0};
/* Plans loaded. pai_loadplans sets it to 0, then, after reading the .plo file,
 * to the number of named entries in g_plan_table; pai_compile_plans_from_text adds
 * 1 for each plan it compiles. Beyond that, only the world state save, restore
 * and checksum use it. */
// GLOBAL: XVT 0x9A8068
int g_plan_count = 0;
/* For each name in g_builtin_plan_name_table, the plan id loaded under that name,
 * or 0 when no plan has it. pai_cache_builtin_plan_ids fills it at flight start;
 * the world state saves and restores it in both builds. */
// GLOBAL: XVT 0x9A7A40
uint8_t g_builtin_plan_id_by_name_index[256] = {0};
/* For each mission order, 0 to 39, the g_builtin_plan_name_table index of the
 * order's leader plan: the plan a craft with no leader flies, and the one
 * mission_init_flight_group_object_slot stores as every new craft's current_plan_id.
 * Nothing writes it. */
// GLOBAL: XVT 0x524120
uint8_t g_order_leader_builtin_plan_name_index[40] = {
	0x01, 0x2f, 0x03, 0x05, 0x27, 0x2a, 0x2b, 0x45, 0x08, 0x09,
	0x14, 0x13, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x25, 0x42,
	0x42, 0x38, 0x3a, 0x3b, 0x3c, 0x3c, 0x3e, 0x3f, 0x01, 0x35,
	0x01, 0x22, 0x44, 0x01, 0x01, 0x01, 0x43, 0x46, 0x01, 0x00,
};
/* For each mission order, 0 to 39, the g_builtin_plan_name_table index of the plan
 * the order gives a craft that follows a leader. */
// GLOBAL: XVT 0x524148
const uint8_t g_order_follower_builtin_plan_name_index[40] = {
	0x02, 0x30, 0x04, 0x06, 0x29, 0x2a, 0x2b, 0x0e, 0x0e, 0x0e,
	0x18, 0x0e, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x04, 0x42,
	0x42, 0x39, 0x3a, 0x3b, 0x39, 0x39, 0x39, 0x39, 0x02, 0x36,
	0x02, 0x22, 0x44, 0x01, 0x01, 0x01, 0x43, 0x46, 0x01, 0x00,
};

/* Runs one AI think for each craft whose think timer has run out. It walks the
 * active region's craft slots and skips empty slots, objects whose mobile
 * object family is not 0, craft breaking up or exploding, and craft whose
 * think_timer is above 0. For a craft no player flies it sets up g_pai_context,
 * loads the craft's own random seed into g_game_rand_feedback_state, runs
 * pai_process_plan and stores the seed back. Every craft it does not skip,
 * player craft too, then gets think_interval added to its think_timer. Restores
 * g_game_rand_feedback_state at the end; leaves g_cur_craft at the last craft
 * visited. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4028A0
void pai_update_all_craft_ai(void)
{
	int16_t saved_rand_state = g_game_rand_feedback_state;
	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		struct object_record *object = &g_object_table[object_index];

		if (object->object_type == 0) {
			continue;
		}
		struct mobile_object *mobile_object = object->mobj;
		if (mobile_object->family != 0) {
			continue;
		}
		g_cur_craft = mobile_object->p_craft;
		struct ai_controller *controller = &g_cur_craft->ai_controller;
		if (g_cur_craft->object_kind == CRAFT_OBJECT_KIND_BREAKING_UP ||
		    g_cur_craft->object_kind == CRAFT_OBJECT_KIND_EXPLODING ||
		    controller->think_timer > 0) {
			continue;
		}
		if (object->player_owner_idx == -1) {
			pai_setupcraftcontext(object_index);
			g_game_rand_feedback_state =
				controller->saved_rand_seed;
			pai_process_plan();
			controller->saved_rand_seed =
				g_game_rand_feedback_state;
		}
		controller->think_timer += controller->think_interval;
	}
	g_game_rand_feedback_state = saved_rand_state;
}

/* Starts g_cur_craft's running_plan_id plan. When the plan's target byte is not
 * 255 it points target_obj_idx at a mission point of the flight group of the
 * object g_pai_context.object_index names: for LOCATARGET (249) point 12; for
 * PRIMARYTARGET (253) point 12 when enabled; for HOMETARGET (254) point 12 when
 * captured_by_flight_group is set and point 12 is enabled, else point 13 when
 * enabled; for any other byte the point waypoint_index names when enabled. Where
 * the point is not enabled it uses 0x8000, the group's current point. It then
 * clears target_signature and has_live_target and sets the aim point there. When
 * the maneuver byte is not 255 it sets maneuver_mode and runs
 * paiman_initmaneuver. Always clears secondary_maneuver_timer, sets
 * last_attacker_obj_idx and ai_flight.threat_obj_idx to 0xFFFF and
 * last_hit_mission_second to 0, and sets think_timer to (object_idx mod 8) eighths
 * of think_interval, which spreads craft thinks over the interval. Does not set
 * current_plan_id. */
// FUNCTION: XVT 0x402970
void pai_apply_running_plan_target_and_maneuver(unsigned int object_idx)
{
	struct ai_controller *controller = &g_cur_craft->ai_controller;
	uint8_t *plan_data = g_plan_data_ptrs[controller->running_plan_id];
	uint16_t target_token = *plan_data++;

	if (target_token != 0xFFu) {
		if (target_token == 0xFDu) {
			if (g_mission_flight_groups
				    [g_object_table[g_pai_context.object_index]
					     .flight_group_idx]
					    .fg.mission_point_enabled[12] !=
			    0) {
				controller->target_obj_idx = 0x800Cu;
			} else {
				controller->target_obj_idx = 0x8000u;
			}
		} else if (target_token == 0xFEu) {
			if (g_cur_craft->captured_by_flight_group != 0 &&
			    g_mission_flight_groups
					    [g_object_table
						     [g_pai_context
							      .object_index]
							     .flight_group_idx]
						    .fg.mission_point_enabled
							    [12] != 0) {
				controller->target_obj_idx = 0x800Cu;
			} else if (g_mission_flight_groups
					   [g_object_table
						    [g_pai_context.object_index]
							    .flight_group_idx]
						   .fg
						   .mission_point_enabled[13] !=
				   0) {
				controller->target_obj_idx = 0x800Du;
			} else {
				controller->target_obj_idx = 0x8000u;
			}
		} else if (target_token == 0xF9u) {
			controller->target_obj_idx = 0x800Cu;
		} else {
			if (g_mission_flight_groups
				    [g_object_table[g_pai_context.object_index]
					     .flight_group_idx]
					    .fg.mission_point_enabled
						    [controller
							     ->waypoint_index] !=
			    0) {
				controller->target_obj_idx =
					(uint16_t)(0x8000u +
						   controller->waypoint_index);
			} else {
				controller->target_obj_idx = 0x8000u;
			}
		}

		controller->target_signature = 0;
		controller->has_live_target = 0;
		if (controller->target_obj_idx != UINT16_MAX) {
			pai_update_aim_point_from_order_target();
		}
	}

	controller->secondary_maneuver_timer = 0;
	uint8_t maneuver_token = *plan_data;
	if (maneuver_token != UINT8_MAX) {
		controller->maneuver_mode = maneuver_token;
		paiman_initmaneuver();
	}

	g_cur_craft->last_attacker_obj_idx = UINT16_MAX;
	g_cur_craft->last_hit_mission_second = 0;
	g_cur_craft->ai_flight.threat_obj_idx = UINT16_MAX;
	controller->think_timer =
		((object_idx & 7) * controller->think_interval) >> 3;
	XVT_LOG_DEBUG(
		"ai.plan_started object=%d fg=%d plan=%d target_code=%d target=%d x=%d y=%d z=%d maneuver=%d tier=%d timer=%d same=%d predicted=%d",
		(int)object_idx, (int)g_pai_context.craft_flight_group_index,
		(int)controller->running_plan_id, (int)target_token,
		(int)controller->target_obj_idx, controller->aim_point_x,
		controller->aim_point_y, controller->aim_point_z,
		(int)controller->maneuver_mode, (int)g_pai_context.skill_tier,
		controller->think_timer,
		(int)(g_cur_craft == g_pai_context.craft),
		g_flight_sim_side_effects_suppressed);
}

/* Runs the orders of the plan set up in g_pai_context. It calls each order's
 * handler in turn and switches to the first order's plan whose handler returns
 * nonzero and whose plan is not nullpln; "variablepln" stands for the plan in
 * g_pai_context.variable_plan_id. Switching sets running_plan_id, sets up the
 * context again and runs pai_apply_running_plan_target_and_maneuver. Returns at
 * order 0 without a switch. Sets g_pai_skip_to_order4_checked to 0 when it starts
 * and when it switches. A player craft on escortldr1pln first runs
 * paifight_checkescortorder, but pai_update_all_craft_ai, the only caller, never
 * passes a player craft. */
// FUNCTION: XVT 0x402B60
void pai_process_plan(void)
{
	struct ai_controller *controller = &g_cur_craft->ai_controller;
	if (g_object_table[g_pai_context.object_index].player_owner_idx != -1 &&
	    strcmp(g_plan_table[controller->current_plan_id].name,
		   "escortldr1pln") == 0) {
		paifight_checkescortorder();
	}

	g_pai_skip_to_order4_checked = 0;
	uint8_t order_id = *g_pai_context.plan_cursor++;
	if (order_id == 0) {
		return;
	}

	while (g_order_table[order_id]() == 0 ||
	       strcmp(g_plan_table[*g_pai_context.plan_cursor].name,
		      "nullpln") == 0) {
		++g_pai_context.plan_cursor;
		order_id = *g_pai_context.plan_cursor++;
		if (order_id == 0) {
			return;
		}
	}
	XVT_LOG_DEBUG(
		"ai.plan_switched object=%d handler=%d from=%d named=%d variable=%d order=%d leader_plan=%d predicted=%d",
		(int)g_pai_context.object_index, (int)order_id,
		(int)controller->running_plan_id,
		(int)*g_pai_context.plan_cursor,
		(int)g_pai_context.variable_plan_id,
		(int)controller->current_order_slot,
		(int)controller->current_plan_id,
		g_flight_sim_side_effects_suppressed);

	if (strcmp(g_plan_table[*g_pai_context.plan_cursor].name,
		   "variablepln") == 0) {
		controller->running_plan_id = g_pai_context.variable_plan_id;
	} else {
		controller->running_plan_id = *g_pai_context.plan_cursor;
	}

	pai_setupcraftcontext(g_pai_context.object_index);
	pai_apply_running_plan_target_and_maneuver(g_pai_context.object_index);
	if (g_pai_skip_to_order4_checked == 1) {
		g_pai_skip_to_order4_checked = 0;
	}
}

/* Fills g_pai_context for one craft object: its index, craft and controller, its
 * leader's index (255 for none) and the leader's craft or its own, its flight
 * group, current order slot, world position and skill tier, and a plan cursor
 * at the first order of the running_plan_id plan, with that plan's maneuver byte
 * in initial_maneuver_id. Clears require_undisabled_target and sets variable_plan_id
 * to nullpln's plan id. Also sets g_world_loc_x, g_world_loc_y and g_world_loc_z to
 * the craft's position. Leaves target_search_flags and the search origin alone.
 * Does not check that the object is a craft. */
// FUNCTION: XVT 0x402CB0
void pai_setupcraftcontext(uint16_t object_idx)
{
	g_pai_context.object_index = object_idx;
	struct object_record *object = &g_object_table[object_idx];
	g_pai_context.craft = object->mobj->p_craft;
	g_pai_context.leader_object_index =
		(uint8_t)g_pai_context.craft->leader_obj_idx;
	struct ai_controller *controller = &g_pai_context.craft->ai_controller;
	g_pai_context.controller = controller;
	struct craft_data *leader_or_self_craft;
	if (g_pai_context.leader_object_index == UINT8_MAX) {
		leader_or_self_craft = object->mobj->p_craft;
	} else {
		leader_or_self_craft =
			g_object_table[g_pai_context.leader_object_index]
				.mobj->p_craft;
	}
	g_pai_context.leader_or_self_craft = leader_or_self_craft;
	g_pai_context.craft_flight_group_index = object->flight_group_idx;
	g_pai_context.order_slot = controller->current_order_slot;
	mission_resolve_object_or_mission_point_world_loc(
		object_idx, g_pai_context.craft_flight_group_index);
	g_pai_context.craft_position_x = g_world_loc_x;
	g_pai_context.craft_position_y = g_world_loc_y;
	g_pai_context.craft_position_z = g_world_loc_z;
	g_pai_context.skill_tier = pai_skill_value_to_tier(
		pai_get_effective_skill_value(g_pai_context.craft));
	g_pai_context.plan_cursor =
		g_plan_data_ptrs[controller->running_plan_id];
	++g_pai_context.plan_cursor;
	g_pai_context.initial_maneuver_id = *g_pai_context.plan_cursor++;
	g_pai_context.require_undisabled_target = 0;
	g_pai_context.variable_plan_id =
		(uint8_t)pai_find_plan_id_by_name_or_zero("nullpln");
}

/* Returns the skill tier of a skill value: 0 below 0x8000, 1 below 0xC000, else
 * 2. */
// FUNCTION: XVT 0x402E00
int pai_skill_value_to_tier(uint16_t skill_value)
{
	if (skill_value < 0x8000) {
		return 0;
	}
	return skill_value < 0xC000 ? 1 : 2;
}

/* Returns the index of the first craft in the active region's craft slots that
 * belongs to the flight group, is not breaking up or exploding, and has no
 * leader; 0xFFFF when there is none. Any flight group works; the callers pass a
 * mothership's. */
// FUNCTION: XVT 0x402E20
uint16_t pai_find_mothership_object(int16_t mothership_flight_group_idx)
{
	uint16_t object_index = (uint16_t)g_active_region_object_slot_start;
	while (object_index < g_active_region_craft_object_slot_end) {
		struct object_record *object = &g_object_table[object_index];
		if (object->object_type != 0) {
			struct craft_data *craft = object->mobj->p_craft;
			uint8_t object_kind = craft->object_kind;
			if (object_kind != CRAFT_OBJECT_KIND_EXPLODING &&
			    object_kind != CRAFT_OBJECT_KIND_BREAKING_UP &&
			    object->flight_group_idx ==
				    (uint16_t)mothership_flight_group_idx &&
			    (uint8_t)craft->leader_obj_idx == UINT8_MAX) {
				return object_index;
			}
		}

		++object_index;
	}

	return UINT16_MAX;
}

/* Returns 1 when pai_is_object_targetable accepts the object and its rough
 * distance from the craft's position in g_pai_context is below a skill range,
 * else 0. The range is 2,560 plus 0x500 times g_ai_skill_value_q16_by_level[skill
 * tier] over 65,536, times 256 world units: 655,360, 737,280 or 819,200 for
 * tiers 0 to 2. A nonzero expand_range adds 0x5555 over 65,536 of it, about a
 * third. The first argument is ignored. Sets g_last_rough_distance. */
// FUNCTION: XVT 0x402EC0
int pai_is_object_targetable_near_craft(int unused_craft_obj_idx,
					unsigned int obj_idx, int expand_range)
{
	(void)unused_craft_obj_idx;
	int targetable = pai_is_object_targetable(obj_idx);

	if (targetable) {
		int max_range_score =
			(uint16_t)math2_fraction(
				0x500u, g_ai_skill_value_q16_by_level
						[g_pai_context.skill_tier]) +
			2560;
		if (expand_range != 0) {
			max_range_score += (uint16_t)math2_fraction(
				(unsigned int)max_range_score, 0x5555u);
		}
		if (pai_is_object_within_range_of_craft(
			    obj_idx, (unsigned int)(max_range_score << 8)) ==
		    1) {
			return 1;
		}
	}
	return 0;
}

/* Returns 1 when the object's rough distance from the craft's position in
 * g_pai_context is below the unexpanded skill range of
 * pai_is_object_targetable_near_craft, else 0. Does not check that the object can
 * be targeted. Sets g_last_rough_distance. */
// FUNCTION: XVT 0x403070
int16_t pai_is_object_within_skill_range_of_craft(uint16_t obj_idx)
{
	uint16_t skill_range = (uint16_t)math2_fraction(
		0x500, g_ai_skill_value_q16_by_level[g_pai_context.skill_tier]);
	return pai_is_object_within_range_of_craft(
		       obj_idx, (skill_range + 0xA00) << 8) == 1;
}

/* Returns 1 when pai_find_boarding_target_from_order finds a target for the order
 * slot, else 0. */
// FUNCTION: XVT 0x403250
int16_t pai_order_slot_can_board_target(uint16_t order_slot)
{
	return pai_find_boarding_target_from_order(order_slot) != -1;
}

/* Returns the nearest boarding target that matches the order slot's first pair
 * of target conditions, or when none does, its second pair; -1 when neither
 * finds one. Uses the craft's flight group in g_pai_context. */
// FUNCTION: XVT 0x403270
int16_t pai_find_boarding_target_from_order(uint16_t order_slot)
{
	int16_t result = pai_find_nearest_boarding_target(
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target1_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target1,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target1_or_target2,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target2_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[order_slot]
			.target2);
	if (result == -1) {
		result = pai_find_nearest_boarding_target(
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_target_types[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_targets[0],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.target3_or_target4,
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_target_types[1],
			g_mission_flight_groups
				[g_pai_context.craft_flight_group_index]
					.fg.orders[order_slot]
					.secondary_targets[1]);
	}
	return result;
}

/* Returns 1 when the object's rough distance from the craft's position in
 * g_pai_context is below max_rough_distance, in world units, else 0. Sets
 * g_last_rough_distance to that distance. The position is the one
 * pai_setupcraftcontext took, not the craft's live one. */
// FUNCTION: XVT 0x403360
int pai_is_object_within_range_of_craft(unsigned int obj_idx,
					unsigned int max_rough_distance)
{
	struct object_record *object = &g_object_table[obj_idx];
	int delta_x = g_pai_context.craft_position_x - object->world_x;
	int delta_y = g_pai_context.craft_position_y - object->world_y;
	int delta_z = g_pai_context.craft_position_z - object->world_z;
	if (delta_x < 0) {
		delta_x = (int)(0u - (unsigned int)delta_x);
	}
	if (delta_y < 0) {
		delta_y = (int)(0u - (unsigned int)delta_y);
	}
	if (delta_z < 0) {
		delta_z = (int)(0u - (unsigned int)delta_z);
	}

	if (delta_y < delta_x) {
		g_last_rough_distance = delta_x + (delta_y >> 1);
	} else {
		g_last_rough_distance = delta_y + (delta_x >> 1);
	}

	if (g_last_rough_distance > delta_z) {
		delta_z >>= 1;
	} else {
		g_last_rough_distance >>= 1;
	}
	g_last_rough_distance += delta_z;

	return g_last_rough_distance < (int)max_rough_distance;
}

/* Sets the controller's aim point to the world position of its target_obj_idx: an
 * object, or a mission point of the craft's flight group, 0x8000 standing for
 * the group's current point. Also sets g_world_loc_x, g_world_loc_y and
 * g_world_loc_z. Does not check for 0xFFFF, no target. */
// FUNCTION: XVT 0x403400
void pai_update_aim_point_from_order_target(void)
{
	mission_resolve_object_or_mission_point_world_loc(
		g_pai_context.controller->target_obj_idx,
		g_object_table[g_pai_context.object_index].flight_group_idx);
	g_pai_context.controller->aim_point_x = g_world_loc_x;
	g_pai_context.controller->aim_point_y = g_world_loc_y;
	g_pai_context.controller->aim_point_z = g_world_loc_z;
}

/* Sets the formation type and separation of every craft of the flight group in
 * the active region's craft slots. */
// FUNCTION: XVT 0x403470
void pai_set_flight_group_formation(unsigned int flight_group_idx,
				    unsigned int formation_type,
				    unsigned int formation_spacing)
{
	for (unsigned int object_index =
		     (unsigned int)g_active_region_object_slot_start;
	     object_index < (unsigned int)g_active_region_craft_object_slot_end;
	     ++object_index) {
		struct object_record *object = &g_object_table[object_index];
		if (object->object_type != 0 &&
		    object->flight_group_idx == flight_group_idx) {
			struct craft_data *craft = object->mobj->p_craft;
			craft->ai_flight.formation_type = formation_type;
			craft->ai_flight.separation = formation_spacing;
		}
	}
	XVT_LOG_DEBUG(
		"ai.formation_set fg=%d formation=%u spacing=%u predicted=%d",
		(int)flight_group_idx, formation_type, formation_spacing,
		g_flight_sim_side_effects_suppressed);
}

/* Works out the direction and distance from one object or mission point to
 * another with trig2_ctop, which sets trig2_xyangle, trig2_pitch and
 * trig2_polardistance. A mission point reference is read from flight group 0's
 * points. Leaves g_world_loc_x, g_world_loc_y and g_world_loc_z at from_ref. */
// FUNCTION: XVT 0x4034E0
void pai_object_ref_direction_to_object_ref(unsigned int from_ref,
					    unsigned int to_ref)
{
	mission_resolve_object_or_mission_point_world_loc(to_ref, 0);
	int target_x = g_world_loc_x;
	int target_y = g_world_loc_y;
	int target_z = g_world_loc_z;

	mission_resolve_object_or_mission_point_world_loc(from_ref, 0);
	target_x = target_x - g_world_loc_x;
	target_y = target_y - g_world_loc_y;
	target_z = target_z - g_world_loc_z;
	trig2_ctop(target_x, target_y, target_z);
}

/* Sets g_last_rough_distance to the rough distance, in world units, between two
 * objects or mission points. A mission point reference is read from flight
 * group 0's points. Leaves g_world_loc_x, g_world_loc_y and g_world_loc_z at
 * to_ref. */
// FUNCTION: XVT 0x403540
void pai_object_ref_update_rough_distance(unsigned int from_ref,
					  unsigned int to_ref)
{
	mission_resolve_object_or_mission_point_world_loc(from_ref, 0);
	int delta_x = g_world_loc_x;
	int delta_y = g_world_loc_y;
	int delta_z = g_world_loc_z;

	mission_resolve_object_or_mission_point_world_loc(to_ref, 0);
	delta_x -= g_world_loc_x;
	delta_y -= g_world_loc_y;
	delta_z -= g_world_loc_z;

	if (delta_x < 0) {
		delta_x = -delta_x;
	}
	if (delta_y < 0) {
		delta_y = -delta_y;
	}
	if (delta_z < 0) {
		delta_z = -delta_z;
	}

	int xy_score;
	if (delta_x > delta_y) {
		xy_score = delta_x + (delta_y >> 1);
	} else {
		xy_score = delta_y + (delta_x >> 1);
	}

	if (xy_score > delta_z) {
		g_last_rough_distance = xy_score + (delta_z >> 1);
	} else {
		g_last_rough_distance = xy_score;
		g_last_rough_distance >>= 1;
		g_last_rough_distance += delta_z;
	}
}

/* Turns a vector given along an object's side, up and forward axes into world
 * axes, in g_rotated_x, g_rotated_y and g_rotated_z. The axes are 1.15 fixed
 * point, so the result keeps the input's unit. When the object's
 * orient_matrix_dirty is set it first rebuilds the object's cached move vector
 * and axes with fview_calcrotatemove and fview_calcrotateorient, which also set
 * the shared matrix globals those two write. */
// FUNCTION: XVT 0x4035D0
void pai_calcrotatedpoint(const struct object_record *obj, int16_t side_arg,
			  int16_t up_arg, int16_t fwd_arg)
{
	/* Rotate local coordinates through the cached Q15 orientation basis. */
	if (obj->mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(obj->pitch, obj->yaw, obj);
		fview_calcrotateorient(obj->roll, 0, obj);
	}

	g_rotated_x = math_mul_q15(side_arg, obj->mobj->cached_side_x);
	int result = math_mul_q15(up_arg, obj->mobj->cached_up_x);
	g_rotated_x += result;
	g_rotated_x += math_mul_q15(fwd_arg, obj->mobj->cached_fwd_x);
	g_rotated_y = math_mul_q15(side_arg, obj->mobj->cached_side_y);
	g_rotated_y += math_mul_q15(up_arg, obj->mobj->cached_up_y);
	g_rotated_y += math_mul_q15(fwd_arg, obj->mobj->cached_fwd_y);
	g_rotated_z = math_mul_q15(side_arg, obj->mobj->cached_side_z);
	g_rotated_z += math_mul_q15(up_arg, obj->mobj->cached_up_z);
	g_rotated_z += math_mul_q15(fwd_arg, obj->mobj->cached_fwd_z);
}

/* Does the same as pai_calcrotatedpoint, with the vector passed as int. */
// FUNCTION: XVT 0x4037B0
void pai_rotate_local_vector_to_world_scratch(
	const struct object_record *obj_record, int local_side, int local_up,
	int local_fwd)
{
	if (obj_record->mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(obj_record->pitch, obj_record->yaw,
				     obj_record);
		fview_calcrotateorient(obj_record->roll, 0, obj_record);
	}

	g_rotated_x = math_mul_q15(local_side, obj_record->mobj->cached_side_x);
	int result = math_mul_q15(local_up, obj_record->mobj->cached_up_x);
	g_rotated_x += result;
	g_rotated_x += math_mul_q15(local_fwd, obj_record->mobj->cached_fwd_x);
	g_rotated_y = math_mul_q15(local_side, obj_record->mobj->cached_side_y);
	g_rotated_y += math_mul_q15(local_up, obj_record->mobj->cached_up_y);
	g_rotated_y += math_mul_q15(local_fwd, obj_record->mobj->cached_fwd_y);
	g_rotated_z = math_mul_q15(local_side, obj_record->mobj->cached_side_z);
	g_rotated_z += math_mul_q15(local_up, obj_record->mobj->cached_up_z);
	g_rotated_z += math_mul_q15(local_fwd, obj_record->mobj->cached_fwd_z);
}

/* Works out the direction and distance from the craft's live position to the
 * controller's aim point with trig2_ctop, which sets trig2_xyangle, trig2_pitch
 * and trig2_polardistance. */
// FUNCTION: XVT 0x403990
void pai_calc_angles_to_aim_point(void)
{
	struct object_record *object =
		&g_object_table[g_pai_context.object_index];
	trig2_ctop(g_pai_context.controller->aim_point_x - object->world_x,
		   g_pai_context.controller->aim_point_y - object->world_y,
		   g_pai_context.controller->aim_point_z - object->world_z);
}

/* Returns the nearest object, by rough distance from the craft in g_pai_context,
 * that matches the target conditions (either one when target_or_mode is 1, else
 * both) and that no other craft is boarding; -1 when none. Among craft it takes
 * only one that can be boarded now: on nullpln, stationaryldrpln or
 * stationaryflwpln, a platform, or with no working subsystems; when the
 * searching craft's plan is not boardtocapturepln or boardtodestroypln, also
 * one waiting to be boarded or stopped. A craft is taken when another craft on
 * a boardto plan targets or carries it. It then looks at the region's static
 * object slots whose type has behavior flag 2, matching by flight group; there
 * only a target counts as taken, and boardtopickuppln is not among the plans
 * checked. Either way an object whose signature is in g_cur_craft's docked list
 * is skipped. Sets g_last_rough_distance. */
// FUNCTION: XVT 0x4039E0
int16_t pai_find_nearest_boarding_target(uint16_t target1_type,
					 uint16_t target1,
					 int16_t target_or_mode,
					 uint16_t target2_type,
					 uint16_t target2)
{
	uint16_t object_idx;
	uint16_t nearest_object = UINT16_MAX;
	unsigned int nearest_range = UINT_MAX;

	for (object_idx = (uint16_t)g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type == 0) {
			continue;
		}
		int16_t first_match = mission_object_matches_trigger_variable(
			object_idx, target1_type, target1);
		int16_t second_match = mission_object_matches_trigger_variable(
			object_idx, target2_type, target2);
		if (target_or_mode == 1) {
			first_match |= second_match;
		} else {
			first_match &= second_match;
		}
		if (first_match == 0) {
			continue;
		}

		/* Until the reset that starts the count, this local is a 0/1
		 * flag: 1 when the craft can be boarded now (parked, a
		 * platform, disabled, stopped, or waiting to be boarded). */
		int16_t craft_reserved_count = 0;
		struct object_record *object = &g_object_table[object_idx];
		struct craft_data *craft =
			g_object_table[object_idx].mobj->p_craft;
		struct ai_controller *controller = &craft->ai_controller;
		const char *plan_name =
			g_plan_table[controller->current_plan_id].name;
		if (strcmp(plan_name, "nullpln") == 0 ||
		    strcmp(plan_name, "stationaryldrpln") == 0 ||
		    strcmp(plan_name, "stationaryflwpln") == 0 ||
		    object->genus_id == CRAFT_GENUS_PLATFORM) {
			craft_reserved_count = 1;
		} else if (strcmp(g_plan_table[g_pai_context.controller
						       ->current_plan_id]
					  .name,
				  "boardtocapturepln") == 0 ||
			   strcmp(g_plan_table[g_pai_context.controller
						       ->current_plan_id]
					  .name,
				  "boardtodestroypln") == 0) {
			if (craft->working_subsystems == 0) {
				craft_reserved_count = 1;
			}
		} else if (craft->working_subsystems == 0 ||
			   controller->maneuver_mode ==
				   AI_MANEUVER_MODE_AWAIT_BOARD ||
			   controller->maneuver_mode == AI_MANEUVER_MODE_STOP) {
			craft_reserved_count = 1;
		}

		if (craft_reserved_count != 0) {
			craft_reserved_count = 0;
			for (uint16_t other_idx = (uint16_t)
				     g_active_region_object_slot_start;
			     other_idx < g_active_region_craft_object_slot_end;
			     ++other_idx) {
				struct object_record *other =
					&g_object_table[other_idx];
				if (other->object_type != 0 &&
				    other_idx != g_pai_context.object_index) {
					struct craft_data *other_craft =
						other->mobj->p_craft;
					int current_plan_id =
						other_craft->ai_controller
							.current_plan_id;
					struct ai_controller *other_controller =
						&other_craft->ai_controller;
					if (strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtogivepln") == 0 ||
					    strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtotakepln") == 0 ||
					    strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtoexchangepln") == 0 ||
					    strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtocapturepln") == 0 ||
					    strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtodestroypln") == 0 ||
					    strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtopickuppln") == 0 ||
					    strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtocontactpln") == 0 ||
					    strcmp(g_plan_table[current_plan_id]
							   .name,
						   "boardtorepairpln") == 0) {
						if (other_controller
							    ->target_obj_idx ==
						    object_idx) {
							++craft_reserved_count;
						} else if (
							other_craft
								->carried_object_index ==
							object_idx) {
							++craft_reserved_count;
						}
					}
				}
			}
			for (uint16_t sig_idx = 0;
			     sig_idx <
			     g_cur_craft->ai_flight.docked_target_count;
			     ++sig_idx) {
				if (g_cur_craft->ai_flight
					    .docked_target_signatures
						    [sig_idx] ==
				    object->object_signature) {
					++craft_reserved_count;
				}
			}
			if (craft_reserved_count == 0) {
				pai_object_ref_update_rough_distance(
					g_pai_context.object_index, object_idx);
				if ((unsigned int)g_last_rough_distance >=
				    nearest_range) {
					continue;
				}
				nearest_range = g_last_rough_distance;
				nearest_object = object_idx;
			}
		}
	}

	for (object_idx = (uint16_t)g_region_main_object_slot_end;
	     (int)(g_region_main_object_slot_end +
		   g_region_static_object_slot_count) > object_idx;
	     ++object_idx) {
		struct object_record *object = &g_object_table[object_idx];
		uint16_t object_type = object->object_type;

		if (object_type != 0 &&
		    (g_object_type_table[object_type].behavior_flags & 2) !=
			    0) {
			int16_t first_match =
				mission_flight_group_matches_trigger_variable(
					object->flight_group_idx, target1_type,
					target1);
			int16_t second_match =
				mission_flight_group_matches_trigger_variable(
					g_object_table[object_idx]
						.flight_group_idx,
					target2_type, target2);
			if (target_or_mode == 1) {
				first_match |= second_match;
			} else {
				first_match &= second_match;
			}
			if (first_match != 0) {
				int16_t reserved_count = 0;

				for (uint16_t other_idx = (uint16_t)
					     g_active_region_object_slot_start;
				     other_idx <
				     g_active_region_craft_object_slot_end;
				     ++other_idx) {
					struct object_record *other =
						&g_object_table[other_idx];
					if (other->object_type != 0 &&
					    other_idx !=
						    g_pai_context
							    .object_index) {
						struct ai_controller *other_controller =
							&other->mobj->p_craft
								 ->ai_controller;
						int current_plan_id =
							other_controller
								->current_plan_id;
						if ((strcmp(g_plan_table
								    [current_plan_id]
									    .name,
							    "boardtogivepln") ==
							     0 ||
						     strcmp(g_plan_table
								    [current_plan_id]
									    .name,
							    "boardtotakepln") ==
							     0 ||
						     strcmp(g_plan_table
								    [current_plan_id]
									    .name,
							    "boardtoexchangepln") ==
							     0 ||
						     strcmp(g_plan_table
								    [current_plan_id]
									    .name,
							    "boardtocapturepln") ==
							     0 ||
						     strcmp(g_plan_table
								    [current_plan_id]
									    .name,
							    "boardtodestroypln") ==
							     0 ||
						     strcmp(g_plan_table
								    [current_plan_id]
									    .name,
							    "boardtocontactpln") ==
							     0 ||
						     strcmp(g_plan_table
								    [current_plan_id]
									    .name,
							    "boardtorepairpln") ==
							     0) &&
						    other_controller->target_obj_idx ==
							    object_idx) {
							++reserved_count;
						}
					}
				}
				{
					uint16_t sig_idx = 0;
					uint8_t signature_count =
						g_cur_craft->ai_flight
							.docked_target_count;
					if (signature_count != 0) {
						do {
							if (g_cur_craft
								    ->ai_flight
								    .docked_target_signatures
									    [sig_idx] ==
							    g_object_table[object_idx]
								    .object_signature) {
								++reserved_count;
							}
							++sig_idx;
						} while (sig_idx <
							 signature_count);
					}
				}
				if (reserved_count == 0) {
					pai_object_ref_update_rough_distance(
						g_pai_context.object_index,
						object_idx);
					if ((unsigned int)
						    g_last_rough_distance >=
					    nearest_range) {
						continue;
					}
					nearest_range = g_last_rough_distance;
					nearest_object = object_idx;
				}
			}
		}
	}
	return (int16_t)nearest_object;
}

/* Returns 1 when the plan's goal for the order slot is met, else 0. For
 * formldr1pln, formevadeldr1pln, starshipformpln, rendezvous1pln, disabledpln
 * and waitforboardpln: goal_progress has reached the order's variable1. For
 * capfreeldr1pln, capescortersldr1pln, caprespondldr1pln, escortldr1pln,
 * disableldr1pln, starshipprotectpln, starshipattackpln and starshipdisablepln:
 * no target is left, now or later. For the eight boardto plans: goal_progress
 * has reached variable2. For dropoffldr1pln: goal_progress has reached the
 * FLIGHT_GROUP_OUTCOME_TOTAL count of the flight group whose index is
 * variable2 minus 1. For
 * waitpln: the maneuver timer has run out. For starshipwaitreturnpln and
 * starshipwaitcreatepln: the matching wait order returns nonzero. Any other
 * plan returns 0. Uses the craft in g_pai_context. */
// FUNCTION: XVT 0x403FE0
int16_t pai_is_plan_complete_for_order_slot(uint16_t plan_id,
					    uint16_t order_slot)
{
	int16_t result = 0;
	if (strcmp(g_plan_table[plan_id].name, "formldr1pln") == 0 ||
	    strcmp(g_plan_table[plan_id].name, "formevadeldr1pln") == 0 ||
	    strcmp(g_plan_table[plan_id].name, "starshipformpln") == 0 ||
	    strcmp(g_plan_table[plan_id].name, "rendezvous1pln") == 0 ||
	    strcmp(g_plan_table[plan_id].name, "disabledpln") == 0 ||
	    strcmp(g_plan_table[plan_id].name, "waitforboardpln") == 0) {
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[order_slot]
			    .variable1 <=
		    g_pai_context.controller->order_progress
			    .goal_progress[order_slot]) {
			result = 1;
		}
	} else if (strcmp(g_plan_table[plan_id].name, "capfreeldr1pln") == 0 ||
		   strcmp(g_plan_table[plan_id].name, "capescortersldr1pln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "caprespondldr1pln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "escortldr1pln") == 0 ||
		   strcmp(g_plan_table[plan_id].name, "disableldr1pln") == 0 ||
		   strcmp(g_plan_table[plan_id].name, "starshipprotectpln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "starshipattackpln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "starshipdisablepln") ==
			   0) {
		if (paifight_search_order_slot_remaining_targets(order_slot) ==
			    0 &&
		    paifight_order_slot_has_future_targets(order_slot) == 0) {
			result = 1;
		}
	} else if (strcmp(g_plan_table[plan_id].name, "boardtogivepln") == 0 ||
		   strcmp(g_plan_table[plan_id].name, "boardtotakepln") == 0 ||
		   strcmp(g_plan_table[plan_id].name, "boardtoexchangepln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "boardtocapturepln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "boardtodestroypln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "boardtopickuppln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "boardtocontactpln") ==
			   0 ||
		   strcmp(g_plan_table[plan_id].name, "boardtorepairpln") ==
			   0) {
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
			    .fg.orders[order_slot]
			    .variable2 <=
		    g_pai_context.controller->order_progress
			    .goal_progress[order_slot]) {
			result = 1;
		}
	} else if (strcmp(g_plan_table[plan_id].name, "dropoffldr1pln") == 0) {
		if (g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.orders[order_slot]
				    .variable2 == 0 ||
		    g_mission_flight_groups[g_pai_context
						    .craft_flight_group_index]
				    .fg.orders[order_slot]
				    .variable2 >
			    g_mission_header.num_flight_groups) {
			XVT_LOG_DEBUG(
				"ai.dropoff_group_invalid object=%d fg=%d order=%d group=%d groups=%d predicted=%d",
				(int)g_pai_context.object_index,
				(int)g_pai_context.craft_flight_group_index,
				(int)order_slot,
				(int)g_mission_flight_groups
					[g_pai_context.craft_flight_group_index]
						.fg.orders[order_slot]
						.variable2,
				(int)g_mission_header.num_flight_groups,
				g_flight_sim_side_effects_suppressed);
		}
		if ((unsigned int)g_mission_fg_stats
			    [(uint16_t)(g_mission_flight_groups
						[g_pai_context
							 .craft_flight_group_index]
							.fg.orders[order_slot]
							.variable2 -
					1)]
				    .outcome_count
					    [FLIGHT_GROUP_OUTCOME_TOTAL] <=
		    g_pai_context.controller->order_progress
			    .goal_progress[order_slot]) {
			result = 1;
		}
	} else if (strcmp(g_plan_table[plan_id].name, "waitpln") == 0) {
		if (g_pai_context.controller->maneuver_timer == 0) {
			result = 1;
		}
	} else if (strcmp(g_plan_table[plan_id].name,
			  "starshipwaitreturnpln") == 0) {
		if (paiorder_waitforallreturnorder() != 0) {
			result = 1;
		}
	} else if (strcmp(g_plan_table[plan_id].name,
			  "starshipwaitcreatepln") == 0 &&
		   paiorder_waitforallcreateorder() != 0) {
		result = 1;
	}
	return result;
}

/* Returns 1 when the plan is one of the eight boardto plans and the order slot
 * has no target left, now or later; else 0. */
// FUNCTION: XVT 0x404380
int16_t pai_is_boarding_plan_complete_for_order_slot(uint16_t plan_id,
						     uint16_t order_slot)
{
	int16_t result = 0;
	if ((strcmp(g_plan_table[plan_id].name, "boardtogivepln") == 0 ||
	     strcmp(g_plan_table[plan_id].name, "boardtotakepln") == 0 ||
	     strcmp(g_plan_table[plan_id].name, "boardtoexchangepln") == 0 ||
	     strcmp(g_plan_table[plan_id].name, "boardtocapturepln") == 0 ||
	     strcmp(g_plan_table[plan_id].name, "boardtodestroypln") == 0 ||
	     strcmp(g_plan_table[plan_id].name, "boardtopickuppln") == 0 ||
	     strcmp(g_plan_table[plan_id].name, "boardtocontactpln") == 0 ||
	     strcmp(g_plan_table[plan_id].name, "boardtorepairpln") == 0) &&
	    paifight_search_order_slot_remaining_targets(order_slot) == 0 &&
	    paifight_order_slot_has_future_targets(order_slot) == 0) {
		result = 1;
	}
	return result;
}

/* Returns 1 when the object matches the target conditions of the craft's
 * current order slot in g_pai_context: the first two, joined by "or" when
 * target1_or_target2 is 1, else by "and", or the second two, joined the same way
 * by target3_or_target4. Else 0. */
// FUNCTION: XVT 0x404450
int16_t pai_current_order_targets_match_object(uint16_t object_idx)
{
	int16_t primary_match = mission_object_matches_trigger_variable(
		object_idx,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.target1_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.target1);
	int16_t match = mission_object_matches_trigger_variable(
		object_idx,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.target2_type,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.target2);
	if (g_mission_flight_groups[g_pai_context.craft_flight_group_index]
		    .fg.orders[g_pai_context.order_slot]
		    .target1_or_target2 == 1) {
		primary_match |= match;
	} else {
		primary_match &= match;
	}

	int16_t secondary_match = mission_object_matches_trigger_variable(
		object_idx,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.secondary_target_types[0],
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.secondary_targets[0]);
	match = mission_object_matches_trigger_variable(
		object_idx,
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.secondary_target_types[1],
		g_mission_flight_groups[g_pai_context.craft_flight_group_index]
			.fg.orders[g_pai_context.order_slot]
			.secondary_targets[1]);
	if (g_mission_flight_groups[g_pai_context.craft_flight_group_index]
		    .fg.orders[g_pai_context.order_slot]
		    .target3_or_target4 == 1) {
		secondary_match |= match;
	} else {
		secondary_match &= match;
	}

	return primary_match || secondary_match;
}

/* Returns the AI skill the craft flies with: the skill in the character data of
 * the object effective_ai_object_link points to, while that object still has the
 * saved signature and has character data, else the craft's own ai_skill. Clears
 * effective_ai_object_link when the signature no longer matches. */
// FUNCTION: XVT 0x404620
uint16_t pai_get_effective_skill_value(struct craft_data *craft)
{
	struct object_record *linked_object = craft->effective_ai_object_link;
	if (linked_object == 0) {
		return craft->ai_skill;
	}

	if (linked_object->object_signature !=
	    craft->effective_ai_object_signature) {
		craft->effective_ai_object_link = 0;
		return craft->ai_skill;
	}

	if (linked_object->mobj == 0) {
		return craft->ai_skill;
	}
	if (linked_object->mobj->p_char_data != 0) {
		return linked_object->mobj->p_char_data->skill_value;
	}

	return craft->ai_skill;
}

/* Returns 1 when one of the object's flight group orders in slots 0 to 2 gives
 * a leader the plan named by leader_plan_name_index and targets the given object,
 * else 0; returns 0 at once for object_idx -1. Sets up g_pai_context for the
 * object, and leaves g_pai_context.order_slot at the matching slot, else at the
 * last slot that gave that plan, else at the craft's current slot.
 * The modern build puts g_pai_context back as it found it before returning: the
 * target description calls this for this machine's own player, so the context
 * it left behind differed between the machines of a network game, and a mine's
 * decoy test in a later step reads it. */
// FUNCTION: XVT 0x404670
int pai_setup_context_and_find_order_plan_on_target(int object_idx,
						    int leader_plan_name_index,
						    int target_obj_idx)
{
	if (object_idx == -1) {
		return 0;
	}
	struct pai_context saved_context = g_pai_context;
	pai_setupcraftcontext(object_idx);
	for (unsigned int order_slot = 0; order_slot < 3; ++order_slot) {
		uint8_t order =
			g_mission_flight_groups[g_object_table[object_idx]
							.flight_group_idx]
				.fg.orders[order_slot]
				.order;
		if (g_order_leader_builtin_plan_name_index[order] ==
		    leader_plan_name_index) {
			g_pai_context.order_slot = (uint16_t)order_slot;
			if (pai_current_order_targets_match_object(
				    target_obj_idx) != 0) {
				g_pai_context = saved_context;
				return 1;
			}
		}
	}
	g_pai_context = saved_context;
	return 0;
}

/* Returns the index of the first g_plan_table entry with the name, compared up
 * to 80 characters, or 256 when none has it. An empty name finds the first free
 * entry. */
// FUNCTION: XVT 0x46AC80
int pai_find_plan_table_index_by_name(const char *plan_name)
{
	const struct pai_plan_record *plan = g_plan_table;
	unsigned int plan_index;

	for (plan_index = 0; plan_index < 256; ++plan, ++plan_index) {
		if (strncmp(plan->name, plan_name, sizeof(plan->name)) == 0) {
			break;
		}
	}
	return plan_index;
}

/* Returns the index of the first g_plan_table entry with an empty name, or 256
 * when none is free. */
// FUNCTION: XVT 0x46ACB0
int pai_find_free_plan_table_index(void)
{
	int plan_index;

	for (plan_index = 0; plan_index < 256; ++plan_index) {
		if (g_plan_table[plan_index].name[0] == '\0') {
			break;
		}
	}
	return plan_index;
}

/* Returns the index of the target token with the name in g_pai_target_token_defs,
 * or the index of its empty end entry when none has it. */
// FUNCTION: XVT 0x46ACD0
int pai_find_target_token_index(const char *token)
{
	int token_index = 0;

	if (g_pai_target_token_defs[0].name[0] != '\0') {
		struct pai_plan_token_def *token_def = g_pai_target_token_defs;

		do {
			if (strcmp(token_def->name, token) == 0) {
				break;
			}
			++token_def;
			++token_index;
		} while (token_def->name[0] != '\0');
	}

	return token_index;
}

/* Returns the index of the maneuver token with the name in
 * g_pai_maneuver_token_defs, or the index of its empty end entry when none has
 * it. */
// FUNCTION: XVT 0x46AD30
int pai_find_maneuver_token_index(const char *token)
{
	int token_index = 0;

	if (g_pai_maneuver_token_defs[0].name[0] != '\0') {
		struct pai_plan_token_def *token_def =
			g_pai_maneuver_token_defs;

		do {
			if (strcmp(token_def->name, token) == 0) {
				break;
			}
			++token_def;
			++token_index;
		} while (token_def->name[0] != '\0');
	}

	return token_index;
}

/* Returns the index of the order token with the name in g_pai_order_token_defs, or
 * the index of its empty end entry when none has it. */
// FUNCTION: XVT 0x46AD90
int pai_find_order_token_index(const char *token)
{
	int token_index = 0;

	if (g_pai_order_token_defs[0].name[0] != '\0') {
		struct pai_plan_token_def *token_def = g_pai_order_token_defs;

		do {
			if (strcmp(token_def->name, token) == 0) {
				break;
			}
			++token_def;
			++token_index;
		} while (token_def->name[0] != '\0');
	}

	return token_index;
}

/* Reads the next token of the plan text into token: it skips spaces, tabs,
 * newlines and commas, and from a semicolon to the end of the line, then copies
 * characters up to a space, tab, newline, comma or the end of the file. Returns
 * 1 on every path; at the end of the file the token is empty. Does not check
 * the token's length. */
// FUNCTION: XVT 0x46ADF0
int pai_read_plan_text_token(char *token, xvt_file *stream)
{
	*token = '\0';
	char read_char;
	for (;;) {
		do {
			if (FILE_RAW_READ(&read_char, 1, 1, stream) != 1) {
				return 1;
			}
		} while (read_char == ' ' || read_char == '\t' ||
			 read_char == '\n' || read_char == ',' ||
			 read_char == '\n');

		if (read_char != ';') {
			break;
		}

		do {
			if (FILE_RAW_READ(&read_char, 1, 1, stream) != 1) {
				return 1;
			}
		} while (read_char != '\n');
	}

	*token = read_char;
	int token_length = 1;
	for (;;) {
		if (FILE_RAW_READ(&read_char, 1, 1, stream) != 1) {
			token[token_length] = '\0';
			return 1;
		}
		if (read_char == ',' || read_char == '\n' || read_char == ' ' ||
		    read_char == '\t' || read_char == '\n') {
			break;
		}
		token[token_length] = read_char;
		++token_length;
	}
	token[token_length] = '\0';
	return 1;
}

/* Compiles the plan text "<baseName>.pln" into g_plan_table, g_plan_order_data and
 * g_plan_data_ptrs, then writes them to "<baseName>.plo". A plan is its name, a
 * target token, a maneuver token, then pairs of an order token and a plan name,
 * ended by NULLORDR; a "*" ends the file. A plan named before it is defined
 * gets an entry marked not defined. Returns 0 when the file does not open, a
 * plan is defined twice, the table is full, a token is unknown, the text ends
 * before the "*", or a named plan is never defined; else 1, whether or not the
 * .plo file could be written. Adds 1 to g_plan_count for each plan. The .plo
 * file holds the table's size and the table, then 0xFFFF and the first 0xFFFF
 * bytes of g_plan_order_data. Does not check that the plans fit in
 * g_plan_order_data. */
// FUNCTION: XVT 0x46AED0
int pai_compile_plans_from_text(const char *base_name)
{
	char file_name[256];

	strcpy(file_name, base_name);
	strcat(file_name, ".pln");
	fe_disk_io_open_global_stream(file_name, "r", 0, 0);
	xvt_file *stream = (xvt_file *)g_stream;
	if (stream == NULL) {
		XVT_LOG_ERROR(
			"ai.plans_compile_failed reason=\"open\" token=\"%s\" plans=%d",
			file_name, g_plan_count);
		return 0;
	}

	uint8_t *cursor = g_plan_order_data;
	char token[256];
	int plan_index;
	for (;;) {
		if (pai_read_plan_text_token(token, stream) == 0) {
			FILE_RAW_CLOSE(stream);
			return 0;
		}
		if (token[0] == '*') {
			break;
		}

		plan_index = pai_find_plan_table_index_by_name(token);
		if (plan_index != 256) {
			if (g_plan_table[plan_index].is_defined == 1) {
				XVT_LOG_ERROR(
					"ai.plans_compile_failed reason=\"duplicate\" token=\"%s\" plans=%d",
					token, g_plan_count);
				FILE_RAW_CLOSE(stream);
				return 0;
			}
		} else {
			plan_index = pai_find_free_plan_table_index();
			if (plan_index == 256) {
				XVT_LOG_ERROR(
					"ai.plans_compile_failed reason=\"table_full\" token=\"%s\" plans=%d",
					token, g_plan_count);
				FILE_RAW_CLOSE(stream);
				return 0;
			}
		}

		strncpy(g_plan_table[plan_index].name, token,
			sizeof(g_plan_table[plan_index].name));
		g_plan_table[plan_index].name[79] = '\0';
		g_plan_table[plan_index].is_defined = 1;
		g_plan_table[plan_index].data_offset =
			(uint32_t)(cursor - g_plan_order_data);
		g_plan_data_ptrs[plan_index] = cursor;

		if (pai_read_plan_text_token(token, stream) == 0) {
			FILE_RAW_CLOSE(stream);
			return 0;
		}
		int target_index = pai_find_target_token_index(token);
		if (g_pai_target_token_defs[target_index].name[0] == '\0') {
			XVT_LOG_ERROR(
				"ai.plans_compile_failed reason=\"target\" token=\"%s\" plans=%d",
				token, g_plan_count);
			FILE_RAW_CLOSE(stream);
			return 0;
		}
		*cursor++ =
			(uint8_t)g_pai_target_token_defs[target_index].value;

		if (pai_read_plan_text_token(token, stream) == 0) {
			FILE_RAW_CLOSE(stream);
			return 0;
		}
		int maneuver_index = pai_find_maneuver_token_index(token);
		if (g_pai_maneuver_token_defs[maneuver_index].name[0] == '\0') {
			XVT_LOG_ERROR(
				"ai.plans_compile_failed reason=\"maneuver\" token=\"%s\" plans=%d",
				token, g_plan_count);
			FILE_RAW_CLOSE(stream);
			return 0;
		}
		*cursor++ = (uint8_t)g_pai_maneuver_token_defs[maneuver_index]
				    .value;

		for (;;) {
			if (pai_read_plan_text_token(token, stream) == 0) {
				FILE_RAW_CLOSE(stream);
				return 0;
			}
			int order_index = pai_find_order_token_index(token);
			if (g_pai_order_token_defs[order_index].name[0] ==
			    '\0') {
				XVT_LOG_ERROR(
					"ai.plans_compile_failed reason=\"order\" token=\"%s\" plans=%d",
					token, g_plan_count);
				FILE_RAW_CLOSE(stream);
				return 0;
			}

			*cursor++ = (uint8_t)g_pai_order_token_defs[order_index]
					    .value;
			if (order_index == 0) {
				++g_plan_count;
				break;
			}

			if (pai_read_plan_text_token(token, stream) == 0) {
				FILE_RAW_CLOSE(stream);
				return 0;
			}
			int next_plan_id =
				pai_find_plan_table_index_by_name(token);
			if (next_plan_id != 256) {
				*cursor++ = (uint8_t)next_plan_id;
				continue;
			}

			int free_plan_id = pai_find_free_plan_table_index();
			if (free_plan_id == 256) {
				XVT_LOG_ERROR(
					"ai.plans_compile_failed reason=\"table_full\" token=\"%s\" plans=%d",
					token, g_plan_count);
				FILE_RAW_CLOSE(stream);
				return 0;
			}
			strncpy(g_plan_table[free_plan_id].name, token,
				sizeof(g_plan_table[free_plan_id].name));
			g_plan_table[free_plan_id].name[79] = '\0';
			g_plan_table[free_plan_id].is_defined = 0;
			*cursor++ = (uint8_t)free_plan_id;
		}
	}

	FILE_RAW_CLOSE(stream);
	for (plan_index = 0; plan_index < 256; ++plan_index) {
		if (g_plan_table[plan_index].name[0] != '\0' &&
		    g_plan_table[plan_index].is_defined != 1) {
			XVT_LOG_ERROR(
				"ai.plans_compile_failed reason=\"undefined\" token=\"%s\" plans=%d",
				g_plan_table[plan_index].name, g_plan_count);
			return 0;
		}
	}

	strcpy(file_name, base_name);
	strcat(file_name, ".plo");
	fe_disk_io_open_global_stream(file_name, "wb", 0, 1);
	stream = (xvt_file *)g_stream;
	if (stream != NULL) {
		if (cursor - g_plan_order_data > 0xFFFF) {
			XVT_LOG_WARN("ai.plans_truncated bytes=%d",
				     (int)(cursor - g_plan_order_data));
		}
		/* From here the same local holds the byte size of each section
		 * of the .plo file; each size is written just before its
		 * section. */
		plan_index = (int)sizeof(g_plan_table);
		FILE_RAW_WRITE(&plan_index, sizeof(plan_index), 1, stream);
		FILE_RAW_WRITE(g_plan_table, (size_t)plan_index, 1, stream);
		plan_index = 0xFFFF;
		FILE_RAW_WRITE(&plan_index, sizeof(plan_index), 1, stream);
		FILE_RAW_WRITE(g_plan_order_data, (size_t)plan_index, 1,
			       stream);
		FILE_RAW_CLOSE(stream);
	} else {
		XVT_LOG_WARN("ai.plans_save_failed file=\"%s\"", file_name);
	}
	XVT_LOG_INFO("ai.plans_compiled plans=%d bytes=%d", g_plan_count,
		     (int)(cursor - g_plan_order_data));

	return 1;
}

/* Loads the AI plans: clears g_plan_table and g_plan_count and zeroes the first
 * 256 bytes of g_plan_data_ptrs, then reads "<baseName>.plo": a size and that
 * many bytes into g_plan_table, then a size and that many bytes into
 * g_plan_order_data. When the file does not open or a read fails, returns what
 * pai_compile_plans_from_text returns. Else points g_plan_data_ptrs at each named
 * plan's bytes, sets g_plan_count to the number of named plans and returns 1.
 * Does not check either size against its array. */
// FUNCTION: XVT 0x46B3D0
int pai_loadplans(const char *base_name)
{
	char file_name[256];

	strcpy(file_name, base_name);
	strcat(file_name, ".plo");
	memset(g_plan_table, 0, sizeof(g_plan_table));
	g_plan_count = 0;
	memset(g_plan_data_ptrs, 0, 0x100);
	fe_disk_io_open_global_stream(file_name, g_file_mode_read_binary, 0, 1);
	xvt_file *stream = (xvt_file *)g_stream;
	if (stream == NULL) {
		XVT_LOG_WARN("ai.plans_fallback reason=\"open\" file=\"%s\"",
			     file_name);
		return pai_compile_plans_from_text(base_name);
	}

	uint32_t buffer_size;
	if (FILE_RAW_READ(&buffer_size, sizeof(buffer_size), 1, stream) != 1) {
		XVT_LOG_WARN(
			"ai.plans_fallback reason=\"table_size\" file=\"%s\"",
			file_name);
		FILE_RAW_CLOSE(stream);
		return pai_compile_plans_from_text(base_name);
	}
	if (buffer_size != sizeof(g_plan_table)) {
		XVT_LOG_WARN(
			"ai.plans_size_unexpected section=\"table\" bytes=%u expected=%u",
			(unsigned)buffer_size, (unsigned)sizeof(g_plan_table));
	}
	if (FILE_RAW_READ(g_plan_table, buffer_size, 1, stream) != 1) {
		XVT_LOG_WARN("ai.plans_fallback reason=\"table\" file=\"%s\"",
			     file_name);
		FILE_RAW_CLOSE(stream);
		return pai_compile_plans_from_text(base_name);
	}
	if (FILE_RAW_READ(&buffer_size, sizeof(buffer_size), 1, stream) != 1) {
		XVT_LOG_WARN(
			"ai.plans_fallback reason=\"data_size\" file=\"%s\"",
			file_name);
		FILE_RAW_CLOSE(stream);
		return pai_compile_plans_from_text(base_name);
	}
	if (buffer_size > sizeof(g_plan_order_data)) {
		XVT_LOG_WARN(
			"ai.plans_size_unexpected section=\"data\" bytes=%u expected=%u",
			(unsigned)buffer_size,
			(unsigned)sizeof(g_plan_order_data));
	}
	if (FILE_RAW_READ(g_plan_order_data, buffer_size, 1, stream) != 1) {
		XVT_LOG_WARN("ai.plans_fallback reason=\"data\" file=\"%s\"",
			     file_name);
		FILE_RAW_CLOSE(stream);
		return pai_compile_plans_from_text(base_name);
	}

	FILE_RAW_CLOSE(stream);
	int plan_index = 0;
	int plan_count = g_plan_count;
	do {
		if (g_plan_table[plan_index].name[0] != '\0') {
			++plan_count;
			g_plan_data_ptrs[plan_index] =
				&g_plan_order_data[g_plan_table[plan_index]
							   .data_offset];
			XVT_LOG_DEBUG(
				"ai.plan_listed plan=%d name=\"%s\" offset=%u defined=%d",
				plan_index, g_plan_table[plan_index].name,
				(unsigned)g_plan_table[plan_index].data_offset,
				(int)g_plan_table[plan_index].is_defined);
		}
		g_plan_count = plan_count;
		++plan_index;
	} while (plan_index < 256);
	XVT_LOG_INFO("ai.plans_loaded plans=%d bytes=%u", g_plan_count,
		     (unsigned)buffer_size);

	return 1;
}

/* Fills g_builtin_plan_id_by_name_index with the plan id of each name in
 * g_builtin_plan_name_table; a name with no plan gets 0, the not-found index 256
 * cut to 8 bits. */
// FUNCTION: XVT 0x46B5B0
void pai_cache_builtin_plan_ids(void)
{
	const char *plan_name = g_builtin_plan_name_table[0];
	int plan_name_ordinal = 0;
	if (*plan_name != '\0') {
		const char *const *plan_name_cursor = g_builtin_plan_name_table;
		do {
			plan_name_cursor++;
			g_builtin_plan_id_by_name_index[plan_name_ordinal++] =
				(uint8_t)pai_find_plan_table_index_by_name(
					plan_name);
			if (plan_name_ordinal > 1 &&
			    g_builtin_plan_id_by_name_index[plan_name_ordinal -
							    1] == 0) {
				XVT_LOG_WARN(
					"ai.builtin_plan_missing token=\"%s\" index=%d",
					plan_name, plan_name_ordinal - 1);
			}
			plan_name = *plan_name_cursor;
		} while (*plan_name != '\0');
	}
}

/* Returns where the named plan's bytes start. Does not check that the plan
 * exists: for an unknown name it reads g_plan_data_ptrs[256], past the end of the
 * array. */
// FUNCTION: XVT 0x46B5F0
uint8_t *pai_getplandataptrbyname(const char *plan_name)
{
	return g_plan_data_ptrs[pai_find_plan_table_index_by_name(plan_name)];
}

/* Returns the plan id of the first g_plan_table entry with the name, compared up
 * to 80 characters, or 0 when none has it. */
// FUNCTION: XVT 0x46B610
int pai_find_plan_id_by_name_or_zero(const char *plan_name)
{
	for (int plan_index = 0; plan_index < 256; ++plan_index) {
		if (strncmp(g_plan_table[plan_index].name, plan_name,
			    sizeof(g_plan_table[plan_index].name)) == 0) {
			return plan_index;
		}
	}
	XVT_LOG_DEBUG("ai.plan_missing token=\"%s\" predicted=%d", plan_name,
		      g_flight_sim_side_effects_suppressed);
	return 0;
}
