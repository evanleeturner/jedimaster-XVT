#include "xvt_runtime/snapshot/render_assets.h"
#include "aeron/aeron.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/render/flight_palette.h"
#include "xvt_runtime/snapshot/render_cockpit_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include "xvt_runtime/storage/storage.h"
#include <stdio.h>
#include <string.h>

enum { SOURCE_OPT = 100, SOURCE_ACT, SOURCE_CAPACITY = XVT_SNAP_ASSETS * 2 + XVT_SNAP_TYPES };

typedef struct Source {
	XvtSnapImageAsset image;
	XvtFrontendImageColors frontend_colors;
	const void* owner;
	uint16_t handle;
	uint8_t dead;
} Source;

static Source g_sources[SOURCE_CAPACITY];
static uint64_t g_bindings[XVT_SNAP_TYPES];
static uint64_t g_nextId, g_optGeneration, g_textureGeneration, g_imageGeneration;
static uint64_t g_exportedTick, g_consumedTick;
static int g_initialized;

static void Changed(uint32_t kind) {
	if (kind == SOURCE_OPT)
		++g_optGeneration;
	else if (kind == SOURCE_ACT)
		++g_textureGeneration;
	else
		++g_imageGeneration;
}

static void Retire(Source* source) {
	if (!source->image.id || source->dead)
		return;
	source->dead = 1;
	Changed(source->image.kind);
	for (unsigned i = 0; i < XVT_SNAP_TYPES; ++i)
		if (g_bindings[i] == source->image.id)
			g_bindings[i] = 0;
	XvtRenderCockpit_Forget(source->image.id);
}

void XvtRenderAssets_Init(void) {
	memset(g_sources, 0, sizeof g_sources);
	memset(g_bindings, 0, sizeof g_bindings);
	g_nextId = 1;
	g_optGeneration = g_textureGeneration = g_imageGeneration = 1;
	g_exportedTick = g_consumedTick = UINT64_MAX;
	g_initialized = 1;
	XvtRenderCockpit_Reset();
	XvtRenderAssets_RegisterImage(g_defaultCursorBitmap, 0, "", XVT_IMAGE_BUILTIN_CURSOR, 0, 1, 0, 0, 0);
}

void XvtRenderAssets_Shutdown(void) {
	g_initialized = 0;
	memset(g_sources, 0, sizeof g_sources);
	XvtRenderCockpit_Reset();
}

static int SnapshotReferencesSource(const XvtRenderSnapshot* snapshot, uint64_t id) {
	if (!snapshot)
		return 0;
	if (snapshot->flight_valid) {
		for (unsigned type = 0; type < XVT_SNAP_TYPES; ++type)
			if (snapshot->types[type].model_asset_id == id || snapshot->types[type].texture_asset_id == id)
				return 1;
		if (snapshot->map.icon_asset_id == id || snapshot->map.font_asset_id == id)
			return 1;
	}
	const XvtCockpitState* cockpit = &snapshot->cockpit;
	if (cockpit->valid) {
		const XvtCockpitDefinition* definition = &cockpit->definition;
		for (unsigned panel = 0; panel < XVT_HUD_PANEL_BINDINGS; ++panel)
			if (definition->panels[panel].asset_id == id)
				return 1;
		for (unsigned view = 0; view < 28; ++view)
			if (definition->layout.descriptors[view].lfd_asset_id == id)
				return 1;
		for (unsigned tier = 0; tier < XVT_HUD_FONT_TIERS; ++tier)
			if (definition->fonts[tier].asset_id == id)
				return 1;
		for (unsigned glyph = 0; glyph < cockpit->page_content.glyph_count; ++glyph)
			if (cockpit->page_content.glyphs[glyph].font_asset_id == id)
				return 1;
		for (unsigned glyph = 0; glyph < cockpit->overlay_content.glyph_count; ++glyph)
			if (cockpit->overlay_content.glyphs[glyph].font_asset_id == id)
				return 1;
		if (cockpit->crt.valid && cockpit->crt.opt_asset_id == id)
			return 1;
	}
	return 0;
}

void XvtRenderAssets_BeginTick(void) {
	if (!g_initialized || g_consumedTick != g_exportedTick)
		return;
	/* A held presentation can still reference an original source after its
	 * classic handle is freed. Keep its descriptor until both snapshots retire it. */
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		if (g_sources[i].dead &&
			!SnapshotReferencesSource(XvtRenderSnapshot_Current(), g_sources[i].image.id) &&
			!SnapshotReferencesSource(XvtRenderSnapshot_Previous(), g_sources[i].image.id)) {
			Changed(g_sources[i].image.kind);
			memset(&g_sources[i], 0, sizeof g_sources[i]);
		}
	}
}

void XvtRenderAssets_Consumed(uint64_t tick) { g_consumedTick = tick; }

static uint64_t Register(const void* owner, uint16_t handle, const char* path, uint32_t kind, uint32_t first,
						 uint32_t count, uint16_t point_size, uint8_t row_bytes, int make_palette,
						 const XvtSnapRect* viewport) {
	if (!g_initialized || (!owner && !handle))
		return 0;
	char resolved[XVT_SNAP_PATH];
	if (kind == XVT_IMAGE_BUILTIN_CURSOR)
		resolved[0] = 0;
	else if (XvtStorage_ResolveAsset(path, resolved, sizeof resolved) != 1) {
		Aeron_LogError("xvt.snapshot", "cannot resolve loaded asset '%s'", path ? path : "");
		Aeron_RequestFatalRendererError("loaded asset path resolution");
		return 0;
	}
	/* The asset VFS is case-insensitive; use one cache key for spelling variants. */
	for (unsigned i = 0; resolved[i]; ++i)
		if (resolved[i] >= 'A' && resolved[i] <= 'Z')
			resolved[i] += 'a' - 'A';
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		Source* s = &g_sources[i];
		if (!s->image.id || s->dead)
			continue;
		if (owner ? s->owner == owner : (!s->owner && s->handle == handle)) {
			if (s->handle == handle && s->image.kind == kind && strcmp(s->image.path, resolved) == 0 &&
				s->image.first_record == first && s->image.record_count == count &&
				s->image.font_point_size == point_size && s->image.font_row_bytes == row_bytes &&
				s->image.make_palette == (make_palette != 0) &&
				(!viewport || memcmp(&s->image.cockpit_viewport, viewport, sizeof *viewport) == 0))
				return s->image.id;
			Retire(s);
		}
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		Source* s = &g_sources[i];
		if (s->image.id)
			continue;
		s->owner = owner;
		s->handle = handle;
		s->image.id = g_nextId++;
		s->image.kind = kind;
		snprintf(s->image.path, sizeof s->image.path, "%s", resolved);
		s->image.first_record = first;
		s->image.record_count = count;
		s->image.font_point_size = point_size;
		s->image.font_row_bytes = row_bytes;
		s->image.make_palette = make_palette != 0;
		if (viewport)
			s->image.cockpit_viewport = *viewport;
		Changed(kind);
		return s->image.id;
	}
	Aeron_RequestFatalRendererError("render source registry capacity exceeded");
	return 0;
}

uint64_t XvtRenderAssets_RegisterImage(const void* owner, uint16_t handle, const char* path,
									   XvtSnapImageKind kind, uint32_t first, uint32_t count,
									   uint16_t point_size, uint8_t row_bytes, int make_palette) {
	return Register(owner, handle, path, kind, first, count, point_size, row_bytes, make_palette, NULL);
}

void XvtRenderAssets_RegisterFrontendImage(const ImageResource* image, const char* path, int make_palette,
										   int pixel_format_555) {
	uint64_t id = Register(image, 0, path, XVT_IMAGE_BMP, 0, 1, 0, 0, make_palette, NULL);
	if (!id)
		return;
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		Source* source = &g_sources[i];
		if (source->image.id != id)
			continue;
		source->frontend_colors.pixel_format_555 = pixel_format_555 != 0;
		for (unsigned color = 0; color < 256; ++color)
			source->frontend_colors.color_lut[color] = (uint16_t)image->colorLUT[color];
		return;
	}
}

int XvtRenderAssets_CopyFrontendColors(uint64_t id, XvtFrontendImageColors* colors) {
	if (!g_initialized || !id || !colors)
		return 0;
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		const Source* source = &g_sources[i];
		if (source->image.id == id && source->image.kind == XVT_IMAGE_BMP) {
			/* Retired sources remain readable until their exported frame is consumed. */
			*colors = source->frontend_colors;
			return 1;
		}
	}
	return 0;
}

uint64_t XvtRenderAssets_RegisterCockpit(const void* owner, uint16_t handle, const char* path,
										 const XvtSnapRect* viewport) {
	return Register(owner, handle, path, XVT_IMAGE_LFD, 0, 1, 0, 0, 0, viewport);
}

void XvtRenderAssets_RegisterOpt(uint16_t handle, const char* path) {
	if (handle)
		Register(NULL, handle, path, SOURCE_OPT, 0, 0, 0, 0, 0, NULL);
}

void XvtRenderAssets_RegisterTexture(uint16_t handle, const char* path) {
	if (handle)
		Register(NULL, handle, path, SOURCE_ACT, 0, 0, 0, 0, 0, NULL);
}

uint64_t XvtRenderAssets_HandleId(uint16_t handle) {
	if (!handle)
		return 0;
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i)
		if (g_sources[i].image.id && !g_sources[i].dead && !g_sources[i].owner &&
			g_sources[i].handle == handle)
			return g_sources[i].image.id;
	return 0;
}

void XvtRenderAssets_BindType(uint16_t type, uint16_t handle) {
	if (type < XVT_SNAP_TYPES)
		g_bindings[type] = XvtRenderAssets_HandleId(handle);
}

void XvtRenderAssets_FreeHandle(unsigned int handle) {
	if (!g_initialized || !handle || handle > UINT16_MAX)
		return;
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i)
		if (g_sources[i].handle == handle)
			Retire(&g_sources[i]);
}

void XvtRenderAssets_FreeImage(const void* owner) {
	if (!g_initialized || !owner)
		return;
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i)
		if (g_sources[i].owner == owner)
			Retire(&g_sources[i]);
}

uint64_t XvtRenderAssets_ImageId(const void* owner) {
	if (!owner)
		return 0;
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i)
		if (!g_sources[i].dead && g_sources[i].owner == owner)
			return g_sources[i].image.id;
	return 0;
}

void XvtRenderAssets_ClearMission(void) {
	memset(g_bindings, 0, sizeof g_bindings);
	++g_optGeneration;
	++g_textureGeneration;
	XvtRenderCockpit_Reset();
}

const uint8_t* XvtRenderAssets_DefaultCursor(void) { return g_defaultCursorBitmap; }

void XvtRenderAssets_Export(XvtRenderSnapshot* snapshot) {
	if (!g_initialized || !snapshot)
		return;
	snapshot->opt_asset_count = snapshot->texture_asset_count = snapshot->image_asset_count = 0;
	if (!snapshot->flight_valid)
		for (unsigned i = 0; i < XVT_SNAP_TYPES; ++i)
			snapshot->types[i].model_asset_id = snapshot->types[i].texture_asset_id = 0;
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		const Source* s = &g_sources[i];
		if (!s->image.id)
			continue;
		if (s->image.kind == SOURCE_OPT && snapshot->opt_asset_count < XVT_SNAP_ASSETS) {
			XvtSnapOptAsset* out = &snapshot->opt_assets[snapshot->opt_asset_count++];
			out->id = s->image.id;
			out->public_handle = s->handle;
			memcpy(out->path, s->image.path, sizeof out->path);
		} else if (s->image.kind == SOURCE_ACT && snapshot->texture_asset_count < XVT_SNAP_TYPES) {
			XvtSnapTextureAsset* out = &snapshot->texture_assets[snapshot->texture_asset_count++];
			out->id = s->image.id;
			out->public_handle = s->handle;
			out->model_type = UINT16_MAX;
			memcpy(out->path, s->image.path, sizeof out->path);
			for (unsigned t = 0; t < XVT_SNAP_TYPES; ++t)
				if (g_bindings[t] == out->id) {
					out->model_type = (uint16_t)t;
					break;
				}
		} else if (s->image.kind < SOURCE_OPT && snapshot->image_asset_count < XVT_SNAP_ASSETS) {
			snapshot->image_assets[snapshot->image_asset_count++] = s->image;
		} else {
			++snapshot->dropped_records;
			Aeron_RequestFatalRendererError("snapshot asset capacity exceeded");
		}
		for (unsigned t = 0; t < XVT_SNAP_TYPES; ++t) {
			if (snapshot->flight_valid || g_bindings[t] != s->image.id)
				continue;
			if (s->image.kind == SOURCE_OPT)
				snapshot->types[t].model_asset_id = s->image.id;
			else if (s->image.kind == SOURCE_ACT)
				snapshot->types[t].texture_asset_id = s->image.id;
		}
	}
	snapshot->opt_asset_generation = g_optGeneration;
	snapshot->texture_asset_generation = g_textureGeneration;
	snapshot->image_asset_generation = g_imageGeneration;
	for (unsigned i = 0; i < 256; ++i)
		snapshot->flight_palette_argb[i] = XvtRenderDraw_Color(i);

	g_exportedTick = snapshot->tick_index;
}
