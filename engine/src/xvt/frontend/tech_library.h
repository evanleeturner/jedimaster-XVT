#ifndef XVT_FRONTEND_TECH_LIBRARY_H
#define XVT_FRONTEND_TECH_LIBRARY_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One craft's entry in specdesc.txt, the text of the craft database. */
struct tech_library_spec_text {
	char craft_name[64];   /* Craft name, the entry's first line. */
	char manufacturer[64]; /* Shown after FRONTSTR_484_MANUFACTURER. */
	char in_use_by[64];    /* Shown after FRONTSTR_485_IN_USE_BY. */
	/* Description, drawn wrapped under the special characteristics
	 * heading. */
	char description[256];
	/* Crew, shown for every genus but starfighter, mine and satellite. */
	char crew[64];
};

extern struct tech_library_spec_text *g_tech_library_spec_text_table;
extern struct craft_tech_stats g_tech_library_craft_stats;
extern int g_tech_library_selected_ship_list_idx;
extern int g_tech_library_light_x;
extern float g_tech_library_preview_yaw_deg;
extern int g_tech_library_light_y;
extern int g_tech_library_light_z;
extern float g_tech_library_preview_pitch_deg;

int tech_library_update(int frame_counter);
int tech_library_update_model_controls(void);
int tech_library_draw_craft_spec_panel(void);
int tech_library_load_spec_text_table(void);

#ifdef __cplusplus
}
#endif

#endif
