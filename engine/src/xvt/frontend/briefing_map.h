#ifndef XVT_FRONTEND_BRIEFING_MAP_H
#define XVT_FRONTEND_BRIEFING_MAP_H

#include <stdint.h>

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A briefing map point or zoom, one value per axis. */
struct briefing_map_s16_pair {
	int16_t x; /* Across: a map x, or the zoom across. */
	int16_t y; /* Down: a map y, or the zoom down. */
};

extern int16_t g_briefing_selected_mission_point14_flight_group_idx;
extern struct briefing_map_s16_pair g_briefing_map_center;
extern struct briefing_map_s16_pair g_briefing_map_target_center;
extern struct briefing_map_s16_pair g_briefing_map_scale;
extern struct briefing_map_s16_pair g_briefing_map_target_scale;
extern int16_t g_briefing_map_center_dirty;
extern int16_t g_briefing_map_scale_dirty;
extern int g_active_briefing_index;
extern struct RECT g_briefing_map_panel_rect;
extern int g_map_icon_by_craft_type[106];
extern struct RECT g_map_icon_rects[70];
extern int16_t g_briefing_map_fg_marker_active[8];
extern int16_t g_briefing_map_fg_marker_flight_group_idx[8];
extern int16_t g_briefing_map_fg_marker_age[8];
extern int16_t g_briefing_map_fg_markers_changed;
extern int16_t g_briefing_map_label_active[8];
extern int16_t g_briefing_map_label_text_idx[8];
extern int16_t g_briefing_map_label_x[8];
extern int16_t g_briefing_map_label_y[8];
extern int16_t g_briefing_map_label_age[8];
extern int16_t g_briefing_map_label_style[8];
extern int16_t g_briefing_map_labels_changed;

int16_t briefing_map_select_nearest_mission_point14_flight_group(
	struct RECT *viewport_rect, int16_t mouse_x, int16_t mouse_y);
void briefing_map_project_point_to_viewport(const struct RECT *viewport_rect,
					    int16_t map_x, int16_t map_y,
					    int16_t *out_x, int16_t *out_y);
int16_t briefing_map_step_s16_toward_target(int16_t current, int16_t target,
					    int16_t step);
void briefing_map_animate_view_state(void);
void briefing_map_update_script_playback_after_animation(void);
int16_t briefing_map_select_flight_group_at_cursor(
	struct RECT *viewport_rect, struct RECT *clip_rect, int left_down,
	int right_down, int16_t mouse_x, int16_t mouse_y);
int16_t briefing_map_draw_viewport_and_selection(struct RECT *viewport_rect,
						 struct RECT *clip_rect,
						 int16_t highlight_phase);
void briefing_map_draw_grid(const struct RECT *viewport_rect,
			    const struct RECT *clip_rect);
void briefing_map_draw_overlays(struct RECT *viewport_rect,
				struct RECT *clip_rect);
void briefing_map_draw_revealed_label_if_active(const char *text,
						int16_t color_ramp_group,
						int16_t x, int16_t y,
						int16_t reveal_count,
						int16_t shade_group);
void briefing_map_draw_revealed_label(const char *text,
				      int16_t color_ramp_group, int16_t x,
				      int16_t y, int16_t reveal_count,
				      int16_t shade_group);
void briefing_map_draw_craft_icon_highlight(struct RECT *viewport_rect,
					    struct RECT *clip_rect,
					    int flight_group_index,
					    int highlight_phase);

#ifdef __cplusplus
}
#endif

#endif
