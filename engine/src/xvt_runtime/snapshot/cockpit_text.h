#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_TEXT_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_TEXT_H

#include "xvt_runtime/snapshot/cockpit_state.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Named cockpit text fields. The recovered HUD text calls record into a fixed table of fields
 * (xvt_cockpit_text_field_id); CopyFields copies it into cockpit state. A field's generation rises
 * whenever its content changes. */

/* Maps an inline color code from 0x40 through the flight text color table, unless it equals
 * bypass; any other code is returned unchanged. */
uint8_t xvt_cockpit_text_resolve_color(uint8_t code, uint8_t bypass);
/* Clears every field, generations included. */
void xvt_cockpit_text_reset_fields(void);
/* Clears a visible field and raises its generation; an invisible or out-of-range field is left
 * alone. */
void xvt_cockpit_text_clear_field(xvt_cockpit_text_field_id field);
/* ClearField for every field from TARGET_NAME through CMD_TIME_UNKNOWN. */
void xvt_cockpit_text_clear_target_fields(void);
/* Records text into field with the live flight text state: font, colors, clip rectangle,
 * cursor, shadow, wrap and color keying. Inline 0xFE color codes are resolved now; a leading
 * 0xFE code or a leading byte below 0x10 sets the foreground. The generation rises only when
 * something changed. Empty text records an invisible field. Ignored for an out-of-range field
 * or NULL text; text that does not fit the caption logs an error and is ignored. */
void xvt_cockpit_text_record_field(xvt_cockpit_text_field_id field,
				   const char *text,
				   xvt_cockpit_alignment alignment);
/* Copies every field into state, then hides each field whose readout, target panel, course,
 * map, shield or warning is hidden in state; the other fields show only in the forward and
 * HUD-only views, except the resource name. Reads state's readouts, target, systems, proving
 * grounds and view, so those must be filled first. */
void xvt_cockpit_text_copy_fields(struct xvt_cockpit_state *state);
/* Copies field id moved by the offset (x only for left-aligned text), drawn after the CRT and
 * keyed on the bypass color. id is not range-checked. */
void xvt_cockpit_text_copy_placed_field(struct xvt_cockpit_text_field *field,
					xvt_cockpit_text_field_id id,
					int offset_x, int offset_y);
/* Fills glyph for character at the live text cursor, relative to the origin, with colors from
 * palette; when keyed, a color equal to the bypass color becomes 0. Returns 1, or 0 when the
 * glyph cell lies outside the clip rectangle, leaving glyph untouched. */
int xvt_cockpit_text_capture_glyph(struct xvt_cockpit_glyph *glyph,
				   unsigned character, unsigned advance,
				   unsigned height, int narrow, int origin_x,
				   int origin_y, const uint32_t palette[256],
				   int keyed);
#ifdef __cplusplus
}
#endif
#endif
