/* Tests for xvt/frontend/frontend_mission_list.c, the two exit functions of
 * the mission list screens. Each check gives the screen a mission list and a
 * mission text from the heap, a few registered scrollable controls and, for
 * the second function, a closed mouse input gate, then calls the exit
 * function and reads what its comment promises back from the game's globals.
 * No image is registered, so the "background" image they free is absent. */
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"

/* A mission list of two entries, a 4096-byte mission text, three scrollable
 * controls and the mouse given to control 7. */
static void open_screen(void)
{
	g_mission_list = calloc(2, sizeof *g_mission_list);
	XVT_ASSERT_TRUE(g_mission_list != NULL);
	g_mission_count = 2;
	g_mission_text = calloc(4096, 1);
	XVT_ASSERT_TRUE(g_mission_text != NULL);
	g_scrollable_control_count = 3;
	g_front_state.mouse_input_gate = 7;
}

/* The exit function frees the list and the text and sets both NULL, forgets
 * the scrollable controls, and returns 0 whatever frame counter it gets. */
static void check_free_screen_resources(void)
{
	open_screen();
	XVT_ASSERT_INT_EQ(frontend_mission_list_free_screen_resources(5), 0);
	XVT_ASSERT_TRUE(g_mission_list == NULL);
	XVT_ASSERT_TRUE(g_mission_text == NULL);
	XVT_ASSERT_INT_EQ(g_scrollable_control_count, 0);

	/* With nothing left to free it still returns 0. */
	XVT_ASSERT_INT_EQ(frontend_mission_list_free_screen_resources(0), 0);
	XVT_ASSERT_TRUE(g_mission_list == NULL);
	XVT_ASSERT_TRUE(g_mission_text == NULL);
}

/* The second exit function does the same and also opens the mouse input
 * gate, setting it to 0. */
static void check_free_and_clear_input_gate(void)
{
	open_screen();
	XVT_ASSERT_INT_EQ(
		frontend_mission_list_free_screen_resources_and_clear_input_gate(),
		0);
	XVT_ASSERT_TRUE(g_mission_list == NULL);
	XVT_ASSERT_TRUE(g_mission_text == NULL);
	XVT_ASSERT_INT_EQ(g_scrollable_control_count, 0);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_input_gate, 0);

	XVT_ASSERT_INT_EQ(
		frontend_mission_list_free_screen_resources_and_clear_input_gate(),
		0);
	XVT_ASSERT_TRUE(g_mission_list == NULL);
	XVT_ASSERT_TRUE(g_mission_text == NULL);
}

int main(void)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	check_free_screen_resources();
	check_free_and_clear_input_gate();
	return 0;
}
