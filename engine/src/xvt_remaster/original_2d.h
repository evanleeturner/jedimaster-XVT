#ifndef XVT_REMASTER_ORIGINAL_2D_H
#define XVT_REMASTER_ORIGINAL_2D_H
#include "aeron/asset/decode_types.h"
#include "aeron/asset/pnl.h"
#include "aeron/scene/runtime_atlas.h"
#include "xvt_remaster/font.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XvtOriginal2d {
	AeronIndexedFrames images;
	AeronDecodedFont font;
	/* PNL uses the flight palette; LFD supplies a range within it. */
	uint16_t palette_first, palette_count;
	int external_palette;
	uint8_t* cockpit_mask;
	size_t cockpit_mask_size;
	uint8_t* panel_bytes;
	size_t panel_size;
	AeronPnlList panel_records;
	uint8_t map_palette_used[256];
} XvtOriginal2d;

int XvtOriginal2d_Load(const XvtSnapImageAsset* source, XvtOriginal2d* out, char* error, size_t capacity);
int XvtOriginal2d_LoadAct(const char* path, XvtOriginal2d* out, char* error, size_t capacity);
void XvtOriginal2d_Free(XvtOriginal2d* source);
int XvtOriginal2d_BuildAtlas(const XvtOriginal2d* source, AeronCommandBuffer* cmd,
							 const uint32_t palette[256], uint16_t key, uint16_t key_alt, int generate_mips,
							 const char* label, AeronRuntimeAtlas* out);
int XvtOriginal2d_BuildFont(const AeronDecodedFont* source, AeronCommandBuffer* cmd, int shadow,
							const char* label, XvtFontAtlas* out);
int XvtOriginal2d_BuildMapIcons(const XvtOriginal2d* source, AeronCommandBuffer* cmd,
								const uint32_t palette[256], int remap, AeronRuntimeAtlas* out);
int XvtCockpitAssets_DecodeLfd(const void* bytes, size_t size, XvtOriginal2d* out, AeronDecodeError* error);
int XvtCockpitAssets_ApplyMask(XvtOriginal2d* image, const XvtSnapRect* viewport);
#ifdef __cplusplus
}
#endif
#endif
