#ifndef XVT_APP_VIDEO_PAGE_H
#define XVT_APP_VIDEO_PAGE_H
#include "aeron/scene/ui.h"
/* The Video page, in a scroll area at least 180 high: the Display, HDR Presentation and Flight Rendering
 * groups edit a copy of the requested video record and submit any change as one request (a rejection is
 * reported to the menu); Restore Defaults requests the shipped defaults. A value
 * outside a list shows as an extra "(configured)" or custom entry; SDR gamma and paper white are enabled only
 * while HDR output is on (never on Apple systems); choosing temporal upscaling sets MSAA off and choosing
 * MSAA sets upscaling off; the motion blur amount shows only with blur on. Notes the HDR output status while
 * HDR is on but not active, and that an undither change applies at the next cockpit load. input is unused. */
void XvtVideoPage_Draw(AeronUiContext *ui, const AeronInputSnapshot *input);
#endif
