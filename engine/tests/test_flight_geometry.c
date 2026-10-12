/* Tests for xvt/flight/flight_geometry.c: the rough distance and the direction
 * between two objects or mission points, and an object's local axes turned
 * into world axes. Each check builds the objects it needs in an object table
 * this file owns. The rotating object's axes are set by hand, each component
 * exactly -1, so every product is exact, and its orientation is marked clean,
 * so the cached axes are used as they are. No game data is read. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/trig2.h"

enum {
	TEST_SLOTS = 2,
	MINUS_ONE_Q15 = -32768,
	/* The first object sits off the axes. */
	FROM_X = 5000,
	FROM_Y = -3000,
	FROM_Z = 2000,
	/* A reference at or above 0x8000 names a mission point of the flight
	 * group: point 3 here. */
	POINT_REF = 0x8000 + 3,
};

static struct object_record g_test_objects[TEST_SLOTS];
static struct mobile_object g_test_mobiles[TEST_SLOTS];

/* Object 0 sits at (FROM_X, FROM_Y, FROM_Z); object 1 sits the given offset
 * away from it. Object 0's side axis points down world -X, its up axis down
 * -Z and its forward axis down -Y. */
static void place_objects(int x, int y, int z)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	g_object_table = g_test_objects;
	for (int i = 0; i < TEST_SLOTS; ++i) {
		g_test_objects[i].mobj = &g_test_mobiles[i];
	}
	g_test_objects[0].world_x = FROM_X;
	g_test_objects[0].world_y = FROM_Y;
	g_test_objects[0].world_z = FROM_Z;
	g_test_objects[1].world_x = FROM_X + x;
	g_test_objects[1].world_y = FROM_Y + y;
	g_test_objects[1].world_z = FROM_Z + z;
	g_test_mobiles[0].cached_side_x = MINUS_ONE_Q15;
	g_test_mobiles[0].cached_up_z = MINUS_ONE_Q15;
	g_test_mobiles[0].cached_fwd_y = MINUS_ONE_Q15;
}

/* The rough distance takes the larger of the X and Y offsets plus half the
 * smaller; when that is larger than the Z offset it adds half the Z offset,
 * else it halves itself and adds the Z offset. Signs do not matter, and the
 * order of the two references does not either. */
static void check_rough_distance(void)
{
	place_objects(-1000, 400, 300);
	pai_object_ref_update_rough_distance(0, 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 1000 + 200 + 150);
	pai_object_ref_update_rough_distance(1, 0);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 1350);
	/* It leaves the world position at the second reference. */
	XVT_ASSERT_INT_EQ(g_world_loc_x, FROM_X);
	XVT_ASSERT_INT_EQ(g_world_loc_y, FROM_Y);
	XVT_ASSERT_INT_EQ(g_world_loc_z, FROM_Z);

	place_objects(400, -1000, 300);
	pai_object_ref_update_rough_distance(0, 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 1350);

	/* Z larger than the X and Y sum: half the sum plus Z. */
	place_objects(400, 100, -3000);
	pai_object_ref_update_rough_distance(0, 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, (400 + 50) / 2 + 3000);

	/* X equal to Y counts Y as the larger. */
	place_objects(500, -500, 0);
	pai_object_ref_update_rough_distance(0, 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 500 + 250);
}

/* A mission point of flight group 0 stands at its stored point times 256,
 * with Y negated. */
static void check_rough_distance_to_mission_point(void)
{
	place_objects(0, 0, 0);
	g_mission_flight_groups[0].fg.mission_point_x[3] = 30;
	g_mission_flight_groups[0].fg.mission_point_y[3] = 20;
	g_mission_flight_groups[0].fg.mission_point_z[3] = 10;
	pai_object_ref_update_rough_distance(0, POINT_REF);
	int dx = 30 * 256 - FROM_X;    /* 2,680 */
	int dy = -(20 * 256) - FROM_Y; /* -2,120, so 2,120 */
	int dz = 10 * 256 - FROM_Z;    /* 560 */
	XVT_ASSERT_INT_EQ(g_last_rough_distance, dx + (-dy >> 1) + (dz >> 1));
}

/* The direction is the one trig2_ctop gives for the second reference's
 * position minus the first's, and the first reference's position is left in
 * g_world_loc_x, g_world_loc_y and g_world_loc_z. */
static void check_direction_between_refs(void)
{
	place_objects(-700, 2500, 900);
	trig2_ctop(700, -2500, -900);
	uint16_t backward_xyangle = trig2_xyangle;
	trig2_ctop(-700, 2500, 900);
	uint16_t xyangle = trig2_xyangle;
	uint16_t pitch = trig2_pitch;
	int distance = trig2_polardistance;
	XVT_ASSERT_TRUE(xyangle != backward_xyangle);

	trig2_xyangle = 0;
	trig2_pitch = 0;
	trig2_polardistance = 0;
	pai_object_ref_direction_to_object_ref(0, 1);
	XVT_ASSERT_INT_EQ(trig2_xyangle, xyangle);
	XVT_ASSERT_INT_EQ(trig2_pitch, pitch);
	XVT_ASSERT_INT_EQ(trig2_polardistance, distance);
	XVT_ASSERT_INT_EQ(g_world_loc_x, FROM_X);
	XVT_ASSERT_INT_EQ(g_world_loc_y, FROM_Y);
	XVT_ASSERT_INT_EQ(g_world_loc_z, FROM_Z);
}

/* Side, up and forward go through the object's side, up and forward axes:
 * with those along world -X, -Z and -Y, (side, up, forward) becomes
 * (-side, -forward, -up). */
static void check_local_to_world(void)
{
	place_objects(0, 0, 0);
	pai_calcrotatedpoint(&g_test_objects[0], 100, 200, 300);
	XVT_ASSERT_INT_EQ(g_rotated_x, -100);
	XVT_ASSERT_INT_EQ(g_rotated_y, -300);
	XVT_ASSERT_INT_EQ(g_rotated_z, -200);

	pai_rotate_local_vector_to_world_scratch(&g_test_objects[0], 100, 200,
						 300);
	XVT_ASSERT_INT_EQ(g_rotated_x, -100);
	XVT_ASSERT_INT_EQ(g_rotated_y, -300);
	XVT_ASSERT_INT_EQ(g_rotated_z, -200);

	/* The int form keeps a vector longer than 16 bits. */
	pai_rotate_local_vector_to_world_scratch(&g_test_objects[0], 100000, 0,
						 -70000);
	XVT_ASSERT_INT_EQ(g_rotated_x, -100000);
	XVT_ASSERT_INT_EQ(g_rotated_y, 70000);
	XVT_ASSERT_INT_EQ(g_rotated_z, 0);
}

int main(void)
{
	check_rough_distance();
	check_rough_distance_to_mission_point();
	check_direction_between_refs();
	check_local_to_world();
	return 0;
}
