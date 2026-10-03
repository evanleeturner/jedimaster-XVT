#include "xvt/flight/hud/msg.h"
#include "xvt/assets/file.h"

#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/trig2.h"
#include "xvt/util/memory.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Index in g_message_log_records of the newest logged message, 0 to 299, or
 * 0xFFFF before the first. Flight start sets 0xFFFF: flight_main_loop in the
 * original build, xvt_flight_loading_mission_setup in the modern one. Only
 * msg_emit_in_flight_message advances it, back to 0 after 299. */
// GLOBAL: XVT 0x5235D0
uint16_t g_message_log_write_index;
/* Messages logged since flight start, not wrapped at 300; only
 * msg_emit_in_flight_message raises it. Flight start sets 0: flight_main_loop in
 * the original build, xvt_flight_loading_mission_setup in the modern one.
 * mfd_draw_message_log_page redraws when it changes. */
// GLOBAL: XVT 0x5235D4
uint16_t g_message_log_total_count;
/* Set to 1 by msg_emit_in_flight_message, its only writer, when
 * g_message_log_write_index first wraps; the message log page then offers all
 * 300 records. Nothing sets it back to 0, so it carries into later flights
 * of the same run. */
// GLOBAL: XVT 0x5235D8
uint16_t g_message_log_wrapped = 0;
/* Arguments of the next in-flight message, taken in order by its '*' and '&'
 * marks. A value below 0x8000 is the number for '&', or for '*' a message
 * whose text it inserts; slot + 0x8000, set by msg_add_message_ptr, makes '*'
 * insert the text at g_msg_ptrs[slot]. Many functions write it, chiefly the
 * msg functions and flight_process_player_actions, just before they emit. */
// GLOBAL: XVT 0xA07BE0
uint16_t g_msg_arg_table[4];
/* Second name buffer for messages that name two objects: paiman_boardmaneuver
 * and player_handle_hyperspace_command write a name into it with
 * msg_format_object_name and pass it as argument 1. */
// GLOBAL: XVT 0x9EC4D0
char g_flight_secondary_object_name_buffer[256] = {0};
/* Text for '*' arguments marked slot + 0x8000 in g_msg_arg_table; only
 * msg_add_message_ptr writes it. A model definition or flight group passed
 * here reads as its name, the first field of each. */
// GLOBAL: XVT 0xA08120
static const void *g_msg_ptrs[4];
/* The message log: a ring of 300 records in the memory of
 * g_message_log_handle, MESSAGE_LOG_BUFFER_BYTES (32,000) long. Set by locking
 * the handle in msg_emit_in_flight_message and mfd_draw_message_log_page;
 * msg_emit_in_flight_message writes each logged message at
 * g_message_log_write_index. */
// GLOBAL: XVT 0x9993FC
struct hud_in_flight_message_record *g_message_log_records = NULL;
/* IFF of the next message's sender, copied into its sender_iff. Many
 * functions write it, chiefly the msg functions and
 * flight_process_player_actions; nothing resets it, so a message whose caller
 * does not set it takes the last sender's. */
// GLOBAL: XVT 0xA08292
uint16_t g_msg_sender_iff = 0;
/* By target designation, the offset of its message from
 * IFMSG_309_TARGET_DESCRIPTION: 1 when msg_build_target_description, its only
 * reader, puts "Our", "Friendly" or "Enemy" before it. */
// GLOBAL: XVT 0x5240B8
static const uint8_t g_target_desc_designation_uses_relation_text[24] = {
	0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0};
/* Voice sound id that msg_emit_in_flight_message stores in messages 196 and 207
 * (IFMSG_196_CODE_02_ARGUMENT, IFMSG_207_CODE_01_ARGUMENT);
 * hud_show_flight_message_pane plays it when such a message is first shown.
 * Only mission_update_logic writes it. */
// GLOBAL: XVT 0x9D7684
uint16_t g_pending_hud_message_voice_sfx_id = 0;
/* In-flight message templates by in_flight_message_id, filled by
 * string_table_load_game_strings. The first byte is the pane type; '*' inserts
 * an argument's text, '&' and a count byte an argument's number, and '[' and
 * ']' switch the text color when the message is drawn. */
// GLOBAL: XVT 0x9A1840
const char *g_str_in_flight_messages[417] = {0};

/* Appends the message log to the first of msglog0.txt to msglog99.txt that
 * does not exist yet, or to msglog99.txt when all do; the modern build looks
 * in the player's files. Writes records 0 to g_message_log_write_index - 1 of
 * g_message_log_records, one line each: the text without its pane type byte
 * (and, after type 1, a digit 0 to 3), a tab, and the mission clock as
 * hours:minutes:seconds. Called at a wrap it writes all 300; called
 * otherwise it leaves out the newest record, at g_message_log_write_index. Does
 * nothing when no file opens. Every existing file it opens to test, but
 * msglog99.txt, stays open. Does not check for the 0xFFFF index before the
 * first logged message, which makes it read 65,535 records. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x450550
void msg_write_message_log_file(void)
{
	char file_name[16];
	int log_index;
	xvt_file *stream;
	int message_index;
	int record_offset;
	struct hud_in_flight_message_record *record;
	int prefix;
	char *text;

	log_index = 0;
	do {
		sprintf(file_name, "msglog%ld.txt", (long)log_index);
#ifdef XVT_MODERN
		stream = xvt_storage_open_root(AERON_VFS_ROOT_USER, file_name,
					       "r");
#else
		stream = FILE_RAW_OPEN(file_name, "r");
#endif
		if (stream == NULL) {
			stream = FILE_RAW_OPEN(file_name, "a");
			break;
		}
		if (log_index == 99) {
			if (stream != NULL) {
				FILE_RAW_CLOSE(stream);
			}
			stream = FILE_RAW_OPEN(file_name, "a");
			break;
		}
		++log_index;
	} while (log_index < 100);

	message_index = 0;
	if (stream != NULL) {
		if (g_message_log_write_index > (uint16_t)message_index) {
			record_offset = 0;
			do {
				record =
					(struct hud_in_flight_message_record
						 *)((uint8_t *)
							    g_message_log_records +
						    record_offset);
				prefix = record->text[0];
				text = record->text;
				if (prefix < 9) {
					++text;
					if (prefix == 1 && *text >= '0' &&
					    *text <= '3') {
						++text;
					}
				}
				record_offset += sizeof(*record);
				++message_index;
				FILE_PRINTF(stream, "%s\t%ld:%ld:%ld\n", text,
					    (long)record->clock_hour,
					    (long)record->clock_minute,
					    (long)record->clock_second);
			} while ((uint16_t)g_message_log_write_index >
				 message_index);
		}
		FILE_RAW_CLOSE(stream);
	}
}

/* Builds in-flight message messageId from its template and the arguments in
 * g_msg_arg_table, logs it, and hands it to a HUD pane. Does nothing when
 * g_flight_sim_side_effects_suppressed is set or player_idx is not g_local_player.
 * The record takes the mission clock (the countdown clock when the mission
 * has a time limit, else the elapsed clock), g_msg_sender_iff, and for
 * messages 196 and 207 g_pending_hud_message_voice_sfx_id. The template's first
 * byte is the pane type and, below 9, stays as the text's first character;
 * '*' inserts the next argument's text, and '&' with a count byte its value
 * in that many places, dropping zeros in front. Text past 69 characters is
 * cut. The modern build swaps in its own text for message 1, the pause
 * notice. A pane type of 9 or more becomes 6.
 *
 * Types 1 and 2 enter the message log unless g_replay_view_mode is set:
 * g_message_log_total_count rises and g_message_log_write_index steps on; at 300
 * it goes back to 0 and g_message_log_wrapped to 1, after
 * msg_write_message_log_file when g_radio_message_backup_enabled is set. Types 3,
 * 4 and 7 replace g_system_message_pane when system messages are on (or this
 * is message 400) and the pane is empty, holds no type 4, or the new one is
 * type 7; else they are dropped. Type 8 replaces g_flight_group_message_pane.
 * Other types go to g_ready_message_pane_queue: an empty slot 0 takes the
 * message and shows it; else, by the type in slot 0, it waits behind it (at
 * most 9 wait; past that it is lost) or takes slot 0 and is shown, after
 * hud_shift_ready_message_queue_for_replacement when slot 0 holds a type 1, 2 or
 * 5. Behind a type 0 it is dropped. Does not check argument slots against
 * the 4-entry tables or an '&' count against g_flight_text_decimal_divisors. */
// FUNCTION: XVT 0x450650
void msg_emit_in_flight_message(in_flight_message_id message_id, int player_idx)
{
	struct hud_in_flight_message_record message;
	const uint8_t *template_cursor;
	const char *argument_text;
	uint16_t text_length;
	uint16_t argument_index;
	uint16_t argument_value;
	uint16_t digit_count;
	uint16_t divisor;
	uint16_t digit_value;
	uint16_t output_char;
	uint16_t remainder;
	uint8_t pane_type;
	uint16_t normalized_pane_type;
	uint8_t new_queue_count;
	int digit_started;

	if (g_flight_sim_side_effects_suppressed != 0 ||
	    player_idx != g_local_player) {
		return;
	}

	message.state_or_message_id = (uint16_t)message_id;
	if (g_flight_mission_state.mission_time_limit_minutes != 0) {
		message.clock_subsecond_ticks =
			(uint16_t)g_mission_countdown_clock.subsecond_ticks;
		message.clock_second = g_mission_countdown_clock.seconds;
		message.clock_minute = g_mission_countdown_clock.minutes;
		message.clock_hour = g_mission_countdown_clock.hours;
	} else {
		message.clock_subsecond_ticks =
			(uint16_t)g_mission_elapsed_clock.subsecond_ticks;
		message.clock_second = g_mission_elapsed_clock.seconds;
		message.clock_minute = g_mission_elapsed_clock.minutes;
		message.clock_hour = g_mission_elapsed_clock.hours;
	}
	message.age_seconds = 0;
	message.show_count = 0;
	message.sender_iff = g_msg_sender_iff;
	if (message_id == IFMSG_207_CODE_01_ARGUMENT ||
	    message_id == IFMSG_196_CODE_02_ARGUMENT) {
		message.voice_sfx_id = g_pending_hud_message_voice_sfx_id;
	} else {
		message.voice_sfx_id = 0;
	}

	text_length = 0;
	argument_index = 0;
	template_cursor = (const uint8_t *)g_str_in_flight_messages[message_id];
	pane_type = *template_cursor;
#ifdef XVT_MODERN
	if (message_id == IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE) {
		message.text[text_length++] = (char)pane_type;
		template_cursor =
			(const uint8_t
				 *)"Mission paused. Press your pause key or button to continue.";
	}
#endif
	while (*template_cursor != '\0' && text_length < sizeof(message.text)) {
		if (*template_cursor == '*') {
			++template_cursor;
			argument_value = g_msg_arg_table[argument_index++];
			if (argument_value < 0x8000) {
				argument_text = g_str_in_flight_messages
					[argument_value];
			} else {
				argument_text = (const char *)
					g_msg_ptrs[argument_value & 0x7FFF];
			}
			while (*argument_text != '\0' &&
			       text_length < sizeof(message.text)) {
				message.text[text_length++] = *argument_text++;
			}
		} else if (*template_cursor == '&') {
			digit_count = template_cursor[1];
			template_cursor += 2;
			digit_started = 0;
			remainder = g_msg_arg_table[argument_index++];
			while (digit_count != 0 &&
			       text_length < sizeof(message.text)) {
				divisor = g_flight_text_decimal_divisors
					[digit_count];
				digit_value = remainder / divisor;
				remainder %= divisor;
				if (digit_started != 0 || digit_count <= 1 ||
				    digit_value != 0) {
					digit_started = 1;
					if (digit_value > 9) {
						digit_value = 9;
					}
					output_char = digit_value + '0';
				} else {
					output_char = ' ';
				}
				if (output_char != ' ') {
					message.text[text_length++] =
						(char)output_char;
				}
				--digit_count;
			}
		} else {
			message.text[text_length++] = (char)*template_cursor++;
		}
	}
	if (text_length >= sizeof(message.text)) {
		message.text[sizeof(message.text) - 1] = '\0';
	} else {
		message.text[text_length] = '\0';
	}

	message.pane_type = pane_type < 9 ? pane_type : 6;
	normalized_pane_type = message.pane_type;
	if (g_replay_view_mode == 0 &&
	    (normalized_pane_type == 2 || normalized_pane_type == 1)) {
		++g_message_log_total_count;
		if (++g_message_log_write_index == 300) {
			if (g_radio_message_backup_enabled != 0) {
				msg_write_message_log_file();
			}
			g_message_log_write_index = 0;
			g_message_log_wrapped = 1;
		}
		g_message_log_records = (struct hud_in_flight_message_record *)
			memory_get_handle_block(g_message_log_handle);
		memory_handle_block_done_stub(g_message_log_handle);
		g_message_log_records[g_message_log_write_index] = message;
	}

	if (normalized_pane_type == 3 || normalized_pane_type == 4 ||
	    normalized_pane_type == 7) {
		if ((g_system_message_display_enabled != 0 ||
		     message_id ==
			     IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF) &&
		    (g_system_message_pane.state_or_message_id == UINT16_MAX ||
		     g_system_message_pane.pane_type != 4 ||
		     normalized_pane_type == 7)) {
			g_system_message_pane = message;
			hud_show_flight_message_pane(
				(int16_t)normalized_pane_type);
		}
		return;
	}
	if (normalized_pane_type == 8) {
		g_flight_group_message_pane = message;
		hud_show_flight_message_pane((int16_t)normalized_pane_type);
		return;
	}
	if (g_ready_message_pane_queue[0].state_or_message_id == UINT16_MAX) {
		g_ready_message_pane_queue[0] = message;
		hud_show_flight_message_pane((int16_t)normalized_pane_type);
		return;
	}

	switch (g_ready_message_pane_queue[0].pane_type) {
	case 1:
		if (normalized_pane_type != 2 && normalized_pane_type != 1) {
			hud_shift_ready_message_queue_for_replacement();
			g_ready_message_pane_queue[0] = message;
			hud_show_flight_message_pane(
				(int16_t)normalized_pane_type);
			break;
		}
		g_ready_message_pane_queue[g_ready_message_queue_count + 1] =
			message;
		new_queue_count = g_ready_message_queue_count + 1;
		g_ready_message_queue_count = new_queue_count;
		if (new_queue_count >= 10) {
			g_ready_message_queue_count = new_queue_count - 1;
		}
		break;

	case 2:
	case 5:
		if (normalized_pane_type == 2) {
			g_ready_message_pane_queue[g_ready_message_queue_count +
						   1] = message;
			new_queue_count = g_ready_message_queue_count + 1;
			g_ready_message_queue_count = new_queue_count;
			if (new_queue_count >= 10) {
				g_ready_message_queue_count =
					new_queue_count - 1;
			}
		} else {
			hud_shift_ready_message_queue_for_replacement();
			g_ready_message_pane_queue[0] = message;
			hud_show_flight_message_pane(
				(int16_t)normalized_pane_type);
		}
		break;

	case 3:
	case 6:
	case 7:
	case 8:
		g_ready_message_pane_queue[0] = message;
		hud_show_flight_message_pane((int16_t)normalized_pane_type);
		break;

	case 4:
		if (normalized_pane_type == 2 || normalized_pane_type == 1) {
			g_ready_message_pane_queue[g_ready_message_queue_count +
						   1] = message;
			new_queue_count = g_ready_message_queue_count + 1;
			g_ready_message_queue_count = new_queue_count;
			if (new_queue_count >= 10) {
				g_ready_message_queue_count =
					new_queue_count - 1;
			}
		} else {
			g_ready_message_pane_queue[0] = message;
			hud_show_flight_message_pane(
				(int16_t)normalized_pane_type);
		}
		break;

	default:
		break;
	}
}

/* Announces flight group flight_group_index's arrival to the local player. Finds
 * where it is with mission_resolve_object_or_mission_point_world_loc: for arrival
 * method 0 with 0x8000, else with the group's craft in the active region that
 * follows no other; with none found, the last resolved place stands. Takes the
 * distance from the player's craft, or from the player's camera while
 * map_camera_state is not 0, and shows (distance * 161 / 65,536 + 50) / 100 km,
 * at least 1; this leaves trig2_polardistance multiplied by 161. Emits message
 * 114 or 115, new craft alert for one craft or several, when the group's IFF is
 * not the player's, else 235 or 236, entering area. Sets g_msg_sender_iff to the
 * group's IFF and fills the message arguments with the craft count,
 * model_index's long name, the group and the range. */
// FUNCTION: XVT 0x451940
void msg_reportfgcreation(uint16_t flight_group_index, uint16_t model_index)
{
	int flight_group_idx;
	uint16_t object_index;
	struct object_record *object;
	struct craft_data *craft;
	struct object_record *local_player_object;
	uint16_t range_km;
	uint8_t iff;
	uint16_t number_of_craft;
	int distance_hundredths;

	flight_group_idx = flight_group_index;
	if (g_mission_flight_groups[flight_group_idx].fg.arrival_method == 0) {
		mission_resolve_object_or_mission_point_world_loc(
			0x8000, flight_group_index);
	} else {
		object_index = (uint16_t)g_active_region_object_slot_start;
		while (object_index < g_active_region_craft_object_slot_end) {
			object = &g_object_table[object_index];
			if (object->object_type != 0) {
				craft = object->mobj->p_craft;
				if (object->flight_group_idx ==
					    flight_group_index &&
				    craft->leader_obj_idx == UINT8_MAX) {
					mission_resolve_object_or_mission_point_world_loc(
						object_index,
						flight_group_index);
					break;
				}
			}
			++object_index;
		}
	}

	if (g_players[g_local_player].map_camera_state == 0) {
		local_player_object =
			&g_object_table[g_players[g_local_player].object_index];
		trig2_ctop(g_world_loc_x - local_player_object->world_x,
			   g_world_loc_y - local_player_object->world_y,
			   g_world_loc_z - local_player_object->world_z);
	} else {
		trig2_ctop(g_world_loc_x - g_players[g_local_player]
						   .view_state.camera_world_x,
			   g_world_loc_y - g_players[g_local_player]
						   .view_state.camera_world_y,
			   g_world_loc_z - g_players[g_local_player]
						   .view_state.camera_world_z);
	}
	trig2_polardistance *= 161;
	distance_hundredths = (trig2_polardistance >> 16) & 0xFFFF;
	range_km = (uint16_t)((distance_hundredths + 50) / 100);
	if (range_km == 0) {
		range_km = 1;
	}
	iff = g_mission_flight_groups[flight_group_idx].fg.iff;
	number_of_craft =
		g_mission_flight_groups[flight_group_idx].fg.number_of_craft;
	g_msg_arg_table[0] = number_of_craft;
	g_msg_sender_iff = iff;
	if ((uint16_t)g_players[g_local_player].iff != iff) {
		msg_add_message_ptr(1, g_model_defs[model_index].name_long);
		g_msg_arg_table[2] = range_km;
		if (number_of_craft == 1) {
			msg_emit_in_flight_message(
				IFMSG_114_NEW_CRAFT_ALERT_ARG_ARG_AT_ARG_KM,
				g_local_player);
		} else {
			msg_emit_in_flight_message(
				IFMSG_115_NEW_CRAFT_ALERT_ARG_ARG_S_AT_ARG_KM,
				g_local_player);
		}
	} else if (number_of_craft == 1) {
		msg_add_message_ptr(0, g_model_defs[model_index].name_long);
		msg_add_message_ptr(1,
				    &g_mission_flight_groups[flight_group_idx]);
		g_msg_arg_table[2] = range_km;
		msg_emit_in_flight_message(
			IFMSG_235_ARG_ARG_ENTERING_AREA_AT_ARG_KM,
			g_local_player);
	} else {
		msg_add_message_ptr(1, g_model_defs[model_index].name_long);
		msg_add_message_ptr(2,
				    &g_mission_flight_groups[flight_group_idx]);
		g_msg_arg_table[3] = range_km;
		msg_emit_in_flight_message(
			IFMSG_236_ARG_ARG_S_FROM_FG_ARG_ENTERING_AREA_AT_ARG_KM,
			g_local_player);
	}
}

/* Makes argument slot insert the text at value: stores value in g_msg_ptrs and
 * slot + 0x8000 in g_msg_arg_table. Does not check slot against the 4
 * entries. */
// FUNCTION: XVT 0x451BF0
void msg_add_message_ptr(uint16_t slot, const void *value)
{
	g_msg_arg_table[slot] = slot + 0x8000;
	g_msg_ptrs[slot] = value;
}

/* Emits a message naming a craft, its model's short name then its flight
 * group, with its number when hud_mission_fg_get_craft_number_if_shown gives one
 * (message 133, else 134), followed by message msg_template_id's text. Sets
 * g_msg_sender_iff to the object's IFF and fills the message arguments. */
// FUNCTION: XVT 0x451C20
void msg_emit_craft_message(uint16_t obj_idx, struct craft_data *craft,
			    int16_t msg_template_id)
{
	struct object_record *object;
	int flight_group_idx;
	uint16_t craft_number;

	object = &g_object_table[obj_idx];
	flight_group_idx = object->flight_group_idx;
	g_msg_sender_iff = (uint8_t)object->mobj->iff;
	msg_add_message_ptr(0, &g_model_defs[craft->model_index]);
	msg_add_message_ptr(1, &g_mission_flight_groups[flight_group_idx]);
	craft_number = (uint16_t)hud_mission_fg_get_craft_number_if_shown(
		flight_group_idx, craft);
	if (craft_number != 0) {
		g_msg_arg_table[2] = craft_number;
		g_msg_arg_table[3] = (uint16_t)msg_template_id;
		msg_emit_in_flight_message(IFMSG_133_CRAFT_EVENT_WITH_NUMBER,
					   g_local_player);
	} else {
		g_msg_arg_table[2] = (uint16_t)msg_template_id;
		msg_emit_in_flight_message(IFMSG_134_CRAFT_EVENT_WITHOUT_NUMBER,
					   g_local_player);
	}
}

/* Shows a wingman's acknowledgment of a command and has it spoken. Sets
 * g_msg_sender_iff to the sender's flight group's IFF, then does nothing more,
 * speech included, unless that group shares the local player's IFF and team.
 * With multiple_recipients set, emits message 269, acknowledged, with the
 * group and commandId's text; else 147 or 148, Roger, with the sender's
 * model, group and number when shown, and commandId's text. Then calls
 * fsfx_speak_wingman_event with response_index. Takes the model index from
 * byte 4 of sender_craft. */
// FUNCTION: XVT 0x451D00
void msg_radio_message(uint16_t sender_obj_idx, uint8_t *sender_craft,
		       uint16_t command_id, uint16_t response_index,
		       int16_t multiple_recipients)
{
	int flight_group_idx;
	uint16_t craft_number;

	flight_group_idx = g_object_table[sender_obj_idx].flight_group_idx;
	g_msg_sender_iff = g_mission_flight_groups[flight_group_idx].fg.iff;
	if (g_players[g_local_player].iff != g_msg_sender_iff ||
	    g_mission_flight_groups[flight_group_idx].fg.team !=
		    g_players[g_local_player].team) {
		return;
	}
	if (multiple_recipients != 0) {
		msg_add_message_ptr(0,
				    &g_mission_flight_groups[flight_group_idx]);
		g_msg_arg_table[1] = command_id;
		msg_emit_in_flight_message(
			IFMSG_269_MESSAGE_ACKNOWLEDGED_FLIGHT_GROUP_ARG_ARG,
			g_local_player);
	} else {
		msg_add_message_ptr(0, &g_model_defs[sender_craft[4]]);
		msg_add_message_ptr(1,
				    &g_mission_flight_groups[flight_group_idx]);
		craft_number =
			(uint16_t)hud_mission_fg_get_craft_number_if_shown(
				flight_group_idx,
				(struct craft_data *)sender_craft);
		if (craft_number != 0) {
			g_msg_arg_table[2] = craft_number;
			g_msg_arg_table[3] = command_id;
			msg_emit_in_flight_message(
				IFMSG_147_ROGER_CRAFT_WITH_NUMBER,
				g_local_player);
		} else {
			g_msg_arg_table[2] = command_id;
			msg_emit_in_flight_message(
				IFMSG_148_ROGER_CRAFT_WITHOUT_NUMBER,
				g_local_player);
		}
	}
	fsfx_speak_wingman_event(g_local_player, sender_obj_idx, 1,
				 response_index, sender_obj_idx, UINT16_MAX);
}

/* As msg_emit_craft_message, with messages 157 and 158, reporting in, and
 * g_msg_sender_iff set from the flight group's IFF instead of the object's. */
// FUNCTION: XVT 0x451E70
void msg_reportmessage(uint16_t obj_idx, struct craft_data *craft,
		       int16_t msg_template_id)
{
	int flight_group_idx;
	uint16_t craft_number;

	flight_group_idx = g_object_table[obj_idx].flight_group_idx;
	g_msg_sender_iff = g_mission_flight_groups[flight_group_idx].fg.iff;
	msg_add_message_ptr(0, &g_model_defs[craft->model_index]);
	msg_add_message_ptr(1, &g_mission_flight_groups[flight_group_idx]);
	craft_number = (uint16_t)hud_mission_fg_get_craft_number_if_shown(
		flight_group_idx, craft);
	if (craft_number != 0) {
		g_msg_arg_table[2] = craft_number;
		g_msg_arg_table[3] = (uint16_t)msg_template_id;
		msg_emit_in_flight_message(IFMSG_157_CRAFT_REPORT_WITH_NUMBER,
					   g_local_player);
	} else {
		g_msg_arg_table[2] = (uint16_t)msg_template_id;
		msg_emit_in_flight_message(
			IFMSG_158_CRAFT_REPORT_WITHOUT_NUMBER, g_local_player);
	}
}

/* Builds the target description of player_idx's target: its name, its
 * designation, and what the player's goals want done with it. Returns 0 at once
 * for an object in the projectile slots. Otherwise fills the message arguments:
 * slot 0 the name, from msg_format_object_name mode 2 into
 * g_flight_text_scratch_buffer; slot 1 "Our", "Friendly" or "Enemy" when
 * g_target_desc_designation_uses_relation_text asks for it; slot 2 the designation's
 * message, from the team's designation table or, when that gives 0, by kind
 * (mine, satellite, probe, nav buoy, wingman, friendly craft, cargo, craft);
 * slot 3 the goal phrase, blank when there is none.
 *
 * The phrase comes from the target group's pending goals for the player's team
 * and the team's global goal triggers that match the group: inspect, else
 * disable (worded for capture or boarding when those apply), else capture,
 * board or destroy. For a craft, an inspection the team has done drops out,
 * capture or boarding of a moving craft asks to disable it, and with a special
 * cargo goal only the special cargo craft gets capture, board, disable or
 * destroy. When an order of the player's flight group whose built-in plan is 19
 * (disable) or 69 (destroy) targets it, the phrase moves to the next message,
 * which tells the player to act, and the target is actionable; inspect always
 * is. With emit_hud_message set for the local player, emits message 309, sets the
 * player's target_description_refresh_timer to 1,180 ticks and
 * g_target_description_message_id to the phrase. Returns the actionable flag when
 * return_actionable_only is set, else the phrase's message id. Does not check a
 * designation from the table against the 24 entries of
 * g_target_desc_designation_uses_relation_text. */
// FUNCTION: XVT 0x451F50
int msg_build_target_description(uint16_t target_obj_idx, int player_idx,
				 int emit_hud_message,
				 int return_actionable_only)
{
	int flight_group_idx;
	int team;
	struct craft_data *craft;
	int inspect_flag;
	int disable_flag;
	int capture_flag;
	int boarded_flag;
	int destroy_flag;
	int special_cargo_relevant;
	int actionable;
	int designation;
	unsigned int goal_index;

	g_msg_arg_table[3] = IFMSG_331_BLANK;
	actionable = 0;
	if ((int)g_projectile_object_slot_start <= target_obj_idx &&
	    (int)g_projectile_object_slot_end > target_obj_idx) {
		return 0;
	}
	if (g_active_region_craft_object_slot_end > target_obj_idx) {
		craft = g_object_table[target_obj_idx].mobj->p_craft;
	} else {
		craft = NULL;
	}
	flight_group_idx = g_object_table[target_obj_idx].flight_group_idx;
	team = g_mission_flight_groups[flight_group_idx].fg.team;
	designation = g_flight_mission_state.runtime.team_fg_designation_code
			      [(uint16_t)g_players[player_idx].team]
			      [flight_group_idx];
	if (designation == 0) {
		if (craft == NULL) {
			if (g_object_table[target_obj_idx].genus_id ==
			    CRAFT_GENUS_MINE) {
				designation = IFMSG_326_MINE -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_object_table[target_obj_idx].object_type >=
					   CRAFT_SPECIES_COMM_SAT_1 &&
				   g_object_table[target_obj_idx].object_type <=
					   CRAFT_SPECIES_SAT_5) {
				designation = IFMSG_327_SATELLITE -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_object_table[target_obj_idx].object_type >=
					   CRAFT_SPECIES_PROBE &&
				   g_object_table[target_obj_idx].object_type <=
					   CRAFT_SPECIES_PROBE_3) {
				designation = IFMSG_328_PROBE -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_object_table[target_obj_idx].object_type >=
					   CRAFT_SPECIES_NAV_BUOY_TYPE_1 &&
				   g_object_table[target_obj_idx].object_type <=
					   CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
				designation = IFMSG_329_NAV_BUOY -
					      IFMSG_309_TARGET_DESCRIPTION;
			}
		} else {
			if (g_players[player_idx].bound_flight_group_idx ==
			    flight_group_idx) {
				designation = IFMSG_323_YOUR_WINGMAN -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (team ==
				   (uint16_t)g_players[player_idx].team) {
				designation = IFMSG_322_FRIENDLY_CRAFT -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_object_table[target_obj_idx].genus_id ==
					   CRAFT_GENUS_FREIGHTER &&
				   craft->ai_flight.max_speed_cache == 0) {
				designation = IFMSG_325_CARGO -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else {
				designation = IFMSG_324_CRAFT -
					      IFMSG_309_TARGET_DESCRIPTION;
			}
		}
	}
	msg_format_object_name(target_obj_idx, 2, g_flight_text_scratch_buffer);
	msg_add_message_ptr(0, g_flight_text_scratch_buffer);
	g_msg_arg_table[1] = IFMSG_331_BLANK;
	if (designation != 0) {
		if (g_target_desc_designation_uses_relation_text[designation] !=
		    0) {
			if (team == (uint16_t)g_players[player_idx].team) {
				g_msg_arg_table[1] = IFMSG_334_OUR;
			} else {
				int current_team =
					g_mission_flight_groups
						[g_object_table[target_obj_idx]
							 .flight_group_idx]
							.fg.team;
				int is_enemy =
					(uint16_t)g_players[player_idx].team !=
						current_team &&
					g_mission_teams[(uint16_t)g_players
								[player_idx]
									.team]
							.allies[current_team] <
						1;
				g_msg_arg_table[1] = IFMSG_333_FRIENDLY;
				if (is_enemy) {
					g_msg_arg_table[1] = IFMSG_332_ENEMY;
				}
			}
		}
		g_msg_arg_table[2] =
			(uint16_t)(designation + IFMSG_309_TARGET_DESCRIPTION);
	} else {
		g_msg_arg_table[2] = IFMSG_331_BLANK;
	}

	inspect_flag = 0;
	destroy_flag = 0;
	disable_flag = 0;
	capture_flag = 0;
	boarded_flag = 0;
	special_cargo_relevant = 0;
	for (goal_index = 0; goal_index < 8; ++goal_index) {
		struct flight_group_goal *goal =
			&g_mission_flight_groups[flight_group_idx]
				 .fg.goals[goal_index];
		int event_condition;
		int player_team = (uint16_t)g_players[player_idx].team;
		if (goal->enabled_teams[player_team] == 0 ||
		    goal->goal_kind != 0 ||
		    g_mission_fg_stats[flight_group_idx]
				    .goal_state[8 * player_team + goal_index] !=
			    4) {
			continue;
		}
		if (goal->amount == GOAL_AMT_ALL_SPECIAL_CARGO) {
			if (g_mission_fg_stats[flight_group_idx]
				    .special_cargo_outcome
					    [FLIGHT_GROUP_OUTCOME_INSPECTED] ==
			    0) {
				inspect_flag = 1;
			}
			special_cargo_relevant = 1;
		}
		event_condition = goal->event_condition;
		if (event_condition == 2) {
			destroy_flag = 1;
		} else if (event_condition != 3) {
			if (event_condition == 8) {
				disable_flag = 1;
			} else if (event_condition == 5) {
				inspect_flag = 1;
			} else if (event_condition == 4 ||
				   event_condition == 44) {
				capture_flag = 1;
			} else if (event_condition == 6) {
				boarded_flag = 1;
			}
		}
	}
	{
		unsigned int pair_offset;
		for (pair_offset = 0;
		     pair_offset < 2 * sizeof(struct mission_trigger_pair);
		     pair_offset += sizeof(struct mission_trigger_pair)) {
			unsigned int trigger_offset;
			for (trigger_offset = 0;
			     trigger_offset <
			     2 * sizeof(struct mission_trigger);
			     trigger_offset += sizeof(struct mission_trigger)) {
				unsigned int trigger_byte_index =
					pair_offset + trigger_offset +
					sizeof(g_mission_global_goals[0]) *
						(uint16_t)g_players[player_idx]
							.team;
				const uint8_t *global_goal_bytes =
					(const uint8_t *)g_mission_global_goals;
				int event_condition = global_goal_bytes
					[trigger_byte_index +
					 offsetof(struct mission_trigger,
						  condition)];
				if (event_condition != 10 &&
				    mission_flight_group_matches_trigger_variable(
					    flight_group_idx,
					    global_goal_bytes
						    [trigger_byte_index +
						     offsetof(struct
							      mission_trigger,
							      variable_type)],
					    global_goal_bytes
						    [trigger_byte_index +
						     offsetof(struct
							      mission_trigger,
							      variable)]) !=
					    0) {
					if (event_condition == 2) {
						destroy_flag = 1;
					} else if (event_condition != 3) {
						if (event_condition == 8) {
							disable_flag = 1;
						} else if (event_condition ==
							   5) {
							inspect_flag = 1;
						} else if (event_condition ==
								   4 ||
							   event_condition ==
								   44) {
							capture_flag = 1;
						} else if (event_condition ==
							   6) {
							boarded_flag = 1;
						}
					}
				}
			}
		}
	}
	if (g_active_region_craft_object_slot_end > target_obj_idx) {
		if (inspect_flag != 0 &&
		    craft->identified_order_by_team
				    [(uint16_t)g_players[player_idx].team] !=
			    0) {
			inspect_flag = 0;
		}
		if (capture_flag != 0 || boarded_flag != 0) {
			if (g_object_table[target_obj_idx].mobj->speed != 0) {
				disable_flag = 1;
			}
			if (special_cargo_relevant != 0 &&
			    g_mission_flight_groups[flight_group_idx]
					    .fg.special_cargo_craft !=
				    craft->craft_ordinal) {
				capture_flag = 0;
				boarded_flag = 0;
			}
		}
		if (disable_flag != 0 && special_cargo_relevant != 0 &&
		    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft !=
			    craft->craft_ordinal) {
			disable_flag = 0;
		}
		if (destroy_flag != 0 && special_cargo_relevant != 0 &&
		    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft !=
			    craft->craft_ordinal) {
			destroy_flag = 0;
		}
	}
	if (inspect_flag != 0) {
		g_msg_arg_table[3] = IFMSG_344_INSPECT_IT;
		actionable = 1;
	} else if (disable_flag != 0) {
		if (capture_flag != 0) {
			g_msg_arg_table[3] = IFMSG_341_TO_BE_CAPTURED;
		} else if (boarded_flag != 0) {
			g_msg_arg_table[3] = IFMSG_345_TO_BE_BOARDED;
		} else {
			g_msg_arg_table[3] = IFMSG_347_OTHERS_WILL_DISABLE_IT;
		}
		if (pai_setup_context_and_find_order_plan_on_target(
			    g_players[player_idx].object_index, 19,
			    target_obj_idx) == 1) {
			++g_msg_arg_table[3];
			actionable = 1;
		}
	} else if (capture_flag != 0) {
		g_msg_arg_table[3] = IFMSG_341_TO_BE_CAPTURED;
	} else if (boarded_flag != 0) {
		g_msg_arg_table[3] = IFMSG_345_TO_BE_BOARDED;
	} else if (destroy_flag != 0) {
		g_msg_arg_table[3] = IFMSG_337_OTHERS_WILL_DESTROY_IT;
		if (pai_setup_context_and_find_order_plan_on_target(
			    g_players[player_idx].object_index, 69,
			    target_obj_idx) == 1) {
			++g_msg_arg_table[3];
			actionable = 1;
		}
	}
	if (emit_hud_message != 0 && player_idx == g_local_player) {
		msg_emit_in_flight_message(IFMSG_309_TARGET_DESCRIPTION,
					   player_idx);
		g_player_flight_transient_timers[g_local_player]
			.target_description_refresh_timer = 1180;
		g_target_description_message_id = g_msg_arg_table[3];
	}
	if (return_actionable_only != 0) {
		return actionable;
	}
	return g_msg_arg_table[3];
}

/* Writes an object's display name into out_name, emptying it first. For a
 * craft: its model's long name (name_mode 1) or short name (0), its flight
 * group's name when it has one, and its number when
 * hud_mission_fg_get_craft_number_if_shown gives one, space separated; other
 * modes leave out the model. Another mobile object gets its warhead name
 * (types 0x8F to 0x9B) or satellite, mine, probe or buoy name, else nothing.
 * An object with no mobile object gets its group's name in modes other than
 * 0 and 1, else the satellite-to-buoy name for its type and the group's
 * name. Does not check out_name's size, or the type of an object with no
 * mobile object. */
// FUNCTION: XVT 0x4525A0
void msg_format_object_name(uint16_t obj_idx, uint16_t name_mode,
			    char *out_name)
{
	int16_t name_part_count;
	int object_index;
	struct object_record *object;
	struct mobile_object *mobile_object;
	uint16_t object_type;
	struct craft_data *craft;
	uint16_t flight_group_idx;
	struct mission_flight_group *flight_group;
	uint16_t craft_number;

	name_part_count = 0;
	*out_name = '\0';
	object_index = obj_idx;
	object = &g_object_table[object_index];
	mobile_object = object->mobj;
	if (mobile_object != NULL) {
		object_type = object->object_type;
		if (mobile_object->family == 0) {
			craft = mobile_object->p_craft;
			flight_group_idx = object->flight_group_idx;
			if (name_mode == 1) {
				msg_append_string(
					g_model_defs[craft->model_index]
						.name_long,
					out_name);
				name_part_count = 1;
			} else if (name_mode == 0) {
				msg_append_string(
					g_model_defs[craft->model_index].name,
					out_name);
				name_part_count = 1;
			}

			flight_group =
				&g_mission_flight_groups[flight_group_idx];
			if (flight_group->fg.name[0] != '\0') {
				if (name_part_count != 0) {
					name_part_count = 0;
					msg_append_char(' ', out_name);
				}
				++name_part_count;
				msg_append_string(flight_group->fg.name,
						  out_name);
			}

			craft_number = (uint16_t)
				hud_mission_fg_get_craft_number_if_shown(
					flight_group_idx, craft);
			if (craft_number != 0) {
				if (name_part_count != 0) {
					msg_append_char(' ', out_name);
				}
				if (craft_number >= 10) {
					msg_append_char(craft_number / 10 + '0',
							out_name);
					msg_append_char(craft_number % 10 + '0',
							out_name);
					return;
				}
				msg_append_char(craft_number + '0', out_name);
			}
			return;
		}

		if (object_type >= 0x8f && object_type <= 0x9b) {
			msg_append_string(
				g_str_warhead_names[object_type - 0x8f],
				out_name);
		} else if (object_type >= CRAFT_SPECIES_COMM_SAT_1 &&
			   object_type <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
			msg_append_string(g_str_sat_mine_probe_buoy_pilot_names
						  [object_type -
						   CRAFT_SPECIES_COMM_SAT_1],
					  out_name);
		}
		return;
	}

	if (name_mode != 1 && name_mode != 0) {
		msg_append_string(
			g_mission_flight_groups[object->flight_group_idx]
				.fg.name,
			out_name);
		return;
	}

	msg_append_string(
		g_str_sat_mine_probe_buoy_pilot_names[object->object_type -
						      CRAFT_SPECIES_COMM_SAT_1],
		out_name);
	flight_group = &g_mission_flight_groups[g_object_table[object_index]
							.flight_group_idx];
	if (flight_group->fg.name[0] != '\0') {
		msg_append_char(' ', out_name);
		msg_append_string(
			g_mission_flight_groups[g_object_table[object_index]
							.flight_group_idx]
				.fg.name,
			out_name);
	}
}

/* Appends source to the string in destination; does not check its size. */
// FUNCTION: XVT 0x452830
void msg_append_string(const char *source, char *destination)
{
	while (*destination != '\0') {
		destination++;
	}
	while (*source != '\0') {
		*destination++ = *source++;
	}
	*destination = '\0';
}

/* Appends ch to the string in destination; does not check its size. */
// FUNCTION: XVT 0x452860
void msg_append_char(char ch, char *destination)
{
	while (*destination != '\0') {
		++destination;
	}
	*destination = ch;
	destination[1] = '\0';
}

/* Emits messageId with arguments naming the local player's craft: its
 * model's short name in slot 0, its flight group in slot 1 and its number,
 * 0 when not shown, in slot 2. Sets g_msg_sender_iff to the craft's IFF. */
// FUNCTION: XVT 0x452880
void msg_emit_local_player_craft_message(in_flight_message_id message_id)
{
	int object_index;

	object_index = (int)g_players[g_local_player].object_index;
	g_msg_sender_iff = (uint8_t)g_object_table[object_index].mobj->iff;
	msg_add_message_ptr(0,
			    &g_model_defs[g_object_table[object_index]
						  .mobj->p_craft->model_index]);
	msg_add_message_ptr(
		1, &g_mission_flight_groups[g_object_table[object_index]
						    .flight_group_idx]);
	g_msg_arg_table[2] = (uint16_t)hud_mission_fg_get_craft_number_if_shown(
		g_object_table[object_index].flight_group_idx,
		g_object_table[object_index].mobj->p_craft);
	msg_emit_in_flight_message(message_id, g_local_player);
}
