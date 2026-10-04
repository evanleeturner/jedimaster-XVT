#include "xvt/frontend/frontend_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XVT_MODERN
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/frontend_task.h"
#endif
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
#include "xvt_runtime/compat/win_message_port.h"

#ifdef XVT_MODERN
#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "aeron/dialog.h"
#include "xvt/util/time.h"
#else
/* drift-ok: camelcase -- a copy of Windows' WNDCLASSA */
struct WNDCLASSA {
	unsigned int style; /* Class style bits; set to 8, CS_DBLCLKS. */
	/* The window procedure, frontend_display_wnd_proc. */
	int32_t(AERON_DXAPI *lpfnWndProc)(void *hWnd, unsigned int Msg,
					  uint32_t wParam, int32_t lParam);
	int cbClsExtra;	     /* Extra bytes per class; set to 0. */
	int cbWndExtra;	     /* Extra bytes per window; set to 0. */
	void *hInstance;     /* The module that owns the class. */
	void *hIcon;	     /* Icon resource 101 of that module. */
	void *hCursor;	     /* The system arrow cursor, 0x7F00 (IDC_ARROW). */
	void *hbrBackground; /* Stock object 4, the black brush. */
	const char *lpszMenuName;  /* No menu; set to NULL. */
	const char *lpszClassName; /* The class name, g_window_name. */
};

/* The system's window message record, filled by GetMessageA and
 * PeekMessageA. */
/* drift-ok: camelcase -- wParam, lParam: Windows' MSG */
struct frontend_display_win32_message {
	void *window;	  /* The target window; no code here reads it. */
	uint32_t message; /* The message number; no code here reads it. */
	/* The first parameter; for the quit message, the exit code the two
	 * frame loops return. */
	uint32_t wParam;
	int32_t lParam;	 /* The second parameter; no code here reads it. */
	uint32_t time;	 /* When it was posted; no code here reads it. */
	int32_t point_x; /* Cursor x when posted; no code here reads it. */
	int32_t point_y; /* Cursor y when posted; no code here reads it. */
};

uint32_t GetTickCount(void);
__declspec(dllimport) int __stdcall
TranslateMessage(const struct frontend_display_win32_message *message);
__declspec(dllimport) int __stdcall
GetMessageA(struct frontend_display_win32_message *message, void *hWnd,
	    unsigned int filter_min, unsigned int filter_max);
__declspec(dllimport) int32_t __stdcall
DispatchMessageA(const struct frontend_display_win32_message *message);
__declspec(dllimport) int __stdcall
PeekMessageA(struct frontend_display_win32_message *message, void *hWnd,
	     unsigned int filter_min, unsigned int filter_max,
	     unsigned int remove_message);
__declspec(dllimport) void *__stdcall LoadIconA(void *hInstance,
						uintptr_t icon_name);
__declspec(dllimport) void *__stdcall LoadCursorA(void *hInstance,
						  uintptr_t cursor_name);
__declspec(dllimport) uint16_t __stdcall
RegisterClassA(const struct WNDCLASSA *window_class);
__declspec(dllimport) void *__stdcall
CreateWindowExA(uint32_t ex_style, const char *class_name,
		const char *window_name, uint32_t style, int x, int y,
		int width, int height, void *parent, void *menu, void *instance,
		void *param);
__declspec(dllimport) int __stdcall UpdateWindow(void *hWnd);
__declspec(dllimport) void *__stdcall SetFocus(void *hWnd);
__declspec(dllimport) int __stdcall GetKeyboardState(uint8_t *key_state);
__declspec(dllimport) void *__stdcall FindWindowA(const char *class_name,
						  const char *window_name);
__declspec(dllimport) int __stdcall MessageBoxA(void *hWnd, const char *text,
						const char *caption,
						unsigned int type);
/* drift-ok: camelcase -- Windows' parameter names */
__declspec(dllimport) int __stdcall ShowWindowAsync(void *hWnd, int nCmdShow);
__declspec(dllimport) void *__stdcall CreateDCA(const char *driver,
						const char *device,
						const char *port,
						void *device_mode);
__declspec(dllimport) void *__stdcall GetStockObject(int object_index);
__declspec(dllimport) void *__stdcall SelectObject(void *dc, void *object);
__declspec(dllimport) int __stdcall GetSystemMetrics(int index);
__declspec(dllimport) int __stdcall Rectangle(void *dc, int left, int top,
					      int right, int bottom);
__declspec(dllimport) int __stdcall DeleteDC(void *dc);
__declspec(dllimport) void *__stdcall
CreateFontA(int height, int width, int escapement, int orientation, int weight,
	    unsigned int italic, unsigned int underline,
	    unsigned int strike_out, unsigned int char_set,
	    unsigned int out_precision, unsigned int clip_precision,
	    unsigned int quality, unsigned int pitch_and_family,
	    const char *face_name);
__declspec(dllimport) int __stdcall SetMapMode(void *dc, int mode);
__declspec(dllimport) int __stdcall SetTextCharacterExtra(void *dc, int extra);
__declspec(dllimport) uint32_t __stdcall SetTextColor(void *dc, uint32_t color);
__declspec(dllimport) uint32_t __stdcall SetBkColor(void *dc, uint32_t color);
__declspec(dllimport) int __stdcall SetBkMode(void *dc, int mode);
__declspec(dllimport) int __stdcall DrawTextA(void *dc, const char *text,
					      int length, struct RECT *rect,
					      unsigned int format);
__declspec(dllimport) int __stdcall DeleteObject(void *object);
__declspec(dllimport) uint32_t __stdcall GetPixel(void *dc, int x, int y);
__declspec(dllimport) uint32_t __stdcall SetPixel(void *dc, int x, int y,
						  uint32_t color);
__declspec(dllimport) int __stdcall DestroyWindow(void *hWnd);
__declspec(dllimport) int __stdcall ShowCursor(int show);
__declspec(dllimport) int __stdcall SetCursorPos(int x, int y);
__declspec(dllimport) void *__stdcall SetCapture(void *hWnd);
__declspec(dllimport) int __stdcall ReleaseCapture(void);
__declspec(dllimport) void *__stdcall SetCursor(void *cursor);
__declspec(dllimport) int __stdcall
PostMessageA(void *hWnd, unsigned int message, uint32_t wParam, int32_t lParam);
__declspec(dllimport) void __stdcall PostQuitMessage(int exit_code);
__declspec(dllimport) int32_t __stdcall DefWindowProcA(void *hWnd,
						       unsigned int message,
						       uint32_t wParam,
						       int32_t lParam);
__declspec(dllimport) void *__stdcall
FindResourceA(void *module, const char *name, uintptr_t type);
__declspec(dllimport) void *__stdcall LoadResource(void *module,
						   void *resource_info);
__declspec(dllimport) void *__stdcall LockResource(void *resource_data);
__declspec(dllimport) int __stdcall _lopen(const char *path, int mode);
__declspec(dllimport) unsigned int __stdcall _lread(int file, void *buffer,
						    unsigned int size);
__declspec(dllimport) int __stdcall _lclose(int file);
#endif

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
 * Shutdown sets it to 1; frontend_display_init and, in the modern build,
 * xvt_frontend_task_init set it to 0. */
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
 * display_is_pixel_format555 asks the frontend. Set to 0 while a flight runs and
 * back to 1 after it, by flight_main in the original build and by
 * xvt_flight_entry_prepare and xvt_flight_entry_cleanup in the modern build. */
// GLOBAL: XVT 0x527EA0
int g_flight_render_to_frontend = 1;
/* 1 when the command line holds "nopageflip" or "nofullscreen" (game_main): the
 * frontend then copies a system-memory back buffer to the screen instead of
 * flipping, and drops to normal cooperative level after making its surfaces.
 * The modern build's xvt_frontend_task_init sets it to 0. */
// GLOBAL: XVT 0xB69CB0
int g_opt_no_fullscreen = 0;
/* 1 when the command line holds "nofrontflip" (game_main), and always in the
 * modern build (xvt_frontend_task_init): the frontend then draws to a 640 by 480
 * system-memory back buffer and copies it to the primary surface each frame
 * instead of flipping. */
// GLOBAL: XVT 0xB69CBC
int g_no_page_flip = 0;
/* The command line game_main was given; frontend_load_resources hands it to
 * pilot_parse_command_line. The modern build points it at an empty string. */
// GLOBAL: XVT 0xB69CAC
char *g_cmd_line;
/* 1 when the command line holds "skipintro": game_main then starts at the
 * concourse rather than the opening movie and credits. Written by game_main and,
 * in the modern build, xvt_frontend_task_init; concourse_update reads it. */
// GLOBAL: XVT 0xB69CB8
int g_opt_skip_intro;
/* 1 when the command line holds "ishost": game_main then starts at the
 * concourse. concourse_update reads it and sets it to 0, as
 * pilot_parse_command_line does. */
// GLOBAL: XVT 0xB69CA8
int g_opt_is_host;
/* 1 when the command line holds "isclient": game_main then starts at the
 * concourse. concourse_update reads it and sets it to 0, as
 * pilot_parse_command_line does. */
// GLOBAL: XVT 0xB69CB4
int g_opt_is_client;
/* Heap buffer for the screen pixels under the cursor sprite, 2 bytes for each
 * pixel of the "cursor" image: frontend_load_resources allocates it and hands it
 * to frontend_cursor_set_image_from_resource_name as the save buffer. Freed by
 * game_main and, in the modern build, xvt_frontend_task_shutdown. */
// GLOBAL: XVT 0xB6A2AC
void *g_cursor_save_buffer;
/* Set to 1 by frontend_load_resources, the main frontend's start, and never set
 * back; game_main returns 1 when it is still 0. */
// GLOBAL: XVT 0x52BA5C
int g_game_main_skip_intro_relaunch_gate;

/* Nothing calls this; the original WinMain is not rebuilt. Runs the original
 * frontend: keeps lpCmdLine in g_cmd_line and reads its options, each found
 * anywhere in it: "nofrontflip" into g_no_page_flip, "nopageflip" or
 * "nofullscreen" into g_opt_no_fullscreen, "skipintro", "ishost" and "isclient"
 * into g_opt_skip_intro, g_opt_is_host and g_opt_is_client. Ends the process with
 * exit(0) when another copy's window exists (win32_check_single_instance). Then
 * runs frontend_display_init at 24 frames a second and 16 bits per pixel,
 * starting at the concourse when any of the last three options is set, else
 * with the opening movie and credits. When that returns it frees
 * g_cursor_save_buffer, g_frontend_chat_log_buffer, g_cutscene_table and
 * g_campaign_award_sprites, saves the pilot, and returns 1 when
 * g_game_main_skip_intro_relaunch_gate is still 0, else 0. */
// FUNCTION: XVT 0x4D3600
int game_main(void *hInstance, const void *hPrevInstance, char *lpCmdLine,
	      int nShowCmd)
{
	frontend_screen_update_fn update_function;
	frontend_screen_exit_fn exit_function;
	int (*init_function)(void);

	g_cmd_line = lpCmdLine;
	if (strstr(lpCmdLine, "nofrontflip") != NULL) {
		g_no_page_flip = 1;
	} else {
		g_no_page_flip = 0;
	}
	if (strstr(lpCmdLine, "nopageflip") != NULL ||
	    strstr(lpCmdLine, "nofullscreen") != NULL) {
		g_opt_no_fullscreen = 1;
	} else {
		g_opt_no_fullscreen = 0;
	}
	if (strstr(lpCmdLine, "skipintro") != NULL) {
		g_opt_skip_intro = 1;
	} else {
		g_opt_skip_intro = 0;
	}
	if (strstr(lpCmdLine, "ishost") != NULL) {
		g_opt_is_host = 1;
	} else {
		g_opt_is_host = 0;
	}
	if (strstr(lpCmdLine, "isclient") != NULL) {
		g_opt_is_client = 1;
	} else {
		g_opt_is_client = 0;
	}
	if (win32_check_single_instance() != 0) {
		exit(0);
	}
	if (g_opt_skip_intro != 0 || g_opt_is_host != 0 ||
	    g_opt_is_client != 0) {
		update_function = concourse_update;
		exit_function = concourse_exit;
		init_function = frontend_load_resources;
	} else {
		update_function =
			frontend_bootstrap_play_opening_and_enter_credits;
		exit_function = frontend_bootstrap_exit_intro_and_load_credits;
		init_function = frontend_bootstrap_init_mode;
	}
	frontend_display_init(hInstance, hPrevInstance, lpCmdLine, nShowCmd,
			      update_function, exit_function, init_function, 24,
			      16);
	free(g_cursor_save_buffer);
	g_cursor_save_buffer = NULL;
	free(g_frontend_chat_log_buffer);
	g_frontend_chat_log_buffer = NULL;
	if (g_cutscene_table != NULL) {
		free(g_cutscene_table);
		g_cutscene_table = NULL;
		g_cutscene_count = 0;
	}
	free(g_campaign_award_sprites);
	g_campaign_award_sprites = NULL;
	g_campaign_award_sprite_count = 0;
	pilot_save(0);
	return g_game_main_skip_intro_relaunch_gate == 0;
}

/* Restores the primary surface and, when that succeeds, the back buffer when
 * g_opt_no_fullscreen or g_no_page_flip is set, else the offscreen surface. Returns
 * the last Restore result, the primary's failure when it fails. */
// FUNCTION: XVT 0x4D37E0
HRESULT frontend_display_restore_lost_surfaces(void)
{
	HRESULT result;

	result = g_front_state.primary_surface->lpVtbl->Restore(
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
 * separate back buffer (with g_opt_no_fullscreen or g_no_page_flip) and DirectDraw
 * itself, and, with b_destroy_window nonzero and a window, destroys the window;
 * the modern build only forgets its handle. Last it shows the system cursor,
 * which the modern build instead keeps hidden. */
// FUNCTION: XVT 0x4D3820
void frontend_display_shutdown(int b_destroy_window)
{
	int screen_index;

	if (g_shutdown_complete != 0) {
		return;
	}
	g_shutdown_complete = 1;
	frontend_sound_shutdown_direct_sound();
	frontend_text_free_all_fonts();
	for (screen_index = 0; screen_index < g_front_state.screen_stack_top;
	     screen_index++) {
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
#ifdef XVT_MODERN
			g_front_state.hWnd = NULL;
#else
			DestroyWindow(g_front_state.hWnd);
			g_front_state.hWnd = NULL;
#endif
		}
	}
#ifdef XVT_MODERN
	Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(1) < 0) {
	}
#endif
}

/* Only the original build calls this, through frontend_display_wnd_proc while the
 * window procedure mode is 0. WM_DESTROY runs frontend_display_shutdown(1),
 * posts the quit message and returns 0. WM_ACTIVATEAPP stores the new state in
 * g_front_state.app_active and, on activation, resumes CD audio, captures the
 * mouse, sets restore_offscreen_overlay_after_activate and hides the system cursor,
 * or on deactivation suspends CD audio and releases the mouse. WM_SETCURSOR
 * hides the cursor and returns 1. Esc in WM_KEYDOWN posts WM_CLOSE while
 * escape_close_enabled is set. WM_KEYUP clears all of key_down_state. WM_CHAR adds
 * the character to char_ring_buffer, dropping the oldest when the ring already
 * holds 1,023. Alt+O, on WM_SYSKEYUP, saves a screenshot and returns 0; Alt+F4
 * returns 0 in both system key messages. WM_MOUSEMOVE stores the position in
 * mouse_x and mouse_y, each clamped to 640 and 480, and moves the system cursor
 * back when it clamped. The button messages set mouse_left_down and
 * mouse_right_down and, on a release, the click latches; when the message reports
 * the other button held, that button's state moves the same way. Everything
 * else goes on to DefWindowProcA. The modern build's arms request a quit
 * instead of posting messages and return 0 instead of calling
 * DefWindowProcA. */
// FUNCTION: XVT 0x4D3A00
int32_t AERON_DXAPI frontend_display_main_wnd_proc(void *hWnd, unsigned int Msg,
						   uint32_t wParam,
						   int32_t lParam)
{
	int cursor_clamped;

	switch (Msg) {
	case 0x02:
		frontend_display_shutdown(1);
#ifdef XVT_MODERN
		Aeron_RequestQuit();
#else
		PostQuitMessage(0);
#endif
		return 0;

	case 0x1C:
		g_front_state.app_active = (int)wParam;
		if (wParam != 0) {
			cd_audio_request_resume_playback();
#ifndef XVT_MODERN
			if (g_front_state.hWnd != NULL) {
				SetCapture(g_front_state.hWnd);
			}
#endif
			g_front_state.restore_offscreen_overlay_after_activate =
				1;
			frontend_cursor_hide_os_cursor();
		} else {
			cd_audio_suspend_playback();
#ifndef XVT_MODERN
			ReleaseCapture();
#endif
		}
		break;

	case 0x20:
#ifdef XVT_MODERN
		Aeron_SetHostCursorVisible(0);
#else
		SetCursor(NULL);
#endif
		return 1;

	case 0x100:
		if (wParam == 27 && g_front_state.escape_close_enabled != 0) {
#ifdef XVT_MODERN
			Aeron_RequestQuit();
#else
			PostMessageA(hWnd, 0x10, 0, 0);
#endif
		}
		break;

	case 0x101:
		memset(g_front_state.key_down_state, 0,
		       sizeof(g_front_state.key_down_state));
		break;

	case 0x102:
		if ((g_front_state.char_read_idx -
				     g_front_state.char_write_idx ==
			     1 ||
		     (g_front_state.char_write_idx == 1023 &&
		      g_front_state.char_read_idx == 0)) &&
		    ++g_front_state.char_read_idx == 1024) {
			g_front_state.char_read_idx = 0;
		}
		g_front_state.char_ring_buffer[g_front_state.char_write_idx] =
			(char)wParam;
		if (g_front_state.char_write_idx == 1023) {
			g_front_state.char_write_idx = 0;
		} else {
			++g_front_state.char_write_idx;
		}
		break;

	case 0x105:
		if (wParam == 79) {
			frontend_display_capture_screenshot();
			return 0;
		}
		if (wParam == 115) {
			return 0;
		}
		/* fall through */
	case 0x104:
		if (wParam == 115) {
			return 0;
		}
		break;

	case 0x200:
		g_front_state.mouse_x = (uint16_t)lParam;
		g_front_state.mouse_y = (uint16_t)((uint32_t)lParam >> 16);
		cursor_clamped = 0;
		if (g_front_state.mouse_x > 640) {
			cursor_clamped = 1;
			g_front_state.mouse_x = 640;
		}
		if (g_front_state.mouse_y > 480) {
			cursor_clamped = 1;
			g_front_state.mouse_y = 480;
		}
		if (cursor_clamped != 0) {
#ifdef XVT_MODERN
			xvt_presentation_warp_classic(g_front_state.mouse_x,
						      g_front_state.mouse_y);
#else
			SetCursorPos(g_front_state.mouse_x,
				     g_front_state.mouse_y);
#endif
		}
		break;

	case 0x201:
		g_front_state.mouse_left_down = 1;
		if ((wParam & 2) != 0) {
			g_front_state.mouse_right_down = 1;
		}
		break;

	case 0x202:
		g_front_state.mouse_left_down = 0;
		g_front_state.mouse_left_click_latch = 1;
		if ((wParam & 2) != 0) {
			g_front_state.mouse_right_down = 0;
			g_front_state.mouse_right_click_latch = 1;
		}
		break;

	case 0x204:
		g_front_state.mouse_right_down = 1;
		if ((wParam & 1) != 0) {
			g_front_state.mouse_left_down = 1;
		}
		break;

	case 0x205:
		g_front_state.mouse_right_down = 0;
		g_front_state.mouse_right_click_latch = 1;
		if ((wParam & 1) != 0) {
			g_front_state.mouse_left_down = 0;
			g_front_state.mouse_left_click_latch = 1;
		}
		break;
	}

#ifdef XVT_MODERN
	(void)hWnd;
	(void)lParam;
	return 0;
#else
	return DefWindowProcA(hWnd, Msg, wParam, lParam);
#endif
}

/* Only the original build calls this, as the window class's procedure
 * (frontend_display_init_main_window). Hands each message to the handler for the
 * window procedure mode: 0 frontend_display_main_wnd_proc, 1 flight_wnd_proc, 2
 * movie_window_proc, any other mode DefWindowProcA, or 0 in the modern build.
 * Returns the handler's result. */
// FUNCTION: XVT 0x4D3D20
int32_t AERON_DXAPI frontend_display_wnd_proc(void *hWnd, unsigned int Msg,
					      uint32_t wParam, int32_t lParam)
{
	switch (frontend_display_get_wnd_proc_mode()) {
	case 0:
		return frontend_display_main_wnd_proc(hWnd, Msg, wParam,
						      lParam);
	case 1:
		return flight_wnd_proc(hWnd, Msg, wParam, lParam);
	case 2:
		return movie_window_proc(
			hWnd, Msg,
			xvt_port_win_message_param_as_pointer(wParam),
			xvt_port_win_message_param_as_pointer(
				(uint32_t)lParam));
	default:
#ifdef XVT_MODERN
		return 0;
#else
		return DefWindowProcA(hWnd, Msg, wParam, lParam);
#endif
	}
}

/* Shows "DirectDraw Init FAILED at <stage>" in a message box titled
 * g_window_name, shuts the frontend down with frontend_display_shutdown(1) and
 * returns 0. frontend_display_init_main_window and frontend_display_reinit_surfaces
 * pass the failing step, 0 to 6. The modern build shows an error box through
 * Aeron. */
// FUNCTION: XVT 0x4D3DA0
int frontend_display_report_direct_draw_init_failure(void *hWnd, int stage)
{
	char message[256];
#ifdef XVT_MODERN
	AeronMessageBoxButton button = {1, "OK", 1, 1};
	AeronMessageBoxOptions options;
	(void)hWnd;
#endif

	sprintf(message, "DirectDraw Init FAILED at %d", stage);
#ifdef XVT_MODERN
	options.kind = AERON_MESSAGE_BOX_ERROR;
	options.title = g_window_name;
	options.message = message;
	options.buttons = &button;
	options.button_count = 1;
	Aeron_ShowMessageBox(&options, NULL);
#else
	MessageBoxA(hWnd, message, g_window_name, 0);
#endif
	frontend_display_shutdown(1);
	return 0;
}

/* Shows text in a warning message box titled g_window_name and returns 1 once it
 * is closed. Unlocks the back buffer and, with DirectDraw, flips to the GDI
 * surface first; afterward locks the back buffer into g_draw_surface_ptr again
 * when it was locked. The modern build shows the box through Aeron. */
// FUNCTION: XVT 0x4D3DF0
int frontend_display_show_game_message_box(const char *text)
{
	int was_back_buffer_locked;
#ifdef XVT_MODERN
	AeronMessageBoxButton button = {1, "OK", 1, 1};
	AeronMessageBoxOptions options = {AERON_MESSAGE_BOX_WARNING,
					  g_window_name, text, &button, 1};
#endif

	was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	if (g_front_state.direct_draw != NULL) {
		g_front_state.direct_draw->lpVtbl->FlipToGDISurface(
			g_front_state.direct_draw);
	}

#ifdef XVT_MODERN
	Aeron_ShowMessageBox(&options, NULL);
#else
	MessageBoxA(g_front_state.hWnd, text, g_window_name, 0x30);
#endif

	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 1;
}

/* The original build's frontend main loop. Only frontend_display_init and
 * frontend_display_init_preserving_network_session call it, and nothing calls them.
 * Makes the window and surfaces (frontend_display_init_main_window), returning 0
 * when that fails; runs g_front_state.mode_init_fn, returning 0 after a shutdown
 * when it returns nonzero; and sets the six text color codes again, codes 1 and
 * 4 differently from frontend_display_init_main_window. Then, while the
 * application is active, it presents a frame every g_front_state.frame_interval_ms
 * milliseconds, polls both joysticks every 100 ms, and after each present runs
 * one frame of the top screen: clears net_ready_player_left_this_frame and, while a
 * text fade runs, the fade color cache; pumps network packets; then, when the
 * screen has an update function, reads the keyboard state, locks the back
 * buffer, calls the update with g_front_state.frame_counter, calls the exit
 * function it read before the update when the update returned 1 or
 * screen_callbacks_dirty is 1, unlocks, pushes a queued screen, draws the cursor
 * when it is shown, clears the joysticks' released flags, raises the frame
 * counter, lowers the text fade, clears the click latches and services CD
 * audio: resumes suspended playback when due and, at a track's end, replays it
 * when looping or marks playback complete. A result of 1 fades the CD audio
 * volume to 0x200 while a track plays, closes the CD device and posts WM_CLOSE,
 * and no frame runs after it. Window messages are dispatched as they come,
 * active or not, and the loop returns the quit message's exit code. The modern
 * build returns 0 at once. */
// FUNCTION: XVT 0x4D3E40
uint32_t frontend_display_run_main_loop(void *hInstance,
					const void *hPrevInstance,
					const char *lpCmdLine, int nShowCmd)
{
#ifdef XVT_MODERN
	(void)hInstance;
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nShowCmd;
	return 0;
#else
	enum {
		SCREEN_CONTINUE = 0,
		SCREEN_FINISHED = 1,
		JOYSTICK_UPDATE_INTERVAL_MS = 100,
		WINDOW_CLOSE_MESSAGE = 0x10,
	};
	struct frontend_display_win32_message message;
	uint32_t frame_start;
	uint32_t joystick_update;
	int update_result;
	int frame_ready;

	(void)hPrevInstance;
	(void)lpCmdLine;
	update_result = SCREEN_CONTINUE;
	frame_ready = 0;
	if (frontend_display_init_main_window(hInstance, nShowCmd) == 0) {
		return 0;
	}
	if (g_front_state.mode_init_fn != NULL &&
	    g_front_state.mode_init_fn() != 0) {
		frontend_display_shutdown(1);
		return 0;
	}

	frame_start = GetTickCount();
	joystick_update = frame_start;
	g_front_state.text_color_codes[0] = 0xFFFF;
	g_front_state.text_color_codes[1] =
		frontend_display_pack_rgb(0x40, 0xC4, 0x40);
	g_front_state.text_color_codes[2] =
		frontend_display_pack_rgb(0xFF, 0, 0);
	g_front_state.text_color_codes[3] =
		frontend_display_pack_rgb(0xFF, 0xFF, 0);
	g_front_state.text_color_codes[4] =
		frontend_display_pack_rgb(0, 0, 0xFF);
	g_front_state.text_color_codes[5] =
		frontend_display_pack_rgb(0x80, 0x80, 0xFF);

	for (;;) {
		if (g_front_state.app_active != 0) {
			if (frame_ready != 0 &&
			    update_result == SCREEN_CONTINUE) {
				frontend_screen_exit_fn exit_fn;

				frame_ready = 0;
				g_front_state.net_ready_player_left_this_frame =
					0;
				if (g_front_state.text_fade_frames_left != 0) {
					memset(&g_front_state
							.text_fade_color_cache,
					       0,
					       sizeof(g_front_state
							      .text_fade_color_cache));
				}
				net_pump_incoming_packets();
				if (g_front_state
					    .screen_states
						    [g_front_state
							     .screen_stack_top]
					    .update_fn != NULL) {
					GetKeyboardState(
						g_front_state.key_state);
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
					exit_fn =
						g_front_state
							.screen_states
								[g_front_state
									 .screen_stack_top]
							.exit_fn;
					update_result =
						g_front_state
							.screen_states
								[g_front_state
									 .screen_stack_top]
							.update_fn(
								g_front_state
									.frame_counter);
					if (g_front_state.screen_callbacks_dirty ==
						    1 ||
					    update_result == SCREEN_FINISHED) {
						g_front_state
							.screen_callbacks_dirty =
							0;
						if (exit_fn != NULL) {
							exit_fn(g_front_state
									.frame_counter);
						}
					}
					frontend_display_unlock_back_buffer();
					if (g_front_state
						    .pending_screen_update_fn !=
					    NULL) {
						frontend_screen_push_state(
							g_front_state
								.pending_screen_update_fn,
							&g_front_state
								 .pending_screen_rect);
						g_front_state
							.pending_screen_update_fn =
							NULL;
					}
					if (g_front_state.cursor_visible == 1) {
						frontend_cursor_draw();
					}
					memset(g_front_state
						       .joystick_button_released
							       [0],
					       0,
					       sizeof(g_front_state
							      .joystick_button_released
								      [0]));
					memset(g_front_state
						       .joystick_button_released
							       [1],
					       0,
					       sizeof(g_front_state
							      .joystick_button_released
								      [1]));
					++g_front_state.frame_counter;
					if (update_result == SCREEN_FINISHED) {
						if (g_front_state.cd_audio_mci_device_id !=
							    0 &&
						    g_front_state.cd_audio_current_track !=
							    0 &&
						    g_front_state.cd_audio_playback_complete ==
							    0) {
							cd_audio_fade_aux_volume(
								g_front_state
									.cd_audio_track_cache
									.current_aux_volume,
								0x200, 2000);
						}
						cd_audio_close_device();
						PostMessageA(
							g_front_state.hWnd,
							WINDOW_CLOSE_MESSAGE, 0,
							0);
					}
					if (g_front_state
						    .text_fade_frames_left !=
					    0) {
						--g_front_state
							  .text_fade_frames_left;
					}
					g_front_state.mouse_left_click_latch =
						0;
					g_front_state.mouse_right_click_latch =
						0;
					if (g_front_state.cd_audio_suspend_state ==
						    CD_AUDIO_RESUME_PENDING &&
					    GetTickCount() >
						    g_front_state
							    .cd_audio_resume_due_ms) {
						cd_audio_resume_suspended_playback();
					}
					if (g_front_state.cd_audio_current_track !=
						    0 &&
					    GetTickCount() >
						    g_front_state
							    .cd_audio_track_end_ms) {
						if (g_front_state
							    .cd_audio_loop_current_track !=
						    0) {
							cd_audio_play_track_from_time(
								g_front_state
									.cd_audio_current_track,
								0, 0);
						} else {
							g_front_state
								.cd_audio_playback_complete =
								1;
						}
					}
				}
			}

			if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
				if (GetMessageA(&message, NULL, 0, 0) == 0) {
					return message.wParam;
				}
				TranslateMessage(&message);
				DispatchMessageA(&message);
				continue;
			}
			if (update_result == SCREEN_CONTINUE) {
				uint32_t now;

				now = GetTickCount();
				if ((int32_t)(now - frame_start) >=
				    g_front_state.frame_interval_ms) {
					frame_ready = 1;
					frame_start = now;
					frontend_display_present_frame();
				}
				if ((int32_t)(now - joystick_update) >=
				    JOYSTICK_UPDATE_INTERVAL_MS) {
					joystick_update_state(0);
					joystick_update_state(1);
					joystick_update = now;
				}
			}
		}

		else if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
			if (GetMessageA(&message, NULL, 0, 0) == 0) {
				return message.wParam;
			}
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}
	}
#endif
}

/* Makes the frontend's display and returns 1, or 0 when a step fails, after
 * frontend_display_report_direct_draw_init_failure unless the window failed. The
 * original build registers the window class and creates a visible popup window
 * the size of the screen, titled g_window_name, into g_front_state.hWnd; the
 * modern build uses the handle the host already stored there. Creates
 * DirectDraw on the video.cfg driver (frontend_display_load_driver_guid), else on
 * the default one, setting secondary_direct_draw_active only when the configured
 * driver was used; takes exclusive full-screen mode at 640 by 480 and
 * g_front_state.display_bpp; creates the primary surface and the back buffer, a
 * flip chain with one back buffer unless g_opt_no_fullscreen or g_no_page_flip is
 * set, else a separate 640 by 480 system-memory surface; and a 640 by 480
 * offscreen surface, recording pixel_format555 and both pitches. Loads the
 * default palette (frontend_display_load_palette) and sets it at 8 bits per
 * pixel, drops to normal cooperative level with g_opt_no_fullscreen, sets the six
 * text color codes, clears and presents both surfaces, loads the size-20 font,
 * finds the joysticks, sets the default cursor, moves the system cursor to (0,
 * 0), starts DirectSound (showing "Sound not available." when it fails), hides
 * the system cursor and allocates and zeroes
 * g_front_state.offscreen_backup_buffer, 480 rows of the offscreen pitch,
 * returning 1 even when that allocation fails. */
// FUNCTION: XVT 0x4D41E0
int frontend_display_init_main_window(void *hInstance, int nShowCmd)
{
#ifndef XVT_MODERN
	struct WNDCLASSA window_class;
#endif
	DDSURFACEDESC surface_desc;
	DDSCAPS attached_surface_caps;
	const DxGuid *driver_guid;
	void *window_handle;
	HRESULT result;
	(void)nShowCmd;

#ifdef XVT_MODERN
	/* The host shell owns the window, so the port keeps the handle it published. */
	(void)hInstance;
	window_handle = g_front_state.hWnd;
#else
	window_class.style = 8; /* CS_DBLCLKS */
	window_class.lpfnWndProc = frontend_display_wnd_proc;
	window_class.cbClsExtra = 0;
	window_class.cbWndExtra = 0;
	window_class.hInstance = hInstance;
	window_class.hIcon = LoadIconA(hInstance, 101);
	window_class.hCursor = LoadCursorA(NULL, 0x7F00); /* IDC_ARROW */
	window_class.hbrBackground = GetStockObject(4);	  /* BLACK_BRUSH */
	window_class.lpszMenuName = NULL;
	window_class.lpszClassName = g_window_name;
	RegisterClassA(&window_class);
	window_handle =
		CreateWindowExA(0, g_window_name, g_window_name, 0x90000000, 0,
				0, GetSystemMetrics(0), GetSystemMetrics(1),
				NULL, NULL, hInstance, NULL);
	if (window_handle == NULL) {
		return 0;
	}
	g_front_state.hWnd = window_handle;
	UpdateWindow(window_handle);
	SetFocus(window_handle);
#endif

	driver_guid = frontend_display_load_driver_guid();
#ifdef XVT_MODERN
	if (DirectDrawCreate_Compat(driver_guid, &g_front_state.direct_draw,
				    NULL) != 0) {
		if (DirectDrawCreate_Compat(NULL, &g_front_state.direct_draw,
					    NULL) != 0) {
#else
	if (DirectDrawCreate(driver_guid, &g_front_state.direct_draw, NULL) !=
	    0) {
		if (DirectDrawCreate(NULL, &g_front_state.direct_draw, NULL) !=
		    0) {
#endif
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

	result = g_front_state.direct_draw->lpVtbl->SetCooperativeLevel(
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

#ifdef XVT_MODERN
	xvt_render_frontend_reset();
#endif
	frontend_display_clear_back_buffer();
	frontend_display_clear_offscreen_surface();
	frontend_display_present_frame();
	if (frontend_text_load_font(20) != 1) {
		return frontend_display_report_direct_draw_init_failure(
			window_handle, 6);
	}

	joystick_init_devices();
	frontend_cursor_init();
#ifdef XVT_MODERN
	xvt_presentation_warp_classic(0, 0);
#else
	SetCursorPos(0, 0);
#endif
	if (frontend_sound_init_direct_sound(g_front_state.hWnd) == 0) {
		frontend_display_show_game_message_box("Sound not available.");
	}
#ifdef XVT_MODERN
	Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(0) >= 0) {
	}
#endif
	g_front_state.offscreen_backup_buffer =
		malloc(480 * g_front_state.offscreen_surface_pitch);
	if (g_front_state.offscreen_backup_buffer != NULL) {
		memset(g_front_state.offscreen_backup_buffer, 0,
		       480 * g_front_state.offscreen_surface_pitch);
	}
	return 1;
}

/* The original build's frontend entry; only game_main calls it, and nothing
 * calls game_main. Zeroes g_front_state, sets the window procedure mode and
 * g_shutdown_complete to 0, seeds rand with the tick count, finds the game and
 * CD paths, allocates the sound buffer and voice tables (0xB1BC and 0x90 bytes)
 * and the image table (0x8800 bytes), and returns 0 when one of those fails or
 * bpp is not 8 or 16. Then sets the clip to 0 to 639 by 0 to 479, clearing
 * after present on, display_bpp to bpp, the frame interval to 1000 / fps
 * milliseconds (an fps under 1 counts as 1), the first screen's update and exit
 * functions, mode_init_fn and Esc-to-close, and returns
 * frontend_display_run_main_loop's result. */
// FUNCTION: XVT 0x4D4770
uint32_t frontend_display_init(void *hInstance, const void *hPrevInstance,
			       const char *lpCmdLine, int nShowCmd,
			       frontend_screen_update_fn screen_update_fn,
			       frontend_screen_exit_fn screen_exit_fn,
			       int (*mode_init_fn)(void), int fps, int bpp)
{
	int frame_rate;
	int zero_value;

	memset(&g_front_state, 0, sizeof(g_front_state));
	g_front_state.frontend_display_wnd_proc_mode = 0;
	g_shutdown_complete = 0;
	srand(GetTickCount());
	file_detect_game_and_cd_paths("\\wave\\PBC\\Pb1los07.wav");
	g_front_state.frontend_sound_buffers = malloc(0xB1BC);
	if (g_front_state.frontend_sound_buffers == NULL) {
		return 0;
	}
	g_front_state.frontend_sound_voices = malloc(0x90);
	if (g_front_state.frontend_sound_voices == NULL) {
		free(g_front_state.frontend_sound_buffers);
		return 0;
	}
	g_front_state.resource_table = malloc(0x8800);
	if (g_front_state.resource_table == NULL) {
		free(g_front_state.frontend_sound_buffers);
		free(g_front_state.frontend_sound_voices);
		return 0;
	}
	if (bpp != 8 && bpp != 16) {
		return 0;
	}

	g_front_state.clip_max_x = 639;
	g_front_state.clip_max_y = 479;
	g_front_state.clear_back_buffer_after_present = 1;
	g_front_state.display_bpp = bpp;
	zero_value = 0;
	g_front_state.pixel_format555 = zero_value;
	g_front_state.clip_min_x = zero_value;
	g_front_state.clip_min_y = zero_value;
	g_front_state.char_write_idx = zero_value;
	g_front_state.char_read_idx = zero_value;
	g_front_state.resource_count = zero_value;
	g_front_state.cd_audio_saved_aux_volume = -1;
	frame_rate = fps;
	if (frame_rate <= zero_value) {
		frame_rate = 1;
	}
	g_front_state.frame_interval_ms = 1000 / frame_rate;
	g_front_state.screen_states[0].update_fn = screen_update_fn;
	g_front_state.screen_states[0].exit_fn = screen_exit_fn;
	g_front_state.mode_init_fn = mode_init_fn;
	g_front_state.escape_close_enabled = 1;
	return frontend_display_run_main_loop(hInstance, hPrevInstance,
					      lpCmdLine, nShowCmd);
}

/* Locks the back buffer and returns its pixels, setting
 * g_front_state.draw_surface_pitch to the back buffer's pitch; when
 * back_buffer_locked is already set it returns the pointer it holds. Retries
 * while the surface is still drawing, busy or obscured, restoring it when it is
 * lost; on any other failure it still sets back_buffer_locked and returns
 * back_buffer_desc.lpSurface unchecked. Returns NULL without DirectDraw or a back
 * buffer. Callers store the result in g_draw_surface_ptr; this does not. The
 * modern build also selects the back buffer as its renderer's target. */
// FUNCTION: XVT 0x4D48E0
uint8_t *frontend_display_lock_back_buffer(void)
{
	enum {
		FRONTEND_DDERR_SURFACEBUSY = -2005532242,
		FRONTEND_DDERR_SURFACEISOBSCURED = -2005532232,
	};

	HRESULT lock_result;

	if (g_front_state.direct_draw == NULL) {
		return NULL;
	}
	if (g_front_state.back_buffer_surface == NULL) {
		return NULL;
	}

#ifdef XVT_MODERN
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
#endif
	g_front_state.draw_surface_pitch = g_front_state.back_buffer_pitch;
	if (g_front_state.back_buffer_locked != 0) {
		return (uint8_t *)g_front_state.back_buffer_desc.lpSurface;
	}

	g_front_state.back_buffer_desc.dwSize =
		sizeof(g_front_state.back_buffer_desc);
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
 * sets the palette at 8 bits per pixel when palette_needs_set is 1, which no code
 * sets, and flips, retrying while the surface is still drawing or after
 * restoring lost surfaces; otherwise it copies the back buffer to the primary
 * surface at (0, 0) and returns early when that copy fails. With offscreen
 * restore on it then puts the offscreen surface back under the next frame:
 * after an activation it first copies g_front_state.offscreen_backup_buffer into
 * the offscreen surface, then copies the offscreen surface onto the back
 * buffer, in non-flip mode through frontend_display_restore_back_buffer, returning
 * early when the flip-mode copy fails. Last it clears the back buffer when
 * clear_back_buffer_after_present is set. Does nothing without DirectDraw. The
 * modern build also presents its renderer's frame after a successful flip or
 * copy and mirrors the copies in its targets. */
// FUNCTION: XVT 0x4D49A0
void frontend_display_present_frame(void)
{
	int vertical_blank_status;
	struct RECT source_rect;
	DDSURFACEDESC surface_desc;
	HRESULT result;

	if (g_front_state.direct_draw == NULL) {
		return;
	}

	if (g_front_state.back_buffer_locked != 0) {
		frontend_display_unlock_back_buffer();
	}

	source_rect.right = 640;
	source_rect.bottom = 480;
	source_rect.left = 0;
	source_rect.top = 0;

	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
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
					return;
				}
			} else if (result != DX_DDERR_WASSTILLDRAWING) {
				return;
			}
		}
	}

#ifdef XVT_MODERN
	if (result == DX_DD_OK) {
		xvt_render_frontend_present();
	}
#endif

	if (g_front_state.offscreen_restore_enabled != 0) {
		if (g_front_state.restore_offscreen_overlay_after_activate !=
		    0) {
			g_front_state.restore_offscreen_overlay_after_activate =
				0;
			if (g_front_state.offscreen_backup_buffer != NULL &&
			    g_front_state.offscreen_surface != NULL) {
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

#ifdef XVT_MODERN
					xvt_render_frontend_copy(
						XVT_TARGET_FRONT_BACKUP,
						XVT_TARGET_FRONT_OFFSCREEN);
#endif
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
#ifdef XVT_MODERN
			xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
						 XVT_TARGET_FRONT_BACK);
#endif

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

/* Fills the back buffer's 640 by 480 pixels with g_front_state.surface_clear_color
 * through a DirectDraw color fill, unlocking it first and, when it was locked,
 * locking it into g_draw_surface_ptr again afterward. Retries while the surface
 * is still drawing or after restoring lost surfaces, and gives up on any other
 * failure. Does nothing without DirectDraw or a back buffer. The modern build
 * also clears its renderer's back target. */
// FUNCTION: XVT 0x4D4C10
void frontend_display_clear_back_buffer(void)
{
	struct RECT rect;
	DDBLTFX effects;
	int was_locked;
	HRESULT result;

	if (g_front_state.direct_draw == NULL ||
	    g_front_state.back_buffer_surface == NULL) {
		return;
	}

	was_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	frontend_draw_rect_assign(&rect, 0, 0, 640, 480);
	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillColor = g_front_state.surface_clear_color;
	for (;;) {
		result = g_front_state.back_buffer_surface->lpVtbl->Blt(
			g_front_state.back_buffer_surface, &rect, NULL, NULL,
			DDBLT_COLORFILL, &effects);
		if (result == DX_DD_OK) {
			break;
		}
		if (result == DX_DDERR_SURFACELOST) {
			if (frontend_display_restore_lost_surfaces() !=
			    DX_DD_OK) {
				if (was_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			if (was_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return;
		}
	}

#ifdef XVT_MODERN
	xvt_render_frontend_clear(XVT_TARGET_FRONT_BACK,
				  g_front_state.surface_clear_color);
#endif

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
 * The modern build also selects its renderer's offscreen target. */
// FUNCTION: XVT 0x4D4E20
int frontend_display_lock_offscreen_surface(void)
{
	DDSURFACEDESC surface_desc;
	HRESULT result;

	if (g_front_state.direct_draw == NULL) {
		return 0;
	}
	if (g_front_state.offscreen_surface == NULL) {
		return 0;
	}

	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	for (;;) {
		result = g_front_state.offscreen_surface->lpVtbl->Lock(
			g_front_state.offscreen_surface, NULL, &surface_desc, 0,
			NULL);
		if (result == DX_DD_OK) {
			break;
		}
		if (result == DX_DDERR_SURFACELOST) {
			g_front_state.offscreen_surface->lpVtbl->Restore(
				g_front_state.offscreen_surface);
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			return 0;
		}
	}

	g_front_state.draw_surface_pitch =
		g_front_state.offscreen_surface_pitch;
	g_draw_surface_ptr = (uint8_t *)surface_desc.lpSurface;

#ifdef XVT_MODERN
	xvt_render_frontend_select(XVT_TARGET_FRONT_OFFSCREEN);
#endif
	return 1;
}

/* Unlocks the offscreen surface and makes the back buffer the drawing target
 * again. With save_to_backup nonzero, and g_draw_surface_ptr and the backup buffer
 * set, it first copies 480 rows of the offscreen pitch from g_draw_surface_ptr
 * into g_front_state.offscreen_backup_buffer. Then sets draw_surface_pitch to the
 * back buffer's pitch and g_draw_surface_ptr to the back buffer's pixels, locking
 * it when it is not locked. Returns 1, or 0 without DirectDraw or the surface.
 * Does not check that the offscreen surface was locked. The modern build
 * mirrors the copy and selects its back target. */
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

#ifdef XVT_MODERN
		xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
					 XVT_TARGET_FRONT_BACKUP);
#endif
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

#ifdef XVT_MODERN
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
#endif
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
 * g_front_state.surface_clear_color, the way frontend_display_clear_back_buffer fills
 * the back buffer, unlocking and relocking the back buffer around it. Does
 * nothing without DirectDraw or the offscreen surface. The modern build also
 * clears its renderer's offscreen target. */
// FUNCTION: XVT 0x4D4F80
void frontend_display_clear_offscreen_surface(void)
{
	struct RECT rect;
	DDBLTFX effects;
	int was_locked;
	HRESULT result;

	if (g_front_state.direct_draw == NULL ||
	    g_front_state.offscreen_surface == NULL) {
		return;
	}

	was_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	frontend_draw_rect_assign(&rect, 0, 0, 640, 480);
	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillColor = g_front_state.surface_clear_color;
	for (;;) {
		result = g_front_state.offscreen_surface->lpVtbl->Blt(
			g_front_state.offscreen_surface, &rect, NULL, NULL,
			DDBLT_COLORFILL, &effects);
		if (result == DX_DD_OK) {
			break;
		}
		if (result == DX_DDERR_SURFACELOST) {
			if (frontend_display_restore_lost_surfaces() !=
			    DX_DD_OK) {
				if (was_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			if (was_locked != 0) {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}
			return;
		}
	}

#ifdef XVT_MODERN
	xvt_render_frontend_clear(XVT_TARGET_FRONT_OFFSCREEN,
				  g_front_state.surface_clear_color);
#endif

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

/* Nothing calls this. Returns g_front_state.display_bpp / 8. */
// FUNCTION: XVT 0x4D5080
int frontend_display_get_bytes_per_pixel(void)
{
	return g_front_state.display_bpp / 8;
}

/* Nothing calls this. Does what frontend_display_init does, except that it
 * resets g_front_state with
 * frontend_display_reset_global_state_preserving_network_session, which keeps the
 * network session, and does not set g_shutdown_complete to 0 or
 * cd_audio_saved_aux_volume to -1. */
// FUNCTION: XVT 0x4D5090
uint32_t frontend_display_init_preserving_network_session(
	void *hInstance, const void *hPrevInstance, const char *lpCmdLine,
	int nShowCmd, frontend_screen_update_fn screen_update_fn,
	frontend_screen_exit_fn screen_exit_fn, int (*mode_init_fn)(void),
	int fps, int bpp)
{
	int frame_rate;
	int zero_value;

	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nShowCmd;

	frontend_display_reset_global_state_preserving_network_session();
	srand(GetTickCount());
	file_detect_game_and_cd_paths("\\wave\\PBC\\Pb1los07.wav");
	g_front_state.frontend_sound_buffers = malloc(0xB1BC);
	if (g_front_state.frontend_sound_buffers == NULL) {
		return 0;
	}
	g_front_state.frontend_sound_voices = malloc(0x90);
	if (g_front_state.frontend_sound_voices == NULL) {
		free(g_front_state.frontend_sound_buffers);
		return 0;
	}
	g_front_state.resource_table = malloc(0x8800);
	if (g_front_state.resource_table == NULL) {
		free(g_front_state.frontend_sound_buffers);
		free(g_front_state.frontend_sound_voices);
		return 0;
	}
	if (bpp != 8 && bpp != 16) {
		return 0;
	}

	g_front_state.clip_max_x = 639;
	g_front_state.clip_max_y = 479;
	g_front_state.clear_back_buffer_after_present = 1;
	g_front_state.display_bpp = bpp;
	zero_value = 0;
	g_front_state.pixel_format555 = zero_value;
	g_front_state.clip_min_x = zero_value;
	g_front_state.clip_min_y = zero_value;
	g_front_state.char_write_idx = zero_value;
	g_front_state.char_read_idx = zero_value;
	g_front_state.resource_count = zero_value;
	frame_rate = fps;
	if (frame_rate <= g_front_state.resource_count) {
		frame_rate = 1;
	}
	g_front_state.frame_interval_ms = 1000 / frame_rate;
	g_front_state.screen_states[0].update_fn = screen_update_fn;
	g_front_state.screen_states[0].exit_fn = screen_exit_fn;
	g_front_state.mode_init_fn = mode_init_fn;
	g_front_state.escape_close_enabled = 1;
	return frontend_display_run_main_loop(hInstance, hPrevInstance,
					      lpCmdLine, nShowCmd);
}

/* Only frontend_display_init_preserving_network_session calls this, and nothing
 * calls that. Zeroes g_front_state but keeps its DirectPlay interface, the
 * application and joined-session GUIDs, the host and group player ids,
 * net_is_host, the session name and the local player record; then sets
 * frontend_post_reset_marker to 1 and net_player_count to 1, with the local player
 * first. With DirectPlay it refreshes the player roster and, when the host's id
 * is no longer listed, makes the host the lowest of the id at net_players[32]
 * and the nonzero ids of ready players. That entry is one past the array, so it
 * reads the net_runtime_local_player field that follows. */
// FUNCTION: XVT 0x4D51E0
void frontend_display_reset_global_state_preserving_network_session(void)
{
	IDirectPlay2A *net_direct_play;
	GUID net_app_guid;
	GUID net_joined_session_guid;
	DPID net_host_player_id;
	DPID net_group_dplay_id;
	int net_is_host;
	char net_session_name[32];
	struct net_player_info net_runtime_local_player;
	int player_index;
	DPID candidate_host_player_id;

	net_direct_play = g_front_state.net_direct_play;
	net_app_guid = g_front_state.net_app_guid;
	net_joined_session_guid = g_front_state.net_joined_session_guid;
	net_host_player_id = g_front_state.net_host_player_id;
	net_group_dplay_id = g_front_state.net_group_dplay_id;
	net_is_host = g_front_state.net_is_host;
	memcpy(net_session_name, g_front_state.net_session_name,
	       sizeof(net_session_name));
	net_runtime_local_player = g_front_state.net_runtime_local_player;

	memset(&g_front_state, 0, sizeof(g_front_state));

	g_front_state.net_direct_play = net_direct_play;
	g_front_state.net_app_guid = net_app_guid;
	g_front_state.net_joined_session_guid = net_joined_session_guid;
	g_front_state.net_host_player_id = net_host_player_id;
	g_front_state.net_group_dplay_id = net_group_dplay_id;
	g_front_state.net_is_host = net_is_host;
	memcpy(g_front_state.net_session_name, net_session_name,
	       sizeof(g_front_state.net_session_name));
	g_front_state.net_runtime_local_player = net_runtime_local_player;
	g_front_state.frontend_post_reset_marker = 1;
	g_front_state.net_player_count = 1;
	g_front_state.net_players[0] = net_runtime_local_player;

	if (g_front_state.net_direct_play != NULL) {
		net_refresh_player_roster();
		for (player_index = 0; player_index < 32; ++player_index) {
			if (g_front_state.net_players[player_index].player_id ==
			    g_front_state.net_host_player_id) {
				break;
			}
		}
		if (player_index == 32) {
			candidate_host_player_id =
				g_front_state.net_players[player_index]
					.player_id;
			for (player_index = 0; player_index < 32;
			     ++player_index) {
				if (g_front_state.net_players[player_index]
						    .player_id != 0 &&
				    g_front_state.net_players[player_index]
						    .ready_flag != 0 &&
				    candidate_host_player_id >
					    g_front_state
						    .net_players[player_index]
						    .player_id) {
					candidate_host_player_id =
						g_front_state
							.net_players
								[player_index]
							.player_id;
				}
			}
			g_front_state.net_host_player_id =
				candidate_host_player_id;
		}
	}
}

/* Saves the back buffer as the first frontscreen<n>.bmp that does not open, n
 * counting from 0, through front_image_save_bmp_file with the display palette, and
 * returns that result. Locks the back buffer into g_draw_surface_ptr and unlocks
 * it. Only frontend_display_main_wnd_proc calls it, on Alt+O, so only the original
 * build does. The modern build looks for the names in the user storage root. */
// FUNCTION: XVT 0x4D5380
int frontend_display_capture_screenshot(void)
{
	char file_name[64];
	int sequence;
	xvt_file *stream;
	int result;

	sequence = 0;
	for (;;) {
		sprintf(file_name, "frontscreen%d.bmp", sequence);
#ifdef XVT_MODERN
		stream = xvt_storage_open_root(AERON_VFS_ROOT_USER, file_name,
					       g_file_mode_read_binary);
#else
		stream = FILE_RAW_OPEN(file_name, g_file_mode_read_binary);
#endif
		if (stream == NULL) {
			break;
		}
#ifdef XVT_MODERN
		file_close(stream);
#else
		FILE_RAW_CLOSE(stream);
#endif
		++sequence;
	}

	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	result = front_image_save_bmp_file(
		file_name, g_draw_surface_ptr, 640, 480,
		g_front_state.back_buffer_pitch, g_front_state.display_bpp,
		g_front_state.pixel_format555, g_front_state.display_palette);
	frontend_display_unlock_back_buffer();
	return result;
}

/* Returns g_draw_surface_ptr, where flight_surface_lock points flight's drawing
 * while g_flight_render_to_frontend is 1. */
// FUNCTION: XVT 0x4D5410
uint8_t *frontend_display_get_draw_surface_for_flight(void)
{
	return g_draw_surface_ptr;
}

/* Only the original build calls this, from frontend_screen_run_modal: runs one
 * frame of the modal screen on top. Dispatches window messages until a frame is
 * due, presenting it and polling the joysticks every 100 ms the way
 * frontend_display_run_main_loop does, and returns 2 when the quit message
 * arrives. Then clears the fade color cache while a text fade runs, pumps
 * network packets and, when the top screen has an update function, runs it as
 * the main loop does but pushes no queued screen; returns 1 when the update
 * returned 1, before lowering the text fade and clearing the click latches. At
 * a CD track's end it replays a looping track or marks playback complete.
 * Returns 0 otherwise. The modern build returns xvt_frontend_task_run_frame's
 * result. */
// FUNCTION: XVT 0x4D5420
int frontend_display_run_frame(void)
{
#ifdef XVT_MODERN
	return xvt_frontend_task_run_frame();
#else
	enum {
		FRAME_CONTINUE = 0,
		FRAME_FINISHED = 1,
		FRAME_QUIT = 2,
		JOYSTICK_UPDATE_INTERVAL_MS = 100,
	};
	struct frontend_display_win32_message message;
	uint32_t frame_start;
	uint32_t joystick_update;
	int frame_ready;

	frame_start = GetTickCount();
	joystick_update = frame_start;
	frame_ready = 0;
	for (;;) {
		if (g_front_state.app_active != 0) {
			uint32_t now;

			if (frame_ready != 0) {
				break;
			}
			if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
				if (GetMessageA(&message, NULL, 0, 0) == 0) {
					return FRAME_QUIT;
				}
				TranslateMessage(&message);
				DispatchMessageA(&message);
				continue;
			}
			now = GetTickCount();
			if ((int32_t)(now - frame_start) >=
			    g_front_state.frame_interval_ms) {
				frame_ready = 1;
				frame_start = now;
				frontend_display_present_frame();
			}
			if ((int32_t)(now - joystick_update) >=
			    JOYSTICK_UPDATE_INTERVAL_MS) {
				joystick_update_state(0);
				joystick_update_state(1);
				joystick_update = now;
			}
		} else {
			if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
				if (GetMessageA(&message, NULL, 0, 0) == 0) {
					return FRAME_QUIT;
				}
				TranslateMessage(&message);
				DispatchMessageA(&message);
			}
		}
	}
	if (g_front_state.text_fade_frames_left != 0) {
		memset(&g_front_state.text_fade_color_cache, 0,
		       sizeof(g_front_state.text_fade_color_cache));
	}
	net_pump_incoming_packets();
	if (g_front_state.screen_states[g_front_state.screen_stack_top]
		    .update_fn != NULL) {
		frontend_screen_exit_fn exit_fn;
		int update_result;
		GetKeyboardState(g_front_state.key_state);
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
		exit_fn = g_front_state
				  .screen_states[g_front_state.screen_stack_top]
				  .exit_fn;
		update_result =
			g_front_state
				.screen_states[g_front_state.screen_stack_top]
				.update_fn(g_front_state.frame_counter);
		if (g_front_state.screen_callbacks_dirty == 1 ||
		    update_result == FRAME_FINISHED) {
			g_front_state.screen_callbacks_dirty = 0;
			if (exit_fn != NULL) {
				exit_fn(g_front_state.frame_counter);
			}
		}
		frontend_display_unlock_back_buffer();
		if (g_front_state.cursor_visible == 1) {
			frontend_cursor_draw();
		}
		memset(g_front_state.joystick_button_released[0], 0,
		       sizeof(g_front_state.joystick_button_released[0]));
		memset(g_front_state.joystick_button_released[1], 0,
		       sizeof(g_front_state.joystick_button_released[1]));
		++g_front_state.frame_counter;
		if (update_result == FRAME_FINISHED) {
			return FRAME_FINISHED;
		}
		if (g_front_state.text_fade_frames_left != 0) {
			--g_front_state.text_fade_frames_left;
		}
		g_front_state.mouse_left_click_latch = 0;
		g_front_state.mouse_right_click_latch = 0;
		if (g_front_state.cd_audio_current_track != 0 &&
		    GetTickCount() > g_front_state.cd_audio_track_end_ms) {
			if (g_front_state.cd_audio_loop_current_track != 0) {
				cd_audio_play_track_from_time(
					g_front_state.cd_audio_current_track, 0,
					0);
				return FRAME_CONTINUE;
			}
			g_front_state.cd_audio_playback_complete = 1;
		}
	}
	return FRAME_CONTINUE;
#endif
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
	DDSURFACEDESC surface_desc;
	DDSCAPS attached_surface_caps;
	void *window_handle;
	HRESULT result;

	window_handle = g_front_state.hWnd;
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
#ifdef XVT_MODERN
	xvt_presentation_warp_classic(0, 0);
#else
	SetCursorPos(0, 0);
#endif

#ifdef XVT_MODERN
	xvt_render_frontend_reset();
#endif
	frontend_display_clear_offscreen_surface();
	frontend_display_clear_back_buffer();
	frontend_display_present_frame();
	if (frontend_sound_init_direct_sound(g_front_state.hWnd) == 0) {
		frontend_display_show_game_message_box("Sound not available.");
	}
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	if (g_front_state.offscreen_backup_buffer == NULL) {
		g_front_state.offscreen_backup_buffer =
			malloc(480 * g_front_state.offscreen_surface_pitch);
		if (g_front_state.offscreen_backup_buffer != NULL) {
			memset(g_front_state.offscreen_backup_buffer, 0,
			       480 * g_front_state.offscreen_surface_pitch);
		}
	}
	return 1;
}

/* Sets g_front_state.frontend_display_wnd_proc_mode, which picks
 * frontend_display_wnd_proc's handler: 0 frontend, 1 flight, 2 movie. */
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

/* Only game_main calls this, and nothing calls game_main. The original build
 * looks for a window whose class and title are both g_window_name; when one
 * exists it restores it (ShowWindowAsync with 9, SW_RESTORE) and returns 1,
 * else 0. The modern build returns 0. */
// FUNCTION: XVT 0x4D5B90
int win32_check_single_instance(void)
{
#ifdef XVT_MODERN
	return 0;
#else
	void *window = FindWindowA(g_window_name, g_window_name);
	if (window != NULL) {
		ShowWindowAsync(window, 9);
		return 1;
	}
	return 0;
#endif
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
	xvt_file *stream;
	int read_succeeded;

	stream = file_open("video.cfg", "rb");
	if (stream == NULL) {
		return NULL;
	}
	read_succeeded =
		file_read_bytes(stream, &g_configured_direct_draw_driver_guid,
				sizeof(g_configured_direct_draw_driver_guid));
	file_close(stream);
	return read_succeeded != 0 ? &g_configured_direct_draw_driver_guid
				   : NULL;
}

/* Only the original build calls this. While secondary_direct_draw_active is set it
 * draws text through GDI across the whole desktop, centered (DrawTextA format
 * 0x25), white on black in 12-pixel Times New Roman, then overlay_text, when not
 * NULL, in red at the top and again at the bottom. Returns 1, or 0 when the
 * flag is clear or a device context or font cannot be made. The modern build
 * returns 0. */
// FUNCTION: XVT 0x4D5C70
int frontend_display_draw_gdi_text_on_desktop(const struct RECT *unused,
					      const char *text,
					      const char *overlay_text)
{
#ifdef XVT_MODERN
	(void)unused;
	(void)text;
	(void)overlay_text;
	return 0;
#else
	void *dc;
	void *font;
	void *previous_object;
	struct RECT rect;

	(void)unused;
	if (g_front_state.secondary_direct_draw_active == 0) {
		return 0;
	}
	dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
	if (dc == NULL) {
		return 0;
	}
	frontend_draw_rect_assign(&rect, 0, 0, GetSystemMetrics(0),
				  GetSystemMetrics(1));
	font = CreateFontA(-12, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 3, 0x12,
			   "times new roman");
	if (font == NULL) {
		DeleteDC(dc);
		return 0;
	}
	previous_object = SelectObject(dc, font);
	SetMapMode(dc, 1);
	SetTextCharacterExtra(dc, 0);
	SetTextColor(dc, 0xFFFFFF);
	SetBkColor(dc, 0);
	SetBkMode(dc, 2);
	DrawTextA(dc, text, strlen(text), &rect, 0x25);
	if (overlay_text != NULL) {
		SetTextColor(dc, 0xFF);
		DrawTextA(dc, overlay_text, -1, &rect, 0x21);
		DrawTextA(dc, overlay_text, -1, &rect, 0x29);
	}
	SelectObject(dc, previous_object);
	DeleteObject(font);
	DeleteDC(dc);
	return 1;
#endif
}

/* Only the original build calls this. While secondary_direct_draw_active is set it
 * fills the whole desktop black through GDI and returns 1; returns 0 when the
 * flag is clear or no device context can be made. The modern build returns
 * 0. */
// FUNCTION: XVT 0x4D5DC0
int frontend_display_clear_desktop_gdi(const struct RECT *unused)
{
#ifdef XVT_MODERN
	(void)unused;
	return 0;
#else
	void *dc;
	struct RECT rect;

	(void)unused;

	if (g_front_state.secondary_direct_draw_active == 0) {
		return 0;
	}

	dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
	if (dc == NULL) {
		return 0;
	}

	SelectObject(dc, GetStockObject(4));
	frontend_draw_rect_assign(&rect, 0, 0, GetSystemMetrics(0),
				  GetSystemMetrics(1));
	Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
	DeleteDC(dc);
	return 1;
#endif
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
	int index;
	unsigned int best_distance;
	int best_index;
	struct frontend_palette_entry *entry;

	switch (g_front_state.display_bpp) {
	case 8:
		best_distance = 0x7FFFFFFFu;
		best_index = 1;
		for (index = 1; index < 256; ++index) {
			int red_delta;
			int green_delta;
			int blue_delta;
			unsigned int distance;

			entry = &g_front_state.display_palette[index];
			red_delta = (int)entry->red - r;
			if (red_delta < 0) {
				red_delta = -red_delta;
			}
			green_delta = (int)entry->green - g;
			if (green_delta < 0) {
				green_delta = -green_delta;
			}
			blue_delta = (int)entry->blue - b;
			if (blue_delta < 0) {
				blue_delta = -blue_delta;
			}
			distance = g_color_dist_lut[blue_delta];
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
 * g_draw_surface_ptr on the back buffer, and unlocks the back buffer when it was
 * not locked before. Returns 1. The modern build mirrors the copy in its
 * renderer's targets. */
// FUNCTION: XVT 0x4DC9B0
int frontend_display_save_back_buffer(void)
{
	int was_back_buffer_locked;
	uint8_t *source;
	uint8_t *destination;
	int row;

#ifdef XVT_MODERN
	xvt_render_frontend_suppress(1);
#endif
	was_back_buffer_locked = g_front_state.back_buffer_locked;
	source = frontend_display_lock_back_buffer();
	frontend_display_lock_offscreen_surface();
	destination = g_draw_surface_ptr;
	for (row = 480; row != 0; --row) {
		memcpy(destination, source,
		       (size_t)(80 * (g_front_state.display_bpp & 0xFFFFFFF8)));
		destination += g_front_state.offscreen_surface_pitch;
		source += g_front_state.back_buffer_pitch;
	}
	frontend_display_unlock_offscreen_surface(0);
	if (was_back_buffer_locked == 0) {
		frontend_display_unlock_back_buffer();
	}

#ifdef XVT_MODERN
	xvt_render_frontend_suppress(0);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_BACK,
				 XVT_TARGET_FRONT_OFFSCREEN);
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
#endif
	return 1;
}

/* Copies the offscreen surface into the back buffer: 480 rows of 80 *
 * (display_bpp & 0xFFFFFFF8) bytes, 640 pixels, locking and unlocking as
 * frontend_display_save_back_buffer does. Returns 1. The modern build mirrors the
 * copy in its renderer's targets. */
// FUNCTION: XVT 0x4DCA20
int frontend_display_restore_back_buffer(void)
{
	int was_back_buffer_locked;
	uint8_t *destination;
	uint8_t *source;
	int row;

#ifdef XVT_MODERN
	xvt_render_frontend_suppress(1);
#endif
	was_back_buffer_locked = g_front_state.back_buffer_locked;
	destination = frontend_display_lock_back_buffer();
	frontend_display_lock_offscreen_surface();
	source = g_draw_surface_ptr;
	for (row = 480; row != 0; --row) {
		memcpy(destination, source,
		       (size_t)(80 * (g_front_state.display_bpp & 0xFFFFFFF8)));
		source += g_front_state.offscreen_surface_pitch;
		destination += g_front_state.back_buffer_pitch;
	}
	frontend_display_unlock_offscreen_surface(0);
	if (was_back_buffer_locked == 0) {
		frontend_display_unlock_back_buffer();
	}

#ifdef XVT_MODERN
	xvt_render_frontend_suppress(0);
	xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
				 XVT_TARGET_FRONT_BACK);
	xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
#endif
	return 1;
}

/* Builds the frontend's 256-entry palette and returns the pointer DirectDraw's
 * CreatePalette fills with a palette made from it; the modern build starts that
 * pointer at NULL, the original build leaves it unset before the call. Starts
 * from a 3-3-2 color cube: entry i has red 255 * ((i & 0xE0) >> 5) / 7, green
 * 255 * ((i & 0x1C) >> 2) / 7 and blue 255 * (i & 3) / 3. With lpName set it
 * then takes the colors of the bitmap resource lpName (original build only) or
 * else the bitmap file lpName, when it has 8 bits per pixel or fewer; both
 * callers pass NULL, so they get the cube. When the returned pointer is not
 * NULL it copies the entries into g_front_state.display_palette, with entry 0
 * black and entry 255 white. */
// FUNCTION: XVT 0x4F0E30
IDirectDrawPalette *frontend_display_load_palette(IDirectDraw *p_dd,
						  const char *lp_name)
{
	IDirectDrawPalette *palette;
	struct frontend_display_bmp_file_header file_header;
	struct frontend_display_bmp_info_header info_header;
	struct frontend_palette_entry entries[256];
	struct frontend_palette_entry *entry;
	int index;
	int color_count;

	entry = entries;
	index = 0;
	do {
		entry->red = (uint8_t)(255 * ((index & 0xE0) >> 5) / 7);
		entry->green = (uint8_t)(255 * ((index & 0x1C) >> 2) / 7);
		entry->blue = (uint8_t)(255 * (index & 3) / 3);
		entry->flags = 0;
		++entry;
		++index;
	} while (entry < entries + 256);

	if (lp_name != NULL) {
#ifndef XVT_MODERN
		void *resource_info;

		resource_info = FindResourceA(NULL, lp_name, 2);
		if (resource_info != NULL) {
			uint8_t *resource_data;
			uint8_t *source_entry;
			uint8_t color;
			uint16_t bits_per_pixel;

			resource_data =
				LockResource(LoadResource(NULL, resource_info));
			source_entry =
				resource_data + *(uint32_t *)resource_data;
			if (resource_data != NULL &&
			    *(uint32_t *)resource_data >=
				    sizeof(struct
					   frontend_display_bmp_info_header) &&
			    (bits_per_pixel =
				     *(uint16_t *)(resource_data + 14)) <= 8) {
				color_count = *(uint32_t *)(resource_data + 32);
				if (color_count == 0) {
					color_count = 1 << bits_per_pixel;
				}
			} else {
				color_count = 0;
			}
			if (color_count > 0) {
				entry = entries;
				do {
					color = source_entry[2];
					entry->red = color;
					color = source_entry[1];
					entry->green = color;
					color = source_entry[0];
					entry->blue = color;
					entry->flags = 0;
					++entry;
					source_entry += 4;
					--color_count;
				} while (color_count != 0);
			}
		} else {
			int file;

			file = _lopen(lp_name, 0);
			if (file != -1) {
				_lread(file, &file_header, sizeof(file_header));
				_lread(file, &info_header, sizeof(info_header));
				_lread(file, entries, sizeof(entries));
				_lclose(file);
				if (info_header.header_size ==
				    sizeof(info_header)) {
					if (info_header.bits_per_pixel <= 8) {
						color_count =
							info_header.colors_used;
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
					uint8_t red;

					red = entries[index].red;
					entries[index].red =
						entries[index].blue;
					entries[index].blue = red;
				}
			}
		}
#else
		xvt_file *stream;

		stream = file_open(lp_name, "rb");
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
				uint8_t red;

				red = entries[index].red;
				entries[index].red = entries[index].blue;
				entries[index].blue = red;
			}
		}
#endif
	}

#ifdef XVT_MODERN
	palette = NULL;
#endif
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

/* Only frontend_display_set_surface_color_key calls this, and nothing calls that.
 * Returns the surface's pixel value for the COLORREF color, or for color
 * 0xFFFFFFFF the surface's first pixel; 0xFFFFFFFF when the lock fails. The
 * original build has GDI set the first pixel to color, reads it back through a
 * lock, masked to the pixel's bits, and puts the old pixel back; the modern
 * build computes the value from the surface's channel masks. */
// FUNCTION: XVT 0x4F1070
uint32_t
frontend_display_convert_color_ref_to_surface_pixel(IDirectDrawSurface *surface,
						    uint32_t color)
{
#ifdef XVT_MODERN
	uint32_t surface_pixel;
	uint32_t red;
	uint32_t green;
	uint32_t blue;
	uint32_t red_mask;
	uint32_t green_mask;
	uint32_t blue_mask;
	uint32_t red_shift;
	uint32_t green_shift;
	uint32_t blue_shift;
	uint32_t red_max;
	uint32_t green_max;
	uint32_t blue_max;
	HRESULT lock_result;
	DDSURFACEDESC surface_desc;

	surface_pixel = UINT32_MAX;
	surface_desc.dwSize = sizeof(surface_desc);
	do {
		lock_result = surface->lpVtbl->Lock(surface, NULL,
						    &surface_desc, 0, NULL);
	} while (lock_result == DX_DDERR_WASSTILLDRAWING);
	if (lock_result != 0) {
		return surface_pixel;
	}
	if (color == UINT32_MAX) {
		surface_pixel = *(uint32_t *)surface_desc.lpSurface;
	} else {
		red = color & 0xFF;
		green = (color >> 8) & 0xFF;
		blue = (color >> 16) & 0xFF;
		red_mask = surface_desc.ddpfPixelFormat.dwRBitMask;
		green_mask = surface_desc.ddpfPixelFormat.dwGBitMask;
		blue_mask = surface_desc.ddpfPixelFormat.dwBBitMask;
		red_shift = 0;
		green_shift = 0;
		blue_shift = 0;
		while (red_mask != 0 && ((red_mask >> red_shift) & 1) == 0) {
			++red_shift;
		}
		while (green_mask != 0 &&
		       ((green_mask >> green_shift) & 1) == 0) {
			++green_shift;
		}
		while (blue_mask != 0 && ((blue_mask >> blue_shift) & 1) == 0) {
			++blue_shift;
		}
		red_max = red_mask >> red_shift;
		green_max = green_mask >> green_shift;
		blue_max = blue_mask >> blue_shift;
		surface_pixel =
			(((red * red_max / 255) << red_shift) & red_mask) |
			(((green * green_max / 255) << green_shift) &
			 green_mask) |
			(((blue * blue_max / 255) << blue_shift) & blue_mask);
	}
	if (surface_desc.ddpfPixelFormat.dwRGBBitCount < 32) {
		surface_pixel &=
			(1u << surface_desc.ddpfPixelFormat.dwRGBBitCount) - 1;
	}
	surface->lpVtbl->Unlock(surface, NULL);
	return surface_pixel;
#else
	uint32_t surface_pixel;
	uint32_t original_color;
	HRESULT lock_result;
	void *dc;
	DDSURFACEDESC surface_desc;

	surface_pixel = UINT32_MAX;
	if (color != UINT32_MAX &&
	    ((frontend_display_surface_get_dc_func)surface->lpVtbl->GetDC)(
		    surface, &dc) == 0) {
		original_color = GetPixel(dc, 0, 0);
		SetPixel(dc, 0, 0, color);
		((frontend_display_surface_release_dc_func)
			 surface->lpVtbl->ReleaseDC)(surface, dc);
	} else {
		original_color = surface_desc.dwSize;
	}
	surface_desc.dwSize = sizeof(surface_desc);
	do {
		lock_result = surface->lpVtbl->Lock(surface, NULL,
						    &surface_desc, 0, NULL);
	} while (lock_result == DX_DDERR_WASSTILLDRAWING);
	if (lock_result == 0) {
		surface_pixel = *(uint32_t *)surface_desc.lpSurface &
				((1u << (int8_t)surface_desc.ddpfPixelFormat
						.dwRGBBitCount) -
				 1);
		surface->lpVtbl->Unlock(surface, NULL);
	}
	if (color != UINT32_MAX &&
	    ((frontend_display_surface_get_dc_func)surface->lpVtbl->GetDC)(
		    surface, &dc) == 0) {
		SetPixel(dc, 0, 0, original_color);
		((frontend_display_surface_release_dc_func)
			 surface->lpVtbl->ReleaseDC)(surface, dc);
	}
	return surface_pixel;
#endif
}

/* Nothing calls this. Sets the surface's source color key to the pixel value
 * frontend_display_convert_color_ref_to_surface_pixel gives for the COLORREF color
 * and returns SetColorKey's result. */
// FUNCTION: XVT 0x4F1160
HRESULT frontend_display_set_surface_color_key(IDirectDrawSurface *surface,
					       uint32_t color)
{
	DDCOLORKEY color_key;

	color_key.dwColorSpaceLowValue =
		frontend_display_convert_color_ref_to_surface_pixel(surface,
								    color);
	color_key.dwColorSpaceHighValue = color_key.dwColorSpaceLowValue;
	return surface->lpVtbl->SetColorKey(surface, DDCKEY_SRCBLT, &color_key);
}
