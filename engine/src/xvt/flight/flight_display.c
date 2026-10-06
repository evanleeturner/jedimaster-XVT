#include "xvt/flight/flight_display.h"

#include <stdio.h>
#include <string.h>

#include "xvt/assets/model_texture.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/render_capture.h"

/* The DirectDraw primary surface: the screen. flight_display_init creates it; in
 * a window it is created only while needed (flight_display_init reads its
 * format, flight_display_flip copies the frame) and released after.
 * flight_display_cleanup_and_report_error and the flight shutdown (flight_main in
 * the original build, xvt_flight_entry_cleanup in the modern one) release it and
 * set NULL. */
// GLOBAL: XVT 0x66E700
IDirectDrawSurface *g_flight_primary_surface;
/* Without page flipping, the memory the cockpit and HUD layer is drawn in
 * (flight_surface_lock hands it out while g_flight_draw_to_hud_layer is set);
 * flight_display_blit_render_surface copies 480 rows of it into
 * g_flight_software_framebuffer. */
// GLOBAL: XVT 0x66E710
uint8_t g_flight_hud_staging_buffer[640 * 480 * 2];
/* Element 0 is the primary surface pitch in bytes. Element 1, which nothing
 * reads, is 2 when the driver can color key with a destination key but not a
 * source key, else 1. */
// GLOBAL: XVT 0x803B70
int g_flight_primary_pitch[2];
/* The surface the 3D view is drawn on and the next flip shows: the primary
 * surface's attached back buffer with page flipping, else the primary itself.
 * Set by flight_display_init in fullscreen only; xvt_flight_entry_cleanup releases
 * it and sets NULL in the modern build. */
// GLOBAL: XVT 0x803F80
IDirectDrawSurface *g_flight_back_buffer;
/* Without page flipping, the frame the 3D view is drawn in; flight_display_flip
 * copies 480 rows of the primary pitch from it to the screen. */
// GLOBAL: XVT 0x803F90
uint8_t g_flight_software_framebuffer[640 * 480 * 2];
/* 1 for exclusive fullscreen with a display mode change, 0 for a window. Starts
 * at 1; the launch options "nofullscreen" and "fullscreen" set it, read by
 * flight_main in the original build and xvt_flight_entry_read_launch_switches in
 * the modern one. */
// GLOBAL: XVT 0x527EA8
int g_flight_fullscreen = 1;
/* 1 when flight_display_flip times its flips to the display's refresh. Starts at
 * 0; at flight start it becomes 0 when a file named flicker.txt can be opened,
 * else 1 (flight_main in the original build, xvt_flight_entry_read_launch_switches
 * in the modern one). */
// GLOBAL: XVT 0x527EA4
int g_flight_conf_flicker = 0;
/* Display mode width copied before the flight's display is set up (flight_main
 * in the original build, xvt_flight_entry_configure_display_size in the modern
 * one); fe_disk_io_init_global_buffers reads it. Starts at 640. */
// GLOBAL: XVT 0x527EBC
int g_render_target_width = 640;
/* g_flight_bytes_per_pixel as it stood before flight_display_init, which may change
 * it; copied by flight_main in the original build and xvt_flight_entry_configure
 * in the modern one, and read by fe_disk_io_init_global_buffers. Starts at 1. */
// GLOBAL: XVT 0x527EC0
int g_requested_flight_bytes_per_pixel = 1;
/* g_use_hardware3d as it stood before flight_display_init, which may clear it;
 * copied and read by the same functions as g_requested_flight_bytes_per_pixel.
 * Starts at 1. */
// GLOBAL: XVT 0x527EC4
int g_requested_flight_hardware3d = 1;
/* timeGetTime, in ms, of the last vertical blank flight_display_flip waited for
 * when timing flips; 0 until the first. Only flight_display_flip writes it. */
// GLOBAL: XVT 0x527F7C
static uint32_t g_flight_flicker_last_sync_time_ms = 0;
/* Width, out of 100,000 parts of a refresh, of the window at either end of the
 * refresh in which flight_display_flip waits for a vertical blank. Nothing
 * changes it from 20,000. */
// GLOBAL: XVT 0x527F80
static int g_flight_flicker_phase_window = 20000;
/* Rate by which flight_display_flip turns elapsed ms into a position within the
 * refresh: the display frequency from GetMonitorFrequency, or, when that fails,
 * 10,000,000 divided by the ms 100 refreshes took. Only flight_display_flip
 * writes it. */
// GLOBAL: XVT 0x622CB0
static int g_flight_flicker_refresh_rate_scale = 0;
/* The 256-entry DirectDraw palette attached to the primary surface in 8-bit
 * fullscreen; NULL otherwise. Created by flight_display_init;
 * flight_display_cleanup_and_report_error and the flight shutdown release it and
 * set NULL. */
// GLOBAL: XVT 0x66DDD8
IDirectDrawPalette *g_flight_palette = NULL;
/* g_flight_back_buffer with page flipping, else g_flight_primary_surface; set by
 * flight_display_init in fullscreen only and cleared by xvt_flight_entry_cleanup.
 * Nothing reads it. */
// GLOBAL: XVT 0x66DDEC
IDirectDrawSurface *g_flight_render_surface;
/* Text flight_display_cleanup_and_report_error formats its error line into before
 * sending it to the debugger output; nothing else uses it. */
// GLOBAL: XVT 0x66E200
static char g_flight_display_debug_message[1280] = {0};
/* Bytes in one bank of the screen memory: the original build's software drawing
 * functions split an offset into bank and offset by it when they draw at the
 * software framebuffer base outside 320x240.
 * flight_display_configure_resolution_state sets it to 480 rows of the primary
 * pitch, or 0x10000 at 320x240 and in an unknown mode. Starts at 0xF000. */
// GLOBAL: XVT 0x5233CC
unsigned int g_vesa_page_size_bytes = 0xF000;
/* Set to 1 by flight_display_configure_resolution_state; starts at 15. Nothing
 * reads it. */
// GLOBAL: XVT 0x5233D0
unsigned int g_vesa_grains_per_page = 15;
/* Folder the cockpit art is loaded from: "CP640\" at start, its digits set by
 * flight_display_configure_resolution_state to "CP320\", "CP640\" or "CP480\" for
 * the resolution. Read by the HUD's cockpit loaders. */
// GLOBAL: XVT 0x52155C
char g_hud_cockpit_resolution_directory[7] = "CP640\\";
/* 240 or 480 for the resolution, set by flight_display_configure_resolution_state.
 * Nothing reads it. */
// GLOBAL: XVT 0x9A7B4C
static int g_flight_resolution_legacy_extent = 0;

/* Sets the drawing state for g_flight_resolution_mode: g_screen_width and
 * g_screen_height (320x240, 640x480 or 480x360, and 320x240 for any other mode),
 * g_surface_pitch from flight_display_get_primary_surface_pitch, g_proj_scale_int (256
 * or 512), g_proj_scale_half_int, g_perspective_shift, g_proj_aspect_y (0),
 * g_flight_resolution_legacy_extent, the digits of
 * g_hud_cockpit_resolution_directory, g_vesa_page_size_bytes and
 * g_vesa_grains_per_page. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x447D90
void flight_display_configure_resolution_state(void)
{
	int primary_surface_pitch = flight_display_get_primary_surface_pitch();
	int resolution_mode = g_flight_resolution_mode;
	g_vesa_page_size_bytes = 480 * primary_surface_pitch;
	g_vesa_grains_per_page = 1;
	switch (resolution_mode) {
	case FLIGHT_RESOLUTION_320X240:
		g_vesa_page_size_bytes = 0x10000;
		g_vesa_grains_per_page = 1;
		g_screen_width = 320;
		g_screen_height = 240;
		g_flight_resolution_legacy_extent = 240;
		g_surface_pitch = flight_display_get_primary_surface_pitch();
		g_proj_scale_int = 256;
		g_proj_scale_half_int = 128;
		g_perspective_shift = 8;
		g_hud_cockpit_resolution_directory[2] = '3';
		g_hud_cockpit_resolution_directory[3] = '2';
		g_proj_aspect_y = 0;
		break;

	case FLIGHT_RESOLUTION_640X480:
		g_screen_width = 640;
		g_screen_height = 480;
		g_flight_resolution_legacy_extent = 480;
		g_surface_pitch = flight_display_get_primary_surface_pitch();
		g_proj_scale_int = 512;
		g_proj_scale_half_int = 256;
		g_perspective_shift = 9;
		g_hud_cockpit_resolution_directory[2] = '6';
		g_hud_cockpit_resolution_directory[3] = '4';
		g_proj_aspect_y = 0;
		break;

	case FLIGHT_RESOLUTION_480X360:
		g_screen_height = 360;
		g_screen_width = 480;
		g_flight_resolution_legacy_extent = 480;
		g_surface_pitch = flight_display_get_primary_surface_pitch();
		g_proj_scale_int = 512;
		g_proj_scale_half_int = 256;
		g_perspective_shift = 9;
		g_hud_cockpit_resolution_directory[2] = '4';
		g_hud_cockpit_resolution_directory[3] = '8';
		g_proj_aspect_y = 0;
		break;

	default:
		XVT_LOG_WARN("display.resolution_unknown mode=%#x",
			     (unsigned)g_flight_resolution_mode);
		g_vesa_page_size_bytes = 0x10000;
		g_screen_width = 320;
		g_screen_height = 240;
		g_flight_resolution_legacy_extent = 240;
		g_surface_pitch = flight_display_get_primary_surface_pitch();
		g_proj_scale_int = 256;
		g_proj_scale_half_int = 128;
		g_perspective_shift = 8;
		g_hud_cockpit_resolution_directory[2] = '3';
		g_hud_cockpit_resolution_directory[3] = '2';
		g_proj_aspect_y = 0;
		break;
	}
	XVT_LOG_DEBUG(
		"display.resolution_set mode=%#x width=%u height=%u pitch=%d scale=%u shift=%d page=%u",
		(unsigned)g_flight_resolution_mode, g_screen_width,
		g_screen_height, g_surface_pitch, (unsigned)g_proj_scale_int,
		(int)g_perspective_shift, g_vesa_page_size_bytes);
}

/* Returns 1 and does nothing else. Its callers treat 0 as a failure (error 12
 * in flight_display_init), which never comes. */
// FUNCTION: XVT 0x4AAFE0
int flight_display_post_primary_surface_create_or_restore_stub(void)
{
	return 1;
}

/* The start of the driver capability block flight_display_init asks DirectDraw
 * for with GetCaps. */
/* drift-ok: camelcase -- the start of DirectDraw's DDCAPS */
struct flight_display_driver_caps {
	uint32_t dwSize; /* Size of this block, set before GetCaps. */
	/* Capability bits; flight_display_init tests 0x400000. */
	uint32_t dwCaps;
	uint32_t dwCaps2; /* Filled by GetCaps; nothing reads it. */
	/* Color key bits; flight_display_init tests 0x1 and 0x200. */
	uint32_t dwCKeyCaps;
	uint32_t reserved[87]; /* The rest of the driver's block, unread. */
};

/* One palette entry as flight_display_init hands it to CreatePalette. */
struct flight_display_palette_entry {
	/* Red, 0 to 255; flight_display_init sets the entry's index. */
	uint8_t red;
	uint8_t green; /* Green, set like red. */
	uint8_t blue;  /* Blue, set like red. */
	uint8_t flags; /* Never set: CreatePalette gets what the stack held. */
};

enum { FLIGHT_DDPCAPS_INITIALIZE = 0x8 };

/* Sets up DirectDraw for flight on the frontend's DirectDraw object
 * (g_flight_direct_draw): exclusive fullscreen or a normal window by
 * g_flight_fullscreen. In fullscreen it sets the display mode g_display_mode_width
 * by g_display_mode_height at 8 times g_flight_bytes_per_pixel bits (2 bytes with
 * g_use_hardware3d). When refused, 320 wide tries 512x384 and then 640x480, 512
 * wide tries 640x480, and then the other pixel size is tried the same way. It
 * creates the primary surface with one back buffer and an offscreen surface of
 * g_surface_width by g_surface_height (g_flight_back_buffer,
 * g_flight_offscreen_surface) when page flipping, else the primary alone serving
 * as both, clears them, and at 1 byte per pixel attaches a gray ramp palette
 * (g_flight_palette, else set to NULL). In a window it creates the primary only
 * to read its format and releases it. Sets g_flight_primary_pitch,
 * g_flight_bytes_per_pixel and g_pixel_format_code (8, 555 or 565) from the surface,
 * and, in fullscreen, g_surface_pitch and g_flight_render_surface. g_use_hardware3d
 * is cleared unless pixels are 2 bytes, and when still set
 * renderer_init_d3d_device runs. Returns 1, or the 0 of
 * flight_display_cleanup_and_report_error with code 2 (cooperative level), 3
 * (display mode), 4 (primary), 5 (back buffer) or 6 (offscreen surface). */
// FUNCTION: XVT 0x4AAFF0
int flight_display_init(void)
{
	g_flight_direct_draw = frontend_display_get_direct_draw();
	HRESULT result;
	if (g_flight_fullscreen != 0) {
		result = g_flight_direct_draw->lpVtbl->SetCooperativeLevel(
			g_flight_direct_draw, g_flight_main_window_handle,
			DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
		if (result != DX_DD_OK) {
			XVT_LOG_ERROR(
				"display.setup_failed call=\"exclusive_level\" result=%#x width=%d height=%d bpp=%d",
				(unsigned)result, g_display_mode_width,
				g_display_mode_height,
				8 * g_flight_bytes_per_pixel);
			return flight_display_cleanup_and_report_error(2);
		}
	} else {
		result = g_flight_direct_draw->lpVtbl->SetCooperativeLevel(
			g_flight_direct_draw, g_flight_main_window_handle,
			DDSCL_NORMAL);
		if (result != DX_DD_OK) {
			XVT_LOG_ERROR(
				"display.setup_failed call=\"normal_level\" result=%#x width=%d height=%d bpp=%d",
				(unsigned)result, g_display_mode_width,
				g_display_mode_height,
				8 * g_flight_bytes_per_pixel);
			return flight_display_cleanup_and_report_error(2);
		}
	}
	if (g_use_hardware3d != 0) {
		g_flight_bytes_per_pixel = 2;
	}
	if (g_flight_fullscreen != 0) {
		result = g_flight_direct_draw->lpVtbl->SetDisplayMode(
			g_flight_direct_draw, g_display_mode_width,
			g_display_mode_height, 8 * g_flight_bytes_per_pixel);
		if (result != DX_DD_OK) {
			XVT_LOG_DEBUG(
				"display.mode_refused width=%d height=%d bpp=%d result=%#x",
				g_display_mode_width, g_display_mode_height,
				8 * g_flight_bytes_per_pixel, (unsigned)result);
			if (g_display_mode_width == 320) {
				g_display_mode_width = 512;
				g_display_mode_height = 384;
				result =
					g_flight_direct_draw->lpVtbl->SetDisplayMode(
						g_flight_direct_draw,
						g_display_mode_width,
						g_display_mode_height,
						8 * g_flight_bytes_per_pixel);
				if (result != DX_DD_OK) {
					g_display_mode_width = 640;
					g_display_mode_height = 480;
					result = g_flight_direct_draw->lpVtbl->SetDisplayMode(
						g_flight_direct_draw,
						g_display_mode_width,
						g_display_mode_height,
						8 * g_flight_bytes_per_pixel);
					if (result != DX_DD_OK) {
						g_display_mode_width = 320;
						g_display_mode_height = 240;
					}
				}
			} else if (g_display_mode_width == 512) {
				g_display_mode_width = 640;
				g_display_mode_height = 480;
				result =
					g_flight_direct_draw->lpVtbl->SetDisplayMode(
						g_flight_direct_draw,
						g_display_mode_width,
						g_display_mode_height,
						8 * g_flight_bytes_per_pixel);
				if (result != DX_DD_OK) {
					g_display_mode_width = 512;
					g_display_mode_height = 384;
				}
			}
			if (result != DX_DD_OK &&
			    g_flight_bytes_per_pixel == 2) {
				g_flight_bytes_per_pixel = 1;
				result =
					g_flight_direct_draw->lpVtbl->SetDisplayMode(
						g_flight_direct_draw,
						g_display_mode_width,
						g_display_mode_height,
						8 * g_flight_bytes_per_pixel);
				if (result != DX_DD_OK) {
					if (g_display_mode_width == 320) {
						g_display_mode_width = 512;
						g_display_mode_height = 384;
						result =
							g_flight_direct_draw
								->lpVtbl
								->SetDisplayMode(
									g_flight_direct_draw,
									g_display_mode_width,
									g_display_mode_height,
									8 * g_flight_bytes_per_pixel);
						if (result != DX_DD_OK) {
							g_display_mode_width =
								640;
							g_display_mode_height =
								480;
							result =
								g_flight_direct_draw
									->lpVtbl
									->SetDisplayMode(
										g_flight_direct_draw,
										g_display_mode_width,
										g_display_mode_height,
										8 * g_flight_bytes_per_pixel);
							if (result !=
							    DX_DD_OK) {
								g_display_mode_width =
									320;
								g_display_mode_height =
									240;
							}
						}
					} else if (g_display_mode_width ==
						   512) {
						g_display_mode_width = 640;
						g_display_mode_height = 480;
						result =
							g_flight_direct_draw
								->lpVtbl
								->SetDisplayMode(
									g_flight_direct_draw,
									g_display_mode_width,
									g_display_mode_height,
									8 * g_flight_bytes_per_pixel);
						if (result != DX_DD_OK) {
							g_display_mode_width =
								512;
							g_display_mode_height =
								384;
						}
					}
				}
			} else if (result != DX_DD_OK &&
				   g_flight_bytes_per_pixel == 1) {
				g_flight_bytes_per_pixel = 2;
				result =
					g_flight_direct_draw->lpVtbl->SetDisplayMode(
						g_flight_direct_draw,
						g_display_mode_width,
						g_display_mode_height,
						8 * g_flight_bytes_per_pixel);
				if (result != DX_DD_OK) {
					if (g_display_mode_width == 320) {
						g_display_mode_width = 512;
						g_display_mode_height = 384;
						result =
							g_flight_direct_draw
								->lpVtbl
								->SetDisplayMode(
									g_flight_direct_draw,
									g_display_mode_width,
									g_display_mode_height,
									8 * g_flight_bytes_per_pixel);
						if (result != DX_DD_OK) {
							g_display_mode_width =
								640;
							g_display_mode_height =
								480;
							result =
								g_flight_direct_draw
									->lpVtbl
									->SetDisplayMode(
										g_flight_direct_draw,
										g_display_mode_width,
										g_display_mode_height,
										8 * g_flight_bytes_per_pixel);
							if (result !=
							    DX_DD_OK) {
								g_display_mode_width =
									320;
								g_display_mode_height =
									240;
							}
						}
					} else if (g_display_mode_width ==
						   512) {
						g_display_mode_width = 640;
						g_display_mode_height = 480;
						result =
							g_flight_direct_draw
								->lpVtbl
								->SetDisplayMode(
									g_flight_direct_draw,
									g_display_mode_width,
									g_display_mode_height,
									8 * g_flight_bytes_per_pixel);
						if (result != DX_DD_OK) {
							g_display_mode_width =
								512;
							g_display_mode_height =
								384;
						}
					}
				}
			}
			if (result != DX_DD_OK) {
				XVT_LOG_ERROR(
					"display.setup_failed call=\"display_mode\" result=%#x width=%d height=%d bpp=%d",
					(unsigned)result, g_display_mode_width,
					g_display_mode_height,
					8 * g_flight_bytes_per_pixel);
				return flight_display_cleanup_and_report_error(
					3);
			}
		}
	}
	if (g_flight_bytes_per_pixel != 2) {
		g_use_hardware3d = 0;
	}

	g_flight_primary_pitch[1] = 1;
	struct flight_display_driver_caps driver_caps;
	memset(&driver_caps, 0, sizeof(driver_caps));
	driver_caps.dwSize = sizeof(driver_caps);
	result = g_flight_direct_draw->lpVtbl->GetCaps(g_flight_direct_draw,
						       &driver_caps, NULL);
	if (result == DX_DD_OK && (driver_caps.dwCaps & 0x400000) != 0 &&
	    (driver_caps.dwCKeyCaps & 1) != 0 &&
	    (driver_caps.dwCKeyCaps & 0x200) == 0) {
		g_flight_primary_pitch[1] = 2;
	}

	DDSURFACEDESC surface_desc;
	if (g_flight_fullscreen != 0) {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		if (g_flight_page_flip != 0) {
			surface_desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
			surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE |
						      DDSCAPS_FLIP |
						      DDSCAPS_COMPLEX;
			surface_desc.dwBackBufferCount = 1;
			if (g_use_hardware3d != 0) {
				surface_desc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
			}
		} else {
			surface_desc.dwFlags = DDSD_CAPS;
			surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			if (g_use_hardware3d != 0) {
				surface_desc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
			}
		}
		result = g_flight_direct_draw->lpVtbl->CreateSurface(
			g_flight_direct_draw, &surface_desc,
			&g_flight_primary_surface, NULL);
		if (result != DX_DD_OK) {
			XVT_LOG_ERROR(
				"display.setup_failed call=\"primary\" result=%#x width=%d height=%d bpp=%d",
				(unsigned)result, g_display_mode_width,
				g_display_mode_height,
				8 * g_flight_bytes_per_pixel);
			return flight_display_cleanup_and_report_error(4);
		}
		g_pixel_format_code = 565;
		if (g_flight_bytes_per_pixel != 2) {
			g_pixel_format_code = 8;
		}
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		g_flight_primary_surface->lpVtbl->GetSurfaceDesc(
			g_flight_primary_surface, &surface_desc);
		g_flight_primary_pitch[0] = surface_desc.lPitch;
		g_surface_pitch = surface_desc.lPitch;
		if ((surface_desc.ddpfPixelFormat.dwFlags &
		     DDPF_PALETTEINDEXED8) != 0) {
			g_flight_bytes_per_pixel = 1;
			g_pixel_format_code = 8;
		} else if ((surface_desc.ddpfPixelFormat.dwFlags & DDPF_RGB) !=
			   0) {
			g_flight_bytes_per_pixel = 2;
			g_pixel_format_code = 565;
			if ((surface_desc.ddpfPixelFormat.dwGBitMask & 0x400) ==
			    0) {
				g_pixel_format_code = 555;
			}
		}
		flight_display_clear_surface(g_flight_primary_surface);

		if (g_flight_page_flip != 0) {
			DDSCAPS attached_caps;
			attached_caps.dwCaps = DDSCAPS_BACKBUFFER;
			result = g_flight_primary_surface->lpVtbl
					 ->GetAttachedSurface(
						 g_flight_primary_surface,
						 &attached_caps,
						 &g_flight_back_buffer);
			if (result != DX_DD_OK) {
				XVT_LOG_ERROR(
					"display.setup_failed call=\"back_buffer\" result=%#x width=%d height=%d bpp=%d",
					(unsigned)result, g_display_mode_width,
					g_display_mode_height,
					8 * g_flight_bytes_per_pixel);
				return flight_display_cleanup_and_report_error(
					5);
			}
			surface_desc.dwFlags =
				DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
			surface_desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
			if (g_use_hardware3d != 0) {
				surface_desc.ddsCaps.dwCaps |=
					DDSCAPS_SYSTEMMEMORY;
			}
			surface_desc.dwWidth = g_surface_width;
			surface_desc.dwHeight = g_surface_height;
			result = g_flight_direct_draw->lpVtbl->CreateSurface(
				g_flight_direct_draw, &surface_desc,
				&g_flight_offscreen_surface, NULL);
			if (result != DX_DD_OK) {
				XVT_LOG_ERROR(
					"display.setup_failed call=\"cockpit_layer\" result=%#x width=%d height=%d bpp=%d",
					(unsigned)result, g_display_mode_width,
					g_display_mode_height,
					8 * g_flight_bytes_per_pixel);
				return flight_display_cleanup_and_report_error(
					6);
			}
			flight_display_clear_surface(g_flight_back_buffer);
			g_flight_render_surface = g_flight_back_buffer;
			flight_display_clear_surface(
				g_flight_offscreen_surface);
		} else {
			g_flight_render_surface = g_flight_primary_surface;
			g_flight_back_buffer = g_flight_primary_surface;
		}
		if (flight_display_post_primary_surface_create_or_restore_stub() ==
		    0) {
			return flight_display_cleanup_and_report_error(12);
		}
	} else {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		surface_desc.dwFlags = DDSD_CAPS;
		surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		result = g_flight_direct_draw->lpVtbl->CreateSurface(
			g_flight_direct_draw, &surface_desc,
			&g_flight_primary_surface, NULL);
		if (result != DX_DD_OK) {
			XVT_LOG_ERROR(
				"display.setup_failed call=\"window_primary\" result=%#x width=%d height=%d bpp=%d",
				(unsigned)result, g_display_mode_width,
				g_display_mode_height,
				8 * g_flight_bytes_per_pixel);
			return flight_display_cleanup_and_report_error(4);
		}
		if (flight_display_post_primary_surface_create_or_restore_stub() ==
		    0) {
			return flight_display_cleanup_and_report_error(12);
		}
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		g_flight_primary_surface->lpVtbl->GetSurfaceDesc(
			g_flight_primary_surface, &surface_desc);
		g_flight_primary_pitch[0] = surface_desc.lPitch;
		if ((surface_desc.ddpfPixelFormat.dwFlags &
		     DDPF_PALETTEINDEXED8) != 0) {
			g_flight_bytes_per_pixel = 1;
			g_pixel_format_code = 8;
		} else if ((surface_desc.ddpfPixelFormat.dwFlags & DDPF_RGB) !=
			   0) {
			g_flight_bytes_per_pixel = 2;
			if ((surface_desc.ddpfPixelFormat.dwGBitMask & 0x400) !=
			    0) {
				g_pixel_format_code = 565;
			} else {
				g_pixel_format_code = 555;
			}
		} else {
			g_flight_bytes_per_pixel = 1;
			g_pixel_format_code = 8;
		}
		if (g_flight_primary_surface != NULL) {
			g_flight_primary_surface->lpVtbl->Release(
				g_flight_primary_surface);
			g_flight_primary_surface = NULL;
		}
	}

	if (g_flight_fullscreen != 0) {
		if (g_flight_bytes_per_pixel == 1) {
			struct flight_display_palette_entry
				palette_entries[256];
			for (int palette_index = 0; palette_index < 256;
			     ++palette_index) {
				palette_entries[palette_index].red =
					(uint8_t)palette_index;
				palette_entries[palette_index].green =
					(uint8_t)palette_index;
				palette_entries[palette_index].blue =
					(uint8_t)palette_index;
			}
			g_flight_direct_draw->lpVtbl->CreatePalette(
				g_flight_direct_draw,
				DDPCAPS_8BIT | FLIGHT_DDPCAPS_INITIALIZE |
					DDPCAPS_ALLOW256,
				palette_entries, &g_flight_palette, NULL);
			if (g_flight_palette != NULL) {
				g_flight_primary_surface->lpVtbl->SetPalette(
					g_flight_primary_surface,
					g_flight_palette);
			} else {
				XVT_LOG_WARN("display.palette_missing");
			}
		} else {
			g_flight_palette = NULL;
		}
	}
	if (g_use_hardware3d != 0) {
		renderer_init_d3d_device();
	}
	XVT_LOG_INFO(
		"display.started fullscreen=%d width=%d height=%d bpp=%d format=%d back_buffer=%d hardware3d=%d",
		g_flight_fullscreen, g_display_mode_width,
		g_display_mode_height, 8 * g_flight_bytes_per_pixel,
		g_pixel_format_code, g_flight_page_flip, g_use_hardware3d);
	return 1;
}

/* Loads entry_count palette entries from first_entry, given as 6-bit RGB
 * triplets, into the display palette. In fullscreen with g_flight_palette it
 * shifts each value up 2 bits and calls DirectDraw SetEntries, returning the
 * low byte of its result; otherwise it calls
 * flight_display_write_vga_palette_entries and returns what that returns. On the
 * DirectDraw path rgbData is read from entry 0 and the local table is filled
 * from first_entry but passed from its start, so it is right only for first_entry
 * 0, which is what its one caller, flight_palette_apply_to_display, passes, with
 * 256 entries. */
// FUNCTION: XVT 0x4AB890
uint8_t flight_display_set_palette_entries(const uint8_t *rgb_data,
					   int first_entry, int entry_count)
{
	XVT_LOG_DEBUG(
		"display.palette_set first=%d count=%d fullscreen=%d palette=%d predicted=%d",
		first_entry, entry_count, g_flight_fullscreen,
		(int)(g_flight_palette != NULL),
		g_flight_sim_side_effects_suppressed);
	if (g_flight_fullscreen != 0) {
		int count = entry_count;
		if (g_flight_palette != NULL) {
			int first = first_entry;
			uint32_t palette_entries[256];
			if (first < first + count) {
				rgb_data += 3 * first;
				unsigned int remaining = count;
				uint8_t *destination =
					(uint8_t *)&palette_entries[first];
				do {
					destination[0] = rgb_data[0] << 2;
					destination[1] = rgb_data[1] << 2;
					destination[2] = rgb_data[2] << 2;
					rgb_data += 3;
					destination += 4;
				} while (--remaining != 0);
			}

			return (uint8_t)g_flight_palette->lpVtbl->SetEntries(
				g_flight_palette, 0, first, count,
				palette_entries);
		}

		return flight_display_write_vga_palette_entries(
			rgb_data, first_entry, count);
	}

	return flight_display_write_vga_palette_entries(rgb_data, first_entry,
							entry_count);
}

/* Reports that flight could not start: sends a ___CleanupAndExit line with
 * errorCode to the debugger output (debug_printf in the modern build), releases
 * g_flight_primary_surface, g_flight_palette and, with page flipping,
 * g_flight_offscreen_surface, setting each to NULL, shuts the network session
 * down (net_session_shutdown), and shows "Game could not start" (a message box
 * in the original build, a debug_printf line in the modern one). Returns 0. */
// FUNCTION: XVT 0x4AB970
int flight_display_cleanup_and_report_error(int error_code)
{
	snprintf(g_flight_display_debug_message,
		 sizeof(g_flight_display_debug_message),
		 "___CleanupAndExit  err = %d\n", error_code);
	XVT_LOG_DEBUG("flight.display_cleanup error=%d", error_code);
	IDirectDrawSurface *surface = g_flight_primary_surface;
	if (surface != NULL) {
		surface->lpVtbl->Release(surface);
		g_flight_primary_surface = NULL;
	}
	IDirectDrawPalette *palette = g_flight_palette;
	if (palette != NULL) {
		palette->lpVtbl->Release(palette);
		g_flight_palette = NULL;
	}
	int page_flip = g_flight_page_flip;
	if (page_flip != 0) {
		surface = g_flight_offscreen_surface;
		if (surface != NULL) {
			surface->lpVtbl->Release(surface);
			g_flight_offscreen_surface = NULL;
		}
	}
	net_session_shutdown();
	XVT_LOG_ERROR("flight.start_failed error=%d", error_code);
	return 0;
}

/* Returns g_flight_primary_pitch[0]: the pitch in bytes of the surface last
 * locked or described. */
// FUNCTION: XVT 0x4ABA10
int flight_display_get_primary_surface_pitch(void)
{
	return g_flight_primary_pitch[0];
}

/* Shows the finished frame. With page flipping it flips the primary surface
 * (DDFLIP_WAIT). With g_flight_conf_flicker set it first lines the flip up with
 * the display's refresh: the first call waits for a vertical blank to begin,
 * records the time in g_flight_flicker_last_sync_time_ms and sets
 * g_flight_flicker_refresh_rate_scale; later calls take the refresh rate scale
 * times the ms since that time, modulo 100,000, and when that lies within
 * g_flight_flicker_phase_window of either end and no blank is under way, wait for
 * the next blank and record its time. When the flip reports no exclusive mode
 * it takes exclusive mode, flips again (restoring the primary, back buffer and
 * z-buffer when lost) and returns to the normal cooperative level, returning
 * that last call's result; the original build stops in the debugger at each
 * failure there. When the flip reports a lost surface it restores the primary
 * (flight_display_restore_primary_surface) and returns 1 when that succeeded, 0
 * when it failed, without flipping again. Otherwise it returns the flip's
 * result. Without page flipping it copies 480 rows of the primary pitch from
 * g_flight_software_framebuffer to the primary surface, which in a window it
 * creates first and releases after, and returns the failing lock's error, the
 * unlock's or release's result, or, when the window's primary cannot be
 * created, flight_display_cleanup_and_report_error's 0. The modern build reports
 * each flip or copy to xvt_render_capture_presented. */
// FUNCTION: XVT 0x4ABEE0
HRESULT flight_display_flip(void)
{
	HRESULT result;

	if (g_flight_page_flip != 0) {
		if (g_flight_conf_flicker != 0) {
			int vertical_blank_status;
			if (g_flight_flicker_last_sync_time_ms == 0) {
				if (g_flight_direct_draw->lpVtbl
					    ->GetVerticalBlankStatus(
						    g_flight_direct_draw,
						    &vertical_blank_status) ==
				    DX_DD_OK) {
					while (vertical_blank_status != 0 &&
					       g_flight_direct_draw->lpVtbl->GetVerticalBlankStatus(
						       g_flight_direct_draw,
						       &vertical_blank_status) ==
						       DX_DD_OK) {
					}
					while (vertical_blank_status == 0 &&
					       g_flight_direct_draw->lpVtbl->GetVerticalBlankStatus(
						       g_flight_direct_draw,
						       &vertical_blank_status) ==
						       DX_DD_OK) {
					}
				}

				g_flight_flicker_last_sync_time_ms =
					timeGetTime();
				if (g_flight_direct_draw->lpVtbl->GetMonitorFrequency(
					    g_flight_direct_draw,
					    (uint32_t
						     *)&g_flight_flicker_refresh_rate_scale) !=
				    DX_DD_OK) {
					for (int i = 0; i < 100; ++i) {
						while (vertical_blank_status !=
							       0 &&
						       g_flight_direct_draw
								       ->lpVtbl
								       ->GetVerticalBlankStatus(
									       g_flight_direct_draw,
									       &vertical_blank_status) ==
							       DX_DD_OK) {
						}
						while (vertical_blank_status ==
							       0 &&
						       g_flight_direct_draw
								       ->lpVtbl
								       ->GetVerticalBlankStatus(
									       g_flight_direct_draw,
									       &vertical_blank_status) ==
							       DX_DD_OK) {
						}
					}
					g_flight_flicker_refresh_rate_scale =
						10000000 /
						(int)(timeGetTime() -
						      g_flight_flicker_last_sync_time_ms);
				}
				XVT_LOG_DEBUG(
					"display.flip_timing rate=%d",
					g_flight_flicker_refresh_rate_scale);
			} else {
				int phase =
					(int)(g_flight_flicker_refresh_rate_scale *
					      (timeGetTime() -
					       g_flight_flicker_last_sync_time_ms)) %
					100000;
				if ((g_flight_flicker_phase_window > phase ||
				     100000 - g_flight_flicker_phase_window <
					     phase) &&
				    g_flight_direct_draw->lpVtbl
						    ->GetVerticalBlankStatus(
							    g_flight_direct_draw,
							    &vertical_blank_status) ==
					    DX_DD_OK &&
				    vertical_blank_status == 0) {
					while (vertical_blank_status == 0 &&
					       g_flight_direct_draw->lpVtbl->GetVerticalBlankStatus(
						       g_flight_direct_draw,
						       &vertical_blank_status) ==
						       DX_DD_OK) {
					}
					g_flight_flicker_last_sync_time_ms =
						timeGetTime();
				}
			}
		}

		HRESULT flip_result = g_flight_primary_surface->lpVtbl->Flip(
			g_flight_primary_surface, NULL, DDFLIP_WAIT);
		xvt_render_capture_presented(flip_result == DX_DD_OK);
		if (flip_result == DX_DDERR_NOEXCLUSIVEMODE) {
			XVT_LOG_WARN("display.exclusive_lost");
			result = g_flight_direct_draw->lpVtbl
					 ->SetCooperativeLevel(
						 g_flight_direct_draw,
						 g_flight_main_window_handle,
						 DDSCL_FULLSCREEN |
							 DDSCL_EXCLUSIVE |
							 DDSCL_ALLOWMODEX);
			if (result != DX_DD_OK) {
				XVT_LOG_ERROR(
					"flight.cooperative_level_failed mode=\"fullscreen\" result=%d",
					result);
			}

			result = g_flight_primary_surface->lpVtbl->Flip(
				g_flight_primary_surface, NULL, DDFLIP_WAIT);
			xvt_render_capture_presented(result == DX_DD_OK);
			if (result == DX_DDERR_SURFACELOST) {
				XVT_LOG_WARN("display.surfaces_restored");
				g_flight_primary_surface->lpVtbl->Restore(
					g_flight_primary_surface);
				g_flight_back_buffer->lpVtbl->Restore(
					g_flight_back_buffer);
				g_std3dz_buffer_surface->lpVtbl->Restore(
					g_std3dz_buffer_surface);
				result = g_flight_primary_surface->lpVtbl->Flip(
					g_flight_primary_surface, NULL,
					DDFLIP_WAIT);
				xvt_render_capture_presented(result ==
							     DX_DD_OK);
			}
			if (result != DX_DD_OK) {
				XVT_LOG_ERROR("flight.flip_failed result=%d",
					      result);
			}

			/* From here flip_result holds the result of returning
			 * to the normal cooperative level, not of a flip; on
			 * this path the function returns it and drops the
			 * retried flip's result. */
			flip_result =
				g_flight_direct_draw->lpVtbl
					->SetCooperativeLevel(
						g_flight_direct_draw,
						g_flight_main_window_handle,
						DDSCL_NORMAL);
			if (flip_result != DX_DD_OK) {
				XVT_LOG_ERROR(
					"flight.cooperative_level_failed mode=\"normal\" result=%d",
					flip_result);
			}
		}

		result = flip_result;
		if (flip_result == DX_DDERR_SURFACELOST) {
			result = flight_display_restore_primary_surface();
			XVT_LOG_WARN(
				"display.surface_lost where=\"flip\" restored=%d",
				(int)result);
			if (result != DX_DD_OK) {
				return flight_display_post_primary_surface_create_or_restore_stub();
			}
		}
	} else {
		DDSURFACEDESC surface_desc;
		if (g_flight_fullscreen == 0) {
			memset(&surface_desc, 0, sizeof(surface_desc));
			surface_desc.dwSize = sizeof(surface_desc);
			surface_desc.dwFlags = DDSD_CAPS;
			surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			result = g_flight_direct_draw->lpVtbl->CreateSurface(
				g_flight_direct_draw, &surface_desc,
				&g_flight_primary_surface, NULL);
			if (result != DX_DD_OK) {
				XVT_LOG_ERROR(
					"display.present_failed step=\"window_primary\" result=%#x",
					(unsigned)result);
				return flight_display_cleanup_and_report_error(
					4);
			}
			if (flight_display_post_primary_surface_create_or_restore_stub() ==
			    0) {
				return flight_display_cleanup_and_report_error(
					12);
			}
		}

		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		g_flight_primary_surface->lpVtbl->GetSurfaceDesc(
			g_flight_primary_surface, &surface_desc);
		g_flight_primary_pitch[0] = surface_desc.lPitch;
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		for (;;) {
			result = g_flight_primary_surface->lpVtbl->Lock(
				g_flight_primary_surface, NULL, &surface_desc,
				0, NULL);
			if (result == DX_DD_OK) {
				break;
			}
			if (result != DX_DDERR_WASSTILLDRAWING) {
				XVT_LOG_ERROR(
					"display.present_failed step=\"lock\" result=%#x",
					(unsigned)result);
				return result;
			}
		}

		memcpy(surface_desc.lpSurface, g_flight_software_framebuffer,
		       480 * flight_display_get_primary_surface_pitch());
		result = g_flight_primary_surface->lpVtbl->Unlock(
			g_flight_primary_surface, surface_desc.lpSurface);
		if (result != DX_DD_OK) {
			XVT_LOG_ERROR(
				"display.present_failed step=\"unlock\" result=%#x",
				(unsigned)result);
		}
		xvt_render_capture_presented(result == DX_DD_OK);
		if (g_flight_fullscreen == 0 &&
		    g_flight_primary_surface != NULL) {
			result = g_flight_primary_surface->lpVtbl->Release(
				g_flight_primary_surface);
			g_flight_primary_surface = NULL;
		}
	}
	return result;
}

/* Does nothing. */
// FUNCTION: XVT 0x4AC250
void nullsub_11(void) {}

/* Lays the cockpit and HUD layer over the frame. With page flipping it copies
 * g_flight_offscreen_surface onto g_flight_back_buffer, centered in the display
 * mode, retrying while DirectDraw is still drawing, and returns the copy's
 * result; after a lost surface it restores the primary and returns 1 when that
 * succeeded, 0 when it failed, without copying again. Without page flipping it
 * copies 480 rows of the primary pitch from g_flight_hud_staging_buffer into
 * g_flight_software_framebuffer and returns that pitch. The modern build then
 * calls xvt_cockpit_latch_composition, unless a page flipping copy failed. */
// FUNCTION: XVT 0x4AC260
int flight_display_blit_render_surface(void)
{
	HRESULT result;
	uint32_t destination_rect[4];
	uint32_t source_rect[4];

	if (g_flight_page_flip != 0) {
		DDBLTFX effects;
		memset(&effects, 0, sizeof(effects));
		effects.dwSize = sizeof(effects);
		effects.dwROP = 0x00CC0020;
		HRESULT blt_result;
		do {
			destination_rect[0] =
				(unsigned int)(g_display_mode_width -
					       g_surface_width) >>
				1;
			destination_rect[1] =
				(unsigned int)(g_display_mode_height -
					       g_surface_height) >>
				1;
			destination_rect[2] =
				g_surface_width + destination_rect[0];
			destination_rect[3] =
				g_surface_height + destination_rect[1];
			source_rect[0] = 0;
			source_rect[1] = 0;
			source_rect[2] = g_surface_width;
			source_rect[3] = g_surface_height;
			blt_result = g_flight_back_buffer->lpVtbl->Blt(
				g_flight_back_buffer, destination_rect,
				g_flight_offscreen_surface, source_rect,
				DDBLT_ROP, &effects);
			result = blt_result;
			if (blt_result == DX_DD_OK) {
				break;
			}
			if (blt_result == DX_DDERR_SURFACELOST) {
				result =
					flight_display_restore_primary_surface();
				XVT_LOG_WARN(
					"display.surface_lost where=\"overlay\" restored=%d",
					(int)result);
				if (result == 0) {
					return result;
				}
				result =
					flight_display_post_primary_surface_create_or_restore_stub();
			}
		} while (blt_result == DX_DDERR_WASSTILLDRAWING);
	} else {
		result = flight_display_get_primary_surface_pitch();
		memcpy(g_flight_software_framebuffer,
		       g_flight_hud_staging_buffer, 480 * result);
	}
	if (g_flight_page_flip == 0 || result == DX_DD_OK) {
		xvt_cockpit_latch_composition();
	} else {
		XVT_LOG_ERROR("display.overlay_copy_failed result=%#x",
			      (unsigned)result);
	}
	return result;
}

/* Does nothing. */
// FUNCTION: XVT 0x4AC380
void flight_display_apply_resolution_mode_backend_stub(int resolution_mode)
{
	(void)resolution_mode;
}

/* Fills g_flight_back_buffer with color 0 by a DirectDraw color fill, retrying
 * while DirectDraw is still drawing; after a lost surface it restores the
 * primary and stops without filling. Nothing calls this. */
// FUNCTION: XVT 0x4AC3A0
void flight_display_clear_back_buffer(void)
{
	DDBLTFX effects;

	effects.dwSize = sizeof(effects);
	effects.dwFillColor = 0;
	HRESULT blt_result;
	do {
		blt_result = g_flight_back_buffer->lpVtbl->Blt(
			g_flight_back_buffer, NULL, NULL, NULL, DDBLT_COLORFILL,
			&effects);
		if (blt_result == DX_DD_OK) {
			break;
		}
		if (blt_result == DX_DDERR_SURFACELOST) {
			if (flight_display_restore_primary_surface() == 0) {
				return;
			}
			flight_display_post_primary_surface_create_or_restore_stub();
		}
	} while (blt_result == DX_DDERR_WASSTILLDRAWING);
}

/* Fills a DirectDraw surface with color 0, as flight_display_clear_back_buffer
 * does for the back buffer: it retries while DirectDraw is still drawing and
 * after a lost surface restores the primary surface, not the one given, and
 * stops without filling. */
// FUNCTION: XVT 0x4AC400
void flight_display_clear_surface(IDirectDrawSurface *surface)
{
	DDBLTFX effects;

	effects.dwSize = sizeof(effects);
	effects.dwFillColor = 0;
	HRESULT blt_result;
	do {
		blt_result = surface->lpVtbl->Blt(surface, NULL, NULL, NULL,
						  DDBLT_COLORFILL, &effects);
		if (blt_result == DX_DD_OK) {
			break;
		}
		if (blt_result == DX_DDERR_SURFACELOST) {
			if (flight_display_restore_primary_surface() == 0) {
				XVT_LOG_WARN("display.clear_failed result=%#x",
					     (unsigned)blt_result);
				return;
			}
			flight_display_post_primary_surface_create_or_restore_stub();
		}
	} while (blt_result == DX_DDERR_WASSTILLDRAWING);
	if (blt_result != DX_DD_OK) {
		XVT_LOG_WARN("display.clear_failed result=%#x",
			     (unsigned)blt_result);
	}
}

/* Restores a lost g_flight_primary_surface; returns 1 on success, else 0. */
// FUNCTION: XVT 0x4AC460
int flight_display_restore_primary_surface(void)
{
	return g_flight_primary_surface->lpVtbl->Restore(
		       g_flight_primary_surface) == DX_DD_OK;
}

/* Returns 1 when 16-bit pixels are laid out 5-5-5: the frontend's answer while
 * g_flight_render_to_frontend is set, the hardware texture format while a model
 * loads with hardware 3D (g_loading_model, g_use_hardware3d), else whether
 * g_pixel_format_code is 555. */
// FUNCTION: XVT 0x4AC4B0
int display_is_pixel_format555(void)
{
	if (g_flight_render_to_frontend != 0) {
		return frontend_display_get_pixel_format555();
	}
	if (g_loading_model != 0 && g_use_hardware3d != 0) {
		return model_texture_is_hardware_format555();
	}
	return g_pixel_format_code == 555;
}

/* Applies nothing: it returns 1 through
 * flight_display_apply_resolution_mode_internal_stub, which calls
 * flight_display_apply_resolution_mode_backend_stub, which does nothing. */
// FUNCTION: XVT 0x4AC7B0
int flight_display_apply_resolution_mode_stub(int resolution_mode)
{
	return flight_display_apply_resolution_mode_internal_stub(
		resolution_mode, 0);
}

/* Calls flight_display_apply_resolution_mode_backend_stub, which does nothing, and
 * returns 1; flags is ignored. */
// FUNCTION: XVT 0x4AC7C0
int flight_display_apply_resolution_mode_internal_stub(int resolution_mode,
						       int flags)
{
	(void)flags;
	flight_display_apply_resolution_mode_backend_stub(resolution_mode);
	return 1;
}

/* In the original build, writes entry_count RGB triplets to the VGA palette
 * registers from first_entry (port 0x3C8 for the index, 0x3C9 for the data)
 * after waiting for a vertical retrace to begin (bit 3 of port 0x3DA), and
 * returns the last blue value written, or 0 for no entries. The modern build
 * does nothing and returns 0. */
// FUNCTION: XVT 0x4AC7E0
uint8_t flight_display_write_vga_palette_entries(const uint8_t *rgb_entries,
						 int16_t first_entry,
						 int16_t entry_count)
{

	(void)rgb_entries;
	(void)first_entry;
	(void)entry_count;
	return 0;
}
