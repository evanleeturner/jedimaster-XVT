/* Tests for xvt/flight/mission/goals.c, the lines of the goals page. Each
 * check draws into this file's own character catcher, installed as the
 * flight's draw-character function, and reads back what was drawn. The font
 * is a table of this file's in which every glyph is one pixel wide, and the
 * text tables hold short marker strings set here: a condition text names its
 * row and column, "r2c5". The flight groups and the model names are set field
 * by field in the game's own tables; no game data is read. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/render/renderer.h"

enum {
	TEST_LINE_HEIGHT = 8,
	/* A clip edge far enough right that nothing drawn here needs a new
	 * line. */
	TEST_WIDE_CLIP = 600,
	/* A clip edge at which a string more than 5 pixels wide, so 5
	 * characters here, needs a new line: the limit is the edge less 11. */
	TEST_NARROW_CLIP = 16,
	TEST_ROWS = 188,
	TEST_COLUMNS = 14,
	TEST_CONDITIONS = 47,
};

static char g_drawn[512];
static int g_drawn_length;
static uint8_t g_glyph_widths[224];
static char g_condition_text[TEST_ROWS][TEST_COLUMNS][12];

/* Keeps every character the goals code draws, in order. */
static void catch_char(uint8_t ch)
{
	XVT_ASSERT_TRUE(g_drawn_length + 1 < (int)sizeof g_drawn);
	g_drawn[g_drawn_length] = (char)ch;
	++g_drawn_length;
	g_drawn[g_drawn_length] = '\0';
}

/* Nothing drawn yet, a one-pixel font of line height 8, the cursor at the
 * left edge and the wide clip; every text table empty, every flight group
 * and its statistics cleared, and the goal line's joining words set: "group "
 * after a group of several craft, ", " and "and " between names, and
 * "under" before a time limit. */
static void fresh_page(void)
{
	memset(g_drawn, 0, sizeof g_drawn);
	g_drawn_length = 0;
	memset(g_glyph_widths, 1, sizeof g_glyph_widths);
	g_flight_draw_char_fn = catch_char;
	g_flight_font_glyph_table_sw = g_glyph_widths;
	g_flight_font_glyph_stride_sw = 1;
	g_flight_font_tier = 0;
	g_flight_font_line_height = TEST_LINE_HEIGHT;
	g_flight_word_wrap_enabled = 0;
	g_flight_cursor_x = 0;
	g_flight_clip_right = TEST_WIDE_CLIP;
	memset(g_str_goal_cond_masculine, 0, sizeof g_str_goal_cond_masculine);
	memset(g_str_goal_cond_feminine, 0, sizeof g_str_goal_cond_feminine);
	memset(g_str_goal_cond_neutered, 0, sizeof g_str_goal_cond_neutered);
	memset(g_craft_gender, 0, sizeof g_craft_gender);
	memset(g_str_species_names_plural, 0,
	       sizeof g_str_species_names_plural);
	memset(g_str_sat_mine_probe_buoy_pilot_names, 0,
	       sizeof g_str_sat_mine_probe_buoy_pilot_names);
	memset(g_str_goal_genus_names, 0, sizeof g_str_goal_genus_names);
	memset(g_str_goal_family_names, 0, sizeof g_str_goal_family_names);
	memset(g_str_goal_sides, 0, sizeof g_str_goal_sides);
	memset(g_str_goal_escape, 0, sizeof g_str_goal_escape);
	memset(g_str_goal_conjunctions, 0, sizeof g_str_goal_conjunctions);
	memset(g_model_defs, 0, sizeof g_model_defs);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	g_str_goal_conjunctions[GOAL_CONJ_STR_GROUP] = "group ";
	g_str_goal_conjunctions[GOAL_CONJ_STR_COMMA] = ", ";
	g_str_goal_conjunctions[GOAL_CONJ_STR_AND] = "and ";
	g_str_goal_conjunctions[GOAL_CONJ_STR_LESS_THAN] = "under";
}

/* Fills a condition text table as strings.txt fills it: in each of the four
 * blocks of 47 rows, condition c's row gets as many wordings as
 * g_goal_condition_text_variant_count gives c, and the columns past them stay
 * NULL. Each text is "r<row>c<column>". */
static void load_condition_text(const char *(*table)[TEST_COLUMNS])
{
	for (int row = 0; row < TEST_ROWS; ++row) {
		int count =
			g_goal_condition_text_variant_count[row %
							    TEST_CONDITIONS];
		for (int column = 0; column < count; ++column) {
			snprintf(g_condition_text[row][column],
				 sizeof g_condition_text[row][column], "r%dc%d",
				 row, column);
			table[row][column] = g_condition_text[row][column];
		}
	}
}

/* The first craft species from 2 whose object type has a model. Starting at
 * 2 leaves a species with a model below it, which no check names. */
static int species_with_model(void)
{
	for (int species = 2; species < CRAFT_SPECIES_COMM_SAT_1; ++species) {
		if (get_model_index_from_type((object_type_id)species) !=
		    MODEL_INDEX_NONE) {
			return species;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* The first species from CRAFT_SPECIES_COMM_SAT_1 to
 * CRAFT_SPECIES_NAV_BUOY_TYPE_2 with no model. */
static int marker_species(void)
{
	for (int species = CRAFT_SPECIES_COMM_SAT_1;
	     species <= CRAFT_SPECIES_NAV_BUOY_TYPE_2; ++species) {
		if (get_model_index_from_type((object_type_id)species) ==
		    MODEL_INDEX_NONE) {
			return species;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* The last species from CRAFT_SPECIES_NAV_BUOY_TYPE_2 down to
 * CRAFT_SPECIES_COMM_SAT_1 with no model. */
static int last_marker_species(void)
{
	for (int species = CRAFT_SPECIES_NAV_BUOY_TYPE_2;
	     species >= CRAFT_SPECIES_COMM_SAT_1; --species) {
		if (get_model_index_from_type((object_type_id)species) ==
		    MODEL_INDEX_NONE) {
			return species;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* The first species past CRAFT_SPECIES_NAV_BUOY_TYPE_2 with no model. */
static int unnamed_species(void)
{
	for (int species = CRAFT_SPECIES_NAV_BUOY_TYPE_2 + 1; species < 201;
	     ++species) {
		if (get_model_index_from_type((object_type_id)species) ==
		    MODEL_INDEX_NONE) {
			return species;
		}
	}
	XVT_ASSERT_TRUE(0);
	return 0;
}

/* Names a species with a model: short "XW", long "X-wing", plural
 * "X-wings". Returns its model index. */
static model_index name_species(int species)
{
	model_index model = get_model_index_from_type((object_type_id)species);
	strcpy(g_model_defs[model].name, "XW");
	g_model_defs[model].name_long = "X-wing";
	g_str_species_names_plural[model] = "X-wings";
	return model;
}

/* Flight group fg flies the named species with model, is called name and has
 * craft_count craft counted in all. */
static void set_group(int fg, const char *name, int craft_count)
{
	g_mission_flight_groups[fg].fg.craft_type =
		(uint8_t)species_with_model();
	strcpy(g_mission_flight_groups[fg].fg.name, name);
	g_mission_flight_groups[fg].fg.number_of_craft = (uint8_t)craft_count;
	g_mission_fg_stats[fg].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] =
		(uint16_t)craft_count;
}

/* Returns 1 when each of the strings appears in what was drawn, each after
 * the end of the one before; the list ends with NULL. */
static int drawn_in_order(const char *first, ...)
{
	va_list pieces;
	va_start(pieces, first);
	const char *cursor = g_drawn;
	int found = 1;
	for (const char *piece = first; piece != NULL;
	     piece = va_arg(pieces, const char *)) {
		const char *at = strstr(cursor, piece);
		if (at == NULL) {
			found = 0;
			break;
		}
		cursor = at + strlen(piece);
	}
	va_end(pieces);
	return found;
}

/* goals_draw_object_type_name draws a species with a model by its plural name
 * when asked for it, else its short name when asked for that, else its long
 * name. A species from CRAFT_SPECIES_COMM_SAT_1 to
 * CRAFT_SPECIES_NAV_BUOY_TYPE_2 without a model draws its entry of
 * g_str_sat_mine_probe_buoy_pilot_names, and any other species an empty
 * string. With the wide clip none of them needs a new line, and each returns
 * 0. */
static void check_object_type_names(void)
{
	fresh_page();
	int species = species_with_model();
	name_species(species);
	XVT_ASSERT_INT_EQ(goals_draw_object_type_name((uint16_t)species, 1, 1),
			  0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "X-wings") == 0);

	fresh_page();
	name_species(species);
	goals_draw_object_type_name((uint16_t)species, 0, 1);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "XW") == 0);

	fresh_page();
	name_species(species);
	goals_draw_object_type_name((uint16_t)species, 0, 0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "X-wing") == 0);

	fresh_page();
	int marker = marker_species();
	g_str_sat_mine_probe_buoy_pilot_names[marker -
					      CRAFT_SPECIES_COMM_SAT_1] =
		"Buoy";
	XVT_ASSERT_INT_EQ(goals_draw_object_type_name((uint16_t)marker, 1, 1),
			  0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "Buoy") == 0);

	fresh_page();
	int last = last_marker_species();
	g_str_sat_mine_probe_buoy_pilot_names[last - CRAFT_SPECIES_COMM_SAT_1] =
		"Probe";
	goals_draw_object_type_name((uint16_t)last, 0, 0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "Probe") == 0);

	fresh_page();
	XVT_ASSERT_INT_EQ(
		goals_draw_object_type_name((uint16_t)unnamed_species(), 0, 0),
		0);
	XVT_ASSERT_INT_EQ(g_drawn_length, 0);
}

/* goals_draw_object_type_name returns what
 * flight_text_get_wrap_height_for_string reports for the name: the line
 * height plus 1 for "X-wings", 7 pixels wide, past the narrow clip's limit
 * of 5, and 0 for "XW", 2 pixels wide. */
static void check_object_type_name_height(void)
{
	fresh_page();
	int species = species_with_model();
	name_species(species);
	g_flight_clip_right = TEST_NARROW_CLIP;
	XVT_ASSERT_INT_EQ(goals_draw_object_type_name((uint16_t)species, 1, 0),
			  TEST_LINE_HEIGHT + 1);
	XVT_ASSERT_INT_EQ(goals_draw_object_type_name((uint16_t)species, 0, 1),
			  0);
}

/* goals_draw_condition_text draws row condition plus the row base, at the
 * column of the amount wording, or column 0 when the condition has one
 * wording: destroyed (2) has 14 and always true (0) has 1. The species' model
 * gender picks the masculine, feminine or neutered table. */
static void check_condition_text_row_and_column(void)
{
	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	int species = species_with_model();
	model_index model = name_species(species);
	XVT_ASSERT_INT_EQ(g_goal_condition_text_variant_count[0], 1);
	XVT_ASSERT_INT_EQ(g_goal_condition_text_variant_count[2], 14);
	XVT_ASSERT_INT_EQ(goals_draw_condition_text((unsigned int)species,
						    MISSION_COND_DESTROYED, 5,
						    94),
			  0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "r96c5") == 0);

	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	goals_draw_condition_text((unsigned int)species,
				  MISSION_COND_ALWAYS_TRUE, 5, 47);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "r47c0") == 0);

	fresh_page();
	g_craft_gender[model] = CRAFT_GENDER_FEMININE;
	g_str_goal_cond_masculine[2][3] = "his";
	g_str_goal_cond_feminine[2][3] = "her";
	g_str_goal_cond_neutered[2][3] = "its";
	goals_draw_condition_text((unsigned int)species, MISSION_COND_DESTROYED,
				  3, 0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "her") == 0);

	fresh_page();
	g_craft_gender[model] = CRAFT_GENDER_NEUTERED;
	g_str_goal_cond_masculine[2][3] = "his";
	g_str_goal_cond_feminine[2][3] = "her";
	g_str_goal_cond_neutered[2][3] = "its";
	goals_draw_condition_text((unsigned int)species, MISSION_COND_DESTROYED,
				  3, 0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "its") == 0);
}

/* A species from CRAFT_SPECIES_COMM_SAT_1 to CRAFT_SPECIES_NAV_BUOY_TYPE_2
 * without a model takes the masculine table, whatever g_craft_gender holds;
 * any other species without a model draws nothing and returns 0. The return
 * is what flight_text_get_wrap_height_for_string reports for the text. */
static void check_condition_text_without_model(void)
{
	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	load_condition_text(g_str_goal_cond_feminine);
	memset(g_craft_gender, CRAFT_GENDER_FEMININE, sizeof g_craft_gender);
	g_flight_clip_right = TEST_NARROW_CLIP;
	XVT_ASSERT_INT_EQ(
		goals_draw_condition_text((unsigned int)marker_species(),
					  MISSION_COND_DESTROYED, 13, 141),
		TEST_LINE_HEIGHT + 1);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "r143c13") == 0);

	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	goals_draw_condition_text((unsigned int)last_marker_species(),
				  MISSION_COND_DESTROYED, 4, 0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "r2c4") == 0);

	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	XVT_ASSERT_INT_EQ(
		goals_draw_condition_text((unsigned int)unnamed_species(),
					  MISSION_COND_DESTROYED, 0, 0),
		0);
	XVT_ASSERT_INT_EQ(g_drawn_length, 0);
}

/* A goal on a flight group of several craft draws the craft type's long
 * name, the group's name and the condition, and ends the line. Goal status
 * picks the block of 47 rows (statuses 0, 2 and 3 block 0, 4 block 1, 1
 * block 2, 5 block 3), and the amount the column: 0 to 9 their own, 10 to 15
 * those of 0 to 5, 16 to 19 columns 10 to 13. "All but one" and "all except
 * the player" draw the plural name. With the wide clip the line is the line
 * height plus 2. */
static void check_goal_line_flight_group(void)
{
	static const struct {
		uint16_t status;
		uint16_t amount;
		const char *line;
	} cases[] = {
		{0, GOAL_AMT_50, "X-wing Red: r2c2\n"},
		{2, GOAL_AMT_25, "X-wing Red: r2c3\n"},
		{3, GOAL_AMT_PLAYER_FG, "X-wing Red: r2c9\n"},
		{0, GOAL_AMT_AT_LEAST_1, "X-wing Red: r2c4\n"},
		{0, GOAL_AMT_AT_LEAST_1_ALT, "X-wing Red: r2c4\n"},
		{4, GOAL_AMT_100_OF_SUBSET, "X-wing Red: r49c0\n"},
		{1, GOAL_AMT_ALL_BUT_1_OF_SUBSET, "X-wing Red: r96c5\n"},
		{5, GOAL_AMT_66, "X-wing Red: r143c10\n"},
		{0, 19, "X-wing Red: r2c13\n"},
		{0, GOAL_AMT_ALL_BUT_1, "X-wings Red: r2c5\n"},
		{0, GOAL_AMT_ALL_EXCEPT_PLAYER, "X-wings Red: r2c8\n"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fresh_page();
		load_condition_text(g_str_goal_cond_masculine);
		name_species(species_with_model());
		set_group(0, "Red", 3);
		XVT_ASSERT_INT_EQ(goals_outputgoal(0, MISSION_COND_DESTROYED,
						   GOAL_TARGET_FLIGHT_GROUP,
						   cases[i].status,
						   cases[i].amount, 0, NULL, -1,
						   0),
				  TEST_LINE_HEIGHT + 2);
		XVT_ASSERT_TRUE(strcmp(g_drawn, cases[i].line) == 0);
	}
}

/* For the special-cargo amounts, 6 and 7, the goal line also draws the
 * special craft's number, counted from 1 (craft 2 shows as 3), once
 * mission_is_special_cargo_inspected says the group's special cargo was
 * inspected, and "?" before that. Condition text given by the caller replaces
 * the table's, and draws the craft type's short name. */
static void check_goal_line_special_cargo(void)
{
	static const uint16_t amounts[2] = {GOAL_AMT_ALL_SPECIAL_CARGO,
					    GOAL_AMT_ALL_NON_SPECIAL};
	static const char *const lines[2] = {"X-wing Gold ?: r2c6\n",
					     "X-wing Gold ?: r2c7\n"};
	for (int i = 0; i < 2; ++i) {
		fresh_page();
		load_condition_text(g_str_goal_cond_masculine);
		name_species(species_with_model());
		set_group(1, "Gold", 4);
		g_mission_flight_groups[1].fg.special_cargo_craft = 2;
		goals_outputgoal(1, MISSION_COND_DESTROYED,
				 GOAL_TARGET_FLIGHT_GROUP, 0, amounts[i], 0,
				 NULL, -1, 0);
		XVT_ASSERT_TRUE(strcmp(g_drawn, lines[i]) == 0);

		fresh_page();
		name_species(species_with_model());
		set_group(1, "Gold", 4);
		g_mission_flight_groups[1].fg.special_cargo_craft = 2;
		g_mission_fg_stats[1]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_INSPECTED] =
			1;
		goals_outputgoal(1, MISSION_COND_DESTROYED,
				 GOAL_TARGET_FLIGHT_GROUP, 0, amounts[i], 0,
				 "Escort", -1, 0);
		XVT_ASSERT_TRUE(strcmp(g_drawn, "XW Gold 3: Escort\n") == 0);
	}
}

/* A flight group goal's time limit, in 5-second units, follows as
 * " (under m:ss)": 13 units are 1:05, 14 are 1:10, 24 are 2:00 and 30 are
 * 2:30. Condition text given by the caller replaces the table's, and the
 * craft type is drawn by its short name. */
static void check_goal_line_time_limit(void)
{
	static const struct {
		uint16_t units;
		const char *line;
	} cases[] = {
		{13, "XW Red: Escort (under 1:05)\n"},
		{14, "XW Red: Escort (under 1:10)\n"},
		{24, "XW Red: Escort (under 2:00)\n"},
		{30, "XW Red: Escort (under 2:30)\n"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fresh_page();
		name_species(species_with_model());
		set_group(0, "Red", 2);
		goals_outputgoal(0, MISSION_COND_DESTROYED,
				 GOAL_TARGET_FLIGHT_GROUP, 0, GOAL_AMT_100,
				 cases[i].units, "Escort", -1, 0);
		XVT_ASSERT_TRUE(strcmp(g_drawn, cases[i].line) == 0);
	}
}

/* With the narrow clip, the line is the line height plus 2, plus the line
 * height plus 1 for each measured piece that does not fit: here the given
 * condition text and the time limit. */
static void check_goal_line_height(void)
{
	fresh_page();
	name_species(species_with_model());
	set_group(0, "Red", 2);
	g_flight_clip_right = TEST_NARROW_CLIP;
	XVT_ASSERT_INT_EQ(goals_outputgoal(0, MISSION_COND_DESTROYED,
					   GOAL_TARGET_FLIGHT_GROUP, 0,
					   GOAL_AMT_100, 13, "Escort", -1, 0),
			  (TEST_LINE_HEIGHT + 2) + 2 * (TEST_LINE_HEIGHT + 1));
}

/* Four groups of global group 4, the third with its arrival disabled, and
 * group 4 in global group 5. A goal on global group 4 lists the three groups
 * that may arrive, in order, joined with a comma and then "and", and leaves
 * out the other two; the condition follows. */
static void check_goal_line_global_group(void)
{
	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	name_species(species_with_model());
	static const char *const names[5] = {"Red", "Blue", "Gray", "Gold",
					     "Green"};
	g_mission_header.num_flight_groups = 5;
	for (int fg = 0; fg < 5; ++fg) {
		set_group(fg, names[fg], fg == 0 ? 2 : 1);
		g_mission_flight_groups[fg].fg.global_group = fg == 4 ? 5 : 4;
		g_mission_fg_stats[fg].arrival_enabled = fg == 2 ? 0 : 1;
	}
	goals_outputgoal(4, MISSION_COND_DESTROYED, GOAL_TARGET_GLOBAL_GROUP, 0,
			 GOAL_AMT_100, 0, NULL, -1, 0);
	XVT_ASSERT_TRUE(drawn_in_order("Red", ", ", "Blue", " and ", "Gold",
				       ": r2c0\n", NULL));
	XVT_ASSERT_TRUE(strstr(g_drawn, "Gray") == NULL);
	XVT_ASSERT_TRUE(strstr(g_drawn, "Green") == NULL);
	XVT_ASSERT_TRUE(strstr(g_drawn, "Blue, ") == NULL);
}

/* Condition text given for a global group goal draws flight group 2's craft
 * type and name, then the text, instead of the list. */
static void check_goal_line_global_group_override(void)
{
	fresh_page();
	name_species(species_with_model());
	g_mission_header.num_flight_groups = 3;
	set_group(0, "Red", 1);
	set_group(1, "Blue", 1);
	set_group(2, "Gold", 1);
	for (int fg = 0; fg < 3; ++fg) {
		g_mission_flight_groups[fg].fg.global_group = 2;
		g_mission_fg_stats[fg].arrival_enabled = 1;
	}
	goals_outputgoal(2, MISSION_COND_DESTROYED, GOAL_TARGET_GLOBAL_GROUP, 0,
			 GOAL_AMT_100, 0, "Escort", -1, 0);
	XVT_ASSERT_TRUE(drawn_in_order("X-wing", "Gold", ": Escort\n", NULL));
	XVT_ASSERT_TRUE(strstr(g_drawn, "Red") == NULL);
}

/* A goal on a species draws the plural name of species target + 1 and the
 * condition for it; on a genus or family, the name g_genus_convert or
 * g_family_convert picks (goal genus 2 is genus 3, goal family 2 family 2);
 * on IFF 0 or 1, side word 0 or 1; on a higher IFF the mission's own name
 * for it, without a leading '1', and side word 2. */
static void check_goal_line_named_targets(void)
{
	int species = species_with_model();
	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	name_species(species);
	goals_outputgoal((uint16_t)(species - 1), MISSION_COND_DESTROYED,
			 GOAL_TARGET_SPECIES, 0, GOAL_AMT_75, 0, NULL, -1, 0);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "X-wings: r2c1\n") == 0);

	static const struct {
		uint16_t type;
		uint16_t target;
		const char *start;
	} cases[] = {
		{GOAL_TARGET_GENUS, 2, "Freighters: "},
		{GOAL_TARGET_FAMILY, 2, "Satellites: "},
		{GOAL_TARGET_IFF, 0, "Rebel: "},
		{GOAL_TARGET_IFF, 1, "Imperial: "},
		{GOAL_TARGET_IFF, 3, "Pirates craft: "},
		{GOAL_TARGET_IFF, 2, "Traders craft: "},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fresh_page();
		load_condition_text(g_str_goal_cond_masculine);
		g_str_goal_genus_names[3] = "Freighters";
		g_str_goal_family_names[2] = "Satellites";
		g_str_goal_sides[0] = "Rebel";
		g_str_goal_sides[1] = "Imperial";
		g_str_goal_sides[2] = " craft";
		strcpy(g_mission_header.iff_names[0], "Traders");
		strcpy(g_mission_header.iff_names[1], "1Pirates");
		goals_outputgoal(cases[i].target, MISSION_COND_DESTROYED,
				 cases[i].type, 0, GOAL_AMT_100, 0, NULL, -1,
				 0);
		XVT_ASSERT_TRUE(strncmp(g_drawn, cases[i].start,
					strlen(cases[i].start)) == 0);
	}
}

/* Goal targets 7 and 10 draw condition 0's text for species target + 1, then
 * the goal's condition; target 9, AI level 1, draws g_str_goal_escape's
 * entry 0. The species is feminine and only the feminine table is loaded, so
 * a text read for any other species would be missing. */
static void check_goal_line_status_targets(void)
{
	int species = species_with_model();
	static const uint16_t types[2] = {GOAL_TARGET_CRAFT_WHEN,
					  GOAL_TARGET_STATUS};
	for (int i = 0; i < 2; ++i) {
		fresh_page();
		load_condition_text(g_str_goal_cond_feminine);
		g_craft_gender[get_model_index_from_type(
			(object_type_id)species)] = CRAFT_GENDER_FEMININE;
		goals_outputgoal((uint16_t)(species - 1),
				 MISSION_COND_DESTROYED, types[i], 4,
				 GOAL_AMT_75, 0, NULL, -1, 0);
		XVT_ASSERT_TRUE(strcmp(g_drawn, "r47c0r49c1\n") == 0);
	}

	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	g_str_goal_escape[0] = "Escape";
	goals_outputgoal(1, MISSION_COND_DESTROYED, GOAL_TARGET_AI_LEVEL, 0,
			 GOAL_AMT_100, 0, NULL, -1, 0);
	XVT_ASSERT_TRUE(strncmp(g_drawn, "Escape", 6) == 0);
}

/* A percent_complete of 0 or more is drawn as " (n%)" at the end of the line,
 * and the text color goes back to the section's color in
 * g_goal_title_color_by_index; a negative one draws nothing. */
static void check_goal_line_percent(void)
{
	for (int title = 0; title < 4; ++title) {
		fresh_page();
		name_species(species_with_model());
		set_group(0, "Red", 2);
		flight_text_set_color(g_goal_title_color_by_index[title]);
		uint8_t section_color = g_flight_text_color_index;
		flight_text_set_color(0);
		goals_outputgoal(0, MISSION_COND_DESTROYED,
				 GOAL_TARGET_FLIGHT_GROUP, 0, GOAL_AMT_100, 0,
				 "Escort", 42, title);
		XVT_ASSERT_TRUE(strcmp(g_drawn, "XW Red: Escort (42%)\n") == 0);
		XVT_ASSERT_INT_EQ(g_flight_text_color_index, section_color);
	}

	fresh_page();
	name_species(species_with_model());
	set_group(0, "Red", 2);
	goals_outputgoal(0, MISSION_COND_DESTROYED, GOAL_TARGET_FLIGHT_GROUP, 0,
			 GOAL_AMT_100, 0, "Escort", 0, 1);
	XVT_ASSERT_TRUE(strcmp(g_drawn, "XW Red: Escort (0%)\n") == 0);
}

/* Known failure ai_level_escape_text_missing, issue #120: goals_outputgoal
 * draws one goal line and ends it with a new line. A goal on AI level 2 draws
 * entry 1 of g_str_goal_escape, which strings.txt never fills; the draw
 * reads through the NULL entry and the program stops. */
static void check_ai_level_goal_line_ends(void)
{
	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	g_str_goal_escape[0] = "Escape";
	goals_outputgoal(2, MISSION_COND_DESTROYED, GOAL_TARGET_AI_LEVEL, 0,
			 GOAL_AMT_100, 0, NULL, -1, 0);
	XVT_ASSERT_TRUE(g_drawn_length > 0);
	XVT_ASSERT_INT_EQ(g_drawn[g_drawn_length - 1], '\n');
}

/* Known failure condition_47_unloaded_text, issue #121:
 * g_goal_condition_text_variant_count says condition 47 has no wording in
 * strings.txt. Asked for it at column 3 in block 0, goals_draw_condition_text
 * reads row 47, the next block's condition 0, whose column 3 was never
 * loaded, and the draw reads through the NULL entry. A condition without
 * text should draw nothing. */
static void check_condition_47_unloaded_text(void)
{
	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	int species = species_with_model();
	XVT_ASSERT_INT_EQ(
		goals_draw_condition_text((unsigned int)species, 47, 3, 0), 0);
	XVT_ASSERT_INT_EQ(g_drawn_length, 0);
}

/* Known failure condition_47_past_table, issue #121: the same condition in
 * block 3, a goal to prevent, is row 188, one past the end of the 188-row
 * table; the sanitizer stops the program on the read. */
static void check_condition_47_past_table(void)
{
	fresh_page();
	load_condition_text(g_str_goal_cond_masculine);
	int species = species_with_model();
	XVT_ASSERT_INT_EQ(
		goals_draw_condition_text((unsigned int)species, 47, 0, 141),
		0);
	XVT_ASSERT_INT_EQ(g_drawn_length, 0);
}

int main(int argc, char **argv)
{
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"ai_level_escape_text_missing",
			 check_ai_level_goal_line_ends},
			{"condition_47_unloaded_text",
			 check_condition_47_unloaded_text},
			{"condition_47_past_table",
			 check_condition_47_past_table},
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
	check_object_type_names();
	check_object_type_name_height();
	check_condition_text_row_and_column();
	check_condition_text_without_model();
	check_goal_line_flight_group();
	check_goal_line_special_cargo();
	check_goal_line_time_limit();
	check_goal_line_height();
	check_goal_line_global_group();
	check_goal_line_global_group_override();
	check_goal_line_named_targets();
	check_goal_line_status_targets();
	check_goal_line_percent();
	return 0;
}
