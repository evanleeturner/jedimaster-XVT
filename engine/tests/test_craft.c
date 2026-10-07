/* Tests for xvt/flight/craft.c: shield banks, model lookups, the Tech
 * Library's ratings, a craft's linked objects, warhead kinds, which meshes the
 * component selector offers, the damage a hit does to a component, and the
 * explosions placed on a craft's meshes. Each check builds the world it needs
 * in the game's own tables: an object table this file owns, a craft record
 * for each of its first slots, and a model whose meshes are set by hand in a
 * memory handle, as a loaded model is kept. No game data is read. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"

enum {
	SLOT_COUNT = 64,
	CRAFT_SLOTS = 8,
	MESH_SLOTS = 9,
	VICTIM = 3,	      /* The slot of the craft that is hit. */
	SOURCE = 5,	      /* The slot of the object that hits it. */
	VICTIM_TYPE = 30,     /* An object type with a mesh cache entry. */
	SSD = 54,	      /* The Super Star Destroyer's object type. */
	CORVETTE = 40,	      /* The Corellian corvette's object type. */
	A_WING = 3,	      /* The A-wing's object type. */
	DYNAMIC_TYPE = 98,    /* An object type past the mesh cache. */
	EXPLOSION_START = 40, /* The explosion slots, 40 to 47. */
	EXPLOSION_END = 48,
	Q15_ONE = 0x7FFF,
	NO_PLAYER = 7,
	DAMAGEABLE = 2, /* The component flag of a damageable mesh. */
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[SLOT_COUNT];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static struct opt_node g_mesh_nodes[MESH_SLOTS];
static struct opt_node *g_mesh_roots[MESH_SLOTS];
static struct mesh_descriptor g_mesh_descriptors[MESH_SLOTS];
static uint16_t g_model_handle;

/* An empty world: no object in any slot, every slot with a mobile object
 * whose axes lie along the world's (side X, forward Y, up Z), the first slots
 * with craft records, no player flying (the local player is slot 7), mission
 * version 0, no proving grounds, explosion slots 40 to 47 free. The game's
 * random generator starts from a fixed state. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	memset(g_object_slot_range_by_genus, 0,
	       sizeof g_object_slot_range_by_genus);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(&g_mission_countdown_clock, 0, sizeof g_mission_countdown_clock);
	g_object_table = g_test_objects;
	g_craft_data_pool_base = g_test_craft;
	for (int i = 0; i < SLOT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].cached_side_x = Q15_ONE;
		g_test_mobiles[i].cached_fwd_y = Q15_ONE;
		g_test_mobiles[i].cached_up_z = Q15_ONE;
		if (i < CRAFT_SLOTS) {
			g_test_mobiles[i].p_craft = &g_test_craft[i];
		}
	}
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
	}
	g_object_slot_range_by_genus[CRAFT_GENUS_EXPLOSION].start =
		EXPLOSION_START;
	g_object_slot_range_by_genus[CRAFT_GENUS_EXPLOSION].end = EXPLOSION_END;
	g_local_player = NO_PLAYER;
	g_mission_file_version = 0;
	g_flight_sim_side_effects_suppressed = 0;
	g_game_rand_value_state = 0x1234;
	g_game_rand_feedback_state = 0x5678;
}

/* Gives object type type a model of count meshes: mesh i has component type
 * types[i], is damageable when bit i of damageable is set, and has its center
 * at (100 + i, 200 + i, 300 + i). A type below 73 gets the same meshes in its
 * mesh cache entry. */
static void set_model(int type, int count, const int *types,
		      unsigned damageable)
{
	struct optimized_poly_object *model =
		memory_get_handle_block(g_model_handle);
	memset(model, 0, sizeof *model);
	model->self_marker = model;
	model->root_node_count = count;
	model->root_nodes = g_mesh_roots;
	for (int i = 0; i < count; ++i) {
		memset(&g_mesh_nodes[i], 0, sizeof g_mesh_nodes[i]);
		memset(&g_mesh_descriptors[i], 0, sizeof g_mesh_descriptors[i]);
		g_mesh_nodes[i].node_type = OPT_MESHDESC;
		g_mesh_nodes[i].payload = &g_mesh_descriptors[i];
		g_mesh_roots[i] = &g_mesh_nodes[i];
		g_mesh_descriptors[i].mesh_type = types[i];
		g_mesh_descriptors[i].component_flags =
			((damageable >> i) & 1) != 0 ? DAMAGEABLE : 0;
		g_mesh_descriptors[i].center.x = (float)(100 + i);
		g_mesh_descriptors[i].center.y = (float)(200 + i);
		g_mesh_descriptors[i].center.z = (float)(300 + i);
	}
	g_loaded_models[type] = g_model_handle;
	g_object_type_table[type].asset_flags |= 1;
	if (type < 73) {
		g_object_type_mesh_cache[type].mesh_count = count;
		for (int i = 0; i < count; ++i) {
			g_object_type_mesh_cache[type].mesh_types[i] = types[i];
		}
	}
}

/* Puts a craft of object type type in craft slot obj at a point away from
 * the origin and off the axes; g_cur_craft points at its record. */
static struct craft_data *place_craft(int obj, int type)
{
	g_test_objects[obj].object_type = (uint8_t)type;
	g_test_objects[obj].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_objects[obj].world_x = 5000 + 100 * obj;
	g_test_objects[obj].world_y = -7000 + 10 * obj;
	g_test_objects[obj].world_z = 3000 + obj;
	g_cur_craft = &g_test_craft[obj];
	return g_cur_craft;
}

/* ------------------------------------------------------------------------ */
/* Shield banks and model lookups. */

/* craft_adjust_current_shield_energy adds delta to one bank of g_cur_craft and
 * clamps it to 0 through twice the model's shield_strength; the other bank is
 * left alone. craft_get_object_max_shield gives that cap. */
static void check_shield_bank_clamps(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(VICTIM, VICTIM_TYPE);
	int model = get_model_index_from_type(1);
	g_model_defs[model].shield_strength = 300;
	craft->model_index = (uint8_t)model;
	craft->shield_energy[0] = 100;
	craft->shield_energy[1] = 50;
	craft_adjust_current_shield_energy(0, 0, 25);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 125);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 50);
	craft_adjust_current_shield_energy(0, 1, -20);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 30);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 125);

	/* Below 0 it stops at 0, above the cap at the cap. */
	craft_adjust_current_shield_energy(0, 1, -31);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 0);
	craft_adjust_current_shield_energy(0, 0, 476);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 600);
	craft_adjust_current_shield_energy(0, 0, -1);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 599);
	craft_adjust_current_shield_energy(0, 0, 2);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 600);
	XVT_ASSERT_INT_EQ(craft_get_object_max_shield(VICTIM), 600);
}

/* get_model_index_from_type gives the type table's model index. */
static void check_model_index_from_type(void)
{
	for (int type = 0; type < 201; ++type) {
		XVT_ASSERT_INT_EQ(get_model_index_from_type(type),
				  g_object_type_table[type].model_index);
	}
}

/* ------------------------------------------------------------------------ */
/* The Tech Library's ratings. */

enum { RATED_TYPE = 20, RATED_MODEL = 3 };

/* Object type RATED_TYPE of genus genus, whose model, RATED_MODEL, has round
 * figures and no weapons. */
static struct model_def *rated_model(int genus)
{
	g_object_type_table[RATED_TYPE].model_index = RATED_MODEL;
	g_object_type_table[RATED_TYPE].genus_id = (uint8_t)genus;
	struct model_def *model = &g_model_defs[RATED_MODEL];
	model->max_speed = 90;
	model->accel_rate = 45;
	model->pitch_rate = 1000;
	model->roll_rate = 2000;
	model->has_shields = 1;
	model->shield_strength = 500;
	model->hull_strength = 1050;
	for (int group = 0; group < 2; ++group) {
		model->laser_group_weapon_type[group] = 0;
		model->laser_group_slot_count[group] = 0;
		model->warhead_launcher_type[group] = 0;
		model->warhead_launcher_capacity[group] = 0;
		model->warhead_launcher_slot_count[group] = 0;
	}
	return model;
}

/* Speed and acceleration are the model's figures times 4/9, maneuver is pitch
 * plus roll rate times the maneuver scale, each rounded to nearest; shield is
 * shield_strength / 50 (0 without shields) and hull hull_strength / 105. The
 * genus is filled, craft_type and unused_rating are left as they were, and 1
 * is returned. */
static void check_tech_stats_ratings(void)
{
	struct model_def *model = rated_model(CRAFT_GENUS_STARFIGHTER);
	struct craft_tech_stats stats;
	memset(&stats, 0, sizeof stats);
	stats.craft_type = RATED_TYPE;
	stats.unused_rating = 77;
	stats.genus_id = 9;
	XVT_ASSERT_INT_EQ(build_craft_tech_stats(&stats), 1);
	XVT_ASSERT_INT_EQ(stats.craft_type, RATED_TYPE);
	XVT_ASSERT_INT_EQ(stats.unused_rating, 77);
	XVT_ASSERT_INT_EQ(stats.genus_id, CRAFT_GENUS_STARFIGHTER);
	XVT_ASSERT_INT_EQ(stats.speed_rating, 40);
	XVT_ASSERT_INT_EQ(stats.acceleration_rating, 20);
	/* 3,000 times the scale is 15.69: rounded, 16. */
	XVT_ASSERT_INT_EQ(stats.maneuver_rating, 16);
	XVT_ASSERT_INT_EQ(stats.shield_rating, 10);
	XVT_ASSERT_INT_EQ(stats.hull_rating, 10);

	/* 4/9 of 92 is 40.9 and of 47 is 20.9: both round up. Below half they
	 * round down. */
	model->max_speed = 92;
	model->accel_rate = 47;
	model->pitch_rate = 900;
	model->roll_rate = 1000;
	build_craft_tech_stats(&stats);
	XVT_ASSERT_INT_EQ(stats.speed_rating, 41);
	XVT_ASSERT_INT_EQ(stats.acceleration_rating, 21);
	/* 1,900 times the scale is 9.94. */
	XVT_ASSERT_INT_EQ(stats.maneuver_rating, 10);
	model->max_speed = 91;
	model->accel_rate = 46;
	model->pitch_rate = 800;
	build_craft_tech_stats(&stats);
	XVT_ASSERT_INT_EQ(stats.speed_rating, 40);
	XVT_ASSERT_INT_EQ(stats.acceleration_rating, 20);
	/* 1,800 times the scale is 9.42. */
	XVT_ASSERT_INT_EQ(stats.maneuver_rating, 9);

	model->has_shields = 0;
	build_craft_tech_stats(&stats);
	XVT_ASSERT_INT_EQ(stats.shield_rating, 0);
	XVT_ASSERT_INT_EQ(stats.hull_rating, 10);
}

/* Starships and platforms get 16 times the shield and hull ratings,
 * freighters 4 times. */
static void check_tech_stats_genus_scale(void)
{
	static const struct {
		int genus;
		int scale;
	} cases[] = {
		{CRAFT_GENUS_STARSHIP, 16},
		{CRAFT_GENUS_PLATFORM, 16},
		{CRAFT_GENUS_FREIGHTER, 4},
		{CRAFT_GENUS_STARFIGHTER, 1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		rated_model(cases[i].genus);
		struct craft_tech_stats stats;
		memset(&stats, 0, sizeof stats);
		stats.craft_type = RATED_TYPE;
		build_craft_tech_stats(&stats);
		XVT_ASSERT_INT_EQ(stats.shield_rating, 10 * cases[i].scale);
		XVT_ASSERT_INT_EQ(stats.hull_rating, 10 * cases[i].scale);
	}
}

/* Lasers count the slots of laser groups firing object type 137 or 139, ions
 * the slots of groups firing 141; another weapon type counts as neither.
 * Warheads sum capacity times slots of each launcher with a type. */
static void check_tech_stats_weapons(void)
{
	struct model_def *model = rated_model(CRAFT_GENUS_STARFIGHTER);
	struct craft_tech_stats stats;
	memset(&stats, 0, sizeof stats);
	stats.craft_type = RATED_TYPE;
	model->laser_group_weapon_type[0] = 137;
	model->laser_group_slot_count[0] = 4;
	model->laser_group_weapon_type[1] = 141;
	model->laser_group_slot_count[1] = 2;
	model->warhead_launcher_type[0] = 143;
	model->warhead_launcher_capacity[0] = 3;
	model->warhead_launcher_slot_count[0] = 2;
	model->warhead_launcher_type[1] = 144;
	model->warhead_launcher_capacity[1] = 5;
	model->warhead_launcher_slot_count[1] = 1;
	build_craft_tech_stats(&stats);
	XVT_ASSERT_INT_EQ(stats.laser_count, 4);
	XVT_ASSERT_INT_EQ(stats.ion_count, 2);
	XVT_ASSERT_INT_EQ(stats.warhead_rating, 11);

	model->laser_group_weapon_type[0] = 139;
	model->laser_group_weapon_type[1] = 139;
	model->warhead_launcher_type[1] = 0;
	build_craft_tech_stats(&stats);
	XVT_ASSERT_INT_EQ(stats.laser_count, 6);
	XVT_ASSERT_INT_EQ(stats.ion_count, 0);
	XVT_ASSERT_INT_EQ(stats.warhead_rating, 6);

	model->laser_group_weapon_type[0] = 138;
	model->laser_group_weapon_type[1] = 142;
	build_craft_tech_stats(&stats);
	XVT_ASSERT_INT_EQ(stats.laser_count, 0);
	XVT_ASSERT_INT_EQ(stats.ion_count, 0);
}

/* The TIE Advanced, T-Wing, Z-95 and R-41 get fixed weapon figures, whatever
 * their models carry; the Z-95 and R-41 keep their warhead rating. */
static void check_tech_stats_fixed_figures(void)
{
	static const struct {
		int type;
		int lasers;
		int ions;
		int warheads; /* -1: the model's own. */
	} cases[] = {
		{CRAFT_SPECIES_TIE_ADVANCED, 4, 0, 8},
		{CRAFT_SPECIES_T_WING, 2, 0, 8},
		{CRAFT_SPECIES_Z_95_HEADHUNTER, 2, 0, -1},
		{CRAFT_SPECIES_R_41_STARCHASER, 2, 2, -1},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		int type = cases[i].type;
		g_object_type_table[type].model_index = RATED_MODEL;
		struct model_def *model = rated_model(CRAFT_GENUS_STARFIGHTER);
		model->laser_group_weapon_type[0] = 141;
		model->laser_group_slot_count[0] = 6;
		model->warhead_launcher_type[0] = 143;
		model->warhead_launcher_capacity[0] = 3;
		model->warhead_launcher_slot_count[0] = 1;
		struct craft_tech_stats stats;
		memset(&stats, 0, sizeof stats);
		stats.craft_type = type;
		XVT_ASSERT_INT_EQ(build_craft_tech_stats(&stats), 1);
		XVT_ASSERT_INT_EQ(stats.laser_count, cases[i].lasers);
		XVT_ASSERT_INT_EQ(stats.ion_count, cases[i].ions);
		XVT_ASSERT_INT_EQ(stats.warhead_rating,
				  cases[i].warheads < 0 ? 3
							: cases[i].warheads);
	}
}

/* A type with no model gets only its genus and returns 0. */
static void check_tech_stats_no_model(void)
{
	g_object_type_table[RATED_TYPE].model_index = MODEL_INDEX_NONE;
	g_object_type_table[RATED_TYPE].genus_id = CRAFT_GENUS_FREIGHTER;
	struct craft_tech_stats stats;
	memset(&stats, 0, sizeof stats);
	stats.craft_type = RATED_TYPE;
	stats.speed_rating = 55;
	stats.hull_rating = 66;
	XVT_ASSERT_INT_EQ(build_craft_tech_stats(&stats), 0);
	XVT_ASSERT_INT_EQ(stats.genus_id, CRAFT_GENUS_FREIGHTER);
	XVT_ASSERT_INT_EQ(stats.speed_rating, 55);
	XVT_ASSERT_INT_EQ(stats.hull_rating, 66);
}

/* ------------------------------------------------------------------------ */
/* Linked objects. */

/* craft_clear_turret_object_links sets all 16 links to NULL and frees
 * nothing. */
static void check_turret_links_cleared(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(VICTIM, VICTIM_TYPE);
	for (int link = 0; link < 16; ++link) {
		g_test_objects[20 + link].object_type = 7;
		craft->turret_object_links[link] = &g_test_objects[20 + link];
	}
	craft_clear_turret_object_links(craft);
	for (int link = 0; link < 16; ++link) {
		XVT_ASSERT_TRUE(craft->turret_object_links[link] == NULL);
		XVT_ASSERT_INT_EQ(g_test_objects[20 + link].object_type, 7);
	}
}

/* craft_free_linked_objects frees the object effective_ai_object_link points
 * at and each object in turret_object_links (object type 0, the free slot
 * mark), and sets those links to NULL; other objects are left alone. */
static void check_linked_objects_freed(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(VICTIM, VICTIM_TYPE);
	for (int obj = 10; obj < 40; ++obj) {
		g_test_objects[obj].object_type = 7;
	}
	craft->effective_ai_object_link = &g_test_objects[10];
	craft->turret_object_links[0] = &g_test_objects[11];
	craft->turret_object_links[7] = &g_test_objects[12];
	craft->turret_object_links[15] = &g_test_objects[13];
	craft_free_linked_objects(craft);
	XVT_ASSERT_TRUE(craft->effective_ai_object_link == NULL);
	for (int link = 0; link < 16; ++link) {
		XVT_ASSERT_TRUE(craft->turret_object_links[link] == NULL);
	}
	for (int obj = 10; obj < 14; ++obj) {
		XVT_ASSERT_INT_EQ(g_test_objects[obj].object_type, 0);
	}
	for (int obj = 14; obj < 40; ++obj) {
		XVT_ASSERT_INT_EQ(g_test_objects[obj].object_type, 7);
	}
	XVT_ASSERT_INT_EQ(g_test_objects[VICTIM].object_type, VICTIM_TYPE);
}

/* ------------------------------------------------------------------------ */
/* Warhead kinds. */

/* object_type_get_warhead_kind_index gives each warhead object type its kind,
 * the place of its messages in each block of launcher messages: the kind
 * added to the first "launcher armed" message is that warhead's. Any other
 * type gives -1. */
static void check_warhead_kinds(void)
{
	static const struct {
		int type;
		int armed_message;
	} warheads[] = {
		{WARHEAD_OBJECT_TYPE_PROTON_TORPEDO,
		 IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED},
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE,
		 IFMSG_009_CONCUSSION_MISSILE_LAUNCHER_ARMED},
		{WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO,
		 IFMSG_010_ADV_PROTON_TORPEDO_LAUNCHER_ARMED},
		{WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE,
		 IFMSG_011_ADV_CONCUSSION_MISSILE_LAUNCHER_ARMED},
		{WARHEAD_OBJECT_TYPE_SPACE_BOMB,
		 IFMSG_012_SPACE_BOMB_LAUNCHER_ARMED},
		{WARHEAD_OBJECT_TYPE_HEAVY_ROCKET,
		 IFMSG_013_HEAVY_ROCKET_LAUNCHER_ARMED},
		{WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE,
		 IFMSG_014_MAGNETIC_PULSE_LAUNCHER_ARMED},
	};
	for (size_t i = 0; i < sizeof warheads / sizeof warheads[0]; ++i) {
		XVT_ASSERT_INT_EQ(
			object_type_get_warhead_kind_index(
				(uint16_t)warheads[i].type) +
				IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED,
			warheads[i].armed_message);
	}
	static const int others[] = {
		0,     1,   PROJECTILE_OBJECT_TYPE_REBEL_LASER,	   142,
		145,   147, COUNTERMEASURE_PROJECTILE_OBJECT_TYPE, 255,
		0x8F00};
	for (size_t i = 0; i < sizeof others / sizeof others[0]; ++i) {
		XVT_ASSERT_INT_EQ(
			object_type_get_warhead_kind_index((uint16_t)others[i]),
			-1);
	}
}

/* Known failure ion_pulse_has_no_kind, issue #26: the function promises each
 * warhead object type its kind. Object types 153 (ion pulse) and 154 are the
 * warheads of choices 8 and 9 (g_warhead_type_ids), and every block of
 * launcher messages has their places (IFMSG_015_ION_PULSE_LAUNCHER_ARMED, then
 * a place kept for the next), but both give -1, which the callers turn into a
 * message number before the block or, cut to 16 bits, far past the table. The
 * throwaway fix lists the two types in the function, giving them the places
 * their messages hold; the callers could instead check for -1, but the
 * function's own promise is the one that breaks. */
static void check_ion_pulse_kind(void)
{
	XVT_ASSERT_INT_EQ(object_type_get_warhead_kind_index(
				  WARHEAD_OBJECT_TYPE_ION_PULSE) +
				  IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED,
			  IFMSG_015_ION_PULSE_LAUNCHER_ARMED);
	int kind =
		object_type_get_warhead_kind_index(WARHEAD_OBJECT_TYPE_LASER_3);
	XVT_ASSERT_TRUE(kind >= 0);
	XVT_ASSERT_TRUE(kind + IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED <=
			IFMSG_017_FUTURE_LAUNCHER_ARMED);
}

/* ------------------------------------------------------------------------ */
/* The component selector. */

/* Misc hull and antenna meshes are never offered; a mesh with target id 0 is,
 * and so is one with target id 1 that is neither main hull nor fuselage; in a
 * group of meshes with the same target id and type only the first is. */
static void check_selectable_meshes(void)
{
	fresh_world();
	static const int types[] = {
		MESH_COMPONENT_18_MISC_HULL, MESH_COMPONENT_19_ANTENNA,
		MESH_COMPONENT_05_LASR_GUN,  MESH_COMPONENT_05_LASR_GUN,
		MESH_COMPONENT_01_MAIN_HULL, MESH_COMPONENT_05_LASR_GUN,
		MESH_COMPONENT_08_SHLD_GEN,  MESH_COMPONENT_03_FUSELAGE,
		MESH_COMPONENT_08_SHLD_GEN,
	};
	for (int pass = 0; pass < 2; ++pass) {
		int type = pass == 0 ? VICTIM_TYPE : DYNAMIC_TYPE;
		set_model(type, 9, types, 0);
		/* Mesh 2 has target id 0; meshes 3 and 5 form a group of
		 * guns with target id 2, mesh 4 a main hull with id 1, mesh 6
		 * a shield generator with id 1, mesh 7 a fuselage with id 1,
		 * mesh 8 a second shield generator with id 1. */
		g_mesh_descriptors[3].target_id = 2;
		g_mesh_descriptors[5].target_id = 2;
		g_mesh_descriptors[4].target_id = 1;
		g_mesh_descriptors[6].target_id = 1;
		g_mesh_descriptors[7].target_id = 1;
		g_mesh_descriptors[8].target_id = 1;
		g_mesh_descriptors[0].target_id = 1;
		g_mesh_descriptors[1].target_id = 3;
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 0), 0);
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 1), 0);
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 2), 1);
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 3), 1);
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 5), 0);
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 6), 1);
		/* With id 1 each of a group is offered. */
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 8), 1);
		/* The main hull and the fuselage with id 1 are each the
		 * first of their type with it. */
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 4), 1);
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 7), 1);
		/* A second main hull with id 1 after the first is not. */
		g_mesh_descriptors[7].mesh_type = MESH_COMPONENT_01_MAIN_HULL;
		if (type < 73) {
			g_object_type_mesh_cache[type].mesh_types[7] =
				MESH_COMPONENT_01_MAIN_HULL;
		}
		XVT_ASSERT_INT_EQ(
			craft_is_selectable_damage_component_mesh(type, 7), 0);
		g_loaded_models[type] = 0;
	}
}

/* ------------------------------------------------------------------------ */
/* Damage to a component. */

/* The victim, a craft of VICTIM_TYPE whose meshes are a laser gun, a shield
 * generator, the main hull and a second shield generator, all damageable;
 * the gun holds hp. The source is an object of type 1 in slot SOURCE. */
static struct craft_data *damage_world(int hp)
{
	static const int types[] = {
		MESH_COMPONENT_05_LASR_GUN,
		MESH_COMPONENT_08_SHLD_GEN,
		MESH_COMPONENT_01_MAIN_HULL,
		MESH_COMPONENT_08_SHLD_GEN,
	};
	fresh_world();
	set_model(VICTIM_TYPE, 4, types, 0xF);
	struct craft_data *craft = place_craft(VICTIM, VICTIM_TYPE);
	craft->component_hp[0] = (uint8_t)hp;
	craft->component_hp[1] = 30;
	craft->component_hp[2] = 40;
	craft->component_hp[3] = 50;
	craft->hull_max = 10000;
	g_test_objects[SOURCE].object_type = 1;
	return craft;
}

/* Damage under 16 times component_hp leaves (16 * hp - damage) / 16 hit points,
 * at least 1, and returns 0; the component stays intact. A damage of 0 counts
 * as 1. */
static void check_component_small_hit(void)
{
	struct craft_data *craft = damage_world(10);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 33, SOURCE), 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], (160 - 33) / 16);
	XVT_ASSERT_INT_EQ(craft->component_state[0], 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[1], 30);

	craft = damage_world(10);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 159, SOURCE), 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 1);

	craft = damage_world(1);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 15, SOURCE), 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 1);
	craft = damage_world(1);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 0, SOURCE), 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 1);

	/* A damage of 0 counts as 1: from 16 * 2 it leaves 31 / 16. */
	craft = damage_world(2);
	craft_damage_component(VICTIM, 1, 0, SOURCE);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 1);
	craft = damage_world(2);
	craft_damage_component(VICTIM, 1, 16, SOURCE);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 1);
	craft = damage_world(3);
	craft_damage_component(VICTIM, 1, 0, SOURCE);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 2);
}

/* A component already at 0, or undamageable (component_hp 255) on a type
 * other than 54, passes the damage on unchanged. */
static void check_component_passes_damage(void)
{
	struct craft_data *craft = damage_world(0);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 77, SOURCE), 77);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);
	XVT_ASSERT_INT_EQ(craft->component_state[0], 0);

	craft = damage_world(UINT8_MAX);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 5000, SOURCE),
			  5000);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], UINT8_MAX);
	XVT_ASSERT_INT_EQ(craft->component_state[0], 0);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 0, SOURCE), 0);
}

/* Damage of at least 16 times component_hp destroys the component: hp 0, the
 * rest returned. A damageable mesh also gets component_state 2. */
static void check_component_destroyed(void)
{
	struct craft_data *craft = damage_world(10);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 160, SOURCE), 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);
	XVT_ASSERT_INT_EQ(craft->component_state[0], 2);
	XVT_ASSERT_INT_EQ(craft->component_state[1], 0);

	craft = damage_world(10);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 1000, SOURCE), 840);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);

	/* A mesh that is not damageable loses its hit points but keeps its
	 * state. */
	craft = damage_world(10);
	g_mesh_descriptors[0].component_flags = 0;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 170, SOURCE), 10);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);
	XVT_ASSERT_INT_EQ(craft->component_state[0], 0);

	/* The mesh is hit_mesh_index - 1. */
	craft = damage_world(10);
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 3, 640, SOURCE), 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[2], 0);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 10);
	XVT_ASSERT_INT_EQ(craft->component_state[2], 2);
}

/* Destroying a damageable component moves every player in the flight who
 * aims at it to the next intact selectable component, wrapping past the last
 * mesh; a player aiming at another component or craft, or not in the
 * flight, keeps their aim. */
static void check_component_destroyed_moves_aim(void)
{
	struct craft_data *craft = damage_world(10);
	for (int player = 0; player < 5; ++player) {
		g_players[player].participation_state = 1;
		g_players[player].current_target_object_idx = VICTIM;
		g_players[player].selected_target_component = 2;
	}
	g_players[1].selected_target_component = 1;
	g_players[2].current_target_object_idx = SOURCE;
	g_players[3].participation_state = 0;
	/* Mesh 3 is intact; mesh 0 and 1 are intact too. */
	craft_damage_component(VICTIM, 3, 640, SOURCE);
	XVT_ASSERT_INT_EQ(g_players[0].selected_target_component, 3);
	XVT_ASSERT_INT_EQ(g_players[4].selected_target_component, 3);
	XVT_ASSERT_INT_EQ(g_players[1].selected_target_component, 1);
	XVT_ASSERT_INT_EQ(g_players[2].selected_target_component, 2);
	XVT_ASSERT_INT_EQ(g_players[3].selected_target_component, 2);

	/* Past the last mesh it goes on from mesh 0, skipping a component
	 * already gone. */
	craft = damage_world(10);
	craft->component_state[0] = 2;
	g_players[0].participation_state = 1;
	g_players[0].current_target_object_idx = VICTIM;
	g_players[0].selected_target_component = 3;
	craft_damage_component(VICTIM, 4, 800, SOURCE);
	XVT_ASSERT_INT_EQ(g_players[0].selected_target_component, 1);
}

/* In the proving grounds a destroyed component counts a target, adds 50 to
 * the score (100 when its mesh_rotation is nonzero) and 2 seconds to the
 * countdown clock, carrying into the minutes. */
static void check_component_destroyed_in_proving_grounds(void)
{
	struct craft_data *craft = damage_world(10);
	g_flight_mission_state.proving_grounds_mode_active = 1;
	g_flight_mission_state.proving_grounds_targets_destroyed = 4;
	g_flight_mission_state.proving_grounds_score = 1230;
	g_mission_countdown_clock.minutes = 1;
	g_mission_countdown_clock.seconds = 20;
	craft_damage_component(VICTIM, 1, 160, SOURCE);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_targets_destroyed, 5);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_score, 1280);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 1);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 22);

	craft = damage_world(10);
	g_flight_mission_state.proving_grounds_mode_active = 1;
	craft->mesh_rotation[0] = 3;
	g_mission_countdown_clock.minutes = 1;
	g_mission_countdown_clock.seconds = 58;
	craft_damage_component(VICTIM, 1, 160, SOURCE);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_score, 100);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 2);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 0);

	/* Outside the proving grounds nothing is counted. */
	craft = damage_world(10);
	g_mission_countdown_clock.seconds = 20;
	craft_damage_component(VICTIM, 1, 160, SOURCE);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_targets_destroyed, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_score, 0);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 20);
}

/* A destroyed damageable component gets an explosion object at the craft's
 * position plus the mesh's center, as the craft's axes turn it into the
 * world, in the first free explosion slot. */
static void check_component_destroyed_explodes(void)
{
	damage_world(10);
	g_test_objects[EXPLOSION_START].object_type = 7;
	craft_damage_component(VICTIM, 1, 160, SOURCE);
	struct object_record *blast = &g_test_objects[EXPLOSION_START + 1];
	XVT_ASSERT_INT_EQ(blast->genus_id, CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_TRUE(blast->object_type != 0);
	pai_rotate_local_vector_to_world_scratch(&g_test_objects[VICTIM], 100,
						 300, -200);
	XVT_ASSERT_INT_EQ(blast->world_x,
			  g_test_objects[VICTIM].world_x + g_rotated_x);
	XVT_ASSERT_INT_EQ(blast->world_y,
			  g_test_objects[VICTIM].world_y + g_rotated_y);
	XVT_ASSERT_INT_EQ(blast->world_z,
			  g_test_objects[VICTIM].world_z + g_rotated_z);
	XVT_ASSERT_INT_EQ(g_test_objects[EXPLOSION_START + 2].object_type, 0);
}

/* In mission version 14 at difficulty 0, losing the last shield generator
 * empties both shield banks; while another generator holds hit points, or at
 * another difficulty or version, the banks are kept. */
static void check_last_generator_empties_shields(void)
{
	struct craft_data *craft = damage_world(10);
	g_mission_file_version = 14;
	craft->component_hp[3] = 0;
	craft->shield_energy[0] = 400;
	craft->shield_energy[1] = 300;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 2, 480, SOURCE), 0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 0);

	craft = damage_world(10);
	g_mission_file_version = 14;
	craft->shield_energy[0] = 400;
	craft->shield_energy[1] = 300;
	craft_damage_component(VICTIM, 2, 480, SOURCE);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 400);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 300);

	craft = damage_world(10);
	g_mission_file_version = 14;
	g_flight_mission_state.difficulty = 1;
	craft->component_hp[3] = 0;
	craft->shield_energy[0] = 400;
	craft_damage_component(VICTIM, 2, 480, SOURCE);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 400);

	craft = damage_world(10);
	craft->component_hp[3] = 0;
	craft->shield_energy[0] = 400;
	craft_damage_component(VICTIM, 2, 480, SOURCE);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 400);
}

/* A Super Star Destroyer, object type 54, whose mesh 0 is the bridge, with
 * hp hit points and an undamageable shield generator at mesh 1 that holds
 * none; its shield banks are empty. In mission version 14. The source in slot
 * SOURCE is of the given type. */
static struct craft_data *bridge_world(int hp, int source_type)
{
	static const int types[] = {
		MESH_COMPONENT_07_BRIDGE,
		MESH_COMPONENT_08_SHLD_GEN,
		MESH_COMPONENT_01_MAIN_HULL,
	};
	fresh_world();
	set_model(SSD, 3, types, 0);
	struct craft_data *craft = place_craft(VICTIM, SSD);
	craft->component_hp[0] = (uint8_t)hp;
	craft->component_hp[2] = UINT8_MAX;
	craft->hull_max = 10000;
	g_mission_file_version = 14;
	place_craft(SOURCE, source_type);
	g_cur_craft = craft;
	return craft;
}

/* While a shield generator holds hit points or a shield bank holds energy,
 * a hit on the bridge passes unchanged and the bridge keeps its hit points;
 * unshielded, so does a hit from a type other than 40 or 3. In another
 * mission version the bridge takes damage as any component does. */
static void check_bridge_shielded(void)
{
	struct craft_data *craft = bridge_world(10, CORVETTE);
	craft->component_hp[1] = 1;
	craft->hull_damage = 5000;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 200, SOURCE), 200);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 10);

	craft = bridge_world(10, CORVETTE);
	craft->shield_energy[0] = 1;
	craft->hull_damage = 5000;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 200, SOURCE), 200);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 10);
	craft = bridge_world(10, CORVETTE);
	craft->shield_energy[1] = 1;
	craft->hull_damage = 5000;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 200, SOURCE), 200);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 10);

	craft = bridge_world(10, 1);
	craft->hull_damage = 5000;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 200, SOURCE), 200);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 10);

	craft = bridge_world(10, CORVETTE);
	g_mission_file_version = 12;
	craft->hull_damage = 5000;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 200, SOURCE), 40);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);
}

/* Unshielded, a corvette's hit on the bridge is raised so that what is
 * returned is hull_max - hull_damage less 5 * (hull_max / 100): the bridge is
 * destroyed and the hull is brought to 95% of hull_max. */
static void check_bridge_corvette_to_95(void)
{
	static const unsigned hull_damages[] = {0, 5000, 9499, 9500};
	for (size_t i = 0; i < sizeof hull_damages / sizeof hull_damages[0];
	     ++i) {
		struct craft_data *craft = bridge_world(10, CORVETTE);
		craft->hull_damage = hull_damages[i];
		unsigned rest =
			(unsigned)craft_damage_component(VICTIM, 1, 50, SOURCE);
		XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);
		XVT_ASSERT_INT_EQ(craft->hull_damage + rest, 9500);
	}
	/* A bridge marked undamageable (255) is still guarded. */
	struct craft_data *craft = bridge_world(UINT8_MAX, CORVETTE);
	craft->hull_damage = 1000;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 50, SOURCE), 8500);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);
}

/* Unshielded, a hit on the bridge from a breaking-up A-wing (object type 3)
 * returns hull_max - hull_damage; one from an A-wing still flying passes
 * unchanged. */
static void check_bridge_breaking_up_a_wing(void)
{
	struct craft_data *craft = bridge_world(10, A_WING);
	g_test_craft[SOURCE].object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	craft->hull_damage = 9900;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 50, SOURCE), 100);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);

	craft = bridge_world(10, A_WING);
	g_test_craft[SOURCE].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->hull_damage = 9900;
	XVT_ASSERT_INT_EQ(craft_damage_component(VICTIM, 1, 50, SOURCE), 50);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 10);
}

/* Known failure corvette_bridge_past_95_percent, issue #200: the comment
 * says a corvette's damage to the bridge "is raised". Past the 95% mark the
 * rewrite lowers it instead: within 16 times the bridge's hit points of the
 * mark a hit that would destroy the bridge only wears it and does no hull
 * damage; further past, the unsigned sum wraps, and what is returned, read as
 * the caller reads it, takes the hull back down to the mark. The rest rests
 * on the issue: a hit is never worth less than its own damage, and never
 * repairs the hull. The throwaway fix computes the rewrite in signed
 * arithmetic and keeps the hit's own damage when that is more; the caller
 * could clamp what it adds instead, but the rewrite is where the damage
 * shrinks. */
static void check_bridge_corvette_past_95(void)
{
	struct craft_data *craft = bridge_world(10, CORVETTE);
	craft->hull_damage = 9600;
	int rest = craft_damage_component(VICTIM, 1, 200, SOURCE);
	XVT_ASSERT_INT_EQ(craft->component_hp[0], 0);
	XVT_ASSERT_INT_EQ(rest, 40);

	craft = bridge_world(10, CORVETTE);
	craft->hull_damage = 9900;
	rest = craft_damage_component(VICTIM, 1, 200, SOURCE);
	XVT_ASSERT_TRUE(rest >= 0);
}

/* ------------------------------------------------------------------------ */
/* Explosions on a craft's meshes. */

/* craft_spawn_explosion_object_at_mesh places a still explosion (genus 13) at
 * the mesh's center as the object's axes turn it into the world, of type 129
 * at the center, with effect size effect_size / 64, and returns its slot;
 * with no slot free it returns UINT16_MAX. */
static void check_explosion_at_mesh_center(void)
{
	static const int types[] = {
		MESH_COMPONENT_01_MAIN_HULL,
		MESH_COMPONENT_05_LASR_GUN,
	};
	fresh_world();
	set_model(VICTIM_TYPE, 2, types, 0);
	place_craft(VICTIM, VICTIM_TYPE);
	g_test_mobiles[VICTIM].cached_side_x = 0;
	g_test_mobiles[VICTIM].cached_side_y = Q15_ONE;
	g_test_mobiles[VICTIM].cached_fwd_y = 0;
	g_test_mobiles[VICTIM].cached_fwd_x = -Q15_ONE;
	g_test_mobiles[EXPLOSION_START].speed = 99;
	int slot = craft_spawn_explosion_object_at_mesh(&g_test_objects[VICTIM],
							1, 640, 0);
	XVT_ASSERT_INT_EQ(slot, EXPLOSION_START);
	struct object_record *blast = &g_test_objects[slot];
	pai_rotate_local_vector_to_world_scratch(&g_test_objects[VICTIM], 101,
						 301, -201);
	XVT_ASSERT_INT_EQ(blast->world_x,
			  g_test_objects[VICTIM].world_x + g_rotated_x);
	XVT_ASSERT_INT_EQ(blast->world_y,
			  g_test_objects[VICTIM].world_y + g_rotated_y);
	XVT_ASSERT_INT_EQ(blast->world_z,
			  g_test_objects[VICTIM].world_z + g_rotated_z);
	XVT_ASSERT_INT_EQ(blast->object_type, 129);
	XVT_ASSERT_INT_EQ(blast->genus_id, CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_INT_EQ(blast->mobj->effect_size, 10);
	XVT_ASSERT_INT_EQ(blast->mobj->speed, 0);

	for (int obj = EXPLOSION_START; obj < EXPLOSION_END; ++obj) {
		g_test_objects[obj].object_type = 7;
	}
	XVT_ASSERT_INT_EQ(craft_spawn_explosion_object_at_mesh(
				  &g_test_objects[VICTIM], 1, 640, 0),
			  UINT16_MAX);
}

/* A forced main hull explosion frees the first explosion slot and places an
 * explosion there at the first main hull mesh's center, with effect size the
 * type's max_bounds_extent / 64; it points g_cur_craft at the craft. */
static void check_forced_main_hull_explosion(void)
{
	static const int types[] = {
		MESH_COMPONENT_05_LASR_GUN,
		MESH_COMPONENT_01_MAIN_HULL,
		MESH_COMPONENT_01_MAIN_HULL,
	};
	fresh_world();
	set_model(VICTIM_TYPE, 3, types, 0);
	place_craft(VICTIM, VICTIM_TYPE);
	g_object_type_table[VICTIM_TYPE].max_bounds_extent = 6400;
	for (int obj = EXPLOSION_START; obj < EXPLOSION_END; ++obj) {
		g_test_objects[obj].object_type = 7;
	}
	g_cur_craft = NULL;
	craft_spawn_main_hull_explosion_effects(VICTIM, 1);
	XVT_ASSERT_TRUE(g_cur_craft == &g_test_craft[VICTIM]);
	struct object_record *blast = &g_test_objects[EXPLOSION_START];
	XVT_ASSERT_INT_EQ(blast->genus_id, CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_INT_EQ(blast->mobj->effect_size, 100);
	pai_rotate_local_vector_to_world_scratch(&g_test_objects[VICTIM], 101,
						 301, -201);
	XVT_ASSERT_INT_EQ(blast->world_x,
			  g_test_objects[VICTIM].world_x + g_rotated_x);
	XVT_ASSERT_INT_EQ(blast->world_z,
			  g_test_objects[VICTIM].world_z + g_rotated_z);
	XVT_ASSERT_INT_EQ(g_test_objects[EXPLOSION_START + 1].object_type, 7);
}

int main(int argc, char **argv)
{
	g_model_handle = memory_alloc_handle_zeroed(
		sizeof(struct optimized_poly_object), 0);
	XVT_ASSERT_TRUE(g_model_handle != 0);
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"ion_pulse_has_no_kind", check_ion_pulse_kind},
			{"corvette_bridge_past_95_percent",
			 check_bridge_corvette_past_95},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		return 0;
	}
	check_shield_bank_clamps();
	check_model_index_from_type();
	check_tech_stats_ratings();
	check_tech_stats_genus_scale();
	check_tech_stats_weapons();
	check_tech_stats_fixed_figures();
	check_tech_stats_no_model();
	check_turret_links_cleared();
	check_linked_objects_freed();
	check_warhead_kinds();
	check_selectable_meshes();
	check_component_small_hit();
	check_component_passes_damage();
	check_component_destroyed();
	check_component_destroyed_moves_aim();
	check_component_destroyed_in_proving_grounds();
	check_component_destroyed_explodes();
	check_last_generator_empties_shields();
	check_bridge_shielded();
	check_bridge_corvette_to_95();
	check_bridge_breaking_up_a_wing();
	check_explosion_at_mesh_center();
	check_forced_main_hull_explosion();
	return 0;
}
