#include "xvt/flight/mission/goals.h"

#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/render/renderer.h"

/* Text color of each goals-page section, as the color letter
 * flight_text_set_color takes: 'J', 'N', 'F' and 'R' (0x4A, 0x4E, 0x46, 0x52)
 * for sections 0 to 3, and 0 for 4 to 7. Nothing writes it. Read by
 * mfd_draw_mission_goals_page and by goals_outputgoal, which sets it back after
 * drawing a percentage. */
// GLOBAL: XVT 0x51BE80
uint8_t g_goal_title_color_by_index[8] = {0x4A, 0x4E, 0x46, 0x52, 0, 0, 0, 0};

/* Per mission condition (a MISSION_COND_ value, 0 to 47), how many wordings
 * its row of condition text has in strings.txt: 14 (one per amount wording)
 * or 1; entry 47 is 0. string_table_load_game_strings reads that many lines per
 * row, and goals_draw_condition_text uses wording 0 when the count is 1.
 * Nothing writes it. */
// GLOBAL: XVT 0x51BEA0
uint8_t g_goal_condition_text_variant_count[48] = {
	1, 14, 14, 14, 14, 14, 14, 14, 14, 1, 1, 1,  14, 1,  1,	 1,
	1, 1,  1,  14, 1,  1,  1,  1,  1,  1, 1, 1,  1,	 1,  1,	 1,
	1, 1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 14, 14, 14, 14, 0};
/* Maps a goal amount (a GOAL_AMT_ value, 0 to 19) to the column of the
 * condition text tables: 0 to 9 keep their value, the subset amounts 10 to
 * 15 use the columns of 0 to 5, and 16 to 19 use 10 to 13. Read only by
 * goals_outputgoal. */
// GLOBAL: XVT 0x51BED0
static const uint16_t g_goal_amount_text_variant_by_op[20] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 10, 11, 12, 13,
};
/* Maps goals_outputgoal's goal status, 0 to 5, to a block of 47 rows in the
 * condition text tables: statuses 0, 2 and 3 use block 0, status 4 block 1,
 * status 1 block 2 and status 5 block 3. */
// GLOBAL: XVT 0x51BEF8
static const uint16_t g_goal_status_condition_row_block[6] = {0, 2, 0, 0, 1, 3};
/* Condition text for craft whose name is feminine (g_craft_gender), from
 * strings.txt: row 47 times the status block plus the condition, column the
 * amount wording. string_table_load_game_strings fills it only when some craft
 * is feminine; columns past a row's count in
 * g_goal_condition_text_variant_count stay NULL. */
// GLOBAL: XVT 0xA60A60
const char *g_str_goal_cond_feminine[188][14] = {0};
/* Condition text for craft whose name is neuter, laid out as
 * g_str_goal_cond_feminine. string_table_load_game_strings fills it only when some
 * craft is neuter. */
// GLOBAL: XVT 0xA633C0
const char *g_str_goal_cond_neutered[188][14] = {0};
/* Condition text for craft whose name is masculine, and for species
 * CRAFT_SPECIES_COMM_SAT_1 to CRAFT_SPECIES_NAV_BUOY_TYPE_2 that have no model
 * index, laid out as g_str_goal_cond_feminine. string_table_load_game_strings
 * always fills it. */
// GLOBAL: XVT 0xA65CE0
const char *g_str_goal_cond_masculine[188][14] = {0};
/* Per model index, the grammatical gender of the craft's name, a
 * CRAFT_GENDER_ value. string_table_load_game_strings sets entries 0 to 72 from
 * the first letter, m, f or n, of each model name line in strings.txt and
 * stops the game on any other letter; entries 73 to 79 stay 0, masculine. */
// GLOBAL: XVT 0xA686E0
uint8_t g_craft_gender[80] = {0};
/* The two operator words (GOAL_OPERATOR_STR_ values) from strings.txt,
 * filled by string_table_load_game_strings. Nothing reads it. */
// GLOBAL: XVT 0xA63380
const char *g_str_goal_operators[2] = {0};
/* Section titles and outcome words of the goals page (GOAL_TITLE_STR_
 * values), from strings.txt by string_table_load_game_strings. Read by
 * mfd_draw_mission_goals_page. */
// GLOBAL: XVT 0xA63390
const char *g_str_goal_titles[9] = {0};
/* The amount words (GOAL_PERCENT_STR_ values) from strings.txt, filled by
 * string_table_load_game_strings. Nothing reads it. */
// GLOBAL: XVT 0xA68610
const char *g_str_goal_percentages[14] = {0};

/* Family names from strings.txt, indexed through g_family_convert; filled by
 * string_table_load_game_strings and drawn by goals_outputgoal for a goal on a
 * family. */
// GLOBAL: XVT 0xA68650
const char *g_str_goal_family_names[7] = {0};
/* Genus names from strings.txt, indexed through g_genus_convert; filled by
 * string_table_load_game_strings and drawn by goals_outputgoal for a goal on a
 * genus. */
// GLOBAL: XVT 0xA68670
const char *g_str_goal_genus_names[16] = {0};
/* Joining words of a goal line (GOAL_CONJ_STR_ values) from strings.txt,
 * filled by string_table_load_game_strings. goals_outputgoal uses the "less
 * than" word before a time limit and the group, "and" and comma words in a
 * list of flight groups. */
// GLOBAL: XVT 0xA686B0
const char *g_str_goal_conjunctions[8] = {0};
/* string_table_load_game_strings fills only entry 0, from strings.txt; entries 1
 * and 2 stay NULL. goals_outputgoal draws entry targetId - 1 for a goal whose
 * target type is GOAL_TARGET_AI_LEVEL. */
// GLOBAL: XVT 0xA686D4
const char *g_str_goal_escape[3] = {0};
/* Side words (GOAL_SIDE_STR_ values) from strings.txt, filled by
 * string_table_load_game_strings. goals_outputgoal draws entry 0 or 1 for a goal
 * on IFF 0 or 1, and entry 2 after the mission's own name for a higher IFF. */
// GLOBAL: XVT 0xA68730
const char *g_str_goal_sides[3] = {0};

/* The word for an unknown value, the line after the warhead names in
 * strings.txt, set by string_table_load_game_strings. Drawn by
 * hud_update_targeting_computer_display and hud_draw_cmd_target_details where a
 * target's cargo or time is not known. */
// GLOBAL: XVT 0xA607AC
const char *g_str_unknown = 0;
/* Names of the species from CRAFT_SPECIES_COMM_SAT_1 (0x46) on, indexed by
 * species minus 0x46; from strings.txt by string_table_load_game_strings. Read
 * by goals_draw_object_type_name for such a species with no model index, and by
 * hud_format_object_display_name and msg_format_object_name. */
// GLOBAL: XVT 0xA607B0
const char *g_str_sat_mine_probe_buoy_pilot_names[16] = {0};
/* Status words from strings.txt, filled by string_table_load_game_strings.
 * Nothing reads it. */
// GLOBAL: XVT 0xA607F0
const char *g_str_status_strings[9] = {0};
/* Warhead names from strings.txt, for object types 0x8F to 0x9B, filled by
 * string_table_load_game_strings. Read by hud_format_object_display_name and
 * msg_format_object_name. */
// GLOBAL: XVT 0xA60820
const char *g_str_warhead_names[13] = {0};
/* Plural craft name per model index, from strings.txt by
 * string_table_load_game_strings. Read only by goals_draw_object_type_name. */
// GLOBAL: XVT 0xA60860
const char *g_str_species_names_plural[73] = {0};
/* Wingman command words from strings.txt, filled by
 * string_table_load_game_strings. Nothing reads it. */
// GLOBAL: XVT 0xA60990
const char *g_str_wingman_commands[10] = {0};

/* Draws one goal line of the goals page at the flight text cursor and ends it
 * with a new line. A flight group target (target_type 1) draws the craft type,
 * the group's name and the condition; for the special-cargo amounts (6 and 7)
 * it also draws the special craft's number when
 * mission_is_special_cargo_inspected returns nonzero, else "?"; then any
 * time limit as "m:ss" (time_limit5_sec_units counts 5 seconds). A global group
 * target (target_type 8) lists every flight group of that global group whose
 * arrival_enabled is set, joined with commas and "and", then the condition;
 * with condition_text_override set it draws flight group targetId's craft type
 * and name instead. Other targets draw a species, genus, family or IFF name,
 * or entry targetId - 1 of g_str_goal_escape (types 7 and 10 draw condition 0's
 * text), then the condition for species targetId + 1. condition_text_override,
 * when not NULL, replaces the condition text for flight group and global
 * group targets. A percent_complete of 0 or more is drawn as " (n%)" in a
 * color picked by goal_title_index, then the color goes back to
 * g_goal_title_color_by_index[goal_title_index]. Returns the font line height plus
 * 2, plus the extra height flight_text_get_wrap_height_for_string reports for the
 * pieces it measures. Writes no globals of its own. Does not check targetId,
 * goal_status (0 to 5) or amount_op (0 to 19) against their tables. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4150D0
int16_t goals_outputgoal(uint16_t target_id, uint16_t condition,
			 uint16_t target_type, uint16_t goal_status,
			 uint16_t amount_op, uint16_t time_limit5_sec_units,
			 const char *condition_text_override,
			 int percent_complete, int goal_title_index)
{
	uint16_t amount_text_variant =
		g_goal_amount_text_variant_by_op[(uint16_t)amount_op];
	int16_t consumed_height = (int16_t)(g_flight_font_line_height + 2);
	/* From here goal_status holds a row offset into the condition text
	 * tables (47 rows per status block), passed below as the condition row
	 * base. */
	goal_status =
		(uint16_t)(47 * g_goal_status_condition_row_block[goal_status]);

	if (target_type == GOAL_TARGET_FLIGHT_GROUP) {
		if (amount_op == GOAL_AMT_ALL_SPECIAL_CARGO) {
			goals_draw_object_type_name(
				g_mission_flight_groups[target_id]
					.fg.craft_type,
				0, condition_text_override != NULL);
			g_flight_draw_char_fn(' ');
			flight_text_draw_string(
				g_mission_flight_groups[target_id].fg.name);
			g_flight_draw_char_fn(' ');
			if (mission_is_special_cargo_inspected(
				    target_id,
				    g_mission_flight_groups[target_id]
					    .fg.special_cargo_craft) != 0) {
				g_flight_draw_char_fn(
					g_mission_flight_groups[target_id]
						.fg.special_cargo_craft +
					'1');
			} else {
				g_flight_draw_char_fn('?');
			}
			flight_text_draw_string(": ");
			if (condition_text_override != NULL) {
				consumed_height =
					(int16_t)(consumed_height +
						  flight_text_get_wrap_height_for_string(
							  condition_text_override));
				flight_text_draw_string(
					condition_text_override);
			} else {
				consumed_height =
					(int16_t)(consumed_height +
						  goals_draw_condition_text(
							  g_mission_flight_groups
								  [target_id]
									  .fg
									  .craft_type,
							  condition,
							  amount_text_variant,
							  (int16_t)
								  goal_status));
			}
		} else if (amount_op == GOAL_AMT_ALL_NON_SPECIAL) {
			goals_draw_object_type_name(
				g_mission_flight_groups[target_id]
					.fg.craft_type,
				0, condition_text_override != NULL);
			g_flight_draw_char_fn(' ');
			flight_text_draw_string(
				g_mission_flight_groups[target_id].fg.name);
			g_flight_draw_char_fn(' ');
			if (mission_is_special_cargo_inspected(
				    target_id,
				    g_mission_flight_groups[target_id]
					    .fg.special_cargo_craft) != 0) {
				g_flight_draw_char_fn(
					g_mission_flight_groups[target_id]
						.fg.special_cargo_craft +
					'1');
			} else {
				g_flight_draw_char_fn('?');
			}
			flight_text_draw_string(": ");
			if (condition_text_override != NULL) {
				consumed_height =
					(int16_t)(consumed_height +
						  flight_text_get_wrap_height_for_string(
							  condition_text_override));
				flight_text_draw_string(
					condition_text_override);
			} else {
				consumed_height =
					(int16_t)(consumed_height +
						  goals_draw_condition_text(
							  g_mission_flight_groups
								  [target_id]
									  .fg
									  .craft_type,
							  condition,
							  amount_text_variant,
							  (int16_t)
								  goal_status));
			}
		} else if (g_mission_fg_stats[target_id]
				   .outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] >
			   1u) {
			int16_t use_plural =
				(amount_op == GOAL_AMT_ALL_BUT_1 ||
				 amount_op == GOAL_AMT_ALL_EXCEPT_PLAYER)
					? 10
					: 0;

			goals_draw_object_type_name(
				g_mission_flight_groups[target_id]
					.fg.craft_type,
				use_plural, condition_text_override != NULL);
			g_flight_draw_char_fn(' ');
			flight_text_draw_string(
				g_mission_flight_groups[target_id].fg.name);
			flight_text_draw_string(": ");
			if (condition_text_override != NULL) {
				consumed_height =
					(int16_t)(consumed_height +
						  flight_text_get_wrap_height_for_string(
							  condition_text_override));
				flight_text_draw_string(
					condition_text_override);
			} else {
				consumed_height =
					(int16_t)(consumed_height +
						  goals_draw_condition_text(
							  g_mission_flight_groups
								  [target_id]
									  .fg
									  .craft_type,
							  condition,
							  amount_text_variant,
							  (int16_t)
								  goal_status));
			}
		} else {
			consumed_height =
				(int16_t)(consumed_height +
					  goals_draw_object_type_name(
						  g_mission_flight_groups
							  [target_id]
								  .fg
								  .craft_type,
						  0,
						  condition_text_override !=
							  NULL));
			g_flight_draw_char_fn(' ');
			flight_text_draw_string(
				g_mission_flight_groups[target_id].fg.name);
			flight_text_draw_string(": ");
			if (condition_text_override != NULL) {
				consumed_height =
					(int16_t)(consumed_height +
						  flight_text_get_wrap_height_for_string(
							  condition_text_override));
				flight_text_draw_string(
					condition_text_override);
			} else {
				consumed_height =
					(int16_t)(consumed_height +
						  goals_draw_condition_text(
							  g_mission_flight_groups
								  [target_id]
									  .fg
									  .craft_type,
							  condition, 9,
							  (int16_t)
								  goal_status));
			}
		}

		if (time_limit5_sec_units != 0) {
			uint16_t time_seconds =
				(uint16_t)(5 * time_limit5_sec_units);
			uint16_t minutes = (uint16_t)(time_seconds / 60);
			uint16_t seconds =
				(uint16_t)(time_seconds - minutes * 60);

			char time_text[32];
			if (seconds < 10) {
				sprintf(time_text, " (%s %ld:0%ld)",
					g_str_goal_conjunctions
						[GOAL_CONJ_STR_LESS_THAN],
					(long)minutes, (long)seconds);
			} else {
				sprintf(time_text, " (%s %ld:%ld)",
					g_str_goal_conjunctions
						[GOAL_CONJ_STR_LESS_THAN],
					(long)minutes, (long)seconds);
			}
			consumed_height =
				(int16_t)(consumed_height +
					  flight_text_get_wrap_height_for_string(
						  time_text));
			flight_text_draw_string(time_text);
		}
	} else if (target_type == GOAL_TARGET_GLOBAL_GROUP) {
		if (condition_text_override != NULL) {
			consumed_height =
				(int16_t)(consumed_height +
					  goals_draw_object_type_name(
						  g_mission_flight_groups
							  [target_id]
								  .fg
								  .craft_type,
						  0, 0));
			consumed_height =
				(int16_t)(consumed_height +
					  flight_text_get_wrap_height_for_string(
						  g_mission_flight_groups
							  [target_id]
								  .fg.name));
			flight_text_draw_string(
				g_mission_flight_groups[target_id].fg.name);
			flight_text_draw_string(": ");
			consumed_height =
				(int16_t)(consumed_height +
					  flight_text_get_wrap_height_for_string(
						  condition_text_override));
			flight_text_draw_string(condition_text_override);
		} else {
			uint16_t matching_count = 0;
			uint16_t flight_group_index = 0;

			if ((int16_t)g_mission_header.num_flight_groups > 0) {
				int flight_group_count =
					(int16_t)g_mission_header
						.num_flight_groups;

				do {
					if (g_mission_flight_groups
							    [flight_group_index]
								    .fg
								    .global_group ==
						    target_id &&
					    g_mission_fg_stats[flight_group_index]
							    .arrival_enabled !=
						    0) {
						++matching_count;
					}
					++flight_group_index;
				} while (flight_group_index <
					 flight_group_count);
			}

			flight_group_index = 0;

			if ((int16_t)g_mission_header.num_flight_groups > 0) {
				do {
					if (g_mission_flight_groups
							    [flight_group_index]
								    .fg
								    .global_group ==
						    target_id &&
					    g_mission_fg_stats[flight_group_index]
							    .arrival_enabled !=
						    0) {
						if (g_mission_flight_groups
							    [flight_group_index]
								    .fg
								    .number_of_craft >
						    1) {
							consumed_height =
								(int16_t)(consumed_height +
									  goals_draw_object_type_name(
										  g_mission_flight_groups
											  [flight_group_index]
												  .fg
												  .craft_type,
										  0,
										  0));
							consumed_height =
								(int16_t)(consumed_height +
									  flight_text_get_wrap_height_for_string(
										  g_str_goal_conjunctions
											  [GOAL_CONJ_STR_GROUP]));
							flight_text_draw_string(
								g_str_goal_conjunctions
									[GOAL_CONJ_STR_GROUP]);
						} else {
							consumed_height =
								(int16_t)(consumed_height +
									  goals_draw_object_type_name(
										  g_mission_flight_groups
											  [flight_group_index]
												  .fg
												  .craft_type,
										  0,
										  0));
						}
						--matching_count;
						consumed_height =
							(int16_t)(consumed_height +
								  flight_text_get_wrap_height_for_string(
									  g_mission_flight_groups[flight_group_index]
										  .fg
										  .name));
						flight_text_draw_string(
							g_mission_flight_groups
								[flight_group_index]
									.fg
									.name);

						if (matching_count == 1) {
							int16_t separator_height = flight_text_get_wrap_height_for_string(
								g_str_goal_conjunctions
									[GOAL_CONJ_STR_AND]);
							consumed_height =
								(int16_t)(consumed_height +
									  separator_height);
							if (separator_height ==
							    0) {
								g_flight_draw_char_fn(
									' ');
							}
							flight_text_draw_string(
								g_str_goal_conjunctions
									[GOAL_CONJ_STR_AND]);
						} else if (matching_count > 1) {
							flight_text_draw_string(
								g_str_goal_conjunctions
									[GOAL_CONJ_STR_COMMA]);
						}
					}
					++flight_group_index;
				} while ((int16_t)g_mission_header
						 .num_flight_groups >
					 flight_group_index);
			}
			flight_text_draw_string(": ");
			consumed_height =
				(int16_t)(consumed_height +
					  goals_draw_condition_text(
						  g_mission_flight_groups
							  [target_id]
								  .fg
								  .craft_type,
						  condition,
						  amount_text_variant,
						  (int16_t)goal_status));
		}
	} else {
		switch (target_type) {
		case GOAL_TARGET_SPECIES:
			consumed_height =
				(int16_t)(consumed_height +
					  goals_draw_object_type_name(
						  (uint16_t)(target_id + 1), 1,
						  0));
			flight_text_draw_string(": ");
			break;

		case GOAL_TARGET_GENUS:
			flight_text_draw_string(
				g_str_goal_genus_names
					[g_genus_convert[target_id]]);
			flight_text_draw_string(": ");
			break;

		case GOAL_TARGET_FAMILY:
			flight_text_draw_string(
				g_str_goal_family_names
					[g_family_convert[target_id]]);
			flight_text_draw_string(": ");
			break;

		case GOAL_TARGET_IFF:
			if (target_id >= 2) {
				uint16_t name_offset =
					(uint16_t)(g_mission_header.iff_names
							   [target_id - 2][0] ==
						   '1');
				flight_text_draw_string(
					&g_mission_header
						 .iff_names[target_id - 2]
							   [name_offset]);
				flight_text_draw_string(
					g_str_goal_sides[GOAL_SIDE_STR_CRAFT]);
			} else {
				flight_text_draw_string(
					g_str_goal_sides[target_id]);
			}
			flight_text_draw_string(": ");
			break;

		case GOAL_TARGET_CRAFT_WHEN:
		case GOAL_TARGET_STATUS:
			consumed_height =
				(int16_t)(consumed_height +
					  goals_draw_condition_text(
						  (uint16_t)(target_id + 1), 0,
						  amount_text_variant,
						  (int16_t)goal_status));
			break;

		case GOAL_TARGET_AI_LEVEL:
			consumed_height =
				(int16_t)(consumed_height +
					  flight_text_get_wrap_height_for_string(
						  g_str_goal_escape[target_id -
								    1]));
			flight_text_draw_string(
				g_str_goal_escape[target_id - 1]);
			break;

		default:
			break;
		}
		consumed_height =
			(int16_t)(consumed_height +
				  goals_draw_condition_text(
					  (uint16_t)(target_id + 1), condition,
					  amount_text_variant,
					  (int16_t)goal_status));
	}

	if (percent_complete >= 0) {
		char percent_text[76];
		sprintf(percent_text, " (%ld%%)", (long)percent_complete);
		if (goal_title_index == 0 || goal_title_index == 2) {
			flight_text_set_color(0x4A);
		} else if (goal_title_index == 3 || goal_title_index == 1) {
			flight_text_set_color(0x52);
		} else {
			flight_text_set_color(0x43);
		}
		consumed_height =
			(int16_t)(consumed_height +
				  flight_text_get_wrap_height_for_string(
					  percent_text));
		flight_text_draw_string(percent_text);
		flight_text_set_color(
			g_goal_title_color_by_index[goal_title_index]);
	}
	g_flight_draw_char_fn('\n');
	return consumed_height;
}

/* Draws the condition text for a craft species and returns
 * flight_text_get_wrap_height_for_string's extra height for it. The row is
 * condition plus condition_row_base; the column is amount_text_variant, or 0 when
 * the condition has one wording in g_goal_condition_text_variant_count. A species
 * with a model index takes the table of its g_craft_gender; species
 * CRAFT_SPECIES_COMM_SAT_1 to CRAFT_SPECIES_NAV_BUOY_TYPE_2 with none take
 * g_str_goal_cond_masculine. Any other species draws nothing and returns 0. Does
 * not check condition (0 to 47) or that the chosen entry was loaded. */
// FUNCTION: XVT 0x415A70
int16_t goals_draw_condition_text(unsigned int craft_species,
				  uint16_t condition,
				  uint16_t amount_text_variant,
				  int16_t condition_row_base)
{
	const char *text = NULL;
	int16_t wrap_height = 0;
	if (g_goal_condition_text_variant_count[condition] == 1) {
		amount_text_variant = 0;
	}

	condition = (uint16_t)(condition + condition_row_base);
	model_index model_index = get_model_index_from_type(craft_species);
	if (model_index != MODEL_INDEX_NONE) {
		switch (g_craft_gender[model_index]) {
		case CRAFT_GENDER_MASCULINE:
			text = g_str_goal_cond_masculine[condition]
							[amount_text_variant];
			break;
		case CRAFT_GENDER_FEMININE:
			text = g_str_goal_cond_feminine[condition]
						       [amount_text_variant];
			break;
		case CRAFT_GENDER_NEUTERED:
			text = g_str_goal_cond_neutered[condition]
						       [amount_text_variant];
			break;
		}

		wrap_height = flight_text_get_wrap_height_for_string(text);
		flight_text_draw_string(text);
		return wrap_height;
	}

	if ((uint16_t)craft_species >= CRAFT_SPECIES_COMM_SAT_1 &&
	    (uint16_t)craft_species <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
		text = g_str_goal_cond_masculine[condition]
						[amount_text_variant];
		wrap_height = flight_text_get_wrap_height_for_string(text);
		flight_text_draw_string(text);
	}
	return wrap_height;
}

/* Draws a craft species' name and returns flight_text_get_wrap_height_for_string's
 * extra height for it. A species with a model index draws its plural name
 * when use_plural_name is nonzero, else its short name when use_short_name is
 * nonzero, else its long name; species CRAFT_SPECIES_COMM_SAT_1 to
 * CRAFT_SPECIES_NAV_BUOY_TYPE_2 with none draw their
 * g_str_sat_mine_probe_buoy_pilot_names entry. For any other species the modern
 * build draws an empty string; the original build leaves the name pointer
 * unset. */
// FUNCTION: XVT 0x415BA0
int16_t goals_draw_object_type_name(uint16_t craft_species,
				    int16_t use_plural_name,
				    int16_t use_short_name)
{
	const char *display_name;

#ifdef XVT_MODERN
	display_name = "";
#endif
	model_index model_index = get_model_index_from_type(craft_species);
	if (model_index != MODEL_INDEX_NONE) {
		if (use_plural_name != 0) {
			display_name = g_str_species_names_plural[model_index];
		} else if (use_short_name != 0) {
			display_name = g_model_defs[model_index].name;
		} else {
			display_name = g_model_defs[model_index].name_long;
		}
	} else if (craft_species >= CRAFT_SPECIES_COMM_SAT_1 &&
		   craft_species <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
		display_name = g_str_sat_mine_probe_buoy_pilot_names
			[craft_species - CRAFT_SPECIES_COMM_SAT_1];
	}

	int16_t wrap_height =
		flight_text_get_wrap_height_for_string(display_name);
	flight_text_draw_string(display_name);
	return wrap_height;
}
