#include "xvt_runtime/snapshot/render_hud.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/object/object.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include <string.h>

static XvtSnapTargetBox g_boxes[XVT_SNAP_TARGET_BOXES];
static unsigned g_boxCount, g_scope = XVT_SCOPE_COCKPIT;

void XvtRenderDraw_Scope(unsigned scope) { g_scope = scope; }

unsigned XvtRenderDraw_ScopeCurrent(void) { return g_scope; }

uint32_t XvtRenderDraw_Color(unsigned index) {
	/* HD colors use the unadjusted DAC palette, independent of classic brightness and pixel format. */
	index &= 255;
	unsigned r = g_swPalette[index].r & 63, g = g_swPalette[index].g & 63, b = g_swPalette[index].b & 63;
	r = (r << 2) | (r >> 4);
	g = (g << 2) | (g >> 4);
	b = (b << 2) | (b >> 4);
	return 0xff000000u | (r << 16) | (g << 8) | b;
}

void XvtRenderHud_Reset(void) {
	g_boxCount = 0;
	g_scope = XVT_SCOPE_COCKPIT;
}

void XvtRenderHud_BeginFrame(void) { XvtRenderHud_Reset(); }

void XvtRenderHud_Publish(XvtRenderSnapshot* out) {
	out->target_box_count = g_boxCount;
	memcpy(out->target_boxes, g_boxes, g_boxCount * sizeof *g_boxes);
}

void XvtRenderHud_TargetBox(unsigned object, unsigned component, int extent, int color) {
	XvtRenderSnapshot* s = XvtRenderSnapshot_Writer();
	if (!s || XvtRenderDraw_ScopeCurrent() == XVT_SCOPE_MAP || XvtRenderDraw_ScopeCurrent() == XVT_SCOPE_CRT)
		return;
	if (g_boxCount == XVT_SNAP_TARGET_BOXES) {
		++s->dropped_records;
		return;
	}
	XvtSnapTargetBox* b = &g_boxes[g_boxCount++];
	*b = (XvtSnapTargetBox) { .object = { UINT16_MAX, 0 },
							  .component = (uint16_t)component,
							  .color_index = (uint16_t)color,
							  .layer = XVT_SCOPE_COCKPIT,
							  .extent = extent,
							  .world_pos = { worldlocx, worldlocy, worldlocz } };
	if (object < (unsigned)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount))
		b->object = (XvtSnapObjectId) { (uint16_t)object, g_objectTable[object].objectSignature };
}
