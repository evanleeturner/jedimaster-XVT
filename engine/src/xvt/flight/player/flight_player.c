#include "xvt/flight/player/flight_player.h"

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"

/* Returns 1 when the local player's craft has an installed subsystem (its flag
 * set in system_flags) whose system_health is 0, else 0. Also returns 0 when the
 * local player has no craft or the object has no craft record. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x46C140
int16_t flight_player_has_disabled_subsystem(void)
{
	int object_index = g_players[g_local_player].object_index;
	if (object_index == -1) {
		return 0;
	}
	struct craft_data *craft = g_object_table[object_index].mobj->p_craft;
	if (craft == NULL) {
		return 0;
	}
	uint16_t system_id_by_display_slot[CRAFT_SUBSYSTEM_COUNT];
	for (int16_t system_id = 0; system_id < CRAFT_SUBSYSTEM_COUNT;
	     ++system_id) {
		system_id_by_display_slot
			[craft->system_display_slot_by_system[system_id]] =
				system_id;
	}
	int16_t all_installed_systems_operational = 1;
	for (int16_t display_slot = 0; display_slot < CRAFT_SUBSYSTEM_COUNT;
	     ++display_slot) {
		if (craft->system_health
				    [system_id_by_display_slot[display_slot]] ==
			    0 &&
		    (g_subsystem_id_to_flag
			     [system_id_by_display_slot[display_slot]] &
		     craft->system_flags) != 0) {
			all_installed_systems_operational = 0;
		}
	}
	return all_installed_systems_operational == 0;
}

/* Does nothing with its message. Nothing calls this. */
// FUNCTION: XVT 0x46C200
void nullsub_8(const char *message) { (void)message; }

/* Adds step to the throttle_speed of the player's craft, holding at 0xFFFF when
 * the sum would wrap past it. Does not check that the player has a craft. */
// FUNCTION: XVT 0x481D90
void flight_player_increase_throttle_speed(int16_t step, int player_idx)
{
	struct player_data *player = &g_players[player_idx];
	uint16_t *throttle_speed_ptr = &g_object_table[player->object_index]
						.mobj->p_craft->throttle_speed;
	uint16_t throttle_speed = *throttle_speed_ptr;
	*throttle_speed_ptr = (uint16_t)(throttle_speed + step);
	if (g_object_table[player->object_index].mobj->p_craft->throttle_speed <
	    throttle_speed) {
		g_object_table[player->object_index]
			.mobj->p_craft->throttle_speed = UINT16_MAX;
	}
}

/* Takes step from the throttle_speed of the player's craft, holding at 0 when
 * the result would wrap below it. Does not check that the player has a
 * craft. */
// FUNCTION: XVT 0x481E10
void flight_player_decrease_throttle_speed(int16_t step, int player_idx)
{
	struct player_data *player = &g_players[player_idx];
	uint16_t *throttle_speed_ptr = &g_object_table[player->object_index]
						.mobj->p_craft->throttle_speed;
	uint16_t throttle_speed = *throttle_speed_ptr;
	*throttle_speed_ptr = (uint16_t)(throttle_speed - step);
	if (g_object_table[player->object_index].mobj->p_craft->throttle_speed >
	    throttle_speed) {
		g_object_table[player->object_index]
			.mobj->p_craft->throttle_speed = 0;
	}
}
