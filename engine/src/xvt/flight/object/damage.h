#ifndef XVT_FLIGHT_OBJECT_DAMAGE_H
#define XVT_FLIGHT_OBJECT_DAMAGE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A craft's damage tally by source, which collide_damagecraft adds to and
 * the mission scoring reads, and its HUD features (craft_data.damage_stats).
 * mission_init_flight_group_object_slot resets it at spawn. */
struct craft_damage_stats {
	/* Set to 0 at spawn and by proving_grounds_init_course_objects; nothing
	 * reads it. */
	uint16_t last_system_hit_time;
	/* All damage dealt to the craft, after the scaling by the victim's
	 * genus and before shields take their share. */
	int damage_received_total;
	/* The part of damage_received_total taken while a player flew it. */
	int damage_received_by_player_owned_craft;
	/* Damage from colliding with a craft, whoever flew it, or with normal
	 * debris. */
	int damage_from_collision;
	/* Damage from objects other than craft (shots, chiefly) whose source is
	 * neither a player nor a starfighter and is not in a static slot, and
	 * from engine wash. */
	int damage_from_starship;
	/* Damage from objects other than craft whose source is in a static
	 * slot (mines) with no player behind it. */
	int damage_from_mine;
	/* Damage by the attacker's flight group: the craft that hit, or the
	 * firer of the shot. Spawn clears only entries 0 to 9 (TEAM_COUNT in
	 * mission_init_flight_group_object_slot). */
	int damage_from_flight_group_amount[48];
	/* Damage by the player behind the source: the player flying a craft
	 * that hit; for a shot, the player flying its firer, or the player its
	 * guidance record names. */
	int damage_from_player[8];
	/* Damage from AI starfighters' shots, by the attacker's flight group
	 * AI rating (group_ai). */
	int damage_from_ai_skill[6];
	/* HUD features the craft is fitted with: all at spawn, less the shield
	 * and beam features when it has none. */
	uint16_t installed_hud_feature_mask;
	/* HUD features working now: the fitted set at spawn.
	 * collide_damagecraft knocks out random features on a hull hit once
	 * hull_damage has reached system_damage_hull_threshold, and a boarding
	 * repair (paiman_boardmaneuver) restores them. */
	uint16_t active_hud_feature_mask;
};

/* Stored as int16_t in the binary (IDB enum damage_system_id). */
typedef int16_t damage_system_id;

enum {
	DAMAGE_SYSTEM_00_ENGINES = 0x0, ///< strings.txt line 14: Engines
	DAMAGE_SYSTEM_01_FLIGHT_CONTROLS =
		0x1, ///< strings.txt line 15: Flight Controls
	DAMAGE_SYSTEM_02_SHIELD_SYSTEM =
		0x2, ///< strings.txt line 16: Shield System
	DAMAGE_SYSTEM_03_CANNON_SYSTEM =
		0x3, ///< strings.txt line 17: Cannon System
	DAMAGE_SYSTEM_04_TARGETING_COMPUTER =
		0x4, ///< strings.txt line 18: Targeting Computer
	DAMAGE_SYSTEM_05_WARHEAD_LAUNCHER =
		0x5, ///< strings.txt line 19: Warhead Launcher
	DAMAGE_SYSTEM_06_BEAM_SYSTEM =
		0x6, ///< strings.txt line 20: Beam System
	DAMAGE_SYSTEM_07_COMMUNICATIONS =
		0x7, ///< strings.txt line 21: Communications
	DAMAGE_SYSTEM_08_COUNTERMEASURES =
		0x8, ///< strings.txt line 22: Countermeasures
	DAMAGE_SYSTEM_09_HYPERDRIVE = 0x9, ///< strings.txt line 23: Hyperdrive
	DAMAGE_SYSTEM_10_DAMAGE_ASSESSMENT =
		0xA, ///< strings.txt line 24: Damage Assessment
	DAMAGE_SYSTEM_ID_COUNT = 0xB,
};

extern const char *g_str_damage_system_names[DAMAGE_SYSTEM_ID_COUNT];
extern int16_t g_damage_mfd_damaged_system_count_cached;
extern int16_t g_damage_mfd_redraw_all_rows;
extern int16_t g_damage_mfd_last_selected_system_id;
extern int16_t g_damage_mfd_current_system_id;

void damage_queue_craft_billboards(uint16_t object_index);
uint16_t
damage_queue_craft_billboards_for_object_type(unsigned int object_index,
					      int object_type);
int16_t damage_display_mfd_page(void);
int16_t damage_find_adjacent_damaged_system(int16_t current_system_idx,
					    int16_t direction_step);
void damage_draw_mfd_system_status_row(damage_system_id system_id, int16_t y,
				       int16_t value_x);

#ifdef __cplusplus
}
#endif

#endif
