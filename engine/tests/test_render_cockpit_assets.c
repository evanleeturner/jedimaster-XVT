#define _POSIX_C_SOURCE 200809L
/* Checks the cockpit asset bindings (xvt_runtime/snapshot/render_cockpit_assets.h, and the cockpit, panel,
 * icon and font calls of render_assets.h that render_cockpit_assets.c defines) against the promises in
 * their headers. Files register through the asset registry, whose paths resolve through storage, so the
 * test binds an Aeron VFS to a fresh temporary folder holding the files it names. The recovered game's
 * HUD layout, cockpit resources, fonts and handles are set here; no game data is read. Each check starts
 * the registry again, which resets the cockpit bindings.
 *
 * Not checkable: the layout generation, which CaptureDefinition always copies as 0 and nothing else
 * reads. Not run here: more than XVT_SNAP_MAP_ICON_FRAMES icons, which requests a fatal error that shows
 * a message box. */
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/render/flight_sw.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_cockpit_assets.h"
#include "xvt_runtime/storage/storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char g_folder[XVT_TEST_PATH_CAPACITY];
static AeronVfs *g_vfs;
static struct xvt_cockpit_definition *g_definition;
static struct xvt_render_snapshot *g_snapshot;
static uint8_t g_micro_font[8], g_small_font[8], g_medium_font[8];
static uint8_t *g_icons_a[5];
static uint8_t *g_icons_b[3];

static void make_roots(void)
{
	char asset[XVT_TEST_PATH_CAPACITY], user[XVT_TEST_PATH_CAPACITY],
		temp[XVT_TEST_PATH_CAPACITY];
	xvt_test_make_folder(g_folder);
	xvt_test_make_subfolder(g_folder, "asset");
	xvt_test_make_subfolder(g_folder, "user");
	xvt_test_make_subfolder(g_folder, "temp");
	xvt_test_join(asset, g_folder, "asset");
	xvt_test_join(user, g_folder, "user");
	xvt_test_join(temp, g_folder, "temp");
	static const char *const k_files[] = {
		"MICRO32.FNT", "MICRO48.FNT", "MICRO64.FNT",
		"panel.pnl",   "panel2.pnl",  "icons.ico",
		"icons2.ico",  "cockpit.lfd", "cockpit2.lfd"};
	for (size_t i = 0; i < sizeof k_files / sizeof k_files[0]; ++i) {
		xvt_test_write_text(asset, k_files[i], "x");
	}
	AeronVfsConfig config = {0};
	config.asset_root = asset;
	config.resource_root = asset;
	config.user_root = user;
	config.temp_root = temp;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	xvt_storage_bind(g_vfs);
}

static void remove_roots(void)
{
	xvt_storage_bind(NULL);
	AeronVfs_Destroy(g_vfs);
	xvt_test_remove_tree(g_folder);
}

/* A live HUD layout with a different value in every field, cockpit resources without handles, fonts and
 * handles for each, the 640x480 resolution, and a fresh registry. */
static void fresh(void)
{
	for (unsigned i = 0; i < HUD_INSTRUMENT_COUNT; ++i) {
		g_hud_element_layouts[i] = (struct hud_element_layout){
			(uint16_t)i,	   (uint16_t)(1000 + i),
			(uint16_t)(i % 7), (uint16_t)(i % 250),
			(uint16_t)(2 * i), (int16_t)-i};
	}
	for (unsigned i = 0; i < 28; ++i) {
		struct hud_cockpit_resource_descriptor *d =
			&g_hud_cockpit_resource_descriptors[i];
		memset(d, 0, sizeof *d);
		d->resource_ref = (uint8_t)(i % 2);
		memcpy(d->lfd_name, "COCKPITxx", 9);
		d->lfd_name[7] = (char)('A' + i);
		memcpy(d->display_name, "Display name ...", 16);
		d->display_name[13] = (char)('a' + i);
		d->viewport_origin_x = (uint16_t)i;
		d->viewport_origin_y = (uint16_t)(2 * i);
		d->viewport_width = (uint16_t)(300 + i);
		d->viewport_height = (uint16_t)(200 + i);
		d->projection_offset_y = (int16_t)(i - 14);
		g_hud_cockpit_resources[i].memory_handle = 0;
	}
	memcpy(g_hud_panel_sprite_file_info.base_name, "PANELBASE", 9);
	g_hud_panel_sprite_file_info.sprite_count = 40;
	g_hud_panel_sprite_file_info.sprite_count_addend = 3;
	for (unsigned i = 0; i < 480; ++i) {
		g_hud_cockpit_inset_span_mask[i] = (uint8_t)i;
		g_hud_only_view_inset_span_mask[i] = (uint8_t)(i * 3);
		g_hud_craft_list_inset_span_mask[i] = (uint8_t)(i * 5);
	}
	g_flight_resolution_mode = FLIGHT_RESOLUTION_640X480;
	g_flight_font_micro_sw = g_micro_font;
	g_flight_font_small_sw = g_small_font;
	g_flight_font_medium_sw = g_medium_font;
	g_flight_micro_font_handle = 41;
	g_flight_small_font_handle = 42;
	g_flight_medium_font_handle = 43;
	g_flight_scratch_screen_buffer_handle = 66;
	g_hud_panel_sprite_data_handle = 70;
	g_flight_icon_frames_handle = 71;
	xvt_render_assets_shutdown();
	xvt_render_assets_init();
}

static const struct xvt_cockpit_definition *definition(void)
{
	memset(g_definition, 0xEE, sizeof *g_definition);
	xvt_render_cockpit_capture_definition(g_definition);
	return g_definition;
}

static int all_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	for (size_t i = 0; i < size; ++i) {
		if (bytes[i]) {
			return 0;
		}
	}
	return 1;
}

static void check_elements(const struct xvt_cockpit_definition *d)
{
	for (unsigned i = 0; i < HUD_INSTRUMENT_COUNT; ++i) {
		const struct hud_element_layout *live =
			&g_hud_element_layouts[i];
		const struct xvt_snap_hud_element *copy =
			&d->layout.elements[i];
		XVT_ASSERT_INT_EQ(copy->x, live->x);
		XVT_ASSERT_INT_EQ(copy->y, live->y);
		XVT_ASSERT_INT_EQ(copy->selector, live->selector);
		XVT_ASSERT_INT_EQ(copy->color_index,
				  live->color_index_or_widget_param);
		XVT_ASSERT_INT_EQ(copy->clip_width, live->clip_width);
		/* Element 127's warning timer is zeroed in the copy. */
		XVT_ASSERT_INT_EQ(
			copy->clip_height_or_foreground,
			i == 127 ? 0 : live->clip_height_or_foreground_color);
	}
}

/* CaptureCockpit copies the descriptors, panel sprite info and span masks 0 and 1, and marks the layout
 * valid; CaptureDefinition then gives the live element values. Both captures raise the resource
 * generation. */
static void check_capture_cockpit(void)
{
	fresh();
	uint64_t generation = definition()->resource_generation;
	xvt_render_assets_capture_cockpit(0);
	const struct xvt_cockpit_definition *d = definition();
	XVT_ASSERT_TRUE(d->resource_generation > generation);
	generation = d->resource_generation;
	XVT_ASSERT_INT_EQ(d->layout.valid, 1);
	check_elements(d);
	for (unsigned i = 0; i < 28; ++i) {
		const struct hud_cockpit_resource_descriptor *live =
			&g_hud_cockpit_resource_descriptors[i];
		const struct xvt_snap_cockpit_descriptor *copy =
			&d->layout.descriptors[i];
		XVT_ASSERT_INT_EQ(copy->enabled, live->resource_ref);
		XVT_ASSERT_INT_EQ(memcmp(copy->lfd_name, live->lfd_name, 9), 0);
		XVT_ASSERT_INT_EQ(
			memcmp(copy->display_name, live->display_name, 16), 0);
		XVT_ASSERT_INT_EQ(copy->viewport.x, live->viewport_origin_x);
		XVT_ASSERT_INT_EQ(copy->viewport.y, live->viewport_origin_y);
		XVT_ASSERT_INT_EQ(copy->viewport.width, live->viewport_width);
		XVT_ASSERT_INT_EQ(copy->viewport.height, live->viewport_height);
		XVT_ASSERT_INT_EQ(copy->projection_offset_y,
				  live->projection_offset_y);
	}
	XVT_ASSERT_INT_EQ(memcmp(d->layout.panel_basename, "PANELBASE", 9), 0);
	XVT_ASSERT_INT_EQ(d->layout.sprite_count, 40);
	XVT_ASSERT_INT_EQ(d->layout.sprite_count_addend, 3);
	for (int mask = 0; mask < 2; ++mask) {
		const uint8_t *live = mask ? g_hud_only_view_inset_span_mask
					   : g_hud_cockpit_inset_span_mask;
		XVT_ASSERT_TRUE(d->layout.mask_bytes[mask] > 0 &&
				d->layout.mask_bytes[mask] <= 480);
		XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[mask], live,
					 d->layout.mask_bytes[mask]),
				  0);
	}

	/* The auxiliary capture takes span mask 2 instead, and leaves the descriptors, panel info and masks 0
	 * and 1 as they were. */
	struct xvt_cockpit_definition before = *d;
	for (unsigned i = 0; i < 28; ++i) {
		g_hud_cockpit_resource_descriptors[i].resource_ref ^= 1;
	}
	memcpy(g_hud_panel_sprite_file_info.base_name, "OTHERNAME", 9);
	for (unsigned i = 0; i < 480; ++i) {
		g_hud_cockpit_inset_span_mask[i] ^= 0xFF;
		g_hud_only_view_inset_span_mask[i] ^= 0xFF;
	}
	xvt_render_assets_capture_cockpit(1);
	d = definition();
	XVT_ASSERT_TRUE(d->resource_generation > generation);
	XVT_ASSERT_INT_EQ(d->layout.valid, 1);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.descriptors,
				 before.layout.descriptors,
				 sizeof d->layout.descriptors),
			  0);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.panel_basename, "PANELBASE", 9), 0);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[0], before.layout.masks[0],
				 sizeof d->layout.masks[0]),
			  0);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[1], before.layout.masks[1],
				 sizeof d->layout.masks[1]),
			  0);
	XVT_ASSERT_TRUE(d->layout.mask_bytes[2] > 0 &&
			d->layout.mask_bytes[2] <= 480);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[2],
				 g_hud_craft_list_inset_span_mask,
				 d->layout.mask_bytes[2]),
			  0);
}

/* The copy has layout generation 0 and element 127's warning timer zeroed, so equal content compares
 * equal; a valid layout takes the live element values first; the beam and shield tables are copied. */
static void check_definition_copy(void)
{
	fresh();
	xvt_render_assets_capture_cockpit(0);
	struct xvt_cockpit_definition *first = malloc(sizeof *first);
	XVT_ASSERT_TRUE(first != NULL);
	*first = *definition();
	XVT_ASSERT_INT_EQ(first->layout.generation, 0);
	g_hud_element_layouts[127].clip_height_or_foreground_color = 99;
	XVT_ASSERT_INT_EQ(memcmp(definition(), first, sizeof *first), 0);

	g_hud_element_layouts[5].x = 4321;
	g_hud_element_layouts[400].selector = 77;
	const struct xvt_cockpit_definition *d = definition();
	XVT_ASSERT_INT_EQ(d->layout.elements[5].x, 4321);
	XVT_ASSERT_INT_EQ(d->layout.elements[400].selector, 77);
	XVT_ASSERT_INT_EQ(memcmp(d->beam_segment_colors,
				 g_hud_beam_segment_color_by_charge_step,
				 sizeof d->beam_segment_colors),
			  0);
	XVT_ASSERT_INT_EQ(memcmp(d->shield_colors, g_hud_shield_colors,
				 sizeof d->shield_colors),
			  0);
	free(first);
}

/* Reset clears the layout, leaving it invalid, and every panel, icon and LFD binding; it raises the
 * resource generation. */
static void check_reset(void)
{
	fresh();
	xvt_render_assets_capture_cockpit(0);
	xvt_render_assets_register_panel("panel.pnl", 0, 4, 0);
	xvt_render_assets_register_icons("icons.ico", g_icons_a, 3);
	xvt_render_assets_register_lfd("cockpit.lfd",
				       g_hud_cockpit_resources[2].entries);
	const struct xvt_cockpit_definition *d = definition();
	XVT_ASSERT_TRUE(d->layout.valid && d->panels[0].asset_id &&
			d->layout.descriptors[2].lfd_asset_id);
	XVT_ASSERT_TRUE(xvt_render_assets_map_icon_frame(0, NULL) != 0);
	uint64_t generation = d->resource_generation;

	xvt_render_cockpit_reset();
	d = definition();
	XVT_ASSERT_TRUE(d->resource_generation > generation);
	XVT_ASSERT_TRUE(all_zero(&d->layout, sizeof d->layout));
	for (unsigned i = 0; i < XVT_HUD_PANEL_BINDINGS; ++i) {
		XVT_ASSERT_INT_EQ(d->panels[i].asset_id, 0);
	}
	XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(0, NULL), 0);
}

/* RegisterPanel binds slot first + i to frame skip + i, and sets the layout's panel asset when first is
 * 0; a count of 0 or a range that leaves the 265 slots does nothing. */
static void check_register_panel(void)
{
	fresh();
	xvt_render_assets_register_panel("panel.pnl", 10, 3, 5);
	const struct xvt_cockpit_definition *d = definition();
	uint64_t id = d->panels[10].asset_id;
	XVT_ASSERT_TRUE(id != 0);
	for (unsigned i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(d->panels[10 + i].asset_id, id);
		XVT_ASSERT_INT_EQ(d->panels[10 + i].frame, 5 + i);
	}
	XVT_ASSERT_INT_EQ(d->panels[9].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->panels[13].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->layout.panel_asset_id, 0);

	xvt_render_assets_register_panel("panel2.pnl", 0, 2, 1);
	d = definition();
	uint64_t base = d->panels[0].asset_id;
	XVT_ASSERT_TRUE(base != 0 && base != id);
	XVT_ASSERT_INT_EQ(d->panels[0].frame, 1);
	XVT_ASSERT_INT_EQ(d->panels[1].frame, 2);
	XVT_ASSERT_INT_EQ(d->layout.panel_asset_id, base);

	xvt_render_assets_register_panel("panel.pnl", 20, 0, 0);
	xvt_render_assets_register_panel("panel.pnl", 265, 1, 0);
	xvt_render_assets_register_panel("panel.pnl", 260, 6, 0);
	d = definition();
	for (unsigned i = 20; i < XVT_HUD_PANEL_BINDINGS; ++i) {
		XVT_ASSERT_INT_EQ(d->panels[i].asset_id, 0);
	}
	xvt_render_assets_register_panel("panel.pnl", 260, 5, 0);
	d = definition();
	for (unsigned i = 260; i < XVT_HUD_PANEL_BINDINGS; ++i) {
		XVT_ASSERT_TRUE(d->panels[i].asset_id != 0);
	}
}

/* RegisterLfd registers the resource's file with its viewport and binds it to the resource's descriptor;
 * a resource without a handle registers under the flight log buffer's handle; entries that are no
 * resource's do nothing. */
static void check_register_lfd(void)
{
	fresh();
	g_hud_cockpit_resources[4].memory_handle = 55;
	xvt_render_assets_register_lfd("cockpit.lfd",
				       g_hud_cockpit_resources[4].entries);
	uint64_t id =
		xvt_render_assets_image_id(g_hud_cockpit_resources[4].entries);
	XVT_ASSERT_TRUE(id != 0);
	XVT_ASSERT_INT_EQ(definition()->layout.descriptors[4].lfd_asset_id, id);

	memset(g_snapshot, 0, sizeof *g_snapshot);
	xvt_render_assets_export(g_snapshot);
	const struct xvt_snap_image_asset *entry = NULL;
	for (uint32_t i = 0; i < g_snapshot->image_asset_count; ++i) {
		if (g_snapshot->image_assets[i].id == id) {
			entry = &g_snapshot->image_assets[i];
		}
	}
	XVT_ASSERT_TRUE(entry != NULL);
	XVT_ASSERT_INT_EQ(entry->kind, XVT_IMAGE_LFD);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.x, 4);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.y, 8);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.width, 304);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.height, 204);

	/* Resource 6 has no handle: freeing the flight log buffer's handle retires it, and drops its binding. */
	xvt_render_assets_register_lfd("cockpit2.lfd",
				       g_hud_cockpit_resources[6].entries);
	uint64_t unhandled =
		xvt_render_assets_image_id(g_hud_cockpit_resources[6].entries);
	XVT_ASSERT_TRUE(unhandled != 0 && unhandled != id);
	xvt_render_assets_retire_handle(55);
	XVT_ASSERT_INT_EQ(
		xvt_render_assets_image_id(g_hud_cockpit_resources[6].entries),
		unhandled);
	xvt_render_assets_retire_handle(g_flight_scratch_screen_buffer_handle);
	XVT_ASSERT_INT_EQ(
		xvt_render_assets_image_id(g_hud_cockpit_resources[6].entries),
		0);
	XVT_ASSERT_INT_EQ(definition()->layout.descriptors[6].lfd_asset_id, 0);

	static uint8_t *other[3];
	struct xvt_cockpit_definition *before = malloc(sizeof *before);
	XVT_ASSERT_TRUE(before != NULL);
	*before = *definition();
	xvt_render_assets_register_lfd("cockpit.lfd", other);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(other), 0);
	XVT_ASSERT_INT_EQ(memcmp(definition()->layout.descriptors,
				 before->layout.descriptors,
				 sizeof before->layout.descriptors),
			  0);
	free(before);
}

/* RegisterIcons binds icon i to frame i below count; icons past count keep a binding to another icon
 * file; a count of 0 does nothing. MapIconFrame gives 0 and frame 0 for an unbound or out-of-range
 * index. */
static void check_register_icons(void)
{
	fresh();
	uint32_t frame = 99;
	XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(0, &frame), 0);
	XVT_ASSERT_INT_EQ(frame, 0);

	xvt_render_assets_register_icons("icons.ico", g_icons_a, 5);
	uint64_t a = xvt_render_assets_map_icon_frame(0, NULL);
	XVT_ASSERT_TRUE(a != 0);
	for (uint32_t i = 0; i < 5; ++i) {
		XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(i, &frame),
				  a);
		XVT_ASSERT_INT_EQ(frame, i);
	}
	frame = 99;
	XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(5, &frame), 0);
	XVT_ASSERT_INT_EQ(frame, 0);
	frame = 99;
	XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(
				  XVT_SNAP_MAP_ICON_FRAMES, &frame),
			  0);
	XVT_ASSERT_INT_EQ(frame, 0);

	xvt_render_assets_register_icons("icons2.ico", g_icons_b, 3);
	uint64_t b = xvt_render_assets_map_icon_frame(0, NULL);
	XVT_ASSERT_TRUE(b != 0 && b != a);
	for (uint32_t i = 0; i < 5; ++i) {
		XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(i, &frame),
				  i < 3 ? b : a);
		XVT_ASSERT_INT_EQ(frame, i);
	}

	xvt_render_assets_register_icons("icons.ico", g_icons_a, 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(0, NULL), b);
}

/* Forget drops every panel, icon and LFD binding to id, and the layout's panel asset; dropping a panel or
 * LFD binding raises the resource generation. */
static void check_forget(void)
{
	fresh();
	xvt_render_assets_register_panel("panel.pnl", 0, 2, 0);
	xvt_render_assets_register_icons("icons.ico", g_icons_a, 2);
	xvt_render_assets_register_lfd("cockpit.lfd",
				       g_hud_cockpit_resources[3].entries);
	const struct xvt_cockpit_definition *d = definition();
	uint64_t panel = d->panels[0].asset_id;
	uint64_t lfd = d->layout.descriptors[3].lfd_asset_id;
	uint64_t icons = xvt_render_assets_map_icon_frame(1, NULL);
	XVT_ASSERT_TRUE(panel != 0 && lfd != 0 && icons != 0);

	xvt_render_cockpit_forget(icons);
	XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(0, NULL), 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_map_icon_frame(1, NULL), 0);
	d = definition();
	XVT_ASSERT_INT_EQ(d->panels[0].asset_id, panel);
	XVT_ASSERT_INT_EQ(d->layout.descriptors[3].lfd_asset_id, lfd);

	uint64_t generation = d->resource_generation;
	xvt_render_cockpit_forget(panel);
	d = definition();
	XVT_ASSERT_INT_EQ(d->panels[0].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->panels[1].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->layout.panel_asset_id, 0);
	XVT_ASSERT_TRUE(d->resource_generation > generation);

	generation = d->resource_generation;
	xvt_render_cockpit_forget(lfd);
	d = definition();
	XVT_ASSERT_INT_EQ(d->layout.descriptors[3].lfd_asset_id, 0);
	XVT_ASSERT_TRUE(d->resource_generation > generation);
}

/* RegisterFlightFonts registers the micro and small fonts, and the medium one except at 320x240; the
 * fonts the definition names are registered ones. */
static void check_flight_fonts(void)
{
	fresh();
	xvt_render_assets_register_flight_fonts();
	uint64_t micro = xvt_render_assets_image_id(g_micro_font);
	uint64_t small = xvt_render_assets_image_id(g_small_font);
	uint64_t medium = xvt_render_assets_image_id(g_medium_font);
	XVT_ASSERT_TRUE(micro != 0 && small != 0 && medium != 0);
	const struct xvt_cockpit_definition *d = definition();
	for (size_t font = 0; font < sizeof d->fonts / sizeof d->fonts[0];
	     ++font) {
		uint64_t id = d->fonts[font].asset_id;
		XVT_ASSERT_TRUE(id == micro || id == small || id == medium);
	}

	fresh();
	g_flight_resolution_mode = FLIGHT_RESOLUTION_320X240;
	xvt_render_assets_register_flight_fonts();
	micro = xvt_render_assets_image_id(g_micro_font);
	small = xvt_render_assets_image_id(g_small_font);
	XVT_ASSERT_TRUE(micro != 0 && small != 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(g_medium_font), 0);
	d = definition();
	for (size_t font = 0; font < sizeof d->fonts / sizeof d->fonts[0];
	     ++font) {
		uint64_t id = d->fonts[font].asset_id;
		XVT_ASSERT_TRUE(id == micro || id == small);
	}
}

int main(void)
{
	g_definition = malloc(sizeof *g_definition);
	g_snapshot = calloc(1, sizeof *g_snapshot);
	XVT_ASSERT_TRUE(g_definition != NULL && g_snapshot != NULL);
	make_roots();
	check_capture_cockpit();
	check_definition_copy();
	check_reset();
	check_register_panel();
	check_register_lfd();
	check_register_icons();
	check_forget();
	check_flight_fonts();
	xvt_render_assets_shutdown();
	remove_roots();
	free(g_definition);
	free(g_snapshot);
	return 0;
}
