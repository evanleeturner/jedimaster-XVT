#ifndef XVT_REMASTER_FONT_H
#define XVT_REMASTER_FONT_H

#include "aeron/scene/font_atlas.h"

/* Layout stays in classic units regardless of atlas resolution or packing. */
struct xvt_font_glyph {
	uint16_t width, height, advance;
};

struct xvt_font_atlas {
	AeronFontAtlas atlas;
	struct xvt_font_glyph
		*glyphs; /* Owned classic metrics, indexed like atlas.glyphs. */
	uint16_t cell_height;
	float white_uv
		[2]; /* Opaque atlas sample for batched text backgrounds. */
};

#endif
