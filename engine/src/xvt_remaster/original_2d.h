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
/* Decoded original 2D resources (PNL panels, LFD cockpit panels, BMP frontend images, ACT textures, ABP
 * and bitmap fonts, the built-in cursor) as indexed frames with the palette and masks that go with
 * them, and the builders that turn them into atlases, font atlases and map-icon atlases. */

/* images: the indexed frames, each with coverage and its own palette. font: a decoded font.
 * external_palette: indices outside the LFD range take the caller's palette. cockpit_mask: the LFD
 * mask runs, raw. panel_bytes, panel_records: the raw PNL and its bitmap records. map_palette_used:
 * the palette indices an ICO's frames use. */
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

/* Zeroes out and decodes source by kind from the asset root: BMP as one frame with an external
 * palette; LFD as a cockpit panel with its palette range and mask; PNL and ICO as the raw records
 * (ICO also its frames, undecoded but for the palette indices they use); ABP as a font with binary
 * coverage, or a micro font; or the built-in 10 x 10 cursor. Returns 0 with error written and out
 * freed for an unreadable file or a decode or allocation failure. */
int XvtOriginal2d_Load(const XvtSnapImageAsset* source, XvtOriginal2d* out, char* error, size_t capacity);
/* Zeroes out and decodes an ACT texture's frames. Returns 0 with error written and out freed on
 * failure. */
int XvtOriginal2d_LoadAct(const char* path, XvtOriginal2d* out, char* error, size_t capacity);
/* Frees everything and zeroes it; NULL is accepted. */
void XvtOriginal2d_Free(XvtOriginal2d* source);
/* Packs every frame into an sRGB atlas: a covered pixel whose index is neither key takes its frame's
 * palette color, or palette's when the source has an external palette and the index is outside its
 * range, with its coverage as alpha; premultiplied alpha mode when every coverage is 255, straight
 * otherwise. Returns 0 for no frames or an allocation or build failure. */
int XvtOriginal2d_BuildAtlas(const XvtOriginal2d* source, AeronCommandBuffer* cmd,
							 const uint32_t palette[256], uint16_t key, uint16_t key_alt, int generate_mips,
							 const char* label, AeronRuntimeAtlas* out);
/* Builds the font's foreground or shadow plane as a font atlas upscaled 4x nearest, with a solid strip
 * for text backgrounds (white_uv), the classic glyph metrics kept in out->glyphs. Returns 1 doing
 * nothing when the plane is absent; 0 when a dimension or glyph metric would overflow 16 bits after
 * scaling, or an allocation or build fails. */
int XvtOriginal2d_BuildFont(const AeronDecodedFont* source, AeronCommandBuffer* cmd, int shadow,
							const char* label, XvtFontAtlas* out);
/* Packs every ICO frame into an sRGB premultiplied atlas colored by palette (the index plus 4 with
 * remap), empty records as 1 x 1 blanks. Returns 0 for no frames or a decode, allocation or build
 * failure. */
int XvtOriginal2d_BuildMapIcons(const XvtOriginal2d* source, AeronCommandBuffer* cmd,
								const uint32_t palette[256], int remap, AeronRuntimeAtlas* out);
/* Parses an LFD cockpit panel: the PANL record as one frame, the PLTT record as the palette range
 * (6-bit VGA values widened), the MASK record kept raw. Returns 0 with error set when a record is
 * missing or malformed or an allocation fails, and 0 with out partly filled when the PANL decode
 * fails. */
int XvtCockpitAssets_DecodeLfd(const void* bytes, size_t size, XvtOriginal2d* out, AeronDecodeError* error);
/* Clears the frame's coverage inside viewport where the mask's runs are open (positive parity),
 * decoding the original's run format for the frame's width. Returns 0 for no frames or mask, a NULL
 * viewport, a viewport outside the frame, or a malformed run; 1 also when the mask ends early. */
int XvtCockpitAssets_ApplyMask(XvtOriginal2d* image, const XvtSnapRect* viewport);
#ifdef __cplusplus
}
#endif
#endif
