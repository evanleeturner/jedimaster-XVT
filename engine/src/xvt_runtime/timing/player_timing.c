#include "xvt_runtime/timing/player_timing.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct PlayerTiming {
	int64_t remainder[XVT_PLAYER_CHANNELS];
	int8_t direction[XVT_PLAYER_CHANNELS];
	int32_t recovery[3];
	uint64_t recovery_serial, lock_serial;
	uint16_t slot, signature, lock_signature, lock_target, lock_target_signature, lock_weapon;
	unsigned lock_mode, lock_half;
	unsigned control_mode;
	uint16_t camera_focus;
	int control_valid;
	int recovery_valid;
} PlayerTiming;

static PlayerTiming g_playersTiming[8];

void XvtPlayerTiming_Reset(void) { memset(g_playersTiming, 0, sizeof g_playersTiming); }

void XvtPlayerTiming_ResetControls(void) {
	/* Network camera motion belongs to replayed player state, not device sampling. */
	if (!XvtFlightTiming_IsNetwork125())
		for (unsigned i = XVT_PLAYER_CAMERA_YAW; i <= XVT_PLAYER_ZOOM; ++i) {
			g_playersTiming[g_localPlayer].remainder[i] = 0;
			g_playersTiming[g_localPlayer].direction[i] = 0;
		}
}

static PlayerTiming* Entry(unsigned player) {
	if (player >= XVT_FLIGHT_PLAYERS)
		return NULL;
	PlayerTiming* s = &g_playersTiming[player];
	int slot = g_players[player].objectIndex;
	if (g_objectTable && slot >= 0 && slot < g_regionMainObjectSlotEnd &&
		(s->slot != slot || s->signature != g_objectTable[slot].objectSignature)) {
		memset(s, 0, sizeof *s);
		s->slot = slot;
		s->signature = g_objectTable[slot].objectSignature;
	}
	return s;
}

void XvtPlayerTiming_BeginWorld(void) {
	XvtPlayerTiming_Reset();
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
		int slot = g_players[i].objectIndex;
		if (!g_objectTable || slot < 0 || slot >= g_regionMainObjectSlotEnd ||
			!g_objectTable[slot].objectType)
			continue;
		PlayerTiming* state = Entry(i);
		state->recovery[0] = g_objectTable[slot].world_x;
		state->recovery[1] = g_objectTable[slot].world_y;
		state->recovery[2] = g_objectTable[slot].world_z;
		state->recovery_valid = 1;
	}
}

void XvtPlayerTiming_BeginControls(unsigned player) {
	PlayerTiming* s = Entry(player);
	if (!s)
		return;
	const PlayerData* p = &g_players[player];
	const MobileObject* mobile = p->objectIndex >= 0 ? g_objectTable[p->objectIndex].mobj : NULL;
	const CraftData* craft = mobile ? mobile->pCraft : NULL;
	unsigned disabled = craft && (!(craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS) ||
								  (craft->beamEffectAccum[1] && !craft->chaffActiveTimer));
	unsigned mode =
		((g_flightKeyMods & XVT_ROLL_MODIFIER_MASK) == XVT_ROLL_MODIFIER ? XVT_CONTROL_ROLL : 0) |
		(disabled ? XVT_CONTROL_DISABLED : 0) | (p->viewState.playerInputBlocked ? XVT_CONTROL_BLOCKED : 0) |
		(p->mapCameraState ? XVT_CONTROL_MAP : 0) | (p->hyperspacePhase ? XVT_CONTROL_HYPERSPACE : 0);
	if (!s->control_valid || s->control_mode != mode) {
		const unsigned channels[] = { XVT_PLAYER_YAW, XVT_PLAYER_PITCH, XVT_PLAYER_ROLL, XVT_PLAYER_SLEW_YAW,
									  XVT_PLAYER_SLEW_PITCH };
		for (unsigned i = 0; i < 5; ++i) {
			s->remainder[channels[i]] = 0;
			s->direction[channels[i]] = 0;
		}
	}
	if (s->camera_focus != p->viewState.cameraFocusObjIdx) {
		for (unsigned i = XVT_PLAYER_CAMERA_YAW; i <= XVT_PLAYER_ZOOM; ++i) {
			s->remainder[i] = 0;
			s->direction[i] = 0;
		}
	}
	s->control_valid = 1;
	s->control_mode = mode;
	s->camera_focus = p->viewState.cameraFocusObjIdx;
}

void XvtPlayerTiming_Clear(unsigned player, unsigned channel) {
	PlayerTiming* s = Entry(player);
	if (s && channel < XVT_PLAYER_CHANNELS) {
		s->remainder[channel] = 0;
		s->direction[channel] = 0;
	}
}

int XvtPlayerTiming_Scale(unsigned player, unsigned channel, int value, unsigned elapsed, unsigned divisor) {
	if (!divisor || channel >= XVT_PLAYER_CHANNELS)
		return 0;
	PlayerTiming* s = Entry(player);
	int sign = (value > 0) - (value < 0);
	if (s && s->direction[channel] != sign) {
		s->remainder[channel] = 0;
		s->direction[channel] = sign;
	}
	int64_t numerator = (int64_t)value * elapsed + (s ? s->remainder[channel] : 0);
	if (s)
		s->remainder[channel] = numerator % divisor;
	int64_t result = numerator / divisor;
	return result < INT_MIN ? INT_MIN : result > INT_MAX ? INT_MAX : (int)result;
}

int XvtPlayerTiming_Slew(unsigned player, unsigned channel, int difference) {
	int magnitude = abs(difference);
	if (magnitude < 8) {
		XvtPlayerTiming_Clear(player, channel);
		return difference;
	}
	int step = magnitude / 29;
	if (!step)
		step = 1;
	step = XvtPlayerTiming_Scale(player, channel, difference < 0 ? -4 * step : 4 * step, g_elapsedTicks, 8);
	if (abs(step) >= magnitude) {
		XvtPlayerTiming_Clear(player, channel);
		return difference;
	}
	return step;
}

unsigned XvtPlayerTiming_LockHalf(unsigned player, unsigned mode) {
	if (!XvtFlightTiming_IsUnlocked())
		return g_elapsedTicks >> 1;
	PlayerTiming* s = Entry(player);
	if (!s)
		return 0;
	int slot = g_players[player].objectIndex;
	unsigned target = (uint16_t)g_players[player].currentTargetObjectIdx;
	uint16_t target_signature = target < (unsigned)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount)
									? g_objectTable[target].objectSignature
									: 0;
	uint16_t signature = slot >= 0 ? g_objectTable[slot].objectSignature : 0;
	if (s->lock_serial + 1 != XvtFlightTiming_AdvanceSerial() || s->lock_mode != mode ||
		s->lock_signature != signature || s->lock_target != target ||
		s->lock_target_signature != target_signature || s->lock_weapon != g_players[player].selectedWarhead) {
		s->lock_half = 0;
	}
	s->lock_serial = XvtFlightTiming_AdvanceSerial();
	s->lock_mode = mode;
	s->lock_signature = signature;
	s->lock_target = target;
	s->lock_target_signature = target_signature;
	s->lock_weapon = g_players[player].selectedWarhead;
	if (!mode) {
		s->lock_half = 0;
		return 0;
	}
	unsigned elapsed = s->lock_half + g_elapsedTicks;
	s->lock_half = elapsed % 2;
	return elapsed / 2;
}

int XvtPlayerTiming_RecordRecovery(unsigned player, int32_t position[3]) {
	PlayerTiming* s = Entry(player);
	if (!s || !XvtFlightTiming_ReferenceDue() || s->recovery_serial == XvtFlightTiming_AdvanceSerial())
		return 0;
	const ObjectRecord* o = &g_objectTable[g_players[player].objectIndex];
	if (!s->recovery_valid) {
		s->recovery[0] = o->mobj->prevWorldX;
		s->recovery[1] = o->mobj->prevWorldY;
		s->recovery[2] = o->mobj->prevWorldZ;
	}
	memcpy(position, s->recovery, sizeof s->recovery);
	s->recovery[0] = o->world_x;
	s->recovery[1] = o->world_y;
	s->recovery[2] = o->world_z;
	s->recovery_valid = 1;
	s->recovery_serial = XvtFlightTiming_AdvanceSerial();
	return 1;
}

void XvtPlayerTiming_Recover(unsigned player) {
	PlayerTiming* s = Entry(player);
	if (!s)
		return;
	const ObjectRecord* o = &g_objectTable[g_players[player].objectIndex];
	XvtFlightIntegration_Reset((unsigned)g_players[player].objectIndex);
	memset(s->remainder, 0, sizeof s->remainder);
	memset(s->direction, 0, sizeof s->direction);
	s->recovery[0] = o->world_x;
	s->recovery[1] = o->world_y;
	s->recovery[2] = o->world_z;
	s->recovery_valid = 1;
}

static const unsigned g_sharedChannels[XVT_STATE_PLAYER_CHANNELS] = {
	XVT_PLAYER_YAW,          XVT_PLAYER_PITCH,      XVT_PLAYER_ROLL,
	XVT_PLAYER_SLEW_YAW,     XVT_PLAYER_SLEW_PITCH, XVT_PLAYER_CAMERA_YAW,
	XVT_PLAYER_CAMERA_PITCH, XVT_PLAYER_DISTANCE,   XVT_PLAYER_ZOOM
};

typedef char XvtPlayerTimingSchemaChannels[(XVT_PLAYER_CHANNELS == XVT_STATE_PLAYER_CHANNELS) ? 1 : -1];

void XvtPlayerTiming_ResetShared(void) {
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
		PlayerTiming* s = &g_playersTiming[i];
		for (unsigned c = 0; c < XVT_STATE_PLAYER_CHANNELS; ++c) {
			s->remainder[g_sharedChannels[c]] = 0;
			s->direction[g_sharedChannels[c]] = 0;
		}
		s->slot = s->signature = s->lock_signature = s->lock_target = s->lock_target_signature =
			s->lock_weapon = 0;
		s->lock_mode = s->lock_half = s->control_mode = s->control_valid = 0;
		s->lock_serial = 0;
		s->camera_focus = 0;
	}
}

void XvtPlayerTiming_Encode(unsigned player, XvtPlayerTimingWire* out) {
	memset(out, 0, sizeof *out);
	out->player = player;
	if (player >= XVT_FLIGHT_PLAYERS)
		return;
	const PlayerTiming* state = &g_playersTiming[player];
	int slot = g_players[player].objectIndex;
	if (slot < 0 || slot >= g_regionMainObjectSlotEnd || !g_objectTable[slot].objectType ||
		state->slot != slot || state->signature != g_objectTable[slot].objectSignature)
		return;
	out->valid = 1;
	XvtWire_Set16(out->slot, state->slot);
	XvtWire_Set16(out->signature, state->signature);
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		XvtWire_Set64(out->remainder[i], (uint64_t)state->remainder[g_sharedChannels[i]]);
		out->direction[i] = state->direction[g_sharedChannels[i]];
	}
	out->lock_mode = state->lock_mode;
	out->lock_half = state->lock_half;
	out->control_valid = state->control_valid;
	XvtWire_Set32(out->control_mode, state->control_mode);
	XvtWire_Set16(out->lock_signature, state->lock_signature);
	XvtWire_Set16(out->lock_target, state->lock_target);
	XvtWire_Set16(out->lock_target_signature, state->lock_target_signature);
	XvtWire_Set16(out->lock_weapon, state->lock_weapon);
	XvtWire_Set64(out->lock_frame, state->lock_serial);
	XvtWire_Set16(out->camera_focus, state->camera_focus);
}

int XvtPlayerTiming_Decode(const XvtPlayerTimingWire* record, int apply) {
	unsigned player = record->player, slot = XvtWire_Get16(record->slot);
	if (player >= XVT_FLIGHT_PLAYERS || record->valid > 1 || XvtWire_Get16(record->reserved) ||
		record->lock_mode > XVT_LOCK_HALF_TARGET_LOSS || record->lock_half > 1 || record->control_valid > 1 ||
		(XvtWire_Get32(record->control_mode) & ~XVT_CONTROL_MASK) || XvtWire_Get16(record->reserved_tail))
		return 0;
	if (!record->valid) {
		XvtPlayerTimingWire empty = { 0 };
		empty.player = player;
		if (memcmp(record, &empty, sizeof empty))
			return 0;
	}
	if (record->valid && slot >= (unsigned)g_regionMainObjectSlotEnd)
		return 0;
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		int64_t remainder = (int64_t)XvtWire_Get64(record->remainder[i]);
		if (record->direction[i] < -1 || record->direction[i] > 1 ||
			remainder <= -SIMULATION_TICKS_PER_SECOND || remainder >= SIMULATION_TICKS_PER_SECOND)
			return 0;
	}
	if (!apply)
		return 1;
	PlayerTiming* state = &g_playersTiming[player];
	state->slot = slot;
	state->signature = XvtWire_Get16(record->signature);
	for (unsigned i = 0; i < XVT_STATE_PLAYER_CHANNELS; ++i) {
		state->remainder[g_sharedChannels[i]] = (int64_t)XvtWire_Get64(record->remainder[i]);
		state->direction[g_sharedChannels[i]] = record->direction[i];
	}
	state->lock_mode = record->lock_mode;
	state->lock_half = record->lock_half;
	state->control_valid = record->control_valid;
	state->control_mode = XvtWire_Get32(record->control_mode);
	state->lock_signature = XvtWire_Get16(record->lock_signature);
	state->lock_target = XvtWire_Get16(record->lock_target);
	state->lock_target_signature = XvtWire_Get16(record->lock_target_signature);
	state->lock_weapon = XvtWire_Get16(record->lock_weapon);
	state->lock_serial = XvtWire_Get64(record->lock_frame);
	state->camera_focus = XvtWire_Get16(record->camera_focus);
	return 1;
}
