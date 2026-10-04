#ifndef XVT_FRONTEND_CONCOURSE_H
#define XVT_FRONTEND_CONCOURSE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char (*g_pilot_list_display_names)[14];
extern struct frontend_file_list *g_pilot_file_list;
extern struct mission_list_entry *g_pilot_record_tournament_mission_list;
extern int g_pilot_record_tournament_mission_count;
extern struct mission_list_entry *g_pilot_record_melee_mission_list;
extern int g_pilot_record_melee_mission_count;
extern struct mission_list_entry
	*g_pilot_record_singleplayer_combat_mission_list;
extern int g_pilot_record_singleplayer_combat_mission_count;
extern struct mission_list_entry
	*g_pilot_record_multiplayer_combat_mission_list;
extern int g_pilot_record_multiplayer_combat_mission_count;
extern struct mission_list_entry
	*g_pilot_record_singleplayer_campaign_mission_list;
extern int g_pilot_record_singleplayer_campaign_mission_count;
extern struct mission_list_entry
	*g_pilot_record_multiplayer_campaign_mission_list;
extern int g_pilot_record_multiplayer_campaign_mission_count;
extern struct mission_list_entry
	*g_pilot_record_singleplayer_training_mission_list;
extern int g_pilot_record_singleplayer_training_mission_count;
extern struct mission_list_entry
	*g_pilot_record_multiplayer_training_mission_list;
extern int g_pilot_record_multiplayer_training_mission_count;

int concourse_exit(int frame_counter);
int concourse_update(int frame_counter);

#ifdef __cplusplus
}
#endif

#endif
