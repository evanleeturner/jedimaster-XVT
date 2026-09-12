#include "xvt_remaster/hud_assets.h"
#include "aeron/aeron.h"
#include "xvt_remaster/assets.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/undither.h"
#include <stdlib.h>
#include <string.h>

typedef struct CockpitAssetGroup {
	uint64_t generation;
	int undither;
	XvtHudAssetSet* views[28];
	uint8_t owns_base[28];
	XvtHudAssetSet parts;
	struct CockpitAssetGroup* next;
} CockpitAssetGroup;

static CockpitAssetGroup *g_groups, *g_pending;
static const XvtHudAssetSet* g_current;
static const XvtHudAssetSet g_empty;

typedef struct DecodedPart {
	XvtCockpitAssetBinding source;
	uint16_t key;
	uint8_t unshifted;
	AeronIndexedFrame bitmap;
} DecodedPart;

static void ReleaseGroup(CockpitAssetGroup* group) {
	if (!group)
		return;
	for (unsigned view = 0; view < 28; ++view) {
		XvtHudAssetSet* set = group->views[view];
		if (!set)
			continue;
		if (g_current == set)
			g_current = NULL;
		if (group->owns_base[view]) {
			Aeron_RuntimeAtlasRelease(&set->base);
			Aeron_ImageFreeCoverage(&set->base_coverage);
		}
		free(set);
	}
	Aeron_RuntimeAtlasRelease(&group->parts.parts);
	free(group);
}

void XvtHudAssets_Abort(void) {
	ReleaseGroup(g_pending);
	g_pending = NULL;
}

void XvtHudAssets_Commit(void) {
	if (!g_pending)
		return;
	g_pending->next = g_groups;
	g_groups = g_pending;
	g_pending = NULL;
}

void XvtHudAssets_Shutdown(void) {
	XvtHudAssets_Abort();
	while (g_groups) {
		CockpitAssetGroup* next = g_groups->next;
		ReleaseGroup(g_groups);
		g_groups = next;
	}
	g_current = NULL;
}

const XvtHudAssetSet* XvtHudAssets_Current(void) { return g_current; }

int XvtHudAssets_UnditherPending(int requested) {
	const XvtRenderSnapshot* snapshot = XvtRenderSnapshot_Current();
	if (!snapshot || !snapshot->cockpit_resources.valid)
		return 0;
	uint64_t generation = snapshot->cockpit_resources.definition.resource_generation;
	for (const CockpitAssetGroup* group = g_groups; group; group = group->next)
		if (group->generation == generation)
			return group->undither != requested;
	return 0;
}

const XvtFontAtlas* XvtHudAssets_FindFont(uint64_t id) { return XvtRemasterAssets_Font(id, 0); }

static int SameRequest(const XvtHudPartRequest* a, const XvtHudPartRequest* b) {
	return a->source.asset_id == b->source.asset_id && a->source.frame == b->source.frame &&
		   a->key == b->key && a->color_mode == b->color_mode &&
		   (a->color_mode != XVT_HUD_PART_INDEXED_FADE || (a->fade == b->fade && a->color == b->color));
}

static int MatchesAssetSet(const XvtHudAssetSet* set, const XvtCockpitState* state,
						   const XvtHudLayout* layout) {
	return set && set->base_asset_id == layout->base_asset_id && set->part_count == layout->part_count &&
		   !memcmp(set->palette, state->palette_argb, sizeof set->palette) &&
		   !memcmp(set->requests, layout->parts, layout->part_count * sizeof *layout->parts);
}

static uint8_t* ColorizeBitmap(const AeronIndexedFrame* bitmap, const XvtOriginal2d* source,
							   const uint32_t palette[256], const XvtHudPartRequest* request) {
	size_t count = (size_t)bitmap->width * bitmap->height;
	uint8_t* rgba = calloc(count, 4);
	if (!rgba)
		return NULL;
	for (size_t pixel = 0; pixel < count; ++pixel) {
		if (!bitmap->coverage[pixel])
			continue;
		unsigned index = bitmap->indices[pixel];
		if (request->color_mode == XVT_HUD_PART_MONOCHROME)
			memset(rgba + pixel * 4, 255, 3);
		else {
			if (request->color_mode == XVT_HUD_PART_INDEXED_FADE)
				index = (uint8_t)(index - request->fade + request->color);
			if (index >= source->palette_first && index < source->palette_first + source->palette_count)
				memcpy(rgba + pixel * 4, source->images.frames[0].palette[index], 3);
			else {
				rgba[pixel * 4] = (uint8_t)(palette[index] >> 16);
				rgba[pixel * 4 + 1] = (uint8_t)(palette[index] >> 8);
				rgba[pixel * 4 + 2] = (uint8_t)palette[index];
			}
		}
		rgba[pixel * 4 + 3] = bitmap->coverage[pixel];
	}
	return rgba;
}

static int DecodePart(const XvtOriginal2d* source, const XvtHudPartRequest* request,
					  AeronIndexedFrame* bitmap) {
	unsigned frame = request->source.frame;
	const AeronByteSpan* record =
		frame < source->panel_records.count ? &source->panel_records.bitmaps[frame] : NULL;
	if (!record || (record->size == 1 && record->data[0] == 0xff)) {
		/* Original loaders allow empty trailing panel records. */
		bitmap->width = bitmap->height = 1;
		bitmap->indices = calloc(1, 1);
		bitmap->coverage = calloc(1, 1);
		return bitmap->indices && bitmap->coverage;
	}
	AeronDecodeError error = { 0 };
	if (!AeronPnl_DecodeIndexed(record->data, record->size, request->color_mode != XVT_HUD_PART_ORIGINAL,
								request->key, bitmap, &error)) {
		Aeron_LogError("xvt.remaster", "cockpit bitmap %u: %s", frame, error.message);
		return 0;
	}
	/* Resident LFD coverage already includes the original main-world opening. */
	if (source->cockpit_mask && source->images.count) {
		const AeronIndexedFrame* base = source->images.frames;
		if (base->width != bitmap->width || base->height != bitmap->height)
			return 0;
		for (size_t pixel = 0; pixel < (size_t)bitmap->width * bitmap->height; ++pixel)
			if (!base->coverage[pixel])
				bitmap->coverage[pixel] = 0;
	}
	return 1;
}

static const AeronIndexedFrame* FindDecodedPart(DecodedPart* decoded, unsigned* count,
												const XvtHudPartRequest* request) {
	unsigned unshifted = request->color_mode != XVT_HUD_PART_ORIGINAL;
	for (unsigned index = 0; index < *count; ++index)
		if (decoded[index].source.asset_id == request->source.asset_id &&
			decoded[index].source.frame == request->source.frame && decoded[index].key == request->key &&
			decoded[index].unshifted == unshifted)
			return &decoded[index].bitmap;
	const XvtOriginal2d* source = XvtRemasterAssets_FindDecodedImage(request->source.asset_id);
	if (!source || !source->panel_bytes)
		return NULL;
	DecodedPart* part = &decoded[(*count)++];
	part->source = request->source;
	part->key = request->key;
	part->unshifted = (uint8_t)unshifted;
	return DecodePart(source, request, &part->bitmap) ? &part->bitmap : NULL;
}

static const AeronRuntimeAtlasOptions g_atlasOptions = { .format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
														 .color_space = AERON_COLOR_SPACE_SRGB,
														 .alpha_mode = AERON_IMAGE_ALPHA_STRAIGHT,
														 .generate_mips = true,
														 .debug_name = "xvt.cockpit.parts" };

static int BuildParts(AeronCommandBuffer* cmd, XvtHudAssetSet* set,
					  const uint32_t* palettes[XVT_HUD_PART_CAPACITY]) {
	if (!set->part_count)
		return 1;
	DecodedPart* decoded = calloc(set->part_count, sizeof *decoded);
	AeronRuntimeAtlasFrame* frames = calloc(set->part_count, sizeof *frames);
	if (!decoded || !frames) {
		free(decoded);
		free(frames);
		return 0;
	}
	unsigned decoded_count = 0, frame_count = 0;
	int ok = 1;
	for (unsigned index = 0; ok && index < set->part_count; ++index) {
		const XvtHudPartRequest* request = &set->requests[index];
		XvtHudPreparedPart* binding = &set->bindings[index];
		binding->monochrome = request->color_mode == XVT_HUD_PART_MONOCHROME;
		binding->color = request->color;
		unsigned previous = 0;
		for (; previous < index; ++previous)
			if (SameRequest(request, &set->requests[previous]) &&
				(request->color_mode == XVT_HUD_PART_MONOCHROME ||
				 !memcmp(palettes[index], palettes[previous], sizeof set->palette)))
				break;
		if (previous != index) {
			binding->atlas_frame = set->bindings[previous].atlas_frame;
			continue;
		}
		const AeronIndexedFrame* bitmap = FindDecodedPart(decoded, &decoded_count, request);
		const XvtOriginal2d* source = XvtRemasterAssets_FindDecodedImage(request->source.asset_id);
		if (!bitmap) {
			ok = 0;
			break;
		}
		uint8_t* rgba = ColorizeBitmap(bitmap, source, palettes[index], request);
		if (!rgba) {
			ok = 0;
			break;
		}
		if (XvtRemasterConfig_Effective()->cockpit_undither && !binding->monochrome) {
			uint8_t colors[256][4];
			for (unsigned entry = 0; entry < 256; ++entry) {
				unsigned color = request->color_mode == XVT_HUD_PART_INDEXED_FADE
									 ? (uint8_t)(entry - request->fade + request->color)
									 : entry;
				if (color >= source->palette_first && color < source->palette_first + source->palette_count)
					memcpy(colors[entry], source->images.frames[0].palette[color], 3);
				else {
					colors[entry][0] = (uint8_t)(palettes[index][color] >> 16);
					colors[entry][1] = (uint8_t)(palettes[index][color] >> 8);
					colors[entry][2] = (uint8_t)palettes[index][color];
				}
				colors[entry][3] = 255;
			}
			if (!XvtUndither_Apply(bitmap, colors, 256, rgba)) {
				Aeron_CommandBufferSetFailure(cmd, "cockpit sprite undithering failed");
				free(rgba);
				ok = 0;
				break;
			}
		}
		binding->atlas_frame = (uint16_t)frame_count;
		frames[frame_count] =
			(AeronRuntimeAtlasFrame) { rgba, bitmap->width, bitmap->height, (int32_t)frame_count, 0, 0 };
		++frame_count;
	}
	if (ok)
		ok = Aeron_RuntimeAtlasBuild(&set->parts, cmd, frames, (int)frame_count, &g_atlasOptions);
	for (unsigned index = 0; index < decoded_count; ++index)
		AeronIndexedFrame_Free(&decoded[index].bitmap);
	for (unsigned index = 0; index < frame_count; ++index)
		free((void*)frames[index].rgba);
	free(decoded);
	free(frames);
	return ok;
}

static int BuildBase(AeronCommandBuffer* cmd, XvtHudAssetSet* set) {
	if (!set->base_asset_id)
		return 1;
	const XvtOriginal2d* source = XvtRemasterAssets_FindDecodedImage(set->base_asset_id);
	if (!source)
		return 0;
	XvtHudPartRequest request = { .source = { set->base_asset_id, 0 }, .key = 0 };
	AeronIndexedFrame bitmap = { 0 };
	if (!DecodePart(source, &request, &bitmap)) {
		AeronIndexedFrame_Free(&bitmap);
		return 0;
	}
	uint8_t* rgba = ColorizeBitmap(&bitmap, source, set->palette, &request);
	if (rgba && XvtRemasterConfig_Effective()->cockpit_undither &&
		!XvtUndither_Apply(&bitmap, source->images.frames[0].palette, source->images.frames[0].palette_count,
						   rgba)) {
		Aeron_CommandBufferSetFailure(cmd, "cockpit background undithering failed");
		free(rgba);
		AeronIndexedFrame_Free(&bitmap);
		return 0;
	}
	AeronRuntimeAtlasFrame frame = { rgba, bitmap.width, bitmap.height, 0, 0, 0 };
	AeronRuntimeAtlasOptions options = g_atlasOptions;
	options.debug_name = "xvt.cockpit.base";
	int ok = rgba && Aeron_ImageBuildCoverageRgba8(rgba, bitmap.width, bitmap.height, &set->base_coverage) &&
			 Aeron_RuntimeAtlasBuild(&set->base, cmd, &frame, 1, &options);
	free(rgba);
	AeronIndexedFrame_Free(&bitmap);
	return ok;
}

static int SourceResident(const XvtRenderSnapshot* snapshot, uint64_t id) {
	if (!id)
		return 1;
	for (unsigned index = 0; index < snapshot->image_asset_count; ++index)
		if (snapshot->image_assets[index].id == id)
			return 1;
	return 0;
}

void XvtHudAssets_Retire(const XvtRenderSnapshot* snapshot) {
	CockpitAssetGroup** link = &g_groups;
	while (*link) {
		CockpitAssetGroup* group = *link;
		int resident = 1;
		for (unsigned view = 0; view < 28 && resident; ++view)
			if (group->views[view])
				resident = SourceResident(snapshot, group->views[view]->base_asset_id);
		for (unsigned part = 0; part < group->parts.part_count && resident; ++part)
			resident = SourceResident(snapshot, group->parts.requests[part].source.asset_id);
		if (resident)
			link = &group->next;
		else {
			*link = group->next;
			ReleaseGroup(group);
		}
	}
}

int XvtHudAssets_HasResources(uint64_t generation) {
	for (const CockpitAssetGroup* group = g_groups; group; group = group->next)
		if (group->generation == generation)
			return 1;
	return 0;
}

static int CompileLoadedView(const XvtCockpitResources* resources, unsigned view, XvtCockpitState* state,
							 XvtHudLayout* layout) {
	unsigned owner = view;
	unsigned enabled = resources->definition.layout.descriptors[view].enabled;
	if (enabled >= 0xc0)
		owner = enabled - 0xc0;
	else if (enabled >= 0x80)
		owner = enabled - 0x80;
	if (owner >= 28)
		return 0;
	state->view = resources->view;
	state->view.hud_state = (uint16_t)view;
	state->view.resource_descriptor = (uint16_t)owner;
	state->view.mirrored = enabled >= 0xc0;
	state->view.instrument_base = view == HUD_VIEW_HUD_ONLY     ? HUD_MAP_INSTRUMENT_BASE_INDEX
								  : view == HUD_VIEW_CRAFT_LIST ? HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX
																: HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
	memcpy(state->palette_argb, resources->palette, sizeof state->palette_argb);
	if (resources->definition.layout.descriptors[owner].lfd_asset_id)
		memcpy(state->palette_argb, resources->view_palette[owner], sizeof resources->view_palette[owner]);
	return XvtHudLayout_Compile(state, layout);
}

int XvtHudAssets_PrepareResources(AeronCommandBuffer* cmd, const XvtCockpitResources* resources) {
	if (!resources->valid || XvtHudAssets_HasResources(resources->definition.resource_generation))
		return 1;
	if (g_pending)
		return 0;
	g_pending = calloc(1, sizeof *g_pending);
	XvtCockpitState* state = calloc(1, sizeof *state);
	XvtHudLayout* layout = malloc(sizeof *layout);
	if (!g_pending || !state || !layout) {
		free(state);
		free(layout);
		return 0;
	}
	g_pending->generation = resources->definition.resource_generation;
	g_pending->undither = XvtRemasterConfig_Effective()->cockpit_undither;
	state->definition = resources->definition;
	state->systems.installed_hud_features = resources->installed_hud_features;
	const uint32_t* palettes[XVT_HUD_PART_CAPACITY];
	int ok = 1;
	for (unsigned view = 0; view < 28 && ok; ++view) {
		if (!resources->definition.layout.descriptors[view].enabled && view != HUD_VIEW_FULL_SCREEN)
			continue;
		if (!CompileLoadedView(resources, view, state, layout)) {
			ok = 0;
			break;
		}
		if (view != HUD_VIEW_FULL_SCREEN && !layout->base_asset_id)
			continue;
		XvtHudAssetSet* set = calloc(1, sizeof *set);
		if (!set) {
			ok = 0;
			break;
		}
		g_pending->views[view] = set;
		set->base_asset_id = layout->base_asset_id;
		set->part_count = layout->part_count;
		memcpy(set->palette, state->palette_argb, sizeof set->palette);
		memcpy(set->requests, layout->parts, layout->part_count * sizeof *layout->parts);
		unsigned owner = 0;
		for (; owner < view; ++owner)
			if (g_pending->views[owner] && g_pending->views[owner]->base_asset_id == set->base_asset_id)
				break;
		if (owner < view) {
			set->base = g_pending->views[owner]->base;
			set->base_coverage = g_pending->views[owner]->base_coverage;
		} else {
			g_pending->owns_base[view] = 1;
			if (!BuildBase(cmd, set)) {
				ok = 0;
				break;
			}
		}
		for (unsigned part = 0; part < set->part_count; ++part) {
			unsigned index = g_pending->parts.part_count;
			if (index == XVT_HUD_PART_CAPACITY) {
				ok = 0;
				break;
			}
			g_pending->parts.requests[index] = set->requests[part];
			palettes[index] = set->palette;
			set->bindings[part].atlas_frame = (uint16_t)index;
			++g_pending->parts.part_count;
		}
	}
	if (ok)
		ok = BuildParts(cmd, &g_pending->parts, palettes);
	if (ok)
		for (unsigned view = 0; view < 28; ++view) {
			XvtHudAssetSet* set = g_pending->views[view];
			if (!set)
				continue;
			for (unsigned part = 0; part < set->part_count; ++part)
				set->bindings[part] = g_pending->parts.bindings[set->bindings[part].atlas_frame];
			set->parts = g_pending->parts.parts;
		}
	free(state);
	free(layout);
	return ok;
}

int XvtHudAssets_Select(const XvtCockpitState* state, const XvtHudLayout* layout) {
	if ((!layout->base_asset_id && !layout->part_count) || !state->definition.layout.valid ||
		(!state->view.instruments_visible && (state->loading.visible || state->alert.active) &&
		 !layout->part_count)) {
		g_current = &g_empty;
		return 1;
	}
	for (const CockpitAssetGroup* group = g_groups; group; group = group->next)
		for (unsigned view = 0; view < 28; ++view)
			if (MatchesAssetSet(group->views[view], state, layout)) {
				g_current = group->views[view];
				return 1;
			}
	Aeron_LogError("xvt.remaster", "cockpit view %u has no prepared resident artwork", state->view.hud_state);
	return 0;
}
