/* Tests for xvt/flight/ai_formation.c, a computer craft's formation place in
 * its view and the formation books its activity keeps, on worlds this file
 * builds: CRAFT_SLOTS craft, a leader and its followers, with one model
 * whose bound sizes are set by hand. No game data is read. The expected
 * places are the deleted 1997 paiman_calcformation's answers for the same
 * worlds (its push plus the follower's position), run against this engine's
 * tables and rotation; the places of 309,180 random and grid worlds were
 * found equal the same way when the port was written. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_formation.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/ai/ai_wire.h"

enum {
	CRAFT_SLOTS = 6,
	/* Slot 0 stays empty: an active region need not start at 0. */
	LEADER = 1,
	FOLLOWER = 2,
	SECOND_FOLLOWER = 3,
	/* A craft of another flight group. */
	STRANGER = 4,
	/* A craft of the leader's group outside the active region. */
	OUTSIDER = 5,
	GROUP = 2,
	OTHER_GROUP = 3,
	MODEL = 7,
	/* A byte no field of a fresh view holds. */
	STALE = 0x5A,
};

/* One slot past the craft slots is a static's. */
static struct object_record g_test_objects[CRAFT_SLOTS + 1];
static struct mobile_object g_test_mobiles[CRAFT_SLOTS + 1];
static struct craft_data g_test_craft[CRAFT_SLOTS + 1];

/* A world's leader and follower, as the 1997 answers were asked about. */
struct place_world {
	int formation;
	int ordinal;
	int separation;
	int shift;
	int bound_x;
	int bound_y;
	int bound_z;
	int32_t leader_x;
	int32_t leader_y;
	int32_t leader_z;
	uint16_t yaw;
	uint16_t pitch;
	uint16_t roll;
};

/* Craft LEADER to OUTSIDER live in their slots, all of GROUP but STRANGER,
 * every follower following LEADER; the active region is slots 1 to 4. Each
 * craft flies model MODEL, whose sizes are 0. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(&g_model_defs[MODEL], 0, sizeof g_model_defs[MODEL]);
	memset(g_mission_flight_groups, 0,
	       (OTHER_GROUP + 1) * sizeof g_mission_flight_groups[0]);
	g_object_table = g_test_objects;
	for (int slot = LEADER; slot < CRAFT_SLOTS; ++slot) {
		g_test_objects[slot].object_type = 5;
		g_test_objects[slot].flight_group_idx = GROUP;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		g_test_craft[slot].model_index = MODEL;
		g_test_craft[slot].leader_obj_idx = LEADER;
	}
	g_test_objects[STRANGER].flight_group_idx = OTHER_GROUP;
	g_test_craft[LEADER].leader_obj_idx = UINT8_MAX;
	g_active_region_object_slot_start = LEADER;
	g_active_region_craft_object_slot_end = OUTSIDER;
	g_region_main_object_slot_end = CRAFT_SLOTS;
	g_region_static_object_slot_count = 0;
	g_flight_sim_side_effects_suppressed = 0;
}

/* Sets up the leader and FOLLOWER as world describes them. */
static void set_place_world(const struct place_world *world)
{
	struct object_record *leader = &g_test_objects[LEADER];
	struct craft_data *follower = &g_test_craft[FOLLOWER];
	leader->world_x = world->leader_x;
	leader->world_y = world->leader_y;
	leader->world_z = world->leader_z;
	leader->yaw = world->yaw;
	leader->pitch = world->pitch;
	leader->roll = world->roll;
	g_test_mobiles[LEADER].orient_matrix_dirty = 1;
	g_test_craft[LEADER].ai_flight.separation = (uint8_t)world->separation;
	follower->ai_flight.formation_type = (uint8_t)world->formation;
	follower->craft_ordinal = (uint8_t)world->ordinal;
	g_model_defs[MODEL].bound_size_x = (int16_t)world->bound_x;
	g_model_defs[MODEL].bound_size_y = (int16_t)world->bound_y;
	g_model_defs[MODEL].bound_size_z = (int16_t)world->bound_z;
	g_model_defs[MODEL].bound_size_shift = (uint16_t)world->shift;
	/* The follower stands anywhere: its place does not depend on it. */
	g_test_objects[FOLLOWER].world_x = 9;
	g_test_objects[FOLLOWER].world_y = -9;
	g_test_objects[FOLLOWER].world_z = 9;
}

/* The view of object_index, written over a view of stale bytes. */
static struct ai_wire_craft_view view_of(uint16_t object_index)
{
	struct ai_wire_craft_view view;
	memset(&view, STALE, sizeof view);
	ai_formation_view_craft(object_index, &view);
	return view;
}

static void assert_place(const struct ai_wire_craft_view *view, int32_t x,
			 int32_t y, int32_t z)
{
	XVT_ASSERT_INT_EQ(view->books & AI_WIRE_BOOKS_PLACE,
			  AI_WIRE_BOOKS_PLACE);
	XVT_ASSERT_INT_EQ(view->place[0], x);
	XVT_ASSERT_INT_EQ(view->place[1], y);
	XVT_ASSERT_INT_EQ(view->place[2], z);
}

static void assert_no_place(const struct ai_wire_craft_view *view)
{
	XVT_ASSERT_INT_EQ(view->books & AI_WIRE_BOOKS_PLACE, 0);
	XVT_ASSERT_INT_EQ(view->place[0], 0);
	XVT_ASSERT_INT_EQ(view->place[1], 0);
	XVT_ASSERT_INT_EQ(view->place[2], 0);
}

/* A follower's place is 1997's for the same world: by its formation and
 * ordinal, its leader's separation, its model's sizes, the leader's axes
 * and the model's shift; the view's other books bits stay as they were. */
static void check_follower_place_is_1997s(void)
{
	static const struct {
		struct place_world world;
		int32_t place[3];
	} cases[] = {
		/* Formation 0, place 1, separation 2. */
		{{0, 1, 2, 0, 100, 200, 50, 1000, 2000, 3000, 0, 0, 0},
		 {1299, 2000, 2400}},
		/* Separation 0: half a bound more on each axis. */
		{{0, 1, 0, 0, 100, 200, 50, 1000, 2000, 3000, 0, 0, 0},
		 {1149, 2000, 2700}},
		/* The leader a quarter turn round. */
		{{0, 1, 2, 0, 100, 200, 50, 1000, 2000, 3000, 0x4000, 0, 0},
		 {1000, 1700, 2400}},
		/* Formation 1, whose offsets are divided by 3, the leader
		 * turned on every axis. */
		{{1, 2, 2, 0, 90, 60, 30, -500, 700, -900, 0x2000, 0x1000,
		  0x0800},
		 {-612, 829, -1037}},
		/* A model whose sizes are shifted by 2. */
		{{5, 4, 1, 2, 300, 250, 120, 40000, -60000, 5000, 0x9000,
		  0xF000, 0x3000},
		 {42880, -71908, 436}},
		/* Separation 255: each offset wraps in 16 bits, as in 1997. */
		{{5, 4, 255, 0, 0x280, 0x27F, 0x13, 0, 0, 0, 0, 0, 0},
		 {0, -19455, 1023}},
		/* Separation 199 in formation 1: the side offset wraps in 16 bits
		 * before the division by 3, as in 1997. */
		{{1, 3, 199, 0, 100, 60, 30, 0, 0, 0, 0, 0, 0},
		 {1844, 3999, 9844}},
	};
	for (size_t n = 0; n < sizeof cases / sizeof cases[0]; ++n) {
		fresh_world();
		set_place_world(&cases[n].world);
		struct ai_wire_craft_view view;
		memset(&view, STALE, sizeof view);
		view.books = AI_WIRE_BOOKS_HOME_POINT;
		ai_formation_view_craft(FOLLOWER, &view);
		assert_place(&view, cases[n].place[0], cases[n].place[1],
			     cases[n].place[2]);
		XVT_ASSERT_INT_EQ(view.books, AI_WIRE_BOOKS_HOME_POINT |
						      AI_WIRE_BOOKS_PLACE);
	}
}

/* A leader, and a craft alone, have no place, whatever the view held: a
 * place bit already set is cleared, the other books bits stay. */
static void check_leader_has_no_place(void)
{
	fresh_world();
	g_model_defs[MODEL].bound_size_x = 100;
	struct ai_wire_craft_view view;
	memset(&view, STALE, sizeof view);
	view.books = UINT32_MAX;
	ai_formation_view_craft(LEADER, &view);
	assert_no_place(&view);
	/* The books are 32 bits: bits above 16 (all launched, captured, the
	 * three Drop Off books) stay as they were. */
	XVT_ASSERT_INT_EQ(view.books,
			  UINT32_MAX & ~(uint32_t)AI_WIRE_BOOKS_PLACE);
	view.books = UINT32_MAX;
	ai_formation_view_escort(LEADER, &view);
	XVT_ASSERT_INT_EQ(view.books,
			  UINT32_MAX & ~(uint32_t)AI_WIRE_BOOKS_STATION);
}

/* A follower whose leader's slot is empty, or whose formation or ordinal
 * lies outside the tables, has no place. */
static void check_no_place_off_the_tables(void)
{
	fresh_world();
	g_test_objects[LEADER].object_type = 0;
	struct ai_wire_craft_view view = view_of(FOLLOWER);
	assert_no_place(&view);

	fresh_world();
	g_test_craft[FOLLOWER].ai_flight.formation_type = 34;
	view = view_of(FOLLOWER);
	assert_no_place(&view);
	g_test_craft[FOLLOWER].ai_flight.formation_type = 33;
	view = view_of(FOLLOWER);
	XVT_ASSERT_INT_EQ(view.books & AI_WIRE_BOOKS_PLACE,
			  AI_WIRE_BOOKS_PLACE);

	fresh_world();
	g_test_craft[FOLLOWER].craft_ordinal = 6;
	view = view_of(FOLLOWER);
	assert_no_place(&view);
	g_test_craft[FOLLOWER].craft_ordinal = 5;
	view = view_of(FOLLOWER);
	XVT_ASSERT_INT_EQ(view.books & AI_WIRE_BOOKS_PLACE,
			  AI_WIRE_BOOKS_PLACE);
}

/* Leaving a hangar ends: every craft of the group in the active region
 * takes the mission's formation and spacing; another group's craft and
 * the group's craft outside the region keep theirs. */
static void check_hangar_exit_sets_group_formation(void)
{
	fresh_world();
	g_mission_flight_groups[GROUP].fg.formation = 12;
	g_mission_flight_groups[GROUP].fg.formation_spacing = 3;
	for (int slot = LEADER; slot < CRAFT_SLOTS; ++slot) {
		g_test_craft[slot].ai_flight.formation_type = 9;
		g_test_craft[slot].ai_flight.separation = 0;
	}

	ai_formation_activity_changed(
		SECOND_FOLLOWER, AI_ACTIVITY_LEAVING_HANGAR, AI_ACTIVITY_NONE);

	for (int slot = LEADER; slot <= SECOND_FOLLOWER; ++slot) {
		XVT_ASSERT_INT_EQ(g_test_craft[slot].ai_flight.formation_type,
				  12);
		XVT_ASSERT_INT_EQ(g_test_craft[slot].ai_flight.separation, 3);
	}
	XVT_ASSERT_INT_EQ(g_test_craft[STRANGER].ai_flight.formation_type, 9);
	XVT_ASSERT_INT_EQ(g_test_craft[STRANGER].ai_flight.separation, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[OUTSIDER].ai_flight.formation_type, 9);
	XVT_ASSERT_INT_EQ(g_test_craft[OUTSIDER].ai_flight.separation, 0);
}

/* Any other change leaves the formations be: a craft not leaving a
 * hangar, or one whose activity stays leaving a hangar. */
static void check_other_changes_keep_formation(void)
{
	fresh_world();
	g_mission_flight_groups[GROUP].fg.formation = 12;
	g_test_craft[LEADER].ai_flight.formation_type = 9;

	ai_formation_activity_changed(LEADER, AI_ACTIVITY_NONE,
				      AI_ACTIVITY_WAITING);
	ai_formation_activity_changed(LEADER, AI_ACTIVITY_LEAVING_HANGAR,
				      AI_ACTIVITY_LEAVING_HANGAR);

	XVT_ASSERT_INT_EQ(g_test_craft[LEADER].ai_flight.formation_type, 9);
}

/* Going home, evading home or entering a hangar closes the craft's
 * spacing to 1; other activities leave it. */
static void check_going_home_closes_spacing(void)
{
	static const uint8_t closing[] = {AI_ACTIVITY_GOING_HOME,
					  AI_ACTIVITY_GOING_HOME_EVADING,
					  AI_ACTIVITY_ENTERING_HANGAR};
	for (size_t n = 0; n < sizeof closing; ++n) {
		fresh_world();
		g_test_craft[LEADER].ai_flight.separation = 4;
		ai_formation_activity_changed(LEADER, AI_ACTIVITY_NONE,
					      closing[n]);
		XVT_ASSERT_INT_EQ(g_test_craft[LEADER].ai_flight.separation, 1);
		XVT_ASSERT_INT_EQ(g_test_craft[FOLLOWER].ai_flight.separation,
				  0);
	}
	fresh_world();
	g_test_craft[LEADER].ai_flight.separation = 4;
	ai_formation_activity_changed(LEADER, AI_ACTIVITY_NONE,
				      AI_ACTIVITY_WAITING);
	XVT_ASSERT_INT_EQ(g_test_craft[LEADER].ai_flight.separation, 4);
}

/* The closed spacing reaches the followers' places: separation 1 puts
 * formation 0's place 1 two bounds out, not five. */
static void check_closed_spacing_moves_place(void)
{
	fresh_world();
	g_model_defs[MODEL].bound_size_x = 100;
	g_test_mobiles[LEADER].orient_matrix_dirty = 1;
	g_test_craft[FOLLOWER].craft_ordinal = 1;
	g_test_craft[LEADER].ai_flight.separation = 4;
	struct ai_wire_craft_view wide = view_of(FOLLOWER);

	ai_formation_activity_changed(LEADER, AI_ACTIVITY_NONE,
				      AI_ACTIVITY_GOING_HOME);
	struct ai_wire_craft_view close = view_of(FOLLOWER);

	XVT_ASSERT_INT_EQ(wide.place[0], 499);
	XVT_ASSERT_INT_EQ(close.place[0], 199);
}

enum {
	/* The escort tests: the escort is LEADER, of flight group GROUP; the
	 * groups it may escort are NEAR_GROUP, FAR_GROUP and EMPTY_GROUP, which
	 * has no craft. */
	NEAR_GROUP = OTHER_GROUP,
	FAR_GROUP = 4,
	EMPTY_GROUP = 5,
	GROUP_COUNT = 6,
	STATIC_SLOT = CRAFT_SLOTS,
	STATIC_TYPE = 9,
	TEST_TYPE = 5,
	BIG_EXTENT = 3000,
};

/* The escort's world: LEADER at 0, 0, 0 on an Escort order whose targets are
 * flight group NEAR_GROUP or FAR_GROUP, variable1 as given. FOLLOWER (x
 * 1,000) is of NEAR_GROUP and follows STRANGER; STRANGER (50,000, -4,000,
 * 700) is of NEAR_GROUP and leads; SECOND_FOLLOWER (x 90,000) is of FAR_GROUP
 * and leads. STRANGER's axes turn an offset of 3,072 to a half, a quarter and
 * an eighth of it: 1,536 on x, 768 on y and 384 on z. */
static void escort_world(int variable1)
{
	fresh_world();
	memset(g_mission_flight_groups, 0,
	       GROUP_COUNT * sizeof g_mission_flight_groups[0]);
	g_mission_header.num_flight_groups = GROUP_COUNT;
	struct mission_order *order =
		&g_mission_flight_groups[GROUP].fg.orders[0];
	order->order = MISSION_ORDER_ESCORT;
	order->variable1 = (uint8_t)variable1;
	order->target1_type = 1;
	order->target1 = NEAR_GROUP;
	order->target1_or_target2 = 1;
	order->target2_type = 1;
	order->target2 = FAR_GROUP;
	g_test_craft[LEADER].leader_obj_idx = UINT8_MAX;
	g_test_objects[FOLLOWER].flight_group_idx = NEAR_GROUP;
	g_test_craft[FOLLOWER].leader_obj_idx = STRANGER;
	g_test_objects[FOLLOWER].world_x = 1000;
	g_test_objects[STRANGER].flight_group_idx = NEAR_GROUP;
	g_test_objects[STRANGER].world_x = 50000;
	g_test_objects[STRANGER].world_y = -4000;
	g_test_objects[STRANGER].world_z = 700;
	g_test_craft[STRANGER].leader_obj_idx = UINT8_MAX;
	g_test_objects[SECOND_FOLLOWER].flight_group_idx = FAR_GROUP;
	g_test_objects[SECOND_FOLLOWER].world_x = 90000;
	g_test_craft[SECOND_FOLLOWER].leader_obj_idx = UINT8_MAX;
	g_test_mobiles[STRANGER].orient_matrix_dirty = 0;
	g_test_mobiles[STRANGER].cached_side_x = 0x4000;
	g_test_mobiles[STRANGER].cached_up_y = 0x2000;
	g_test_mobiles[STRANGER].cached_fwd_z = 0x1000;
	g_object_type_table[TEST_TYPE].max_bounds_extent = BIG_EXTENT - 1;
}

/* The escort's view, over stale bytes. */
static struct ai_wire_craft_view escort_view(void)
{
	struct ai_wire_craft_view view;
	memset(&view, STALE, sizeof view);
	ai_formation_view_escort(LEADER, &view);
	return view;
}

static void assert_no_station(const struct ai_wire_craft_view *view)
{
	XVT_ASSERT_INT_EQ(view->escorted, UINT16_MAX);
	XVT_ASSERT_INT_EQ(view->books & AI_WIRE_BOOKS_STATION, 0);
	for (int axis = 0; axis < 3; ++axis) {
		XVT_ASSERT_INT_EQ(view->station[axis], 0);
	}
}

static void assert_station(const struct ai_wire_craft_view *view, int escorted,
			   int32_t x, int32_t y, int32_t z)
{
	XVT_ASSERT_INT_EQ(view->escorted, escorted);
	XVT_ASSERT_INT_EQ(view->books & AI_WIRE_BOOKS_STATION,
			  AI_WIRE_BOOKS_STATION);
	XVT_ASSERT_INT_EQ(view->station[0], x);
	XVT_ASSERT_INT_EQ(view->station[1], y);
	XVT_ASSERT_INT_EQ(view->station[2], z);
}

/* The craft escorted is the first with no leader in the flight group of the
 * nearest object the order names: not the nearest object itself when it
 * follows, not the farther group's; a static is an object too, and the
 * second target pair is used only when the first names nothing. */
static void check_escorted_pick(void)
{
	escort_world(27);
	/* FOLLOWER is the nearest object; its group's first craft with no
	 * leader is STRANGER, though SECOND_FOLLOWER leads and is later. */
	XVT_ASSERT_INT_EQ(escort_view().escorted, STRANGER);
	/* The other group's craft is escorted once it is the nearest. */
	g_test_objects[SECOND_FOLLOWER].world_x = 10;
	XVT_ASSERT_INT_EQ(escort_view().escorted, SECOND_FOLLOWER);
	g_test_objects[SECOND_FOLLOWER].world_x = 90000;
	/* Of two equally near objects the first is taken. */
	g_test_objects[SECOND_FOLLOWER].world_x = 1000;
	XVT_ASSERT_INT_EQ(escort_view().escorted, STRANGER);
	g_test_objects[SECOND_FOLLOWER].world_x = 90000;
	/* A static nearer than any craft names its group, which has no craft:
	 * none is escorted, and no station is given. */
	struct mission_order *order =
		&g_mission_flight_groups[GROUP].fg.orders[0];
	order->target2 = EMPTY_GROUP;
	g_region_static_object_slot_count = 1;
	g_test_objects[STATIC_SLOT].object_type = STATIC_TYPE;
	g_test_objects[STATIC_SLOT].flight_group_idx = EMPTY_GROUP;
	g_test_objects[STATIC_SLOT].world_x = 500;
	struct ai_wire_craft_view view = escort_view();
	assert_no_station(&view);
	/* Farther than the craft, the static is passed over. */
	g_test_objects[STATIC_SLOT].world_x = 5000000;
	XVT_ASSERT_INT_EQ(escort_view().escorted, STRANGER);
	/* The second pair is used when the first names no object. */
	g_test_objects[STATIC_SLOT].object_type = 0;
	order->target1 = EMPTY_GROUP;
	order->target2_type = 0;
	order->secondary_target_types[0] = 1;
	order->secondary_targets[0] = NEAR_GROUP;
	order->target3_or_target4 = 1;
	XVT_ASSERT_INT_EQ(escort_view().escorted, STRANGER);
	/* The first pair wins when it names an object, however far. */
	order->target1 = FAR_GROUP;
	XVT_ASSERT_INT_EQ(escort_view().escorted, SECOND_FOLLOWER);
}

/* The station is the escorted craft's position plus the offset the order's
 * first variable picks along its axes: 27 is on it, 0, 14 and 26 are a
 * corner, a face and the opposite corner; sixteen times as far for 3,000 or
 * more of size, not for 2,999. */
static void check_station_offsets(void)
{
	escort_world(27);
	struct ai_wire_craft_view view = escort_view();
	assert_station(&view, STRANGER, 50000, -4000, 700);
	escort_world(0);
	view = escort_view();
	assert_station(&view, STRANGER, 50000 - 1536, -4000 + 768, 700 + 384);
	escort_world(14);
	view = escort_view();
	assert_station(&view, STRANGER, 50000 + 1536, -4000, 700);
	escort_world(26);
	view = escort_view();
	assert_station(&view, STRANGER, 50000 + 1536, -4000 - 768, 700 - 384);
	escort_world(0);
	g_object_type_table[TEST_TYPE].max_bounds_extent = BIG_EXTENT;
	view = escort_view();
	assert_station(&view, STRANGER, 50000 - 24576, -4000 + 12288,
		       700 + 6144);
	g_object_type_table[TEST_TYPE].max_bounds_extent = BIG_EXTENT - 1;
}

/* A first variable of 27 is the last station and gives one; 28 and more read
 * past 1997's tables and give none, the escorted craft with it. */
static void check_station_variable_limit(void)
{
	escort_world(27);
	struct ai_wire_craft_view view = escort_view();
	XVT_ASSERT_INT_EQ(view.books & AI_WIRE_BOOKS_STATION,
			  AI_WIRE_BOOKS_STATION);
	escort_world(28);
	view = escort_view();
	assert_no_station(&view);
	escort_world(255);
	view = escort_view();
	assert_no_station(&view);
}

/* No station off an Escort order, and none for a follower, which flies its
 * formation place; the stale station bit is cleared either way. */
static void check_station_only_for_escorting_leaders(void)
{
	escort_world(27);
	g_mission_flight_groups[GROUP].fg.orders[0].order =
		MISSION_ORDER_ATTACK;
	struct ai_wire_craft_view view = escort_view();
	assert_no_station(&view);
	escort_world(27);
	g_test_craft[LEADER].leader_obj_idx = SECOND_FOLLOWER;
	view = escort_view();
	assert_no_station(&view);
	/* The current order slot, not slot 0, decides. */
	escort_world(27);
	g_mission_flight_groups[GROUP].fg.orders[1] =
		g_mission_flight_groups[GROUP].fg.orders[0];
	g_mission_flight_groups[GROUP].fg.orders[0].order =
		MISSION_ORDER_ATTACK;
	XVT_ASSERT_INT_EQ(escort_view().escorted, UINT16_MAX);
	g_test_craft[LEADER].ai_controller.current_order_slot = 1;
	XVT_ASSERT_INT_EQ(escort_view().escorted, STRANGER);
}

int main(void)
{
	check_follower_place_is_1997s();
	check_leader_has_no_place();
	check_no_place_off_the_tables();
	check_hangar_exit_sets_group_formation();
	check_other_changes_keep_formation();
	check_going_home_closes_spacing();
	check_closed_spacing_moves_place();
	check_escorted_pick();
	check_station_offsets();
	check_station_variable_limit();
	check_station_only_for_escorting_leaders();
	return 0;
}
