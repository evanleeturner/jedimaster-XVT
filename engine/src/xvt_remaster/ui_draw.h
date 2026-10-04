#ifndef XVT_REMASTER_UI_DRAW_H
#define XVT_REMASTER_UI_DRAW_H
#include "aeron/scene/draw_list2d.h"
#include "xvt_remaster/assets.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 2D helpers for the frontend and the map: ARGB to premultiplied linear color,
 * glyphs and text from the image cache's fonts, map icons, paint records, and
 * the copy pass between frontend surfaces. */

/* out = argb's sRGB channels linearized and premultiplied by its alpha, alpha last. */
void xvt_ui_color(uint32_t argb, float out[4]);
/* Draws one snapshot glyph scaled and offset: its background over its advance
 * (plus one with a shadow) by its glyph height when enabled, the shadow one
 * scaled pixel down and right (cut at the glyph's bottom) when enabled, then
 * the foreground, all cut to the glyph's clip; nothing when its font is not
 * loaded or its character is outside the font. */
void xvt_ui_glyph(AeronDrawList2D *list, const struct xvt_snap_glyph *glyph,
		  float scale, float ox, float oy);
/* Draws text in the font asset at x, y scaled, in color, centered on x when
 * asked; a character outside the font draws nothing and advances nothing.
 * Nothing without the font or text. */
void xvt_ui_text(AeronDrawList2D *list, uint64_t font_asset_id,
		 const char *text, float x, float y, float scale,
		 uint32_t color, int centered);
/* Draws frame of the asset's map-icon atlas under palette and remap (prepared
 * through the image cache with cmd when absent) at x, y scaled, cut to width x
 * height. Returns 0 only when the atlas cannot be prepared; 1 also when the
 * frame is absent. */
int xvt_ui_map_icon(AeronDrawList2D *list, AeronCommandBuffer *cmd,
		    uint64_t asset_id, unsigned frame, int remap,
		    const uint32_t palette[256], float x, float y, float scale,
		    int width, int height);
/* Draws a paint record scaled: a line between pixel centers, a frame, or a fill
 * from (x0, y0) to (x1, y1), opaque except a translucent fill, cut to its
 * clip. */
void xvt_ui_paint(AeronDrawList2D *list, const struct xvt_snap_paint *paint,
		  float scale);
/* Copies the from rectangle of src (source_width x source_height) into the to
 * rectangle of dst with nearest sampling, in a pass of its own, creating the
 * copy pipeline on first use. Returns 0 when the pipeline or sampler is
 * missing, dst or src is NULL, a size is not positive, or the pass cannot
 * begin. */
int xvt_ui_copy_frontend(AeronCommandBuffer *cmd, AeronRenderTarget *dst,
			 AeronTexture *src, const struct xvt_snap_rect *from,
			 const struct xvt_snap_rect *to, int source_width,
			 int source_height);
/* Destroys the copy pipeline, its shaders and sampler. */
void xvt_ui_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
