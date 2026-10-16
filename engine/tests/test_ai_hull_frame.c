/* Tests for the frame a model sits in: the axes fview_calcrotatemove and
 * fview_calcrotateorient cache for an object from its yaw, pitch and roll,
 * and the world point flight_geometry_local_to_world gives a point of its
 * model. The pilot program turns world points into a craft's model frame
 * and back, so its answers have to come from this integer math and nowhere
 * else.
 *
 * Each case is a pose (yaw, pitch, roll, a world position away from the
 * origin) and a model box. A row of the answer file is one point of the box
 * in that pose: the box's eight corners and two points inside it, in the
 * model's own axes (x to the side, y backward, z up), the same point as the
 * engine takes it (side x, up z, forward -y), the pose's cached axes and
 * the world point, which is the position plus the turned local point.
 * Every value is an integer and is compared exactly.
 *
 * Run without arguments, the program reads the answer file named by
 * XVT_HULL_FRAME_FILE and asserts every row. Run as "test_ai_hull_frame
 * --write PATH", it writes the rows to PATH instead; that is how the
 * checked-in file is made. No game data is read. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"

#ifndef XVT_HULL_FRAME_FILE
#error "XVT_HULL_FRAME_FILE names the answer file"
#endif

enum {
	CASE_COUNT = 7,
	/* The box's eight corners and two points inside it. */
	POINTS_PER_CASE = 10,
	CORNERS = 8,
	ROW_COUNT = CASE_COUNT * POINTS_PER_CASE,
	/* The columns of a row: the case number, yaw, pitch, roll, the
	 * position, the side, up and forward axes, the model point, the local
	 * point and the world point. */
	COLUMN_COUNT = 25,
	LINE_BYTES = 512,
	/* The pitch of a level craft. */
	PITCH_LEVEL = 0x4000,
};

struct hull_case {
	uint16_t yaw;
	uint16_t pitch;
	uint16_t roll;
	int32_t position[3];
	int32_t box_min[3];
	int32_t box_max[3];
};

/* The cases, each away from the origin and with a yaw other than 0. */
static const struct hull_case g_cases[CASE_COUNT] = {
	/* A starfighter's box with a quarter-turn-ish roll to each side:
	 * pins the roll's sign. */
	{0x1234,
	 PITCH_LEVEL,
	 0x2000,
	 {123456, -78901, 23456},
	 {-20, -60, -10},
	 {20, 60, 10}},
	{0x1234,
	 PITCH_LEVEL,
	 0xC000,
	 {123456, -78901, 23456},
	 {-20, -60, -10},
	 {20, 60, 10}},
	/* Near straight up (pitch 0 is up) and near straight down. */
	{0x2345,
	 0x0100,
	 0,
	 {-40000, 55000, -9000},
	 {-20, -60, -10},
	 {20, 60, 10}},
	{0xA000, 0x7F00, 0, {7000, 8000, 90000}, {-20, -60, -10}, {20, 60, 10}},
	/* A super star destroyer's box: corners far from its centre. */
	{0x6000,
	 PITCH_LEVEL,
	 0x1000,
	 {200000, -150000, 30000},
	 {-40000, -162468, -20000},
	 {40000, 165215, 30000}},
	/* A box that is not centred on x, with all three angles turned. */
	{0x9000,
	 0x3800,
	 0x5555,
	 {310000, -220000, -5000},
	 {-3000, -500, -400},
	 {9000, 800, 600}},
	/* The destroyer's box again, pitched and rolled, with the points
	 * inside it that a craft's position can be made from. */
	{0xE000,
	 0x4800,
	 0xC000,
	 {-250000, 90000, -60000},
	 {-40000, -162468, -20000},
	 {40000, 165215, 30000}},
};

struct hull_row {
	int32_t value[COLUMN_COUNT];
};

static const char *const g_column_names[COLUMN_COUNT] = {
	"case",	      "yaw",	    "pitch",   "roll",	  "position_x",
	"position_y", "position_z", "side_x",  "side_y",  "side_z",
	"up_x",	      "up_y",	    "up_z",    "fwd_x",	  "fwd_y",
	"fwd_z",      "model_x",    "model_y", "model_z", "local_side",
	"local_up",   "local_fwd",  "world_x", "world_y", "world_z"};

static struct object_record g_test_object;
static struct mobile_object g_test_mobile;

/* The model point of a case: point 0 to 7 is a corner, bit 0 of the point
 * choosing the box's largest x, bit 1 its largest y and bit 2 its largest z;
 * point 8 is the point a quarter of the way from the largest corner to the
 * smallest, point 9 the one a quarter of the way the other way, both cut
 * toward zero. */
static void model_point(const struct hull_case *hull, int point,
			int32_t model[3])
{
	for (int axis = 0; axis < 3; ++axis) {
		int32_t smallest = hull->box_min[axis];
		int32_t largest = hull->box_max[axis];
		if (point < CORNERS) {
			model[axis] = (point >> axis) & 1 ? largest : smallest;
		} else if (point == CORNERS) {
			model[axis] = (smallest + 3 * largest) / 4;
		} else {
			model[axis] = (3 * smallest + largest) / 4;
		}
	}
}

/* Sets up the object of a case with its orientation marked dirty, so
 * flight_geometry_local_to_world rebuilds the axes the way the engine does,
 * and fills the row from the engine's answer. */
static void compute_row(int case_index, int point, struct hull_row *row)
{
	const struct hull_case *hull = &g_cases[case_index];
	int32_t model[3];
	model_point(hull, point, model);

	memset(&g_test_object, 0, sizeof g_test_object);
	memset(&g_test_mobile, 0, sizeof g_test_mobile);
	g_test_object.mobj = &g_test_mobile;
	g_test_object.world_x = hull->position[0];
	g_test_object.world_y = hull->position[1];
	g_test_object.world_z = hull->position[2];
	g_test_object.yaw = hull->yaw;
	g_test_object.pitch = hull->pitch;
	g_test_object.roll = hull->roll;
	g_test_mobile.orient_matrix_dirty = 1;

	/* The engine takes a model point's x, z and -y as side, up and
	 * forward. */
	int local_side = model[0];
	int local_up = model[2];
	int local_fwd = -model[1];
	flight_geometry_local_to_world(&g_test_object, local_side, local_up,
				       local_fwd);

	int32_t *value = row->value;
	value[0] = case_index;
	value[1] = hull->yaw;
	value[2] = hull->pitch;
	value[3] = hull->roll;
	value[4] = hull->position[0];
	value[5] = hull->position[1];
	value[6] = hull->position[2];
	value[7] = g_test_mobile.cached_side_x;
	value[8] = g_test_mobile.cached_side_y;
	value[9] = g_test_mobile.cached_side_z;
	value[10] = g_test_mobile.cached_up_x;
	value[11] = g_test_mobile.cached_up_y;
	value[12] = g_test_mobile.cached_up_z;
	value[13] = g_test_mobile.cached_fwd_x;
	value[14] = g_test_mobile.cached_fwd_y;
	value[15] = g_test_mobile.cached_fwd_z;
	value[16] = model[0];
	value[17] = model[1];
	value[18] = model[2];
	value[19] = local_side;
	value[20] = local_up;
	value[21] = local_fwd;
	value[22] = hull->position[0] + g_rotated_x;
	value[23] = hull->position[1] + g_rotated_y;
	value[24] = hull->position[2] + g_rotated_z;
}

static void write_rows(const char *path)
{
	FILE *file = fopen(path, "w");
	XVT_ASSERT_TRUE(file != NULL);
	fprintf(file,
		"# Made by the engine: build the test (cmake --build BUILD --target test_ai_hull_frame), then run BUILD/test_ai_hull_frame --write ai/tests/data/hull_frame.txt. One row is one point of a model box in one pose; every value is an integer.\n");
	fprintf(file, "#");
	for (int column = 0; column < COLUMN_COUNT; ++column) {
		fprintf(file, " %s", g_column_names[column]);
	}
	fprintf(file, "\n");
	for (int case_index = 0; case_index < CASE_COUNT; ++case_index) {
		for (int point = 0; point < POINTS_PER_CASE; ++point) {
			struct hull_row row;
			compute_row(case_index, point, &row);
			for (int column = 0; column < COLUMN_COUNT; ++column) {
				fprintf(file, column == 0 ? "%d" : " %d",
					(int)row.value[column]);
			}
			fprintf(file, "\n");
		}
	}
	XVT_ASSERT_TRUE(fclose(file) == 0);
}

/* Reads each row of the answer file and checks the engine gives it again,
 * value by value. A row the file lacks, one it has too many of and any
 * differing value fail the check. */
static void check_rows_match_file(void)
{
	FILE *file = fopen(XVT_HULL_FRAME_FILE, "r");
	if (file == NULL) {
		fprintf(stderr, "cannot open the answer file %s\n",
			XVT_HULL_FRAME_FILE);
	}
	XVT_ASSERT_TRUE(file != NULL);
	char line[LINE_BYTES];
	int rows_read = 0;
	while (fgets(line, sizeof line, file) != NULL) {
		if (line[0] == '#' || line[0] == '\n') {
			continue;
		}
		XVT_ASSERT_TRUE(rows_read < ROW_COUNT);
		struct hull_row expected;
		char *at = line;
		for (int column = 0; column < COLUMN_COUNT; ++column) {
			char *end;
			expected.value[column] = (int32_t)strtol(at, &end, 10);
			XVT_ASSERT_TRUE(end != at);
			at = end;
		}
		struct hull_row found;
		compute_row(rows_read / POINTS_PER_CASE,
			    rows_read % POINTS_PER_CASE, &found);
		for (int column = 0; column < COLUMN_COUNT; ++column) {
			if (found.value[column] != expected.value[column]) {
				fprintf(stderr,
					"row %d, column %s: file %d, engine %d\n",
					rows_read, g_column_names[column],
					(int)expected.value[column],
					(int)found.value[column]);
			}
			XVT_ASSERT_INT_EQ(found.value[column],
					  expected.value[column]);
		}
		++rows_read;
	}
	fclose(file);
	XVT_ASSERT_INT_EQ(rows_read, ROW_COUNT);
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--write") == 0) {
		write_rows(argv[2]);
		return 0;
	}
	check_rows_match_file();
	return 0;
}
