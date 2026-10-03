#include "xvt_runtime/runtime/flight_checkpoint.h"
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
#include <string.h>

static struct XvtPairedMotionWire g_paired[XVT_FLIGHT_PLAYERS];
static struct XvtMembershipWire g_membership;

static unsigned XvtFlightCheckpoint_Slots(void)
{
	return g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
}

static int XvtFlightCheckpoint_Shared(unsigned slot)
{
	return slot < XvtFlightCheckpoint_Slots() &&
	       !(slot >= (unsigned)g_localTransientSlotStart &&
		 slot < (unsigned)g_localDebrisSlotEnd);
}

void XvtFlightCheckpoint_Begin(uint8_t mask)
{
	memset(g_paired, 0, sizeof g_paired);
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		g_paired[player].player = player;
	}
	memset(&g_membership, 0, sizeof g_membership);
	g_membership.initial = g_membership.confirmed = mask;
}

uint8_t XvtFlightCheckpoint_InitialMask(void) { return g_membership.initial; }

uint8_t XvtFlightCheckpoint_ConfirmedMask(void)
{
	return g_membership.confirmed;
}

void XvtFlightCheckpoint_ApplyConfirmedMask(uint8_t mask)
{
	g_membership.confirmed = mask & g_membership.initial;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		g_playerAbortFlags[player] =
			(g_membership.initial & (1u << player)) &&
			!(g_membership.confirmed & (1u << player));
	}
}

size_t XvtFlightCheckpoint_Maximum(void)
{
	return sizeof(struct XvtStateHeader) +
	       XvtFlightCheckpoint_Slots() *
		       sizeof(struct XvtObjectMotionWire) +
	       XVT_FLIGHT_PLAYERS * sizeof(struct XvtPlayerTimingWire) +
	       sizeof g_paired + sizeof g_membership +
	       sizeof(struct XvtStateFooter);
}

void XvtFlightCheckpoint_InvalidatePlayer(unsigned player)
{
	if (player >= XVT_FLIGHT_PLAYERS) {
		return;
	}
	memset(&g_paired[player], 0, sizeof g_paired[player]);
	g_paired[player].player = player;
}

static unsigned XvtFlightCheckpoint_Carried(unsigned slot)
{
	if (!XvtFlightCheckpoint_Shared(slot)) {
		return UINT16_MAX;
	}
	const struct MobileObject *mobile = g_objectTable[slot].mobj;
	return mobile && mobile->pCraft ? mobile->pCraft->carriedObjectIndex
					: UINT16_MAX;
}

static void
XvtFlightCheckpoint_SaveMotion(unsigned slot,
			       struct XvtObjectIdentityWire *identity,
			       struct XvtObjectMotionWire *motion)
{
	XvtWire_Set16(identity->slot, slot);
	XvtWire_Set16(identity->signature, g_objectTable[slot].objectSignature);
	XvtReferenceMotion_Encode(slot, &motion->reference);
	XvtFlightIntegration_Encode(slot, &motion->integration);
}

void XvtFlightCheckpoint_SavePlayer(unsigned player, int tick)
{
	if (!XvtFlightTiming_IsNetwork125() || player >= XVT_FLIGHT_PLAYERS) {
		return;
	}
	XvtFlightCheckpoint_InvalidatePlayer(player);
	unsigned slot = g_players[player].objectIndex;
	if (!XvtFlightCheckpoint_Shared(slot) ||
	    !g_objectTable[slot].objectType) {
		return;
	}
	struct XvtPairedMotionWire *out = &g_paired[player];
	out->validity = XVT_PAIRED_OWNER_VALID;
	XvtWire_Set32(out->saved_tick, (unsigned)tick);
	XvtFlightCheckpoint_SaveMotion(slot, &out->owner_id, &out->owner);
	unsigned carried = XvtFlightCheckpoint_Carried(slot);
	if (carried != slot && XvtFlightCheckpoint_Shared(carried) &&
	    g_objectTable[carried].objectType) {
		out->validity |= XVT_PAIRED_CARRIED_VALID;
		XvtFlightCheckpoint_SaveMotion(carried, &out->carried_id,
					       &out->carried);
	}
}

void XvtFlightCheckpoint_RestorePlayer(unsigned player)
{
	if (!XvtFlightTiming_IsNetwork125() || player >= XVT_FLIGHT_PLAYERS) {
		return;
	}
	const struct XvtPairedMotionWire *saved = &g_paired[player];
	unsigned slot = XvtWire_Get16(saved->owner_id.slot);
	if (!(saved->validity & XVT_PAIRED_OWNER_VALID) ||
	    slot != (unsigned)g_players[player].objectIndex ||
	    !XvtFlightCheckpoint_Shared(slot) ||
	    XvtWire_Get16(saved->owner_id.signature) !=
		    g_objectTable[slot].objectSignature ||
	    XvtWire_Get32(saved->saved_tick) !=
		    (unsigned)g_players[player].lockstepTimestamp) {
		if (XvtFlightCheckpoint_Shared(
			    (unsigned)g_players[player].objectIndex)) {
			XvtFlightIntegration_ResetSlotAndMotion(
				g_players[player].objectIndex);
		}
		XvtFlightCheckpoint_InvalidatePlayer(player);
		return;
	}
	XvtFlightIntegration_Decode(&saved->owner.integration, 1);
	XvtReferenceMotion_Decode(&saved->owner.reference, 1);
	unsigned carried = XvtFlightCheckpoint_Carried(slot);
	if ((saved->validity & XVT_PAIRED_CARRIED_VALID) &&
	    carried == XvtWire_Get16(saved->carried_id.slot) &&
	    XvtFlightCheckpoint_Shared(carried) &&
	    XvtWire_Get16(saved->carried_id.signature) ==
		    g_objectTable[carried].objectSignature) {
		XvtFlightIntegration_Decode(&saved->carried.integration, 1);
		XvtReferenceMotion_Decode(&saved->carried.reference, 1);
	} else if (XvtFlightCheckpoint_Shared(carried)) {
		XvtFlightIntegration_ResetSlotAndMotion(carried);
	}
}

static int
XvtFlightCheckpoint_ValidateMotion(const struct XvtObjectIdentityWire *identity,
				   const struct XvtObjectMotionWire *motion)
{
	unsigned slot = XvtWire_Get16(identity->slot);
	return XvtFlightCheckpoint_Shared(slot) &&
	       XvtWire_Get16(motion->reference.slot) == slot &&
	       XvtWire_Get16(motion->integration.slot) == slot &&
	       XvtReferenceMotion_Decode(&motion->reference, 0) &&
	       XvtFlightIntegration_Decode(&motion->integration, 0);
}

static int
XvtFlightCheckpoint_ValidatePaired(const struct XvtPairedMotionWire *record)
{
	if (record->player >= XVT_FLIGHT_PLAYERS ||
	    (record->validity &
	     ~(XVT_PAIRED_OWNER_VALID | XVT_PAIRED_CARRIED_VALID)) ||
	    record->validity == XVT_PAIRED_CARRIED_VALID ||
	    XvtWire_Get16(record->reserved)) {
		return 0;
	}
	if (!record->validity) {
		struct XvtPairedMotionWire empty = {0};
		empty.player = record->player;
		return !memcmp(record, &empty, sizeof empty);
	}
	if (!XvtFlightWire_ValidTick(XvtWire_Get32(record->saved_tick)) ||
	    !XvtFlightCheckpoint_ValidateMotion(&record->owner_id,
						&record->owner)) {
		return 0;
	}
	if (record->validity & XVT_PAIRED_CARRIED_VALID) {
		return XvtFlightCheckpoint_ValidateMotion(&record->carried_id,
							  &record->carried);
	}
	return XvtWire_IsZero(&record->carried_id, sizeof record->carried_id) &&
	       XvtWire_IsZero(&record->carried, sizeof record->carried);
}

size_t XvtFlightCheckpoint_Append(uint8_t *image, size_t prefix)
{
	uint8_t *start = image + prefix;
	uint8_t *cursor = start + sizeof(struct XvtStateHeader);
	unsigned count = 0;
	for (unsigned slot = 0; slot < XvtFlightCheckpoint_Slots(); ++slot) {
		if (!XvtFlightCheckpoint_Shared(slot)) {
			continue;
		}
		struct XvtReferenceMotionWire record;
		XvtReferenceMotion_Encode(slot, &record);
		memcpy(cursor, &record, sizeof record);
		cursor += sizeof record;
		++count;
	}
	for (unsigned slot = 0; slot < XvtFlightCheckpoint_Slots(); ++slot) {
		if (!XvtFlightCheckpoint_Shared(slot)) {
			continue;
		}
		struct XvtIntegrationWire record;
		XvtFlightIntegration_Encode(slot, &record);
		memcpy(cursor, &record, sizeof record);
		cursor += sizeof record;
	}
	struct XvtStateHeader header;
	XvtWire_Set16(header.schema, XVT_STATE_SCHEMA);
	XvtWire_Set16(header.reference_count, count);
	XvtWire_Set16(header.integration_count, count);
	XvtWire_Set16(header.player_count, XVT_FLIGHT_PLAYERS);
	memcpy(start, &header, sizeof header);
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		struct XvtPlayerTimingWire record;
		XvtPlayerTiming_Encode(player, &record);
		memcpy(cursor, &record, sizeof record);
		cursor += sizeof record;
	}
	memcpy(cursor, g_paired, sizeof g_paired);
	cursor += sizeof g_paired;
	memcpy(cursor, &g_membership, sizeof g_membership);
	cursor += sizeof g_membership;
	size_t extension = cursor - start;
	struct XvtStateFooter footer;
	XvtWire_Set32(footer.magic, XVT_STATE_MAGIC);
	XvtWire_Set16(footer.schema, XVT_STATE_SCHEMA);
	XvtWire_Set16(footer.profile, XVT_WIRE_PROFILE_NETWORK_125);
	XvtWire_Set32(footer.cookie, XvtFlightNetwork_Cookie());
	XvtWire_Set32(footer.completed_tick, g_gameTime);
	XvtWire_Set32(footer.world_bytes, prefix);
	XvtWire_Set32(footer.timing_bytes, extension);
	XvtWire_Set32(footer.timing_crc,
		      XvtFlightWire_Crc32c(start, extension));
	memcpy(cursor, &footer, sizeof footer);
	return prefix + extension + sizeof footer;
}

int XvtFlightCheckpoint_Read(const uint8_t *image, size_t size,
			     struct XvtFlightCheckpointView *view)
{
	if (size < sizeof(struct XvtStateFooter)) {
		return 0;
	}
	struct XvtStateFooter footer;
	memcpy(&footer, image + size - sizeof footer, sizeof footer);
	if (XvtWire_Get32(footer.magic) != XVT_STATE_MAGIC ||
	    XvtWire_Get16(footer.schema) != XVT_STATE_SCHEMA ||
	    XvtWire_Get16(footer.profile) != XVT_WIRE_PROFILE_NETWORK_125 ||
	    XvtWire_Get32(footer.cookie) != XvtFlightNetwork_Cookie()) {
		return 0;
	}
	view->tick = (int)XvtWire_Get32(footer.completed_tick);
	view->prefix = XvtWire_Get32(footer.world_bytes);
	size_t length = XvtWire_Get32(footer.timing_bytes);
	size_t fixed = sizeof(struct XvtStateHeader) +
		       XVT_FLIGHT_PLAYERS * sizeof(struct XvtPlayerTimingWire) +
		       sizeof g_paired + sizeof g_membership;
	if (view->tick < 0 || view->tick % XVT_NETWORK_STEP_TICKS ||
	    view->prefix > size - sizeof footer ||
	    length != size - sizeof footer - view->prefix || length < fixed) {
		return 0;
	}
	const uint8_t *start = image + view->prefix;
	if (XvtWire_Get32(footer.timing_crc) !=
	    XvtFlightWire_Crc32c(start, length)) {
		return 0;
	}
	struct XvtStateHeader header;
	memcpy(&header, start, sizeof header);
	unsigned expected = 0;
	for (unsigned slot = 0; slot < XvtFlightCheckpoint_Slots(); ++slot) {
		expected += XvtFlightCheckpoint_Shared(slot);
	}
	view->object_count = XvtWire_Get16(header.reference_count);
	if (XvtWire_Get16(header.schema) != XVT_STATE_SCHEMA ||
	    XvtWire_Get16(header.player_count) != XVT_FLIGHT_PLAYERS ||
	    view->object_count != expected ||
	    XvtWire_Get16(header.integration_count) != expected ||
	    length != fixed + expected * sizeof(struct XvtObjectMotionWire)) {
		return 0;
	}
	view->reference = start + sizeof header;
	view->integration = view->reference +
			    expected * sizeof(struct XvtReferenceMotionWire);
	view->players = view->integration +
			expected * sizeof(struct XvtIntegrationWire);
	view->paired = view->players +
		       XVT_FLIGHT_PLAYERS * sizeof(struct XvtPlayerTimingWire);
	memcpy(&view->membership, view->paired + sizeof g_paired,
	       sizeof view->membership);
	unsigned row = 0;
	for (unsigned slot = 0; slot < XvtFlightCheckpoint_Slots(); ++slot) {
		if (!XvtFlightCheckpoint_Shared(slot)) {
			continue;
		}
		struct XvtReferenceMotionWire reference;
		struct XvtIntegrationWire integration;
		memcpy(&reference, view->reference + row * sizeof reference,
		       sizeof reference);
		memcpy(&integration,
		       view->integration + row * sizeof integration,
		       sizeof integration);
		++row;
		if (XvtWire_Get16(reference.slot) != slot ||
		    XvtWire_Get16(integration.slot) != slot ||
		    !XvtReferenceMotion_Decode(&reference, 0) ||
		    !XvtFlightIntegration_Decode(&integration, 0)) {
			return 0;
		}
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		struct XvtPlayerTimingWire record;
		struct XvtPairedMotionWire paired;
		memcpy(&record, view->players + player * sizeof record,
		       sizeof record);
		memcpy(&paired, view->paired + player * sizeof paired,
		       sizeof paired);
		if (record.player != player || paired.player != player ||
		    !XvtPlayerTiming_Decode(&record, 0) ||
		    !XvtFlightCheckpoint_ValidatePaired(&paired)) {
			return 0;
		}
	}
	return view->membership.initial == g_membership.initial &&
	       !(view->membership.confirmed & ~view->membership.initial) &&
	       !XvtWire_Get16(view->membership.reserved);
}

int XvtFlightCheckpoint_Validate(const uint8_t *image, size_t size,
				 size_t *prefix, int *tick)
{
	struct XvtFlightCheckpointView view;
	if (!XvtFlightCheckpoint_Read(image, size, &view)) {
		return 0;
	}
	*prefix = view.prefix;
	*tick = view.tick;
	return 1;
}

void XvtFlightCheckpoint_Restore(const struct XvtFlightCheckpointView *view)
{
	XvtReferenceMotion_ResetShared();
	XvtFlightIntegration_ResetShared();
	XvtPlayerTiming_ResetShared();
	for (unsigned row = 0; row < view->object_count; ++row) {
		struct XvtReferenceMotionWire reference;
		struct XvtIntegrationWire integration;
		memcpy(&reference, view->reference + row * sizeof reference,
		       sizeof reference);
		memcpy(&integration,
		       view->integration + row * sizeof integration,
		       sizeof integration);
		XvtReferenceMotion_Decode(&reference, 1);
		XvtFlightIntegration_Decode(&integration, 1);
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		struct XvtPlayerTimingWire record;
		memcpy(&record, view->players + player * sizeof record,
		       sizeof record);
		XvtPlayerTiming_Decode(&record, 1);
	}
	memcpy(g_paired, view->paired, sizeof g_paired);
	XvtFlightCheckpoint_ApplyConfirmedMask(view->membership.confirmed);
	g_gameTime = view->tick;
	XvtFlightTiming_RestoreNetworkTick(view->tick);
}
