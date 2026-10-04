#ifndef XVT_FRONTEND_FRONTEND_BUTTON_H
#define XVT_FRONTEND_FRONTEND_BUTTON_H

#include <stdint.h>

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum frontend_navigation_slot_state {
	FRONTEND_NAVIGATION_SLOT_INACTIVE = 0,
	FRONTEND_NAVIGATION_SLOT_ACTIVE = 1,
	FRONTEND_NAVIGATION_SLOT_SELECTED = 2,
} frontend_navigation_slot_state;

extern const char *g_button_overlay_text;

int frontend_button_handle_text_button(const struct RECT *rect,
				       const char *text, int font_size,
				       int unused_color, int held_state_slot,
				       const char *click_sound_name);
int frontend_button_handle_sprite_button(
	struct RECT *rect, const char *normal_sprite,
	const char *pressed_sprite, const char *tooltip_text, int font_size,
	int unused_color, int held_state_slot, const char *press_sound_name);
int frontend_button_draw_text_button_state(const struct RECT *rect,
					   const char *text, int font_size,
					   int unused_color, char is_pressed);
void frontend_button_draw_sprite_and_tooltip(struct RECT *rect,
					     const char *sprite_name,
					     const char *tooltip_text,
					     int font_size, int unused_color);
void frontend_button_draw_eight_slot_navigation_state(
	const frontend_navigation_slot_state *slot_states);
int frontend_button_draw_overlay_text(struct RECT *rect, const char *str);
void frontend_button_enable_overlay_text(void);
void frontend_button_disable_overlay_text(void);
const char *frontend_button_set_overlay_text(const char *text);
void frontend_button_use_pressed_overlay_style(void);
int frontend_button_is_overlay_text_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
