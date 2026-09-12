#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_TEXT_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_TEXT_H

#include "xvt_runtime/snapshot/cockpit_state.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
uint8_t XvtCockpitText_ResolveColor(uint8_t code, uint8_t bypass);
void XvtCockpitText_ResetFields(void);
void XvtCockpitText_ClearField(XvtCockpitTextFieldId field);
void XvtCockpitText_ClearTargetFields(void);
void XvtCockpitText_RecordField(XvtCockpitTextFieldId field, const char* text, XvtCockpitAlignment alignment);
void XvtCockpitText_CopyFields(XvtCockpitState* state);
void XvtCockpitText_CopyPlacedField(XvtCockpitTextField* field, XvtCockpitTextFieldId id, int offset_x,
									int offset_y);
int XvtCockpitText_CaptureGlyph(XvtCockpitGlyph* glyph, unsigned character, unsigned advance, unsigned height,
								int narrow, int origin_x, int origin_y, const uint32_t palette[256],
								int keyed);
#ifdef __cplusplus
}
#endif
#endif
