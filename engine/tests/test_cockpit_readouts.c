/* Checks the cockpit's numeric readouts and target panel (xvt_runtime/snapshot/cockpit_readouts.h) against
 * the promises in its header: what a recorded number holds, the 16-bit rule and its exceptions, which
 * numbers CopyState shows, the course panel, the target panel across a change of target or mode, its
 * cover, armament and order fields, and the launcher counts. The test builds its own world (an object
 * table with two main slots and one static slot, the local player in seat 0) and sets the flight text
 * globals and the palette itself; every case starts from Reset and that world.
 *
 * Not checked here: which HUD element bindings RecordCachedNumber maps to which readout, since the header
 * names the readouts but not their binding numbers; only an out-of-range binding is checked. */
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"

enum { BYPASS = 9, FOREGROUND = 7, BACKGROUND = 4, SHADOW = 5 };

enum { TARGET_A = 1, SIGNATURE_A = 0x0101, TARGET_B = 2, SIGNATURE_B = 0x0202 };

static struct object_record g_test_objects[3];
static struct xvt_cockpit_state g_state;
static uint8_t g_framebuffer[16];

/* Slots 0 and 1 are main slots and slot 2 is a static one; the local player in seat 0 targets nothing.
 * The text cursor is at (30, 40) inside the clip (10, 20) to (210, 120). */
static void cockpit_readouts_start(void)
{
	for (unsigned index = 0; index < 256; ++index) {
		g_sw_palette[index] = (struct rgb_triplet){
			(uint8_t)(index & 63), (uint8_t)(index >> 6), 7};
	}
	memset(g_test_objects, 0, sizeof g_test_objects);
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = 2;
	g_region_static_object_slot_count = 1;
	g_test_objects[TARGET_A].object_type = 3;
	g_test_objects[TARGET_A].object_signature = SIGNATURE_A;
	g_test_objects[TARGET_B].object_type = 4;
	g_test_objects[TARGET_B].object_signature = SIGNATURE_B;
	memset(g_players, 0, sizeof g_players);
	g_local_player = 0;
	g_players[0].current_target_object_idx = -1;
	memset(g_hud_element_layouts, 0, sizeof g_hud_element_layouts);

	g_flight_clip_left = 10;
	g_flight_clip_top = 20;
	g_flight_clip_right = 210;
	g_flight_clip_bottom = 120;
	g_flight_cursor_x = 30;
	g_flight_cursor_y = 40;
	g_flight_font_tier = 2;
	g_flight_text_color_index = FOREGROUND;
	g_flight_text_bg_color = BACKGROUND;
	g_flight_text_shadow_color = SHADOW;
	g_flight_text_shadow_enabled = 1;
	g_flight_transparent_color_index = BYPASS;
	g_flight_offscreen_buffer = NULL;
	g_flight_sw_framebuffer_base = g_framebuffer;

	xvt_cockpit_text_reset_fields();
	xvt_cockpit_readouts_reset();
}

/* A state whose instruments all show, as xvt_cockpit_instruments_build leaves it in the forward view. */
static struct xvt_cockpit_state *shown(void)
{
	memset(&g_state, 0, sizeof g_state);
	g_state.view.hud_state = HUD_VIEW_FORWARD;
	g_state.view.instruments_visible = 1;
	g_state.readouts.speed.visible = g_state.readouts.throttle.visible = 1;
	g_state.readouts.clock_minutes.visible =
		g_state.readouts.clock_seconds.visible = 1;
	g_state.systems.countermeasure_count.visible = 1;
	for (unsigned slot = 0; slot < 4; ++slot) {
		g_state.weapons.launchers[slot].count.visible = 1;
	}
	for (unsigned slot = 0; slot < XVT_HUD_WEAPON_SLOTS; ++slot) {
		g_state.weapons.slots[slot].charge_percent.visible = 1;
	}
	return &g_state;
}

static const struct xvt_cockpit_state *copied(void)
{
	xvt_cockpit_readouts_copy_state(shown());
	return &g_state;
}

static void cockpit_readouts_target(int slot)
{
	g_players[0].current_target_object_idx = (int16_t)slot;
}

static void record(xvt_cockpit_number_id id, unsigned value)
{
	xvt_cockpit_readouts_record_number(id, value, 3, 2);
}

static const struct xvt_cockpit_text_field *
text_field(xvt_cockpit_text_field_id id)
{
	static struct xvt_cockpit_state fields;
	memset(&fields, 0, sizeof fields);
	fields.view.hud_state = HUD_VIEW_FORWARD;
	xvt_cockpit_text_copy_fields(&fields);
	return &fields.text_fields[id];
}

static void check_record_number(void)
{
	cockpit_readouts_start();
	xvt_cockpit_readouts_record_number(XVT_COCKPIT_NUMBER_SPEED, 0x12345, 3,
					   2);
	const struct xvt_cockpit_number *speed = &copied()->readouts.speed;
	XVT_ASSERT_INT_EQ(speed->visible, 1);
	/* Values keep 16 bits. */
	XVT_ASSERT_INT_EQ(speed->value, 0x2345);
	XVT_ASSERT_INT_EQ(speed->field_width, 3);
	XVT_ASSERT_INT_EQ(speed->minimum_digits, 2);
	XVT_ASSERT_INT_EQ(speed->x, 30);
	XVT_ASSERT_INT_EQ(speed->y, 40);
	XVT_ASSERT_INT_EQ(speed->bounds.x, 10);
	XVT_ASSERT_INT_EQ(speed->bounds.y, 20);
	XVT_ASSERT_INT_EQ(speed->bounds.width, 200);
	XVT_ASSERT_INT_EQ(speed->bounds.height, 100);
	XVT_ASSERT_INT_EQ(speed->font_tier, 2);
	XVT_ASSERT_INT_EQ(speed->foreground, FOREGROUND);
	XVT_ASSERT_INT_EQ(speed->background, BACKGROUND);
	XVT_ASSERT_INT_EQ(speed->shadow_enabled, 1);

	/* The course score keeps all its bits. */
	xvt_cockpit_readouts_record_course(1, 2, 3, 4);
	record(XVT_COCKPIT_NUMBER_COURSE_SCORE, 0x12345);
	XVT_ASSERT_INT_EQ(copied()->proving_grounds.score.value, 0x12345);
}

static void check_number_all_ones(void)
{
	cockpit_readouts_start();
	/* A 16-bit 0xFFFF is drawn in the '@' color with no shadow. */
	record(XVT_COCKPIT_NUMBER_SPEED, 0xFFFF);
	const struct xvt_cockpit_number *speed = &copied()->readouts.speed;
	XVT_ASSERT_INT_EQ(speed->foreground,
			  xvt_cockpit_text_resolve_color('@', BYPASS));
	XVT_ASSERT_INT_EQ(speed->shadow_enabled, 0);
	/* Any other value keeps the live foreground. */
	record(XVT_COCKPIT_NUMBER_SPEED, 0xFFFE);
	XVT_ASSERT_INT_EQ(copied()->readouts.speed.foreground, FOREGROUND);
}

static void check_record_number_out_of_range(void)
{
	cockpit_readouts_start();
	xvt_cockpit_readouts_record_number(XVT_COCKPIT_NUMBER_COUNT, 5, 3, 2);
	xvt_cockpit_readouts_record_number((xvt_cockpit_number_id)-1, 5, 3, 2);
	/* A binding past the element table is not a readout. */
	xvt_cockpit_readouts_record_cached_number(HUD_INSTRUMENT_COUNT + 40, 5,
						  2);
	const struct xvt_cockpit_state *state = copied();
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 0);
	XVT_ASSERT_INT_EQ(state->readouts.throttle.visible, 0);
	XVT_ASSERT_INT_EQ(state->proving_grounds.score.visible, 0);
	XVT_ASSERT_INT_EQ(state->weapons.slots[XVT_HUD_WEAPON_SLOTS - 1]
				  .charge_percent.visible,
			  0);
}

static void check_copy_state_needs_record_and_instrument(void)
{
	cockpit_readouts_start();
	record(XVT_COCKPIT_NUMBER_SPEED, 11);
	record(XVT_COCKPIT_NUMBER_COUNTERMEASURES, 12);
	record((xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 2),
	       13);
	record((xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LASER_FIRST + 5), 14);

	/* Recorded and shown: visible with the recorded value. */
	const struct xvt_cockpit_state *state = copied();
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 1);
	XVT_ASSERT_INT_EQ(state->readouts.speed.value, 11);
	XVT_ASSERT_INT_EQ(state->systems.countermeasure_count.visible, 1);
	XVT_ASSERT_INT_EQ(state->systems.countermeasure_count.value, 12);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[2].count.visible, 1);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[2].count.value, 13);
	XVT_ASSERT_INT_EQ(state->weapons.slots[5].charge_percent.visible, 1);
	XVT_ASSERT_INT_EQ(state->weapons.slots[5].charge_percent.value, 14);

	/* Shown but never recorded: hidden. */
	XVT_ASSERT_INT_EQ(state->readouts.throttle.visible, 0);
	XVT_ASSERT_INT_EQ(state->readouts.clock_minutes.visible, 0);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[1].count.visible, 0);
	XVT_ASSERT_INT_EQ(state->weapons.slots[4].charge_percent.visible, 0);

	/* Recorded but the state does not show the instrument: hidden. */
	struct xvt_cockpit_state *hidden = shown();
	hidden->readouts.speed.visible = 0;
	hidden->systems.countermeasure_count.visible = 0;
	hidden->weapons.launchers[2].count.visible = 0;
	hidden->weapons.slots[5].charge_percent.visible = 0;
	xvt_cockpit_readouts_copy_state(hidden);
	XVT_ASSERT_INT_EQ(hidden->readouts.speed.visible, 0);
	XVT_ASSERT_INT_EQ(hidden->systems.countermeasure_count.visible, 0);
	XVT_ASSERT_INT_EQ(hidden->weapons.launchers[2].count.visible, 0);
	XVT_ASSERT_INT_EQ(hidden->weapons.slots[5].charge_percent.visible, 0);
}

static void check_course(void)
{
	cockpit_readouts_start();
	record(XVT_COCKPIT_NUMBER_COURSE_LEVEL, 3);
	/* Course numbers follow the course: hidden until it is recorded. */
	XVT_ASSERT_INT_EQ(copied()->proving_grounds.level.visible, 0);

	xvt_cockpit_readouts_record_course(5, 6, 70, 80);
	const struct xvt_cockpit_state *state = copied();
	XVT_ASSERT_INT_EQ(state->proving_grounds.visible, 1);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.x, 5);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.y, 6);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.width, 70);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.height, 80);
	XVT_ASSERT_INT_EQ(state->proving_grounds.level.visible, 1);
	XVT_ASSERT_INT_EQ(state->proving_grounds.level.value, 3);

	/* A new update hides the course until it is recorded again. */
	xvt_cockpit_readouts_begin_update();
	state = copied();
	XVT_ASSERT_INT_EQ(state->proving_grounds.visible, 0);
	XVT_ASSERT_INT_EQ(state->proving_grounds.level.visible, 0);
}

static void check_target_shows(void)
{
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(0);
	const struct xvt_cockpit_state *state = copied();
	XVT_ASSERT_INT_EQ(state->target.visible, 1);
	XVT_ASSERT_INT_EQ(state->target.object.slot, TARGET_A);
	XVT_ASSERT_INT_EQ(state->target.object.signature, SIGNATURE_A);
	XVT_ASSERT_INT_EQ(state->target.cmd_mode, 0);

	/* The static slot is a target too. */
	cockpit_readouts_target(TARGET_B);
	xvt_cockpit_readouts_begin_target(0);
	XVT_ASSERT_INT_EQ(copied()->target.object.slot, TARGET_B);

	/* Only while instruments are visible... */
	struct xvt_cockpit_state *hidden = shown();
	hidden->view.instruments_visible = 0;
	xvt_cockpit_readouts_copy_state(hidden);
	XVT_ASSERT_INT_EQ(hidden->target.visible, 0);

	/* ...and only when updated since BeginUpdate. */
	xvt_cockpit_readouts_begin_update();
	XVT_ASSERT_INT_EQ(copied()->target.visible, 0);
	xvt_cockpit_readouts_begin_target(0);
	XVT_ASSERT_INT_EQ(copied()->target.visible, 1);
}

static void check_target_values_by_mode(void)
{
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(0);
	record(XVT_COCKPIT_NUMBER_TARGET_SYSTEMS, 1);
	record(XVT_COCKPIT_NUMBER_TARGET_SHIELDS, 2);
	record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	record(XVT_COCKPIT_NUMBER_TARGET_RANGE, 4);
	record(XVT_COCKPIT_NUMBER_TARGET_RANGE_FRACTION, 5);
	record(XVT_COCKPIT_NUMBER_ORDER_RANGE, 6);
	record(XVT_COCKPIT_NUMBER_ORDER_MINUTES, 7);

	/* Outside command mode: range and systems show, order range and time do not. */
	const struct xvt_cockpit_target *target = &copied()->target;
	XVT_ASSERT_INT_EQ(target->systems.visible, 1);
	XVT_ASSERT_INT_EQ(target->shields.visible, 1);
	XVT_ASSERT_INT_EQ(target->hull.visible, 1);
	XVT_ASSERT_INT_EQ(target->hull.value, 3);
	XVT_ASSERT_INT_EQ(target->distance.visible, 1);
	XVT_ASSERT_INT_EQ(target->distance_fraction.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_distance.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 0);

	/* In command mode the other way round; shields and hull show in both. */
	xvt_cockpit_readouts_begin_target(1);
	record(XVT_COCKPIT_NUMBER_TARGET_SYSTEMS, 1);
	record(XVT_COCKPIT_NUMBER_TARGET_SHIELDS, 2);
	record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	record(XVT_COCKPIT_NUMBER_TARGET_RANGE, 4);
	record(XVT_COCKPIT_NUMBER_ORDER_RANGE, 6);
	record(XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION, 8);
	record(XVT_COCKPIT_NUMBER_ORDER_MINUTES, 7);
	record(XVT_COCKPIT_NUMBER_ORDER_SECONDS, 9);
	target = &copied()->target;
	XVT_ASSERT_INT_EQ(target->cmd_mode, 1);
	XVT_ASSERT_INT_EQ(target->systems.visible, 0);
	XVT_ASSERT_INT_EQ(target->distance.visible, 0);
	XVT_ASSERT_INT_EQ(target->shields.visible, 1);
	XVT_ASSERT_INT_EQ(target->hull.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_distance.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_distance.value, 6);
	XVT_ASSERT_INT_EQ(target->order_distance_fraction.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_seconds.visible, 1);
}

static void check_target_cover(void)
{
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(0);
	record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	xvt_cockpit_readouts_record_target_cover(77);
	const struct xvt_cockpit_target *target = &copied()->target;
	XVT_ASSERT_INT_EQ(target->visible, 1);
	XVT_ASSERT_INT_EQ(target->panel_cover, 1);
	XVT_ASSERT_INT_EQ(target->cover_binding, 77);
	XVT_ASSERT_INT_EQ(target->labels_visible, 0);
	/* The values need the panel uncovered. */
	XVT_ASSERT_INT_EQ(target->hull.visible, 0);

	/* Hiding keeps a covered panel shown... */
	xvt_cockpit_readouts_hide_target();
	XVT_ASSERT_INT_EQ(copied()->target.visible, 1);
	/* ...and hides an uncovered one. */
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(0);
	xvt_cockpit_readouts_hide_target();
	XVT_ASSERT_INT_EQ(copied()->target.visible, 0);
}

static void check_hide_target_clears_fields(void)
{
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(0);
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_TARGET_NAME, "TIE",
				      XVT_COCKPIT_ALIGN_LEFT);
	uint64_t generation =
		text_field(XVT_COCKPIT_TEXT_TARGET_NAME)->generation;
	xvt_cockpit_readouts_hide_target();
	XVT_ASSERT_INT_EQ(
		text_field(XVT_COCKPIT_TEXT_TARGET_NAME)->caption.text[0], 0);
	XVT_ASSERT_TRUE(text_field(XVT_COCKPIT_TEXT_TARGET_NAME)->generation >
			generation);
}

static void check_target_change_clears(void)
{
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(0);
	record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	record(XVT_COCKPIT_NUMBER_ORDER_SECONDS, 4);
	record(XVT_COCKPIT_NUMBER_SPEED, 5);
	xvt_cockpit_readouts_record_armament(1, 2);
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_TARGET_NAME, "TIE",
				      XVT_COCKPIT_ALIGN_LEFT);
	uint64_t name = text_field(XVT_COCKPIT_TEXT_TARGET_NAME)->generation;

	/* The same target in the same mode keeps everything. */
	xvt_cockpit_readouts_begin_target(0);
	const struct xvt_cockpit_state *state = copied();
	XVT_ASSERT_INT_EQ(state->target.hull.visible, 1);
	XVT_ASSERT_INT_EQ(state->target.armament[1].state, 2);
	XVT_ASSERT_INT_EQ(
		strcmp(text_field(XVT_COCKPIT_TEXT_TARGET_NAME)->caption.text,
		       "TIE"),
		0);

	/* A new target clears the target, its numbers and its text fields, but not the other numbers. */
	cockpit_readouts_target(TARGET_B);
	xvt_cockpit_readouts_begin_target(1);
	record(XVT_COCKPIT_NUMBER_TARGET_SHIELDS, 6);
	state = copied();
	XVT_ASSERT_INT_EQ(state->target.hull.visible, 0);
	XVT_ASSERT_INT_EQ(state->target.order_seconds.visible, 0);
	XVT_ASSERT_INT_EQ(state->target.shields.visible, 1);
	XVT_ASSERT_INT_EQ(state->target.armament[1].state, 0);
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 1);
	XVT_ASSERT_INT_EQ(
		text_field(XVT_COCKPIT_TEXT_TARGET_NAME)->caption.text[0], 0);
	XVT_ASSERT_TRUE(text_field(XVT_COCKPIT_TEXT_TARGET_NAME)->generation >
			name);

	/* A change of mode alone clears them too. */
	xvt_cockpit_readouts_begin_target(0);
	XVT_ASSERT_INT_EQ(copied()->target.shields.visible, 0);
}

static void check_command_change_resets_threat_cache(void)
{
	cockpit_readouts_start();
	g_hud_element_state_cache[102] = g_hud_element_state_cache[103] = 55;
	cockpit_readouts_target(TARGET_A);
	/* A change outside command mode leaves the cache alone. */
	xvt_cockpit_readouts_begin_target(0);
	XVT_ASSERT_INT_EQ(g_hud_element_state_cache[102], 55);
	XVT_ASSERT_INT_EQ(g_hud_element_state_cache[103], 55);
	/* A change into command mode resets both entries. */
	xvt_cockpit_readouts_begin_target(1);
	XVT_ASSERT_TRUE(g_hud_element_state_cache[102] != 55);
	XVT_ASSERT_TRUE(g_hud_element_state_cache[103] != 55);
	/* No change, no reset. */
	g_hud_element_state_cache[102] = g_hud_element_state_cache[103] = 55;
	xvt_cockpit_readouts_begin_target(1);
	XVT_ASSERT_INT_EQ(g_hud_element_state_cache[102], 55);
	XVT_ASSERT_INT_EQ(g_hud_element_state_cache[103], 55);
}

static void check_reset_targets_no_object(void)
{
	cockpit_readouts_start();
	/* Reset clears the target, replacing what the state held. */
	struct xvt_cockpit_state *state = shown();
	state->target.cover_binding = 99;
	state->target.type = 99;
	xvt_cockpit_readouts_copy_state(state);
	XVT_ASSERT_INT_EQ(state->target.cover_binding, 0);
	XVT_ASSERT_INT_EQ(state->target.type, 0);
	struct xvt_snap_object_id none = state->target.object;

	/* A player with no target starts the panel on the same no-object target Reset left: no change, so a
	 * target number recorded before it survives. */
	record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	xvt_cockpit_readouts_begin_target(0);
	state = shown();
	xvt_cockpit_readouts_copy_state(state);
	XVT_ASSERT_INT_EQ(state->target.object.slot, none.slot);
	XVT_ASSERT_INT_EQ(state->target.object.signature, none.signature);
	XVT_ASSERT_INT_EQ(state->target.hull.visible, 1);
}

static void check_armament(void)
{
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(1);
	xvt_cockpit_readouts_record_armament(3, 2);
	xvt_cockpit_readouts_record_armament(4, 9);
	const struct xvt_cockpit_target *target = &copied()->target;
	XVT_ASSERT_INT_EQ(target->armament[3].state, 2);
	XVT_ASSERT_INT_EQ(target->armament[0].state, 0);
}

static void check_clear_order(void)
{
	cockpit_readouts_start();
	cockpit_readouts_target(TARGET_A);
	xvt_cockpit_readouts_begin_target(1);
	record(XVT_COCKPIT_NUMBER_ORDER_RANGE, 1);
	record(XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION, 2);
	record(XVT_COCKPIT_NUMBER_ORDER_MINUTES, 3);
	record(XVT_COCKPIT_NUMBER_ORDER_SECONDS, 4);
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR,
				      ".", XVT_COCKPIT_ALIGN_LEFT);
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
				      ":", XVT_COCKPIT_ALIGN_LEFT);

	xvt_cockpit_readouts_clear_order_range();
	const struct xvt_cockpit_target *target = &copied()->target;
	XVT_ASSERT_INT_EQ(target->order_distance.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_distance_fraction.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_seconds.visible, 1);
	XVT_ASSERT_INT_EQ(text_field(XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR)
				  ->caption.text[0],
			  0);
	XVT_ASSERT_INT_EQ(
		strcmp(text_field(XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR)
			       ->caption.text,
		       ":"),
		0);

	xvt_cockpit_readouts_clear_order_time();
	target = &copied()->target;
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_seconds.visible, 0);
	XVT_ASSERT_INT_EQ(text_field(XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR)
				  ->caption.text[0],
			  0);
}

static void check_launchers(void)
{
	cockpit_readouts_start();
	record((xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 1),
	       7);
	record((xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 3),
	       8);
	struct xvt_cockpit_number number;
	xvt_cockpit_readouts_copy_launcher(&number, 1);
	XVT_ASSERT_INT_EQ(number.visible, 1);
	XVT_ASSERT_INT_EQ(number.value, 7);

	xvt_cockpit_readouts_clear_launcher(1);
	xvt_cockpit_readouts_copy_launcher(&number, 1);
	XVT_ASSERT_INT_EQ(number.visible, 0);

	/* Launchers from 4 up are ignored: the copy leaves number alone and the clear touches nothing. */
	memset(&number, 0x5A, sizeof number);
	struct xvt_cockpit_number before;
	memcpy(&before, &number, sizeof before);
	xvt_cockpit_readouts_copy_launcher(&number, 4);
	XVT_ASSERT_INT_EQ(memcmp(&number, &before, sizeof number), 0);
	xvt_cockpit_readouts_clear_launcher(4);
	xvt_cockpit_readouts_copy_launcher(&number, 3);
	XVT_ASSERT_INT_EQ(number.visible, 1);
	XVT_ASSERT_INT_EQ(number.value, 8);
}

static void check_reset_clears_numbers(void)
{
	cockpit_readouts_start();
	record(XVT_COCKPIT_NUMBER_SPEED, 1);
	record((xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 1),
	       2);
	xvt_cockpit_readouts_record_course(1, 2, 3, 4);
	xvt_cockpit_readouts_reset();
	const struct xvt_cockpit_state *state = copied();
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 0);
	XVT_ASSERT_INT_EQ(state->readouts.speed.value, 0);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[1].count.visible, 0);
	XVT_ASSERT_INT_EQ(state->proving_grounds.visible, 0);
}

int main(void)
{
	check_record_number();
	check_number_all_ones();
	check_record_number_out_of_range();
	check_copy_state_needs_record_and_instrument();
	check_course();
	check_target_shows();
	check_target_values_by_mode();
	check_target_cover();
	check_hide_target_clears_fields();
	check_target_change_clears();
	check_command_change_resets_threat_cache();
	check_reset_targets_no_object();
	check_armament();
	check_clear_order();
	check_launchers();
	check_reset_clears_numbers();
	return 0;
}
