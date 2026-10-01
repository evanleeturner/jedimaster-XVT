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
static AeronVfs* g_vfs;
static XvtCockpitDefinition* g_definition;
static XvtRenderSnapshot* g_snapshot;
static uint8_t g_microFont[8], g_smallFont[8], g_mediumFont[8];
static uint8_t* g_iconsA[5];
static uint8_t* g_iconsB[3];

static void MakeRoots(void) {
	char asset[XVT_TEST_PATH_CAPACITY], user[XVT_TEST_PATH_CAPACITY], temp[XVT_TEST_PATH_CAPACITY];
	XvtTest_MakeFolder(g_folder);
	XvtTest_MakeSubfolder(g_folder, "asset");
	XvtTest_MakeSubfolder(g_folder, "user");
	XvtTest_MakeSubfolder(g_folder, "temp");
	XvtTest_Join(asset, g_folder, "asset");
	XvtTest_Join(user, g_folder, "user");
	XvtTest_Join(temp, g_folder, "temp");
	static const char* const kFiles[] = { "MICRO32.FNT", "MICRO48.FNT", "MICRO64.FNT",
										  "panel.pnl",   "panel2.pnl",  "icons.ico",
										  "icons2.ico",  "cockpit.lfd", "cockpit2.lfd" };
	for (size_t i = 0; i < sizeof kFiles / sizeof kFiles[0]; ++i)
		XvtTest_WriteText(asset, kFiles[i], "x");
	AeronVfsConfig config = { 0 };
	config.asset_root = asset;
	config.resource_root = asset;
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

/* A live HUD layout with a different value in every field, cockpit resources without handles, fonts and
 * handles for each, the 640x480 resolution, and a fresh registry. */
static void Fresh(void) {
	for (unsigned i = 0; i < HUD_INSTRUMENT_COUNT; ++i)
		g_hudElementLayouts[i] =
			(HudElementLayout) { (uint16_t)i,         (uint16_t)(1000 + i), (uint16_t)(i % 7),
								 (uint16_t)(i % 250), (uint16_t)(2 * i),    (int16_t)-i };
	for (unsigned i = 0; i < 28; ++i) {
		HudCockpitResourceDescriptor* d = &g_hudCockpitResourceDescriptors[i];
		memset(d, 0, sizeof *d);
		d->enabled = (uint8_t)(i % 2);
		memcpy(d->lfdName, "COCKPITxx", 9);
		d->lfdName[7] = (char)('A' + i);
		memcpy(d->displayName, "Display name ...", 16);
		d->displayName[13] = (char)('a' + i);
		d->viewportOriginX = (uint16_t)i;
		d->viewportOriginY = (uint16_t)(2 * i);
		d->viewportWidth = (uint16_t)(300 + i);
		d->viewportHeight = (uint16_t)(200 + i);
		d->projectionOffsetY = (int16_t)(i - 14);
		g_hudCockpitResources[i].memoryHandle = 0;
	}
	memcpy(g_hudPanelSpriteFileInfo.baseName, "PANELBASE", 9);
	g_hudPanelSpriteFileInfo.spriteCount = 40;
	g_hudPanelSpriteFileInfo.spriteCountAddend = 3;
	for (unsigned i = 0; i < 480; ++i) {
		g_hudViewportSpanMask0[i] = (uint8_t)i;
		g_hudViewportSpanMask1[i] = (uint8_t)(i * 3);
		g_hudViewportSpanMask2[i] = (uint8_t)(i * 5);
	}
	g_flightResolutionMode = FLIGHT_RESOLUTION_640X480;
	g_flightFontMicroSw = g_microFont;
	g_flightFontSmallSw = g_smallFont;
	g_flightFontMediumSw = g_mediumFont;
	g_flightMicroFontHandle = 41;
	g_flightTinyFontHandle = 42;
	g_flightSmallFontHandle = 43;
	g_flightLog1BufferHandle = 66;
	g_hudPanelSpriteDataHandle = 70;
	g_flightIconFramesHandle = 71;
	XvtRenderAssets_Shutdown();
	XvtRenderAssets_Init();
}

static const XvtCockpitDefinition* Definition(void) {
	memset(g_definition, 0xEE, sizeof *g_definition);
	XvtRenderCockpit_CaptureDefinition(g_definition);
	return g_definition;
}

static int AllZero(const void* data, size_t size) {
	const uint8_t* bytes = data;
	for (size_t i = 0; i < size; ++i)
		if (bytes[i])
			return 0;
	return 1;
}

static void CheckElements(const XvtCockpitDefinition* d) {
	for (unsigned i = 0; i < HUD_INSTRUMENT_COUNT; ++i) {
		const HudElementLayout* live = &g_hudElementLayouts[i];
		const XvtSnapHudElement* copy = &d->layout.elements[i];
		XVT_ASSERT_INT_EQ(copy->x, live->x);
		XVT_ASSERT_INT_EQ(copy->y, live->y);
		XVT_ASSERT_INT_EQ(copy->selector, live->selector);
		XVT_ASSERT_INT_EQ(copy->color_index, live->colorIndex);
		XVT_ASSERT_INT_EQ(copy->clip_width, live->clipWidth);
		/* Element 127's warning timer is zeroed in the copy. */
		XVT_ASSERT_INT_EQ(copy->clip_height_or_foreground, i == 127 ? 0 : live->clipHeightOrForegroundColor);
	}
}

/* CaptureCockpit copies the descriptors, panel sprite info and span masks 0 and 1, and marks the layout
 * valid; CaptureDefinition then gives the live element values. Both captures raise the resource
 * generation. */
static void CheckCaptureCockpit(void) {
	Fresh();
	uint64_t generation = Definition()->resource_generation;
	XvtRenderAssets_CaptureCockpit(0);
	const XvtCockpitDefinition* d = Definition();
	XVT_ASSERT_TRUE(d->resource_generation > generation);
	generation = d->resource_generation;
	XVT_ASSERT_INT_EQ(d->layout.valid, 1);
	CheckElements(d);
	for (unsigned i = 0; i < 28; ++i) {
		const HudCockpitResourceDescriptor* live = &g_hudCockpitResourceDescriptors[i];
		const XvtSnapCockpitDescriptor* copy = &d->layout.descriptors[i];
		XVT_ASSERT_INT_EQ(copy->enabled, live->enabled);
		XVT_ASSERT_INT_EQ(memcmp(copy->lfd_name, live->lfdName, 9), 0);
		XVT_ASSERT_INT_EQ(memcmp(copy->display_name, live->displayName, 16), 0);
		XVT_ASSERT_INT_EQ(copy->viewport.x, live->viewportOriginX);
		XVT_ASSERT_INT_EQ(copy->viewport.y, live->viewportOriginY);
		XVT_ASSERT_INT_EQ(copy->viewport.width, live->viewportWidth);
		XVT_ASSERT_INT_EQ(copy->viewport.height, live->viewportHeight);
		XVT_ASSERT_INT_EQ(copy->projection_offset_y, live->projectionOffsetY);
	}
	XVT_ASSERT_INT_EQ(memcmp(d->layout.panel_basename, "PANELBASE", 9), 0);
	XVT_ASSERT_INT_EQ(d->layout.sprite_count, 40);
	XVT_ASSERT_INT_EQ(d->layout.sprite_count_addend, 3);
	for (int mask = 0; mask < 2; ++mask) {
		const uint8_t* live = mask ? g_hudViewportSpanMask1 : g_hudViewportSpanMask0;
		XVT_ASSERT_TRUE(d->layout.mask_bytes[mask] > 0 && d->layout.mask_bytes[mask] <= 480);
		XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[mask], live, d->layout.mask_bytes[mask]), 0);
	}

	/* The auxiliary capture takes span mask 2 instead, and leaves the descriptors, panel info and masks 0
	 * and 1 as they were. */
	XvtCockpitDefinition before = *d;
	for (unsigned i = 0; i < 28; ++i)
		g_hudCockpitResourceDescriptors[i].enabled ^= 1;
	memcpy(g_hudPanelSpriteFileInfo.baseName, "OTHERNAME", 9);
	for (unsigned i = 0; i < 480; ++i) {
		g_hudViewportSpanMask0[i] ^= 0xFF;
		g_hudViewportSpanMask1[i] ^= 0xFF;
	}
	XvtRenderAssets_CaptureCockpit(1);
	d = Definition();
	XVT_ASSERT_TRUE(d->resource_generation > generation);
	XVT_ASSERT_INT_EQ(d->layout.valid, 1);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.descriptors, before.layout.descriptors, sizeof d->layout.descriptors),
					  0);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.panel_basename, "PANELBASE", 9), 0);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[0], before.layout.masks[0], sizeof d->layout.masks[0]), 0);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[1], before.layout.masks[1], sizeof d->layout.masks[1]), 0);
	XVT_ASSERT_TRUE(d->layout.mask_bytes[2] > 0 && d->layout.mask_bytes[2] <= 480);
	XVT_ASSERT_INT_EQ(memcmp(d->layout.masks[2], g_hudViewportSpanMask2, d->layout.mask_bytes[2]), 0);
}

/* The copy has layout generation 0 and element 127's warning timer zeroed, so equal content compares
 * equal; a valid layout takes the live element values first; the beam and shield tables are copied. */
static void CheckDefinitionCopy(void) {
	Fresh();
	XvtRenderAssets_CaptureCockpit(0);
	XvtCockpitDefinition* first = malloc(sizeof *first);
	XVT_ASSERT_TRUE(first != NULL);
	*first = *Definition();
	XVT_ASSERT_INT_EQ(first->layout.generation, 0);
	g_hudElementLayouts[127].clipHeightOrForegroundColor = 99;
	XVT_ASSERT_INT_EQ(memcmp(Definition(), first, sizeof *first), 0);

	g_hudElementLayouts[5].x = 4321;
	g_hudElementLayouts[400].selector = 77;
	const XvtCockpitDefinition* d = Definition();
	XVT_ASSERT_INT_EQ(d->layout.elements[5].x, 4321);
	XVT_ASSERT_INT_EQ(d->layout.elements[400].selector, 77);
	XVT_ASSERT_INT_EQ(memcmp(d->beam_fades, g_hudBeamSegmentFadeByChargeStep, sizeof d->beam_fades), 0);
	XVT_ASSERT_INT_EQ(memcmp(d->shield_colors, g_hudShieldColors, sizeof d->shield_colors), 0);
	free(first);
}

/* Reset clears the layout, leaving it invalid, and every panel, icon and LFD binding; it raises the
 * resource generation. */
static void CheckReset(void) {
	Fresh();
	XvtRenderAssets_CaptureCockpit(0);
	XvtRenderAssets_RegisterPanel("panel.pnl", 0, 4, 0);
	XvtRenderAssets_RegisterIcons("icons.ico", g_iconsA, 3);
	XvtRenderAssets_RegisterLfd("cockpit.lfd", g_hudCockpitResources[2].entries);
	const XvtCockpitDefinition* d = Definition();
	XVT_ASSERT_TRUE(d->layout.valid && d->panels[0].asset_id && d->layout.descriptors[2].lfd_asset_id);
	XVT_ASSERT_TRUE(XvtRenderAssets_MapIconFrame(0, NULL) != 0);
	uint64_t generation = d->resource_generation;

	XvtRenderCockpit_Reset();
	d = Definition();
	XVT_ASSERT_TRUE(d->resource_generation > generation);
	XVT_ASSERT_TRUE(AllZero(&d->layout, sizeof d->layout));
	for (unsigned i = 0; i < XVT_HUD_PANEL_BINDINGS; ++i)
		XVT_ASSERT_INT_EQ(d->panels[i].asset_id, 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(0, NULL), 0);
}

/* RegisterPanel binds slot first + i to frame skip + i, and sets the layout's panel asset when first is
 * 0; a count of 0 or a range that leaves the 265 slots does nothing. */
static void CheckRegisterPanel(void) {
	Fresh();
	XvtRenderAssets_RegisterPanel("panel.pnl", 10, 3, 5);
	const XvtCockpitDefinition* d = Definition();
	uint64_t id = d->panels[10].asset_id;
	XVT_ASSERT_TRUE(id != 0);
	for (unsigned i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(d->panels[10 + i].asset_id, id);
		XVT_ASSERT_INT_EQ(d->panels[10 + i].frame, 5 + i);
	}
	XVT_ASSERT_INT_EQ(d->panels[9].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->panels[13].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->layout.panel_asset_id, 0);

	XvtRenderAssets_RegisterPanel("panel2.pnl", 0, 2, 1);
	d = Definition();
	uint64_t base = d->panels[0].asset_id;
	XVT_ASSERT_TRUE(base != 0 && base != id);
	XVT_ASSERT_INT_EQ(d->panels[0].frame, 1);
	XVT_ASSERT_INT_EQ(d->panels[1].frame, 2);
	XVT_ASSERT_INT_EQ(d->layout.panel_asset_id, base);

	XvtRenderAssets_RegisterPanel("panel.pnl", 20, 0, 0);
	XvtRenderAssets_RegisterPanel("panel.pnl", 265, 1, 0);
	XvtRenderAssets_RegisterPanel("panel.pnl", 260, 6, 0);
	d = Definition();
	for (unsigned i = 20; i < XVT_HUD_PANEL_BINDINGS; ++i)
		XVT_ASSERT_INT_EQ(d->panels[i].asset_id, 0);
	XvtRenderAssets_RegisterPanel("panel.pnl", 260, 5, 0);
	d = Definition();
	for (unsigned i = 260; i < XVT_HUD_PANEL_BINDINGS; ++i)
		XVT_ASSERT_TRUE(d->panels[i].asset_id != 0);
}

/* RegisterLfd registers the resource's file with its viewport and binds it to the resource's descriptor;
 * a resource without a handle registers under the flight log buffer's handle; entries that are no
 * resource's do nothing. */
static void CheckRegisterLfd(void) {
	Fresh();
	g_hudCockpitResources[4].memoryHandle = 55;
	XvtRenderAssets_RegisterLfd("cockpit.lfd", g_hudCockpitResources[4].entries);
	uint64_t id = XvtRenderAssets_ImageId(g_hudCockpitResources[4].entries);
	XVT_ASSERT_TRUE(id != 0);
	XVT_ASSERT_INT_EQ(Definition()->layout.descriptors[4].lfd_asset_id, id);

	memset(g_snapshot, 0, sizeof *g_snapshot);
	XvtRenderAssets_Export(g_snapshot);
	const XvtSnapImageAsset* entry = NULL;
	for (uint32_t i = 0; i < g_snapshot->image_asset_count; ++i)
		if (g_snapshot->image_assets[i].id == id)
			entry = &g_snapshot->image_assets[i];
	XVT_ASSERT_TRUE(entry != NULL);
	XVT_ASSERT_INT_EQ(entry->kind, XVT_IMAGE_LFD);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.x, 4);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.y, 8);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.width, 304);
	XVT_ASSERT_INT_EQ(entry->cockpit_viewport.height, 204);

	/* Resource 6 has no handle: freeing the flight log buffer's handle retires it, and drops its binding. */
	XvtRenderAssets_RegisterLfd("cockpit2.lfd", g_hudCockpitResources[6].entries);
	uint64_t unhandled = XvtRenderAssets_ImageId(g_hudCockpitResources[6].entries);
	XVT_ASSERT_TRUE(unhandled != 0 && unhandled != id);
	XvtRenderAssets_FreeHandle(55);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(g_hudCockpitResources[6].entries), unhandled);
	XvtRenderAssets_FreeHandle(g_flightLog1BufferHandle);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(g_hudCockpitResources[6].entries), 0);
	XVT_ASSERT_INT_EQ(Definition()->layout.descriptors[6].lfd_asset_id, 0);

	static uint8_t* other[3];
	XvtCockpitDefinition* before = malloc(sizeof *before);
	XVT_ASSERT_TRUE(before != NULL);
	*before = *Definition();
	XvtRenderAssets_RegisterLfd("cockpit.lfd", other);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(other), 0);
	XVT_ASSERT_INT_EQ(memcmp(Definition()->layout.descriptors, before->layout.descriptors,
							 sizeof before->layout.descriptors),
					  0);
	free(before);
}

/* RegisterIcons binds icon i to frame i below count; icons past count keep a binding to another icon
 * file; a count of 0 does nothing. MapIconFrame gives 0 and frame 0 for an unbound or out-of-range
 * index. */
static void CheckRegisterIcons(void) {
	Fresh();
	uint32_t frame = 99;
	XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(0, &frame), 0);
	XVT_ASSERT_INT_EQ(frame, 0);

	XvtRenderAssets_RegisterIcons("icons.ico", g_iconsA, 5);
	uint64_t a = XvtRenderAssets_MapIconFrame(0, NULL);
	XVT_ASSERT_TRUE(a != 0);
	for (uint32_t i = 0; i < 5; ++i) {
		XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(i, &frame), a);
		XVT_ASSERT_INT_EQ(frame, i);
	}
	frame = 99;
	XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(5, &frame), 0);
	XVT_ASSERT_INT_EQ(frame, 0);
	frame = 99;
	XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(XVT_SNAP_MAP_ICON_FRAMES, &frame), 0);
	XVT_ASSERT_INT_EQ(frame, 0);

	XvtRenderAssets_RegisterIcons("icons2.ico", g_iconsB, 3);
	uint64_t b = XvtRenderAssets_MapIconFrame(0, NULL);
	XVT_ASSERT_TRUE(b != 0 && b != a);
	for (uint32_t i = 0; i < 5; ++i) {
		XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(i, &frame), i < 3 ? b : a);
		XVT_ASSERT_INT_EQ(frame, i);
	}

	XvtRenderAssets_RegisterIcons("icons.ico", g_iconsA, 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(0, NULL), b);
}

/* Forget drops every panel, icon and LFD binding to id, and the layout's panel asset; dropping a panel or
 * LFD binding raises the resource generation. */
static void CheckForget(void) {
	Fresh();
	XvtRenderAssets_RegisterPanel("panel.pnl", 0, 2, 0);
	XvtRenderAssets_RegisterIcons("icons.ico", g_iconsA, 2);
	XvtRenderAssets_RegisterLfd("cockpit.lfd", g_hudCockpitResources[3].entries);
	const XvtCockpitDefinition* d = Definition();
	uint64_t panel = d->panels[0].asset_id;
	uint64_t lfd = d->layout.descriptors[3].lfd_asset_id;
	uint64_t icons = XvtRenderAssets_MapIconFrame(1, NULL);
	XVT_ASSERT_TRUE(panel != 0 && lfd != 0 && icons != 0);

	XvtRenderCockpit_Forget(icons);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(0, NULL), 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_MapIconFrame(1, NULL), 0);
	d = Definition();
	XVT_ASSERT_INT_EQ(d->panels[0].asset_id, panel);
	XVT_ASSERT_INT_EQ(d->layout.descriptors[3].lfd_asset_id, lfd);

	uint64_t generation = d->resource_generation;
	XvtRenderCockpit_Forget(panel);
	d = Definition();
	XVT_ASSERT_INT_EQ(d->panels[0].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->panels[1].asset_id, 0);
	XVT_ASSERT_INT_EQ(d->layout.panel_asset_id, 0);
	XVT_ASSERT_TRUE(d->resource_generation > generation);

	generation = d->resource_generation;
	XvtRenderCockpit_Forget(lfd);
	d = Definition();
	XVT_ASSERT_INT_EQ(d->layout.descriptors[3].lfd_asset_id, 0);
	XVT_ASSERT_TRUE(d->resource_generation > generation);
}

/* RegisterFlightFonts registers the micro and small fonts, and the medium one except at 320x240; the
 * fonts the definition names are registered ones. */
static void CheckFlightFonts(void) {
	Fresh();
	XvtRenderAssets_RegisterFlightFonts();
	uint64_t micro = XvtRenderAssets_ImageId(g_microFont);
	uint64_t small = XvtRenderAssets_ImageId(g_smallFont);
	uint64_t medium = XvtRenderAssets_ImageId(g_mediumFont);
	XVT_ASSERT_TRUE(micro != 0 && small != 0 && medium != 0);
	const XvtCockpitDefinition* d = Definition();
	for (size_t font = 0; font < sizeof d->fonts / sizeof d->fonts[0]; ++font) {
		uint64_t id = d->fonts[font].asset_id;
		XVT_ASSERT_TRUE(id == micro || id == small || id == medium);
	}

	Fresh();
	g_flightResolutionMode = FLIGHT_RESOLUTION_320X240;
	XvtRenderAssets_RegisterFlightFonts();
	micro = XvtRenderAssets_ImageId(g_microFont);
	small = XvtRenderAssets_ImageId(g_smallFont);
	XVT_ASSERT_TRUE(micro != 0 && small != 0);
	XVT_ASSERT_INT_EQ(XvtRenderAssets_ImageId(g_mediumFont), 0);
	d = Definition();
	for (size_t font = 0; font < sizeof d->fonts / sizeof d->fonts[0]; ++font) {
		uint64_t id = d->fonts[font].asset_id;
		XVT_ASSERT_TRUE(id == micro || id == small);
	}
}

int main(void) {
	g_definition = malloc(sizeof *g_definition);
	g_snapshot = calloc(1, sizeof *g_snapshot);
	XVT_ASSERT_TRUE(g_definition != NULL && g_snapshot != NULL);
	MakeRoots();
	CheckCaptureCockpit();
	CheckDefinitionCopy();
	CheckReset();
	CheckRegisterPanel();
	CheckRegisterLfd();
	CheckRegisterIcons();
	CheckForget();
	CheckFlightFonts();
	XvtRenderAssets_Shutdown();
	RemoveRoots();
	free(g_definition);
	free(g_snapshot);
	return 0;
}
