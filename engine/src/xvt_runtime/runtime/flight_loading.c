#include "xvt_runtime/runtime/flight_internal.h"

enum {
	PLAYER_COUNT = sizeof(g_players) / sizeof(g_players[0]),
	PALETTE_COLOR_COUNT = 256,
	PALETTE_BYTES = PALETTE_COLOR_COUNT * sizeof(struct rgb_triplet),
	PALETTE_HALF_BYTES = PALETTE_BYTES / 2,
	PALETTE_LAST_COLOR_OFFSET = PALETTE_BYTES - sizeof(struct rgb_triplet),
	MISSION_PALETTE_FIRST_COLOR = 64,
	FLIGHT_RESOURCE_SCRATCH_BYTES = 1024,
	MISSION_EXTENSION_LENGTH = 3,
	MISSION_EXTENSION_FIRST = 0,
	MISSION_EXTENSION_SECOND = 1,
	MISSION_EXTENSION_THIRD = 2,
	PALETTE_CHANNEL_RED = 0,
	PALETTE_CHANNEL_GREEN = 1,
	PALETTE_CHANNEL_BLUE = 2,
	NOISE_TABLE_VALUE_LIMIT = 124,
	NO_VIEWPORT_INSET = 0,
	MUSIC_TRACK_FLIGHT = 2,
	MUSIC_START_CHOICE_COUNT = 4,
	MUSIC_VOLUME_MAX_LEVEL = 9,
	MUSIC_FADE_DIVISOR = 8,
	MUSIC_FADE_DURATION_MS = 1000,
	MILLISECONDS_PER_SECOND = 1000,
	MILLISECONDS_PER_MINUTE = 60000,
	PROVING_GROUNDS_DEFAULT_CRAFT = 2,
	PROVING_GROUNDS_DEFAULT_LEVEL = 4,
	PROVING_GROUNDS_SCORE_STEP_POINTS = 2000,
	PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL = 5,
	RANDOM_SEED_XOR = 0xBEEF,
	ASTEROID_FIELD_RANDOM_SEED = -21267,
	DEFAULT_MODEL_LIGHT_DIRECTION = 18900,
};

void xvt_flight_loading_reset(void)
{
	g_object_table_handle = 0;
	g_mobile_object_pool_handle = 0;
	g_mobile_object_char_data_handle = 0;
	g_craft_data_pool_handle = 0;
	g_warhead_guidance_pool_handle = 0;
	g_string_data_handle = 0;
	g_render_object_list_handle = 0;
	g_flight_small_font_handle = 0;
	g_flight_micro_font_handle = 0;
	g_flight_medium_font_handle = 0;
	g_flight_scratch_screen_buffer_handle = 0;
	g_flight_aux_buffer_handle = 0;
	g_flight_offscreen_buffer_handle = 0;
	g_hud_panel_sprite_data_handle = 0;
	g_flight_icon_frames_handle = 0;
	g_message_log_handle = 0;
	g_object_table = NULL;
	g_mobile_object_pool_base = NULL;
	g_mobile_object_char_data_pool = NULL;
	g_craft_data_pool_base = NULL;
	g_projectile_guidance_states = NULL;
}

/* Copies this flight's game rules from the settings into the mission state. Multiplayer combat
 * engagements always fly at medium difficulty; only multiplayer takes the time limits and the AI
 * choice from the settings; a combat engagement inside a mission sequence has no random variation. */
static void xvt_flight_loading_mission_rules(void)
{
	if ((unsigned int)g_pilot_data.num_human_players_last_mission > 1 &&
	    (unsigned int)g_pilot_data.mission_directory_id >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		g_flight_mission_state.difficulty = GAME_DIFFICULTY_MEDIUM;
	} else {
		g_flight_mission_state.difficulty = g_game_config.difficulty;
		if (g_flight_mission_state.difficulty > GAME_DIFFICULTY_HARD) {
			g_flight_mission_state.difficulty =
				GAME_DIFFICULTY_EASY;
		}
	}
	g_flight_mission_state.collisions_enabled = g_game_config.collisions;
	g_flight_mission_state.craft_jumping_enabled =
		g_game_config.craft_jumping;
	g_flight_mission_state.random_variation_enabled =
		g_game_config.random_setup;
	g_flight_mission_state.battle_length_index =
		g_game_config.battle_length_index;
	g_flight_mission_state.locate_players_enabled =
		g_game_config.locate_players;
	g_flight_mission_state.player_flight_group_wave_mode =
		g_game_config.craft_waves;
	if (g_pilot_data.num_human_players_last_mission > 1) {
		g_flight_mission_state.mission_time_limit_minutes =
			g_game_config.mission_time_limit;
		g_flight_mission_state.team_victory_time_limit_minutes =
			g_game_config.last_team_time_limit_minutes;
		g_flight_mission_state.ai_opponents_enabled =
			g_game_config.ai_opponents;
	} else {
		g_flight_mission_state.mission_time_limit_minutes = UINT8_MAX;
		g_flight_mission_state.team_victory_time_limit_minutes = 0;
		g_flight_mission_state.ai_opponents_enabled = 1;
	}
	g_flight_mission_state.craft_impact_bounce_enabled = 1;
	if ((unsigned int)g_pilot_data.mission_directory_id >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilot_data.mission_sequence_active == 1) {
		g_flight_mission_state.random_variation_enabled = 0;
	}
}

void xvt_flight_loading_globals(void)
{
	int16_t abort_player_index;
	int16_t disconnect_player_index;
	int16_t connect_player_index;
	xvt_flight_loading_reset();
	flight_pump_window_messages();
	g_packet_drop_indicator = 0;
	g_lag_indicator = 0;
	g_sw3d_skip_odd_scanlines = 0;
	g_flight_net_host_abort_received = 0;
	for (abort_player_index = 0; abort_player_index < PLAYER_COUNT;
	     ++abort_player_index) {
		g_player_abort_flags[abort_player_index] = 0;
	}

	fsfx_clear_sfx_name_table();
	flight_sync_reset_remote_player_render_smoothing();
	flight_loading_reset_progress_state();
	time_reset_elapsed_ticks();
	g_flight_display_surfaces_active = 1;
	g_flight_draw_to_hud_layer = 1;
	if (g_flight_viewport_inset_x == NO_VIEWPORT_INSET &&
	    g_surface_width == 320) {
		g_flight_resolution_mode = FLIGHT_RESOLUTION_320X240;
	} else if (g_flight_viewport_inset_x == NO_VIEWPORT_INSET &&
		   g_surface_width == 480) {
		g_flight_resolution_mode = FLIGHT_RESOLUTION_480X360;
	} else {
		g_flight_resolution_mode = FLIGHT_RESOLUTION_640X480;
	}

	g_flight_sim_side_effects_suppressed = 0;
	g_unused_flight_session_reset_state = 0;
	g_unused_flight_transient_reset_state = 0;
	g_local_player = net_session_find_player_slot_by_dpid(
		net_session_get_local_dplay_id());
	g_active_flight_player_count = net_session_get_player_count();
	g_flight_player_count = g_active_flight_player_count;
	memset(g_replay_inputs, 0, sizeof(g_replay_inputs));
	memset(g_unused_flight_network_block, 0,
	       sizeof(g_unused_flight_network_block));
	memset(g_unused_flight_runtime_block, 0,
	       sizeof(g_unused_flight_runtime_block));
	memset(&g_current_input_frame, 0, sizeof(g_current_input_frame));
	g_remote_player_render_smoothing_enabled = g_internet_play_enabled;
	g_flight_mission_state.connected_player_count =
		g_active_flight_player_count;
	g_flight_mission_state.max_connected_player_count_this_mission =
		g_active_flight_player_count;

	xvt_flight_loading_mission_rules();

	if (g_active_flight_player_count != 1) {
		g_game_rand_feedback_state = (int16_t)g_game_config.random_seed;
	} else {
		uint16_t random_seed;

		random_seed = (uint16_t)timeGetTime();
		random_seed ^= RANDOM_SEED_XOR;
		g_game_rand_feedback_state = (int16_t)random_seed;
	}
	{
		uint32_t random_time;

		random_time = timeGetTime();
		g_asteroid_field_rand_seed =
			(uint16_t)ASTEROID_FIELD_RANDOM_SEED;
		g_game_rand2_feedback_state =
			(uint16_t)(random_time + g_game_rand_feedback_state);
	}

	memset(g_players, 0, sizeof(g_players));
	{
		int16_t reset_player_index;

		for (reset_player_index = 0; reset_player_index < PLAYER_COUNT;
		     ++reset_player_index) {
			g_input_frame_count[reset_player_index] = 0;
			g_player_connected[reset_player_index] = 1;
			g_flight_net_world_checksum_peer_status
				[reset_player_index] = 0;
			g_players[reset_player_index].lockstep_timestamp = 0;
			g_players[reset_player_index]
				.next_engine_wash_check_time = 0;
			g_players[reset_player_index].field_5b5 = 0;
		}
	}
	for (disconnect_player_index = 0;
	     disconnect_player_index < PLAYER_COUNT;
	     ++disconnect_player_index) {
		g_players[disconnect_player_index].participation_state = 0;
	}
	for (connect_player_index = 0;
	     connect_player_index < g_active_flight_player_count;
	     ++connect_player_index) {
		g_players[connect_player_index].participation_state = 1;
	}

	g_flight_net_buffer_world_messages_until_checksum = 0;
	g_flight_net_world_checksum_epoch = 0;
	g_single_object_update_override_idx = -1;
	if (g_flight_conf_no_pilot == 0) {
		mission_sync_pilot_network_players_to_session_slots();
	}
	pai_loadplans((char *)g_pai_plan_resource_base_name);
	pai_cache_builtin_plan_ids();
	g_hud_cockpit_resources_loaded = 0;
	g_flight_sw_rot_sprite_coeff_cache_valid = 0;
	g_unused_flight_startup_object_pass_state = 0;
	g_unused_flight_debug_log_file = NULL;
	g_flight_sw_rot_sprite_span_runs_enabled = 1;
}

void xvt_flight_loading_palette(void)
{
	uint8_t resource_scratch[FLIGHT_RESOURCE_SCRATCH_BYTES];
	int16_t palette_byte_offset;
	int16_t mission_extension_offset;
	char saved_mission_extension_prefix[2];
	char saved_mission_extension_third;
	flight_surface_lock();
	flight_display_configure_resolution_state();
	flight_surface_unlock();
	flight_render_transition_hook_stub();
	flight_surface_lock();
	flight_sw_init_framebuffer();
	flight_surface_unlock();
	nullsub_11();
	flight_display_flip();
	flight_render_configure_callbacks_for_resolution(3);
	fe_disk_io_read_all_bytes_or_fatal(g_flight_palette_resource_file_name,
					   resource_scratch);
	for (palette_byte_offset = 0; palette_byte_offset < PALETTE_HALF_BYTES;
	     palette_byte_offset += sizeof(struct rgb_triplet)) {
		uint8_t channel;

		channel = resource_scratch[palette_byte_offset +
					   PALETTE_CHANNEL_RED] >>
			  2;
		resource_scratch[palette_byte_offset + PALETTE_CHANNEL_RED] =
			resource_scratch[PALETTE_LAST_COLOR_OFFSET -
					 palette_byte_offset +
					 PALETTE_CHANNEL_RED] >>
			2;
		resource_scratch[PALETTE_LAST_COLOR_OFFSET -
				 palette_byte_offset + PALETTE_CHANNEL_RED] =
			channel;
		channel = resource_scratch[palette_byte_offset +
					   PALETTE_CHANNEL_GREEN] >>
			  2;
		resource_scratch[palette_byte_offset + PALETTE_CHANNEL_GREEN] =
			resource_scratch[PALETTE_LAST_COLOR_OFFSET -
					 palette_byte_offset +
					 PALETTE_CHANNEL_GREEN] >>
			2;
		resource_scratch[PALETTE_LAST_COLOR_OFFSET -
				 palette_byte_offset + PALETTE_CHANNEL_GREEN] =
			channel;
		channel = resource_scratch[palette_byte_offset +
					   PALETTE_CHANNEL_BLUE] >>
			  2;
		resource_scratch[palette_byte_offset + PALETTE_CHANNEL_BLUE] =
			resource_scratch[PALETTE_LAST_COLOR_OFFSET -
					 palette_byte_offset +
					 PALETTE_CHANNEL_BLUE] >>
			2;
		resource_scratch[PALETTE_LAST_COLOR_OFFSET -
				 palette_byte_offset + PALETTE_CHANNEL_BLUE] =
			channel;
	}
	g_flight_set_palette_range_fn((struct rgb_triplet *)resource_scratch, 0,
				      PALETTE_COLOR_COUNT);
	flight_palette_apply_to_display();
	flight_surface_lock();
	fe_disk_io_init_global_buffers();
	flight_surface_unlock();
	flight_surface_clear_to_black();

	if (g_flight_bytes_per_pixel == 1) {
		mission_extension_offset = (int)strlen(g_current_mission_file) -
					   MISSION_EXTENSION_LENGTH;
		saved_mission_extension_prefix[MISSION_EXTENSION_FIRST] =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_FIRST];
		saved_mission_extension_prefix[MISSION_EXTENSION_SECOND] =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_SECOND];
		saved_mission_extension_third =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_THIRD];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_FIRST] = 'p';
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_SECOND] = 'a';
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_THIRD] = 'l';
		if (fe_disk_io_open_global_stream(g_current_mission_file, "rb",
						  0, 0) == 0) {
			g_generate_mission_palette = 1;
		} else {
			g_generate_mission_palette = 0;
			fe_disk_io_close_global_stream(0);
			fe_disk_io_read_all_bytes_or_fatal(
				g_current_mission_file, g_flight_aux_buffer);
			g_flight_set_palette_range_fn(
				(struct rgb_triplet *)g_flight_aux_buffer,
				MISSION_PALETTE_FIRST_COLOR,
				PALETTE_COLOR_COUNT -
					MISSION_PALETTE_FIRST_COLOR);
		}
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_FIRST] =
			saved_mission_extension_prefix[MISSION_EXTENSION_FIRST];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_SECOND] =
			saved_mission_extension_prefix
				[MISSION_EXTENSION_SECOND];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_THIRD] =
			saved_mission_extension_third;
	}
}

void xvt_flight_loading_mission_setup(void)
{
	int16_t mfd_index;
	if (g_flight_conf_train_course != 0) {
		g_flight_mission_state.proving_grounds_craft_type =
			PROVING_GROUNDS_DEFAULT_CRAFT;
		g_flight_mission_state.proving_grounds_level =
			PROVING_GROUNDS_DEFAULT_LEVEL;
	} else {
		g_flight_mission_state.proving_grounds_craft_type = 0;
		g_flight_mission_state.proving_grounds_level = 0;
	}
	flight_surface_lock();
	flight_input_reset_runtime_state();
	flight_surface_unlock();
	{
		int16_t noise_index;

		for (noise_index = 0;
		     noise_index < (int)sizeof(g_flight_noise_table) - 1;
		     noise_index += 2) {
			do {
				g_flight_noise_table[noise_index] =
					(uint8_t)(rand() & 0x7F);
			} while (g_flight_noise_table[noise_index] >
				 NOISE_TABLE_VALUE_LIMIT);
			g_flight_noise_table[noise_index + 1] =
				(uint8_t)(rand() & 3);
		}
	}

	g_message_log_total_count = 0;
	g_unused_flight_message_runtime_state = 0;
	g_world_light_direction_x = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_world_light_direction_y = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_world_light_direction_z = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_system_message_display_enabled = 1;
	g_ready_message_pane_left = -1;
	g_message_log_write_index = UINT16_MAX;
	g_mfd_active_page = MFD_PAGE_NONE;
	g_mfd_secondary_page = MFD_PAGE_NONE;
	g_mfd_saved_active_page = MFD_PAGE_NONE;
	g_mfd_saved_secondary_page = MFD_PAGE_NONE;
	for (mfd_index = 0; mfd_index < (int)(sizeof(g_mfd_page_states) /
					      sizeof(g_mfd_page_states[0]));
	     ++mfd_index) {
		g_saved_mfd_page_states[mfd_index] = MFD_PAGE_STATE_CLOSED;
		g_mfd_page_states[mfd_index] = MFD_PAGE_STATE_CLOSED;
	}
	g_damage_mfd_current_system_id = 0;
}

void xvt_flight_loading_runtime(void)
{
	flight_surface_lock();
	mission_init_flight_runtime_state();
	flight_surface_unlock();
	g_dynamic_music_outcome_latched = 0;
	if (g_game_config.music_enabled != 0 &&
	    g_game_config.music_volume != 0 && music_cd_initialize() != 0) {
		int music_choice;
		uint16_t music_volume;
		uint32_t music_update_ms;

		music_volume = UINT16_MAX * g_game_config.music_volume /
			       MUSIC_VOLUME_MAX_LEVEL;
		music_cd_set_aux_volume(music_volume);
		music_choice = game_rand2() & (MUSIC_START_CHOICE_COUNT - 1);
		music_cd_play_track_from_time(
			MUSIC_TRACK_FLIGHT,
			g_dynamic_music_initial_start_minute_choices
				[music_choice],
			g_dynamic_music_initial_start_second_choices
				[music_choice]);
		g_dynamic_music_track_remaining_ms =
			music_cd_get_track_length_ms(MUSIC_TRACK_FLIGHT);
		g_dynamic_music_track_remaining_ms -=
			MILLISECONDS_PER_MINUTE *
			g_dynamic_music_initial_start_minute_choices
				[music_choice];
		g_dynamic_music_track_remaining_ms -=
			MILLISECONDS_PER_SECOND *
			g_dynamic_music_initial_start_second_choices
				[music_choice];
		music_update_ms = timeGetTime();
		g_dynamic_music_state = MUSIC_TRACK_FLIGHT;
		g_dynamic_music_last_update_ms = music_update_ms;
	} else {
		g_dynamic_music_track_remaining_ms = INT32_MAX;
		g_dynamic_music_state = 0;
	}

	if (g_flight_mission_state.proving_grounds_mode_active != 0) {
		proving_grounds_init_course_objects();
		proving_grounds_start_level(
			g_flight_mission_state.proving_grounds_level);
		if (g_flight_mission_state.proving_grounds_mode_active != 0 &&
		    g_flight_mission_state.proving_grounds_level > 1) {
			g_msg_arg_table[0] =
				g_flight_mission_state.proving_grounds_level -
				1;
			msg_emit_in_flight_message(
				IFMSG_197_ARG_10000_POINTS_AWARDED_FOR_PREVIOUS_LEVELS,
				g_local_player);
			g_flight_mission_state.proving_grounds_score =
				PROVING_GROUNDS_SCORE_STEP_POINTS *
				(PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL *
					 g_flight_mission_state
						 .proving_grounds_level -
				 PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL);
		}
	}
}
