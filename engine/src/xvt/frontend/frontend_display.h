#ifndef XVT_FRONTEND_FRONTEND_DISPLAY_H
#define XVT_FRONTEND_FRONTEND_DISPLAY_H

#include <stdint.h>

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One palette color in DirectDraw's entry layout; g_front_state.display_palette
 * holds 256 of them. */
struct frontend_palette_entry {
	uint8_t red;   /* Red, 0 to 255. */
	uint8_t green; /* Green, 0 to 255. */
	uint8_t blue;  /* Blue, 0 to 255. */
	/* DirectDraw's entry flags; 0 in every entry
	 * frontend_display_load_palette builds. No code reads the field itself;
	 * DirectDraw gets it with the entries. */
	uint8_t flags;
};

extern int g_pixel_format_code;
extern int g_flight_render_to_frontend;
extern int g_opt_no_fullscreen;
extern int g_no_page_flip;
extern int g_opt_skip_intro;
extern int g_opt_is_host;
extern int g_opt_is_client;
extern char *g_cmd_line;
extern int g_shutdown_complete;
extern void *g_cursor_save_buffer;
extern int g_game_main_skip_intro_relaunch_gate;
extern const unsigned int g_color_dist_lut[256];

int game_main(void *hInstance, void *hPrevInstance, char *lpCmdLine,
	      int nShowCmd);
HRESULT frontend_display_restore_lost_surfaces(void);
void frontend_display_shutdown(int b_destroy_window);
int32_t AERON_DXAPI frontend_display_main_wnd_proc(void *hWnd, unsigned int Msg,
						   uint32_t wParam,
						   int32_t lParam);
int32_t AERON_DXAPI frontend_display_wnd_proc(void *hWnd, unsigned int Msg,
					      uint32_t wParam, int32_t lParam);
int frontend_display_report_direct_draw_init_failure(void *hWnd, int stage);
int frontend_display_show_game_message_box(const char *text);
uint32_t frontend_display_run_main_loop(void *hInstance, void *hPrevInstance,
					char *lpCmdLine, int nShowCmd);
/* drift-ok: camelcase -- WinMain's argument names */
int frontend_display_init_main_window(void *hInstance, int nShowCmd);
uint32_t frontend_display_init(void *hInstance, void *hPrevInstance,
			       char *lpCmdLine, int nShowCmd,
			       frontend_screen_update_fn screen_update_fn,
			       frontend_screen_exit_fn screen_exit_fn,
			       int (*mode_init_fn)(void), int fps, int bpp);
uint8_t *frontend_display_lock_back_buffer(void);
void frontend_display_unlock_back_buffer(void);
void frontend_display_present_frame(void);
void frontend_display_disable_clear_after_present(void);
void frontend_display_set_surface_clear_color(uint32_t color);
void frontend_display_clear_back_buffer(void);
void frontend_display_get_screen_clip_rect(struct RECT *out_rect);
void frontend_display_set_screen_clip_rect640x480(const struct RECT *src);
void frontend_display_disable_escape_close(void);
int frontend_display_get_frame_counter(void);
int frontend_display_set_frame_rate(int fps);
int frontend_display_lock_offscreen_surface(void);
int frontend_display_unlock_offscreen_surface(int save_to_backup);
int frontend_display_enable_offscreen_restore(void);
int frontend_display_disable_offscreen_restore(void);
void frontend_display_clear_offscreen_surface(void);
int frontend_display_get_pixel_format555(void);
int frontend_display_get_frontend_or_flight_draw_pitch(void);
int frontend_display_get_bytes_per_pixel(void);
uint32_t frontend_display_init_preserving_network_session(
	void *hInstance, void *hPrevInstance, char *lpCmdLine, int nShowCmd,
	frontend_screen_update_fn screen_update_fn,
	frontend_screen_exit_fn screen_exit_fn, int (*mode_init_fn)(void),
	int fps, int bpp);
void frontend_display_reset_global_state_preserving_network_session(void);
int frontend_display_capture_screenshot(void);
uint8_t *frontend_display_get_draw_surface_for_flight(void);
int frontend_display_run_frame(void);
void *frontend_display_get_main_window_handle(void);
IDirectDraw *frontend_display_get_direct_draw(void);
int frontend_display_release_surfaces_for_flight(void);
int frontend_display_reinit_surfaces(void);
void frontend_display_set_wnd_proc_mode(uint8_t mode);
int frontend_display_get_wnd_proc_mode(void);
int win32_check_single_instance(void);
void frontend_display_flip_direct_draw_to_gdi_surface(void);
const DxGuid *frontend_display_load_driver_guid(void);
int frontend_display_draw_gdi_text_on_desktop(const struct RECT *unused,
					      const char *text,
					      const char *overlay_text);
int frontend_display_clear_desktop_gdi(const struct RECT *unused);
int frontend_display_is_secondary_direct_draw_active(void);
void frontend_display_set_palette(void);
int frontend_display_pack_rgb(uint8_t r, uint8_t g, uint8_t b);
int frontend_display_save_back_buffer(void);
int frontend_display_restore_back_buffer(void);
IDirectDrawPalette *frontend_display_load_palette(IDirectDraw *p_dd,
						  const char *lp_name);
uint32_t
frontend_display_convert_color_ref_to_surface_pixel(IDirectDrawSurface *surface,
						    uint32_t color);
HRESULT frontend_display_set_surface_color_key(IDirectDrawSurface *surface,
					       uint32_t color);

#ifdef __cplusplus
}
#endif

#endif
