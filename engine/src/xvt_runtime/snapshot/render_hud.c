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

static struct xvt_snap_target_box g_boxes[XVT_SNAP_TARGET_BOXES];
static unsigned g_box_count, g_scope = XVT_SCOPE_COCKPIT;

void xvt_render_draw_scope(unsigned scope) { g_scope = scope; }

unsigned xvt_render_draw_scope_current(void) { return g_scope; }

uint32_t xvt_render_draw_color(unsigned index)
{
	/* HD colors use the unadjusted DAC palette, independent of classic brightness and pixel format. */
	index &= 255;
	unsigned r = g_sw_palette[index].r & 63, g = g_sw_palette[index].g & 63,
		 b = g_sw_palette[index].b & 63;
	r = (r << 2) | (r >> 4);
	g = (g << 2) | (g >> 4);
	b = (b << 2) | (b >> 4);
	return 0xff000000u | (r << 16) | (g << 8) | b;
}

void xvt_render_hud_reset(void)
{
	g_box_count = 0;
	g_scope = XVT_SCOPE_COCKPIT;
}

void xvt_render_hud_begin_frame(void) { xvt_render_hud_reset(); }

void xvt_render_hud_publish(struct xvt_render_snapshot *out)
{
	out->target_box_count = g_box_count;
	memcpy(out->target_boxes, g_boxes, g_box_count * sizeof *g_boxes);
}

void xvt_render_hud_target_box(unsigned object, unsigned component, int extent,
			       int color)
{
	struct xvt_render_snapshot *s = xvt_render_snapshot_writer();
	if (!s || xvt_render_draw_scope_current() == XVT_SCOPE_MAP ||
	    xvt_render_draw_scope_current() == XVT_SCOPE_CRT) {
		return;
	}
	if (g_box_count == XVT_SNAP_TARGET_BOXES) {
		++s->dropped_records;
		return;
	}
	struct xvt_snap_target_box *b = &g_boxes[g_box_count++];
	*b = (struct xvt_snap_target_box){
		.object = {UINT16_MAX, 0},
		.component = (uint16_t)component,
		.color_index = (uint16_t)color,
		.scope = XVT_SCOPE_COCKPIT,
		.extent = extent,
		.world_pos = {g_world_loc_x, g_world_loc_y, g_world_loc_z}};
	if (object < (unsigned)(g_region_main_object_slot_end +
				g_region_static_object_slot_count)) {
		b->object = (struct xvt_snap_object_id){
			(uint16_t)object,
			g_object_table[object].object_signature};
	}
}
