#include "xvt/frontend/frontend_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "aeron/dialog.h"
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/flight/flight.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_bootstrap.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"
#include "xvt_runtime/compat/win_message_port.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/frontend_task.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_frontend.h"

typedef HRESULT(AERON_DXAPI *frontend_display_surface_get_dc_func)(
	IDirectDrawSurface *surface, void **dc);
typedef HRESULT(AERON_DXAPI *frontend_display_surface_release_dc_func)(
	IDirectDrawSurface *surface, void *dc);

#pragma pack(push, 1)

/* A .bmp file's first header, read by frontend_display_load_palette only to step
 * past it. */
struct frontend_display_bmp_file_header {
	uint16_t signature;    /* "BM" in a bitmap; not checked. */
	uint32_t file_size;    /* The file's size in bytes; not read. */
	uint16_t reserved0;    /* Not read. */
	uint16_t reserved1;    /* Not read. */
	uint32_t pixel_offset; /* Where the pixels start; not read. */
};

/* A .bmp file's info header, the 40-byte form; frontend_display_load_palette
 * reads its palette size from it. */
struct frontend_display_bmp_info_header {
	uint32_t header_size; /* Must be 40, this struct's size, to be used. */
	int32_t width;	      /* Image width in pixels; not read. */
	int32_t height;	      /* Image height in pixels; not read. */
	uint16_t planes;      /* Not read. */
	uint16_t bits_per_pixel;    /* Over 8 means no palette is taken. */
	uint32_t compression;	    /* Not read. */
	uint32_t image_size;	    /* Not read. */
	int32_t pixels_per_meter_x; /* Not read. */
	int32_t pixels_per_meter_y; /* Not read. */
	/* Palette entries in the file; 0 means 1 << bitsPerPixel. */
	uint32_t colors_used;
	uint32_t colors_important; /* Not read. */
};

#pragma pack(pop)
typedef char xvt_size_frontend_display_bmp_file_header
	[(sizeof(struct frontend_display_bmp_file_header) == 14) ? 1 : -1];
typedef char xvt_size_frontend_display_bmp_info_header
	[(sizeof(struct frontend_display_bmp_info_header) == 40) ? 1 : -1];

/* 1 once frontend_display_shutdown has run, so a second call does nothing. Only
 * Shutdown sets it to 1; xvt_frontend_task_init sets it to 0. */
// GLOBAL: XVT 0x52BA8C
int g_shutdown_complete = 0;
/* The window's title and class name, also the title of the message boxes; never
 * written. */
// GLOBAL: XVT 0x52BA90
static char g_window_name[] = "X-Wing vs. TIE Fighter";
/* Squares: entry n is n * n, for n from 0 to 255. frontend_display_pack_rgb sums
 * three of them as the distance between two colors. */
// GLOBAL: XVT 0x52BB40
const unsigned int g_color_dist_lut[256] = {
	0,     1,     4,     9,	    16,	   25,	  36,	 49,	64,    81,
	100,   121,   144,   169,   196,   225,	  256,	 289,	324,   361,
	400,   441,   484,   529,   576,   625,	  676,	 729,	784,   841,
	900,   961,   1024,  1089,  1156,  1225,  1296,	 1369,	1444,  1521,
	1600,  1681,  1764,  1849,  1936,  2025,  2116,	 2209,	2304,  2401,
	2500,  2601,  2704,  2809,  2916,  3025,  3136,	 3249,	3364,  3481,
	3600,  3721,  3844,  3969,  4096,  4225,  4356,	 4489,	4624,  4761,
	4900,  5041,  5184,  5329,  5476,  5625,  5776,	 5929,	6084,  6241,
	6400,  6561,  6724,  6889,  7056,  7225,  7396,	 7569,	7744,  7921,
	8100,  8281,  8464,  8649,  8836,  9025,  9216,	 9409,	9604,  9801,
	10000, 10201, 10404, 10609, 10816, 11025, 11236, 11449, 11664, 11881,
	12100, 12321, 12544, 12769, 12996, 13225, 13456, 13689, 13924, 14161,
	14400, 14641, 14884, 15129, 15376, 15625, 15876, 16129, 16384, 16641,
	16900, 17161, 17424, 17689, 17956, 18225, 18496, 18769, 19044, 19321,
	19600, 19881, 20164, 20449, 20736, 21025, 21316, 21609, 21904, 22201,
	22500, 22801, 23104, 23409, 23716, 24025, 24336, 24649, 24964, 25281,
	25600, 25921, 26244, 26569, 26896, 27225, 27556, 27889, 28224, 28561,
	28900, 29241, 29584, 29929, 30276, 30625, 30976, 31329, 31684, 32041,
	32400, 32761, 33124, 33489, 33856, 34225, 34596, 34969, 35344, 35721,
	36100, 36481, 36864, 37249, 37636, 38025, 38416, 38809, 39204, 39601,
	40000, 40401, 40804, 41209, 41616, 42025, 42436, 42849, 43264, 43681,
	44100, 44521, 44944, 45369, 45796, 46225, 46656, 47089, 47524, 47961,
	48400, 48841, 49284, 49729, 50176, 50625, 51076, 51529, 51984, 52441,
	52900, 53361, 53824, 54289, 54756, 55225, 55696, 56169, 56644, 57121,
	57600, 58081, 58564, 59049, 59536, 60025, 60516, 61009, 61504, 62001,
	62500, 63001, 63504, 64009, 64516, 65025,
};
/* The DirectDraw driver GUID read from video.cfg; only
 * frontend_display_load_driver_guid writes it. */
// GLOBAL: XVT 0x665420
static DxGuid g_configured_direct_draw_driver_guid = {0};

/* The flight display's pixel format: 8 for 8-bit palette color, 565 or 555 for
 * the two 16-bit layouts. Starts at 8; only flight_display_init writes it.
 * display_is_pixel_format555 reads it while g_flight_render_to_frontend is 0. */
// GLOBAL: XVT 0x527EB8
int g_pixel_format_code = 8;
/* 1 while flight-side drawing goes to the frontend's surfaces:
 * flight_surface_lock then takes g_draw_surface_ptr and its pitch, and
 * display_is_pixel_format555 asks the frontend. Set to 0 while a flight runs
 * and back to 1 after it, by xvt_flight_entry_prepare and
 * xvt_flight_entry_cleanup. */
// GLOBAL: XVT 0x527EA0
int g_flight_render_to_frontend = 1;
/* 1 makes the frontend copy a system-memory back buffer to the screen instead
 * of flipping, and drop to normal cooperative level after making its surfaces.
 * xvt_frontend_task_init sets it to 0. */
// GLOBAL: XVT 0xB69CB0
int g_opt_no_fullscreen = 0;
/* 1 makes the frontend draw to a 640 by 480 system-memory back buffer and copy
 * it to the primary surface each frame instead of flipping.
 * xvt_frontend_task_init sets it to 1. */
// GLOBAL: XVT 0xB69CBC
int g_no_page_flip = 0;
/* The command line the frontend was given; frontend_load_resources hands it to
 * pilot_parse_command_line. xvt_frontend_task_init points it at an empty
 * string. */
// GLOBAL: XVT 0xB69CAC
char *g_cmd_line;
/* 1 to skip the opening movie and credits: the frontend then starts at the
 * concourse. xvt_frontend_task_init sets it from its skip_intro argument;
 * concourse_update reads it. */
// GLOBAL: XVT 0xB69CB8
int g_opt_skip_intro;
/* 1 when the command line holds "ishost": the frontend then starts at the
 * concourse. concourse_update reads it and sets it to 0, as
 * pilot_parse_command_line does. */
// GLOBAL: XVT 0xB69CA8
int g_opt_is_host;
/* 1 when the command line holds "isclient": the frontend then starts at the
 * concourse. concourse_update reads it and sets it to 0, as
 * pilot_parse_command_line does. */
// GLOBAL: XVT 0xB69CB4
int g_opt_is_client;
/* Heap buffer for the screen pixels under the cursor sprite, 2 bytes for each
 * pixel of the "cursor" image: frontend_load_resources allocates it and hands
 * it to frontend_cursor_set_image_from_resource_name as the save buffer. Freed
 * by xvt_frontend_task_shutdown. */
// GLOBAL: XVT 0xB6A2AC
void *g_cursor_save_buffer;
/* Set to 1 by frontend_load_resources, the main frontend's start, and never set
 * back; nothing reads it. */
// GLOBAL: XVT 0x52BA5C
int g_game_main_skip_intro_relaunch_gate;

/* Restores the primary surface and, when that succeeds, the back buffer when
 * g_opt_no_fullscreen or g_no_page_flip is set, else the offscreen surface. Returns
 * the last Restore result, the primary's failure when it fails. */
// FUNCTION: XVT 0x4D37E0
HRESULT frontend_display_restore_lost_surfaces(void)
{
	XVT_LOG_WARN("display.menu_surfaces_lost");
	HRESULT result = g_front_state.primary_surface->lpVtbl->Restore(
		g_front_state.primary_surface);
	if (result == 0) {
		if (g_opt_no_fullscreen != 0 || g_no_page_flip != 0) {
			return g_front_state.back_buffer_surface->lpVtbl
				->Restore(g_front_state.back_buffer_surface);
		}
		return g_front_state.offscreen_surface->lpVtbl->Restore(
			g_front_state.offscreen_surface);
	}
	return result;
}

/* Tears the frontend down once: does nothing when g_shutdown_complete is set,
 * else sets it, shuts down DirectSound and frees the fonts, the saved pixels of
 * the stacked screens below the top, every registered image, the sound tables,
 * the image table, the offscreen backup buffer and the string table. With
 * DirectDraw it then flips to the GDI surface, restores the display mode,
 * releases the primary surface, the palette, the offscreen surface, the
 * separate back buffer (with g_opt_no_fullscreen or g_no_page_flip) and
 * DirectDraw itself, and, with b_destroy_window nonzero and a window, forgets
 * the window handle. Last it hides the system cursor. */
// FUNCTION: XVT 0x4D3820
void frontend_display_shutdown(int b_destroy_window)
{
	if (g_shutdown_complete != 0) {
		return;
	}
	g_shutdown_complete = 1;
	XVT_LOG_DEBUG(
		"display.menu_shutdown depth=%d direct_draw=%d surfaces=%d destroy=%d",
		g_front_state.screen_stack_top,
		(int)(g_front_state.direct_draw != NULL),
		(int)(g_front_state.primary_surface != NULL), b_destroy_window);
	frontend_sound_shutdown_direct_sound();
	frontend_text_free_all_fonts();
	for (int screen_index = 0;
	     screen_index < g_front_state.screen_stack_top; screen_index++) {
		if (g_front_state.screen_states[screen_index]
			    .saved_image.pixels != NULL) {
			free(g_front_state.screen_states[screen_index]
				     .saved_image.pixels);
			g_front_state.screen_states[screen_index]
				.saved_image.pixels = NULL;
			g_front_state.screen_states[screen_index]
				.saved_image.pixel_data_bytes = 0;
		}
	}
	front_image_free_all_resources();
	if (g_front_state.frontend_sound_buffers != NULL) {
		free(g_front_state.frontend_sound_buffers);
		g_front_state.frontend_sound_buffers = NULL;
	}
	if (g_front_state.frontend_sound_voices != NULL) {
		free(g_front_state.frontend_sound_voices);
		g_front_state.frontend_sound_voices = NULL;
	}
	if (g_front_state.resource_table != NULL) {
		free(g_front_state.resource_table);
		g_front_state.resource_table = NULL;
	}
	if (g_front_state.offscreen_backup_buffer != NULL) {
		free(g_front_state.offscreen_backup_buffer);
		g_front_state.offscreen_backup_buffer = NULL;
	}
	frontend_string_unload_table();
	if (g_front_state.direct_draw != NULL) {
		g_front_state.direct_draw->lpVtbl->FlipToGDISurface(
			g_front_state.direct_draw);
		g_front_state.direct_draw->lpVtbl->RestoreDisplayMode(
			g_front_state.direct_draw);
		if (g_front_state.primary_surface != NULL) {
			g_front_state.primary_surface->lpVtbl->Release(
				g_front_state.primary_surface);
			g_front_state.primary_surface = NULL;
		}
		if (g_front_state.dd_palette != NULL) {
			g_front_state.dd_palette->lpVtbl->Release(
				g_front_state.dd_palette);
			g_front_state.dd_palette = NULL;
		}
		if (g_front_state.offscreen_surface != NULL) {
			g_front_state.offscreen_surface->lpVtbl->Release(
				g_front_state.offscreen_surface);
			g_front_state.offscreen_surface = NULL;
		}
		if ((g_opt_no_fullscreen != 0 || g_no_page_flip != 0) &&
		    g_front_state.back_buffer_surface != NULL) {
			g_front_state.back_buffer_surface->lpVtbl->Release(
				g_front_state.back_buffer_surface);
			g_front_state.back_buffer_surface = NULL;
		}
		g_front_state.direct_draw->lpVtbl->Release(
			g_front_state.direct_draw);
		g_front_state.direct_draw = NULL;
		if (g_front_state.hWnd != NULL && b_destroy_window != 0) {
			g_front_state.hWnd = NULL;
		}
	}
	Aeron_SetHostCursorVisible(0);
}

/* Shows "DirectDraw Init FAILED at <stage>" in an error box through Aeron,
 * titled g_window_name, shuts the frontend down with
 * frontend_display_shutdown(1) and returns 0. frontend_display_init_main_window
 * and frontend_display_reinit_surfaces pass the failing step, 0 to 6. */
// FUNCTION: XVT 0x4D3DA0
int frontend_display_report_direct_draw_init_failure(void *hWnd, int stage)
{
	(void)hWnd;
	AeronMessageBoxButton button = {1, "OK", 1, 1};
	AeronMessageBoxOptions options;

	char message[256];
	sprintf(message, "DirectDraw Init FAILED at %d", stage);
	XVT_LOG_ERROR("display.menu_setup_failed stage=%d", stage);
	options.kind = AERON_MESSAGE_BOX_ERROR;
	options.title = g_window_name;
	options.message = message;
	options.buttons = &button;
	options.button_count = 1;
	Aeron_ShowMessageBox(&options, NULL);
	frontend_display_shutdown(1);
	return 0;
}

/* Shows text in a warning box through Aeron, titled g_window_name, and returns
 * 1 once it is closed. Unlocks the back buffer and, with DirectDraw, flips to
 * the GDI surface first; afterward locks the back buffer into
 * g_draw_surface_ptr again when it was locked. */
// FUNCTION: XVT 0x4D3DF0
int frontend_display_show_game_message_box(const char *text)
{
	AeronMessageBoxButton button = {1, "OK", 1, 1};
	AeronMessageBoxOptions options = {AERON_MESSAGE_BOX_WARNING,
					  g_window_name, text, &button, 1};

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	if (g_front_state.direct_draw != NULL) {
		g_front_state.direct_draw->lpVtbl->FlipToGDISurface(
			g_front_state.direct_draw);
	}

	Aeron_ShowMessageBox(&options, NULL);

	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 1;
}

/* Makes the frontend's display and returns 1, or 0 when a step fails, after
 * frontend_display_report_direct_draw_init_failure unless the window failed.
 * Uses the window handle the host already stored in g_front_state.hWnd. Creates
 * DirectDraw on the video.cfg driver (frontend_display_load_driver_guid), else
 * on the default one, setting secondary_direct_draw_active only when the
 * configured driver was used; takes exclusive full-screen mode at 640 by 480
 * and g_front_state.display_bpp; creates the primary surface and the back
 * buffer, a flip chain with one back buffer unless g_opt_no_fullscreen or
 * g_no_page_flip is set, else a separate 640 by 480 system-memory surface; and
 * a 640 by 480 offscreen surface, recording pixel_format555 and both pitches.
 * Loads the default palette (frontend_display_load_palette) and sets it at 8
 * bits per pixel, drops to normal cooperative level with g_opt_no_fullscreen,
 * sets the six text color codes, clears and presents both surfaces, loads the
 * size-20 font, finds the joysticks, sets the default cursor, moves the system
 * cursor to (0, 0), starts DirectSound (showing "Sound not available." when it
 * fails), hides the system cursor and allocates and zeroes
 * g_front_state.offscreen_backup_buffer, 480 rows of the offscreen pitch,
 * returning 1 even when that allocation fails. */
// FUNCTION: XVT 0x4D41E0
int frontend_display_init_main_window(void *hInstance, int nShowCmd)
{
	(void)nShowCmd;

	void *window_handle;
	/* The host shell owns the window, so the port keeps the handle it published. */
	(void)hInstance;
	window_handle = g_front_state.hWnd;

	const DxGuid *driver_guid = frontend_display_load_driver_guid();
	if (DirectDrawCreate_Compat(driver_guid, &g_front_state.direct_draw,
				    NULL) != 0) {
		if (DirectDrawCreate_Compat(NULL, &g_front_state.direct_draw,
					    NULL) != 0) {
			return frontend_display_report_direct_draw_init_failure(
				window_handle, 0);
		}
		g_front_state.secondary_direct_draw_active = 0;
	} else {
		g_front_state.secondary_direct_draw_active = 0;
		if (driver_guid != NULL) {
			g_front_state.secondary_direct_draw_active = 1;
		}
	}

	HRESULT result = g_front_state.direct_draw->lpVtbl->SetCooperativeLevel(
		g_front_state.direct_draw, window_handle,
		DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 1);
	}
	result = g_front_state.direct_draw->lpVtbl->SetDisplayMode(
		g_front_state.direct_draw, 640, 480, g_front_state.display_bpp);
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 2);
	}

	DDSURFACEDESC surface_desc;
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		surface_desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		surface_desc.ddsCaps.dwCaps =
			DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
		surface_desc.dwBackBufferCount = 1;
	} else {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		surface_desc.dwFlags = DDSD_CAPS;
		surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
	}
	result = g_front_state.direct_draw->lpVtbl->CreateSurface(
		g_front_state.direct_draw, &surface_desc,
		&g_front_state.primary_surface, NULL);
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 3);
	}
	g_front_state.primary_surface->lpVtbl->GetSurfaceDesc(
		g_front_state.primary_surface, &surface_desc);
	g_front_state.pixel_format555 =
		(surface_desc.ddpfPixelFormat.dwGBitMask & 0x400) == 0;

	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		DDSCAPS attached_surface_caps;
		attached_surface_caps.dwCaps = DDSCAPS_BACKBUFFER;
		result = g_front_state.primary_surface->lpVtbl
				 ->GetAttachedSurface(
					 g_front_state.primary_surface,
					 &attached_surface_caps,
					 &g_front_state.back_buffer_surface);
	} else {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		surface_desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		surface_desc.ddsCaps.dwCaps =
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		surface_desc.dwWidth = 640;
		surface_desc.dwHeight = 480;
		result = g_front_state.direct_draw->lpVtbl->CreateSurface(
			g_front_state.direct_draw, &surface_desc,
			&g_front_state.back_buffer_surface, NULL);
	}
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 4);
	}
	g_front_state.back_buffer_surface->lpVtbl->GetSurfaceDesc(
		g_front_state.back_buffer_surface, &surface_desc);
	g_front_state.back_buffer_pitch = surface_desc.lPitch;

	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	surface_desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		surface_desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	} else {
		surface_desc.ddsCaps.dwCaps =
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	}
	surface_desc.dwWidth = 640;
	surface_desc.dwHeight = 480;
	result = g_front_state.direct_draw->lpVtbl->CreateSurface(
		g_front_state.direct_draw, &surface_desc,
		&g_front_state.offscreen_surface, NULL);
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 5);
	}
	g_front_state.offscreen_surface->lpVtbl->GetSurfaceDesc(
		g_front_state.offscreen_surface, &surface_desc);
	g_front_state.offscreen_surface_pitch = surface_desc.lPitch;

	g_front_state.dd_palette =
		frontend_display_load_palette(g_front_state.direct_draw, NULL);
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		if (g_front_state.dd_palette != NULL &&
		    g_front_state.display_bpp == 8) {
			g_front_state.primary_surface->lpVtbl->SetPalette(
				g_front_state.primary_surface,
				g_front_state.dd_palette);
		}
	} else {
		if (g_front_state.dd_palette != NULL &&
		    g_front_state.display_bpp == 8) {
			g_front_state.primary_surface->lpVtbl->SetPalette(
				g_front_state.primary_surface,
				g_front_state.dd_palette);
			frontend_display_set_palette();
		}
		if (g_opt_no_fullscreen != 0) {
			result = g_front_state.direct_draw->lpVtbl
					 ->SetCooperativeLevel(
						 g_front_state.direct_draw,
						 window_handle, DDSCL_NORMAL);
			if (result != 0) {
				return frontend_display_report_direct_draw_init_failure(
					window_handle, 1);
			}
		}
	}

	g_front_state.text_color_codes[0] = 0xFFFF;
	g_front_state.text_color_codes[1] =
		frontend_display_pack_rgb(0, 0xFF, 0);
	g_front_state.text_color_codes[2] =
		frontend_display_pack_rgb(0xFF, 0, 0);
	g_front_state.text_color_codes[3] =
		frontend_display_pack_rgb(0xFF, 0xFF, 0);
	g_front_state.text_color_codes[4] =
		frontend_display_pack_rgb(0x32, 0x32, 0xFF);
	g_front_state.text_color_codes[5] =
		frontend_display_pack_rgb(0x80, 0x80, 0xFF);

	xvt_render_frontend_reset();
	frontend_display_clear_back_buffer();
	frontend_display_clear_offscreen_surface();
	frontend_display_present_frame();
	if (frontend_text_load_font(20) != 1) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 6);
	}

	joystick_init_devices();
	frontend_cursor_init();
	xvt_presentation_warp_classic(0, 0);
	if (frontend_sound_init_direct_sound(g_front_state.hWnd) == 0) {
		XVT_LOG_WARN("frontend.sound_unavailable site=\"start\"");
		frontend_display_show_game_message_box("Sound not available.");
	}
	Aeron_SetHostCursorVisible(0);
	g_front_state.offscreen_backup_buffer =
		malloc(480 * g_front_state.offscreen_surface_pitch);
	if (g_front_state.offscreen_backup_buffer != NULL) {
		memset(g_front_state.offscreen_backup_buffer, 0,
		       480 * g_front_state.offscreen_surface_pitch);
	} else {
		XVT_LOG_WARN(
			"display.menu_backup_alloc_failed site=\"start\" bytes=%d",
			480 * g_front_state.offscreen_surface_pitch);
	}
	XVT_LOG_DEBUG(
		"display.menu_started bpp=%d flip=%d format555=%d back_pitch=%d offscreen_pitch=%d palette=%d configured=%d secondary=%d frame_ms=%d",
		g_front_state.display_bpp,
		(int)(g_opt_no_fullscreen == 0 && g_no_page_flip == 0),
		(int)g_front_state.pixel_format555,
		g_front_state.back_buffer_pitch,
		g_front_state.offscreen_surface_pitch,
		(int)(g_front_state.dd_palette != NULL),
		(int)(driver_guid != NULL),
		(int)g_front_state.secondary_direct_draw_active,
		g_front_state.frame_interval_ms);
	return 1;
}

/* Locks the back buffer and returns its pixels, setting
 * g_front_state.draw_surface_pitch to the back buffer's pitch; when
 * back_buffer_locked is already set it returns the pointer it holds. Retries
 * while the surface is still drawing, busy or obscured, restoring it when it is
 * lost; on any other failure it still sets back_buffer_locked and returns
 * back_buffer_desc.lpSurface unchecked. Returns NULL without DirectDraw or a
 * back buffer. Callers store the result in g_draw_surface_ptr; this does not.
 * It also selects the back buffer as its renderer's target. */
// FUNCTION: XVT 0x4D48E0
uint8_t *frontend_display_lock_back_buffer(void)
{
	enum {
		FRONTEND_DDERR_SURFACEBUSY = -2005532242,
		FRONTEND_DDERR_SURFACEISOBSCURED = -2005532232,
	};

	if (g_front_state.direct_draw == NULL) {
		return NULL;
	}
	if (g_front_state.back_buffer_surface == NULL) {
		return NULL;
	}

	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
	g_front_state.draw_surface_pitch = g_front_state.back_buffer_pitch;
	if (g_front_state.back_buffer_locked != 0) {
		return (uint8_t *)g_front_state.back_buffer_desc.lpSurface;
	}

	g_front_state.back_buffer_desc.dwSize =
		sizeof(g_front_state.back_buffer_desc);
	HRESULT lock_result;
	do {
		do {
			lock_result =
				g_front_state.back_buffer_surface->lpVtbl->Lock(
					g_front_state.back_buffer_surface, NULL,
					&g_front_state.back_buffer_desc, 0,
					NULL);
			if (lock_result != DX_DDERR_SURFACELOST) {
				break;
			}
			g_front_state.back_buffer_surface->lpVtbl->Restore(
				g_front_state.back_buffer_surface);
		} while (1);
	} while (lock_result == DX_DDERR_WASSTILLDRAWING ||
		 lock_result == FRONTEND_DDERR_SURFACEBUSY ||
		 lock_result == FRONTEND_DDERR_SURFACEISOBSCURED);

	if (lock_result != DX_DD_OK) {
		XVT_LOG_ERROR(
			"display.menu_lock_failed surface=\"back_buffer\" result=%#x",
			(unsigned)lock_result);
	}
	g_front_state.back_buffer_locked = 1;
	return (uint8_t *)g_front_state.back_buffer_desc.lpSurface;
}

/* Unlocks the back buffer and clears g_front_state.back_buffer_locked when
 * DirectDraw and the back buffer exist; does not check that it was locked. */
// FUNCTION: XVT 0x4D4970
void frontend_display_unlock_back_buffer(void)
{
	if (g_front_state.direct_draw != NULL &&
	    g_front_state.back_buffer_surface != NULL) {
		g_front_state.back_buffer_surface->lpVtbl->Unlock(
			g_front_state.back_buffer_surface, NULL);
		g_front_state.back_buffer_locked = 0;
	}
}

/* Shows the back buffer. Unlocks it when locked; in page-flip mode
 * (g_opt_no_fullscreen and g_no_page_flip both 0) waits for the vertical blank,
 * sets the palette at 8 bits per pixel when palette_needs_set is 1, which no
 * code sets, and flips, retrying while the surface is still drawing or after
 * restoring lost surfaces; otherwise it copies the back buffer to the primary
 * surface at (0, 0) and returns early when that copy fails. With offscreen
 * restore on it then puts the offscreen surface back under the next frame:
 * after an activation it first copies g_front_state.offscreen_backup_buffer
 * into the offscreen surface, then copies the offscreen surface onto the back
 * buffer, in non-flip mode through frontend_display_restore_back_buffer,
 * returning early when the flip-mode copy fails. Last it clears the back buffer
 * when clear_back_buffer_after_present is set. Does nothing without DirectDraw.
 * It also presents its renderer's frame after a successful flip or copy and
 * mirrors the copies in its targets. */
// FUNCTION: XVT 0x4D49A0
void frontend_display_present_frame(void)
{
	if (g_front_state.direct_draw == NULL) {
		return;
	}

	if (g_front_state.back_buffer_locked != 0) {
		frontend_display_unlock_back_buffer();
	}

	struct RECT source_rect;
	source_rect.right = 640;
	source_rect.bottom = 480;
	source_rect.left = 0;
	source_rect.top = 0;

	HRESULT result;
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		int vertical_blank_status;
		do {
			result = g_front_state.direct_draw->lpVtbl
					 ->GetVerticalBlankStatus(
						 g_front_state.direct_draw,
						 &vertical_blank_status);
		} while (result == DX_DD_OK && vertical_blank_status == 0);

		if (g_front_state.palette_needs_set == 1 &&
		    g_front_state.display_bpp == 8) {
			frontend_display_set_palette();
			g_front_state.palette_needs_set = 0;
		}

		for (;;) {
			result = g_front_state.primary_surface->lpVtbl->Flip(
				g_front_state.primary_surface,
				g_front_state.back_buffer_surface, 1);
			if (result == DX_DD_OK) {
				break;
			}
			if (result == DX_DDERR_SURFACELOST) {
				if (frontend_display_restore_lost_surfaces() ==
				    DX_DD_OK) {
					continue;
				}
			} else if (result == DX_DDERR_WASSTILLDRAWING) {
				continue;
			}
			break;
		}
	} else {
		for (;;) {
			result = g_front_state.primary_surface->lpVtbl->BltFast(
				g_front_state.primary_surface, 0, 0,
				g_front_state.back_buffer_surface, &source_rect,
				0);
			if (result == DX_DD_OK) {
				break;
			}
			if (result == DX_DDERR_SURFACELOST) {
				if (frontend_display_restore_lost_surfaces() !=
				    DX_DD_OK) {
					XVT_LOG_ERROR(
						"display.menu_present_failed result=%#x",
						(unsigned)result);
					return;
				}
			} else if (result != DX_DDERR_WASSTILLDRAWING) {
				XVT_LOG_ERROR(
					"display.menu_present_failed result=%#x",
					(unsigned)result);
				return;
			}
		}
	}

	if (result == DX_DD_OK) {
		xvt_render_frontend_present();
	}

	if (g_front_state.offscreen_restore_enabled != 0) {
		if (g_front_state.restore_offscreen_overlay_after_activate !=
		    0) {
			g_front_state.restore_offscreen_overlay_after_activate =
				0;
			if (g_front_state.offscreen_backup_buffer != NULL &&
			    g_front_state.offscreen_surface != NULL) {
				DDSURFACEDESC surface_desc;
				memset(&surface_desc, 0, sizeof(surface_desc));
				surface_desc.dwSize = sizeof(surface_desc);
				for (;;) {
					result =
						g_front_state.offscreen_surface
							->lpVtbl
							->Lock(g_front_state
								       .offscreen_surface,
							       NULL,
							       &surface_desc, 0,
							       NULL);
					if (result == DX_DD_OK) {
						break;
					}
					if (result == DX_DDERR_SURFACELOST) {
						g_front_state.offscreen_surface
							->lpVtbl->Restore(
								g_front_state
									.offscreen_surface);
					} else if (result !=
						   DX_DDERR_WASSTILLDRAWING) {
						break;
					}
				}

				if (surface_desc.lpSurface != NULL) {

					xvt_render_frontend_copy(
						XVT_TARGET_FRONT_BACKUP,
						XVT_TARGET_FRONT_OFFSCREEN);
					memcpy(surface_desc.lpSurface,
					       g_front_state
						       .offscreen_backup_buffer,
					       (size_t)(480 *
							g_front_state
								.offscreen_surface_pitch));
					g_front_state.offscreen_surface->lpVtbl
						->Unlock(
							g_front_state
								.offscreen_surface,
							NULL);
				}
			}
		}

		if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
			for (;;) {
				result =
					g_front_state.back_buffer_surface
						->lpVtbl->BltFast(
							g_front_state
								.back_buffer_surface,
							0, 0,
							g_front_state
								.offscreen_surface,
							&source_rect, 0);
				if (result == DX_DD_OK) {
					break;
				}
				if (result == DX_DDERR_SURFACELOST) {
					if (frontend_display_restore_lost_surfaces() !=
					    DX_DD_OK) {
						return;
					}
				} else if (result != DX_DDERR_WASSTILLDRAWING) {
					return;
				}
			}
			xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
						 XVT_TARGET_FRONT_BACK);

		} else {
			frontend_display_restore_back_buffer();
		}
	}

	if (g_front_state.clear_back_buffer_after_present != 0) {
		frontend_display_clear_back_buffer();
	}
}

/* Sets g_front_state.clear_back_buffer_after_present to 0, so
 * frontend_display_present_frame keeps the back buffer's pixels. */
// FUNCTION: XVT 0x4D4BF0
void frontend_display_disable_clear_after_present(void)
{
	g_front_state.clear_back_buffer_after_present = 0;
}

/* Sets g_front_state.surface_clear_color, the display pixel value the two clear
 * functions fill with. */
// FUNCTION: XVT 0x4D4C00
void frontend_display_set_surface_clear_color(uint32_t color)
{
	g_front_state.surface_clear_color = color;
}

/* Fills the back buffer's 640 by 480 pixels with
 * g_front_state.surface_clear_color through a DirectDraw color fill, unlocking
 * it first and, when it was locked, locking it into g_draw_surface_ptr again
 * afterward. Retries while the surface is still drawing or after restoring lost
 * surfaces, and gives up on any other failure. Does nothing without DirectDraw
 * or a back buffer. It also clears its renderer's back target. */
// FUNCTION: XVT 0x4D4C10
void frontend_display_clear_back_buffer(void)
{
	if (g_front_state.direct_draw == NULL ||
	    g_front_state.back_buffer_surface == NULL) {
		return;
	}

	int was_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 0, 0, 640, 480);
	DDBLTFX effects;
	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillColor = g_front_state.surface_clear_color;
	for (;;) {
		HRESULT result = g_front_state.back_buffer_surface->lpVtbl->Blt(
			g_front_state.back_buffer_surface, &rect, NULL, NULL,
			DDBLT_COLORFILL, &effects);
		if (result == DX_DD_OK) {
			break;
		}
		if (result == DX_DDERR_SURFACELOST) {
			if (frontend_display_restore_lost_surfaces() !=
			    DX_DD_OK) {
				XVT_LOG_WARN(
					"display.menu_clear_failed surface=\"back_buffer\" result=%#x",
					(unsigned)result);
				if (was_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			XVT_LOG_WARN(
				"display.menu_clear_failed surface=\"back_buffer\" result=%#x",
				(unsigned)result);
			if (was_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return;
		}
	}

	xvt_render_frontend_clear(XVT_TARGET_FRONT_BACK,
				  g_front_state.surface_clear_color);

	if (was_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
}

/* Copies the clip bounds into *out_rect: clip_min_x, clip_min_y, clip_max_x and
 * clip_max_y as left, top, right and bottom. */
// FUNCTION: XVT 0x4D4CF0
void frontend_display_get_screen_clip_rect(struct RECT *out_rect)
{
	frontend_draw_rect_assign(
		out_rect, g_front_state.clip_min_x, g_front_state.clip_min_y,
		g_front_state.clip_max_x, g_front_state.clip_max_y);
}

/* Sets the clip bounds to *src clamped to 0 to 639 by 0 to 479, leaving them as
 * they were when the clamped rect has right under left or bottom under top. */
// FUNCTION: XVT 0x4D4D20
void frontend_display_set_screen_clip_rect640x480(const struct RECT *src)
{
	struct RECT clipped_rect;

	frontend_draw_rect_copy(&clipped_rect, src);
	if (clipped_rect.left < 0) {
		clipped_rect.left = 0;
	}
	if (clipped_rect.top < 0) {
		clipped_rect.top = 0;
	}
	if (clipped_rect.right >= 640) {
		clipped_rect.right = 639;
	}
	if (clipped_rect.bottom >= 480) {
		clipped_rect.bottom = 479;
	}

	if (clipped_rect.right >= clipped_rect.left) {
		if (clipped_rect.top <= clipped_rect.bottom) {
			g_front_state.clip_min_x = clipped_rect.left;
			g_front_state.clip_min_y = clipped_rect.top;
			g_front_state.clip_max_x = clipped_rect.right;
			g_front_state.clip_max_y = clipped_rect.bottom;
		}
	}
}

/* Sets g_front_state.escape_close_enabled to 0, so Esc no longer closes the
 * window. */
// FUNCTION: XVT 0x4D4DD0
void frontend_display_disable_escape_close(void)
{
	g_front_state.escape_close_enabled = 0;
}

/* Returns g_front_state.frame_counter, the frame number of the top screen. */
// FUNCTION: XVT 0x4D4DE0
int frontend_display_get_frame_counter(void)
{
	return g_front_state.frame_counter;
}

/* Sets g_front_state.frame_interval_ms to 1000 / fps milliseconds and returns it.
 * Does not check fps for 0. */
// FUNCTION: XVT 0x4D4E00
int frontend_display_set_frame_rate(int fps)
{
	return g_front_state.frame_interval_ms = 1000 / fps;
}

/* Locks the offscreen surface and makes it the drawing target: sets
 * g_draw_surface_ptr to its pixels and g_front_state.draw_surface_pitch to its
 * pitch, and returns 1. Retries while it is still drawing, restoring it when it
 * is lost; returns 0 on another failure or without DirectDraw or the surface.
 * It also selects its renderer's offscreen target. */
// FUNCTION: XVT 0x4D4E20
int frontend_display_lock_offscreen_surface(void)
{
	if (g_front_state.direct_draw == NULL) {
		return 0;
	}
	if (g_front_state.offscreen_surface == NULL) {
		return 0;
	}

	DDSURFACEDESC surface_desc;
	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	for (;;) {
		HRESULT result = g_front_state.offscreen_surface->lpVtbl->Lock(
			g_front_state.offscreen_surface, NULL, &surface_desc, 0,
			NULL);
		if (result == DX_DD_OK) {
			break;
		}
		if (result == DX_DDERR_SURFACELOST) {
			g_front_state.offscreen_surface->lpVtbl->Restore(
				g_front_state.offscreen_surface);
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			XVT_LOG_ERROR(
				"display.menu_lock_failed surface=\"offscreen\" result=%#x",
				(unsigned)result);
			return 0;
		}
	}

	g_front_state.draw_surface_pitch =
		g_front_state.offscreen_surface_pitch;
	g_draw_surface_ptr = (uint8_t *)surface_desc.lpSurface;

	xvt_render_frontend_select(XVT_TARGET_FRONT_OFFSCREEN);
	return 1;
}

/* Unlocks the offscreen surface and makes the back buffer the drawing target
 * again. With save_to_backup nonzero, and g_draw_surface_ptr and the backup
 * buffer set, it first copies 480 rows of the offscreen pitch from
 * g_draw_surface_ptr into g_front_state.offscreen_backup_buffer. Then sets
 * draw_surface_pitch to the back buffer's pitch and g_draw_surface_ptr to the
 * back buffer's pixels, locking it when it is not locked. Returns 1, or 0
 * without DirectDraw or the surface. Does not check that the offscreen surface
 * was locked. It also mirrors the copy and selects its back target. */
// FUNCTION: XVT 0x4D4EC0
int frontend_display_unlock_offscreen_surface(int save_to_backup)
{
	if (g_front_state.direct_draw == NULL) {
		return 0;
	}
	if (g_front_state.offscreen_surface == NULL) {
		return 0;
	}

	if (g_draw_surface_ptr != NULL &&
	    g_front_state.offscreen_backup_buffer != NULL &&
	    save_to_backup != 0) {

		xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
					 XVT_TARGET_FRONT_BACKUP);
		memcpy(g_front_state.offscreen_backup_buffer,
		       g_draw_surface_ptr,
		       (size_t)(480 * g_front_state.offscreen_surface_pitch));
	}

	g_front_state.offscreen_surface->lpVtbl->Unlock(
		g_front_state.offscreen_surface, NULL);
	g_front_state.draw_surface_pitch = g_front_state.back_buffer_pitch;
	if (g_front_state.back_buffer_locked != 0) {
		g_draw_surface_ptr =
			(uint8_t *)g_front_state.back_buffer_desc.lpSurface;
	} else {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}

	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
	return 1;
}

/* Sets g_front_state.offscreen_restore_enabled to 1 and returns 1. While it is
 * set, frontend_display_present_frame copies the offscreen surface onto the back
 * buffer after each present, and the screen stack saves and restores pixels on
 * the offscreen surface. */
// FUNCTION: XVT 0x4D4F60
int frontend_display_enable_offscreen_restore(void)
{
	g_front_state.offscreen_restore_enabled = 1;
	return 1;
}

/* Sets g_front_state.offscreen_restore_enabled to 0 and returns 0. */
// FUNCTION: XVT 0x4D4F70
int frontend_display_disable_offscreen_restore(void)
{
	g_front_state.offscreen_restore_enabled = 0;
	return 0;
}

/* Fills the offscreen surface's 640 by 480 pixels with
 * g_front_state.surface_clear_color, the way frontend_display_clear_back_buffer
 * fills the back buffer, unlocking and relocking the back buffer around it.
 * Does nothing without DirectDraw or the offscreen surface. It also clears its
 * renderer's offscreen target. */
// FUNCTION: XVT 0x4D4F80
void frontend_display_clear_offscreen_surface(void)
{
	if (g_front_state.direct_draw == NULL ||
	    g_front_state.offscreen_surface == NULL) {
		return;
	}

	int was_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 0, 0, 640, 480);
	DDBLTFX effects;
	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillColor = g_front_state.surface_clear_color;
	for (;;) {
		HRESULT result = g_front_state.offscreen_surface->lpVtbl->Blt(
			g_front_state.offscreen_surface, &rect, NULL, NULL,
			DDBLT_COLORFILL, &effects);
		if (result == DX_DD_OK) {
			break;
		}
		if (result == DX_DDERR_SURFACELOST) {
			if (frontend_display_restore_lost_surfaces() !=
			    DX_DD_OK) {
				XVT_LOG_WARN(
					"display.menu_clear_failed surface=\"offscreen\" result=%#x",
					(unsigned)result);
				if (was_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			XVT_LOG_WARN(
				"display.menu_clear_failed surface=\"offscreen\" result=%#x",
				(unsigned)result);
			if (was_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return;
		}
	}

	xvt_render_frontend_clear(XVT_TARGET_FRONT_OFFSCREEN,
				  g_front_state.surface_clear_color);

	if (was_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
}

/* Returns g_front_state.pixel_format555: 1 when the primary surface's green mask
 * lacks the 0x400 bit, as at 5-5-5 and at 8 bits per pixel, 0 at 5-6-5. */
// FUNCTION: XVT 0x4D5060
int frontend_display_get_pixel_format555(void)
{
	return g_front_state.pixel_format555;
}

/* Returns g_front_state.draw_surface_pitch, the byte pitch of the surface
 * g_draw_surface_ptr points into; flight_surface_lock uses it while flight draws
 * to the frontend. */
// FUNCTION: XVT 0x4D5070
int frontend_display_get_frontend_or_flight_draw_pitch(void)
{
	return g_front_state.draw_surface_pitch;
}

/* Returns g_draw_surface_ptr, where flight_surface_lock points flight's drawing
 * while g_flight_render_to_frontend is 1. */
// FUNCTION: XVT 0x4D5410
uint8_t *frontend_display_get_draw_surface_for_flight(void)
{
	return g_draw_surface_ptr;
}

/* Returns g_front_state.hWnd, the frontend window. */
// FUNCTION: XVT 0x4D5690
void *frontend_display_get_main_window_handle(void)
{
	return g_front_state.hWnd;
}

/* Returns g_front_state.direct_draw, which flight's display code also uses. */
// FUNCTION: XVT 0x4D56A0
IDirectDraw *frontend_display_get_direct_draw(void)
{
	return g_front_state.direct_draw;
}

/* Before a flight starts: unlocks the back buffer, unloads every frontend
 * sound, shuts down DirectSound, closes the CD device, and releases the primary
 * surface, the palette, the offscreen surface and, with g_opt_no_fullscreen or
 * g_no_page_flip, the separate back buffer, setting each pointer to NULL. Keeps
 * DirectDraw and the window. Returns 1. */
// FUNCTION: XVT 0x4D56B0
int frontend_display_release_surfaces_for_flight(void)
{
	XVT_LOG_DEBUG(
		"display.menu_surfaces_released screen_surface=%d palette=%d offscreen=%d back_buffer=%d",
		(int)(g_front_state.primary_surface != NULL),
		(int)(g_front_state.dd_palette != NULL),
		(int)(g_front_state.offscreen_surface != NULL),
		(int)((g_opt_no_fullscreen != 0 || g_no_page_flip != 0) &&
		      g_front_state.back_buffer_surface != NULL));
	frontend_display_unlock_back_buffer();
	frontend_sound_unload_all_buffers();
	frontend_sound_shutdown_direct_sound();
	cd_audio_close_device();
	if (g_front_state.primary_surface != NULL) {
		g_front_state.primary_surface->lpVtbl->Release(
			g_front_state.primary_surface);
		g_front_state.primary_surface = NULL;
	}
	if (g_front_state.dd_palette != NULL) {
		g_front_state.dd_palette->lpVtbl->Release(
			g_front_state.dd_palette);
		g_front_state.dd_palette = NULL;
	}
	if (g_front_state.offscreen_surface != NULL) {
		g_front_state.offscreen_surface->lpVtbl->Release(
			g_front_state.offscreen_surface);
		g_front_state.offscreen_surface = NULL;
	}
	if ((g_opt_no_fullscreen != 0 || g_no_page_flip != 0) &&
	    g_front_state.back_buffer_surface != NULL) {
		g_front_state.back_buffer_surface->lpVtbl->Release(
			g_front_state.back_buffer_surface);
		g_front_state.back_buffer_surface = NULL;
	}
	return 1;
}

/* Rebuilds the frontend's surfaces after a flight, on the DirectDraw object and
 * window frontend_display_release_surfaces_for_flight kept: normal cooperative
 * level with g_opt_no_fullscreen, else exclusive full-screen and the 640 by 480
 * mode; then the primary surface, back buffer, offscreen surface and palette as
 * frontend_display_init_main_window makes them. Sets the text color codes, moves
 * the system cursor to (0, 0), clears both surfaces and presents, restarts
 * DirectSound, locks the back buffer into g_draw_surface_ptr and allocates
 * offscreen_backup_buffer when there is none. Returns 1, or 0 after
 * frontend_display_report_direct_draw_init_failure. */
// FUNCTION: XVT 0x4D5760
int frontend_display_reinit_surfaces(void)
{
	void *window_handle = g_front_state.hWnd;
	HRESULT result;
	if (g_opt_no_fullscreen != 0) {
		result = g_front_state.direct_draw->lpVtbl->SetCooperativeLevel(
			g_front_state.direct_draw, window_handle, DDSCL_NORMAL);
		if (result != 0) {
			return frontend_display_report_direct_draw_init_failure(
				window_handle, 1);
		}
	} else {
		result = g_front_state.direct_draw->lpVtbl->SetCooperativeLevel(
			g_front_state.direct_draw, window_handle,
			DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
		if (result != 0) {
			return frontend_display_report_direct_draw_init_failure(
				window_handle, 1);
		}
		result = g_front_state.direct_draw->lpVtbl->SetDisplayMode(
			g_front_state.direct_draw, 640, 480,
			g_front_state.display_bpp);
		if (result != 0) {
			return frontend_display_report_direct_draw_init_failure(
				window_handle, 2);
		}
	}

	DDSURFACEDESC surface_desc;
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		surface_desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		surface_desc.ddsCaps.dwCaps =
			DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
		surface_desc.dwBackBufferCount = 1;
	} else {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		surface_desc.dwFlags = DDSD_CAPS;
		surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
	}
	result = g_front_state.direct_draw->lpVtbl->CreateSurface(
		g_front_state.direct_draw, &surface_desc,
		&g_front_state.primary_surface, NULL);
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 3);
	}
	g_front_state.primary_surface->lpVtbl->GetSurfaceDesc(
		g_front_state.primary_surface, &surface_desc);
	g_front_state.pixel_format555 =
		(surface_desc.ddpfPixelFormat.dwGBitMask & 0x400) == 0;

	if (g_opt_no_fullscreen != 0 || g_no_page_flip != 0) {
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		surface_desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		surface_desc.ddsCaps.dwCaps =
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		surface_desc.dwWidth = 640;
		surface_desc.dwHeight = 480;
		result = g_front_state.direct_draw->lpVtbl->CreateSurface(
			g_front_state.direct_draw, &surface_desc,
			&g_front_state.back_buffer_surface, NULL);
	} else {
		DDSCAPS attached_surface_caps;
		attached_surface_caps.dwCaps = DDSCAPS_BACKBUFFER;
		result = g_front_state.primary_surface->lpVtbl
				 ->GetAttachedSurface(
					 g_front_state.primary_surface,
					 &attached_surface_caps,
					 &g_front_state.back_buffer_surface);
	}
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 4);
	}
	g_front_state.back_buffer_surface->lpVtbl->GetSurfaceDesc(
		g_front_state.back_buffer_surface, &surface_desc);
	g_front_state.back_buffer_pitch = surface_desc.lPitch;

	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	surface_desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	if (g_opt_no_fullscreen != 0 || g_no_page_flip != 0) {
		surface_desc.ddsCaps.dwCaps =
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	} else {
		surface_desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	}
	surface_desc.dwWidth = 640;
	surface_desc.dwHeight = 480;
	result = g_front_state.direct_draw->lpVtbl->CreateSurface(
		g_front_state.direct_draw, &surface_desc,
		&g_front_state.offscreen_surface, NULL);
	if (result != 0) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 5);
	}
	g_front_state.offscreen_surface->lpVtbl->GetSurfaceDesc(
		g_front_state.offscreen_surface, &surface_desc);
	g_front_state.offscreen_surface_pitch = surface_desc.lPitch;

	g_front_state.dd_palette =
		frontend_display_load_palette(g_front_state.direct_draw, NULL);
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		if (g_front_state.dd_palette != NULL &&
		    g_front_state.display_bpp == 8) {
			g_front_state.primary_surface->lpVtbl->SetPalette(
				g_front_state.primary_surface,
				g_front_state.dd_palette);
		}
	} else if (g_front_state.dd_palette != NULL &&
		   g_front_state.display_bpp == 8) {
		g_front_state.primary_surface->lpVtbl->SetPalette(
			g_front_state.primary_surface,
			g_front_state.dd_palette);
		frontend_display_set_palette();
	}

	g_front_state.text_color_codes[0] = 0xFFFF;
	g_front_state.text_color_codes[1] =
		frontend_display_pack_rgb(0, 0xFF, 0);
	g_front_state.text_color_codes[2] =
		frontend_display_pack_rgb(0xFF, 0, 0);
	g_front_state.text_color_codes[3] =
		frontend_display_pack_rgb(0xFF, 0xFF, 0);
	g_front_state.text_color_codes[4] =
		frontend_display_pack_rgb(0x32, 0x32, 0xFF);
	g_front_state.text_color_codes[5] =
		frontend_display_pack_rgb(0x80, 0x80, 0xFF);
	xvt_presentation_warp_classic(0, 0);

	xvt_render_frontend_reset();
	frontend_display_clear_offscreen_surface();
	frontend_display_clear_back_buffer();
	frontend_display_present_frame();
	if (frontend_sound_init_direct_sound(g_front_state.hWnd) == 0) {
		XVT_LOG_WARN(
			"frontend.sound_unavailable site=\"after_flight\"");
		frontend_display_show_game_message_box("Sound not available.");
	}
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	if (g_front_state.offscreen_backup_buffer == NULL) {
		g_front_state.offscreen_backup_buffer =
			malloc(480 * g_front_state.offscreen_surface_pitch);
		if (g_front_state.offscreen_backup_buffer != NULL) {
			memset(g_front_state.offscreen_backup_buffer, 0,
			       480 * g_front_state.offscreen_surface_pitch);
		} else {
			XVT_LOG_WARN(
				"display.menu_backup_alloc_failed site=\"after_flight\" bytes=%d",
				480 * g_front_state.offscreen_surface_pitch);
		}
	}
	XVT_LOG_DEBUG(
		"display.menu_rebuilt bpp=%d flip=%d format555=%d back_pitch=%d offscreen_pitch=%d palette=%d",
		g_front_state.display_bpp,
		(int)(g_opt_no_fullscreen == 0 && g_no_page_flip == 0),
		(int)g_front_state.pixel_format555,
		g_front_state.back_buffer_pitch,
		g_front_state.offscreen_surface_pitch,
		(int)(g_front_state.dd_palette != NULL));
	return 1;
}

/* Sets g_front_state.frontend_display_wnd_proc_mode: 0 frontend, 1 flight, 2
 * movie. */
// FUNCTION: XVT 0x4D5B70
void frontend_display_set_wnd_proc_mode(uint8_t mode)
{
	g_front_state.frontend_display_wnd_proc_mode = mode;
}

/* Returns g_front_state.frontend_display_wnd_proc_mode. */
// FUNCTION: XVT 0x4D5B80
int frontend_display_get_wnd_proc_mode(void)
{
	return g_front_state.frontend_display_wnd_proc_mode;
}

/* Calls FlipToGDISurface on the frontend's DirectDraw object when it exists. */
// FUNCTION: XVT 0x4D5BC0
void frontend_display_flip_direct_draw_to_gdi_surface(void)
{
	if (g_front_state.direct_draw != NULL) {
		g_front_state.direct_draw->lpVtbl->FlipToGDISurface(
			g_front_state.direct_draw);
	}
}

/* Reads a DirectDraw driver GUID from the file video.cfg into
 * g_configured_direct_draw_driver_guid and returns its address, or NULL when the
 * file does not open or the read fails, so DirectDraw uses its default
 * driver. */
// FUNCTION: XVT 0x4D5C20
const DxGuid *frontend_display_load_driver_guid(void)
{
	xvt_file *stream = file_open("video.cfg", "rb");
	if (stream == NULL) {
		return NULL;
	}
	int read_succeeded =
		file_read_bytes(stream, &g_configured_direct_draw_driver_guid,
				sizeof(g_configured_direct_draw_driver_guid));
	file_close(stream);
	if (read_succeeded == 0) {
		XVT_LOG_WARN("display.menu_driver_file_short");
	}
	return read_succeeded != 0 ? &g_configured_direct_draw_driver_guid
				   : NULL;
}

/* Returns g_front_state.secondary_direct_draw_active, 1 when
 * frontend_display_init_main_window made DirectDraw on the driver named in
 * video.cfg. */
// FUNCTION: XVT 0x4D5E60
int frontend_display_is_secondary_direct_draw_active(void)
{
	return g_front_state.secondary_direct_draw_active;
}

/* Loads the 256 entries of g_front_state.display_palette into the DirectDraw
 * palette; does nothing with g_opt_no_fullscreen. Does not check that the palette
 * exists. */
// FUNCTION: XVT 0x4D6CA0
void frontend_display_set_palette(void)
{
	if (g_opt_no_fullscreen != 0) {
		return;
	}
	g_front_state.dd_palette->lpVtbl->SetEntries(
		g_front_state.dd_palette, 0, 0, 256,
		g_front_state.display_palette);
}

/* Returns the display pixel value for the color r, g, b. At 8 bits per pixel it
 * is the index, 1 to 255, of the display palette entry with the smallest sum of
 * squared channel differences (g_color_dist_lut), the first of equals winning and
 * an exact match returned at once; index 0 is never chosen. At 16 bits it packs
 * the high bits of each channel, 5-5-5 or 5-6-5 by g_front_state.pixel_format555.
 * At any other depth it returns g_front_state.display_bpp. */
// FUNCTION: XVT 0x4D6E30
int frontend_display_pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	unsigned int best_distance;
	int best_index;

	switch (g_front_state.display_bpp) {
	case 8:
		best_distance = 0x7FFFFFFFu;
		best_index = 1;
		for (int index = 1; index < 256; ++index) {
			struct frontend_palette_entry *entry =
				&g_front_state.display_palette[index];
			int red_delta = (int)entry->red - r;
			if (red_delta < 0) {
				red_delta = -red_delta;
			}
			int green_delta = (int)entry->green - g;
			if (green_delta < 0) {
				green_delta = -green_delta;
			}
			int blue_delta = (int)entry->blue - b;
			if (blue_delta < 0) {
				blue_delta = -blue_delta;
			}
			unsigned int distance = g_color_dist_lut[blue_delta];
			distance += g_color_dist_lut[red_delta];
			distance += g_color_dist_lut[green_delta];
			if (distance == 0) {
				return index;
			}
			if (distance < best_distance) {
				best_distance = distance;
				best_index = index;
			}
		}
		return best_index;
	case 16: {
		uint8_t red_component;
		uint8_t green_component;
		uint8_t blue_component;

		if (g_front_state.pixel_format555 != 0) {
			red_component = r >> 3;
			green_component = g >> 3;
			blue_component = b >> 3;
			return 32 * (green_component + 32 * red_component) +
			       blue_component;
		}
		red_component = r >> 3;
		green_component = g >> 2;
		blue_component = b >> 3;
		return 32 * (green_component + (red_component << 6)) +
		       blue_component;
	}
	default:
		return g_front_state.display_bpp;
	}
}

/* Copies the back buffer into the offscreen surface: 480 rows of 80 *
 * (display_bpp & 0xFFFFFFF8) bytes, 640 pixels. Locks both, then unlocks the
 * offscreen surface without saving it to the backup buffer, which leaves
 * g_draw_surface_ptr on the back buffer, and unlocks the back buffer when it
 * was not locked before. Returns 1. It mirrors the copy in its renderer's
 * targets. */
// FUNCTION: XVT 0x4DC9B0
int frontend_display_save_back_buffer(void)
{
	xvt_render_frontend_suppress(1);
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	uint8_t *source = frontend_display_lock_back_buffer();
	frontend_display_lock_offscreen_surface();
	uint8_t *destination = g_draw_surface_ptr;
	for (int row = 480; row != 0; --row) {
		memcpy(destination, source,
		       (size_t)(80 * (g_front_state.display_bpp & 0xFFFFFFF8)));
		destination += g_front_state.offscreen_surface_pitch;
		source += g_front_state.back_buffer_pitch;
	}
	frontend_display_unlock_offscreen_surface(0);
	if (was_back_buffer_locked == 0) {
		frontend_display_unlock_back_buffer();
	}

	xvt_render_frontend_suppress(0);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_BACK,
				 XVT_TARGET_FRONT_OFFSCREEN);
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
	return 1;
}

/* Copies the offscreen surface into the back buffer: 480 rows of 80 *
 * (display_bpp & 0xFFFFFFF8) bytes, 640 pixels, locking and unlocking as
 * frontend_display_save_back_buffer does. Returns 1. It mirrors the copy in its
 * renderer's targets. */
// FUNCTION: XVT 0x4DCA20
int frontend_display_restore_back_buffer(void)
{
	xvt_render_frontend_suppress(1);
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	uint8_t *destination = frontend_display_lock_back_buffer();
	frontend_display_lock_offscreen_surface();
	uint8_t *source = g_draw_surface_ptr;
	for (int row = 480; row != 0; --row) {
		memcpy(destination, source,
		       (size_t)(80 * (g_front_state.display_bpp & 0xFFFFFFF8)));
		source += g_front_state.offscreen_surface_pitch;
		destination += g_front_state.back_buffer_pitch;
	}
	frontend_display_unlock_offscreen_surface(0);
	if (was_back_buffer_locked == 0) {
		frontend_display_unlock_back_buffer();
	}

	xvt_render_frontend_suppress(0);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACK);
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
	return 1;
}

/* Builds the frontend's 256-entry palette and returns the pointer DirectDraw's
 * CreatePalette fills with a palette made from it; the pointer starts at NULL.
 * Starts from a 3-3-2 color cube: entry i has red 255 * ((i & 0xE0) >> 5) / 7,
 * green 255 * ((i & 0x1C) >> 2) / 7 and blue 255 * (i & 3) / 3. With lpName set
 * it then takes the colors of the bitmap file lpName, when it has 8 bits per
 * pixel or fewer; both callers pass NULL, so they get the cube. When the
 * returned pointer is not NULL it copies the entries into
 * g_front_state.display_palette, with entry 0 black and entry 255 white. */
// FUNCTION: XVT 0x4F0E30
IDirectDrawPalette *frontend_display_load_palette(IDirectDraw *p_dd,
						  const char *lp_name)
{
	struct frontend_palette_entry entries[256];

	struct frontend_palette_entry *entry = entries;
	int index = 0;
	do {
		entry->red = (uint8_t)(255 * ((index & 0xE0) >> 5) / 7);
		entry->green = (uint8_t)(255 * ((index & 0x1C) >> 2) / 7);
		entry->blue = (uint8_t)(255 * (index & 3) / 3);
		entry->flags = 0;
		++entry;
		++index;
	} while (entry < entries + 256);

	struct frontend_display_bmp_file_header file_header;
	struct frontend_display_bmp_info_header info_header;
	int color_count;
	if (lp_name != NULL) {
		xvt_file *stream = file_open(lp_name, "rb");
		if (stream != NULL) {
			file_read_bytes(stream, &file_header,
					sizeof(file_header));
			file_read_bytes(stream, &info_header,
					sizeof(info_header));
			file_read_bytes(stream, entries, sizeof(entries));
			file_close(stream);
			if (info_header.header_size == sizeof(info_header)) {
				if (info_header.bits_per_pixel <= 8) {
					color_count = info_header.colors_used;
					if (color_count == 0) {
						color_count =
							1
							<< info_header
								   .bits_per_pixel;
					}
				} else {
					color_count = 0;
				}
			} else {
				color_count = 0;
			}
			for (index = 0; index < color_count; ++index) {
				uint8_t red = entries[index].red;
				entries[index].red = entries[index].blue;
				entries[index].blue = red;
			}
		}
	}

	IDirectDrawPalette *palette;
	palette = NULL;
	p_dd->lpVtbl->CreatePalette(p_dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256,
				    entries, &palette, NULL);
	if (palette != NULL) {
		memcpy(g_front_state.display_palette, entries,
		       sizeof(g_front_state.display_palette));
		g_front_state.display_palette[0].red = 0;
		g_front_state.display_palette[0].green = 0;
		g_front_state.display_palette[0].blue = 0;
		g_front_state.display_palette[255].red = 255;
		g_front_state.display_palette[255].green = 255;
		g_front_state.display_palette[255].blue = 255;
	}
	return palette;
}
