/* Tests for xvt/frontend/briefing_script.c, the briefing map's script player.
 * Each check writes a short script into g_briefing_script, plays it frame by
 * frame and reads back the text slots, markers, labels and map targets the
 * script sets. Datapad sounds stay off, so no sound is played, and the label
 * texts are the test's own strings. No game data is read. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission.h"

enum {
	END_TIME = 9999,
	END_OPCODE = 34,
};

static char g_label_text[32][40];

/* An empty script of the given length: every word 0, the label texts the
 * test's empty strings, two flight groups, no playback and no sounds. The
 * caller writes the entries. */
static void fresh_script(int duration)
{
	memset(&g_briefing_script, 0, sizeof g_briefing_script);
	g_briefing_script.duration_frames = (int16_t)duration;
	memset(g_label_text, 0, sizeof g_label_text);
	for (int i = 0; i < 32; ++i) {
		g_briefing_map_label_texts[i] = g_label_text[i];
	}
	memset(&g_frontend_mission, 0, sizeof g_frontend_mission);
	g_frontend_mission.flight_group_count = 2;
	g_game_config.sfx_datapad_enabled = 0;
	g_briefing_playback_active = 0;
}

/* Writes words into the script from word index at; returns the index after
 * them. */
static int put(int at, const int16_t *words, int count)
{
	for (int i = 0; i < count; ++i) {
		g_briefing_script.words[at + i] = words[i];
	}
	return at + count;
}

/* The default script is 200 frames long, with header words 2 and 0 and one
 * entry, the end entry at time 9999. Only words 0 and 1 are written. It then
 * starts over, which plays frame 0, and returns the word index that leaves:
 * the end entry's, 0. */
static void check_default_script(void)
{
	fresh_script(0);
	g_briefing_script.words[2] = 77;
	g_briefing_script.header_word06 = 5;
	g_briefing_script.header_word08 = 6;
	XVT_ASSERT_INT_EQ(briefing_script_init_default_script(), 0);
	XVT_ASSERT_INT_EQ(g_briefing_script.duration_frames, 200);
	XVT_ASSERT_INT_EQ(g_briefing_script.header_word06, 2);
	XVT_ASSERT_INT_EQ(g_briefing_script.header_word08, 0);
	XVT_ASSERT_INT_EQ(g_briefing_script.words[0], END_TIME);
	XVT_ASSERT_INT_EQ(g_briefing_script.words[1], END_OPCODE);
	XVT_ASSERT_INT_EQ(g_briefing_script.words[2], 77);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(g_briefing_script.cursor_word_index, 0);
}

/* Starting over puts the map center and its target at (0, 0), both zooms at
 * 32, and the text slots, markers and labels off, then plays frame 0 at once:
 * its entries show text block 6 in slot 1, marker 2 on flight group 1 and
 * label 3, the last two at age 80. It returns the word index of the first
 * later entry and leaves the page count alone. */
static void check_reset_state(void)
{
	fresh_script(100);
	int at = 0;
	/* Text block 6 in slot 1. */
	at = put(at, (const int16_t[]){0, 5, 6}, 3);
	/* Marker 2 on flight group 1. */
	at = put(at, (const int16_t[]){0, 11, 1}, 3);
	/* Label 3. */
	at = put(at, (const int16_t[]){0, 21, 4, -30, 40, 2}, 6);
	/* Text block 1 in slot 0, later. */
	at = put(at, (const int16_t[]){7, 4, 1}, 3);
	at = put(at, (const int16_t[]){END_TIME, END_OPCODE}, 2);
	g_briefing_map_center.x = 5;
	g_briefing_map_center.y = 6;
	g_briefing_map_target_center.x = 7;
	g_briefing_map_target_center.y = 8;
	g_briefing_map_scale.x = 9;
	g_briefing_map_scale.y = 10;
	g_briefing_map_target_scale.x = 11;
	g_briefing_map_target_scale.y = 12;
	g_briefing_text_slot_active[0] = 1;
	g_briefing_map_fg_marker_active[0] = 1;
	g_briefing_map_fg_marker_active[7] = 1;
	g_briefing_map_label_active[0] = 1;
	g_briefing_map_label_active[7] = 1;
	g_briefing_text_page_number = 3;
	g_briefing_last_narrated_text_block_idx = 4;
	g_briefing_script.current_frame = 50;
	g_briefing_script.cursor_word_index = 15;

	XVT_ASSERT_INT_EQ(briefing_script_reset_state(), 12);
	XVT_ASSERT_INT_EQ(g_briefing_script.cursor_word_index, 12);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.y, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_center.x, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_center.y, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 32);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 32);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.x, 32);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.y, 32);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[0], 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[1], 1);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[1], 6);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_active[i], i == 2);
		XVT_ASSERT_INT_EQ(g_briefing_map_label_active[i], i == 3);
	}
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_flight_group_idx[2], 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_age[2], 80);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_age[3], 80);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 3);
	XVT_ASSERT_INT_EQ(g_briefing_last_narrated_text_block_idx, 4);
}

/* A frame applies the entries timed at it, skips earlier ones and leaves the
 * cursor on the first later entry, raises the frame by one and returns the
 * cursor. Entry by entry: a marker at frame 2 shows at age 0 and is not
 * shown at frame 1; a skipped entry timed before the current frame is not
 * applied; at frame 3 the pause marker, the label (age 0, text, point and
 * shade row) and the center and zoom targets, which leave the current values
 * alone away from time 0; at frame 4 opcode 3 clears both text slots, 8 the
 * markers and 17 the labels, and opcode 30 does nothing. Each new frame
 * clears the pause marker and the cleared-slots flag. */
static void check_advance_frame(void)
{
	fresh_script(100);
	int at = 0;
	/* Frame 0: text block 9 in slot 0. */
	at = put(at, (const int16_t[]){0, 4, 9}, 3);
	/* Frame 0: text block 8 in slot 1. */
	at = put(at, (const int16_t[]){0, 5, 8}, 3);
	/* Frame 2: marker 7 on group 0. */
	at = put(at, (const int16_t[]){2, 16, 0}, 3);
	/* Frame 3: pause marker. */
	at = put(at, (const int16_t[]){3, 1}, 2);
	/* Frame 3: label 0. */
	at = put(at, (const int16_t[]){3, 18, 2, 100, -200, 4}, 6);
	/* Frame 3: center target. */
	at = put(at, (const int16_t[]){3, 6, 300, -400}, 4);
	/* Frame 3: zoom target. */
	at = put(at, (const int16_t[]){3, 7, 64, 16}, 4);
	/* Frame 4: clear the text slots, the markers and the labels; opcode
	 * 30 does nothing. */
	at = put(at, (const int16_t[]){4, 3}, 2);
	at = put(at, (const int16_t[]){4, 8}, 2);
	at = put(at, (const int16_t[]){4, 17}, 2);
	at = put(at, (const int16_t[]){4, 30}, 2);
	at = put(at, (const int16_t[]){END_TIME, END_OPCODE}, 2);
	strcpy(g_label_text[2], "Convoy");

	XVT_ASSERT_INT_EQ(briefing_script_advance_frame(0), 6);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[0], 1);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[0], 9);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[1], 8);

	XVT_ASSERT_INT_EQ(briefing_script_advance_frame(0), 6);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_active[7], 0);
	g_briefing_map_fg_marker_age[7] = 55;
	XVT_ASSERT_INT_EQ(briefing_script_advance_frame(0), 9);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 3);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_active[7], 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_age[7], 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_flight_group_idx[7], 0);

	/* An earlier entry under the cursor is read past without being
	 * applied. */
	g_briefing_script.words[8] = 2;
	g_briefing_script.cursor_word_index = 6;
	g_briefing_map_fg_marker_active[7] = 0;
	g_briefing_map_label_age[0] = 55;
	g_briefing_map_center.x = 1;
	g_briefing_map_scale.x = 20;
	XVT_ASSERT_INT_EQ(briefing_script_advance_frame(0), 25);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_active[7], 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_flight_group_idx[7], 0);
	XVT_ASSERT_INT_EQ(g_briefing_script_pause_marker_reached, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_active[0], 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_age[0], 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_text_idx[0], 2);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_x[0], 100);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_y[0], -200);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_style[0], 4);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_center.x, 300);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_center.y, -400);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_center_dirty, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.x, 64);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.y, 16);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 20);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale_dirty, 1);

	g_briefing_map_fg_marker_active[1] = 1;
	g_briefing_map_label_active[5] = 1;
	XVT_ASSERT_INT_EQ(briefing_script_advance_frame(0), 33);
	XVT_ASSERT_INT_EQ(g_briefing_script_pause_marker_reached, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_center_dirty, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale_dirty, 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_slots_changed, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_markers_changed, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_labels_changed, 1);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_active[i], 0);
		XVT_ASSERT_INT_EQ(g_briefing_map_label_active[i], 0);
	}
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[0], 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[1], 0);

	XVT_ASSERT_INT_EQ(briefing_script_advance_frame(0), 33);
	XVT_ASSERT_INT_EQ(g_briefing_text_slots_changed, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_markers_changed, 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_labels_changed, 0);
}

/* Center and zoom entries set the current values as well as the targets when
 * the entry's time is 0 or the frame is applied at once; a marker or label
 * applied at once starts at age 80. */
static void check_advance_frame_at_once(void)
{
	fresh_script(100);
	int at = 0;
	at = put(at, (const int16_t[]){0, 6, 10, 20}, 4);
	at = put(at, (const int16_t[]){0, 7, 48, 24}, 4);
	at = put(at, (const int16_t[]){5, 6, -10, -20}, 4);
	at = put(at, (const int16_t[]){5, 7, 8, 12}, 4);
	at = put(at, (const int16_t[]){5, 9, 1}, 3);
	at = put(at, (const int16_t[]){5, 25, 0, 1, 2, 3}, 6);
	at = put(at, (const int16_t[]){END_TIME, END_OPCODE}, 2);
	briefing_script_advance_frame(0);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 10);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.y, 20);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 48);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 24);

	g_briefing_script.current_frame = 5;
	XVT_ASSERT_INT_EQ(briefing_script_advance_frame(1), 25);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, -10);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.y, -20);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_center.x, -10);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 8);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 12);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.y, 12);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_active[0], 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_age[0], 80);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_flight_group_idx[0], 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_active[7], 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_age[7], 80);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_style[7], 3);
}

/* Playing through a time returns 0 and does nothing when that time was the
 * last frame played. Otherwise it plays frames until the current frame passes
 * the time, starting over first when the time is behind, and returns 1. */
static void check_advance_until_time(void)
{
	fresh_script(100);
	int at = 0;
	at = put(at, (const int16_t[]){3, 4, 1}, 3);
	at = put(at, (const int16_t[]){6, 4, 2}, 3);
	at = put(at, (const int16_t[]){END_TIME, END_OPCODE}, 2);
	briefing_script_reset_state();
	XVT_ASSERT_INT_EQ(briefing_script_advance_until_time(0, 1), 0);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(briefing_script_advance_until_time(4, 1), 1);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 5);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[0], 1);
	XVT_ASSERT_INT_EQ(briefing_script_advance_until_time(4, 1), 0);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 5);
	XVT_ASSERT_INT_EQ(briefing_script_advance_until_time(6, 1), 1);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[0], 2);

	/* Through the current frame: that frame plays without starting over,
	 * so neither frame 6's entry nor the zoom of the start comes back. */
	g_briefing_text_slot_block_idx[0] = 0;
	g_briefing_map_scale.x = 50;
	XVT_ASSERT_INT_EQ(briefing_script_advance_until_time(7, 1), 1);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 8);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[0], 0);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 50);

	/* Behind: starts over, so frame 3's entry plays again. */
	g_briefing_text_slot_block_idx[0] = 0;
	XVT_ASSERT_INT_EQ(briefing_script_advance_until_time(3, 1), 1);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 4);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[0], 1);
}

/* Forward replays from the start and stops after the first frame, at or past
 * the one it started from, that first shows a text slot or holds a pause
 * marker, then plays one more frame: from frame 1 it stops on the text shown
 * at frame 10 and leaves the script at frame 12, from there on the pause
 * marker at frame 20, and from there it reaches the end entry first, which
 * starts the briefing over with the page count at 0 and returns the reset's
 * word index. */
static void check_forward(void)
{
	fresh_script(40);
	int at = 0;
	at = put(at, (const int16_t[]){10, 5, 3}, 3);
	at = put(at, (const int16_t[]){20, 1}, 2);
	at = put(at, (const int16_t[]){30, END_OPCODE}, 2);
	briefing_script_reset_state();
	XVT_ASSERT_INT_EQ(briefing_script_advance_to_next_visible_line(), 1);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 12);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[1], 1);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_block_idx[1], 3);

	XVT_ASSERT_INT_EQ(briefing_script_advance_to_next_visible_line(), 1);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 22);

	g_briefing_text_page_number = 4;
	g_briefing_last_narrated_text_block_idx = 3;
	XVT_ASSERT_INT_EQ(briefing_script_advance_to_next_visible_line(), 0);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 0);
	XVT_ASSERT_INT_EQ(g_briefing_last_narrated_text_block_idx, 0);
}

/* With playback off a frame does nothing. With it on, the zoom steps toward
 * its target and one script frame plays; once the current frame reaches the
 * script's length the briefing starts over, with the page count at 0. */
static void check_advance_or_reset(void)
{
	fresh_script(3);
	int at = 0;
	at = put(at, (const int16_t[]){END_TIME, END_OPCODE}, 2);
	briefing_script_reset_state();
	g_briefing_map_target_scale.x = 40;
	briefing_script_advance_or_reset_at_end(0);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 32);

	g_briefing_playback_active = 1;
	briefing_script_advance_or_reset_at_end(0);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 2);
	XVT_ASSERT_TRUE(g_briefing_map_scale.x > 32);
	briefing_script_advance_or_reset_at_end(0);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 3);
	g_briefing_text_page_number = 2;
	briefing_script_advance_or_reset_at_end(0);
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_target_scale.x, 32);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 0);
	g_briefing_playback_active = 0;
}

/* Known failure end_entry_read_past, issue #151: the end entry, opcode 34,
 * ends the script (briefing_script_init_default_script's comment). Here it is
 * timed at frame 5, before the script's length, and an entry showing text
 * block 7 follows it at the same time. The frame that reaches the end entry
 * reads on past it and shows the block. */
static void check_end_entry_read_past(void)
{
	fresh_script(100);
	int at = 0;
	at = put(at, (const int16_t[]){5, END_OPCODE}, 2);
	at = put(at, (const int16_t[]){5, 4, 7}, 3);
	at = put(at, (const int16_t[]){END_TIME, END_OPCODE}, 2);
	briefing_script_reset_state();
	briefing_script_advance_until_time(5, 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_slot_active[0], 0);
}

/* Known failure script_without_end_past_words, issue #151: the script holds
 * 400 words (briefing_script.h). A script with no end entry, all words 0,
 * reads as entries timed at frame 0, and starting over reads them on past the
 * last word. The reader should stay inside the 400 words. */
static void check_script_without_end_past_words(void)
{
	fresh_script(100);
	briefing_script_reset_state();
	XVT_ASSERT_TRUE(g_briefing_script.cursor_word_index <= 398);
}

/* Known failure opcode_past_count_table, issue #156: the argument count of an
 * entry is read from the 35-entry table at its opcode, unchecked (the
 * function's comment says no opcode is checked). An entry with opcode 35
 * reads its count from past the table. An unknown opcode should take no
 * arguments and leave the cursor on the next entry, word 2. */
static void check_opcode_past_count_table(void)
{
	fresh_script(100);
	int at = 0;
	at = put(at, (const int16_t[]){0, 35}, 2);
	at = put(at, (const int16_t[]){END_TIME, END_OPCODE}, 2);
	XVT_ASSERT_INT_EQ(briefing_script_reset_state(), 2);
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
			{"end_entry_read_past", check_end_entry_read_past},
			{"script_without_end_past_words",
			 check_script_without_end_past_words},
			{"opcode_past_count_table",
			 check_opcode_past_count_table},
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
	check_default_script();
	check_reset_state();
	check_advance_frame();
	check_advance_frame_at_once();
	check_advance_until_time();
	check_forward();
	check_advance_or_reset();
	return 0;
}
