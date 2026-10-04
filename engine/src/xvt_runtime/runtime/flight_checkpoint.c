#include "xvt_runtime/runtime/flight_checkpoint.h"

#include <string.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_net.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

static struct xvt_paired_motion_wire g_paired[XVT_FLIGHT_PLAYERS];
static struct xvt_membership_wire g_membership;

static unsigned xvt_flight_checkpoint_slots(void)
{
	return g_region_main_object_slot_end +
	       g_region_static_object_slot_count;
}

static int xvt_flight_checkpoint_shared(unsigned slot)
{
	return slot < xvt_flight_checkpoint_slots() &&
	       !(slot >= (unsigned)g_local_transient_slot_start &&
		 slot < (unsigned)g_local_debris_slot_end);
}

void xvt_flight_checkpoint_begin(uint8_t mask)
{
	memset(g_paired, 0, sizeof g_paired);
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		g_paired[player].player = player;
	}
	memset(&g_membership, 0, sizeof g_membership);
	g_membership.confirmed = mask;
	g_membership.initial = g_membership.confirmed;
}

uint8_t xvt_flight_checkpoint_initial_mask(void)
{
	return g_membership.initial;
}

uint8_t xvt_flight_checkpoint_confirmed_mask(void)
{
	return g_membership.confirmed;
}

void xvt_flight_checkpoint_apply_confirmed_mask(uint8_t mask)
{
	g_membership.confirmed = mask & g_membership.initial;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		g_player_abort_flags[player] =
			(g_membership.initial & (1u << player)) &&
			!(g_membership.confirmed & (1u << player));
	}
}

size_t xvt_flight_checkpoint_maximum(void)
{
	return sizeof(struct xvt_state_header) +
	       xvt_flight_checkpoint_slots() *
		       sizeof(struct xvt_object_motion_wire) +
	       XVT_FLIGHT_PLAYERS * sizeof(struct xvt_player_timing_wire) +
	       sizeof g_paired + sizeof g_membership +
	       sizeof(struct xvt_state_footer);
}

void xvt_flight_checkpoint_invalidate_player(unsigned player)
{
	if (player >= XVT_FLIGHT_PLAYERS) {
		return;
	}
	memset(&g_paired[player], 0, sizeof g_paired[player]);
	g_paired[player].player = player;
}

static unsigned xvt_flight_checkpoint_carried(unsigned slot)
{
	if (!xvt_flight_checkpoint_shared(slot)) {
		return UINT16_MAX;
	}
	const struct mobile_object *mobile = g_object_table[slot].mobj;
	return mobile && mobile->p_craft ? mobile->p_craft->carried_object_index
					 : UINT16_MAX;
}

static void
xvt_flight_checkpoint_save_motion(unsigned slot,
				  struct xvt_object_identity_wire *identity,
				  struct xvt_object_motion_wire *motion)
{
	xvt_wire_set16(identity->slot, slot);
	xvt_wire_set16(identity->signature,
		       g_object_table[slot].object_signature);
	xvt_reference_motion_encode(slot, &motion->reference);
	xvt_flight_integration_encode(slot, &motion->integration);
}

void xvt_flight_checkpoint_save_player(unsigned player, int tick)
{
	if (!xvt_flight_timing_is_network125() ||
	    player >= XVT_FLIGHT_PLAYERS) {
		return;
	}
	xvt_flight_checkpoint_invalidate_player(player);
	unsigned slot = g_players[player].object_index;
	if (!xvt_flight_checkpoint_shared(slot) ||
	    !g_object_table[slot].object_type) {
		return;
	}
	struct xvt_paired_motion_wire *out = &g_paired[player];
	out->validity = XVT_PAIRED_OWNER_VALID;
	xvt_wire_set32(out->saved_tick, (unsigned)tick);
	xvt_flight_checkpoint_save_motion(slot, &out->owner_id, &out->owner);
	unsigned carried = xvt_flight_checkpoint_carried(slot);
	if (carried != slot && xvt_flight_checkpoint_shared(carried) &&
	    g_object_table[carried].object_type) {
		out->validity |= XVT_PAIRED_CARRIED_VALID;
		xvt_flight_checkpoint_save_motion(carried, &out->carried_id,
						  &out->carried);
	}
}

void xvt_flight_checkpoint_restore_player(unsigned player)
{
	if (!xvt_flight_timing_is_network125() ||
	    player >= XVT_FLIGHT_PLAYERS) {
		return;
	}
	const struct xvt_paired_motion_wire *saved = &g_paired[player];
	unsigned slot = xvt_wire_get16(saved->owner_id.slot);
	if (!(saved->validity & XVT_PAIRED_OWNER_VALID) ||
	    slot != (unsigned)g_players[player].object_index ||
	    !xvt_flight_checkpoint_shared(slot) ||
	    xvt_wire_get16(saved->owner_id.signature) !=
		    g_object_table[slot].object_signature ||
	    xvt_wire_get32(saved->saved_tick) !=
		    (unsigned)g_players[player].lockstep_timestamp) {
		if (xvt_flight_checkpoint_shared(
			    (unsigned)g_players[player].object_index)) {
			xvt_flight_integration_reset_slot_and_motion(
				g_players[player].object_index);
		}
		xvt_flight_checkpoint_invalidate_player(player);
		return;
	}
	xvt_flight_integration_decode(&saved->owner.integration, 1);
	xvt_reference_motion_decode(&saved->owner.reference, 1);
	unsigned carried = xvt_flight_checkpoint_carried(slot);
	if ((saved->validity & XVT_PAIRED_CARRIED_VALID) &&
	    carried == xvt_wire_get16(saved->carried_id.slot) &&
	    xvt_flight_checkpoint_shared(carried) &&
	    xvt_wire_get16(saved->carried_id.signature) ==
		    g_object_table[carried].object_signature) {
		xvt_flight_integration_decode(&saved->carried.integration, 1);
		xvt_reference_motion_decode(&saved->carried.reference, 1);
	} else if (xvt_flight_checkpoint_shared(carried)) {
		xvt_flight_integration_reset_slot_and_motion(carried);
	}
}

static int xvt_flight_checkpoint_validate_motion(
	const struct xvt_object_identity_wire *identity,
	const struct xvt_object_motion_wire *motion)
{
	unsigned slot = xvt_wire_get16(identity->slot);
	return xvt_flight_checkpoint_shared(slot) &&
	       xvt_wire_get16(motion->reference.slot) == slot &&
	       xvt_wire_get16(motion->integration.slot) == slot &&
	       xvt_reference_motion_decode(&motion->reference, 0) &&
	       xvt_flight_integration_decode(&motion->integration, 0);
}

static int xvt_flight_checkpoint_validate_paired(
	const struct xvt_paired_motion_wire *record)
{
	if (record->player >= XVT_FLIGHT_PLAYERS ||
	    (record->validity &
	     ~(XVT_PAIRED_OWNER_VALID | XVT_PAIRED_CARRIED_VALID)) ||
	    record->validity == XVT_PAIRED_CARRIED_VALID ||
	    xvt_wire_get16(record->reserved)) {
		return 0;
	}
	if (!record->validity) {
		struct xvt_paired_motion_wire empty = {0};
		empty.player = record->player;
		return !memcmp(record, &empty, sizeof empty);
	}
	if (!xvt_flight_wire_valid_tick(xvt_wire_get32(record->saved_tick)) ||
	    !xvt_flight_checkpoint_validate_motion(&record->owner_id,
						   &record->owner)) {
		return 0;
	}
	if (record->validity & XVT_PAIRED_CARRIED_VALID) {
		return xvt_flight_checkpoint_validate_motion(
			&record->carried_id, &record->carried);
	}
	return xvt_wire_is_zero(&record->carried_id,
				sizeof record->carried_id) &&
	       xvt_wire_is_zero(&record->carried, sizeof record->carried);
}

size_t xvt_flight_checkpoint_append(uint8_t *image, size_t prefix)
{
	uint8_t *start = image + prefix;
	uint8_t *cursor = start + sizeof(struct xvt_state_header);
	unsigned count = 0;
	for (unsigned slot = 0; slot < xvt_flight_checkpoint_slots(); ++slot) {
		if (!xvt_flight_checkpoint_shared(slot)) {
			continue;
		}
		struct xvt_reference_motion_wire record;
		xvt_reference_motion_encode(slot, &record);
		memcpy(cursor, &record, sizeof record);
		cursor += sizeof record;
		++count;
	}
	for (unsigned slot = 0; slot < xvt_flight_checkpoint_slots(); ++slot) {
		if (!xvt_flight_checkpoint_shared(slot)) {
			continue;
		}
		struct xvt_integration_wire record;
		xvt_flight_integration_encode(slot, &record);
		memcpy(cursor, &record, sizeof record);
		cursor += sizeof record;
	}
	struct xvt_state_header header;
	xvt_wire_set16(header.schema, XVT_STATE_SCHEMA);
	xvt_wire_set16(header.reference_count, count);
	xvt_wire_set16(header.integration_count, count);
	xvt_wire_set16(header.player_count, XVT_FLIGHT_PLAYERS);
	memcpy(start, &header, sizeof header);
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		struct xvt_player_timing_wire record;
		xvt_player_timing_encode(player, &record);
		memcpy(cursor, &record, sizeof record);
		cursor += sizeof record;
	}
	memcpy(cursor, g_paired, sizeof g_paired);
	cursor += sizeof g_paired;
	memcpy(cursor, &g_membership, sizeof g_membership);
	cursor += sizeof g_membership;
	size_t extension = cursor - start;
	struct xvt_state_footer footer;
	xvt_wire_set32(footer.magic, XVT_STATE_MAGIC);
	xvt_wire_set16(footer.schema, XVT_STATE_SCHEMA);
	xvt_wire_set16(footer.profile, XVT_WIRE_PROFILE_NETWORK_125);
	xvt_wire_set32(footer.cookie, xvt_flight_network_cookie());
	xvt_wire_set32(footer.completed_tick, g_game_time);
	xvt_wire_set32(footer.world_bytes, prefix);
	xvt_wire_set32(footer.timing_bytes, extension);
	xvt_wire_set32(footer.timing_crc,
		       xvt_flight_wire_crc32c(start, extension));
	memcpy(cursor, &footer, sizeof footer);
	return prefix + extension + sizeof footer;
}

int xvt_flight_checkpoint_read(const uint8_t *image, size_t size,
			       struct xvt_flight_checkpoint_view *view)
{
	if (size < sizeof(struct xvt_state_footer)) {
		return 0;
	}
	struct xvt_state_footer footer;
	memcpy(&footer, image + size - sizeof footer, sizeof footer);
	if (xvt_wire_get32(footer.magic) != XVT_STATE_MAGIC ||
	    xvt_wire_get16(footer.schema) != XVT_STATE_SCHEMA ||
	    xvt_wire_get16(footer.profile) != XVT_WIRE_PROFILE_NETWORK_125 ||
	    xvt_wire_get32(footer.cookie) != xvt_flight_network_cookie()) {
		return 0;
	}
	view->tick = (int)xvt_wire_get32(footer.completed_tick);
	view->prefix = xvt_wire_get32(footer.world_bytes);
	size_t length = xvt_wire_get32(footer.timing_bytes);
	size_t fixed =
		sizeof(struct xvt_state_header) +
		XVT_FLIGHT_PLAYERS * sizeof(struct xvt_player_timing_wire) +
		sizeof g_paired + sizeof g_membership;
	if (view->tick < 0 || view->tick % XVT_NETWORK_STEP_TICKS ||
	    view->prefix > size - sizeof footer ||
	    length != size - sizeof footer - view->prefix || length < fixed) {
		return 0;
	}
	const uint8_t *start = image + view->prefix;
	if (xvt_wire_get32(footer.timing_crc) !=
	    xvt_flight_wire_crc32c(start, length)) {
		return 0;
	}
	struct xvt_state_header header;
	memcpy(&header, start, sizeof header);
	unsigned expected = 0;
	for (unsigned slot = 0; slot < xvt_flight_checkpoint_slots(); ++slot) {
		expected += xvt_flight_checkpoint_shared(slot);
	}
	view->object_count = xvt_wire_get16(header.reference_count);
	if (xvt_wire_get16(header.schema) != XVT_STATE_SCHEMA ||
	    xvt_wire_get16(header.player_count) != XVT_FLIGHT_PLAYERS ||
	    view->object_count != expected ||
	    xvt_wire_get16(header.integration_count) != expected ||
	    length !=
		    fixed + expected * sizeof(struct xvt_object_motion_wire)) {
		return 0;
	}
	view->reference = start + sizeof header;
	view->integration = view->reference +
			    expected * sizeof(struct xvt_reference_motion_wire);
	view->players = view->integration +
			expected * sizeof(struct xvt_integration_wire);
	view->paired =
		view->players +
		XVT_FLIGHT_PLAYERS * sizeof(struct xvt_player_timing_wire);
	memcpy(&view->membership, view->paired + sizeof g_paired,
	       sizeof view->membership);
	unsigned row = 0;
	for (unsigned slot = 0; slot < xvt_flight_checkpoint_slots(); ++slot) {
		if (!xvt_flight_checkpoint_shared(slot)) {
			continue;
		}
		struct xvt_reference_motion_wire reference;
		struct xvt_integration_wire integration;
		memcpy(&reference, view->reference + row * sizeof reference,
		       sizeof reference);
		memcpy(&integration,
		       view->integration + row * sizeof integration,
		       sizeof integration);
		++row;
		if (xvt_wire_get16(reference.slot) != slot ||
		    xvt_wire_get16(integration.slot) != slot ||
		    !xvt_reference_motion_decode(&reference, 0) ||
		    !xvt_flight_integration_decode(&integration, 0)) {
			return 0;
		}
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		struct xvt_player_timing_wire record;
		struct xvt_paired_motion_wire paired;
		memcpy(&record, view->players + player * sizeof record,
		       sizeof record);
		memcpy(&paired, view->paired + player * sizeof paired,
		       sizeof paired);
		if (record.player != player || paired.player != player ||
		    !xvt_player_timing_decode(&record, 0) ||
		    !xvt_flight_checkpoint_validate_paired(&paired)) {
			return 0;
		}
	}
	return view->membership.initial == g_membership.initial &&
	       !(view->membership.confirmed & ~view->membership.initial) &&
	       !xvt_wire_get16(view->membership.reserved);
}

int xvt_flight_checkpoint_validate(const uint8_t *image, size_t size,
				   size_t *prefix, int *tick)
{
	struct xvt_flight_checkpoint_view view;
	if (!xvt_flight_checkpoint_read(image, size, &view)) {
		return 0;
	}
	*prefix = view.prefix;
	*tick = view.tick;
	return 1;
}

void xvt_flight_checkpoint_restore(
	const struct xvt_flight_checkpoint_view *view)
{
	xvt_reference_motion_reset_shared();
	xvt_flight_integration_reset_shared();
	xvt_player_timing_reset_shared();
	for (unsigned row = 0; row < view->object_count; ++row) {
		struct xvt_reference_motion_wire reference;
		struct xvt_integration_wire integration;
		memcpy(&reference, view->reference + row * sizeof reference,
		       sizeof reference);
		memcpy(&integration,
		       view->integration + row * sizeof integration,
		       sizeof integration);
		xvt_reference_motion_decode(&reference, 1);
		xvt_flight_integration_decode(&integration, 1);
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		struct xvt_player_timing_wire record;
		memcpy(&record, view->players + player * sizeof record,
		       sizeof record);
		xvt_player_timing_decode(&record, 1);
	}
	memcpy(g_paired, view->paired, sizeof g_paired);
	xvt_flight_checkpoint_apply_confirmed_mask(view->membership.confirmed);
	g_game_time = view->tick;
	xvt_flight_timing_restore_network_tick(view->tick);
}
