#include "xvt/frontend/briefing_script.h"

#include <string.h>

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission.h"

/* Argument words that follow each briefing script opcode, by opcode 0 to
 * 34. */
// GLOBAL: XVT 0x52CF10
const int16_t g_briefing_script_opcode_arg_counts[35] = {
	0, 0, 1, 0, 1, 1, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0,
	4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* 1 while the briefing map plays: only then does
 * briefing_script_advance_or_reset_at_end move the view and step the script.
 * frontend_mission_init_for_briefing sets 1; the mission setup screen's Stop
 * and Play buttons set 0 and 1. */
// GLOBAL: XVT 0x6691E6
int16_t g_briefing_playback_active = 0;
/* Text block last counted as a page of narration:
 * briefing_map_draw_viewport_and_selection stores slot 1's block here whenever it
 * differs, raising g_briefing_text_page_number. Set to 0, with the page number,
 * whenever the briefing starts over: by frontend_mission_init_for_briefing,
 * briefing_map_update_script_playback_after_animation,
 * briefing_script_advance_to_next_visible_line, and the mission setup screen's
 * Rewind button and its Forward button when that brings no new block. */
// GLOBAL: XVT 0x669218
int g_briefing_last_narrated_text_block_idx = 0;
/* Page number drawn under the briefing map after the FRONTSTR_640_PAGE
 * string: narration blocks shown since the briefing last started over.
 * Raised by briefing_map_draw_viewport_and_selection; set to 0 together with
 * g_briefing_last_narrated_text_block_idx. */
// GLOBAL: XVT 0x66921C
int g_briefing_text_page_number = 0;
/* Per text slot, 1 while it shows a text block: script opcodes 4 and 5 set
 * slot 0 and 1, opcode 3 clears both, and so do briefing_script_reset_state and
 * frontend_mission_init_for_briefing. Only slot 1 is drawn, under the map;
 * briefing_script_advance_to_next_visible_line reads both. */
// GLOBAL: XVT 0x6696DA
int16_t g_briefing_text_slot_active[2] = {0};
/* Per text slot, the index in g_briefing_text_blocks of the block it shows: the
 * argument of opcode 4 or 5. Only briefing_script_advance_frame writes it. */
// GLOBAL: XVT 0x6696DE
int16_t g_briefing_text_slot_block_idx[2] = {0};
/* 1 when the script frame just played held opcode 3, which cleared the text
 * slots; briefing_script_advance_frame sets 0 at the start of every frame. Read
 * by briefing_script_advance_to_next_visible_line. */
// GLOBAL: XVT 0x6696E2
int16_t g_briefing_text_slots_changed = 0;
/* 1 when the script frame just played held opcode 1, a stopping point for
 * briefing_script_advance_to_next_visible_line, its one reader;
 * briefing_script_advance_frame sets 0 at the start of every frame. */
// GLOBAL: XVT 0x669778
int16_t g_briefing_script_pause_marker_reached = 0;

/* The active briefing's script. frontend_mission_init_for_briefing puts in the
 * default from briefing_script_init_default_script, and
 * frontend_mission_load_current_with_briefing copies in the briefing of the
 * pilot's team from the mission file. briefing_script_advance_frame and
 * briefing_script_reset_state move its play position. */
// GLOBAL: XVT 0x669258
struct frontend_briefing_script g_briefing_script;

/* While g_briefing_playback_active is nonzero, moves the map view one step
 * toward its targets and plays one script frame with its sounds, or starts
 * the briefing over once its duration has passed
 * (briefing_map_update_script_playback_after_animation). Ignores frame_counter. The
 * mission setup screen calls it every frame it shows the briefing map. */
// FUNCTION: XVT 0x4F6950
void briefing_script_advance_or_reset_at_end(int frame_counter)
{
	(void)frame_counter;

	if (g_briefing_playback_active != 0) {
		briefing_map_animate_view_state();
		briefing_map_update_script_playback_after_animation();
	}
}

/* Makes g_briefing_script the default script: 200 frames long, header_word06 2,
 * header_word08 0, and one entry, opcode 34 at time 9999, which ends the
 * script. Only words 0 and 1 are written; the rest keep what they held. Then
 * starts it over through briefing_script_reset_state and returns that
 * function's result. */
// FUNCTION: XVT 0x4F7340
int16_t briefing_script_init_default_script(void)
{
	g_briefing_script.duration_frames = 200;
	g_briefing_script.header_word06 = 2;
	g_briefing_script.words[0] = 9999;
	g_briefing_script.words[1] = 34;
	g_briefing_script.current_frame = 0;
	g_briefing_script.cursor_word_index = 0;
	g_briefing_script.header_word08 = 0;
	return briefing_script_reset_state();
}

/* Starts the briefing over: map center and target center (0, 0), scale and
 * target scale 32 on both axes, both text slots, the 8 flight group markers
 * and the 8 labels off, and the script at frame 0 and word 0. Then plays
 * frame 0 through briefing_script_advance_frame(1), without sounds and with
 * markers and labels shown in full, and returns its word index. Leaves
 * g_briefing_last_narrated_text_block_idx and g_briefing_text_page_number alone. */
// FUNCTION: XVT 0x4F7380
int16_t briefing_script_reset_state(void)
{
	int16_t index;

	g_briefing_map_center.x = 0;
	g_briefing_map_center.y = 0;
	g_briefing_map_target_center.x = 0;
	g_briefing_map_target_center.y = 0;
	g_briefing_map_scale.x = 32;
	g_briefing_map_scale.y = 32;
	g_briefing_map_target_scale.x = 32;
	g_briefing_map_target_scale.y = 32;
	for (index = 0; index < 2; ++index) {
		g_briefing_text_slot_active[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefing_map_fg_marker_active[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefing_map_label_active[index] = 0;
	}
	g_briefing_script.current_frame = 0;
	g_briefing_script.cursor_word_index = 0;
	return briefing_script_advance_frame(1);
}

/* Plays the script through frame target_time, passing initialize_state to
 * briefing_script_advance_frame as its apply_instantly flag. Returns 0 and does
 * nothing when the current frame is target_time + 1, so target_time was the
 * last frame played. Otherwise starts over first when target_time is behind
 * the current frame, plays frames until the current frame passes target_time,
 * and returns 1. */
// FUNCTION: XVT 0x4F7420
int16_t briefing_script_advance_until_time(int16_t target_time,
					   int16_t initialize_state)
{
	if (g_briefing_script.current_frame - target_time != 1) {
		if (target_time < g_briefing_script.current_frame) {
			briefing_script_reset_state();
		}
		while (target_time >= g_briefing_script.current_frame) {
			briefing_script_advance_frame(initialize_state);
		}
		return 1;
	}
	return 0;
}

/* The briefing map's Forward button. Starts over and replays at once,
 * without sounds; once it is back at or past the frame it started from, it
 * stops after a frame that held a pause marker (opcode 1) or that first
 * showed a text slot since the slots last changed, plays the next frame with
 * sounds through briefing_script_advance_until_time, and returns 1. When it
 * reaches the end entry (opcode 34) first, it sets
 * g_briefing_last_narrated_text_block_idx and g_briefing_text_page_number to 0,
 * starts over, and returns briefing_script_reset_state's result. */
// FUNCTION: XVT 0x4F7480
int16_t briefing_script_advance_to_next_visible_line(void)
{
	int16_t text_slot_active;
	int16_t visible_text_frames;
	int16_t opcode;
	int16_t slot_index;
	int16_t start_time;
	int16_t done;
	int16_t target_time;
	int16_t target_opcode;

	start_time = g_briefing_script.current_frame;
	text_slot_active = 0;
	visible_text_frames = 0;
	opcode = 0;
	done = 0;
	briefing_script_reset_state();
	do {
		if (opcode == 34) {
			break;
		}
		opcode =
			g_briefing_script
				.words[g_briefing_script.cursor_word_index + 1];
		if (g_briefing_text_slots_changed != 0) {
			visible_text_frames = 0;
			text_slot_active = 0;
		}
		for (slot_index = 0; slot_index < 2; ++slot_index) {
			if (g_briefing_text_slot_active[slot_index] != 0) {
				text_slot_active = 1;
			}
		}
		if (text_slot_active != 0) {
			++visible_text_frames;
		}
		if ((g_briefing_script_pause_marker_reached != 0 ||
		     visible_text_frames == 1) &&
		    start_time <= g_briefing_script.current_frame) {
			done = 1;
		} else {
			briefing_script_advance_frame(1);
		}
	} while (done == 0);

	if (g_briefing_script_pause_marker_reached != 0 ||
	    visible_text_frames == 1) {
		target_time = g_briefing_script.current_frame;
		target_opcode = 0;
	} else {
		target_time =
			g_briefing_script
				.words[g_briefing_script.cursor_word_index];
		target_opcode =
			g_briefing_script
				.words[g_briefing_script.cursor_word_index + 1];
	}
	if (target_opcode == 34) {
		g_briefing_last_narrated_text_block_idx = 0;
		g_briefing_text_page_number = 0;
		return briefing_script_reset_state();
	}
	return briefing_script_advance_until_time(target_time, 0);
}

/* Plays the script frame at g_briefing_script.current_frame, raises
 * current_frame by one, and returns the cursor's new word index. First sets
 * g_briefing_text_slots_changed, g_briefing_map_fg_markers_changed,
 * g_briefing_map_labels_changed, g_briefing_map_center_dirty,
 * g_briefing_map_scale_dirty and g_briefing_script_pause_marker_reached to 0. An
 * entry is a time word, an opcode word and the opcode's argument words, as
 * g_briefing_script_opcode_arg_counts gives them. It reads entries from the cursor
 * while their time is not past the current frame, applies those whose time
 * equals it and skips earlier ones, and leaves the cursor on the first later
 * entry. Opcodes: 1 pause marker; 3 clears both text slots; 4 and 5 show text
 * block args[0] in slot 0 or 1; 6 and 7 set the map center or scale target to
 * (args[0], args[1]), and the current value too when the entry's time is 0 or
 * apply_instantly is set; 8 clears the flight group markers; 9 to 16 show
 * marker opcode - 9 on flight group args[0]; 17 clears the labels; 18 to 25
 * show label opcode - 18 with text args[0] at map point (args[1], args[2])
 * in shade ramp args[3]; any other opcode does nothing. A new marker or
 * label starts at age 0, or 80 with apply_instantly set. Without
 * apply_instantly, a marker plays "sfxTarget2" for an IFF 1 flight group and
 * "sfxTarget1" for others, and a label with text plays "sfxText", each only
 * with datapad sounds on, at 12 times the datapad volume. Checks no opcode,
 * argument or index range. */
// FUNCTION: XVT 0x4F7590
int16_t briefing_script_advance_frame(int16_t apply_instantly)
{
	int16_t cursor_word_index;
	int16_t saved_cursor_word_index;
	int16_t event_time;
	int16_t opcode;
	int16_t args[8];
	int16_t argument_count;
	int16_t argument_index;
	int16_t slot_index;
	int16_t iff;
	char label_text[40];

	cursor_word_index = g_briefing_script.cursor_word_index;
	saved_cursor_word_index = cursor_word_index;
	event_time = g_briefing_script.words[cursor_word_index];
	g_briefing_text_slots_changed = 0;
	g_briefing_map_fg_markers_changed = 0;
	g_briefing_map_labels_changed = 0;
	g_briefing_map_center_dirty = 0;
	g_briefing_map_scale_dirty = 0;
	g_briefing_script_pause_marker_reached = 0;

	if (event_time <= g_briefing_script.current_frame) {
		do {
			saved_cursor_word_index = cursor_word_index;
			event_time =
				g_briefing_script.words[cursor_word_index++];
			opcode = g_briefing_script.words[cursor_word_index++];
			argument_count =
				g_briefing_script_opcode_arg_counts[opcode];
			for (argument_index = 0;
			     argument_index < argument_count;
			     ++argument_index) {
				args[argument_index] =
					g_briefing_script
						.words[cursor_word_index++];
			}

			if (event_time == g_briefing_script.current_frame) {
				switch (opcode) {
				case 1:
					g_briefing_script_pause_marker_reached =
						1;
					break;
				case 3:
					for (slot_index = 0; slot_index < 2;
					     ++slot_index) {
						g_briefing_text_slot_active
							[slot_index] = 0;
					}
					g_briefing_text_slots_changed = 1;
					break;
				case 4:
				case 5:
					slot_index = opcode - 4;
					g_briefing_text_slot_active
						[slot_index] = 1;
					g_briefing_text_slot_block_idx
						[slot_index] = args[0];
					break;
				case 6:
					if (event_time == 0 ||
					    apply_instantly != 0) {
						g_briefing_map_target_center.x =
							args[0];
						g_briefing_map_center.x =
							args[0];
						g_briefing_map_target_center.y =
							args[1];
						g_briefing_map_center.y =
							args[1];
					} else {
						g_briefing_map_target_center.x =
							args[0];
						g_briefing_map_target_center.y =
							args[1];
					}
					g_briefing_map_center_dirty = 1;
					break;
				case 7:
					if (event_time == 0 ||
					    apply_instantly != 0) {
						g_briefing_map_target_scale.x =
							args[0];
						g_briefing_map_scale.x =
							args[0];
						g_briefing_map_target_scale.y =
							args[1];
						g_briefing_map_scale.y =
							args[1];
					} else {
						g_briefing_map_target_scale.x =
							args[0];
						g_briefing_map_target_scale.y =
							args[1];
					}
					g_briefing_map_scale_dirty = 1;
					break;
				case 8:
					for (slot_index = 0; slot_index < 8;
					     ++slot_index) {
						g_briefing_map_fg_marker_active
							[slot_index] = 0;
					}
					g_briefing_map_fg_markers_changed = 1;
					break;
				case 9:
				case 10:
				case 11:
				case 12:
				case 13:
				case 14:
				case 15:
				case 16:
					if (apply_instantly == 0) {
						iff = g_frontend_mission
							      .flight_groups
								      [args[0]]
							      .iff;
						if (iff > 2) {
							iff = 2;
						}
						if (iff == 1) {
							if (g_game_config
								    .sfx_datapad_enabled !=
							    0) {
								frontend_sound_play_ui_sound(
									"sfxTarget2",
									1, 0,
									127,
									12 * g_game_config
											.sfx_datapad_volume,
									63);
							}
						} else if (
							g_game_config
								.sfx_datapad_enabled !=
							0) {
							frontend_sound_play_ui_sound(
								"sfxTarget1", 1,
								0, 127,
								12 * g_game_config
										.sfx_datapad_volume,
								63);
						}
					}
					slot_index = opcode - 9;
					g_briefing_map_fg_marker_active
						[slot_index] = 1;
					g_briefing_map_fg_marker_age
						[slot_index] =
							apply_instantly == 0
								? 0
								: 80;
					g_briefing_map_fg_marker_flight_group_idx
						[slot_index] = args[0];
					break;
				case 17:
					for (slot_index = 0; slot_index < 8;
					     ++slot_index) {
						g_briefing_map_label_active
							[slot_index] = 0;
					}
					g_briefing_map_labels_changed = 1;
					break;
				case 18:
				case 19:
				case 20:
				case 21:
				case 22:
				case 23:
				case 24:
				case 25:
					if (apply_instantly == 0) {
						strcpy(label_text,
						       g_briefing_map_label_texts
							       [args[0]]);
						if ((uint16_t)strlen(
							    label_text) != 0 &&
						    g_game_config.sfx_datapad_enabled !=
							    0) {
							frontend_sound_play_ui_sound(
								"sfxText", 1, 0,
								127,
								12 * g_game_config
										.sfx_datapad_volume,
								63);
						}
					}
					slot_index = opcode - 18;
					g_briefing_map_label_active
						[slot_index] = 1;
					g_briefing_map_label_age[slot_index] =
						apply_instantly == 0 ? 0 : 80;
					g_briefing_map_label_text_idx
						[slot_index] = args[0];
					g_briefing_map_label_x[slot_index] =
						args[1];
					g_briefing_map_label_y[slot_index] =
						args[2];
					g_briefing_map_label_style[slot_index] =
						args[3];
					break;
				default:
					break;
				}
			}
		} while (event_time <= g_briefing_script.current_frame);
	}

	++g_briefing_script.current_frame;
	g_briefing_script.cursor_word_index = saved_cursor_word_index;
	return saved_cursor_word_index;
}
