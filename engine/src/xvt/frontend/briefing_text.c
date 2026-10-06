#include "xvt/frontend/briefing_text.h"

#include <stdlib.h>

#include "xvt_runtime/log/log.h"

/* The active briefing's 32 map label strings: 40-byte heap buffers that
 * frontend_mission_init_for_briefing allocates and
 * frontend_mission_load_current_with_briefing fills from the mission file. Label
 * entries of the briefing script pick one by index. */
// GLOBAL: XVT 0x669582
char *g_briefing_map_label_texts[32] = {0};
/* The active briefing's 32 text blocks: 320-byte heap buffers, allocated and
 * filled like g_briefing_map_label_texts. The block text slot 1 names is drawn
 * under the briefing map. */
// GLOBAL: XVT 0x669602
char *g_briefing_text_blocks[32] = {0};
/* Twenty 1024-byte heap buffers that frontend_mission_init_for_briefing
 * allocates and briefing_text_free_allocated_buffers frees; nothing else uses
 * them. */
// GLOBAL: XVT 0x669682
char *g_briefing_unused_buffers[20] = {0};
/* 4096-byte heap buffer of the text shown with a mission or a network game:
 * the mission's description from mission_setup_load_mission_desc_text, or the
 * text mission_debrief_read_outcome_text writes; NULL while none is held. Many
 * functions allocate, fill, clear and free it, chiefly in mission_setup.c,
 * mission_debrief.c and frontend_net.c. */
// GLOBAL: XVT 0xAA6118
char *g_mission_text = NULL;

/* Frees the briefing's text buffers through briefing_text_free_allocated_buffers
 * and returns 1. Its one caller is mission_setup_free_screen_resources. */
// FUNCTION: XVT 0x4F68B0
int16_t briefing_text_free_allocated_buffers_exit(void)
{
	briefing_text_free_allocated_buffers();
	return 1;
}

/* Frees every non-NULL buffer in g_briefing_map_label_texts,
 * g_briefing_text_blocks and g_briefing_unused_buffers and leaves the pointers as
 * they were, so a second call before frontend_mission_init_for_briefing
 * allocates again frees them twice. */
// FUNCTION: XVT 0x4F6B10
void briefing_text_free_allocated_buffers(void)
{
	int16_t index;

	XVT_LOG_DEBUG("briefing.text_freed labels=%d blocks=%d spare=%d",
		      g_briefing_map_label_texts[0] != NULL,
		      g_briefing_text_blocks[0] != NULL,
		      g_briefing_unused_buffers[0] != NULL);
	for (index = 0; index < 32; ++index) {
		if (g_briefing_map_label_texts[index] != NULL) {
			free(g_briefing_map_label_texts[index]);
		}
	}

	for (index = 0; index < 32; ++index) {
		if (g_briefing_text_blocks[index] != NULL) {
			free(g_briefing_text_blocks[index]);
		}
	}

	for (index = 0; index < 20; ++index) {
		if (g_briefing_unused_buffers[index] != NULL) {
			free(g_briefing_unused_buffers[index]);
		}
	}
}
