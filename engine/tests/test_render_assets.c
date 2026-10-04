#define _POSIX_C_SOURCE 200809L
/* Checks the render asset registry (xvt_runtime/snapshot/render_assets.h, and RegisterCockpit from
 * render_cockpit_assets.h) against the promises in its headers, for the calls render_assets.c defines.
 * Paths resolve through storage, so the test binds an Aeron VFS to a fresh temporary folder and writes the
 * asset files it registers there; no game file is read. Most checks start the registry with Init alone;
 * the check of when a freed source leaves the exports runs the render snapshot as the game does.
 *
 * Not run here: an unresolvable path and a full registry or export list, which request a fatal renderer
 * error and show a message box. Not checkable: that Shutdown keeps the type bindings, since Export does
 * nothing until Init, and Init clears them. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt/frontend/front_image.h"
#include "xvt/render/flight_palette.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_cockpit_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#include "xvt_runtime/storage/storage.h"

static char g_folder[XVT_TEST_PATH_CAPACITY];
static char g_asset[XVT_TEST_PATH_CAPACITY];
static AeronVfs *g_vfs;
static struct xvt_render_snapshot *g_snapshot;
static int g_owner_a;
static int g_owner_b;

/* A fresh folder whose asset root holds every file the checks register, bound to storage. */
static void make_roots(void)
{
	xvt_test_make_folder(g_folder);
	xvt_test_make_subfolder(g_folder, "asset");
	xvt_test_make_subfolder(g_folder, "user");
	xvt_test_make_subfolder(g_folder, "temp");
	xvt_test_join(g_asset, g_folder, "asset");
	static const char *const k_files[] = {
		"ship.opt",  "wing.opt", "skin.act", "hull.act",   "pic.bmp",
		"other.bmp", "A.OPT",	 "a.opt",    "cockpit.lfd"};
	for (size_t i = 0; i < sizeof k_files / sizeof k_files[0]; ++i) {
		xvt_test_write_text(g_asset, k_files[i], "x");
	}
	char user[XVT_TEST_PATH_CAPACITY];
	char temp[XVT_TEST_PATH_CAPACITY];
	xvt_test_join(user, g_folder, "user");
	xvt_test_join(temp, g_folder, "temp");
	AeronVfsConfig config = {0};
	config.asset_root = g_asset;
	config.resource_root = g_asset;
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

static void fresh(void)
{
	xvt_render_assets_shutdown();
	xvt_render_assets_init();
}

/* Exports into the test's snapshot, which first has no valid flight view and no types. */
static void export(void)
{
	memset(g_snapshot->types, 0, sizeof g_snapshot->types);
	g_snapshot->flight_valid = 0;
	g_snapshot->dropped_records = 0;
	xvt_render_assets_export(g_snapshot);
}

static int lists_opt(uint64_t id)
{
	for (uint32_t i = 0; i < g_snapshot->opt_asset_count; ++i) {
		if (g_snapshot->opt_assets[i].id == id) {
			return 1;
		}
	}
	return 0;
}

/* The exported texture entry for id, or NULL. */
static const struct xvt_snap_texture_asset *texture_entry(uint64_t id)
{
	for (uint32_t i = 0; i < g_snapshot->texture_asset_count; ++i) {
		if (g_snapshot->texture_assets[i].id == id) {
			return &g_snapshot->texture_assets[i];
		}
	}
	return NULL;
}

/* The exported image entry for id, or NULL. */
static const struct xvt_snap_image_asset *image_entry(uint64_t id)
{
	for (uint32_t i = 0; i < g_snapshot->image_asset_count; ++i) {
		if (g_snapshot->image_assets[i].id == id) {
			return &g_snapshot->image_assets[i];
		}
	}
	return NULL;
}

static uint64_t register_bmp(const void *owner, const char *path)
{
	return xvt_render_assets_register_image(owner, 0, path, XVT_IMAGE_BMP,
						0, 1, 0, 0, 0);
}

/* Before Init nothing registers, nothing is freed or exported, and no colors are copied. */
static void check_before_init(void)
{
	XVT_ASSERT_INT_EQ(register_bmp(&g_owner_a, "pic.bmp"), 0);
	xvt_render_assets_register_opt(3, "ship.opt");
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(3), 0);
	memset(g_snapshot, 0, sizeof *g_snapshot);
	g_snapshot->opt_asset_count = 77;
	g_snapshot->image_asset_count = 77;
	xvt_render_assets_export(g_snapshot);
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_count, 77);
	XVT_ASSERT_INT_EQ(g_snapshot->image_asset_count, 77);
	XVT_ASSERT_INT_EQ(g_snapshot->image_asset_generation, 0);
	struct xvt_frontend_image_colors colors;
	XVT_ASSERT_INT_EQ(xvt_render_assets_copy_frontend_colors(1, &colors),
			  0);
	xvt_render_assets_retire_handle(3);
	xvt_render_assets_retire_image(&g_owner_a);
	xvt_render_assets_begin_frame();
}

/* Init restarts ids and generations at 1 and registers the built-in default cursor, the first id. */
static void check_init(void)
{
	fresh();
	XVT_ASSERT_TRUE(xvt_render_assets_default_cursor() != NULL);
	uint64_t cursor =
		xvt_render_assets_image_id(xvt_render_assets_default_cursor());
	XVT_ASSERT_INT_EQ(cursor, 1);
	export();
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_count, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->texture_asset_count, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->image_asset_count, 1);
	XVT_ASSERT_INT_EQ(g_snapshot->image_assets[0].kind,
			  XVT_IMAGE_BUILTIN_CURSOR);
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_generation, 1);
	XVT_ASSERT_INT_EQ(g_snapshot->texture_asset_generation, 1);
	/* Registering the cursor changed the image sources. */
	XVT_ASSERT_TRUE(g_snapshot->image_asset_generation > 1);

	/* A second registry life starts the same way. */
	xvt_render_assets_register_opt(4, "ship.opt");
	fresh();
	XVT_ASSERT_INT_EQ(
		xvt_render_assets_image_id(xvt_render_assets_default_cursor()),
		1);
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(4), 0);
}

/* The same file with the same parameters keeps its id; anything else under the same key retires the old
 * source and gets a new id. */
static void check_register_rule(void)
{
	fresh();
	uint64_t first = register_bmp(&g_owner_a, "pic.bmp");
	XVT_ASSERT_TRUE(first != 0);
	XVT_ASSERT_INT_EQ(register_bmp(&g_owner_a, "pic.bmp"), first);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), first);

	uint64_t ids[8];
	ids[0] = first;
	ids[1] = register_bmp(&g_owner_a, "other.bmp");
	ids[2] = xvt_render_assets_register_image(&g_owner_a, 0, "other.bmp",
						  XVT_IMAGE_PNL, 0, 1, 0, 0, 0);
	ids[3] = xvt_render_assets_register_image(&g_owner_a, 0, "other.bmp",
						  XVT_IMAGE_PNL, 2, 1, 0, 0, 0);
	ids[4] = xvt_render_assets_register_image(&g_owner_a, 0, "other.bmp",
						  XVT_IMAGE_PNL, 2, 5, 0, 0, 0);
	ids[5] = xvt_render_assets_register_image(&g_owner_a, 0, "other.bmp",
						  XVT_IMAGE_PNL, 2, 5, 9, 0, 0);
	ids[6] = xvt_render_assets_register_image(&g_owner_a, 0, "other.bmp",
						  XVT_IMAGE_PNL, 2, 5, 9, 4, 0);
	ids[7] = xvt_render_assets_register_image(&g_owner_a, 0, "other.bmp",
						  XVT_IMAGE_PNL, 2, 5, 9, 4, 1);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_TRUE(ids[i] != 0);
		for (int j = 0; j < i; ++j) {
			XVT_ASSERT_TRUE(ids[i] != ids[j]);
		}
	}
	/* Only the newest is live under the key; the retired ones are still exported. */
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), ids[7]);
	export();
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_TRUE(image_entry(ids[i]) != NULL);
	}

	/* Another owner is another key. */
	uint64_t other = register_bmp(&g_owner_b, "pic.bmp");
	XVT_ASSERT_TRUE(other != 0 && other != first);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), ids[7]);

	/* With no owner the handle is the key; with neither there is no id. */
	uint64_t by_handle = xvt_render_assets_register_image(
		NULL, 21, "pic.bmp", XVT_IMAGE_BMP, 0, 1, 0, 0, 0);
	XVT_ASSERT_TRUE(by_handle != 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_register_image(NULL, 21, "pic.bmp",
							   XVT_IMAGE_BMP, 0, 1,
							   0, 0, 0),
			  by_handle);
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(21), by_handle);
	XVT_ASSERT_INT_EQ(xvt_render_assets_register_image(NULL, 0, "pic.bmp",
							   XVT_IMAGE_BMP, 0, 1,
							   0, 0, 0),
			  0);
}

/* Resolved paths compare in any letter case: two names for one file in different case are one source. */
static void check_path_case(void)
{
	fresh();
	xvt_render_assets_register_opt(5, "A.OPT");
	uint64_t id = xvt_render_assets_handle_id(5);
	XVT_ASSERT_TRUE(id != 0);
	xvt_render_assets_register_opt(5, "a.opt");
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(5), id);
	xvt_render_assets_register_opt(5, "ship.opt");
	XVT_ASSERT_TRUE(xvt_render_assets_handle_id(5) != id);
}

/* A cockpit source also needs the same viewport to keep its id. */
static void check_register_cockpit(void)
{
	fresh();
	struct xvt_snap_rect viewport = {0, 0, 640, 300};
	uint64_t id = xvt_render_assets_register_cockpit(
		&g_owner_a, 12, "cockpit.lfd", &viewport);
	XVT_ASSERT_TRUE(id != 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_register_cockpit(
				  &g_owner_a, 12, "cockpit.lfd", &viewport),
			  id);
	struct xvt_snap_rect moved = {0, 10, 640, 300};
	uint64_t again = xvt_render_assets_register_cockpit(
		&g_owner_a, 12, "cockpit.lfd", &moved);
	XVT_ASSERT_TRUE(again != 0 && again != id);
	export();
	XVT_ASSERT_INT_EQ(image_entry(again)->kind, XVT_IMAGE_LFD);
}

/* Model and texture files are keyed by handle; handle 0 is ignored. HandleId never matches an
 * owner-keyed or retired source. */
static void check_handles(void)
{
	fresh();
	xvt_render_assets_register_opt(0, "ship.opt");
	xvt_render_assets_register_texture(0, "skin.act");
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(0), 0);
	export();
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_count, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->texture_asset_count, 0);

	xvt_render_assets_register_opt(7, "ship.opt");
	xvt_render_assets_register_texture(8, "skin.act");
	uint64_t opt = xvt_render_assets_handle_id(7);
	uint64_t texture = xvt_render_assets_handle_id(8);
	XVT_ASSERT_TRUE(opt != 0 && texture != 0 && opt != texture);
	export();
	XVT_ASSERT_TRUE(lists_opt(opt));
	XVT_ASSERT_TRUE(texture_entry(texture) != NULL);

	XVT_ASSERT_TRUE(xvt_render_assets_register_image(
				&g_owner_a, 30, "pic.bmp", XVT_IMAGE_BMP, 0, 1,
				0, 0, 0) != 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(30), 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(NULL), 0);

	xvt_render_assets_retire_handle(7);
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(7), 0);
}

/* Each kind's generation rises when its sources change. */
static void check_generations(void)
{
	fresh();
	export();
	uint64_t opt = g_snapshot->opt_asset_generation;
	uint64_t texture = g_snapshot->texture_asset_generation;
	uint64_t image = g_snapshot->image_asset_generation;

	xvt_render_assets_register_opt(7, "ship.opt");
	export();
	XVT_ASSERT_TRUE(g_snapshot->opt_asset_generation > opt);
	opt = g_snapshot->opt_asset_generation;

	xvt_render_assets_register_texture(8, "skin.act");
	export();
	XVT_ASSERT_TRUE(g_snapshot->texture_asset_generation > texture);
	texture = g_snapshot->texture_asset_generation;

	register_bmp(&g_owner_a, "pic.bmp");
	export();
	XVT_ASSERT_TRUE(g_snapshot->image_asset_generation > image);
	image = g_snapshot->image_asset_generation;

	/* Retiring is a change too. */
	xvt_render_assets_retire_handle(7);
	export();
	XVT_ASSERT_TRUE(g_snapshot->opt_asset_generation > opt);
	xvt_render_assets_retire_image(&g_owner_a);
	export();
	XVT_ASSERT_TRUE(g_snapshot->image_asset_generation > image);
}

/* Bound model types take their asset ids at export without a valid flight view; a texture entry names
 * the first model type bound to it. */
static void check_bindings(void)
{
	fresh();
	xvt_render_assets_register_opt(7, "ship.opt");
	xvt_render_assets_register_texture(8, "skin.act");
	xvt_render_assets_register_texture(9, "hull.act");
	uint64_t opt = xvt_render_assets_handle_id(7);
	uint64_t skin = xvt_render_assets_handle_id(8);
	uint64_t hull = xvt_render_assets_handle_id(9);
	xvt_render_assets_bind_type(3, 7);
	xvt_render_assets_bind_type(5, 8);
	xvt_render_assets_bind_type(12, 8);
	xvt_render_assets_bind_type(XVT_SNAP_TYPES, 7);
	export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, opt);
	XVT_ASSERT_INT_EQ(g_snapshot->types[5].texture_asset_id, skin);
	XVT_ASSERT_INT_EQ(g_snapshot->types[12].texture_asset_id, skin);
	XVT_ASSERT_INT_EQ(texture_entry(skin)->model_type, 5);
	XVT_ASSERT_INT_EQ(texture_entry(hull)->model_type, UINT16_MAX);

	/* With a valid flight view the types keep what they hold. */
	memset(g_snapshot->types, 0, sizeof g_snapshot->types);
	g_snapshot->flight_valid = 1;
	xvt_render_assets_export(g_snapshot);
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);

	/* Handle 0 or an unknown handle unbinds. */
	xvt_render_assets_bind_type(3, 0);
	xvt_render_assets_bind_type(5, 444);
	export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->types[5].texture_asset_id, 0);
	XVT_ASSERT_INT_EQ(texture_entry(skin)->model_type, 12);
}

/* FreeHandle retires every source carrying the handle, owner-keyed ones included, and drops the type
 * bindings to them; retired sources are still exported. It does nothing for 0 or above 65535. */
static void check_free_handle(void)
{
	fresh();
	xvt_render_assets_register_opt(9, "ship.opt");
	uint64_t opt = xvt_render_assets_handle_id(9);
	uint64_t image = xvt_render_assets_register_image(
		&g_owner_a, 9, "pic.bmp", XVT_IMAGE_BMP, 0, 1, 0, 0, 0);
	xvt_render_assets_bind_type(3, 9);

	xvt_render_assets_retire_handle(0);
	xvt_render_assets_retire_handle(65536 + 9);
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(9), opt);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), image);

	xvt_render_assets_retire_handle(9);
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(9), 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), 0);
	export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);
	XVT_ASSERT_TRUE(lists_opt(opt));
	XVT_ASSERT_TRUE(image_entry(image) != NULL);

	/* Ids are never reused: a new source under the freed handle gets a new one. */
	xvt_render_assets_register_opt(9, "ship.opt");
	XVT_ASSERT_TRUE(xvt_render_assets_handle_id(9) != 0 &&
			xvt_render_assets_handle_id(9) != opt);
}

/* FreeImage retires every source keyed by owner, and nothing for NULL. */
static void check_free_image(void)
{
	fresh();
	uint64_t a = register_bmp(&g_owner_a, "pic.bmp");
	uint64_t b = register_bmp(&g_owner_b, "pic.bmp");
	xvt_render_assets_retire_image(NULL);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), a);
	xvt_render_assets_retire_image(&g_owner_a);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_b), b);
	export();
	XVT_ASSERT_TRUE(image_entry(a) != NULL);
}

/* ClearMission clears the type bindings and raises the model and texture generations; it retires no
 * source. */
static void check_clear_mission(void)
{
	fresh();
	xvt_render_assets_register_opt(7, "ship.opt");
	xvt_render_assets_register_texture(8, "skin.act");
	xvt_render_assets_bind_type(3, 7);
	xvt_render_assets_bind_type(4, 8);
	export();
	uint64_t opt = g_snapshot->opt_asset_generation;
	uint64_t texture = g_snapshot->texture_asset_generation;
	xvt_render_assets_clear_mission();
	export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->types[4].texture_asset_id, 0);
	XVT_ASSERT_TRUE(g_snapshot->opt_asset_generation > opt);
	XVT_ASSERT_TRUE(g_snapshot->texture_asset_generation > texture);
	XVT_ASSERT_TRUE(xvt_render_assets_handle_id(7) != 0);
	XVT_ASSERT_TRUE(xvt_render_assets_handle_id(8) != 0);
}

/* Shutdown clears the registry, which takes no new source until Init. */
static void check_shutdown(void)
{
	fresh();
	xvt_render_assets_register_opt(7, "ship.opt");
	uint64_t image = register_bmp(&g_owner_a, "pic.bmp");
	XVT_ASSERT_TRUE(image != 0);
	xvt_render_assets_shutdown();
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(7), 0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_image_id(&g_owner_a), 0);
	XVT_ASSERT_INT_EQ(register_bmp(&g_owner_a, "pic.bmp"), 0);
	xvt_render_assets_register_opt(7, "ship.opt");
	XVT_ASSERT_INT_EQ(xvt_render_assets_handle_id(7), 0);
	xvt_render_assets_init();
	XVT_ASSERT_TRUE(register_bmp(&g_owner_a, "pic.bmp") != 0);
}

/* The frontend colors of a BMP source can be copied, retired or not; other ids give 0. */
static void check_frontend_colors(void)
{
	fresh();
	static struct image_resource image;
	memset(&image, 0, sizeof image);
	for (int i = 0; i < 256; ++i) {
		image.color_lut[i] = i * 37;
	}
	xvt_render_assets_register_frontend_image(&image, "pic.bmp", 0, 1);
	uint64_t id = xvt_render_assets_image_id(&image);
	XVT_ASSERT_TRUE(id != 0);

	struct xvt_frontend_image_colors colors;
	memset(&colors, 0, sizeof colors);
	XVT_ASSERT_INT_EQ(xvt_render_assets_copy_frontend_colors(id, &colors),
			  1);
	for (int i = 0; i < 256; ++i) {
		XVT_ASSERT_INT_EQ(colors.color_lut[i], image.color_lut[i]);
	}
	XVT_ASSERT_INT_EQ(colors.pixel_format_555, 1);

	xvt_render_assets_retire_image(&image);
	memset(&colors, 0, sizeof colors);
	XVT_ASSERT_INT_EQ(xvt_render_assets_copy_frontend_colors(id, &colors),
			  1);
	XVT_ASSERT_INT_EQ(colors.color_lut[5], image.color_lut[5]);

	XVT_ASSERT_INT_EQ(xvt_render_assets_copy_frontend_colors(0, &colors),
			  0);
	XVT_ASSERT_INT_EQ(xvt_render_assets_copy_frontend_colors(id, NULL), 0);
	xvt_render_assets_register_opt(7, "ship.opt");
	XVT_ASSERT_INT_EQ(xvt_render_assets_copy_frontend_colors(
				  xvt_render_assets_handle_id(7), &colors),
			  0);
	uint64_t panel = xvt_render_assets_register_image(
		&g_owner_b, 0, "pic.bmp", XVT_IMAGE_PNL, 0, 1, 0, 0, 0);
	XVT_ASSERT_INT_EQ(
		xvt_render_assets_copy_frontend_colors(panel, &colors), 0);
}

/* Export stamps the flight palette, and does nothing for NULL. */
static void check_export_palette(void)
{
	fresh();
	for (int i = 0; i < 256; ++i) {
		g_sw_palette[i] = (struct rgb_triplet){(uint8_t)(i & 63),
						       (uint8_t)((i * 3) & 63),
						       (uint8_t)((i * 7) & 63)};
	}
	export();
	for (unsigned i = 0; i < 256; ++i) {
		XVT_ASSERT_INT_EQ(g_snapshot->flight_palette_argb[i],
				  xvt_render_draw_color(i));
	}
	xvt_render_assets_export(NULL);
}

/* A freed source stays exported until the renderer has consumed the last export and neither the current
 * nor the previous snapshot uses it. The render snapshot runs here as the game runs it: its Init starts
 * the registry, each frame's BeginFrame runs the registry's, and each Commit exports. */
static void check_retired_lifetime(void)
{
	xvt_render_assets_shutdown();
	xvt_render_snapshot_shutdown();
	xvt_render_snapshot_init();
	xvt_render_capture_begin_mission();
	xvt_render_assets_register_opt(7, "ship.opt");
	uint64_t id = xvt_render_assets_handle_id(7);
	XVT_ASSERT_TRUE(id != 0);

	/* Frame 1 publishes a flight view whose model type 3 uses the source. */
	xvt_render_snapshot_begin_frame();
	struct xvt_render_snapshot *writer = xvt_render_snapshot_writer();
	writer->flight_valid = 1;
	writer->types[3].model_asset_id = id;
	xvt_render_snapshot_commit(1, 1, 0);
	xvt_render_assets_retire_handle(7);
	/* From now on no view is published: later snapshots do not use the source. */
	xvt_render_capture_end_mission();

	/* Frame 2: the export of frame 1 is not consumed yet, and frame 1 uses the source. */
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(2, 1, 0);
	const struct xvt_render_snapshot *current =
		xvt_render_snapshot_current();
	int listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i) {
		listed |= current->opt_assets[i].id == id;
	}
	XVT_ASSERT_TRUE(listed);

	/* Frame 3: consumed, but the previous snapshot (frame 1) still uses it. */
	xvt_render_assets_consumed(current->snapshot_serial);
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(3, 1, 0);
	current = xvt_render_snapshot_current();
	listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i) {
		listed |= current->opt_assets[i].id == id;
	}
	XVT_ASSERT_TRUE(listed);

	/* Frame 4: neither snapshot uses it, but the export of frame 3 is not consumed. */
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(4, 1, 0);
	current = xvt_render_snapshot_current();
	listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i) {
		listed |= current->opt_assets[i].id == id;
	}
	XVT_ASSERT_TRUE(listed);

	/* Consuming an older export is not enough. */
	xvt_render_assets_consumed(current->snapshot_serial - 1);
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(5, 1, 0);
	current = xvt_render_snapshot_current();
	listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i) {
		listed |= current->opt_assets[i].id == id;
	}
	XVT_ASSERT_TRUE(listed);

	/* Frame 6: the last export is consumed and no snapshot uses it: it is gone. */
	xvt_render_assets_consumed(current->snapshot_serial);
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_commit(6, 1, 0);
	current = xvt_render_snapshot_current();
	for (uint32_t i = 0; i < current->opt_asset_count; ++i) {
		XVT_ASSERT_TRUE(current->opt_assets[i].id != id);
	}
	xvt_render_snapshot_shutdown();
}

int main(void)
{
	g_snapshot = calloc(1, sizeof *g_snapshot);
	XVT_ASSERT_TRUE(g_snapshot != NULL);
	make_roots();
	check_before_init();
	check_init();
	check_register_rule();
	check_path_case();
	check_register_cockpit();
	check_handles();
	check_generations();
	check_bindings();
	check_free_handle();
	check_free_image();
	check_clear_mission();
	check_shutdown();
	check_frontend_colors();
	check_export_palette();
	check_retired_lifetime();
	xvt_render_assets_shutdown();
	remove_roots();
	free(g_snapshot);
	return 0;
}
