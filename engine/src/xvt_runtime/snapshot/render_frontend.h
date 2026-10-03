#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_FRONTEND_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_FRONTEND_H
#include "xvt/frontend/front_image.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Frontend draw capture. The recovered frontend's image, glyph, paint, copy and clear calls are
 * recorded as ordered draw records in the writer snapshot, each tagged with the current draw
 * target and clip rectangle. Colors are converted from the live frontend pixel format.
 * Every draw call does nothing without an open tick or while capture is suppressed, and counts
 * a dropped record when its list is full. */

/* Resets targets, suppression, serials, cursor and font indexes. */
void XvtRenderFrontend_Init(void);
/* BeginDraw clears the text-entry mark; TextEntry sets it when field is the active text field. */
void XvtRenderFrontend_BeginDraw(void);
void XvtRenderFrontend_TextEntry(int field);
/* Return or set the draw target; Select is ignored while capture is suppressed. */
unsigned XvtRenderFrontend_Target(void);
void XvtRenderFrontend_Select(unsigned target);
/* Nests capture suppression: begin adds a level, end removes one; it never drops below zero. */
void XvtRenderFrontend_Suppress(int begin);
/* Records an image draw. While the cursor is being drawn, the whole image goes to the cursor
 * sprite instead. A NULL or empty image is ignored; one with no registered asset counts as
 * dropped. */
void XvtRenderFrontend_Image(const struct ImageResource *image, int sx, int sy,
			     int x, int y, int width, int height, unsigned kind,
			     unsigned tint);
/* Records a glyph by finding it in an in-use font slot indexed by FontLoaded, matched by pixel
 * address and size; a glyph found in no slot counts as dropped. With remap at 16 bits per pixel
 * and a fade running, the color is faded. */
void XvtRenderFrontend_Glyph(const struct ImageResource *glyph, int x, int y,
			     unsigned color, int remap);
/* Records a paint primitive; translucent paint at 16 bits per pixel gets half alpha. */
void XvtRenderFrontend_Paint(unsigned kind, int x0, int y0, int x1, int y1,
			     unsigned color);
/* Copy records a full 640x480 copy between targets; Clear records a clear of target. Either one
 * aimed at the back buffer hides the cursor, unless capture is suppressed. */
void XvtRenderFrontend_Copy(unsigned source, unsigned target);
void XvtRenderFrontend_Clear(unsigned target, unsigned color);
/* Records the cursor sprite if visible, then a present event and the presented scene: modal
 * dialog, loading or frontend. If the sprite list is full with the cursor visible, it counts a
 * dropped record and records neither. */
void XvtRenderFrontend_Present(void);
/* Re-enables classic rendering, raises the frontend generation, clears the released mark and the
 * cursor, records a reset to opaque black and selects the back buffer. */
void XvtRenderFrontend_Reset(void);
/* Marks the frontend surfaces released until the next Reset. */
void XvtRenderFrontend_ReleaseSurfaces(void);
/* Records saving (restore 0) or restoring a saved screen slot's rectangle, against the back
 * buffer or, when offscreen restore is enabled, the offscreen buffer. Other slots are ignored. */
void XvtRenderFrontend_Screen(int slot, int restore);
/* Does nothing without an open tick or while suppressed. With restore, hides the cursor.
 * Otherwise begins drawing it at the mouse: from the named cursor
 * sprite when there is one, else the default cursor. EndCursor ends cursor drawing. */
void XvtRenderFrontend_Cursor(int restore);
void XvtRenderFrontend_EndCursor(void);
/* Indexes the glyphs of font, if it is one of the 10 frontend font slots, and records its asset
 * id as registered now. Other fonts are ignored. */
void XvtRenderFrontend_FontLoaded(const struct BitmapFont *font);
/* Begin selects the movie target and clears it; end presents it, records the movie scene and
 * selects the back buffer again. */
void XvtRenderFrontend_Movie(int begin);
/* Stamps snapshot with the presentation serial, presented scene and target, frontend generation,
 * released mark, and text entry (only in a frontend scene). */
void XvtRenderFrontend_Commit(struct XvtRenderSnapshot *snapshot);
/* PresentedScene raises the presentation serial and sets the presented scene, with the flight,
 * movie or back-buffer target to match; FlightUiScene does the same but always targets flight. */
void XvtRenderFrontend_PresentedScene(XvtSceneKind kind);
void XvtRenderFrontend_FlightUiScene(XvtSceneKind kind);
#ifdef __cplusplus
}
#endif
#endif
