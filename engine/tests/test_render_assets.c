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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char g_folder[XVT_TEST_PATH_CAPACITY];
static char g_asset[XVT_TEST_PATH_CAPACITY];
static AeronVfs* g_vfs;
static XvtRenderSnapshot* g_snapshot;
static int g_ownerA, g_ownerB;

/* A fresh folder whose asset root holds every file the checks register, bound to storage. */
static void MakeRoots(void) {
	XvtTest_MakeFolder(g_folder);
	XvtTest_MakeSubfolder(g_folder, "asset");
	XvtTest_MakeSubfolder(g_folder, "user");
	XvtTest_MakeSubfolder(g_folder, "temp");
	XvtTest_Join(g_asset, g_folder, "asset");
	static const char* const kFiles[] = { "ship.opt",  "wing.opt", "skin.act", "hull.act",   "pic.bmp",
										  "other.bmp", "A.OPT",    "a.opt",    "cockpit.lfd" };
	for (size_t i = 0; i < sizeof kFiles / sizeof kFiles[0]; ++i)
		XvtTest_WriteText(g_asset, kFiles[i], "x");
	char user[XVT_TEST_PATH_CAPACITY], temp[XVT_TEST_PATH_CAPACITY];
	XvtTest_Join(user, g_folder, "user");
	XvtTest_Join(temp, g_folder, "temp");
	AeronVfsConfig config = { 0 };
	config.asset_root = g_asset;
	config.resource_root = g_asset;
	config.user_root = user;
	config.temp_root = temp;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	XvtStorage_Bind(g_vfs);
}

static void RemoveRoots(void) {
	XvtStorage_Bind(NULL);
	AeronVfs_Destroy(g_vfs);
	XvtTest_RemoveTree(g_folder);
}

static void Fresh(void) {
	XvtRenderAssets_Shutdown();
	XvtRenderAssets_Init();
}

/* Exports into the test's snapshot, which first has no valid flight view and no types. */
static void Export(void) {
	memset(g_snapshot->types, 0, sizeof g_snapshot->types);
	g_snapshot->flight_valid = 0;
	g_snapshot->dropped_records = 0;
	XvtRenderAssets_Export(g_snapshot);
}

static int ListsOpt(uint64_t id) {
	for (uint32_t i = 0; i < g_snapshot->opt_asset_count; ++i)
		if (g_snapshot->opt_assets[i].id == id)
			return 1;
	return 0;
}

/* The exported texture entry for id, or NULL. */
static const XvtSnapTextureAsset* TextureEntry(uint64_t id) {
	for (uint32_t i = 0; i < g_snapshot->texture_asset_count; ++i)
		if (g_snapshot->texture_assets[i].id == id)
			return &g_snapshot->texture_assets[i];
	return NULL;
}

/* The exported image entry for id, or NULL. */
static const XvtSnapImageAsset* ImageEntry(uint64_t id) {
	for (uint32_t i = 0; i < g_snapshot->image_asset_count; ++i)
		if (g_snapshot->image_assets[i].id == id)
			return &g_snapshot->image_assets[i];
	return NULL;
}

static uint64_t RegisterBmp(const void* owner, const char* path) {
	return XvtRenderAssets_RegisterImage(owner, 0, path, XVT_IMAGE_BMP, 0, 1, 0, 0, 0);
}

/* Before Init nothing registers, nothing is freed or exported, and no colors are copied. */
static void CheckBeforeInit(void) {
	XVT_ASSERT_INT_EQ(RegisterBmp(&g_ownerA, "pic.bmp"), 0);
	XvtRenderAssets_RegisterOpt(3, "ship.opt");
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(3), 0);
	memset(g_snapshot, 0, sizeof *g_snapshot);
	g_snapshot->opt_asset_count = 77;
	g_snapshot->image_asset_count = 77;
	XvtRenderAssets_Export(g_snapshot);
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_count, 77);
	XVT_ASSERT_INT_EQ(g_snapshot->image_asset_count, 77);
	XVT_ASSERT_INT_EQ(g_snapshot->image_asset_generation, 0);
	XvtFrontendImageColors colors;
	XVT_ASSERT_INT_EQ(XvtRenderAssets_CopyFrontendColors(1, &colors), 0);
	XvtRenderAssets_RetireHandle(3);
	XvtRenderAssets_RetireImage(&g_ownerA);
	XvtRenderAssets_BeginFrame();
}

/* Init restarts ids and generations at 1 and registers the built-in default cursor, the first id. */
static void CheckInit(void) {
	Fresh();
	XVT_ASSERT_TRUE(XvtRenderAssets_DefaultCursor() != NULL);
	uint64_t cursor = XvtRenderAssets_ImageId(XvtRenderAssets_DefaultCursor());
	XVT_ASSERT_INT_EQ(cursor, 1);
	Export();
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_count, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->texture_asset_count, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->image_asset_count, 1);
	XVT_ASSERT_INT_EQ(g_snapshot->image_assets[0].kind, XVT_IMAGE_BUILTIN_CURSOR);
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_generation, 1);
	XVT_ASSERT_INT_EQ(g_snapshot->texture_asset_generation, 1);
	/* Registering the cursor changed the image sources. */
	XVT_ASSERT_TRUE(g_snapshot->image_asset_generation > 1);

	/* A second registry life starts the same way. */
	XvtRenderAssets_RegisterOpt(4, "ship.opt");
	Fresh();
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(XvtRenderAssets_DefaultCursor()), 1);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(4), 0);
}

/* The same file with the same parameters keeps its id; anything else under the same key retires the old
 * source and gets a new id. */
static void CheckRegisterRule(void) {
	Fresh();
	uint64_t first = RegisterBmp(&g_ownerA, "pic.bmp");
	XVT_ASSERT_TRUE(first != 0);
	XVT_ASSERT_INT_EQ(RegisterBmp(&g_ownerA, "pic.bmp"), first);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), first);

	uint64_t ids[8];
	ids[0] = first;
	ids[1] = RegisterBmp(&g_ownerA, "other.bmp");
	ids[2] = XvtRenderAssets_RegisterImage(&g_ownerA, 0, "other.bmp", XVT_IMAGE_PNL, 0, 1, 0, 0, 0);
	ids[3] = XvtRenderAssets_RegisterImage(&g_ownerA, 0, "other.bmp", XVT_IMAGE_PNL, 2, 1, 0, 0, 0);
	ids[4] = XvtRenderAssets_RegisterImage(&g_ownerA, 0, "other.bmp", XVT_IMAGE_PNL, 2, 5, 0, 0, 0);
	ids[5] = XvtRenderAssets_RegisterImage(&g_ownerA, 0, "other.bmp", XVT_IMAGE_PNL, 2, 5, 9, 0, 0);
	ids[6] = XvtRenderAssets_RegisterImage(&g_ownerA, 0, "other.bmp", XVT_IMAGE_PNL, 2, 5, 9, 4, 0);
	ids[7] = XvtRenderAssets_RegisterImage(&g_ownerA, 0, "other.bmp", XVT_IMAGE_PNL, 2, 5, 9, 4, 1);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_TRUE(ids[i] != 0);
		for (int j = 0; j < i; ++j)
			XVT_ASSERT_TRUE(ids[i] != ids[j]);
	}
	/* Only the newest is live under the key; the retired ones are still exported. */
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), ids[7]);
	Export();
	for (int i = 0; i < 8; ++i)
		XVT_ASSERT_TRUE(ImageEntry(ids[i]) != NULL);

	/* Another owner is another key. */
	uint64_t other = RegisterBmp(&g_ownerB, "pic.bmp");
	XVT_ASSERT_TRUE(other != 0 && other != first);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), ids[7]);

	/* With no owner the handle is the key; with neither there is no id. */
	uint64_t by_handle = XvtRenderAssets_RegisterImage(NULL, 21, "pic.bmp", XVT_IMAGE_BMP, 0, 1, 0, 0, 0);
	XVT_ASSERT_TRUE(by_handle != 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_RegisterImage(NULL, 21, "pic.bmp", XVT_IMAGE_BMP, 0, 1, 0, 0, 0),
					  by_handle);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(21), by_handle);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_RegisterImage(NULL, 0, "pic.bmp", XVT_IMAGE_BMP, 0, 1, 0, 0, 0), 0);
}

/* Resolved paths compare in any letter case: two names for one file in different case are one source. */
static void CheckPathCase(void) {
	Fresh();
	XvtRenderAssets_RegisterOpt(5, "A.OPT");
	uint64_t id = XvtRenderAssets_HandleId(5);
	XVT_ASSERT_TRUE(id != 0);
	XvtRenderAssets_RegisterOpt(5, "a.opt");
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(5), id);
	XvtRenderAssets_RegisterOpt(5, "ship.opt");
	XVT_ASSERT_TRUE(XvtRenderAssets_HandleId(5) != id);
}

/* A cockpit source also needs the same viewport to keep its id. */
static void CheckRegisterCockpit(void) {
	Fresh();
	XvtSnapRect viewport = { 0, 0, 640, 300 };
	uint64_t id = XvtRenderAssets_RegisterCockpit(&g_ownerA, 12, "cockpit.lfd", &viewport);
	XVT_ASSERT_TRUE(id != 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_RegisterCockpit(&g_ownerA, 12, "cockpit.lfd", &viewport), id);
	XvtSnapRect moved = { 0, 10, 640, 300 };
	uint64_t again = XvtRenderAssets_RegisterCockpit(&g_ownerA, 12, "cockpit.lfd", &moved);
	XVT_ASSERT_TRUE(again != 0 && again != id);
	Export();
	XVT_ASSERT_INT_EQ(ImageEntry(again)->kind, XVT_IMAGE_LFD);
}

/* Model and texture files are keyed by handle; handle 0 is ignored. HandleId never matches an
 * owner-keyed or retired source. */
static void CheckHandles(void) {
	Fresh();
	XvtRenderAssets_RegisterOpt(0, "ship.opt");
	XvtRenderAssets_RegisterTexture(0, "skin.act");
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(0), 0);
	Export();
	XVT_ASSERT_INT_EQ(g_snapshot->opt_asset_count, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->texture_asset_count, 0);

	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	XvtRenderAssets_RegisterTexture(8, "skin.act");
	uint64_t opt = XvtRenderAssets_HandleId(7);
	uint64_t texture = XvtRenderAssets_HandleId(8);
	XVT_ASSERT_TRUE(opt != 0 && texture != 0 && opt != texture);
	Export();
	XVT_ASSERT_TRUE(ListsOpt(opt));
	XVT_ASSERT_TRUE(TextureEntry(texture) != NULL);

	XVT_ASSERT_TRUE(XvtRenderAssets_RegisterImage(&g_ownerA, 30, "pic.bmp", XVT_IMAGE_BMP, 0, 1, 0, 0, 0) !=
					0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(30), 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(NULL), 0);

	XvtRenderAssets_RetireHandle(7);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(7), 0);
}

/* Each kind's generation rises when its sources change. */
static void CheckGenerations(void) {
	Fresh();
	Export();
	uint64_t opt = g_snapshot->opt_asset_generation;
	uint64_t texture = g_snapshot->texture_asset_generation;
	uint64_t image = g_snapshot->image_asset_generation;

	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	Export();
	XVT_ASSERT_TRUE(g_snapshot->opt_asset_generation > opt);
	opt = g_snapshot->opt_asset_generation;

	XvtRenderAssets_RegisterTexture(8, "skin.act");
	Export();
	XVT_ASSERT_TRUE(g_snapshot->texture_asset_generation > texture);
	texture = g_snapshot->texture_asset_generation;

	RegisterBmp(&g_ownerA, "pic.bmp");
	Export();
	XVT_ASSERT_TRUE(g_snapshot->image_asset_generation > image);
	image = g_snapshot->image_asset_generation;

	/* Retiring is a change too. */
	XvtRenderAssets_RetireHandle(7);
	Export();
	XVT_ASSERT_TRUE(g_snapshot->opt_asset_generation > opt);
	XvtRenderAssets_RetireImage(&g_ownerA);
	Export();
	XVT_ASSERT_TRUE(g_snapshot->image_asset_generation > image);
}

/* Bound model types take their asset ids at export without a valid flight view; a texture entry names
 * the first model type bound to it. */
static void CheckBindings(void) {
	Fresh();
	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	XvtRenderAssets_RegisterTexture(8, "skin.act");
	XvtRenderAssets_RegisterTexture(9, "hull.act");
	uint64_t opt = XvtRenderAssets_HandleId(7);
	uint64_t skin = XvtRenderAssets_HandleId(8);
	uint64_t hull = XvtRenderAssets_HandleId(9);
	XvtRenderAssets_BindType(3, 7);
	XvtRenderAssets_BindType(5, 8);
	XvtRenderAssets_BindType(12, 8);
	XvtRenderAssets_BindType(XVT_SNAP_TYPES, 7);
	Export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, opt);
	XVT_ASSERT_INT_EQ(g_snapshot->types[5].texture_asset_id, skin);
	XVT_ASSERT_INT_EQ(g_snapshot->types[12].texture_asset_id, skin);
	XVT_ASSERT_INT_EQ(TextureEntry(skin)->model_type, 5);
	XVT_ASSERT_INT_EQ(TextureEntry(hull)->model_type, UINT16_MAX);

	/* With a valid flight view the types keep what they hold. */
	memset(g_snapshot->types, 0, sizeof g_snapshot->types);
	g_snapshot->flight_valid = 1;
	XvtRenderAssets_Export(g_snapshot);
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);

	/* Handle 0 or an unknown handle unbinds. */
	XvtRenderAssets_BindType(3, 0);
	XvtRenderAssets_BindType(5, 444);
	Export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->types[5].texture_asset_id, 0);
	XVT_ASSERT_INT_EQ(TextureEntry(skin)->model_type, 12);
}

/* FreeHandle retires every source carrying the handle, owner-keyed ones included, and drops the type
 * bindings to them; retired sources are still exported. It does nothing for 0 or above 65535. */
static void CheckFreeHandle(void) {
	Fresh();
	XvtRenderAssets_RegisterOpt(9, "ship.opt");
	uint64_t opt = XvtRenderAssets_HandleId(9);
	uint64_t image = XvtRenderAssets_RegisterImage(&g_ownerA, 9, "pic.bmp", XVT_IMAGE_BMP, 0, 1, 0, 0, 0);
	XvtRenderAssets_BindType(3, 9);

	XvtRenderAssets_RetireHandle(0);
	XvtRenderAssets_RetireHandle(65536 + 9);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(9), opt);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), image);

	XvtRenderAssets_RetireHandle(9);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(9), 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), 0);
	Export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);
	XVT_ASSERT_TRUE(ListsOpt(opt));
	XVT_ASSERT_TRUE(ImageEntry(image) != NULL);

	/* Ids are never reused: a new source under the freed handle gets a new one. */
	XvtRenderAssets_RegisterOpt(9, "ship.opt");
	XVT_ASSERT_TRUE(XvtRenderAssets_HandleId(9) != 0 && XvtRenderAssets_HandleId(9) != opt);
}

/* FreeImage retires every source keyed by owner, and nothing for NULL. */
static void CheckFreeImage(void) {
	Fresh();
	uint64_t a = RegisterBmp(&g_ownerA, "pic.bmp");
	uint64_t b = RegisterBmp(&g_ownerB, "pic.bmp");
	XvtRenderAssets_RetireImage(NULL);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), a);
	XvtRenderAssets_RetireImage(&g_ownerA);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerB), b);
	Export();
	XVT_ASSERT_TRUE(ImageEntry(a) != NULL);
}

/* ClearMission clears the type bindings and raises the model and texture generations; it retires no
 * source. */
static void CheckClearMission(void) {
	Fresh();
	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	XvtRenderAssets_RegisterTexture(8, "skin.act");
	XvtRenderAssets_BindType(3, 7);
	XvtRenderAssets_BindType(4, 8);
	Export();
	uint64_t opt = g_snapshot->opt_asset_generation;
	uint64_t texture = g_snapshot->texture_asset_generation;
	XvtRenderAssets_ClearMission();
	Export();
	XVT_ASSERT_INT_EQ(g_snapshot->types[3].model_asset_id, 0);
	XVT_ASSERT_INT_EQ(g_snapshot->types[4].texture_asset_id, 0);
	XVT_ASSERT_TRUE(g_snapshot->opt_asset_generation > opt);
	XVT_ASSERT_TRUE(g_snapshot->texture_asset_generation > texture);
	XVT_ASSERT_TRUE(XvtRenderAssets_HandleId(7) != 0);
	XVT_ASSERT_TRUE(XvtRenderAssets_HandleId(8) != 0);
}

/* Shutdown clears the registry, which takes no new source until Init. */
static void CheckShutdown(void) {
	Fresh();
	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	uint64_t image = RegisterBmp(&g_ownerA, "pic.bmp");
	XVT_ASSERT_TRUE(image != 0);
	XvtRenderAssets_Shutdown();
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(7), 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(&g_ownerA), 0);
	XVT_ASSERT_INT_EQ(RegisterBmp(&g_ownerA, "pic.bmp"), 0);
	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	XVT_ASSERT_INT_EQ(XvtRenderAssets_HandleId(7), 0);
	XvtRenderAssets_Init();
	XVT_ASSERT_TRUE(RegisterBmp(&g_ownerA, "pic.bmp") != 0);
}

/* The frontend colors of a BMP source can be copied, retired or not; other ids give 0. */
static void CheckFrontendColors(void) {
	Fresh();
	static ImageResource image;
	memset(&image, 0, sizeof image);
	for (int i = 0; i < 256; ++i)
		image.colorLUT[i] = i * 37;
	XvtRenderAssets_RegisterFrontendImage(&image, "pic.bmp", 0, 1);
	uint64_t id = XvtRenderAssets_ImageId(&image);
	XVT_ASSERT_TRUE(id != 0);

	XvtFrontendImageColors colors;
	memset(&colors, 0, sizeof colors);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_CopyFrontendColors(id, &colors), 1);
	for (int i = 0; i < 256; ++i)
		XVT_ASSERT_INT_EQ(colors.color_lut[i], image.colorLUT[i]);
	XVT_ASSERT_INT_EQ(colors.pixel_format_555, 1);

	XvtRenderAssets_RetireImage(&image);
	memset(&colors, 0, sizeof colors);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_CopyFrontendColors(id, &colors), 1);
	XVT_ASSERT_INT_EQ(colors.color_lut[5], image.colorLUT[5]);

	XVT_ASSERT_INT_EQ(XvtRenderAssets_CopyFrontendColors(0, &colors), 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_CopyFrontendColors(id, NULL), 0);
	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	XVT_ASSERT_INT_EQ(XvtRenderAssets_CopyFrontendColors(XvtRenderAssets_HandleId(7), &colors), 0);
	uint64_t panel = XvtRenderAssets_RegisterImage(&g_ownerB, 0, "pic.bmp", XVT_IMAGE_PNL, 0, 1, 0, 0, 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_CopyFrontendColors(panel, &colors), 0);
}

/* Export stamps the flight palette, and does nothing for NULL. */
static void CheckExportPalette(void) {
	Fresh();
	for (int i = 0; i < 256; ++i)
		g_swPalette[i] = (RgbTriplet) { (uint8_t)(i & 63), (uint8_t)((i * 3) & 63), (uint8_t)((i * 7) & 63) };
	Export();
	for (unsigned i = 0; i < 256; ++i)
		XVT_ASSERT_INT_EQ(g_snapshot->flight_palette_argb[i], XvtRenderDraw_Color(i));
	XvtRenderAssets_Export(NULL);
}

/* A freed source stays exported until the renderer has consumed the last export and neither the current
 * nor the previous snapshot uses it. The render snapshot runs here as the game runs it: its Init starts
 * the registry, each frame's BeginFrame runs the registry's, and each Commit exports. */
static void CheckRetiredLifetime(void) {
	XvtRenderAssets_Shutdown();
	XvtRenderSnapshot_Shutdown();
	XvtRenderSnapshot_Init();
	XvtRenderCapture_BeginMission();
	XvtRenderAssets_RegisterOpt(7, "ship.opt");
	uint64_t id = XvtRenderAssets_HandleId(7);
	XVT_ASSERT_TRUE(id != 0);

	/* Frame 1 publishes a flight view whose model type 3 uses the source. */
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot* writer = XvtRenderSnapshot_Writer();
	writer->flight_valid = 1;
	writer->types[3].model_asset_id = id;
	XvtRenderSnapshot_Commit(1, 1, 0);
	XvtRenderAssets_RetireHandle(7);
	/* From now on no view is published: later snapshots do not use the source. */
	XvtRenderCapture_EndMission();

	/* Frame 2: the export of frame 1 is not consumed yet, and frame 1 uses the source. */
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(2, 1, 0);
	const XvtRenderSnapshot* current = XvtRenderSnapshot_Current();
	int listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i)
		listed |= current->opt_assets[i].id == id;
	XVT_ASSERT_TRUE(listed);

	/* Frame 3: consumed, but the previous snapshot (frame 1) still uses it. */
	XvtRenderAssets_Consumed(current->snapshot_serial);
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(3, 1, 0);
	current = XvtRenderSnapshot_Current();
	listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i)
		listed |= current->opt_assets[i].id == id;
	XVT_ASSERT_TRUE(listed);

	/* Frame 4: neither snapshot uses it, but the export of frame 3 is not consumed. */
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(4, 1, 0);
	current = XvtRenderSnapshot_Current();
	listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i)
		listed |= current->opt_assets[i].id == id;
	XVT_ASSERT_TRUE(listed);

	/* Consuming an older export is not enough. */
	XvtRenderAssets_Consumed(current->snapshot_serial - 1);
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(5, 1, 0);
	current = XvtRenderSnapshot_Current();
	listed = 0;
	for (uint32_t i = 0; i < current->opt_asset_count; ++i)
		listed |= current->opt_assets[i].id == id;
	XVT_ASSERT_TRUE(listed);

	/* Frame 6: the last export is consumed and no snapshot uses it: it is gone. */
	XvtRenderAssets_Consumed(current->snapshot_serial);
	XvtRenderSnapshot_BeginFrame();
	XvtRenderSnapshot_Commit(6, 1, 0);
	current = XvtRenderSnapshot_Current();
	for (uint32_t i = 0; i < current->opt_asset_count; ++i)
		XVT_ASSERT_TRUE(current->opt_assets[i].id != id);
	XvtRenderSnapshot_Shutdown();
}

int main(void) {
	g_snapshot = calloc(1, sizeof *g_snapshot);
	XVT_ASSERT_TRUE(g_snapshot != NULL);
	MakeRoots();
	CheckBeforeInit();
	CheckInit();
	CheckRegisterRule();
	CheckPathCase();
	CheckRegisterCockpit();
	CheckHandles();
	CheckGenerations();
	CheckBindings();
	CheckFreeHandle();
	CheckFreeImage();
	CheckClearMission();
	CheckShutdown();
	CheckFrontendColors();
	CheckExportPalette();
	CheckRetiredLifetime();
	XvtRenderAssets_Shutdown();
	RemoveRoots();
	free(g_snapshot);
	return 0;
}
