#include "xvt_runtime/timing/player_timing.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"

struct player_timing {
	int64_t remainder[XVT_PLAYER_CHANNELS];
	int8_t direction[XVT_PLAYER_CHANNELS];
	int32_t recovery[3];
	uint64_t recovery_serial;
	uint64_t lock_serial;
	uint16_t slot;
	uint16_t signature;
	uint16_t lock_signature;
	uint16_t lock_target;
	uint16_t lock_target_signature;
	uint16_t lock_weapon;
	unsigned lock_mode;
	unsigned lock_odd_tick;
	unsigned control_mode;
	uint16_t camera_focus;
	int control_valid;
	int recovery_valid;
};

static struct player_timing g_players_timing[8];

void xvt_player_timing_reset(void)
{
	memset(g_players_timing, 0, sizeof g_players_timing);
}

void xvt_player_timing_reset_controls(void)
{
	/* Network camera motion belongs to replayed player state, not device sampling. */
	if (!xvt_flight_timing_is_network125()) {
		for (unsigned i = XVT_PLAYER_CAMERA_YAW; i <= XVT_PLAYER_ZOOM;
		     ++i) {
			g_players_timing[g_local_player].remainder[i] = 0;
			g_players_timing[g_local_player].direction[i] = 0;
		}
	}
}

static struct player_timing *entry(unsigned player)
{
	if (player >= XVT_FLIGHT_PLAYERS) {
		return NULL;
	}
	struct player_timing *s = &g_players_timing[player];
	int slot = g_players[player].object_index;
	if (g_object_table && slot >= 0 &&
	    slot < g_region_main_object_slot_end &&
	    (s->slot != slot ||
	     s->signature != g_object_table[slot].object_signature)) {
		memset(s, 0, sizeof *s);
		s->slot = slot;
		s->signature = g_object_table[slot].object_signature;
	}
	return s;
}

void xvt_player_timing_begin_world(void)
{
	xvt_player_timing_reset();
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
		int slot = g_players[i].object_index;
		if (!g_object_table || slot < 0 ||
		    slot >= g_region_main_object_slot_end ||
		    !g_object_table[slot].object_type) {
			continue;
		}
		struct player_timing *state = entry(i);
		state->recovery[0] = g_object_table[slot].world_x;
		state->recovery[1] = g_object_table[slot].world_y;
		state->recovery[2] = g_object_table[slot].world_z;
		state->recovery_valid = 1;
	}
}

void xvt_player_timing_begin_controls(unsigned player)
{
	struct player_timing *s = entry(player);
	if (!s) {
		return;
	}
	const struct player_data *p = &g_players[player];
	const struct mobile_object *mobile =
		p->object_index >= 0 ? g_object_table[p->object_index].mobj
				     : NULL;
	const struct craft_data *craft = mobile ? mobile->p_craft : NULL;
	unsigned disabled =
		craft &&
		(!(craft->working_subsystems &
		   CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS) ||
		 (craft->beam_effect_accum[1] && !craft->chaff_active_seconds));
	unsigned mode =
		((g_flight_key_mods & XVT_ROLL_MODIFIER_MASK) ==
				 XVT_ROLL_MODIFIER
			 ? XVT_CONTROL_ROLL
			 : 0) |
		(disabled ? XVT_CONTROL_DISABLED : 0) |
		(p->view_state.player_input_blocked ? XVT_CONTROL_BLOCKED : 0) |
		(p->map_camera_state ? XVT_CONTROL_MAP : 0) |
		(p->hyperspace_phase ? XVT_CONTROL_HYPERSPACE : 0);
	if (!s->control_valid || s->control_mode != mode) {
		const unsigned channels[] = {
			XVT_PLAYER_YAW, XVT_PLAYER_PITCH, XVT_PLAYER_ROLL,
			XVT_PLAYER_SLEW_YAW, XVT_PLAYER_SLEW_PITCH};
		for (unsigned i = 0; i < 5; ++i) {
			s->remainder[channels[i]] = 0;
			s->direction[channels[i]] = 0;
		}
	}
	if (s->camera_focus != p->view_state.camera_focus_obj_idx) {
		for (unsigned i = XVT_PLAYER_CAMERA_YAW; i <= XVT_PLAYER_ZOOM;
		     ++i) {
			s->remainder[i] = 0;
			s->direction[i] = 0;
		}
	}
	s->control_valid = 1;
	s->control_mode = mode;
	s->camera_focus = p->view_state.camera_focus_obj_idx;
}

void xvt_player_timing_clear(unsigned player, unsigned channel)
{
	struct player_timing *s = entry(player);
	if (s && channel < XVT_PLAYER_CHANNELS) {
		s->remainder[channel] = 0;
		s->direction[channel] = 0;
	}
}

int xvt_player_timing_scale(unsigned player, unsigned channel, int value,
			    unsigned elapsed, unsigned divisor)
{
	if (!divisor || channel >= XVT_PLAYER_CHANNELS) {
		return 0;
	}
	struct player_timing *s = entry(player);
	int sign = (value > 0) - (value < 0);
	if (s && s->direction[channel] != sign) {
		s->remainder[channel] = 0;
		s->direction[channel] = sign;
	}
	int64_t numerator =
		(int64_t)value * elapsed + (s ? s->remainder[channel] : 0);
	if (s) {
		s->remainder[channel] = numerator % divisor;
	}
	int64_t result = numerator / divisor;
	return result < INT_MIN	  ? INT_MIN
	       : result > INT_MAX ? INT_MAX
				  : (int)result;
}

int xvt_player_timing_slew(unsigned player, unsigned channel, int difference)
{
	int magnitude = abs(difference);
	if (magnitude < 8) {
		xvt_player_timing_clear(player, channel);
		return difference;
	}
	int step = magnitude / 29;
	if (!step) {
		step = 1;
	}
	step = xvt_player_timing_scale(player, channel,
				       difference < 0 ? -4 * step : 4 * step,
				       g_elapsed_ticks, 8);
	if (abs(step) >= magnitude) {
		xvt_player_timing_clear(player, channel);
		return difference;
	}
	return step;
}

unsigned xvt_player_timing_lock_half(unsigned player, unsigned mode)
{
	if (!xvt_flight_timing_is_unlocked()) {
		return g_elapsed_ticks >> 1;
	}
	struct player_timing *s = entry(player);
	if (!s) {
		return 0;
	}
	int slot = g_players[player].object_index;
	unsigned target = (uint16_t)g_players[player].current_target_object_idx;
	uint16_t target_signature =
		target < (unsigned)(g_region_main_object_slot_end +
				    g_region_static_object_slot_count)
			? g_object_table[target].object_signature
			: 0;
	uint16_t signature =
		slot >= 0 ? g_object_table[slot].object_signature : 0;
	if (s->lock_serial + 1 != xvt_flight_timing_advance_serial() ||
	    s->lock_mode != mode || s->lock_signature != signature ||
	    s->lock_target != target ||
	    s->lock_target_signature != target_signature ||
	    s->lock_weapon != g_players[player].selected_weapon_bank) {
		s->lock_odd_tick = 0;
	}
	s->lock_serial = xvt_flight_timing_advance_serial();
	s->lock_mode = mode;
	s->lock_signature = signature;
	s->lock_target = target;
	s->lock_target_signature = target_signature;
	s->lock_weapon = g_players[player].selected_weapon_bank;
	if (!mode) {
		s->lock_odd_tick = 0;
		return 0;
	}
	unsigned elapsed = s->lock_odd_tick + g_elapsed_ticks;
	s->lock_odd_tick = elapsed % 2;
	return elapsed / 2;
}

int xvt_player_timing_record_recovery(unsigned player, int32_t position[3])
{
	struct player_timing *s = entry(player);
	if (!s || !xvt_flight_timing_reference_due() ||
	    s->recovery_serial == xvt_flight_timing_advance_serial()) {
		return 0;
	}
	const struct object_record *o =
		&g_object_table[g_players[player].object_index];
	if (!s->recovery_valid) {
		s->recovery[0] = o->mobj->prev_world_x;
		s->recovery[1] = o->mobj->prev_world_y;
		s->recovery[2] = o->mobj->prev_world_z;
	}
	memcpy(position, s->recovery, sizeof s->recovery);
	s->recovery[0] = o->world_x;
	s->recovery[1] = o->world_y;
	s->recovery[2] = o->world_z;
	s->recovery_valid = 1;
	s->recovery_serial = xvt_flight_timing_advance_serial();
	return 1;
}

void xvt_player_timing_recover(unsigned player)
{
	struct player_timing *s = entry(player);
	if (!s) {
		return;
	}
	const struct object_record *o =
		&g_object_table[g_players[player].object_index];
	xvt_flight_integration_reset_slot_and_motion(
		(unsigned)g_players[player].object_index);
	memset(s->remainder, 0, sizeof s->remainder);
	memset(s->direction, 0, sizeof s->direction);
	s->recovery[0] = o->world_x;
	s->recovery[1] = o->world_y;
	s->recovery[2] = o->world_z;
	s->recovery_valid = 1;
}

static const unsigned g_shared_channels[XVT_STATE_PLAYER_CHANNELS] = {
	XVT_PLAYER_YAW,		 XVT_PLAYER_PITCH,	XVT_PLAYER_ROLL,
	XVT_PLAYER_SLEW_YAW,	 XVT_PLAYER_SLEW_PITCH, XVT_PLAYER_CAMERA_YAW,
	XVT_PLAYER_CAMERA_PITCH, XVT_PLAYER_DISTANCE,	XVT_PLAYER_ZOOM};

typedef char xvt_player_timing_schema_channels
	[(XVT_PLAYER_CHANNELS == XVT_STATE_PLAYER_CHANNELS) ? 1 : -1];

void xvt_player_timing_reset_shared(void)
{
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
		struct player_timing *s = &g_players_timing[i];
		for (unsigned c = 0; c < XVT_STATE_PLAYER_CHANNELS; ++c) {
			s->remainder[g_shared_channels[c]] = 0;
			s->direction[g_shared_channels[c]] = 0;
		}
		s->slot = s->signature = s->lock_signature = s->lock_target =
			s->lock_target_signature = s->lock_weapon = 0;
		s->lock_mode = s->lock_odd_tick = s->control_mode =
			s->control_valid = 0;
		s->lock_serial = 0;
		s->camera_focus = 0;
	}
}

void xvt_player_timing_encode(unsigned player,
			      struct xvt_player_timing_wire *out)
{
	memset(out, 0, sizeof *out);
	out->player = player;
	if (player >= XVT_FLIGHT_PLAYERS) {
		return;
	}
	const struct player_timing *state = &g_players_timing[player];
	int slot = g_players[player].object_index;
	if (slot < 0 || slot >= g_region_main_object_slot_end ||
	    !g_object_table[slot].object_type || state->slot != slot ||
	    state->signature != g_object_table[slot].object_signature) {
		return;
	}
	out->valid = 1;
	xvt_wire_set16(out->slot, state->slot);
	xvt_wire_set16(out->signature, state->signature);
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		xvt_wire_set64(
			out->remainder[i],
			(uint64_t)state->remainder[g_shared_channels[i]]);
		out->direction[i] = state->direction[g_shared_channels[i]];
	}
	out->lock_mode = state->lock_mode;
	out->lock_odd_tick = state->lock_odd_tick;
	out->control_valid = state->control_valid;
	xvt_wire_set32(out->control_mode, state->control_mode);
	xvt_wire_set16(out->lock_signature, state->lock_signature);
	xvt_wire_set16(out->lock_target, state->lock_target);
	xvt_wire_set16(out->lock_target_signature,
		       state->lock_target_signature);
	xvt_wire_set16(out->lock_weapon, state->lock_weapon);
	xvt_wire_set64(out->lock_serial, state->lock_serial);
	xvt_wire_set16(out->camera_focus, state->camera_focus);
}

int xvt_player_timing_decode(const struct xvt_player_timing_wire *record,
			     int apply)
{
	unsigned player = record->player;
	unsigned slot = xvt_wire_get16(record->slot);
	if (player >= XVT_FLIGHT_PLAYERS || record->valid > 1 ||
	    xvt_wire_get16(record->reserved) ||
	    record->lock_mode > XVT_LOCK_HALF_TARGET_LOSS ||
	    record->lock_odd_tick > 1 || record->control_valid > 1 ||
	    (xvt_wire_get32(record->control_mode) & ~XVT_CONTROL_MASK) ||
	    xvt_wire_get16(record->reserved_tail)) {
		return 0;
	}
	if (!record->valid) {
		struct xvt_player_timing_wire empty = {0};
		empty.player = player;
		if (memcmp(record, &empty, sizeof empty)) {
			return 0;
		}
	}
	if (record->valid && slot >= (unsigned)g_region_main_object_slot_end) {
		return 0;
	}
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		int64_t remainder =
			(int64_t)xvt_wire_get64(record->remainder[i]);
		if (record->direction[i] < -1 || record->direction[i] > 1 ||
		    remainder <= -SIMULATION_TICKS_PER_SECOND ||
		    remainder >= SIMULATION_TICKS_PER_SECOND) {
			return 0;
		}
	}
	if (!apply) {
		return 1;
	}
	struct player_timing *state = &g_players_timing[player];
	state->slot = slot;
	state->signature = xvt_wire_get16(record->signature);
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		state->remainder[g_shared_channels[i]] =
			(int64_t)xvt_wire_get64(record->remainder[i]);
		state->direction[g_shared_channels[i]] = record->direction[i];
	}
	state->lock_mode = record->lock_mode;
	state->lock_odd_tick = record->lock_odd_tick;
	state->control_valid = record->control_valid;
	state->control_mode = xvt_wire_get32(record->control_mode);
	state->lock_signature = xvt_wire_get16(record->lock_signature);
	state->lock_target = xvt_wire_get16(record->lock_target);
	state->lock_target_signature =
		xvt_wire_get16(record->lock_target_signature);
	state->lock_weapon = xvt_wire_get16(record->lock_weapon);
	state->lock_serial = xvt_wire_get64(record->lock_serial);
	state->camera_focus = xvt_wire_get16(record->camera_focus);
	return 1;
}
