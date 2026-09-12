#include "xvt_runtime/snapshot/render_cockpit_assets.h"
#include "aeron/aeron.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/render/flight_sw.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include <string.h>

static XvtSnapCockpitLayout g_layout;
static uint64_t g_layoutGeneration;
static uint64_t g_resourceGeneration;

typedef struct CockpitSourceBinding {
	uint64_t id;
	uint32_t frame;
} CockpitSourceBinding;

static CockpitSourceBinding g_panels[265], g_icons[XVT_SNAP_MAP_ICON_FRAMES], g_lfd[28];

void XvtRenderCockpit_Reset(void) {
	++g_resourceGeneration;
	memset(&g_layout, 0, sizeof g_layout);
	memset(g_panels, 0, sizeof g_panels);
	memset(g_icons, 0, sizeof g_icons);
	memset(g_lfd, 0, sizeof g_lfd);
	g_layout.generation = ++g_layoutGeneration;
}

void XvtRenderCockpit_Forget(uint64_t id) {
	int owned = 0;
	for (unsigned panel = 0; panel < 265; ++panel)
		owned |= g_panels[panel].id == id;
	for (unsigned view = 0; view < 28; ++view)
		owned |= g_lfd[view].id == id;
	if (owned)
		++g_resourceGeneration;
	for (unsigned i = 0; i < 265; ++i)
		if (g_panels[i].id == id)
			memset(&g_panels[i], 0, sizeof g_panels[i]);
	for (unsigned i = 0; i < XVT_SNAP_MAP_ICON_FRAMES; ++i)
		if (g_icons[i].id == id)
			memset(&g_icons[i], 0, sizeof g_icons[i]);
	for (unsigned i = 0; i < 28; ++i)
		if (g_lfd[i].id == id) {
			memset(&g_lfd[i], 0, sizeof g_lfd[i]);
			g_layout.descriptors[i].lfd_asset_id = 0;
			g_layout.generation = ++g_layoutGeneration;
		}
	if (g_layout.panel_asset_id == id)
		g_layout.panel_asset_id = 0;
}

static void RefreshLayoutBindings(void) {
	/* Layout selectors may be assigned during a panel-set rebuild. */
	if (g_layout.valid) {
		for (unsigned i = 0; i < XVT_SNAP_INSTRUMENTS; ++i) {
			const HudElementLayout* e = &g_hudElementLayouts[i];
			XvtSnapHudElement value = { e->x,          e->y,         e->selector,
										e->colorIndex, e->clipWidth, e->clipHeightOrForegroundColor };
			if (memcmp(&g_layout.elements[i], &value, sizeof value) != 0) {
				g_layout.elements[i] = value;
				g_layout.generation = ++g_layoutGeneration;
			}
		}
	}
}

void XvtRenderCockpit_CaptureDefinition(XvtCockpitDefinition* definition) {
	RefreshLayoutBindings();
	memset(definition, 0, sizeof *definition);
	definition->resource_generation = g_resourceGeneration;
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
		const void* font = g_flightFontMicroSw;
		unsigned height = 5, half_height = 3;
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480) {
			font = tier ? g_flightFontMediumSw : g_flightFontSmallSw;
			height = tier ? 10 : 8;
			half_height = tier ? 5 : 4;
		} else if (g_flightResolutionMode == FLIGHT_RESOLUTION_480X360 && tier) {
			font = g_flightFontSmallSw;
			height = 8;
			half_height = 4;
		}
		definition->fonts[tier].asset_id = XvtRenderAssets_ImageId(font);
		definition->fonts[tier].line_height = (uint16_t)height;
		definition->fonts[tier].half_height = (uint16_t)half_height;
	}
	memcpy(definition->beam_fades, g_hudBeamSegmentFadeByChargeStep, sizeof definition->beam_fades);
	memcpy(definition->shield_colors, g_hudShieldColors, sizeof definition->shield_colors);
	for (unsigned index = 0; index < 9; ++index) {
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480) {
			definition->beam_offsets[index][0] = definition->beam_offsets[index][1] =
				(int16_t)(3 * (8 - index));
		} else {
			const HudBeamSegmentOffset* offsets = g_flightResolutionMode == FLIGHT_RESOLUTION_480X360
													  ? g_hudBeamSegmentOffsets480x360
													  : g_hudBeamSegmentOffsets320x240;
			definition->beam_offsets[index][0] = offsets[index].x;
			definition->beam_offsets[index][1] = offsets[index].y;
		}
	}
}

void XvtRenderAssets_CaptureCockpit(int auxiliary) {
	++g_resourceGeneration;
	unsigned first = auxiliary ? 288 : 0;
	unsigned end = auxiliary ? 432 : 288;
	for (unsigned i = first; i < end; ++i) {
		const HudElementLayout* e = &g_hudElementLayouts[i];
		g_layout.elements[i] =
			(XvtSnapHudElement) { e->x,          e->y,         e->selector,
								  e->colorIndex, e->clipWidth, e->clipHeightOrForegroundColor };
	}
	uint16_t mask_size = g_flightResolutionMode == FLIGHT_RESOLUTION_320X240 ? 200 : 480;
	if (!auxiliary) {
		for (unsigned i = 0; i < 28; ++i) {
			const HudCockpitResourceDescriptor* d = &g_hudCockpitResourceDescriptors[i];
			XvtSnapCockpitDescriptor* out = &g_layout.descriptors[i];
			out->enabled = d->enabled;
			memcpy(out->lfd_name, d->lfdName, sizeof d->lfdName);
			out->lfd_name[9] = 0;
			memcpy(out->display_name, d->displayName, sizeof d->displayName);
			out->display_name[16] = 0;
			out->viewport =
				(XvtSnapRect) { d->viewportOriginX, d->viewportOriginY, d->viewportWidth, d->viewportHeight };
			out->projection_offset_y = d->projectionOffsetY;
		}
		memcpy(g_layout.panel_basename, g_hudPanelSpriteFileInfo.baseName, 9);
		g_layout.panel_basename[9] = 0;
		g_layout.sprite_count = g_hudPanelSpriteFileInfo.spriteCount;
		g_layout.sprite_count_addend = g_hudPanelSpriteFileInfo.spriteCountAddend;
		g_layout.mask_bytes[0] = g_layout.mask_bytes[1] = mask_size;
		memcpy(g_layout.masks[0], g_hudViewportSpanMask0, mask_size);
		memcpy(g_layout.masks[1], g_hudViewportSpanMask1, mask_size);
	} else {
		g_layout.mask_bytes[2] = mask_size;
		memcpy(g_layout.masks[2], g_hudViewportSpanMask2, mask_size);
	}
	g_layout.valid = 1;
	g_layout.generation = ++g_layoutGeneration;
}

void XvtRenderAssets_RegisterLfd(const char* path, uint8_t** entries) {
	for (unsigned i = 0; i < 28; ++i) {
		if (entries != g_hudCockpitResources[i].entries)
			continue;
		uint16_t handle = (uint16_t)g_hudCockpitResources[i].memoryHandle;
		if (!handle)
			handle = g_flightLog1BufferHandle;
		const HudCockpitResourceDescriptor* d = &g_hudCockpitResourceDescriptors[i];
		XvtSnapRect viewport = { d->viewportOriginX, d->viewportOriginY, d->viewportWidth,
								 d->viewportHeight };
		uint64_t id = XvtRenderAssets_RegisterCockpit(entries, handle, path, &viewport);
		if (g_lfd[i].id != id)
			++g_resourceGeneration;
		g_lfd[i] = (CockpitSourceBinding) { id, 0 };
		g_layout.descriptors[i].lfd_asset_id = id;
		g_layout.generation = ++g_layoutGeneration;
		break;
	}
}

void XvtRenderAssets_RegisterPanel(const char* path, uint16_t first, uint16_t count, uint16_t skip) {
	if (!count || first >= 265 || count > 265 - first)
		return;
	uint64_t id = XvtRenderAssets_RegisterImage(&g_panels[first], g_hudPanelSpriteDataHandle, path,
												XVT_IMAGE_PNL, skip, count, 0, 0, 0);
	if (g_panels[first].id != id)
		++g_resourceGeneration;
	for (unsigned i = 0; i < count; ++i)
		g_panels[first + i] = (CockpitSourceBinding) { id, skip + i };
	if (!first)
		g_layout.panel_asset_id = id;
	g_layout.generation = ++g_layoutGeneration;
}

void XvtRenderAssets_RegisterIcons(const char* path, uint8_t** frames, uint16_t count) {
	if (!count)
		return;
	if (count > XVT_SNAP_MAP_ICON_FRAMES) {
		Aeron_LogError("xvt.snapshot", "%s: %u icon frames exceed the original %u-frame allocation", path,
					   count, XVT_SNAP_MAP_ICON_FRAMES);
		Aeron_RequestFatalError("Map asset error", "Loaded map icons exceed the original frame capacity.");
		return;
	}
	uint64_t id = XvtRenderAssets_RegisterImage(frames, g_flightIconFramesHandle, path, XVT_IMAGE_ICO, 0,
												count, 0, 0, 0);
	for (unsigned i = 0; i < count; ++i)
		g_icons[i] = (CockpitSourceBinding) { id, i };
}

void XvtRenderAssets_RegisterFlightFonts(void) {
	XvtRenderAssets_RegisterImage(g_flightFontMicroSw, g_flightMicroFontHandle, "MICRO32.FNT",
								  XVT_IMAGE_MICRO_FNT, 0, 224, 0, 4, 0);
	XvtRenderAssets_RegisterImage(g_flightFontSmallSw, g_flightTinyFontHandle, "MICRO48.FNT",
								  XVT_IMAGE_MICRO_FNT, 0, 224, 0, 4, 0);
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240)
		XvtRenderAssets_RegisterImage(g_flightFontMediumSw, g_flightSmallFontHandle, "MICRO64.FNT",
									  XVT_IMAGE_MICRO_FNT, 0, 224, 0, 4, 0);
}

uint64_t XvtRenderAssets_MapIconFrame(unsigned index, uint32_t* frame) {
	if (frame)
		*frame = index < XVT_SNAP_MAP_ICON_FRAMES ? g_icons[index].frame : 0;
	return index < XVT_SNAP_MAP_ICON_FRAMES ? g_icons[index].id : 0;
}
