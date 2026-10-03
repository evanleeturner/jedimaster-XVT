#include "xvt_runtime/snapshot/cockpit_readouts.h"

#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include <string.h>

static struct xvt_cockpit_number g_numbers[XVT_COCKPIT_NUMBER_COUNT];
static struct xvt_cockpit_target g_target;
static int g_target_updated;
static struct xvt_cockpit_proving_grounds g_course;

void xvt_cockpit_readouts_reset(void)
{
	memset(g_numbers, 0, sizeof g_numbers);
	memset(&g_target, 0, sizeof g_target);
	g_target.object.slot = UINT16_MAX;
	g_target_updated = 0;
	memset(&g_course, 0, sizeof g_course);
}

void xvt_cockpit_readouts_begin_update(void)
{
	g_target_updated = 0;
	g_course.visible = 0;
}

void xvt_cockpit_readouts_record_course(int x, int y, int width, int height)
{
	g_course.visible = 1;
	g_course.bounds = (struct xvt_snap_rect){x, y, width, height};
}

void xvt_cockpit_readouts_record_number(xvt_cockpit_number_id id,
					unsigned value, unsigned width,
					unsigned digits)
{
	if ((unsigned)id >= XVT_COCKPIT_NUMBER_COUNT) {
		return;
	}
	struct xvt_cockpit_number *number = &g_numbers[id];
	memset(number, 0, sizeof *number);
	number->value = id == XVT_COCKPIT_NUMBER_COURSE_SCORE ? (int32_t)value
							      : (uint16_t)value;
	number->bounds = (struct xvt_snap_rect){
		g_flight_clip_left, g_flight_clip_top,
		g_flight_clip_right - g_flight_clip_left,
		g_flight_clip_bottom - g_flight_clip_top};
	number->x = g_flight_cursor_x;
	number->y = g_flight_cursor_y;
	number->minimum_digits = (uint16_t)digits;
	number->field_width = (uint16_t)width;
	number->visible = 1;
	number->font_tier = g_flight_font_tier;
	number->foreground = g_flight_text_color_index;
	number->background = g_flight_text_bg_color;
	number->shadow_enabled = g_flight_text_shadow_enabled;
	number->shadow_color =
		g_flight_text_shadow_enabled ? g_flight_text_shadow_color : 0;
	number->phase = XVT_COCKPIT_BEFORE_CRT;
	number->word_wrap = g_flight_word_wrap_enabled != 0;
	number->clear_line = g_flight_clear_line_bg_enabled != 0;
	number->narrow =
		g_flight_draw_char_fn == flight_text_draw_narrow_glyph8bpp ||
		g_flight_draw_char_fn == flight_text_draw_narrow_glyph;
	number->keyed =
		g_flight_sw_framebuffer_base == g_flight_offscreen_buffer;
	if (number->keyed) {
		number->color_key_argb =
			xvt_render_draw_color(g_flight_transparent_color_index);
	}
	if (id != XVT_COCKPIT_NUMBER_COURSE_SCORE &&
	    (uint16_t)value == UINT16_MAX) {
		number->foreground = xvt_cockpit_text_resolve_color(
			'@', g_flight_transparent_color_index);
		number->shadow_enabled = number->shadow_color = 0;
	}
	number->clear_background = id == XVT_COCKPIT_NUMBER_COUNTERMEASURES ||
				   id == XVT_COCKPIT_NUMBER_ORDER_MINUTES;
	number->trailing_space = id >= XVT_COCKPIT_NUMBER_COURSE_REMAINING &&
				 id <= XVT_COCKPIT_NUMBER_COURSE_SCORE;
}

void xvt_cockpit_readouts_record_cached_number(unsigned binding, unsigned value,
					       unsigned digits)
{
	if (binding >= HUD_INSTRUMENT_COUNT) {
		return;
	}
	xvt_cockpit_number_id id;
	switch (binding % HUD_INSTRUMENTS_PER_SET) {
	case 40:
		id = XVT_COCKPIT_NUMBER_SPEED;
		break;
	case 41:
		id = XVT_COCKPIT_NUMBER_THROTTLE;
		break;
	case 82:
		id = XVT_COCKPIT_NUMBER_TARGET_SYSTEMS;
		break;
	case 83:
		id = XVT_COCKPIT_NUMBER_TARGET_RANGE;
		break;
	case 84:
		id = XVT_COCKPIT_NUMBER_TARGET_RANGE_FRACTION;
		break;
	case 85:
	case 102:
		id = XVT_COCKPIT_NUMBER_TARGET_SHIELDS;
		break;
	case 86:
	case 103:
		id = XVT_COCKPIT_NUMBER_TARGET_HULL;
		break;
	default:
		return;
	}
	xvt_cockpit_readouts_record_number(
		id, value, g_hud_element_layouts[binding].selector, digits);
	g_numbers[id].clear_background = 1;
}

void xvt_cockpit_readouts_begin_target(int cmd)
{
	const struct player_data *player = &g_players[g_local_player];
	struct xvt_snap_object_id object = {UINT16_MAX, 0};
	unsigned slot = (uint16_t)player->current_target_object_idx;
	if (g_object_table &&
	    slot < (unsigned)(g_region_main_object_slot_end +
			      g_region_static_object_slot_count)) {
		object = (struct xvt_snap_object_id){
			(uint16_t)slot, g_object_table[slot].object_signature};
	}
	if (object.slot != g_target.object.slot ||
	    object.signature != g_target.object.signature ||
	    cmd != g_target.cmd_mode) {
		memset(&g_target, 0, sizeof g_target);
		for (unsigned id = XVT_COCKPIT_NUMBER_TARGET_SYSTEMS;
		     id <= XVT_COCKPIT_NUMBER_ORDER_SECONDS; ++id) {
			memset(&g_numbers[id], 0, sizeof g_numbers[id]);
		}
		if (cmd) {
			/* Recapture the target shield and hull readouts even when the new target has the same
			 * percentages. */
			g_hud_element_state_cache[102] = -2;
			g_hud_element_state_cache[103] = -2;
		}
		xvt_cockpit_text_clear_target_fields();
	}
	g_target.object = object;
	g_target.type = object.slot == UINT16_MAX
				? 0
				: g_object_table[slot].object_type;
	g_target.component = (uint16_t)player->selected_target_component;
	g_target.box_visible = player->target_box_enabled;
	g_target.visible = 1;
	g_target.cmd_mode = cmd != 0;
	g_target.panel_cover = 0;
	g_target.labels_visible = cmd || object.slot != UINT16_MAX;
	g_target_updated = 1;
}

void xvt_cockpit_readouts_hide_target(void)
{
	g_target.visible = g_target.panel_cover;
	xvt_cockpit_text_clear_target_fields();
}

void xvt_cockpit_readouts_record_target_cover(unsigned binding)
{
	g_target.visible = 1;
	g_target.panel_cover = 1;
	g_target.cover_binding = (uint16_t)binding;
	g_target.labels_visible = 0;
}

void xvt_cockpit_readouts_record_armament(unsigned index, unsigned value)
{
	if (index < 4) {
		g_target.armament[index] = (struct xvt_cockpit_indicator){
			1, (uint8_t)value, 0, XVT_COCKPIT_BEFORE_CRT};
	}
}

void xvt_cockpit_readouts_clear_order_range(void)
{
	xvt_cockpit_text_clear_field(XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR);
	g_numbers[XVT_COCKPIT_NUMBER_ORDER_RANGE].visible = 0;
	g_numbers[XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION].visible = 0;
}

void xvt_cockpit_readouts_clear_order_time(void)
{
	xvt_cockpit_text_clear_field(XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR);
	g_numbers[XVT_COCKPIT_NUMBER_ORDER_MINUTES].visible = 0;
	g_numbers[XVT_COCKPIT_NUMBER_ORDER_SECONDS].visible = 0;
}

void xvt_cockpit_readouts_clear_launcher(unsigned launcher)
{
	if (launcher < 4) {
		g_numbers[XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + launcher]
			.visible = 0;
	}
}

void xvt_cockpit_readouts_copy_launcher(struct xvt_cockpit_number *number,
					unsigned launcher)
{
	if (launcher < 4) {
		*number =
			g_numbers[XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + launcher];
	}
}

static void copy_visible_number(struct xvt_cockpit_number *destination,
				xvt_cockpit_number_id id, int visible)
{
	*destination = g_numbers[id];
	destination->visible &= visible != 0;
}

void xvt_cockpit_readouts_copy_state(struct xvt_cockpit_state *state)
{
	state->proving_grounds = g_course;
	copy_visible_number(&state->proving_grounds.level,
			    XVT_COCKPIT_NUMBER_COURSE_LEVEL, g_course.visible);
	copy_visible_number(&state->proving_grounds.remaining,
			    XVT_COCKPIT_NUMBER_COURSE_REMAINING,
			    g_course.visible);
	copy_visible_number(&state->proving_grounds.passed,
			    XVT_COCKPIT_NUMBER_COURSE_PASSED, g_course.visible);
	copy_visible_number(&state->proving_grounds.targets,
			    XVT_COCKPIT_NUMBER_COURSE_TARGETS,
			    g_course.visible);
	copy_visible_number(&state->proving_grounds.score,
			    XVT_COCKPIT_NUMBER_COURSE_SCORE, g_course.visible);
	copy_visible_number(&state->readouts.speed, XVT_COCKPIT_NUMBER_SPEED,
			    state->readouts.speed.visible);
	copy_visible_number(&state->readouts.throttle,
			    XVT_COCKPIT_NUMBER_THROTTLE,
			    state->readouts.throttle.visible);
	copy_visible_number(&state->readouts.clock_minutes,
			    XVT_COCKPIT_NUMBER_CLOCK_MINUTES,
			    state->readouts.clock_minutes.visible);
	copy_visible_number(&state->readouts.clock_seconds,
			    XVT_COCKPIT_NUMBER_CLOCK_SECONDS,
			    state->readouts.clock_seconds.visible);
	copy_visible_number(&state->systems.countermeasure_count,
			    XVT_COCKPIT_NUMBER_COUNTERMEASURES,
			    state->systems.countermeasure_count.visible);
	for (unsigned slot = 0; slot < 4; ++slot) {
		copy_visible_number(
			&state->weapons.launchers[slot].count,
			(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
						slot),
			state->weapons.launchers[slot].count.visible);
	}
	for (unsigned slot = 0; slot < XVT_HUD_WEAPON_SLOTS; ++slot) {
		copy_visible_number(
			&state->weapons.slots[slot].charge_percent,
			(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LASER_FIRST +
						slot),
			state->weapons.slots[slot].charge_percent.visible);
	}
	state->target = g_target;
	state->target.visible &=
		g_target_updated && state->view.instruments_visible;
	int values_visible =
		state->target.visible && !state->target.panel_cover;
	copy_visible_number(&state->target.systems,
			    XVT_COCKPIT_NUMBER_TARGET_SYSTEMS,
			    values_visible && !g_target.cmd_mode);
	copy_visible_number(&state->target.shields,
			    XVT_COCKPIT_NUMBER_TARGET_SHIELDS, values_visible);
	copy_visible_number(&state->target.hull, XVT_COCKPIT_NUMBER_TARGET_HULL,
			    values_visible);
	copy_visible_number(&state->target.distance,
			    XVT_COCKPIT_NUMBER_TARGET_RANGE,
			    values_visible && !g_target.cmd_mode);
	copy_visible_number(&state->target.distance_fraction,
			    XVT_COCKPIT_NUMBER_TARGET_RANGE_FRACTION,
			    values_visible && !g_target.cmd_mode);
	copy_visible_number(&state->target.order_distance,
			    XVT_COCKPIT_NUMBER_ORDER_RANGE,
			    values_visible && g_target.cmd_mode);
	copy_visible_number(&state->target.order_distance_fraction,
			    XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION,
			    values_visible && g_target.cmd_mode);
	copy_visible_number(&state->target.order_minutes,
			    XVT_COCKPIT_NUMBER_ORDER_MINUTES,
			    values_visible && g_target.cmd_mode);
	copy_visible_number(&state->target.order_seconds,
			    XVT_COCKPIT_NUMBER_ORDER_SECONDS,
			    values_visible && g_target.cmd_mode);
}
