#include "xvt_runtime/runtime/sith.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt_runtime/log/log.h"

enum { CRAFT_TYPE_COUNT = 96 };

static int g_setting = XVT_SITH_NONE;
static int g_lord_flight_group = -1;
static int g_promoted_group = -1;
static char g_none_reason[128];
static int g_apprentices_lost;
static int g_lord_lost;

enum {
	/* The craft slots of a flight (mission.c, CRAFT_SLOT_COUNT): the bound
	 * of the steering-rate log memory. */
	RATE_MEMORY_SLOTS = 32,
	/* A third of full throttle, where the player's throttle scale
	 * changes branch (player.c). */
	BASE_THROTTLE_SCALE = 0x5555,
	/* Angle per player-rate segment, yaw and bank, and pitch (player.c). */
	YAW_SEGMENT = 0x3800,
	PITCH_SEGMENT = 0x1400,
	/* A player's full stick: the axis at 127 times 120 for yaw and bank,
	 * and times 50 doubled for pitch (flight_input.c, player.c). */
	YAW_STICK = 127 * 120,
	PITCH_STICK = 127 * 50 * 2
};

/* What the last sith.steering_rates line for a slot said. */
struct rate_memory {
	int logged;
	uint16_t signature;
	uint8_t rank;
	uint16_t scale;
};
static struct rate_memory g_rate_memory[RATE_MEMORY_SLOTS];

void xvt_sith_set_setting(int setting)
{
	g_setting = setting == XVT_SITH_APPRENTICES || setting == XVT_SITH_LORD
			    ? setting
			    : XVT_SITH_NONE;
}

int xvt_sith_get_setting(void) { return g_setting; }

void xvt_sith_set_from_description(int described)
{
	xvt_sith_set_setting(described);
}

const char *xvt_sith_setting_name(int setting)
{
	switch (setting) {
	case XVT_SITH_APPRENTICES:
		return "apprentices";
	case XVT_SITH_LORD:
		return "lord";
	default:
		return "none";
	}
}

void xvt_sith_note_craft_destroyed(int ai_rank)
{
	if (ai_rank == XVT_SITH_APPRENTICE_LEVEL) {
		++g_apprentices_lost;
	} else if (ai_rank == XVT_SITH_LORD_LEVEL) {
		++g_lord_lost;
	}
}

const char *xvt_sith_rank_tag(int ai_rank)
{
	switch (ai_rank) {
	case XVT_SITH_APPRENTICE_LEVEL:
		return " Sith";
	case XVT_SITH_LORD_LEVEL:
		return " Sith Lord";
	default:
		return "";
	}
}

int xvt_sith_append_target_tag(unsigned int object_slot, int log_change)
{
	if (g_object_table == NULL ||
	    object_slot >=
		    (unsigned int)g_active_region_craft_object_slot_end) {
		return 0;
	}
	const struct mobile_object *mobile = g_object_table[object_slot].mobj;
	if (mobile == NULL || mobile->p_craft == NULL) {
		return 0;
	}
	int ai_rank = mobile->p_craft->ai_rank;
	const char *tag = xvt_sith_rank_tag(ai_rank);
	if (tag[0] == '\0') {
		return 0;
	}
	flight_text_append_scratch_string(tag);
	if (log_change) {
		XVT_LOG_DEBUG("sith.target_tag slot=%u rank=%d tag=\"%s\"",
			      object_slot, ai_rank, tag);
	}
	return 1;
}

void xvt_sith_get_report(struct xvt_sith_report *out)
{
	memset(out, 0, sizeof(*out));
	out->setting = g_setting;
	out->flight_group = -1;
	out->craft_type = -1;
	if (g_setting == XVT_SITH_NONE) {
		return;
	}
	snprintf(out->why, sizeof(out->why), "%s", g_none_reason);
	if (g_promoted_group < 0) {
		return;
	}
	const struct xvt_flight_group *group =
		&g_mission_flight_groups[g_promoted_group].fg;
	int spawned = g_mission_fg_stats[g_promoted_group].spawned_craft_count;
	out->flight_group = g_promoted_group;
	out->craft_type = group->craft_type;
	memcpy(out->flight_name, group->name, sizeof(group->name));
	out->flight_name[sizeof(group->name)] = '\0';
	out->lord_flown = g_lord_flight_group >= 0 && spawned > 0;
	out->apprentices_flown = spawned - out->lord_flown;
	out->apprentices_lost = g_apprentices_lost;
	out->lord_lost = g_lord_lost;
}

void xvt_sith_end_flight(void)
{
	g_setting = XVT_SITH_NONE;
	g_lord_flight_group = -1;
	g_promoted_group = -1;
	g_none_reason[0] = '\0';
	g_apprentices_lost = 0;
	g_lord_lost = 0;
	memset(g_rate_memory, 0, sizeof g_rate_memory);
}

int xvt_sith_lord_flight_group(void) { return g_lord_flight_group; }

int xvt_sith_craft_level(int flight_group, int group_level, int craft_number)
{
	if (flight_group >= 0 && flight_group == g_lord_flight_group &&
	    craft_number == 0) {
		return XVT_SITH_LORD_LEVEL;
	}
	return group_level;
}

/* 1 when flight group index flies on a team hostile to player_team, by the
 * test the Hard difficulty uses: another team that does not count player_team
 * among its allies. */
static int is_hostile(int index, int player_team)
{
	int team = g_mission_flight_groups[index].fg.team;
	return player_team == team
		       ? 0
		       : g_mission_teams[team].allies[player_team] == 0;
}

/* 1 when the flight group's craft type is a starfighter. */
static int flies_starfighters(int index)
{
	unsigned craft_type = g_mission_flight_groups[index].fg.craft_type;
	return craft_type < CRAFT_TYPE_COUNT &&
	       g_object_type_table[g_craft_type_to_object_type[craft_type]]
			       .genus_id == CRAFT_GENUS_STARFIGHTER;
}

void xvt_sith_promote_flight_groups(void)
{
	g_lord_flight_group = -1;
	g_promoted_group = -1;
	g_none_reason[0] = '\0';
	g_apprentices_lost = 0;
	g_lord_lost = 0;
	memset(g_rate_memory, 0, sizeof g_rate_memory);
	if (g_setting == XVT_SITH_NONE) {
		return;
	}
	int player_team = (uint16_t)g_players[0].team;
	int groups = g_mission_header.num_flight_groups;
	int hostile_computer = 0;
	int chosen = -1;
	for (int index = 0; index < groups; ++index) {
		if (g_mission_flight_groups[index].fg.player_number != 0 ||
		    !is_hostile(index, player_team)) {
			continue;
		}
		++hostile_computer;
		if (flies_starfighters(index) &&
		    (chosen < 0 ||
		     g_mission_flight_groups[index].fg.group_ai >
			     g_mission_flight_groups[chosen].fg.group_ai)) {
			chosen = index;
		}
	}
	if (chosen < 0) {
		snprintf(
			g_none_reason, sizeof(g_none_reason), "%s",
			hostile_computer == 0
				? "no hostile flight group that no player can fly"
				: "none of those groups flies starfighters");
		XVT_LOG_DEBUG(
			"sith.none_to_promote setting=\"%s\" groups=%d hostile_computer=%d reason=\"%s\"",
			xvt_sith_setting_name(g_setting), groups,
			hostile_computer, g_none_reason);
		return;
	}
	struct mission_flight_group *group = &g_mission_flight_groups[chosen];
	g_promoted_group = chosen;
	int level_was = group->fg.group_ai;
	if (level_was < XVT_SITH_APPRENTICE_LEVEL) {
		group->fg.group_ai = XVT_SITH_APPRENTICE_LEVEL;
	}
	if (g_setting == XVT_SITH_LORD) {
		g_lord_flight_group = chosen;
	}
	XVT_LOG_DEBUG(
		"sith.promoted setting=\"%s\" fg=%d team=%d craft=%d count=%d level_was=%d level=%d lord_rank=%d",
		xvt_sith_setting_name(g_setting), chosen, (int)group->fg.team,
		(int)group->fg.craft_type, (int)group->fg.number_of_craft,
		level_was, (int)group->fg.group_ai,
		g_setting == XVT_SITH_LORD ? XVT_SITH_LORD_LEVEL : 0);
}

/* A player's throttle scale (player.c): below a third of full throttle,
 * twice the throttle plus a third; from there up, falling from full to a
 * half. */
static uint16_t throttle_scale(uint16_t throttle)
{
	if (throttle < BASE_THROTTLE_SCALE) {
		return (uint16_t)(2 * throttle + BASE_THROTTLE_SCALE);
	}
	return (uint16_t)((BASE_THROTTLE_SCALE - throttle) / 2 - 1);
}

/* The angle per 236 ticks a player turns at with the stick fully over, for a
 * model rate, a throttle scale, a segment and a full stick (player.c): the
 * scaled rate counts whole segments of the full stick plus a share of one,
 * never below the craft's own rate and never above 0xFFFF. */
static uint16_t rate_at_full_stick(uint16_t model_rate, uint16_t scale,
				   unsigned segment, unsigned stick,
				   uint16_t own)
{
	uint16_t scaled = (uint16_t)math2_fraction(model_rate, scale);
	unsigned whole = scaled / segment;
	uint16_t share = math2_ratio_q16((uint16_t)(scaled % segment),
					 (uint16_t)segment);
	uint32_t value = math2_fraction((uint16_t)stick, share) +
			 (uint32_t)stick * whole;
	if (value < own) {
		value = own;
	}
	return value > 0xFFFFu ? 0xFFFFu : (uint16_t)value;
}

void xvt_sith_steering_rates(unsigned int object_slot,
			     const struct craft_data *craft,
			     struct xvt_sith_rates *out)
{
	out->turn = (uint16_t)craft->ai_flight.turn_rate;
	out->pitch = (uint16_t)craft->ai_flight.pitch_rate;
	out->roll = (uint16_t)craft->ai_flight.roll_rate;
	if (craft->ai_rank != XVT_SITH_APPRENTICE_LEVEL &&
	    craft->ai_rank != XVT_SITH_LORD_LEVEL) {
		return;
	}
	uint16_t throttle = craft->throttle_speed;
	if ((craft->working_subsystems & CRAFT_SUBSYSTEM_FLAG_ENGINES) == 0) {
		throttle = 0;
	}
	uint16_t scale = throttle_scale(throttle);
	uint16_t model_roll = (uint16_t)craft->ai_flight.roll_rate;
	uint16_t model_pitch = (uint16_t)craft->ai_flight.pitch_rate;
	out->turn = rate_at_full_stick(model_roll, scale, YAW_SEGMENT,
				       YAW_STICK, out->turn);
	out->pitch = rate_at_full_stick(model_pitch, scale, PITCH_SEGMENT,
					PITCH_STICK, out->pitch);
	out->roll = rate_at_full_stick(model_roll, scale, YAW_SEGMENT,
				       YAW_STICK, out->roll);

	if (object_slot >= RATE_MEMORY_SLOTS) {
		return;
	}
	uint16_t signature =
		g_object_table != NULL
			? g_object_table[object_slot].object_signature
			: 0;
	struct rate_memory *last = &g_rate_memory[object_slot];
	if (last->logged && last->signature == signature &&
	    last->rank == craft->ai_rank && last->scale == scale) {
		return;
	}
	last->logged = 1;
	last->signature = signature;
	last->rank = craft->ai_rank;
	last->scale = scale;
	XVT_LOG_DEBUG(
		"sith.steering_rates slot=%u rank=%d throttle=%u scale=%u turn=%u pitch=%u roll=%u",
		object_slot, (int)craft->ai_rank, (unsigned)throttle,
		(unsigned)scale, (unsigned)out->turn, (unsigned)out->pitch,
		(unsigned)out->roll);
}
