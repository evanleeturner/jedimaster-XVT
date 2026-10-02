#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_TEXT_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_TEXT_H

#include "xvt_runtime/snapshot/cockpit_state.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Named cockpit text fields. The recovered HUD text calls record into a fixed table of fields
 * (XvtCockpitTextFieldId); CopyFields copies it into cockpit state. A field's generation rises
 * whenever its content changes. */

/* Maps an inline color code from 0x40 through the flight text color table, unless it equals
 * bypass; any other code is returned unchanged. */
uint8_t XvtCockpitText_ResolveColor(uint8_t code, uint8_t bypass);
/* Clears every field, generations included. */
void XvtCockpitText_ResetFields(void);
/* Clears a visible field and raises its generation; an invisible or out-of-range field is left
 * alone. */
void XvtCockpitText_ClearField(XvtCockpitTextFieldId field);
/* ClearField for every field from TARGET_NAME through CMD_TIME_UNKNOWN. */
void XvtCockpitText_ClearTargetFields(void);
/* Records text into field with the live flight text state: font, colors, clip rectangle,
 * cursor, shadow, wrap and color keying. Inline 0xFE color codes are resolved now; a leading
 * 0xFE code or a leading byte below 0x10 sets the foreground. The generation rises only when
 * something changed. Empty text records an invisible field. Ignored for an out-of-range field
 * or NULL text; text that does not fit the caption logs an error and is ignored. */
void XvtCockpitText_RecordField(XvtCockpitTextFieldId field, const char *text,
				XvtCockpitAlignment alignment);
/* Copies every field into state, then hides each field whose readout, target panel, course,
 * map, shield or warning is hidden in state; the other fields show only in the forward and
 * HUD-only views, except the resource name. Reads state's readouts, target, systems, proving
 * grounds and view, so those must be filled first. */
void XvtCockpitText_CopyFields(XvtCockpitState *state);
/* Copies field id moved by the offset (x only for left-aligned text), drawn after the CRT and
 * keyed on the bypass color. id is not range-checked. */
void XvtCockpitText_CopyPlacedField(XvtCockpitTextField *field,
				    XvtCockpitTextFieldId id, int offset_x,
				    int offset_y);
/* Fills glyph for character at the live text cursor, relative to the origin, with colors from
 * palette; when keyed, a color equal to the bypass color becomes 0. Returns 1, or 0 when the
 * glyph cell lies outside the clip rectangle, leaving glyph untouched. */
int XvtCockpitText_CaptureGlyph(XvtCockpitGlyph *glyph, unsigned character,
				unsigned advance, unsigned height, int narrow,
				int origin_x, int origin_y,
				const uint32_t palette[256], int keyed);
#ifdef __cplusplus
}
#endif
#endif
