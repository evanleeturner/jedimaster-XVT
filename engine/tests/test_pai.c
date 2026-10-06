/* Tests for xvt/flight/ai/pai.c, the computer pilots' context, range tests
 * and plans. Checks that pai_setup_context_and_find_order_plan_on_target puts
 * g_pai_context back as it found it, on a world this file builds itself: two
 * craft in two flight groups and one plan. Then the skill and range tests,
 * the order target match, and the plan text reader, compiler and loader,
 * which read plan files this file writes into a temporary asset folder. No
 * game data is read.
 *
 * POSIX only, for the temporary folder (test_asset_folder.h). */
#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/assets/file.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"

static struct object_record g_test_objects[2];
static struct mobile_object g_test_mobiles[2];
static struct craft_data g_test_craft[2];
/* The plan's bytes: a target byte, then the maneuver byte setup reads. */
static uint8_t g_test_plan[4] = {255, 6, 0, 0};
static uint8_t g_marker_plan[4];
static struct craft_data g_marker_craft;
static struct ai_controller g_marker_controller;

/* Object 0 flies in flight group 0 with no leader; object 1 is the target, in
 * flight group 1. Every order of group 0 is order 0, whose leader plan name
 * index is 7, and its first target is flight group target_fg. */
static void build_world(uint16_t target_fg)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0,
	       2 * sizeof g_mission_flight_groups[0]);
	memset(g_plan_table, 0, sizeof g_plan_table);
	g_object_table = g_test_objects;
	for (int i = 0; i < 2; ++i) {
		g_test_objects[i].object_type = 1;
		g_test_objects[i].flight_group_idx = (uint8_t)i;
		g_test_objects[i].world_x = 1000 * (i + 1);
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].p_craft = &g_test_craft[i];
		g_test_craft[i].leader_obj_idx = UINT8_MAX;
	}
	strcpy(g_plan_table[0].name, "nullpln");
	g_plan_data_ptrs[0] = g_test_plan;
	g_order_leader_builtin_plan_name_index[0] = 7;
	for (int slot = 0; slot < 3; ++slot) {
		g_mission_flight_groups[0].fg.orders[slot].order = 0;
		g_mission_flight_groups[0].fg.orders[slot].target1_type = 1;
		g_mission_flight_groups[0].fg.orders[slot].target1 =
			(uint8_t)target_fg;
		g_mission_flight_groups[0].fg.orders[slot].target1_or_target2 =
			1;
	}
}

/* A context unlike anything setup writes, so a field it changes shows. */
static void mark_context(void)
{
	memset(&g_pai_context, 0, sizeof g_pai_context);
	g_pai_context.object_index = 1;
	g_pai_context.craft = &g_marker_craft;
	g_pai_context.controller = &g_marker_controller;
	g_pai_context.leader_object_index = 3;
	g_pai_context.leader_or_self_craft = &g_marker_craft;
	g_pai_context.craft_flight_group_index = 5;
	g_pai_context.order_slot = 2;
	g_pai_context.craft_position_x = -11;
	g_pai_context.craft_position_y = -12;
	g_pai_context.craft_position_z = -13;
	g_pai_context.skill_tier = 2;
	g_pai_context.initial_maneuver_id = 9;
	g_pai_context.plan_cursor = &g_marker_plan[1];
	g_pai_context.require_undisabled_target = 1;
	g_pai_context.variable_plan_id = 4;
	g_pai_context.target_search_flags = 0x21;
	g_pai_context.target_search_origin_x = 21;
	g_pai_context.target_search_origin_y = 22;
	g_pai_context.target_search_origin_z = 23;
}

static void assert_context_marked(void)
{
	XVT_ASSERT_INT_EQ(g_pai_context.object_index, 1);
	XVT_ASSERT_TRUE(g_pai_context.craft == &g_marker_craft);
	XVT_ASSERT_TRUE(g_pai_context.controller == &g_marker_controller);
	XVT_ASSERT_INT_EQ(g_pai_context.leader_object_index, 3);
	XVT_ASSERT_TRUE(g_pai_context.leader_or_self_craft == &g_marker_craft);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_flight_group_index, 5);
	XVT_ASSERT_INT_EQ(g_pai_context.order_slot, 2);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_position_x, -11);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_position_y, -12);
	XVT_ASSERT_INT_EQ(g_pai_context.craft_position_z, -13);
	XVT_ASSERT_INT_EQ(g_pai_context.skill_tier, 2);
	XVT_ASSERT_INT_EQ(g_pai_context.initial_maneuver_id, 9);
	XVT_ASSERT_TRUE(g_pai_context.plan_cursor == &g_marker_plan[1]);
	XVT_ASSERT_INT_EQ(g_pai_context.require_undisabled_target, 1);
	XVT_ASSERT_INT_EQ(g_pai_context.variable_plan_id, 4);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_flags, 0x21);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_origin_x, 21);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_origin_y, 22);
	XVT_ASSERT_INT_EQ(g_pai_context.target_search_origin_z, 23);
}

static void check_found_plan_puts_context_back(void)
{
	build_world(1);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(0, 7, 1), 1);
	assert_context_marked();
}

static void check_no_plan_puts_context_back(void)
{
	/* The orders give the plan but name another flight group as target. */
	build_world(2);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(0, 7, 1), 0);
	assert_context_marked();

	/* No order gives the plan asked for. */
	build_world(1);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(0, 8, 1), 0);
	assert_context_marked();
}

static void check_no_object_leaves_context(void)
{
	build_world(1);
	mark_context();
	XVT_ASSERT_INT_EQ(
		pai_setup_context_and_find_order_plan_on_target(-1, 7, 1), 0);
	assert_context_marked();
}

/* ------------------------------------------------------------------------ */
/* Skill tiers and range tests. */

/* The tier is 0 below 0x8000, 1 below 0xC000, else 2. */
static void check_skill_tiers(void)
{
	XVT_ASSERT_INT_EQ(pai_skill_value_to_tier(0), 0);
	XVT_ASSERT_INT_EQ(pai_skill_value_to_tier(0x7FFF), 0);
	XVT_ASSERT_INT_EQ(pai_skill_value_to_tier(0x8000), 1);
	XVT_ASSERT_INT_EQ(pai_skill_value_to_tier(0xBFFF), 1);
	XVT_ASSERT_INT_EQ(pai_skill_value_to_tier(0xC000), 2);
	XVT_ASSERT_INT_EQ(pai_skill_value_to_tier(0xFFFF), 2);
}

/* Object 1 sits at the given offset from the craft's position in the
 * context, which is away from the origin. */
static void place_target(int x, int y, int z)
{
	build_world(1);
	memset(&g_pai_context, 0, sizeof g_pai_context);
	g_pai_context.craft_position_x = 5000;
	g_pai_context.craft_position_y = -3000;
	g_pai_context.craft_position_z = 2000;
	g_test_objects[1].world_x = 5000 + x;
	g_test_objects[1].world_y = -3000 + y;
	g_test_objects[1].world_z = 2000 + z;
}

/* The rough distance takes the larger of the X and Y offsets plus half the
 * smaller; when that is larger than the Z offset it adds half the Z offset,
 * else it halves itself and adds the Z offset. The test is "below". */
static void check_rough_distance(void)
{
	place_target(-1000, 400, 300);
	XVT_ASSERT_INT_EQ(pai_is_object_within_range_of_craft(1, 1351), 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 1000 + 200 + 150);
	XVT_ASSERT_INT_EQ(pai_is_object_within_range_of_craft(1, 1350), 0);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 1350);

	place_target(400, -1000, 300);
	XVT_ASSERT_INT_EQ(pai_is_object_within_range_of_craft(1, 1351), 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 1350);

	place_target(400, 100, -3000);
	XVT_ASSERT_INT_EQ(pai_is_object_within_range_of_craft(1, 4000), 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, (400 + 50) / 2 + 3000);
}

/* The skill range is 655,360, 737,280 or 819,200 world units for tiers 0 to 2;
 * expand_range adds about a third. The object must be targetable too. */
static void check_skill_range(void)
{
	static const int ranges[3] = {655360, 737280, 819200};
	for (int tier = 0; tier < 3; ++tier) {
		place_target(ranges[tier] - 1, 0, 0);
		g_pai_context.skill_tier = tier;
		XVT_ASSERT_INT_EQ(pai_is_object_targetable_near_craft(0, 1, 0),
				  1);
		XVT_ASSERT_INT_EQ(pai_is_object_within_skill_range_of_craft(1),
				  1);
		place_target(ranges[tier], 0, 0);
		g_pai_context.skill_tier = tier;
		XVT_ASSERT_INT_EQ(pai_is_object_targetable_near_craft(0, 1, 0),
				  0);
		XVT_ASSERT_INT_EQ(pai_is_object_within_skill_range_of_craft(1),
				  0);
		/* A third more, to within 1,024 for the rounded fraction. */
		place_target(ranges[tier] * 4 / 3 - 1024, 0, 0);
		g_pai_context.skill_tier = tier;
		XVT_ASSERT_INT_EQ(pai_is_object_targetable_near_craft(0, 1, 1),
				  1);
		place_target(ranges[tier] * 4 / 3 + 1024, 0, 0);
		g_pai_context.skill_tier = tier;
		XVT_ASSERT_INT_EQ(pai_is_object_targetable_near_craft(0, 1, 1),
				  0);
	}

	/* An object that cannot be targeted is never near. */
	place_target(100, 0, 0);
	g_test_objects[1].object_type = 0;
	XVT_ASSERT_INT_EQ(pai_is_object_targetable_near_craft(0, 1, 1), 0);
}

/* The craft's own ai_skill, unless effective_ai_object_link points to an
 * object that still has the saved signature and has character data. A
 * signature that no longer matches clears the link. */
static void check_effective_skill(void)
{
	static struct mobile_object_char_data char_data;
	build_world(1);
	struct craft_data *craft = &g_test_craft[0];
	craft->ai_skill = 0x4000;
	XVT_ASSERT_INT_EQ(pai_get_effective_skill_value(craft), 0x4000);

	char_data.skill_value = 0xC000;
	g_test_mobiles[1].p_char_data = &char_data;
	g_test_objects[1].object_signature = 7;
	craft->effective_ai_object_link = &g_test_objects[1];
	craft->effective_ai_object_signature = 7;
	XVT_ASSERT_INT_EQ(pai_get_effective_skill_value(craft), 0xC000);
	XVT_ASSERT_TRUE(craft->effective_ai_object_link == &g_test_objects[1]);

	g_test_objects[1].mobj = NULL;
	XVT_ASSERT_INT_EQ(pai_get_effective_skill_value(craft), 0x4000);
	g_test_objects[1].mobj = &g_test_mobiles[1];
	g_test_mobiles[1].p_char_data = NULL;
	XVT_ASSERT_INT_EQ(pai_get_effective_skill_value(craft), 0x4000);
	XVT_ASSERT_TRUE(craft->effective_ai_object_link == &g_test_objects[1]);

	g_test_mobiles[1].p_char_data = &char_data;
	g_test_objects[1].object_signature = 8;
	XVT_ASSERT_INT_EQ(pai_get_effective_skill_value(craft), 0x4000);
	XVT_ASSERT_TRUE(craft->effective_ai_object_link == NULL);
}

/* The object matches the first two target conditions of the current order
 * slot, joined by "or" when target1_or_target2 is 1, else by "and", or the
 * second two, joined the same way by target3_or_target4. Conditions of type
 * 1 name a flight group: object 0 is in group 0, object 1 in group 1. */
static void check_order_targets_match(void)
{
	build_world(1);
	memset(&g_pai_context, 0, sizeof g_pai_context);
	g_pai_context.order_slot = 1;
	struct mission_order *order = &g_mission_flight_groups[0].fg.orders[1];
	order->target1_type = 1;
	order->target1 = 1;
	order->target2_type = 1;
	order->target2 = 0;
	order->secondary_target_types[0] = 1;
	order->secondary_targets[0] = 2;
	order->secondary_target_types[1] = 1;
	order->secondary_targets[1] = 2;
	order->target1_or_target2 = 1;
	order->target3_or_target4 = 1;
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(1), 1);
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(0), 1);
	order->target1_or_target2 = 0;
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(1), 0);
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(0), 0);

	/* The second pair alone. */
	order->secondary_targets[1] = 0;
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(0), 1);
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(1), 0);
	order->target3_or_target4 = 0;
	order->secondary_targets[0] = 0;
	order->secondary_targets[1] = 2;
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(0), 0);
	order->secondary_targets[1] = 0;
	XVT_ASSERT_INT_EQ(pai_current_order_targets_match_object(0), 1);
}

/* ------------------------------------------------------------------------ */
/* Plan names and tokens. */

/* The first entry with the name, compared up to 80 characters, or 256; an
 * empty name finds the first free entry. The free-entry search gives 256
 * when every entry has a name, and the id search gives 0 for a missing name. */
static void check_plan_table_lookups(void)
{
	memset(g_plan_table, 0, sizeof g_plan_table);
	strcpy(g_plan_table[0].name, "nullpln");
	strcpy(g_plan_table[1].name, "formldr1pln");
	strcpy(g_plan_table[3].name, "formldr1pln");
	XVT_ASSERT_INT_EQ(pai_find_plan_table_index_by_name("formldr1pln"), 1);
	XVT_ASSERT_INT_EQ(pai_find_plan_id_by_name_or_zero("formldr1pln"), 1);
	XVT_ASSERT_INT_EQ(pai_find_plan_table_index_by_name("formldr"), 256);
	XVT_ASSERT_INT_EQ(pai_find_plan_id_by_name_or_zero("formldr"), 0);
	XVT_ASSERT_INT_EQ(pai_find_plan_table_index_by_name(""), 2);
	XVT_ASSERT_INT_EQ(pai_find_free_plan_table_index(), 2);

	memset(g_plan_table[255].name, 'a', sizeof g_plan_table[255].name);
	char long_name[100];
	memset(long_name, 'a', sizeof long_name);
	long_name[sizeof long_name - 1] = '\0';
	XVT_ASSERT_INT_EQ(pai_find_plan_table_index_by_name(long_name), 255);
	XVT_ASSERT_INT_EQ(pai_find_plan_id_by_name_or_zero(long_name), 255);

	for (int i = 0; i < 256; ++i) {
		g_plan_table[i].name[0] = 'p';
	}
	XVT_ASSERT_INT_EQ(pai_find_free_plan_table_index(), 256);
	XVT_ASSERT_INT_EQ(pai_find_plan_table_index_by_name(""), 256);
}

/* The index of the token with the name in its table, or of the table's empty
 * end entry: 9 for the targets, 32 for the maneuvers, 48 for the orders. */
static void check_token_lookups(void)
{
	XVT_ASSERT_INT_EQ(pai_find_target_token_index("LOCATARGET"), 0);
	XVT_ASSERT_INT_EQ(pai_find_target_token_index("HOMETARGET"), 5);
	XVT_ASSERT_INT_EQ(pai_find_target_token_index("0x80"), 8);
	XVT_ASSERT_INT_EQ(pai_find_target_token_index("hometarget"), 9);
	XVT_ASSERT_INT_EQ(pai_find_maneuver_token_index("NULLMANR"), 0);
	XVT_ASSERT_INT_EQ(pai_find_maneuver_token_index("SPLITSMANR"), 2);
	XVT_ASSERT_INT_EQ(pai_find_maneuver_token_index("NOMANR"), 32);
	XVT_ASSERT_INT_EQ(pai_find_order_token_index("NULLORDR"), 0);
	XVT_ASSERT_INT_EQ(pai_find_order_token_index("UNDERATTACKORDR"), 2);
	XVT_ASSERT_INT_EQ(pai_find_order_token_index("PLAYERINPUTORDR"), 47);
	XVT_ASSERT_INT_EQ(pai_find_order_token_index(""), 48);
}

/* Built-in plan names get the id of the plan loaded under each; a name with no
 * plan gets 0. The table's last name is number 73, and its end entry gets
 * nothing. */
static void check_builtin_plan_ids(void)
{
	memset(g_plan_table, 0, sizeof g_plan_table);
	memset(g_builtin_plan_id_by_name_index, 0xEE,
	       sizeof g_builtin_plan_id_by_name_index);
	strcpy(g_plan_table[0].name, g_builtin_plan_name_table[0]);
	strcpy(g_plan_table[7].name, g_builtin_plan_name_table[3]);
	strcpy(g_plan_table[200].name, g_builtin_plan_name_table[73]);
	pai_cache_builtin_plan_ids();
	XVT_ASSERT_INT_EQ(g_builtin_plan_id_by_name_index[0], 0);
	XVT_ASSERT_INT_EQ(g_builtin_plan_id_by_name_index[1], 0);
	XVT_ASSERT_INT_EQ(g_builtin_plan_id_by_name_index[3], 7);
	XVT_ASSERT_INT_EQ(g_builtin_plan_id_by_name_index[73], 200);
	XVT_ASSERT_INT_EQ(g_builtin_plan_id_by_name_index[74], 0xEE);
}

/* ------------------------------------------------------------------------ */
/* The plan text and the saved plan file. */

static struct xvt_test_assets g_assets;

/* Clears the plans, the way flight loading finds them before a load. */
static void clear_plans(void)
{
	memset(g_plan_table, 0, sizeof g_plan_table);
	memset(g_plan_data_ptrs, 0, sizeof g_plan_data_ptrs);
	memset(g_plan_order_data, 0, sizeof g_plan_order_data);
	g_plan_count = 0;
}

/* Opens the named file of the asset folder for the token reader. */
static xvt_file *open_text(const char *name, const char *text)
{
	xvt_test_write_text(g_assets.asset, name, text);
	xvt_file *stream = file_open(name, "r");
	XVT_ASSERT_TRUE(stream != NULL);
	return stream;
}

static void assert_next_token(xvt_file *stream, const char *expected)
{
	char token[256];
	memset(token, 'x', sizeof token);
	XVT_ASSERT_INT_EQ(pai_read_plan_text_token(token, stream), 1);
	XVT_ASSERT_TRUE(strcmp(token, expected) == 0);
}

/* Spaces, tabs, newlines and commas are skipped, and from a semicolon to the
 * end of the line; a token runs to the next of those characters or the end of
 * the file. At the end of the file the token is empty, and the call still
 * returns 1. */
static void check_plan_text_tokens(void)
{
	xvt_test_open_assets(&g_assets);
	xvt_file *stream = open_text(
		"tokens.pln",
		" ,\tALPHA BETA,GAMMA\n; a comment, with words\n\n\tDELTA;x\n"
		"; a last comment");
	assert_next_token(stream, "ALPHA");
	assert_next_token(stream, "BETA");
	assert_next_token(stream, "GAMMA");
	assert_next_token(stream, "DELTA;x");
	assert_next_token(stream, "");
	assert_next_token(stream, "");
	FILE_RAW_CLOSE(stream);

	stream = open_text("last.pln", "OMEGA");
	assert_next_token(stream, "OMEGA");
	assert_next_token(stream, "");
	FILE_RAW_CLOSE(stream);
	xvt_test_close_assets(&g_assets);
}

/* Known failure plan_text_crlf, issue #208: a plan text whose lines end in
 * CR LF reads as one whose lines end in LF, as it does where the file is
 * read in text mode. The reader keeps the CR as part of the word. */
static void check_plan_text_crlf(void)
{
	xvt_test_open_assets(&g_assets);
	xvt_file *stream = open_text("crlf.pln", "ALPHA\r\n\r\nBETA\r\n");
	assert_next_token(stream, "ALPHA");
	assert_next_token(stream, "BETA");
	assert_next_token(stream, "");
	FILE_RAW_CLOSE(stream);
	xvt_test_close_assets(&g_assets);
}

/* Two plans, each naming the other: the first before the text defines the
 * second, the second after the first is defined. */
static const char g_two_plans[] = "; Two plans.\n"
				  "alphapln LOCATARGET CRUISEMANR\n"
				  "\tUNDERATTACKORDR betapln\n"
				  "\tNULLORDR\n"
				  "betapln HOMETARGET ATTACKMANR\n"
				  "\tUNDERATTACKORDR alphapln NULLORDR\n"
				  "*\n";

/* The bytes each of g_two_plans compiles to. */
static void assert_two_plans(int alpha, int beta)
{
	XVT_ASSERT_TRUE(strcmp(g_plan_table[alpha].name, "alphapln") == 0);
	XVT_ASSERT_TRUE(strcmp(g_plan_table[beta].name, "betapln") == 0);
	XVT_ASSERT_INT_EQ(g_plan_table[alpha].is_defined, 1);
	XVT_ASSERT_INT_EQ(g_plan_table[beta].is_defined, 1);
	XVT_ASSERT_INT_EQ(g_plan_count, 2);
	const uint8_t alpha_bytes[] = {249, AI_MANEUVER_MODE_CRUISE, 2,
				       (uint8_t)beta, 0};
	const uint8_t beta_bytes[] = {254, AI_MANEUVER_MODE_ATTACK, 2,
				      (uint8_t)alpha, 0};
	uint8_t *alpha_data = g_plan_data_ptrs[alpha];
	uint8_t *beta_data = g_plan_data_ptrs[beta];
	XVT_ASSERT_TRUE(alpha_data ==
			g_plan_order_data + g_plan_table[alpha].data_offset);
	XVT_ASSERT_TRUE(beta_data ==
			g_plan_order_data + g_plan_table[beta].data_offset);
	XVT_ASSERT_TRUE(memcmp(alpha_data, alpha_bytes, sizeof alpha_bytes) ==
			0);
	XVT_ASSERT_TRUE(memcmp(beta_data, beta_bytes, sizeof beta_bytes) == 0);
	XVT_ASSERT_TRUE(pai_getplandataptrbyname("betapln") == beta_data);
}

/* A plan text compiles into the table, the plan bytes and their pointers, one
 * plan counted for each, and is saved as the table's size and the table, then
 * 0xFFFF and that many plan bytes. The saved file then loads back the same
 * plans. */
static void check_compile_and_load(void)
{
	xvt_test_open_assets(&g_assets);
	xvt_test_write_text(g_assets.asset, "two.pln", g_two_plans);
	clear_plans();
	XVT_ASSERT_INT_EQ(pai_compile_plans_from_text("two"), 1);
	assert_two_plans(0, 1);

	char user[XVT_TEST_PATH_CAPACITY];
	xvt_test_join(user, g_assets.folder, "user");
	size_t size = 0;
	char *saved = xvt_test_read_file(user, "cache/two.plo", &size);
	XVT_ASSERT_INT_EQ(size, 4 + sizeof g_plan_table + 4 + 0xFFFF);
	uint32_t section_size = 0;
	memcpy(&section_size, saved, 4);
	XVT_ASSERT_INT_EQ(section_size, sizeof g_plan_table);
	XVT_ASSERT_TRUE(memcmp(saved + 4, g_plan_table, sizeof g_plan_table) ==
			0);
	memcpy(&section_size, saved + 4 + sizeof g_plan_table, 4);
	XVT_ASSERT_INT_EQ(section_size, 0xFFFF);
	XVT_ASSERT_TRUE(memcmp(saved + 8 + sizeof g_plan_table,
			       g_plan_order_data, 0xFFFF) == 0);
	free(saved);

	/* The saved file alone gives the plans: the text now defines none.
	 * Another plan's leftover entry is cleared by the load. */
	xvt_test_write_text(g_assets.asset, "two.pln", "*\n");
	clear_plans();
	strcpy(g_plan_table[9].name, "leftoverpln");
	XVT_ASSERT_INT_EQ(pai_loadplans("two"), 1);
	assert_two_plans(0, 1);
	XVT_ASSERT_INT_EQ(g_plan_table[9].name[0], '\0');
	xvt_test_close_assets(&g_assets);
}

/* With no saved plan file the loader compiles the text and returns what the
 * compiler returns; it clears the table and the count first. */
static void check_load_falls_back_to_text(void)
{
	xvt_test_open_assets(&g_assets);
	xvt_test_write_text(g_assets.asset, "text.pln", g_two_plans);
	clear_plans();
	strcpy(g_plan_table[0].name, "leftoverpln");
	g_plan_count = 5;
	XVT_ASSERT_INT_EQ(pai_loadplans("text"), 1);
	assert_two_plans(0, 1);

	clear_plans();
	XVT_ASSERT_INT_EQ(pai_loadplans("missing"), 0);

	/* A saved file cut short at any of its four reads falls back too. */
	static const char *const names[4] = {"cut0", "cut1", "cut2", "cut3"};
	const size_t cuts[4] = {2, 4 + 100, 4 + sizeof g_plan_table + 2,
				8 + sizeof g_plan_table + 1};
	for (int i = 0; i < 4; ++i) {
		char file_name[16];
		snprintf(file_name, sizeof file_name, "%s.plo", names[i]);
		uint8_t *saved = calloc(cuts[i], 1);
		XVT_ASSERT_TRUE(saved != NULL);
		const uint32_t table_size = sizeof g_plan_table;
		const uint32_t data_size = 16;
		memcpy(saved, &table_size, cuts[i] < 4 ? cuts[i] : 4);
		if (cuts[i] >= 8 + sizeof g_plan_table) {
			memcpy(saved + 4 + sizeof g_plan_table, &data_size, 4);
		}
		xvt_test_write_file(g_assets.asset, file_name, saved, cuts[i]);
		free(saved);
		snprintf(file_name, sizeof file_name, "%s.pln", names[i]);
		xvt_test_write_text(g_assets.asset, file_name, g_two_plans);
		clear_plans();
		XVT_ASSERT_INT_EQ(pai_loadplans(names[i]), 1);
		assert_two_plans(0, 1);
	}
	xvt_test_close_assets(&g_assets);
}

/* Compiles the text, saved as bad.pln, into a cleared table. */
static int compile_text(const char *text)
{
	xvt_test_write_text(g_assets.asset, "bad.pln", text);
	clear_plans();
	return pai_compile_plans_from_text("bad");
}

/* The compiler returns 0 when the file does not open, a plan is defined
 * twice, the table is full, a token is unknown, the text ends before the "*",
 * or a named plan is never defined. */
static void check_compile_refusals(void)
{
	xvt_test_open_assets(&g_assets);
	clear_plans();
	XVT_ASSERT_INT_EQ(pai_compile_plans_from_text("missing"), 0);
	XVT_ASSERT_INT_EQ(compile_text("a LOCATARGET CRUISEMANR NULLORDR\n"
				       "a LOCATARGET CRUISEMANR NULLORDR\n*"),
			  0);
	XVT_ASSERT_INT_EQ(compile_text("a NOTARGETATALL CRUISEMANR NULLORDR *"),
			  0);
	XVT_ASSERT_INT_EQ(compile_text("a LOCATARGET NOMANR NULLORDR *"), 0);
	XVT_ASSERT_INT_EQ(compile_text("a LOCATARGET CRUISEMANR NOORDR *"), 0);
	XVT_ASSERT_INT_EQ(compile_text("a LOCATARGET CRUISEMANR NULLORDR"), 0);
	XVT_ASSERT_INT_EQ(compile_text("a LOCATARGET CRUISEMANR\n"
				       "UNDERATTACKORDR b NULLORDR *"),
			  0);
	XVT_ASSERT_INT_EQ(compile_text("a LOCATARGET CRUISEMANR NULLORDR *"),
			  1);

	/* A plan name keeps its first 79 characters. */
	char text[200];
	memset(text, 'n', 90);
	strcpy(text + 90, " LOCATARGET CRUISEMANR NULLORDR *");
	XVT_ASSERT_INT_EQ(compile_text(text), 1);
	XVT_ASSERT_INT_EQ(strlen(g_plan_table[0].name), 79);

	/* A full table takes no new plan, defined or named. */
	xvt_test_write_text(g_assets.asset, "full.pln",
			    "p LOCATARGET CRUISEMANR NULLORDR *");
	clear_plans();
	for (int i = 0; i < 256; ++i) {
		g_plan_table[i].name[0] = 'q';
	}
	XVT_ASSERT_INT_EQ(pai_compile_plans_from_text("full"), 0);
	xvt_test_write_text(g_assets.asset, "full.pln",
			    "q LOCATARGET CRUISEMANR UNDERATTACKORDR r *");
	clear_plans();
	for (int i = 1; i < 256; ++i) {
		g_plan_table[i].name[0] = 'z';
		g_plan_table[i].is_defined = 1;
	}
	XVT_ASSERT_INT_EQ(pai_compile_plans_from_text("full"), 0);
	xvt_test_close_assets(&g_assets);
}

/* Known failure plan_word_too_long, issue #207: the compiler reads each word
 * into a 256-byte buffer, and the reader does not count what it copies. A
 * plan name of 300 characters writes past the buffer, and the sanitizer
 * stops the program there. A word too long for the buffer must not be
 * written past it. */
static void check_plan_word_too_long(void)
{
	char text[400];
	memset(text, 'a', 300);
	strcpy(text + 300, " LOCATARGET CRUISEMANR NULLORDR *");
	xvt_test_open_assets(&g_assets);
	compile_text(text);
	xvt_test_close_assets(&g_assets);
}

/* Known failure plan_name_unknown, issue #207: g_plan_data_ptrs holds a
 * pointer for each of the 256 plan ids. For a name no plan has, the lookup
 * reads the entry after the last, past the array, and the sanitizer stops
 * the program there. */
static void check_plan_data_unknown_name(void)
{
	clear_plans();
	strcpy(g_plan_table[0].name, "nullpln");
	(void)pai_getplandataptrbyname("nosuchpln");
}

/* Writes a saved plan file whose two sections give the sizes asked for, and
 * holds that many bytes each. */
static void write_saved_plans(const char *name, uint32_t table_size,
			      uint32_t data_size)
{
	size_t size = 8 + (size_t)table_size + data_size;
	uint8_t *file = calloc(size, 1);
	XVT_ASSERT_TRUE(file != NULL);
	memcpy(file, &table_size, 4);
	memcpy(file + 4 + table_size, &data_size, 4);
	xvt_test_write_file(g_assets.asset, name, file, size);
	free(file);
}

/* Known failure plo_table_too_large, issue #207: g_plan_table holds 256
 * records. A saved plan file whose table section is one record longer is
 * read whole into the table, past its end, and the sanitizer stops the
 * program there. */
static void check_saved_table_too_large(void)
{
	xvt_test_open_assets(&g_assets);
	write_saved_plans("bigtable.plo",
			  sizeof g_plan_table + sizeof g_plan_table[0], 16);
	clear_plans();
	(void)pai_loadplans("bigtable");
	xvt_test_close_assets(&g_assets);
}

/* Known failure plo_data_too_large, issue #207: the same for the plan bytes,
 * a section 16 bytes longer than g_plan_order_data. */
static void check_saved_data_too_large(void)
{
	xvt_test_open_assets(&g_assets);
	write_saved_plans("bigdata.plo", sizeof g_plan_table,
			  sizeof g_plan_order_data + 16);
	clear_plans();
	(void)pai_loadplans("bigdata");
	xvt_test_close_assets(&g_assets);
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
			{"plan_text_crlf", check_plan_text_crlf},
			{"plan_word_too_long", check_plan_word_too_long},
			{"plan_name_unknown", check_plan_data_unknown_name},
			{"plo_table_too_large", check_saved_table_too_large},
			{"plo_data_too_large", check_saved_data_too_large},
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
	check_found_plan_puts_context_back();
	check_no_plan_puts_context_back();
	check_no_object_leaves_context();
	check_skill_tiers();
	check_rough_distance();
	check_skill_range();
	check_effective_skill();
	check_order_targets_match();
	check_plan_table_lookups();
	check_token_lookups();
	check_builtin_plan_ids();
	check_plan_text_tokens();
	check_compile_and_load();
	check_load_falls_back_to_text();
	check_compile_refusals();
	return 0;
}
