#include "xvt/flight/flight_surface.h"

#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

#include <string.h>

/* 1 when flight draws through DirectDraw surfaces and flips pages; 0 when it
 * draws into memory and flight_display_flip copies g_flight_software_framebuffer
 * to the screen. Starts at 1; the launch options "nopageflip" and
 * "pageflip" set it, read by flight_main in the original build and
 * xvt_flight_entry_read_launch_switches in the modern one. */
// GLOBAL: XVT 0x527EAC
int g_flight_page_flip = 1;
/* 1 when flight_surface_lock is to hand out the cockpit and HUD layer
 * (g_flight_offscreen_surface, or g_flight_hud_staging_buffer without page
 * flipping); 0 for the frame the 3D view is drawn in (g_flight_back_buffer or
 * g_flight_software_framebuffer). Starts at 1. Many functions write it,
 * chiefly flight_view_render, the alert box code and the pause key, which set
 * it to 0 around their own drawing and then to 1. */
// GLOBAL: XVT 0x527ED0
int g_flight_draw_to_hud_layer = 1;
/* With page flipping, the offscreen surface, g_surface_width by
 * g_surface_height, that holds the cockpit and HUD layer;
 * flight_display_blit_render_surface and flight_view_composite_masked_software_surface
 * copy it onto the back buffer. Four functions write it: flight_display_init
 * creates it; flight_display_cleanup_and_report_error and the flight shutdown
 * (flight_main in the original build, xvt_flight_entry_cleanup in the modern
 * one) release it and set NULL. */
// GLOBAL: XVT 0x66DDCC
IDirectDrawSurface *g_flight_offscreen_surface = 0;
/* 1 while the flight's back buffer and offscreen surface are in use, so
 * flight_surface_lock locks one of them instead of the primary surface. Set
 * to 1 at flight start and 0 at its end (flight_main_loop in the original
 * build; xvt_flight_loading_globals and xvt_flight_task_release_mission in the
 * modern one); fe_disk_io_show_fatal_error_message_and_wait_key sets 1 while it
 * shows its message and then puts the old value back. */
// GLOBAL: XVT 0x9ED23B
uint8_t g_flight_display_surfaces_active = 0;
/* Ticks the local input clock (g_input_timestamp) aims to run ahead of
 * g_server_tick_time: 30, or 130 in internet play, at mission start; then the
 * host's measured start delay, at least 35, and the clock probes move it.
 * Six functions write it: flight_net_wait_for_mission_start and
 * flight_net_process_incoming_packets in the original build;
 * xvt_flight_network_wait_for_mission_start, xvt_flight_network_answer_clock_probe,
 * xvt_flight_network_apply_clock_probe_reply and xvt_flight_network_control in the
 * modern one. flight_surface_lock and flight_surface_unlock also read its low
 * byte: see flight_surface_lock. */
// GLOBAL: XVT 0x9ED23C
int32_t g_flight_net_clock_lead_ticks = 0;
/* Nothing sets it to anything but 0 (flight_view_render does, three times),
 * so the tests of it in the HUD box drawing, the rotated sprite blitter and
 * the software face drawer always find 0. */
// GLOBAL: XVT 0xA0813F
uint8_t g_flight_surface_already_locked = 0;

/* Nesting depth of flight_surface_lock calls not yet undone by
 * flight_surface_unlock; only those two functions write it. */
// GLOBAL: XVT 0x527F78
int g_surface_lock_count = 0;
/* Pixel base the software renderer starts from when its render target is
 * reset (flight_sw_set_render_target with NULL, flight_sw_init_framebuffer).
 * Starts at 0xA0000; only flight_surface_set_software_framebuffer_base writes
 * it, as flight_surface_lock picks a surface. */
// GLOBAL: XVT 0x5280E8
void *g_sw_framebuffer_base = (void *)0xA0000;
/* Bytes in 480 rows of the primary pitch, recorded by
 * flight_surface_set_viewport480_byte_span on every first lock. Nothing reads
 * it. */
// GLOBAL: XVT 0x5280EC
static int g_flight_surface_viewport480_byte_span;

/* Fills the screen-sized buffer g_flight_offscreen_buffer with the color
 * index g_flight_transparent_color_index: by memset at 1 byte per pixel, else
 * through g_flight_fill_clip_rect_fn with the buffer as a temporary render
 * target, which it then resets (flight_sw_set_render_target with NULL). Changes
 * the text clip rectangle and background color on that path. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x49CAE0
void flight_surface_clear_to_black(void)
{
	if (g_flight_bytes_per_pixel == 1) {
		memset(g_flight_offscreen_buffer,
		       g_flight_transparent_color_index,
		       g_screen_width * g_screen_height);
	} else {
		flight_sw_set_render_target(g_flight_offscreen_buffer,
					    g_screen_width, g_screen_height,
					    g_screen_width *
						    g_flight_bytes_per_pixel);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_clip_rect(0, 0, g_screen_width,
					  g_screen_height);
		g_flight_fill_clip_rect_fn();
		flight_sw_set_render_target(NULL, 320, 240, 0);
	}
}

/* Returns g_surface_lock_count. */
// FUNCTION: XVT 0x4ABA40
int flight_surface_get_lock_count(void) { return g_surface_lock_count; }

/* Locks the surface the software renderer draws to and points
 * g_flight_sw_framebuffer_base, g_surface_pixels and g_sw_framebuffer_base at its
 * pixels. Only the first of nested calls does the work; the rest add to
 * g_surface_lock_count. With g_flight_render_to_frontend 1 it takes the
 * frontend's draw surface and pitch instead, leaving the count alone.
 * Without page flipping it hands out memory: g_flight_hud_staging_buffer when
 * g_flight_draw_to_hud_layer is set, else g_flight_software_framebuffer. With page
 * flipping and the surfaces active, it locks g_flight_offscreen_surface when
 * g_flight_draw_to_hud_layer is set, else g_flight_back_buffer; with them not
 * active, the primary surface. The surfaces count as active when
 * g_flight_display_surfaces_active or the low byte of
 * g_flight_net_clock_lead_ticks is nonzero. On the back buffer and the primary
 * the pointers are moved to center g_surface_width by g_surface_height in the
 * display mode. Sets g_flight_primary_pitch[0] to the locked surface's pitch
 * and, on the offscreen surface and back buffer, g_surface_pitch and the
 * render target when the pitch changed. Retries while DirectDraw is still
 * drawing; on any other lock failure it returns with the count already
 * raised. */
// FUNCTION: XVT 0x4ABA50
void flight_surface_lock(void)
{
	DDSURFACEDESC surface_desc;
	HRESULT lock_result;
	uint8_t display_surface_state;
	unsigned int horizontal_offset;
	unsigned int vertical_offset;

	if (g_flight_render_to_frontend == 1) {
		g_flight_sw_framebuffer_base =
			frontend_display_get_draw_surface_for_flight();
		g_surface_pixels = g_flight_sw_framebuffer_base;
		g_surface_pitch =
			frontend_display_get_frontend_or_flight_draw_pitch();
		return;
	}
	if (g_surface_lock_count != 0) {
		++g_surface_lock_count;
		return;
	}
	++g_surface_lock_count;

	if (g_flight_page_flip != 0) {
		display_surface_state = g_flight_display_surfaces_active;
		display_surface_state |= (uint8_t)g_flight_net_clock_lead_ticks;
		if (display_surface_state != 0) {
			if (g_flight_draw_to_hud_layer != 0) {
				memset(&surface_desc, 0, sizeof(surface_desc));
				surface_desc.dwSize = sizeof(surface_desc);
				for (;;) {
					lock_result =
						g_flight_offscreen_surface
							->lpVtbl
							->Lock(g_flight_offscreen_surface,
							       NULL,
							       &surface_desc, 0,
							       NULL);
					if (lock_result == DX_DD_OK) {
						break;
					}
					if (lock_result !=
					    DX_DDERR_WASSTILLDRAWING) {
						return;
					}
				}

				flight_surface_set_software_framebuffer_base(
					surface_desc.lpSurface);
				g_flight_sw_framebuffer_base =
					surface_desc.lpSurface;
				g_surface_pixels = surface_desc.lpSurface;
				memset(&surface_desc, 0, sizeof(surface_desc));
				surface_desc.dwSize = sizeof(surface_desc);
				g_flight_offscreen_surface->lpVtbl
					->GetSurfaceDesc(
						g_flight_offscreen_surface,
						&surface_desc);
				g_flight_primary_pitch[0] = surface_desc.lPitch;
				flight_surface_set_viewport480_byte_span(
					480 *
					flight_display_get_primary_surface_pitch());
				if (g_surface_pitch !=
				    g_flight_primary_pitch[0]) {
					g_surface_pitch =
						g_flight_primary_pitch[0];
					flight_sw_set_render_target(
						g_flight_sw_framebuffer_base,
						g_flight_primary_pitch[0], 480,
						-1);
				}
			} else {
				memset(&surface_desc, 0, sizeof(surface_desc));
				surface_desc.dwSize = sizeof(surface_desc);
				for (;;) {
					lock_result =
						g_flight_back_buffer->lpVtbl->Lock(
							g_flight_back_buffer,
							NULL, &surface_desc, 0,
							NULL);
					if (lock_result == DX_DD_OK) {
						break;
					}
					if (lock_result !=
					    DX_DDERR_WASSTILLDRAWING) {
						return;
					}
				}

				flight_surface_set_software_framebuffer_base(
					surface_desc.lpSurface);
				g_flight_sw_framebuffer_base =
					surface_desc.lpSurface;
				g_surface_pixels = surface_desc.lpSurface;
				memset(&surface_desc, 0, sizeof(surface_desc));
				surface_desc.dwSize = sizeof(surface_desc);
				g_flight_back_buffer->lpVtbl->GetSurfaceDesc(
					g_flight_back_buffer, &surface_desc);
				g_flight_primary_pitch[0] = surface_desc.lPitch;
				horizontal_offset =
					g_flight_bytes_per_pixel *
					((unsigned int)(g_display_mode_width -
							g_surface_width) >>
					 1);
				vertical_offset =
					surface_desc.lPitch *
					((unsigned int)(g_display_mode_height -
							g_surface_height) >>
					 1);
				g_flight_sw_framebuffer_base +=
					horizontal_offset;
				g_flight_sw_framebuffer_base += vertical_offset;
				g_surface_pixels = (uint8_t *)g_surface_pixels +
						   horizontal_offset;
				g_surface_pixels = (uint8_t *)g_surface_pixels +
						   vertical_offset;
				flight_surface_set_viewport480_byte_span(
					480 *
					flight_display_get_primary_surface_pitch());
				if (g_surface_pitch !=
				    g_flight_primary_pitch[0]) {
					g_surface_pitch =
						g_flight_primary_pitch[0];
					flight_sw_set_render_target(
						g_flight_sw_framebuffer_base,
						g_flight_primary_pitch[0], 480,
						-1);
				}
			}
		} else {
			memset(&surface_desc, 0, sizeof(surface_desc));
			surface_desc.dwSize = sizeof(surface_desc);
			for (;;) {
				lock_result =
					g_flight_primary_surface->lpVtbl->Lock(
						g_flight_primary_surface, NULL,
						&surface_desc, 0, NULL);
				if (lock_result == DX_DD_OK) {
					break;
				}
				if (lock_result != DX_DDERR_WASSTILLDRAWING) {
					return;
				}
			}

			flight_surface_set_software_framebuffer_base(
				surface_desc.lpSurface);
			g_flight_sw_framebuffer_base = surface_desc.lpSurface;
			g_surface_pixels = surface_desc.lpSurface;
			memset(&surface_desc, 0, sizeof(surface_desc));
			surface_desc.dwSize = sizeof(surface_desc);
			g_flight_primary_surface->lpVtbl->GetSurfaceDesc(
				g_flight_primary_surface, &surface_desc);
			g_flight_primary_pitch[0] = surface_desc.lPitch;
			horizontal_offset =
				g_flight_bytes_per_pixel *
				((unsigned int)(g_display_mode_width -
						g_surface_width) >>
				 1);
			vertical_offset =
				surface_desc.lPitch *
				((unsigned int)(g_display_mode_height -
						g_surface_height) >>
				 1);
			g_flight_sw_framebuffer_base += horizontal_offset;
			g_flight_sw_framebuffer_base += vertical_offset;
			g_surface_pixels =
				(uint8_t *)g_surface_pixels + horizontal_offset;
			g_surface_pixels =
				(uint8_t *)g_surface_pixels + vertical_offset;
			flight_surface_set_viewport480_byte_span(
				480 *
				flight_display_get_primary_surface_pitch());
		}
	} else if (g_flight_draw_to_hud_layer != 0) {
		flight_surface_set_software_framebuffer_base(
			g_flight_hud_staging_buffer);
		flight_surface_set_viewport480_byte_span(
			480 * flight_display_get_primary_surface_pitch());
		g_flight_sw_framebuffer_base = g_flight_hud_staging_buffer;
		g_surface_pixels = g_flight_hud_staging_buffer;
	} else {
		flight_surface_set_software_framebuffer_base(
			g_flight_software_framebuffer);
		flight_surface_set_viewport480_byte_span(
			480 * flight_display_get_primary_surface_pitch());
		g_flight_sw_framebuffer_base = g_flight_software_framebuffer;
		g_surface_pixels = g_flight_software_framebuffer;
	}
}

/* Undoes one flight_surface_lock: above 1 it only lowers g_surface_lock_count;
 * below 1 it sets it to 0; at 1 it lowers it to 0 and, with page flipping,
 * unlocks the surface flight_surface_lock picks by the same test. Does nothing
 * when g_flight_render_to_frontend is 1. */
// FUNCTION: XVT 0x4ABE50
void flight_surface_unlock(void)
{
	uint8_t display_surface_state;

	if (g_flight_render_to_frontend == 1) {
		return;
	}
	if (g_surface_lock_count > 1) {
		--g_surface_lock_count;
		return;
	}
	if (g_surface_lock_count < 1) {
		g_surface_lock_count = 0;
		return;
	}

	--g_surface_lock_count;
	if (g_flight_page_flip == 0) {
		return;
	}
	display_surface_state = g_flight_display_surfaces_active;
	display_surface_state |= (uint8_t)g_flight_net_clock_lead_ticks;
	if (display_surface_state != 0) {
		if (g_flight_draw_to_hud_layer != 0) {
			g_flight_offscreen_surface->lpVtbl->Unlock(
				g_flight_offscreen_surface, g_surface_pixels);
		} else {
			g_flight_back_buffer->lpVtbl->Unlock(
				g_flight_back_buffer, g_surface_pixels);
		}
	} else {
		g_flight_primary_surface->lpVtbl->Unlock(
			g_flight_primary_surface, g_surface_pixels);
	}
}

/* Sets g_flight_surface_viewport480_byte_span, which nothing reads, and returns
 * it. */
// FUNCTION: XVT 0x4AC780
int flight_surface_set_viewport480_byte_span(int byte_span)
{
	return g_flight_surface_viewport480_byte_span = byte_span;
}

/* Sets g_sw_framebuffer_base and returns it. */
// FUNCTION: XVT 0x4AC790
void *flight_surface_set_software_framebuffer_base(void *framebuffer_base)
{
	return g_sw_framebuffer_base = framebuffer_base;
}

/* Returns g_sw_framebuffer_base. */
// FUNCTION: XVT 0x4AC7A0
void *flight_surface_get_software_framebuffer_base(void)
{
	return g_sw_framebuffer_base;
}
