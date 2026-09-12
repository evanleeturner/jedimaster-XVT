#ifndef XVT_REMASTER_HUD_ASSETS_H
#define XVT_REMASTER_HUD_ASSETS_H

#include "aeron/scene/runtime_atlas.h"
#include "xvt_remaster/font.h"
#include "xvt_remaster/hud_layout.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

typedef struct XvtHudPreparedPart {
	uint16_t atlas_frame;
	uint8_t monochrome, color;
} XvtHudPreparedPart;

typedef struct XvtHudAssetSet {
	AeronRuntimeAtlas base, parts;
	AeronImageCoverage base_coverage;
	uint64_t base_asset_id;
	uint32_t palette[256];
	uint16_t part_count;
	XvtHudPartRequest requests[XVT_HUD_PART_CAPACITY];
	XvtHudPreparedPart bindings[XVT_HUD_PART_CAPACITY];
} XvtHudAssetSet;

/* Prepare the loaded view family during loading, after source synchronization.
 * Commit only after upload submission succeeds. Select never uploads or decodes.
 * Invalidate prepared draw lists before retiring their source-dependent groups. */
int XvtHudAssets_PrepareResources(AeronCommandBuffer* cmd, const XvtCockpitResources* resources);
int XvtHudAssets_HasResources(uint64_t generation);
int XvtHudAssets_Select(const XvtCockpitState* state, const XvtHudLayout* layout);
void XvtHudAssets_Retire(const XvtRenderSnapshot* snapshot);
void XvtHudAssets_Commit(void);
void XvtHudAssets_Abort(void);
void XvtHudAssets_Shutdown(void);
const XvtHudAssetSet* XvtHudAssets_Current(void);
/* True when loaded cockpit artwork differs from the requested preparation option. */
int XvtHudAssets_UnditherPending(int requested);
/* Fonts retain the existing source-generation ownership in the image cache. */
const XvtFontAtlas* XvtHudAssets_FindFont(uint64_t asset_id);

#endif
