/* Checks the timing extension of a network125 world checkpoint (xvt_runtime/runtime/flight_checkpoint.h)
 * against the promises in its header: the membership masks and the abort flags SetMask raises, an extension
 * appended after world bytes and read back, what Read refuses, what Restore puts
 * back, and the per-player paired motion. No game data is read: the test builds a world of five object slots,
 * slots 0 to 3 in the main region and slot 4 in the static region, with slot 2 the local transient range, so
 * slots 0, 1, 3 and 4 are shared. Player 0 flies slot 0, whose craft can carry another object, and player 1
 * flies slot 1. Each check starts from that world in a fresh network125 timing session at game time TICK,
 * with the integration and reference motion tables made for it and a flight begun with players 0 and 1.
 *
 * Most checks watch one integration channel through xvt_flight_integration_rate (flight_integration.h): Seed
 * leaves a carried remainder of 3/4 on a slot, and Probe adds 1/4 more, so Probe returns 1 exactly when that
 * remainder is there. */
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_net.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_state.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	SLOTS = 5,
	MAIN_SLOTS = 4,
	LOCAL_SLOT = 2,
	SHARED_SLOTS = 4,
	PREFIX = 100,
	TICK = 40,
	CHANNEL = XVT_INTEGRATE_PUSH_X,
};

static struct object_record g_test_objects[SLOTS];
static struct mobile_object g_test_mobiles[SLOTS];
static struct craft_data g_test_craft;
static uint8_t *g_image, *g_saved, *g_again;
static size_t g_capacity;

static void flight_checkpoint_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(&g_test_craft, 0, sizeof g_test_craft);
	for (int i = 0; i < SLOTS; ++i) {
		g_test_objects[i].object_type = 1;
		g_test_objects[i].object_signature = (uint16_t)(0x300 + i);
		g_test_objects[i].world_x = 1000 * i;
		g_test_objects[i].mobj = &g_test_mobiles[i];
	}
	g_test_mobiles[0].p_craft = &g_test_craft;
	g_test_craft.carried_object_index = UINT16_MAX;
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = MAIN_SLOTS;
	g_region_static_object_slot_count = SLOTS - MAIN_SLOTS;
	g_local_transient_slot_start = LOCAL_SLOT;
	g_local_debris_slot_end = LOCAL_SLOT + 1;
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
		g_player_abort_flags[i] = 0;
	}
	g_players[0].object_index = 0;
	g_players[0].participation_state = 1;
	g_players[0].lockstep_timestamp = TICK;
	g_players[1].object_index = 1;
	g_players[1].participation_state = 1;
	g_local_player = 0;
	g_game_time = TICK;
	g_elapsed_ticks = XVT_NETWORK_STEP_TICKS;
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_NETWORK_125);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_init(SLOTS), 1);
	XVT_ASSERT_INT_EQ(xvt_reference_motion_init(SLOTS), 1);
	xvt_player_timing_reset();
	xvt_flight_checkpoint_begin(0x03);

	free(g_image);
	free(g_saved);
	free(g_again);
	g_capacity = PREFIX + xvt_flight_checkpoint_maximum();
	g_image = calloc(1, g_capacity);
	g_saved = calloc(1, g_capacity);
	g_again = calloc(1, g_capacity);
	XVT_ASSERT_TRUE(g_image && g_saved && g_again);
	for (int i = 0; i < PREFIX; ++i) {
		g_image[i] = (uint8_t)(i * 7 + 1);
	}
}

static void seed(unsigned slot)
{
	xvt_flight_integration_clear(slot, CHANNEL);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_rate(slot, CHANNEL, 3, 1, 4),
			  0);
}

static int probe(unsigned slot)
{
	return xvt_flight_integration_rate(slot, CHANNEL, 1, 1, 4);
}

static size_t append(uint8_t *image)
{
	return xvt_flight_checkpoint_append(image, PREFIX);
}

/* The player's paired record as an extension appended now carries it. */
static struct xvt_paired_motion_wire paired(unsigned player)
{
	size_t size = append(g_image);
	struct xvt_flight_checkpoint_view view;
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_read(g_image, size, &view), 1);
	struct xvt_paired_motion_wire record;
	memcpy(&record, view.paired + player * sizeof record, sizeof record);
	return record;
}

static struct xvt_state_footer flight_checkpoint_footer(const uint8_t *image,
							size_t size)
{
	struct xvt_state_footer footer;
	memcpy(&footer, image + size - sizeof footer, sizeof footer);
	return footer;
}

static void set_footer(uint8_t *image, size_t size,
		       const struct xvt_state_footer *footer)
{
	memcpy(image + size - sizeof *footer, footer, sizeof *footer);
}

/* After a change inside the extension, writes its CRC-32C into the footer again, so that Read judges the
 * change itself and not the CRC. */
static void reseal(uint8_t *image, size_t size)
{
	struct xvt_state_footer footer = flight_checkpoint_footer(image, size);
	xvt_wire_set32(footer.timing_crc,
		       xvt_flight_wire_crc32c(
			       image + xvt_wire_get32(footer.world_bytes),
			       xvt_wire_get32(footer.timing_bytes)));
	set_footer(image, size, &footer);
}

static int read_image(const uint8_t *image, size_t size)
{
	struct xvt_flight_checkpoint_view view;
	return xvt_flight_checkpoint_read(image, size, &view);
}

static void check_begin_and_masks(void)
{
	flight_checkpoint_world();
	xvt_flight_checkpoint_begin(0x07);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_initial_mask(), 0x07);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_confirmed_mask(), 0x07);

	/* Dropping an initial player raises its abort flag; every other player's flag is cleared. */
	g_player_abort_flags[6] = 1;
	xvt_flight_checkpoint_apply_confirmed_mask(0x05);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_confirmed_mask(), 0x05);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_initial_mask(), 0x07);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_player_abort_flags[i], i == 1);
	}

	/* The confirmed mask stays within the initial one. */
	xvt_flight_checkpoint_apply_confirmed_mask(0xFF);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_confirmed_mask(), 0x07);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_player_abort_flags[i], 0);
	}

	xvt_flight_checkpoint_apply_confirmed_mask(0);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_confirmed_mask(), 0);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_player_abort_flags[i], i < 3);
	}

	/* Begin sets both masks again and clears every paired record. */
	seed(0);
	xvt_flight_checkpoint_save_player(0, TICK);
	XVT_ASSERT_TRUE(paired(0).validity & XVT_PAIRED_OWNER_VALID);
	xvt_flight_checkpoint_begin(0x03);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_initial_mask(), 0x03);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_confirmed_mask(), 0x03);
	XVT_ASSERT_INT_EQ(paired(0).validity, 0);
}

static void check_append_read(void)
{
	flight_checkpoint_world();
	seed(0);
	seed(1);
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(0, XVT_PLAYER_YAW, 3, 1, 4),
			  0);
	size_t size = append(g_image);
	XVT_ASSERT_TRUE(size > PREFIX + sizeof(struct xvt_state_footer));
	XVT_ASSERT_TRUE(size - PREFIX <= xvt_flight_checkpoint_maximum());

	struct xvt_flight_checkpoint_view view;
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_read(g_image, size, &view), 1);
	XVT_ASSERT_INT_EQ(view.prefix, PREFIX);
	XVT_ASSERT_INT_EQ(view.tick, TICK);
	XVT_ASSERT_INT_EQ(view.object_count, SHARED_SLOTS);
	XVT_ASSERT_INT_EQ(view.membership.initial, 0x03);
	XVT_ASSERT_INT_EQ(view.membership.confirmed, 0x03);
	/* The view points into the image, after the world bytes. */
	const uint8_t *parts[] = {view.reference, view.integration,
				  view.players, view.paired};
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_TRUE(parts[i] >= g_image + PREFIX &&
				parts[i] < g_image + size);
	}

	size_t prefix = 0;
	int tick = -1;
	XVT_ASSERT_INT_EQ(
		xvt_flight_checkpoint_validate(g_image, size, &prefix, &tick),
		1);
	XVT_ASSERT_INT_EQ(prefix, PREFIX);
	XVT_ASSERT_INT_EQ(tick, TICK);

	/* The world bytes are not checked. */
	g_image[0] ^= 0xFF;
	g_image[PREFIX - 1] ^= 0xFF;
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 1);

	/* Read changes no state: an extension appended afterwards is the same. */
	XVT_ASSERT_INT_EQ(append(g_again), size);
	XVT_ASSERT_INT_EQ(
		memcmp(g_again + PREFIX, g_image + PREFIX, size - PREFIX), 0);
	XVT_ASSERT_INT_EQ(probe(0), 1);
	XVT_ASSERT_INT_EQ(probe(1), 1);

	/* The footer records the game time: a later time reads back as the tick. */
	g_game_time = TICK + 2 * XVT_NETWORK_STEP_TICKS;
	size = append(g_image);
	XVT_ASSERT_INT_EQ(
		xvt_flight_checkpoint_validate(g_image, size, &prefix, &tick),
		1);
	XVT_ASSERT_INT_EQ(tick, g_game_time);
}

static void check_read_refusals(void)
{
	flight_checkpoint_world();
	XVT_ASSERT_INT_EQ(xvt_flight_network_cookie(), 0);
	seed(0);
	size_t size = append(g_saved);
	memcpy(g_image, g_saved, size);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 1);
	struct xvt_flight_checkpoint_view view;
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_read(g_saved, size, &view), 1);
	size_t reference = (size_t)(view.reference - g_saved);
	size_t players = (size_t)(view.players - g_saved);
	size_t membership =
		(size_t)(view.paired - g_saved) +
		XVT_FLIGHT_PLAYERS * sizeof(struct xvt_paired_motion_wire);
	struct xvt_state_footer good = flight_checkpoint_footer(g_saved, size),
				footer;

	XVT_ASSERT_INT_EQ(
		read_image(g_image, sizeof(struct xvt_state_footer) - 1), 0);

	/* The footer's magic, schema, profile and cookie. */
	footer = good;
	xvt_wire_set32(footer.magic, xvt_wire_get32(good.magic) + 1);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	footer = good;
	xvt_wire_set16(footer.schema, xvt_wire_get16(good.schema) + 1);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	footer = good;
	xvt_wire_set16(footer.profile, xvt_wire_get16(good.profile) + 1);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	footer = good;
	xvt_wire_set32(footer.cookie, xvt_flight_network_cookie() + 1);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);

	/* A tick off the step, or negative. */
	footer = good;
	xvt_wire_set32(footer.completed_tick, TICK + 1);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	xvt_wire_set32(footer.completed_tick,
		       (uint32_t)-XVT_NETWORK_STEP_TICKS);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	xvt_wire_set32(footer.completed_tick, TICK + XVT_NETWORK_STEP_TICKS);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 1);

	/* Lengths that do not add up to the image. */
	footer = good;
	xvt_wire_set32(footer.world_bytes, PREFIX + 1);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	footer = good;
	xvt_wire_set32(footer.timing_bytes,
		       xvt_wire_get32(good.timing_bytes) - 1);
	set_footer(g_image, size, &footer);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	set_footer(g_image, size, &good);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 1);
	XVT_ASSERT_INT_EQ(read_image(g_image + 1, size - 1), 0);

	/* A changed byte in the extension fails the CRC. */
	g_image[reference] ^= 0x01;
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	memcpy(g_image, g_saved, size);

	/* The shared-slot count must be this world's. */
	g_local_debris_slot_end = LOCAL_SLOT;
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	g_local_debris_slot_end = LOCAL_SLOT + 1;
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 1);

	/* Every record must decode: a reference record with an unknown flag, a player record whose valid
	 * byte is neither 0 nor 1 (reference_motion.h and player_timing.h say which records are well formed). */
	g_image[reference +
		offsetof(struct xvt_reference_motion_wire, flags)] |= 0x80;
	reseal(g_image, size);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	memcpy(g_image, g_saved, size);
	g_image[players + offsetof(struct xvt_player_timing_wire, valid)] = 2;
	reseal(g_image, size);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	memcpy(g_image, g_saved, size);

	/* Membership: the confirmed mask must stay within the initial one... */
	g_image[membership + offsetof(struct xvt_membership_wire, confirmed)] =
		0x83;
	reseal(g_image, size);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	g_image[membership + offsetof(struct xvt_membership_wire, confirmed)] =
		0x01;
	reseal(g_image, size);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 1);
	/* ...and the initial mask must be this flight's. */
	memcpy(g_image, g_saved, size);
	xvt_flight_checkpoint_begin(0x07);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 0);
	xvt_flight_checkpoint_begin(0x03);
	XVT_ASSERT_INT_EQ(read_image(g_image, size), 1);
}

static void check_restore(void)
{
	flight_checkpoint_world();
	seed(0);
	seed(1);
	XVT_ASSERT_INT_EQ(xvt_player_timing_scale(0, XVT_PLAYER_YAW, 3, 1, 4),
			  0);
	xvt_flight_checkpoint_apply_confirmed_mask(0x01);
	size_t size = append(g_saved);
	memcpy(g_image, g_saved, size);
	struct xvt_flight_checkpoint_view view;
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_read(g_image, size, &view), 1);

	/* Move on: spend slot 0's remainder, seed one on slot 3, confirm both players again, and let time
	 * pass. */
	XVT_ASSERT_INT_EQ(probe(0), 1);
	seed(3);
	xvt_flight_checkpoint_apply_confirmed_mask(0x03);
	XVT_ASSERT_INT_EQ(g_player_abort_flags[1], 0);
	g_game_time = TICK + 10 * XVT_NETWORK_STEP_TICKS;

	xvt_flight_checkpoint_restore(&view);
	XVT_ASSERT_INT_EQ(g_game_time, TICK);
	/* The network tick is the view's (flight_timing.h: the advance serial is tick / step). */
	XVT_ASSERT_TRUE(xvt_flight_timing_advance_serial() ==
			TICK / XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(xvt_flight_checkpoint_confirmed_mask(), 0x01);
	XVT_ASSERT_INT_EQ(g_player_abort_flags[1], 1);

	/* Every record is back: the extension appended now is the one restored from. */
	XVT_ASSERT_INT_EQ(append(g_again), size);
	XVT_ASSERT_INT_EQ(
		memcmp(g_again + PREFIX, g_saved + PREFIX, size - PREFIX), 0);
	XVT_ASSERT_INT_EQ(probe(0), 1);
	XVT_ASSERT_INT_EQ(probe(3), 0);
}

static void check_save_player(void)
{
	flight_checkpoint_world();
	seed(0);
	xvt_flight_checkpoint_save_player(0, TICK);
	struct xvt_paired_motion_wire record = paired(0);
	XVT_ASSERT_INT_EQ(record.validity, XVT_PAIRED_OWNER_VALID);
	XVT_ASSERT_INT_EQ(record.player, 0);
	XVT_ASSERT_INT_EQ(xvt_wire_get32(record.saved_tick), TICK);
	XVT_ASSERT_INT_EQ(xvt_wire_get16(record.owner_id.slot), 0);
	XVT_ASSERT_INT_EQ(xvt_wire_get16(record.owner_id.signature), 0x300);
	/* Only player 0's record changed. */
	XVT_ASSERT_INT_EQ(paired(1).validity, 0);

	/* The record stays invalid when the player's object is not a live shared slot: a local slot, an
	 * empty slot, or none. */
	g_players[0].object_index = LOCAL_SLOT;
	xvt_flight_checkpoint_save_player(0, TICK);
	XVT_ASSERT_INT_EQ(paired(0).validity, 0);
	g_players[0].object_index = 1;
	g_test_objects[1].object_type = 0;
	xvt_flight_checkpoint_save_player(0, TICK);
	XVT_ASSERT_INT_EQ(paired(0).validity, 0);
	g_test_objects[1].object_type = 1;
	g_players[0].object_index = -1;
	xvt_flight_checkpoint_save_player(0, TICK);
	XVT_ASSERT_INT_EQ(paired(0).validity, 0);
	g_players[0].object_index = 0;

	/* A player out of range is ignored. */
	xvt_flight_checkpoint_save_player(XVT_FLIGHT_PLAYERS, TICK);
	xvt_flight_checkpoint_restore_player(XVT_FLIGHT_PLAYERS);

	/* InvalidatePlayer clears the record; out of range it is ignored. */
	xvt_flight_checkpoint_save_player(0, TICK);
	xvt_flight_checkpoint_save_player(1, TICK);
	xvt_flight_checkpoint_invalidate_player(0);
	xvt_flight_checkpoint_invalidate_player(XVT_FLIGHT_PLAYERS);
	XVT_ASSERT_INT_EQ(paired(0).validity, 0);
	XVT_ASSERT_INT_EQ(paired(1).validity, XVT_PAIRED_OWNER_VALID);
}

static void check_restore_player(void)
{
	/* A record saved at the player's lockstep tick for its current object is restored. */
	flight_checkpoint_world();
	seed(0);
	xvt_flight_checkpoint_save_player(0, TICK);
	XVT_ASSERT_INT_EQ(probe(0), 1);
	xvt_flight_checkpoint_restore_player(0);
	XVT_ASSERT_INT_EQ(probe(0), 1);

	/* Saved at another tick: the object's integration is reset... */
	flight_checkpoint_world();
	seed(0);
	xvt_flight_checkpoint_save_player(0, TICK);
	g_players[0].lockstep_timestamp = TICK + XVT_NETWORK_STEP_TICKS;
	xvt_flight_checkpoint_restore_player(0);
	XVT_ASSERT_INT_EQ(probe(0), 0);
	/* ...and the record invalidated: at the right tick again it no longer restores, it resets. */
	XVT_ASSERT_INT_EQ(paired(0).validity, 0);
	g_players[0].lockstep_timestamp = TICK;
	seed(0);
	xvt_flight_checkpoint_restore_player(0);
	XVT_ASSERT_INT_EQ(probe(0), 0);

	/* Saved for another slot: the integration of the player's current object is reset. */
	flight_checkpoint_world();
	seed(0);
	xvt_flight_checkpoint_save_player(0, TICK);
	g_players[0].object_index = 1;
	seed(1);
	xvt_flight_checkpoint_restore_player(0);
	XVT_ASSERT_INT_EQ(probe(1), 0);
	XVT_ASSERT_INT_EQ(paired(0).validity, 0);
}

static void check_carried_object(void)
{
	/* Slot 0's craft carries slot 3: both motions are saved, and both restored. */
	flight_checkpoint_world();
	g_test_craft.carried_object_index = 3;
	seed(0);
	seed(3);
	xvt_flight_checkpoint_save_player(0, TICK);
	struct xvt_paired_motion_wire record = paired(0);
	XVT_ASSERT_INT_EQ(record.validity,
			  XVT_PAIRED_OWNER_VALID | XVT_PAIRED_CARRIED_VALID);
	XVT_ASSERT_INT_EQ(xvt_wire_get16(record.carried_id.slot), 3);
	XVT_ASSERT_INT_EQ(xvt_wire_get16(record.carried_id.signature), 0x303);
	XVT_ASSERT_INT_EQ(probe(3), 1);
	xvt_flight_checkpoint_restore_player(0);
	XVT_ASSERT_INT_EQ(probe(3), 1);

	/* A carried object that is not another live shared slot is not saved: the local slot. */
	flight_checkpoint_world();
	g_test_craft.carried_object_index = LOCAL_SLOT;
	seed(0);
	xvt_flight_checkpoint_save_player(0, TICK);
	XVT_ASSERT_INT_EQ(paired(0).validity, XVT_PAIRED_OWNER_VALID);

	/* The craft now carries another shared object: that object's integration is reset. */
	flight_checkpoint_world();
	g_test_craft.carried_object_index = 3;
	seed(0);
	seed(3);
	xvt_flight_checkpoint_save_player(0, TICK);
	g_test_craft.carried_object_index = 4;
	seed(4);
	xvt_flight_checkpoint_restore_player(0);
	XVT_ASSERT_INT_EQ(probe(4), 0);
	XVT_ASSERT_INT_EQ(probe(0), 1);
}

static void check_network125_only(void)
{
	flight_checkpoint_world();
	seed(0);
	xvt_flight_checkpoint_save_player(0, TICK);

	/* Outside network125, SavePlayer leaves the record as it was and RestorePlayer does nothing, not even
	 * on a mismatch. */
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	xvt_flight_checkpoint_save_player(0, TICK + 8);
	xvt_flight_checkpoint_save_player(1, TICK);
	struct xvt_paired_motion_wire record = paired(0);
	XVT_ASSERT_INT_EQ(record.validity, XVT_PAIRED_OWNER_VALID);
	XVT_ASSERT_INT_EQ(xvt_wire_get32(record.saved_tick), TICK);
	XVT_ASSERT_INT_EQ(paired(1).validity, 0);
	g_players[0].lockstep_timestamp = TICK + XVT_NETWORK_STEP_TICKS;
	seed(0);
	xvt_flight_checkpoint_restore_player(0);
	XVT_ASSERT_INT_EQ(probe(0), 1);
	XVT_ASSERT_INT_EQ(paired(0).validity, XVT_PAIRED_OWNER_VALID);
}

int main(void)
{
	check_begin_and_masks();
	check_append_read();
	check_read_refusals();
	check_restore();
	check_save_player();
	check_restore_player();
	check_carried_object();
	check_network125_only();
	xvt_flight_integration_shutdown();
	xvt_reference_motion_shutdown();
	xvt_flight_timing_end_session();
	free(g_image);
	free(g_saved);
	free(g_again);
	return 0;
}
