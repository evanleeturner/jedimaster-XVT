#include "xvt/flight/craft.h"

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

/* Craft records in the pool at g_craft_data_pool_base, one for each craft object
 * slot. mission_init sets it to 32 (CRAFT_SLOT_COUNT) when a flight loads; a
 * world-state restore copies in the saved value: flight_restore_world_state in
 * the original build, xvt_snapshot_decode in the modern one. */
// GLOBAL: XVT 0x9ECA28
int g_craft_data_pool_capacity = 0;
/* The craft record the code running now works on. Many functions point it at an
 * object's craft before reading it or calling code that does, chiefly the AI,
 * laser, collision and flight loop code; a few save and restore it, the rest
 * leave it set. Nothing sets it back to NULL. */
// GLOBAL: XVT 0xA08104
struct craft_data *g_cur_craft;
/* Locked memory of g_craft_data_pool_handle: g_craft_data_pool_capacity craft
 * records, into which each craft object's mobj->p_craft points. Two functions
 * write it: fe_disk_io_lock_global_buffers, which locks the handle, and, in the
 * modern build, xvt_flight_loading_reset, which sets it to NULL. */
// GLOBAL: XVT 0xA07BD0
struct craft_data *g_craft_data_pool_base = 0;
/* Scale, 4/9, from a model's max_speed and accel_rate to the Tech Library's speed
 * and acceleration ratings. Only build_craft_tech_stats reads it. */
// GLOBAL: XVT 0x5180E0
static const double g_craft_tech_speed_acceleration_rating_scale =
	0.4444444444444444;
/* Added before build_craft_tech_stats truncates a rating, so the rating rounds to
 * nearest. */
// GLOBAL: XVT 0x5180E8
static const double g_craft_tech_rating_rounding_bias = 0.5;
/* Scale from a model's pitch_rate plus roll_rate to the Tech Library's maneuver
 * rating. Only build_craft_tech_stats reads it. */
// GLOBAL: XVT 0x5180F0
static const double g_craft_tech_maneuver_rating_scale = 0.005231575698284567;

/* Adds delta to shield bank shield_index of g_cur_craft and clamps the bank to 0
 * through twice the model's shield_strength. The first argument is ignored;
 * shield_index is not checked. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x405790
void craft_adjust_current_shield_energy(unsigned int unused_object_idx,
					uint16_t shield_index, int16_t delta)
{
	(void)unused_object_idx;
	int max_shield_energy =
		2 * g_model_defs[g_cur_craft->model_index].shield_strength;
	g_cur_craft->shield_energy[shield_index] =
		g_cur_craft->shield_energy[shield_index] + delta;
	if (g_cur_craft->shield_energy[shield_index] < 0) {
		g_cur_craft->shield_energy[shield_index] = 0;
	}
	if (g_cur_craft->shield_energy[shield_index] > max_shield_energy) {
		g_cur_craft->shield_energy[shield_index] = max_shield_energy;
	}
}

/* Returns twice the shield_strength of the object's craft model, the cap
 * craft_adjust_current_shield_energy puts on one shield bank. Does not check that
 * the object has a craft record. */
// FUNCTION: XVT 0x405820
int craft_get_object_max_shield(uint16_t obj_idx)
{
	return g_model_defs[g_object_table[obj_idx].mobj->p_craft->model_index]
		       .shield_strength *
	       2;
}

/* Returns g_object_type_table's model index for an object type. The type is not
 * checked. */
// FUNCTION: XVT 0x4269E0
model_index get_model_index_from_type(object_type_id object_type)
{
	return g_object_type_table[object_type].model_index;
}

/* Fills the Tech Library's ratings for stats->craft_type from its model. Returns
 * 0, with only genus_id filled, when the type has no model (MODEL_INDEX_NONE);
 * else 1. Speed and acceleration are max_speed and accel_rate times 4/9, maneuver
 * is pitch_rate plus roll_rate times g_craft_tech_maneuver_rating_scale, each rounded
 * to nearest; shield is shield_strength / 50 (0 without shields) and hull is
 * hull_strength / 105, both times 16 for starships and platforms and 4 for
 * freighters. Lasers and ions count the slots of laser groups firing object
 * types 137 or 139, and 141; warheads sum capacity times slots of each
 * launcher. The TIE Advanced, T-Wing, Z-95 and R-41 then get fixed weapon
 * figures. Leaves craft_type and unused_rating as they were. */
// FUNCTION: XVT 0x426A00
int build_craft_tech_stats(struct craft_tech_stats *stats)
{
	stats->genus_id = g_object_type_table[stats->craft_type].genus_id;
	model_index model_index =
		get_model_index_from_type((unsigned int)stats->craft_type);
	if (model_index == MODEL_INDEX_NONE) {
		return 0;
	}

	stats->speed_rating =
		(int)((double)g_model_defs[model_index].max_speed *
			      g_craft_tech_speed_acceleration_rating_scale +
		      g_craft_tech_rating_rounding_bias);
	stats->acceleration_rating =
		(int)((double)g_model_defs[model_index].accel_rate *
			      g_craft_tech_speed_acceleration_rating_scale +
		      g_craft_tech_rating_rounding_bias);
	stats->maneuver_rating =
		(int)((double)((uint16_t)g_model_defs[model_index].pitch_rate +
			       (uint16_t)g_model_defs[model_index].roll_rate) *
			      g_craft_tech_maneuver_rating_scale +
		      g_craft_tech_rating_rounding_bias);

	int *shield_rating = &stats->shield_rating;
	if (g_model_defs[model_index].has_shields != 0) {
		*shield_rating = g_model_defs[model_index].shield_strength / 50;
	} else {
		*shield_rating = 0;
	}
	unsigned int hull_rating =
		(unsigned int)g_model_defs[model_index].hull_strength / 105;
	craft_genus genus_id = stats->genus_id;
	stats->hull_rating = (int)hull_rating;
	if (genus_id == CRAFT_GENUS_STARSHIP ||
	    genus_id == CRAFT_GENUS_PLATFORM) {
		int scaled_shield_rating = 16 * *shield_rating;
		stats->hull_rating = (int)(16 * hull_rating);
		*shield_rating = scaled_shield_rating;
	}
	if (genus_id == CRAFT_GENUS_FREIGHTER) {
		int scaled_hull_rating = 4 * stats->hull_rating;
		*shield_rating = 4 * *shield_rating;
		stats->hull_rating = scaled_hull_rating;
	}

	stats->laser_count = 0;
	stats->ion_count = 0;
	int group_index;
	for (group_index = 0; group_index < 2; ++group_index) {
		uint8_t weapon_type =
			g_model_defs[model_index]
				.laser_group_weapon_type[group_index];
		if (weapon_type == 0x89 || weapon_type == 0x8B) {
			stats->laser_count +=
				g_model_defs[model_index]
					.laser_group_slot_count[group_index];
		}
		if (g_model_defs[model_index]
			    .laser_group_weapon_type[group_index] == 0x8D) {
			stats->ion_count +=
				g_model_defs[model_index]
					.laser_group_slot_count[group_index];
		}
	}

	group_index = 0;
	stats->warhead_rating = 0;
	do {
		if (g_model_defs[model_index]
			    .warhead_launcher_type[group_index] != 0) {
			stats->warhead_rating +=
				g_model_defs[model_index]
					.warhead_launcher_capacity
						[group_index] *
				g_model_defs[model_index]
					.warhead_launcher_slot_count
						[group_index];
		}
		++group_index;
	} while (group_index < 2);

	switch (stats->craft_type) {
	case CRAFT_SPECIES_TIE_ADVANCED:
		stats->laser_count = 4;
		stats->ion_count = 0;
		stats->warhead_rating = 8;
		break;
	case CRAFT_SPECIES_T_WING:
		stats->laser_count = 2;
		stats->ion_count = 0;
		stats->warhead_rating = 8;
		break;
	case CRAFT_SPECIES_Z_95_HEADHUNTER:
		stats->laser_count = 2;
		stats->ion_count = 0;
		break;
	case CRAFT_SPECIES_R_41_STARCHASER:
		stats->laser_count = 2;
		stats->ion_count = 2;
		break;
	default:
		break;
	}
	return 1;
}

/* Sets all 16 of a craft's turret_object_links to NULL without freeing the
 * objects they point at. */
// FUNCTION: XVT 0x458750
void craft_clear_turret_object_links(struct craft_data *craft)
{
	for (uint16_t turret_index = 0; turret_index < 16; ++turret_index) {
		craft->turret_object_links[turret_index] = NULL;
	}
}

/* Frees the objects a craft links to: sets objectType to 0, the free slot mark,
 * on the object effective_ai_object_link points at and on each object in
 * turret_object_links, and sets those links to NULL. */
// FUNCTION: XVT 0x458780
void craft_free_linked_objects(struct craft_data *craft)
{
	if (craft->effective_ai_object_link != NULL) {
		craft->effective_ai_object_link->object_type = 0;
		craft->effective_ai_object_link = NULL;
	}

	struct object_record **object_link = craft->turret_object_links;
	int remaining = 16;
	do {
		if (*object_link != NULL) {
			(*object_link)->object_type = 0;
			*object_link = NULL;
		}
		++object_link;
		--remaining;
	} while (remaining != 0);
}

/* Knocks a damageable mesh off a craft that has more than one mesh: the first
 * one, or every one when detach_all is set, whose component_state is 0. Each
 * becomes a small debris object (object_spawn_detached_component) given a random
 * spin, a yaw and pitch nudge, type_specific_byte[1] 2 and a lifetime_timer of 1
 * or 2 times SIMULATION_TICKS_PER_SECOND. Sets that mesh's component_state to 4
 * and the entry after the last mesh, which holds the fuselage damage animation
 * step, to 2. A mesh with no free debris slot is left in place. Does not check
 * that the entry after the last mesh lies inside component_state. */
// FUNCTION: XVT 0x458FA0
void craft_detach_damageable_component(uint16_t object_index,
				       int16_t detach_all)
{
	int object_type = g_object_table[object_index].object_type;
	uint16_t mesh_count;
	if (object_type < 73) {
		mesh_count = (uint16_t)g_object_type_mesh_cache[object_type]
				     .mesh_count;
	} else {
		mesh_count = (uint16_t)model_mesh_get_object_type_mesh_count(
			g_object_table[object_index].object_type);
	}
	if (mesh_count <= 1) {
		return;
	}

	unsigned int mesh_index = 0;
	struct craft_data *craft = g_object_table[object_index].mobj->p_craft;
	if (mesh_count == 0) {
		return;
	}

	do {
		if (craft->component_state[mesh_index] == 0 &&
		    model_mesh_is_object_type_mesh_damageable(
			    g_object_table[object_index].object_type,
			    mesh_index) != 0) {
			uint16_t fragment_object_index =
				object_spawn_detached_component(
					object_index, (int16_t)mesh_index);
			if (fragment_object_index != UINT16_MAX) {
				int16_t roll_impulse =
					(game_rand() & 0x3FFF) + 0x4000;
				int16_t yaw_offset =
					(game_rand() & 0x7FF) + 0x400;
				int16_t pitch_offset =
					(game_rand() & 0xFFF) + 0x400;
				if ((game_rand() & 1) != 0) {
					roll_impulse = -roll_impulse;
					yaw_offset = -yaw_offset;
				}
				if ((game_rand() & 1) != 0) {
					pitch_offset = -pitch_offset;
				}

				g_object_table[fragment_object_index]
					.mobj->roll_impulse_rate = roll_impulse;
				g_object_table[fragment_object_index].yaw +=
					yaw_offset;
				g_object_table[fragment_object_index].pitch +=
					pitch_offset;
				int16_t pitch =
					g_object_table[fragment_object_index]
						.pitch;
				if ((uint16_t)pitch >= 0x8000) {
					g_object_table[fragment_object_index]
						.pitch = -pitch;
					g_object_table[fragment_object_index]
						.yaw += (uint16_t)0x8000;
				}
				g_object_table[fragment_object_index]
					.mobj->orient_matrix_dirty = 1;
				g_object_table[fragment_object_index]
					.mobj->move_vector_dirty =
					g_object_table[fragment_object_index]
						.mobj->orient_matrix_dirty;
				g_object_table[fragment_object_index]
					.mobj->lifetime_timer =
					SIMULATION_TICKS_PER_SECOND *
					((game_rand() & 1) + 1);
				g_object_table[fragment_object_index]
					.type_specific_byte[1] = 2;
				craft->component_state[mesh_index] = 4;
				craft->component_state[mesh_count] = 2;
				if (detach_all == 0) {
					break;
				}
			}
		}
		++mesh_index;
	} while (mesh_count > mesh_index);
}

/* Returns the warhead_kind_index for a warhead object type. For any other type
 * the modern build returns -1; the original build returns an uninitialized
 * value. */
// FUNCTION: XVT 0x484D80
warhead_kind_index object_type_get_warhead_kind_index(uint16_t object_type)
{
	warhead_kind_index result;

#ifdef XVT_MODERN
	result = -1;
#endif
	switch (object_type) {
	case WARHEAD_OBJECT_TYPE_PROTON_TORPEDO:
		result = WARHEAD_KIND_PROTON_TORPEDO;
		break;
	case WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE:
		result = WARHEAD_KIND_CONCUSSION_MISSILE;
		break;
	case WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO:
		result = WARHEAD_KIND_ADVANCED_PROTON_TORPEDO;
		break;
	case WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE:
		result = WARHEAD_KIND_ADVANCED_CONCUSSION_MISSILE;
		break;
	case WARHEAD_OBJECT_TYPE_SPACE_BOMB:
		result = WARHEAD_KIND_SPACE_BOMB;
		break;
	case WARHEAD_OBJECT_TYPE_HEAVY_ROCKET:
		result = WARHEAD_KIND_HEAVY_ROCKET;
		break;
	case WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE:
		result = WARHEAD_KIND_MAGNETIC_PULSE;
		break;
	default:
		break;
	}
	return result;
}

/* Tells whether the target component selector may offer a mesh: returns 0 for
 * misc hull and antenna meshes; 1 for a mesh with target id 0, or with target
 * id 1 that is neither main hull nor fuselage; otherwise 1 only when the mesh
 * is the first with its target id and mesh type, so a group of alike meshes is
 * offered once. For an object type below 73, a mesh index past the type's
 * cached count reads the last mesh's type. */
// FUNCTION: XVT 0x484E10
int craft_is_selectable_damage_component_mesh(int object_type, int mesh_index)
{
	int adjusted_mesh_index = mesh_index;
	mesh_component_type mesh_type;
	if (object_type < 73) {
		if (mesh_index < 0) {
			mesh_type = MESH_COMPONENT_00_DEFAULT;
		} else {
			int mesh_count = g_object_type_mesh_cache[object_type]
						 .mesh_count;
			if (mesh_count <= mesh_index) {
				adjusted_mesh_index = mesh_count - 1;
			}
			mesh_type = g_object_type_mesh_cache[object_type]
					    .mesh_types[adjusted_mesh_index];
		}
	} else {
		mesh_type = model_mesh_get_object_type_mesh_type(object_type,
								 mesh_index);
	}
	if (mesh_type == MESH_COMPONENT_18_MISC_HULL ||
	    mesh_type == MESH_COMPONENT_19_ANTENNA) {
		return 0;
	}
	int target_id = model_mesh_get_target_id(object_type, mesh_index);
	if (target_id == 0) {
		return 1;
	}
	if (target_id == 1 && mesh_type != MESH_COMPONENT_01_MAIN_HULL &&
	    mesh_type != MESH_COMPONENT_03_FUSELAGE) {
		return 1;
	}
	int object_type_mesh_count;
	if (object_type < 73) {
		object_type_mesh_count =
			g_object_type_mesh_cache[object_type].mesh_count;
	} else {
		object_type_mesh_count =
			model_mesh_get_object_type_mesh_count(object_type);
	}
	int candidate_mesh_index = 0;
	mesh_component_type candidate_mesh_type;
	while (object_type_mesh_count > candidate_mesh_index) {
		if (model_mesh_get_target_id(
			    object_type, candidate_mesh_index) == target_id) {
			int adjusted_candidate_index = candidate_mesh_index;
			if (object_type < 73) {
				if (candidate_mesh_index < 0) {
					candidate_mesh_type =
						MESH_COMPONENT_00_DEFAULT;
				} else {
					int candidate_mesh_count =
						g_object_type_mesh_cache
							[object_type]
								.mesh_count;
					if (candidate_mesh_count <=
					    candidate_mesh_index) {
						adjusted_candidate_index =
							candidate_mesh_count -
							1;
					}
					candidate_mesh_type =
						g_object_type_mesh_cache[object_type]
							.mesh_types
								[adjusted_candidate_index];
				}
			} else {
				candidate_mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type,
						candidate_mesh_index);
			}
			if (candidate_mesh_type == mesh_type) {
				return candidate_mesh_index == mesh_index;
			}
		}
		++candidate_mesh_index;
	}

	return 0;
}

/* Applies damage to one component, mesh hit_mesh_index - 1, of the craft
 * g_cur_craft points at, which must be victim_obj_idx's craft (not checked), and
 * returns the damage left for the hull. Returns the damage unchanged when the
 * component is already at 0 or is undamageable (component_hp 255), except a
 * bridge on object type 54. A damage of 0 counts as 1. In mission version 14
 * that bridge is guarded: while a shield generator has component_hp or either
 * shield bank holds energy, the damage passes unchanged; after that it also
 * passes unchanged unless the source is object type 40 or a breaking-up object
 * type 3, whose damage is raised so that what is returned is hull_max -
 * hull_damage, less 5 * (hull_max / 100) for type 40. Damage of at least 16
 * times component_hp destroys the component: component_hp 0, and the rest is
 * returned. In mission version 14 at difficulty 0, losing the last shield
 * generator empties both shield banks. When the mesh is damageable, destroying
 * it also sets component_state 2; moves every player aiming at that component
 * to the next intact selectable one; in the proving grounds counts a target
 * destroyed and adds 50 to the score (100 when mesh_rotation is nonzero) and 2
 * seconds to g_mission_countdown_clock; spawns an explosion object at the
 * component, when a slot is free, with a sound; and, when the local player's
 * craft is the source, raises fsfx_speak_wingman_event event 23 for a gun,
 * turret, shield generator, warhead launcher, communications or beam
 * component. Smaller damage sets component_hp to (16 * component_hp - damage) /
 * 16, at least 1, and returns 0. */
// FUNCTION: XVT 0x4A6990
int craft_damage_component(uint16_t victim_obj_idx, int16_t hit_mesh_index,
			   unsigned int damage_amount, uint16_t source_obj_idx)
{
	enum {
		OBJECT_TYPE_MESH_CACHE_COUNT = 73,
		SUPER_STAR_DESTROYER_OBJECT_TYPE = 54,
		MAX_PLAYERS = 8
	};

	--hit_mesh_index;
	int mesh_index = (uint16_t)hit_mesh_index;

	if (g_cur_craft->component_hp[mesh_index] == 0) {
		return damage_amount;
	}
	int mesh_count;
	int mesh_type;
	if (g_cur_craft->component_hp[mesh_index] == UINT8_MAX) {
		if (g_object_table[victim_obj_idx].object_type !=
		    SUPER_STAR_DESTROYER_OBJECT_TYPE) {
			return damage_amount;
		}
		int adjusted_index = mesh_index;
		int object_type = g_object_table[victim_obj_idx].object_type;
		if (object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
			if (adjusted_index < 0) {
				mesh_type = MESH_COMPONENT_00_DEFAULT;
			} else {
				mesh_count =
					g_object_type_mesh_cache[object_type]
						.mesh_count;
				if (mesh_count <= mesh_index) {
					adjusted_index = mesh_count - 1;
				}
				mesh_type =
					g_object_type_mesh_cache[object_type]
						.mesh_types[adjusted_index];
			}
		} else {
			mesh_type = model_mesh_get_object_type_mesh_type(
				object_type, mesh_index);
		}
		if (mesh_type != MESH_COMPONENT_07_BRIDGE) {
			return damage_amount;
		}
	}
	if (damage_amount == 0) {
		damage_amount = 1;
	}

	if (g_mission_file_version == 14 &&
	    g_object_table[victim_obj_idx].object_type ==
		    SUPER_STAR_DESTROYER_OBJECT_TYPE) {
		int adjusted_index = mesh_index;
		int object_type = g_object_table[victim_obj_idx].object_type;
		if (object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
			if (adjusted_index < 0) {
				mesh_type = MESH_COMPONENT_00_DEFAULT;
			} else {
				mesh_count =
					g_object_type_mesh_cache[object_type]
						.mesh_count;
				if (mesh_count <= mesh_index) {
					adjusted_index = mesh_count - 1;
				}
				mesh_type =
					g_object_type_mesh_cache[object_type]
						.mesh_types[adjusted_index];
			}
		} else {
			mesh_type = model_mesh_get_object_type_mesh_type(
				object_type, mesh_index);
		}
		if (mesh_type == MESH_COMPONENT_07_BRIDGE) {
			object_type =
				g_object_table[victim_obj_idx].object_type;
			if (object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
				mesh_count =
					g_object_type_mesh_cache[object_type]
						.mesh_count;
			} else {
				mesh_count =
					model_mesh_get_object_type_mesh_count(
						object_type);
			}
			int shield_generator_count = 0;
			for (int i = 0; mesh_count > i; ++i) {
				int candidate_index = i;
				object_type = g_object_table[victim_obj_idx]
						      .object_type;
				if (object_type <
				    OBJECT_TYPE_MESH_CACHE_COUNT) {
					if (candidate_index < 0) {
						mesh_type =
							MESH_COMPONENT_00_DEFAULT;
					} else {
						int candidate_mesh_count =
							g_object_type_mesh_cache
								[object_type]
									.mesh_count;
						if (candidate_mesh_count <=
						    candidate_index) {
							candidate_index =
								candidate_mesh_count -
								1;
						}
						mesh_type =
							g_object_type_mesh_cache[object_type]
								.mesh_types
									[candidate_index];
					}
				} else {
					mesh_type =
						model_mesh_get_object_type_mesh_type(
							object_type, i);
				}
				if (mesh_type == MESH_COMPONENT_08_SHLD_GEN &&
				    g_cur_craft->component_hp[i] != 0) {
					++shield_generator_count;
				}
			}
			if (shield_generator_count != 0 ||
			    g_cur_craft->shield_energy[0] != 0 ||
			    g_cur_craft->shield_energy[1] != 0) {
				return damage_amount;
			}
			if (g_object_table[source_obj_idx].object_type == 40) {
				unsigned int hull_max = g_cur_craft->hull_max;
				unsigned int residual =
					16 * g_cur_craft->component_hp
							[mesh_index] -
					5 * (hull_max / 100) -
					g_cur_craft->hull_damage;
				damage_amount = hull_max + residual;
			} else {
				if (g_object_table[source_obj_idx]
						    .object_type != 3 ||
				    g_object_table[source_obj_idx]
						    .mobj->p_craft
						    ->object_kind !=
					    CRAFT_OBJECT_KIND_BREAKING_UP) {
					return damage_amount;
				}
				damage_amount = g_cur_craft->hull_max +
						16 * g_cur_craft->component_hp
								[mesh_index] -
						g_cur_craft->hull_damage;
			}
		}
	}

	unsigned int component_hp_as_damage =
		16 * g_cur_craft->component_hp[mesh_index];
	if (damage_amount >= component_hp_as_damage) {
		g_cur_craft->component_hp[mesh_index] = 0;
		damage_amount -= component_hp_as_damage;

		if (g_mission_file_version == 14 &&
		    g_flight_mission_state.difficulty == 0) {
			int adjusted_index = mesh_index;
			int object_type =
				g_object_table[victim_obj_idx].object_type;
			if (object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
				if (adjusted_index < 0) {
					mesh_type = MESH_COMPONENT_00_DEFAULT;
				} else {
					if (g_object_type_mesh_cache
						    [object_type]
							    .mesh_count <=
					    mesh_index) {
						adjusted_index =
							g_object_type_mesh_cache
								[object_type]
									.mesh_count -
							1;
					}
					mesh_type =
						g_object_type_mesh_cache[object_type]
							.mesh_types
								[adjusted_index];
				}
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type, mesh_index);
			}
			if (mesh_type == MESH_COMPONENT_08_SHLD_GEN) {
				object_type = g_object_table[victim_obj_idx]
						      .object_type;
				if (object_type <
				    OBJECT_TYPE_MESH_CACHE_COUNT) {
					mesh_count =
						g_object_type_mesh_cache
							[object_type]
								.mesh_count;
				} else {
					mesh_count =
						model_mesh_get_object_type_mesh_count(
							object_type);
				}
				int active_generators = 0;
				for (int i = 0; mesh_count > i; ++i) {
					if (mesh_index == i) {
						continue;
					}
					int candidate_index = i;
					object_type =
						g_object_table[victim_obj_idx]
							.object_type;
					if (object_type <
					    OBJECT_TYPE_MESH_CACHE_COUNT) {
						if (candidate_index < 0) {
							mesh_type =
								MESH_COMPONENT_00_DEFAULT;
						} else {
							int candidate_mesh_count =
								g_object_type_mesh_cache
									[object_type]
										.mesh_count;
							if (candidate_mesh_count <=
							    candidate_index) {
								candidate_index =
									candidate_mesh_count -
									1;
							}
							mesh_type =
								g_object_type_mesh_cache[object_type]
									.mesh_types
										[candidate_index];
						}
					} else {
						mesh_type =
							model_mesh_get_object_type_mesh_type(
								object_type, i);
					}
					if (mesh_type ==
						    MESH_COMPONENT_08_SHLD_GEN &&
					    g_cur_craft->component_hp[i] != 0) {
						++active_generators;
					}
				}
				if (active_generators == 0) {
					g_cur_craft->shield_energy[1] = 0;
					g_cur_craft->shield_energy[0] =
						g_cur_craft->shield_energy[1];
				}
			}
		}

		unsigned int victim_idx = victim_obj_idx;
		if (model_mesh_is_object_type_mesh_damageable(
			    g_object_table[victim_idx].object_type,
			    mesh_index) != 0) {
			g_cur_craft->component_state[mesh_index] = 2;
			for (int player_index = 0; player_index < MAX_PLAYERS;
			     ++player_index) {
				if (g_players[player_index]
						    .participation_state != 0 &&
				    victim_obj_idx ==
					    (uint16_t)g_players[player_index]
						    .current_target_object_idx &&
				    g_players[player_index]
						    .selected_target_component ==
					    hit_mesh_index) {
					int object_type =
						g_object_table[victim_idx]
							.object_type;
					int victim_mesh_count;
					if (object_type <
					    OBJECT_TYPE_MESH_CACHE_COUNT) {
						victim_mesh_count =
							g_object_type_mesh_cache
								[object_type]
									.mesh_count;
					} else {
						victim_mesh_count =
							model_mesh_get_object_type_mesh_count(
								object_type);
					}
					do {
						uint16_t next_component =
							(uint16_t)(g_players[player_index]
									   .selected_target_component +
								   1);
						g_players[player_index]
							.selected_target_component =
							next_component;
						if (next_component >=
						    victim_mesh_count) {
							g_players[player_index]
								.selected_target_component =
								0;
						}
						if (g_cur_craft->component_state
								    [(uint16_t)g_players
									     [player_index]
										     .selected_target_component] ==
							    0 &&
						    craft_is_selectable_damage_component_mesh(
							    g_object_table[victim_idx]
								    .object_type,
							    (uint16_t)g_players[player_index]
								    .selected_target_component) !=
							    0) {
							break;
						}
					} while (
						g_players[player_index]
							.selected_target_component !=
						hit_mesh_index);
				}
			}

			if (g_flight_mission_state
				    .proving_grounds_mode_active != 0) {
				++g_flight_mission_state
					  .proving_grounds_targets_destroyed;
				unsigned int score =
					g_flight_mission_state
						.proving_grounds_score +
					50;
				g_flight_mission_state.proving_grounds_score =
					score;
				if (g_cur_craft->mesh_rotation[mesh_index] !=
				    0) {
					g_flight_mission_state
						.proving_grounds_score =
						score + 50;
				}
				uint8_t seconds =
					g_mission_countdown_clock.seconds + 2;
				g_mission_countdown_clock.seconds = seconds;
				if (seconds >= 60) {
					g_mission_countdown_clock.seconds =
						seconds - 60;
					++g_mission_countdown_clock.minutes;
				}
			}

			{
				uint16_t explosion_obj_idx =
					object_alloc_slot_for_genus(
						CRAFT_GENUS_EXPLOSION);
				if (explosion_obj_idx != UINT16_MAX) {
					g_object_table[explosion_obj_idx]
						.world_x =
						g_object_table[victim_idx]
							.world_x;
					g_object_table[explosion_obj_idx]
						.world_y =
						g_object_table[victim_idx]
							.world_y;
					g_object_table[explosion_obj_idx]
						.world_z =
						g_object_table[victim_idx]
							.world_z;
					int center_x = model_mesh_get_center_x(
						g_object_table[victim_idx]
							.object_type,
						mesh_index);
					int center_y = model_mesh_get_center_y(
						g_object_table[victim_idx]
							.object_type,
						mesh_index);
					int center_z = model_mesh_get_center_z(
						g_object_table[victim_idx]
							.object_type,
						mesh_index);
					if (g_flight_mission_state
						    .proving_grounds_mode_active !=
					    0) {
						int16_t mesh_rotation =
							g_cur_craft->mesh_rotation
								[mesh_index];
						if (mesh_rotation != 0) {
							int16_t angle = -(
								int16_t)(mesh_rotation
									 << 8);
							int16_t sine =
								trig2_getsignedsin(
									angle);
							int cosine =
								trig2_getsignedcos(
									angle);
							uint16_t rotated_x = (uint16_t)
								math_dot2q15_wrapped(
									cosine,
									-sine,
									center_x,
									center_z);
							center_z =
								math_dot2q15_wrapped(
									sine,
									cosine,
									center_x,
									center_z);
							center_x = rotated_x;
						}
					}
					g_rotated_x = center_x;
					g_rotated_y = center_z;
					g_rotated_z = -center_y;
					pai_rotate_local_vector_to_world_scratch(
						&g_object_table[victim_idx],
						center_x, center_z, -center_y);
					g_object_table[explosion_obj_idx]
						.world_x += g_rotated_x;
					g_object_table[explosion_obj_idx]
						.world_y += g_rotated_y;
					g_object_table[explosion_obj_idx]
						.world_z += g_rotated_z;
					g_object_table[explosion_obj_idx]
						.object_type = -127;
					g_object_table[explosion_obj_idx]
						.genus_id =
						CRAFT_GENUS_EXPLOSION;
					g_object_table[explosion_obj_idx]
						.mobj->family = 5;
					g_object_table[explosion_obj_idx]
						.type_specific_byte[0] = 2;
					g_object_table[explosion_obj_idx]
						.mobj->seconds_alive = 0;
					g_object_table[explosion_obj_idx]
						.mobj->lifetime_timer = 0;
					g_object_table[explosion_obj_idx]
						.mobj->speed =
						g_object_table[victim_idx]
							.mobj->speed;
					g_object_table[explosion_obj_idx]
						.pitch =
						g_object_table[victim_idx]
							.pitch;
					g_object_table[explosion_obj_idx].yaw =
						g_object_table[victim_idx].yaw;
					g_object_table[explosion_obj_idx].roll =
						0;
					g_object_table[explosion_obj_idx]
						.mobj->orient_matrix_dirty = 1;
					g_object_table[explosion_obj_idx]
						.mobj->move_vector_dirty =
						g_object_table[explosion_obj_idx]
							.mobj
							->orient_matrix_dirty;
					fsfx_play_sound(
						(game_rand() &
						 3) + FLIGHT_SOUND_SMALL_EXPLOSION_FIRST,
						explosion_obj_idx,
						g_local_player);
					int effect_size =
						model_mesh_get_component_max_extent(
							g_object_table[victim_idx]
								.object_type,
							mesh_index) >>
						9;
					if (effect_size > UINT8_MAX) {
						effect_size = UINT8_MAX;
					}
					g_object_table[explosion_obj_idx]
						.mobj->effect_size =
						(uint8_t)effect_size;
				}
			}

			if (g_players[g_local_player].object_index ==
			    source_obj_idx) {
				int adjusted_index = mesh_index;
				int object_type =
					g_object_table[victim_idx].object_type;
				if (object_type <
				    OBJECT_TYPE_MESH_CACHE_COUNT) {
					if (adjusted_index < 0) {
						mesh_type =
							MESH_COMPONENT_00_DEFAULT;
					} else {
						mesh_count =
							g_object_type_mesh_cache
								[object_type]
									.mesh_count;
						if (mesh_count <= mesh_index) {
							adjusted_index =
								mesh_count - 1;
						}
						mesh_type =
							g_object_type_mesh_cache[object_type]
								.mesh_types
									[adjusted_index];
					}
				} else {
					mesh_type =
						model_mesh_get_object_type_mesh_type(
							object_type,
							mesh_index);
				}
				switch (mesh_type) {
				case MESH_COMPONENT_04_LASR_TUR:
				case MESH_COMPONENT_05_LASR_GUN:
				case MESH_COMPONENT_21_ROTATING_LASR_TUR:
					fsfx_speak_wingman_event(
						g_local_player, -1, 23, 2,
						victim_obj_idx, 0x4000);
					break;
				case MESH_COMPONENT_08_SHLD_GEN:
					fsfx_speak_wingman_event(
						g_local_player, -1, 23, 3,
						victim_obj_idx, UINT16_MAX);
					break;
				case MESH_COMPONENT_10_WHEAD_LN:
				case MESH_COMPONENT_22_WHEAD_LN:
					fsfx_speak_wingman_event(
						g_local_player, -1, 23, 1,
						victim_obj_idx, UINT16_MAX);
					break;
				case MESH_COMPONENT_11_COMM_SYS:
				case MESH_COMPONENT_12_BEAM_SYS:
				case MESH_COMPONENT_23_COMM_SYS:
				case MESH_COMPONENT_24_BEAM_SYS:
					fsfx_speak_wingman_event(
						g_local_player, -1, 23, 4,
						victim_obj_idx, UINT16_MAX);
					break;
				default:
					break;
				}
			}
		}
	} else {
		int new_hp = (int)(16 * g_cur_craft->component_hp[mesh_index] -
				   damage_amount) >>
			     4;
		if (new_hp == 0) {
			new_hp = 1;
		}
		damage_amount = 0;
		g_cur_craft->component_hp[mesh_index] = (uint8_t)new_hp;
	}
	return damage_amount;
}

/* Besides spawning the effects, this points g_cur_craft at the object's craft
 * and leaves it there. */
/* Spawns explosions on a craft's main hull meshes (the first 16 found; mesh 0
 * when none). Unless forced, returns at once unless a game_rand value read as 16
 * bits is below 0x1FFF. Past that point it points g_cur_craft at the craft and
 * rebuilds the object's orientation matrix when it is marked dirty. Forced:
 * frees the first explosion slot, spawns an explosion with effect_size the
 * type's max_bounds_extent at the first hull mesh's center and plays
 * FLIGHT_SOUND_LARGE_EXPLOSION. Otherwise picks a random hull mesh and, when it
 * still has component_hp, spawns one with effect_size max_bounds_extent / 16 at a
 * random vertex of it, with one of four small explosion sounds. */
// FUNCTION: XVT 0x4A7480
void craft_spawn_main_hull_explosion_effects(uint16_t object_idx,
					     int16_t force_main_explosion)
{
	/* Spawn main-hull explosion effects for eligible mesh components. */
	enum {
		OBJECT_TYPE_MESH_CACHE_COUNT = 73,
		MAX_HULL_MESHES = 16,
		MAIN_EXPLOSION_SLOT_COUNT = 1
	};

	if ((uint16_t)game_rand() >= 0x1FFF && force_main_explosion == 0) {
		return;
	}

	struct object_record *object = &g_object_table[object_idx];
	g_cur_craft = object->mobj->p_craft;
	uint16_t object_type = object->object_type;
	if (object->mobj->orient_matrix_dirty != 0) {
		fview_set_object_transform(object->roll, object->pitch,
					   object->yaw, 0, object);
	}
	int mesh_count;
	if (object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
		mesh_count = g_object_type_mesh_cache[object_type].mesh_count;
	} else {
		mesh_count = model_mesh_get_object_type_mesh_count(object_type);
	}

	uint16_t hull_count = 0;
	uint8_t hull_meshes[MAX_HULL_MESHES];
	for (uint16_t mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
		int lookup_index = mesh_index;
		int mesh_type;
		if (object_type < OBJECT_TYPE_MESH_CACHE_COUNT) {
			if (lookup_index < 0) {
				mesh_type = MESH_COMPONENT_00_DEFAULT;
			} else {
				int cached_mesh_count =
					g_object_type_mesh_cache[object_type]
						.mesh_count;
				if (lookup_index >= cached_mesh_count) {
					lookup_index = cached_mesh_count - 1;
				}
				mesh_type =
					g_object_type_mesh_cache[object_type]
						.mesh_types[lookup_index];
			}
		} else {
			mesh_type = model_mesh_get_object_type_mesh_type(
				object_type, lookup_index);
		}
		if (mesh_type == MESH_COMPONENT_01_MAIN_HULL) {
			hull_meshes[hull_count++] = (uint8_t)mesh_index;
		}
		if (hull_count == MAX_HULL_MESHES) {
			break;
		}
	}
	if (hull_count == 0) {
		hull_meshes[0] = 0;
		hull_count = 1;
	}

	if (force_main_explosion != 0) {
		uint16_t explosion_slot =
			g_object_slot_range_by_genus[CRAFT_GENUS_EXPLOSION]
				.start;
		for (uint16_t cleared_slots = 0;
		     cleared_slots < MAIN_EXPLOSION_SLOT_COUNT;
		     ++cleared_slots) {
			g_object_table[explosion_slot++].object_type = 0;
		}
		uint16_t main_mesh = hull_meshes[0];
		int main_extent =
			g_object_type_table[object_type].max_bounds_extent;
		craft_spawn_explosion_object_at_mesh(object, main_mesh,
						     main_extent, 0);
		fsfx_play_sound(FLIGHT_SOUND_LARGE_EXPLOSION, object_idx,
				g_local_player);
	} else {
		uint16_t random_value = game_rand();
		uint8_t selected_mesh = hull_meshes[random_value % hull_count];
		uint8_t component_hp = g_cur_craft->component_hp[selected_mesh];
		if (component_hp != 0) {
			int explosion_size = g_object_type_table[object_type]
						     .max_bounds_extent >>
					     4;
			uint16_t explosion_idx =
				craft_spawn_explosion_object_at_mesh(
					object, selected_mesh, explosion_size,
					1);
			if (explosion_idx != UINT16_MAX) {
				fsfx_play_sound(
					(game_rand2() &
					 3) + FLIGHT_SOUND_SMALL_EXPLOSION_FIRST,
					explosion_idx, g_local_player);
			}
		}
	}
}

/* Spawns a still explosion object (genus 13) at a mesh of obj_record: at its
 * center, or at a random vertex when use_random_vertex is set; type 129 at the
 * center or vertex 0, else 127 or 128 at random; effect size effect_size / 64.
 * Returns the new object's index, or UINT16_MAX when no explosion slot is free.
 * Writes g_rotated_x, g_rotated_y and g_rotated_z. Does not check that the mesh
 * has a vertex. */
// FUNCTION: XVT 0x4A76D0
int craft_spawn_explosion_object_at_mesh(const struct object_record *obj_record,
					 uint16_t mesh_index, int effect_size,
					 uint16_t use_random_vertex)
{
	/* Place an explosion object at a mesh center or random vertex. */
	uint16_t object_type = obj_record->object_type;

	int local_x;
	int local_y;
	int local_z;
	if (use_random_vertex == 0) {
		local_x = model_mesh_get_center_x(object_type, mesh_index);
		local_y = model_mesh_get_center_y(object_type, mesh_index);
		local_z = model_mesh_get_center_z(object_type, mesh_index);
	} else {
		/* From here use_random_vertex holds the chosen vertex index,
		 * not the flag. The explosion type below tests that index, so
		 * choosing vertex 0 gives the mesh-center type. */
		use_random_vertex =
			(uint16_t)game_rand() %
			model_mesh_get_vertex_count(object_type, mesh_index);
		local_x = model_mesh_get_vertex_x(object_type, mesh_index,
						  use_random_vertex);
		local_y = model_mesh_get_vertex_y(object_type, mesh_index,
						  use_random_vertex);
		local_z = model_mesh_get_vertex_z(object_type, mesh_index,
						  use_random_vertex);
	}

	pai_rotate_local_vector_to_world_scratch(obj_record, local_x, local_z,
						 -local_y);
	{
		uint16_t object_idx = object_alloc_slot_for_genus(13);
		if (object_idx == UINT16_MAX) {
			return object_idx;
		}
		g_object_table[object_idx].world_x =
			obj_record->world_x + g_rotated_x;
		g_object_table[object_idx].world_y =
			obj_record->world_y + g_rotated_y;
		g_object_table[object_idx].world_z =
			obj_record->world_z + g_rotated_z;
		g_object_table[object_idx].object_type =
			use_random_vertex == 0
				? -127
				: (uint8_t)((game_rand() & 1) + 127);
		g_object_table[object_idx].genus_id = 13;
		g_object_table[object_idx].mobj->family = 5;
		g_object_table[object_idx].type_specific_byte[0] = 2;
		g_object_table[object_idx].mobj->seconds_alive = 0;
		g_object_table[object_idx].mobj->lifetime_timer = 0;
		g_object_table[object_idx].mobj->effect_size = effect_size >> 6;
		g_object_table[object_idx].mobj->speed = 0;
		g_object_table[object_idx].pitch = 0;
		g_object_table[object_idx].yaw = 0;
		g_object_table[object_idx].roll = 0;
		g_object_table[object_idx].mobj->orient_matrix_dirty = 1;
		g_object_table[object_idx].mobj->move_vector_dirty =
			g_object_table[object_idx].mobj->orient_matrix_dirty;
		return object_idx;
	}
}
