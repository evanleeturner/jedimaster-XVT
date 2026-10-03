/* Checks the flight loading steps in flight_loading.c against their promises in
 * xvt_runtime/runtime/flight_internal.h, for the two that need no game files or display: Reset and
 * MissionSetup. No game data is read: the test sets the globals they touch itself, with the flight drawing
 * to its own staging buffer rather than the frontend's surface.
 *
 * Not checked here: Globals loads the AI plans from the retail game files, Palette loads the flight palette
 * and the mission's .pal file from them and configures the display, and Runtime initializes the mission
 * runtime of a loaded mission and starts the music CD; they need the retail game files and a running
 * flight's display. */
#include "test_assert.h"
#include "xvt_runtime/runtime/flight_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void surface(void)
{
	g_flight_render_to_frontend = 0;
	g_flight_page_flip = 0;
	g_surface_lock_count = 0;
}

static void check_reset(void)
{
	/* Handles and pool pointers are forgotten, not freed: the test frees the memory itself afterwards. */
	void *objects = malloc(64);
	void *mobiles = malloc(64);
	void *char_data = malloc(64);
	void *craft = malloc(64);
	void *guidance = malloc(64);
	XVT_ASSERT_TRUE(objects && mobiles && char_data && craft && guidance);
	g_object_table = objects;
	g_mobile_object_pool_base = mobiles;
	g_mobile_object_char_data_pool = char_data;
	g_craft_data_pool_base = craft;
	g_projectile_guidance_states = guidance;
	g_object_table_handle = 11;
	g_mobile_object_pool_handle = 12;
	g_mobile_object_char_data_handle = 13;
	g_craft_data_pool_handle = 14;
	g_warhead_guidance_pool_handle = 15;
	g_string_data_handle = 16;
	g_render_object_list_handle = 17;
	g_message_log_handle = 18;

	xvt_flight_loading_reset();
	XVT_ASSERT_TRUE(g_object_table == NULL);
	XVT_ASSERT_TRUE(g_mobile_object_pool_base == NULL);
	XVT_ASSERT_TRUE(g_mobile_object_char_data_pool == NULL);
	XVT_ASSERT_TRUE(g_craft_data_pool_base == NULL);
	XVT_ASSERT_TRUE(g_projectile_guidance_states == NULL);
	XVT_ASSERT_INT_EQ(g_object_table_handle, 0);
	XVT_ASSERT_INT_EQ(g_mobile_object_pool_handle, 0);
	XVT_ASSERT_INT_EQ(g_mobile_object_char_data_handle, 0);
	XVT_ASSERT_INT_EQ(g_craft_data_pool_handle, 0);
	XVT_ASSERT_INT_EQ(g_warhead_guidance_pool_handle, 0);
	XVT_ASSERT_INT_EQ(g_string_data_handle, 0);
	XVT_ASSERT_INT_EQ(g_render_object_list_handle, 0);
	XVT_ASSERT_INT_EQ(g_message_log_handle, 0);
	/* The sanitizer reports a double free here if Reset freed any of them. */
	free(objects);
	free(mobiles);
	free(char_data);
	free(craft);
	free(guidance);
}

static void check_proving_grounds(void)
{
	/* Craft 2, level 4 for traincourse... */
	surface();
	g_flight_conf_train_course = 1;
	g_flight_mission_state.proving_grounds_craft_type = 0;
	g_flight_mission_state.proving_grounds_level = 0;
	xvt_flight_loading_mission_setup();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_craft_type, 2);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_level, 4);
	/* ...else none. */
	g_flight_conf_train_course = 0;
	xvt_flight_loading_mission_setup();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_craft_type, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_level, 0);
	XVT_ASSERT_INT_EQ(g_surface_lock_count, 0);
}

static void check_resets(void)
{
	/* The flight input, the message log and the MFD pages are reset. */
	surface();
	g_flight_conf_train_course = 0;
	g_key_mods = 3;
	g_mouse_buttons = 1;
	g_message_log_total_count = 9;
	g_mfd_active_page = 2;
	g_mfd_secondary_page = 3;
	for (int i = 0; i < MFD_PAGE_COUNT; ++i) {
		g_mfd_page_states[i] = 1;
	}
	xvt_flight_loading_mission_setup();
	XVT_ASSERT_INT_EQ(g_key_mods, 0);
	XVT_ASSERT_INT_EQ(g_mouse_buttons, 0);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_NONE);
	XVT_ASSERT_INT_EQ(g_mfd_secondary_page, MFD_PAGE_NONE);
	for (int i = 0; i < MFD_PAGE_COUNT; ++i) {
		XVT_ASSERT_INT_EQ(g_mfd_page_states[i], MFD_PAGE_STATE_CLOSED);
	}
}

static void check_noise_table(void)
{
	/* The noise table is refilled from rand(): the same seed gives the same table whatever it held before,
	 * and another seed another table. */
	static uint8_t first[sizeof g_flight_noise_table];
	surface();
	srand(7);
	xvt_flight_loading_mission_setup();
	memcpy(first, g_flight_noise_table, sizeof first);
	memset(g_flight_noise_table, 0xA5, sizeof g_flight_noise_table);
	srand(7);
	xvt_flight_loading_mission_setup();
	XVT_ASSERT_INT_EQ(memcmp(first, g_flight_noise_table, sizeof first), 0);
	srand(8);
	xvt_flight_loading_mission_setup();
	XVT_ASSERT_TRUE(memcmp(first, g_flight_noise_table, sizeof first) != 0);
}

int main(void)
{
	check_reset();
	check_proving_grounds();
	check_resets();
	check_noise_table();
	return 0;
}
