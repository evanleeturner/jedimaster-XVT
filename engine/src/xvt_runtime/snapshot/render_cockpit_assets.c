#include "xvt_runtime/snapshot/render_cockpit_assets.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/render/flight_sw.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_assets.h"

static struct xvt_snap_cockpit_layout g_layout;
static uint64_t g_layout_generation;
static uint64_t g_resource_generation;

struct cockpit_source_binding {
	uint64_t id;
	uint32_t frame;
};

static struct cockpit_source_binding g_panels[265];
static struct cockpit_source_binding g_icons[XVT_SNAP_MAP_ICON_FRAMES];
static struct cockpit_source_binding g_lfd[28];

void xvt_render_cockpit_reset(void)
{
	++g_resource_generation;
	memset(&g_layout, 0, sizeof g_layout);
	memset(g_panels, 0, sizeof g_panels);
	memset(g_icons, 0, sizeof g_icons);
	memset(g_lfd, 0, sizeof g_lfd);
	g_layout.generation = ++g_layout_generation;
}

void xvt_render_cockpit_forget(uint64_t id)
{
	int owned = 0;
	for (unsigned panel = 0; panel < 265; ++panel) {
		owned |= g_panels[panel].id == id;
	}
	for (unsigned view = 0; view < 28; ++view) {
		owned |= g_lfd[view].id == id;
	}
	if (owned) {
		++g_resource_generation;
	}
	for (unsigned i = 0; i < 265; ++i) {
		if (g_panels[i].id == id) {
			memset(&g_panels[i], 0, sizeof g_panels[i]);
		}
	}
	for (unsigned i = 0; i < XVT_SNAP_MAP_ICON_FRAMES; ++i) {
		if (g_icons[i].id == id) {
			memset(&g_icons[i], 0, sizeof g_icons[i]);
		}
	}
	for (unsigned i = 0; i < 28; ++i) {
		if (g_lfd[i].id == id) {
			memset(&g_lfd[i], 0, sizeof g_lfd[i]);
			g_layout.descriptors[i].lfd_asset_id = 0;
			g_layout.generation = ++g_layout_generation;
		}
	}
	if (g_layout.panel_asset_id == id) {
		g_layout.panel_asset_id = 0;
	}
}

static void refresh_layout_elements(void)
{
	/* Layout selectors may be assigned during a panel-set rebuild. */
	if (g_layout.valid) {
		for (unsigned i = 0; i < XVT_SNAP_INSTRUMENTS; ++i) {
			const struct hud_element_layout *e =
				&g_hud_element_layouts[i];
			struct xvt_snap_hud_element value = {
				e->x,
				e->y,
				e->selector,
				e->color_index_or_widget_param,
				e->clip_width,
				e->clip_height_or_foreground_color};
			if (memcmp(&g_layout.elements[i], &value,
				   sizeof value) != 0) {
				g_layout.elements[i] = value;
				g_layout.generation = ++g_layout_generation;
			}
		}
	}
}

void xvt_render_cockpit_capture_definition(
	struct xvt_cockpit_definition *definition)
{
	refresh_layout_elements();
	memset(definition, 0, sizeof *definition);
	definition->resource_generation = g_resource_generation;
	definition->layout = g_layout;
	/* The root definition generation compares actual layout content. Entry 127
	 * carries the original warning timer, which belongs to the captured readout. */
	definition->layout.generation = 0;
	definition->layout.elements[127].clip_height_or_foreground = 0;
	for (unsigned index = 0; index < XVT_HUD_PANEL_BINDINGS; ++index) {
		definition->panels[index].asset_id = g_panels[index].id;
		definition->panels[index].frame = g_panels[index].frame;
	}
	for (unsigned tier = 0; tier < XVT_HUD_FONT_TIERS; ++tier) {
		const void *font = g_flight_font_micro_sw;
		unsigned height = 5;
		unsigned half_height = 3;
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
			font = tier ? g_flight_font_medium_sw
				    : g_flight_font_small_sw;
			height = tier ? 10 : 8;
			half_height = tier ? 5 : 4;
		} else if (g_flight_resolution_mode ==
				   FLIGHT_RESOLUTION_480X360 &&
			   tier) {
			font = g_flight_font_small_sw;
			height = 8;
			half_height = 4;
		}
		definition->fonts[tier].asset_id =
			xvt_render_assets_image_id(font);
		definition->fonts[tier].line_height = (uint16_t)height;
		definition->fonts[tier].half_height = (uint16_t)half_height;
	}
	memcpy(definition->beam_segment_colors,
	       g_hud_beam_segment_color_by_charge_step,
	       sizeof definition->beam_segment_colors);
	memcpy(definition->shield_colors, g_hud_shield_colors,
	       sizeof definition->shield_colors);
	for (unsigned index = 0; index < 9; ++index) {
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
			definition->beam_offsets[index][0] =
				definition->beam_offsets[index][1] =
					(int16_t)(3 * (8 - index));
		} else {
			const struct hud_beam_segment_offset *offsets =
				g_flight_resolution_mode ==
						FLIGHT_RESOLUTION_480X360
					? g_hud_beam_segment_offsets480x360
					: g_hud_beam_segment_offsets320x240;
			definition->beam_offsets[index][0] = offsets[index].x;
			definition->beam_offsets[index][1] = offsets[index].y;
		}
	}
}

void xvt_render_assets_capture_cockpit(int auxiliary)
{
	++g_resource_generation;
	unsigned first = auxiliary ? 288 : 0;
	unsigned end = auxiliary ? 432 : 288;
	for (unsigned i = first; i < end; ++i) {
		const struct hud_element_layout *e = &g_hud_element_layouts[i];
		g_layout.elements[i] = (struct xvt_snap_hud_element){
			e->x,	       e->y,
			e->selector,   e->color_index_or_widget_param,
			e->clip_width, e->clip_height_or_foreground_color};
	}
	uint16_t mask_size =
		g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240 ? 200
								      : 480;
	if (!auxiliary) {
		for (unsigned i = 0; i < 28; ++i) {
			const struct hud_cockpit_resource_descriptor *d =
				&g_hud_cockpit_resource_descriptors[i];
			struct xvt_snap_cockpit_descriptor *out =
				&g_layout.descriptors[i];
			out->enabled = d->resource_ref;
			memcpy(out->lfd_name, d->lfd_name, sizeof d->lfd_name);
			out->lfd_name[9] = 0;
			memcpy(out->display_name, d->display_name,
			       sizeof d->display_name);
			out->display_name[16] = 0;
			out->viewport = (struct xvt_snap_rect){
				d->viewport_origin_x, d->viewport_origin_y,
				d->viewport_width, d->viewport_height};
			out->projection_offset_y = d->projection_offset_y;
		}
		memcpy(g_layout.panel_basename,
		       g_hud_panel_sprite_file_info.base_name, 9);
		g_layout.panel_basename[9] = 0;
		g_layout.sprite_count =
			g_hud_panel_sprite_file_info.sprite_count;
		g_layout.sprite_count_addend =
			g_hud_panel_sprite_file_info.sprite_count_addend;
		g_layout.mask_bytes[0] = g_layout.mask_bytes[1] = mask_size;
		memcpy(g_layout.masks[0], g_hud_cockpit_inset_span_mask,
		       mask_size);
		memcpy(g_layout.masks[1], g_hud_only_view_inset_span_mask,
		       mask_size);
	} else {
		g_layout.mask_bytes[2] = mask_size;
		memcpy(g_layout.masks[2], g_hud_craft_list_inset_span_mask,
		       mask_size);
	}
	g_layout.valid = 1;
	g_layout.generation = ++g_layout_generation;
}

void xvt_render_assets_register_lfd(const char *path, uint8_t **entries)
{
	for (unsigned i = 0; i < 28; ++i) {
		if (entries != g_hud_cockpit_resources[i].entries) {
			continue;
		}
		uint16_t handle =
			(uint16_t)g_hud_cockpit_resources[i].memory_handle;
		if (!handle) {
			handle = g_flight_scratch_screen_buffer_handle;
		}
		const struct hud_cockpit_resource_descriptor *d =
			&g_hud_cockpit_resource_descriptors[i];
		struct xvt_snap_rect viewport = {
			d->viewport_origin_x, d->viewport_origin_y,
			d->viewport_width, d->viewport_height};
		uint64_t id = xvt_render_assets_register_cockpit(
			entries, handle, path, &viewport);
		if (g_lfd[i].id != id) {
			++g_resource_generation;
		}
		g_lfd[i] = (struct cockpit_source_binding){id, 0};
		g_layout.descriptors[i].lfd_asset_id = id;
		g_layout.generation = ++g_layout_generation;
		break;
	}
}

void xvt_render_assets_register_panel(const char *path, uint16_t first,
				      uint16_t count, uint16_t skip)
{
	if (!count || first >= 265 || count > 265 - first) {
		return;
	}
	uint64_t id = xvt_render_assets_register_image(
		&g_panels[first], g_hud_panel_sprite_data_handle, path,
		XVT_IMAGE_PNL, skip, count, 0, 0, 0);
	if (g_panels[first].id != id) {
		++g_resource_generation;
	}
	for (unsigned i = 0; i < count; ++i) {
		g_panels[first + i] =
			(struct cockpit_source_binding){id, skip + i};
	}
	if (!first) {
		g_layout.panel_asset_id = id;
	}
	g_layout.generation = ++g_layout_generation;
}

void xvt_render_assets_register_icons(const char *path, uint8_t **frames,
				      uint16_t count)
{
	if (!count) {
		return;
	}
	if (count > XVT_SNAP_MAP_ICON_FRAMES) {
		XVT_LOG_ERROR(
			"snapshot.icon_frames_overflow path=\"%s\" count=%u limit=%u",
			path, count, XVT_SNAP_MAP_ICON_FRAMES);
		Aeron_RequestFatalError(
			"Map asset error",
			"Loaded map icons exceed the original frame capacity.");
		return;
	}
	uint64_t id = xvt_render_assets_register_image(
		frames, g_flight_icon_frames_handle, path, XVT_IMAGE_ICO, 0,
		count, 0, 0, 0);
	for (unsigned i = 0; i < count; ++i) {
		g_icons[i] = (struct cockpit_source_binding){id, i};
	}
}

void xvt_render_assets_register_flight_fonts(void)
{
	xvt_render_assets_register_image(
		g_flight_font_micro_sw, g_flight_micro_font_handle,
		"MICRO32.FNT", XVT_IMAGE_MICRO_FNT, 0, 224, 0, 4, 0);
	xvt_render_assets_register_image(
		g_flight_font_small_sw, g_flight_small_font_handle,
		"MICRO48.FNT", XVT_IMAGE_MICRO_FNT, 0, 224, 0, 4, 0);
	if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240) {
		xvt_render_assets_register_image(
			g_flight_font_medium_sw, g_flight_medium_font_handle,
			"MICRO64.FNT", XVT_IMAGE_MICRO_FNT, 0, 224, 0, 4, 0);
	}
}

uint64_t xvt_render_assets_map_icon_frame(unsigned index, uint32_t *frame)
{
	if (frame) {
		*frame = index < XVT_SNAP_MAP_ICON_FRAMES ? g_icons[index].frame
							  : 0;
	}
	return index < XVT_SNAP_MAP_ICON_FRAMES ? g_icons[index].id : 0;
}
