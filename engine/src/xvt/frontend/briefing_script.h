#ifndef XVT_FRONTEND_BRIEFING_SCRIPT_H
#define XVT_FRONTEND_BRIEFING_SCRIPT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One briefing's script, read whole from the mission file;
 * briefing_script_reset_state then sets current_frame and cursor_word_index. */
struct frontend_briefing_script {
	/* Frames played before the briefing starts over. */
	int16_t duration_frames;
	int16_t current_frame;	   /* Next frame to play, counted from 0. */
	int16_t cursor_word_index; /* Index in words of the next entry to read. */
	/* Read with the script, set to 2 by briefing_script_init_default_script;
	 * nothing reads it. */
	int16_t header_word06;
	/* Read with the script, set to 0 by briefing_script_init_default_script;
	 * nothing reads it. */
	int16_t header_word08;
	/* Entries: a time in frames, an opcode, then the opcode's arguments. */
	int16_t words[400];
};

extern struct frontend_briefing_script g_briefing_script;
extern int16_t g_briefing_playback_active;
extern int g_briefing_last_narrated_text_block_idx;
extern int g_briefing_text_page_number;
extern int16_t g_briefing_text_slot_active[2];
extern int16_t g_briefing_text_slot_block_idx[2];
extern int16_t g_briefing_text_slots_changed;
extern int16_t g_briefing_script_pause_marker_reached;
extern const int16_t g_briefing_script_opcode_arg_counts[35];

void briefing_script_advance_or_reset_at_end(int frame_counter);
int16_t briefing_script_init_default_script(void);
int16_t briefing_script_reset_state(void);
int16_t briefing_script_advance_until_time(int16_t target_time,
					   int16_t initialize_state);
int16_t briefing_script_advance_to_next_visible_line(void);
int16_t briefing_script_advance_frame(int16_t apply_instantly);

#ifdef __cplusplus
}
#endif

#endif
