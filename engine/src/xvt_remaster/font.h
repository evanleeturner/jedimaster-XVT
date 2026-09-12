#ifndef XVT_REMASTER_FONT_H
#define XVT_REMASTER_FONT_H

#include "aeron/scene/font_atlas.h"

/* Layout stays in classic units regardless of atlas resolution or packing. */
typedef struct XvtFontGlyph {
	uint16_t width, height, advance;
} XvtFontGlyph;

typedef struct XvtFontAtlas {
	AeronFontAtlas atlas;
	XvtFontGlyph* glyphs; /* Owned classic metrics, indexed like atlas.glyphs. */
	uint16_t cell_height;
	float white_uv[2]; /* Opaque atlas sample for batched text backgrounds. */
} XvtFontAtlas;

#endif
