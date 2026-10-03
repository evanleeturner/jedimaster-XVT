#include "xvt_runtime/snapshot/cockpit_text.h"

#include "aeron/aeron.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include <string.h>

static struct xvt_cockpit_text_field g_fields[XVT_COCKPIT_TEXT_FIELD_COUNT];

static int field_clears_background(xvt_cockpit_text_field_id field)
{
	switch (field) {
	case XVT_COCKPIT_TEXT_TARGET_NAME:
	case XVT_COCKPIT_TEXT_TARGET_CARGO:
	case XVT_COCKPIT_TEXT_TARGET_DETAIL:
	case XVT_COCKPIT_TEXT_CMD_RANGE:
	case XVT_COCKPIT_TEXT_CMD_ORDERS_LABEL:
	case XVT_COCKPIT_TEXT_CMD_TARGET_LABEL:
	case XVT_COCKPIT_TEXT_CMD_TARGET:
	case XVT_COCKPIT_TEXT_CMD_RANGE_LABEL:
	case XVT_COCKPIT_TEXT_CMD_TIME_LABEL:
	case XVT_COCKPIT_TEXT_CRAFT_STATUS:
	case XVT_COCKPIT_TEXT_MAP_FOLLOWING:
	case XVT_COCKPIT_TEXT_MAP_TRACKING:
	case XVT_COCKPIT_TEXT_RESOURCE_NAME:
	case XVT_COCKPIT_TEXT_SHIELD_FORE:
	case XVT_COCKPIT_TEXT_SHIELD_AFT:
	case XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST:
		return 1;
	case XVT_COCKPIT_TEXT_CRITICAL_WARNING:
		return (uint16_t)g_hud_element_layouts[127]
			       .clip_height_or_foreground_color > 512;
	default:
		return 0;
	}
}

void xvt_cockpit_text_reset_fields(void)
{
	memset(g_fields, 0, sizeof g_fields);
}

void xvt_cockpit_text_clear_field(xvt_cockpit_text_field_id field)
{
	if ((unsigned)field >= XVT_COCKPIT_TEXT_FIELD_COUNT) {
		return;
	}
	struct xvt_cockpit_text_field *current = &g_fields[field];
	if (!current->caption.visible) {
		return;
	}
	uint64_t generation = current->generation + 1;
	memset(current, 0, sizeof *current);
	current->generation = generation;
}

void xvt_cockpit_text_clear_target_fields(void)
{
	for (unsigned field = XVT_COCKPIT_TEXT_TARGET_NAME;
	     field <= XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN; ++field) {
		xvt_cockpit_text_clear_field((xvt_cockpit_text_field_id)field);
	}
}

void xvt_cockpit_text_record_field(xvt_cockpit_text_field_id field,
				   const char *text,
				   xvt_cockpit_alignment alignment)
{
	if ((unsigned)field >= XVT_COCKPIT_TEXT_FIELD_COUNT || !text) {
		return;
	}
	struct xvt_cockpit_text_field next;
	memset(&next, 0, sizeof next);
	size_t length = strlen(text);
	if (length >= sizeof next.caption.text) {
		XVT_LOG_ERROR("snapshot.text_overflow field=%u limit=%zu",
			      (unsigned)field, sizeof next.caption.text);
		return;
	}
	memcpy(next.caption.text, text, length + 1);
	/* Match TIE's named-string capture: resolve inline logical colors once. */
	for (size_t index = 0; index + 1 < length; ++index) {
		if ((uint8_t)next.caption.text[index] == 0xfe) {
			++index;
			next.caption.text[index] =
				(char)xvt_cockpit_text_resolve_color(
					(uint8_t)next.caption.text[index],
					g_flight_transparent_color_index);
		}
	}
	next.caption.visible = length != 0;
	next.caption.font_tier = g_flight_font_tier;
	next.caption.foreground = g_flight_text_color_index;
	if (length > 1 && (uint8_t)next.caption.text[0] == 0xfe) {
		next.caption.foreground = (uint8_t)next.caption.text[1];
	} else if (length && (uint8_t)next.caption.text[0] < 0x10) {
		next.caption.foreground = (uint8_t)next.caption.text[0];
	}
	next.caption.background = g_flight_text_bg_color;
	next.caption.alignment = (uint8_t)alignment;
	next.caption.phase = XVT_COCKPIT_BEFORE_CRT;
	next.bounds = (struct xvt_snap_rect){
		g_flight_clip_left, g_flight_clip_top,
		g_flight_clip_right - g_flight_clip_left,
		g_flight_clip_bottom - g_flight_clip_top};
	next.x = alignment == XVT_COCKPIT_ALIGN_LEFT ? g_flight_cursor_x : 0;
	next.y = g_flight_cursor_y;
	next.shadow_enabled = g_flight_text_shadow_enabled;
	next.shadow_color =
		g_flight_text_shadow_enabled ? g_flight_text_shadow_color : 0;
	next.lowercase = g_flight_font_tier == 0 || g_flight_font_has_lowercase;
	next.word_wrap = g_flight_word_wrap_enabled != 0;
	next.clear_line = g_flight_clear_line_bg_enabled != 0;
	next.keyed = g_flight_sw_framebuffer_base == g_flight_offscreen_buffer;
	if (next.keyed) {
		next.color_key_argb =
			xvt_render_draw_color(g_flight_transparent_color_index);
	}
	next.narrow =
		g_flight_draw_char_fn == flight_text_draw_narrow_glyph8bpp ||
		g_flight_draw_char_fn == flight_text_draw_narrow_glyph;
	next.clear_background = field_clears_background(field);
	next.generation = g_fields[field].generation;
	if (memcmp(&next, &g_fields[field], sizeof next)) {
		++next.generation;
	}
	g_fields[field] = next;
}

void xvt_cockpit_text_copy_fields(struct xvt_cockpit_state *state)
{
	memcpy(state->text_fields, g_fields, sizeof g_fields);
	int cockpit = state->view.hud_state == HUD_VIEW_FORWARD ||
		      state->view.hud_state == HUD_VIEW_HUD_ONLY;
	for (unsigned id = 0; id < XVT_COCKPIT_TEXT_FIELD_COUNT; ++id) {
		struct xvt_cockpit_text_field *field = &state->text_fields[id];
		int visible = 1;
		if (id == XVT_COCKPIT_TEXT_CLOCK_SEPARATOR) {
			visible = state->readouts.clock_minutes.visible &&
				  state->readouts.clock_seconds.visible;
		} else if (id == XVT_COCKPIT_TEXT_THROTTLE_PERCENT) {
			visible = state->readouts.throttle.visible;
		} else if (id == XVT_COCKPIT_TEXT_TARGET_RANGE_SEPARATOR) {
			visible = state->target.distance.visible &&
				  state->target.distance_fraction.visible;
		} else if (id == XVT_COCKPIT_TEXT_TARGET_SHIELD_PERCENT) {
			visible = state->target.shields.visible &&
				  !state->target.cmd_mode;
		} else if (id == XVT_COCKPIT_TEXT_TARGET_HULL_PERCENT) {
			visible = state->target.hull.visible &&
				  !state->target.cmd_mode;
		} else if (id == XVT_COCKPIT_TEXT_TARGET_SYSTEM_PERCENT) {
			visible = state->target.systems.visible;
		} else if (id == XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR) {
			visible = state->target.order_distance.visible &&
				  state->target.order_distance_fraction.visible;
		} else if (id == XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR) {
			visible = state->target.order_minutes.visible &&
				  state->target.order_seconds.visible;
		} else if (id <= XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN) {
			visible = state->target.visible &&
				  !state->target.panel_cover;
		} else if (id >= XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST &&
			   id <= XVT_COCKPIT_TEXT_COURSE_LABEL_LAST) {
			visible = state->proving_grounds.visible;
		} else if (id >= XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST &&
			   id <= XVT_COCKPIT_TEXT_ARMAMENT_LABEL_LAST) {
			visible =
				state->target.visible &&
				state->target.cmd_mode &&
				state->target
					.armament
						[id -
						 XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST]
					.state;
		} else if (id >= XVT_COCKPIT_TEXT_CMD_HEADER_FIRST &&
			   id <= XVT_COCKPIT_TEXT_CMD_HEADER_LAST) {
			visible =
				state->target.visible && state->target.cmd_mode;
		} else if (id >= XVT_COCKPIT_TEXT_TARGET_SYSTEM_LABEL &&
			   id <= XVT_COCKPIT_TEXT_TARGET_HULL_LABEL) {
			visible = state->target.visible &&
				  state->target.labels_visible &&
				  !state->target.cmd_mode;
		} else if (id == XVT_COCKPIT_TEXT_MAP_FOLLOWING ||
			   id == XVT_COCKPIT_TEXT_MAP_TRACKING) {
			visible = state->view.map_active;
		} else if (id == XVT_COCKPIT_TEXT_SHIELD_FORE ||
			   id == XVT_COCKPIT_TEXT_SHIELD_AFT) {
			unsigned side = id == XVT_COCKPIT_TEXT_SHIELD_AFT;
			visible = state->systems.shields[side].visible &&
				  state->systems.shields[side].text_mode;
		} else if (id == XVT_COCKPIT_TEXT_CRITICAL_WARNING) {
			visible = state->systems.critical_warning.visible;
		} else if (id != XVT_COCKPIT_TEXT_RESOURCE_NAME) {
			visible = cockpit;
		}
		field->caption.visible &= visible != 0;
	}
}

void xvt_cockpit_text_copy_placed_field(struct xvt_cockpit_text_field *field,
					xvt_cockpit_text_field_id id,
					int offset_x, int offset_y)
{
	*field = g_fields[id];
	field->bounds.x += offset_x;
	field->bounds.y += offset_y;
	if (field->caption.alignment == XVT_COCKPIT_ALIGN_LEFT) {
		field->x += (int16_t)offset_x;
	}
	field->y += (int16_t)offset_y;
	field->caption.phase = XVT_COCKPIT_AFTER_CRT;
	field->keyed = 1;
	field->color_key_argb =
		xvt_render_draw_color(g_flight_transparent_color_index);
}

int xvt_cockpit_text_capture_glyph(struct xvt_cockpit_glyph *glyph,
				   unsigned character, unsigned advance,
				   unsigned height, int narrow, int origin_x,
				   int origin_y, const uint32_t palette[256],
				   int keyed)
{
	int x = g_flight_cursor_x, y = g_flight_cursor_y;
	if (x >= g_flight_clip_right || y >= g_flight_clip_bottom ||
	    x + (int)advance + 1 <= g_flight_clip_left ||
	    y + (int)height + 1 <= g_flight_clip_top) {
		return 0;
	}
	memset(glyph, 0, sizeof *glyph);
	glyph->font_asset_id =
		xvt_render_assets_image_id(g_flight_font_glyph_table_sw);
	glyph->clip = (struct xvt_snap_rect){
		g_flight_clip_left - origin_x, g_flight_clip_top - origin_y,
		g_flight_clip_right - g_flight_clip_left,
		g_flight_clip_bottom - g_flight_clip_top};
	glyph->x = (int16_t)(x - origin_x);
	glyph->y = (int16_t)(y - origin_y);
	glyph->character = (uint16_t)character;
	glyph->advance = (uint16_t)advance;
	glyph->height = (uint16_t)height;
	glyph->foreground_argb = palette[g_flight_text_color_index];
	glyph->background_argb = palette[g_flight_text_bg_color];
	glyph->shadow_argb = palette[g_flight_text_shadow_color];
	if (keyed) {
		uint32_t key = palette[g_flight_transparent_color_index];
		if (glyph->foreground_argb == key) {
			glyph->foreground_argb = 0;
		}
		if (glyph->background_argb == key) {
			glyph->background_argb = 0;
		}
		if (glyph->shadow_argb == key) {
			glyph->shadow_argb = 0;
		}
	}
	glyph->narrow = narrow != 0;
	glyph->shadow_enabled = g_flight_text_shadow_enabled;
	return 1;
}

uint8_t xvt_cockpit_text_resolve_color(uint8_t code, uint8_t bypass)
{
	if (code >= 0x40 && code < 0x40 + sizeof g_flight_char_to_color_lut &&
	    code != bypass) {
		return g_flight_char_to_color_lut[code - 0x40];
	}
	return code;
}
