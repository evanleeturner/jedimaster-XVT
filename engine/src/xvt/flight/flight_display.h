#ifndef XVT_FLIGHT_FLIGHT_DISPLAY_H
#define XVT_FLIGHT_FLIGHT_DISPLAY_H

#include <stdint.h>

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern IDirectDrawSurface *g_flight_primary_surface;
extern IDirectDrawSurface *g_flight_render_surface;
extern IDirectDrawSurface *g_flight_back_buffer;
extern int g_flight_primary_pitch[2];
extern int g_flight_conf_flicker;
extern int g_flight_fullscreen;
extern int g_render_target_width;
extern int g_requested_flight_bytes_per_pixel;
extern int g_requested_flight_hardware3d;
extern IDirectDrawPalette *g_flight_palette;
extern uint8_t g_flight_hud_staging_buffer[640 * 480 * 2];
extern uint8_t g_flight_software_framebuffer[640 * 480 * 2];
extern char g_hud_cockpit_resolution_directory[7];

void flight_display_configure_resolution_state(void);
int flight_display_apply_resolution_mode_stub(int resolution_mode);
int flight_display_post_primary_surface_create_or_restore_stub(void);
int flight_display_init(void);
uint8_t flight_display_set_palette_entries(const uint8_t *rgb_data,
					   int first_entry, int entry_count);
int flight_display_cleanup_and_report_error(int error_code);
int flight_display_get_primary_surface_pitch(void);
HRESULT flight_display_flip(void);
void nullsub_11(void);
int flight_display_blit_render_surface(void);
void flight_display_apply_resolution_mode_backend_stub(int resolution_mode);
void flight_display_clear_surface(IDirectDrawSurface *surface);
int flight_display_restore_primary_surface(void);
int display_is_pixel_format555(void);
int flight_display_apply_resolution_mode_internal_stub(int resolution_mode,
						       int flags);
uint8_t flight_display_write_vga_palette_entries(const uint8_t *rgb_entries,
						 int16_t first_entry,
						 int16_t entry_count);

#ifdef __cplusplus
}
#endif

#endif
