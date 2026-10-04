#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/network_session.h"

enum {
	BUILTIN_ARGUMENT_COUNT = 2,
	PARSED_ARGUMENT_COUNT = 7,
	REQUIRED_ARGUMENT_COUNT =
		BUILTIN_ARGUMENT_COUNT + PARSED_ARGUMENT_COUNT,
	BRIGHTNESS_CONFIG_OFFSET = 4,
	BRIGHTNESS_CONFIG_SHIFT = 6,
	BRIGHTNESS_SCALE_MIN = 256,
	BRIGHTNESS_SCALE_MAX = 704,
	STAR_GRID_DIVISOR_LOW_DENSITY = 4,
	STAR_GRID_DIVISOR_MEDIUM_DENSITY = 2,
	STAR_GRID_DIVISOR_HIGH_DENSITY = 1,
	LOD_CONFIG_OFFSET = 5,
	LOD_CONFIG_MAX_VALUE = 20,
	LOD_SCALE_INVERSION_NUMERATOR = 1,
	MIPMAPPING_DISABLED_VALUE = 19,
	DISPLAY_WIDTH_LOW = 320,
	DISPLAY_HEIGHT_LOW = 240,
	DISPLAY_WIDTH_MEDIUM = 512,
	DISPLAY_HEIGHT_MEDIUM = 384,
	DISPLAY_WIDTH_HIGH = 640,
	DISPLAY_HEIGHT_HIGH = 480,
	WINDOW_WIDTH_MEDIUM = 480,
	WINDOW_HEIGHT_MEDIUM = 360,
	DISPLAY_CONFIG_LOW = 0,
	DISPLAY_CONFIG_MEDIUM = 1,
	DISPLAY_CONFIG_HIGH = 2,
	PALETTED_BYTES_PER_PIXEL = 1,
	HIGH_COLOR_BYTES_PER_PIXEL = 2,
	DISPLAY_INIT_SOUND_ERROR = 13,
};

static int g_game_session_started;
static int g_sound_engine_started;
static int g_flight_devices_created;

/* Sets the flight's start-up flags: flicker (off when a flicker.txt file exists), laser timing,
 * the async option and the launch switches. Each switch is found by substring anywhere in the
 * mission command line, so a pilot or game name containing a switch's word sets it too. */
static void xvt_flight_entry_read_launch_switches(const char *mission_cmd_line)
{
	xvt_file *flicker_file = file_open("flicker.txt", "r");
	if (flicker_file != NULL) {
		file_close(flicker_file);
		g_flight_conf_flicker = 0;
	} else {
		g_flight_conf_flicker = 1;
	}

	g_laser_fire_timestamp_tracking_enabled = 1;
	g_internet_play_enabled = g_game_config.internet_play;
	char *option_match = strstr(mission_cmd_line, "traincourse");
	g_flight_conf_train_course = 1;
	if (option_match == NULL) {
		g_flight_conf_train_course = 0;
	}
	option_match = strstr(mission_cmd_line, "nopilot");
	g_flight_conf_no_pilot = 1;
	if (option_match == NULL) {
		g_flight_conf_no_pilot = 0;
	}
	if (strstr(mission_cmd_line, "nodinput") != NULL) {
		g_flight_conf_direct_input = 0;
	} else if (strstr(mission_cmd_line, "dinput") != NULL) {
		g_flight_conf_direct_input = 1;
	} else {
		g_flight_conf_direct_input = 1;
	}
	if (strstr(mission_cmd_line, "nosfx") != NULL) {
		g_flight_conf_sfx_enabled = 0;
	} else if (strstr(mission_cmd_line, "sfx") != NULL) {
		g_flight_conf_sfx_enabled = 1;
	} else {
		g_flight_conf_sfx_enabled = 1;
	}
	if (strstr(mission_cmd_line, "nomusic") != NULL) {
		g_flight_conf_music_enabled = 0;
	} else if (strstr(mission_cmd_line, "music") != NULL) {
		g_flight_conf_music_enabled = 1;
	} else {
		g_flight_conf_music_enabled = 1;
	}
	if (strstr(mission_cmd_line, "novoice") != NULL) {
		g_flight_conf_voice_enabled = 0;
	} else if (strstr(mission_cmd_line, "voice") != NULL) {
		g_flight_conf_voice_enabled = 1;
	} else {
		g_flight_conf_voice_enabled = 1;
	}
	if (strstr(mission_cmd_line, "notickcounter") != NULL) {
		g_flight_conf_tick_counter_enabled = 0;
	} else if (strstr(mission_cmd_line, "tickcounter") != NULL) {
		g_flight_conf_tick_counter_enabled = 1;
	} else {
		g_flight_conf_tick_counter_enabled = 0;
	}
	if (strstr(mission_cmd_line, "nomipmaps") != NULL) {
		g_mipmapping_enabled = 0;
	} else if (strstr(mission_cmd_line, "mipmaps") != NULL) {
		g_mipmapping_enabled = 1;
	} else {
		g_mipmapping_enabled = 1;
	}
	option_match = strstr(mission_cmd_line, "inprogress");
	g_flight_in_progress_launch = 1;
	if (option_match == NULL) {
		g_flight_in_progress_launch = 0;
	}
	option_match = strstr(mission_cmd_line, "newnet");
	g_flight_conf_new_net = 1;
	if (option_match == NULL) {
		g_flight_conf_new_net = 0;
	}
	option_match = strstr(mission_cmd_line, "nolauncher");
	g_flight_conf_no_launcher = 1;
	if (option_match == NULL) {
		g_flight_conf_no_launcher = 0;
	}
	if (strstr(mission_cmd_line, "nofullscreen") != NULL) {
		g_flight_fullscreen = 0;
	} else if (strstr(mission_cmd_line, "fullscreen") != NULL) {
		g_flight_fullscreen = 1;
	}
	if (strstr(mission_cmd_line, "nopageflip") != NULL) {
		g_flight_page_flip = 0;
	} else if (strstr(mission_cmd_line, "pageflip") != NULL) {
		g_flight_page_flip = 1;
	}
	if (mission_cmd_line[0] == '-') {
		g_flight_started_with_dash_arg = 1;
	} else if (mission_cmd_line[0] == '/' && mission_cmd_line[1] == '+') {
		g_unused_flight_cmd_line_plus_switch_flag = 1;
	}
}

int xvt_flight_entry_prepare(char *mission_cmd_line)
{
	g_game_session_started = 0;
	g_sound_engine_started = 0;
	g_flight_devices_created = 0;
	model_preview_free_resources();
	g_flight_render_to_frontend = 0;
	if (mission_cmd_line == NULL) {
		return 0;
	}

	config_load();
	xvt_flight_entry_read_launch_switches(mission_cmd_line);

	if (flight_update_and_focus_main_window() == 0) {
		return 0;
	}

	int command_line_offset = 0;
	int quoted_argument = 0;
	int argument_count = BUILTIN_ARGUMENT_COUNT;
	g_flight_launch_args.program_name = "xtie";
	g_flight_launch_args.sentinel = "/trebla";
	if (mission_cmd_line[0] != '\0') {
		for (int argument_index = 0;
		     argument_index < PARSED_ARGUMENT_COUNT; ++argument_index) {
			g_flight_launch_args.arguments[argument_index] =
				&mission_cmd_line[command_line_offset];
			while (1) {
				char character =
					mission_cmd_line[command_line_offset];
				if (character == ' ') {
					if (quoted_argument != 1) {
						break;
					}
				} else if (character == '\0') {
					break;
				}
				if (character == '~') {
					if (quoted_argument != 0) {
						quoted_argument = 0;
						mission_cmd_line
							[command_line_offset] =
								'\0';
						++command_line_offset;
					} else {
						quoted_argument = 1;
						++command_line_offset;
						g_flight_launch_args.arguments
							[argument_index] =
							&mission_cmd_line
								[command_line_offset];
					}
				} else {
					++command_line_offset;
				}
			}
			++argument_count;
			if (mission_cmd_line[command_line_offset] == '\0') {
				break;
			}
			mission_cmd_line[command_line_offset] = '\0';
			++command_line_offset;
			if (mission_cmd_line[command_line_offset] == '\0') {
				break;
			}
		}
	}
	if (argument_count < REQUIRED_ARGUMENT_COUNT) {
		return 0;
	}

	network_transport_type network_type =
		(network_transport_type)g_game_config.network_type;
	const char *connection_address;
	switch (network_type) {
	case NET_TRANSPORT_TCPIP:
		connection_address = g_game_config.ip_address;
		break;
	case NET_TRANSPORT_MODEM:
		connection_address = g_game_config.phone_number;
		break;
	default:
	case NET_TRANSPORT_IPX:
		connection_address = NULL;
		break;
	}
	g_game_session_started = 1;
	return net_session_init_game_session(
		g_flight_launch_args.arguments[FLIGHT_LAUNCH_ARG_FORMAL_NAME],
		g_flight_launch_args.arguments[FLIGHT_LAUNCH_ARG_PILOT_NAME],
		atoi(g_flight_launch_args.arguments[FLIGHT_LAUNCH_ARG_IS_HOST]),
		g_flight_launch_args.arguments[FLIGHT_LAUNCH_ARG_MP_GAME_NAME],
		network_type,
		atoi(g_flight_launch_args
			     .arguments[FLIGHT_LAUNCH_ARG_NUM_PLAYERS]),
		g_flight_in_progress_launch, connection_address);
}

/* Sets the distance scale for model detail levels from the level-of-detail setting, capped at its
 * maximum, bent by the configured curve and inverted; clears any forced detail level. */
static void xvt_flight_entry_configure_lod_distance(void)
{
	int lod_config_value =
		g_game_config.lod[net_session_get_player_count() > 1] +
		LOD_CONFIG_OFFSET;
	g_lod_distance_scale = (float)lod_config_value;
	if (g_lod_distance_scale > g_lod_config_max_value) {
		g_lod_distance_scale = (float)LOD_CONFIG_MAX_VALUE;
	}
	g_lod_distance_scale = g_lod_distance_scale * g_lod_config_scale_factor;
	g_lod_distance_scale = g_lod_distance_scale * g_lod_config_curve_double;
	if (g_lod_distance_scale > g_lod_config_curve_threshold) {
		g_lod_distance_scale =
			g_lod_config_curve_threshold /
			(g_lod_config_curve_double - g_lod_distance_scale);
	}
	g_forced_lod_level = 0;
	g_lod_distance_scale =
		(float)LOD_SCALE_INVERSION_NUMERATOR / g_lod_distance_scale;
}

/* Turns mipmapping off when the mipmap setting is at its disabled value; otherwise turns it on and
 * sets the mip distance scale from the setting through the same curve and inversion as the
 * detail levels. */
static void xvt_flight_entry_configure_mipmaps(void)
{
	int mipmap_config_option =
		g_game_config.mipmap[net_session_get_player_count() > 1];
	if (mipmap_config_option != MIPMAPPING_DISABLED_VALUE) {
		int64_t mipmap_config_value =
			g_game_config
				.mipmap[net_session_get_player_count() > 1];
		g_mip_lod_scale = (float)mipmap_config_value;
		g_mip_lod_scale =
			g_mip_lod_scale * g_mipmap_config_scale_factor;
		g_mip_lod_scale = g_mip_lod_scale * g_lod_config_curve_double;
		if (g_mip_lod_scale > g_lod_config_curve_threshold) {
			g_mip_lod_scale =
				g_lod_config_curve_threshold /
				(g_lod_config_curve_double - g_mip_lod_scale);
		}
		g_mipmapping_enabled = 1;
		g_mip_lod_scale =
			(float)LOD_SCALE_INVERSION_NUMERATOR / g_mip_lod_scale;
	} else {
		g_mipmapping_enabled = 0;
	}
}

/* Sets the render size from the screen resolution setting and the window surface size from the
 * window size setting, then the render target width. */
static void xvt_flight_entry_configure_display_size(void)
{
	switch (g_game_config.screen_res[net_session_get_player_count() > 1]) {
	case DISPLAY_CONFIG_LOW:
		g_display_mode_width = DISPLAY_WIDTH_LOW;
		g_display_mode_height = DISPLAY_HEIGHT_LOW;
		break;
	case DISPLAY_CONFIG_MEDIUM:
		g_display_mode_width = DISPLAY_WIDTH_MEDIUM;
		g_display_mode_height = DISPLAY_HEIGHT_MEDIUM;
		break;
	default:
		g_display_mode_width = DISPLAY_WIDTH_HIGH;
		g_display_mode_height = DISPLAY_HEIGHT_HIGH;
		break;
	}
	switch (g_game_config.window_size[net_session_get_player_count() > 1]) {
	case DISPLAY_CONFIG_LOW:
		g_surface_width = DISPLAY_WIDTH_LOW;
		g_surface_height = DISPLAY_HEIGHT_LOW;
		break;
	case DISPLAY_CONFIG_MEDIUM:
		g_surface_width = WINDOW_WIDTH_MEDIUM;
		g_surface_height = WINDOW_HEIGHT_MEDIUM;
		break;
	default:
		g_surface_width = DISPLAY_WIDTH_HIGH;
		g_surface_height = DISPLAY_HEIGHT_HIGH;
		break;
	}
	g_render_target_width = g_display_mode_width;
}

static void xvt_flight_entry_configure(void)
{
	g_flight_brightness_scale_q8 =
		(g_game_config.brightness[net_session_get_player_count() > 1] +
		 BRIGHTNESS_CONFIG_OFFSET)
		<< BRIGHTNESS_CONFIG_SHIFT;
	int brightness_limit = BRIGHTNESS_SCALE_MIN;
	if ((unsigned int)g_flight_brightness_scale_q8 < BRIGHTNESS_SCALE_MIN) {
		g_flight_brightness_scale_q8 = brightness_limit;
	} else {
		brightness_limit = BRIGHTNESS_SCALE_MAX;
		if ((unsigned int)g_flight_brightness_scale_q8 >
		    BRIGHTNESS_SCALE_MAX) {
			g_flight_brightness_scale_q8 = brightness_limit;
		}
	}
	g_backdrops_enabled =
		g_game_config.backdrop[net_session_get_player_count() > 1];
	g_debris_enabled =
		g_game_config.debris[net_session_get_player_count() > 1];
	switch (g_game_config
			.star_density[net_session_get_player_count() > 1]) {
	case 0:
		g_star_grid_divisor = STAR_GRID_DIVISOR_LOW_DENSITY;
		break;
	case 1:
		g_star_grid_divisor = STAR_GRID_DIVISOR_MEDIUM_DENSITY;
		break;
	case 2:
		g_star_grid_divisor = STAR_GRID_DIVISOR_HIGH_DENSITY;
		break;
	default:
		break;
	}
	g_use_hardware3d =
		g_game_config
			.use3d_hardware[net_session_get_player_count() > 1];
	g_bilinear_enabled =
		g_game_config.bilinear[net_session_get_player_count() > 1];
	{
		int bpp_config_value =
			g_game_config.color_depth_choice
				[net_session_get_player_count() > 1];
		switch (bpp_config_value) {
		case DISPLAY_CONFIG_LOW:
			g_flight_bytes_per_pixel = PALETTED_BYTES_PER_PIXEL;
			break;
		case DISPLAY_CONFIG_MEDIUM:
			g_flight_bytes_per_pixel = HIGH_COLOR_BYTES_PER_PIXEL;
			break;
		default:
			g_flight_bytes_per_pixel = PALETTED_BYTES_PER_PIXEL;
			break;
		}
	}
	net_session_get_player_count();
	xvt_flight_entry_configure_lod_distance();
	xvt_flight_entry_configure_mipmaps();
	switch (g_game_config.texture_res[net_session_get_player_count() > 1]) {
	case 0:
		g_texture_resolution_level = 0;
		break;
	case 1:
		g_texture_resolution_level = 1;
		break;
	default:
		g_texture_resolution_level = 2;
		break;
	}
	{
		int local_lights_enabled =
			g_game_config
				.local_lights[net_session_get_player_count() >
					      1];
		g_local_lights_enabled = 1;
		if (local_lights_enabled == 0) {
			g_local_lights_enabled = 0;
		}
	}
	{
		int specular_enabled =
			g_game_config
				.specular[net_session_get_player_count() > 1];
		g_specular_enabled = 1;
		if (specular_enabled == 0) {
			g_specular_enabled = 0;
		}
	}
	{
		int diffuse_lighting_enabled =
			g_game_config
				.diffuse[net_session_get_player_count() > 1];
		g_dir_lighting_enabled = 1;
		if (diffuse_lighting_enabled == 0) {
			g_dir_lighting_enabled = 0;
		}
	}
	{
		int dithering_enabled =
			g_game_config
				.dither[net_session_get_player_count() > 1];
		g_dithering_enabled = 1;
		if (dithering_enabled == 0) {
			g_dithering_enabled = 0;
		}
	}
	xvt_flight_entry_configure_display_size();
	g_requested_flight_bytes_per_pixel = g_flight_bytes_per_pixel;
	g_requested_flight_hardware3d = g_use_hardware3d;
}

int xvt_flight_entry_create_devices(void)
{
	xvt_flight_entry_configure();
	g_flight_devices_created = 1;
	if (flight_display_init() == 0) {
		return 0;
	}

	switch (g_display_mode_width) {
	case DISPLAY_WIDTH_LOW:
		g_game_config.screen_res[net_session_get_player_count() > 1] =
			DISPLAY_CONFIG_LOW;
		break;
	case DISPLAY_WIDTH_MEDIUM:
		g_game_config.screen_res[net_session_get_player_count() > 1] =
			DISPLAY_CONFIG_MEDIUM;
		break;
	case DISPLAY_WIDTH_HIGH:
		g_game_config.screen_res[net_session_get_player_count() > 1] =
			DISPLAY_CONFIG_HIGH;
		break;
	default:
		break;
	}
	switch (g_flight_bytes_per_pixel) {
	case PALETTED_BYTES_PER_PIXEL:
		g_game_config
			.color_depth_choice[net_session_get_player_count() >
					    1] = DISPLAY_CONFIG_LOW;
		break;
	case HIGH_COLOR_BYTES_PER_PIXEL:
		g_game_config
			.color_depth_choice[net_session_get_player_count() >
					    1] = DISPLAY_CONFIG_MEDIUM;
		break;
	default:
		break;
	}
	g_game_config.use3d_hardware[net_session_get_player_count() > 1] =
		(uint8_t)g_use_hardware3d;
	debug_printf("Init Dinput\n");
	if (g_flight_conf_direct_input != 0 && dinput_init() == 0) {
		g_flight_conf_direct_input = 0;
	}
	debug_printf("Init Dsound\n");
	g_flight_sound_init_start_time_ms = timeGetTime();
	g_sound_engine_started = 1;
	if (sound_init_sound_engine(g_flight_main_window_handle) == 0) {
		flight_display_cleanup_and_report_error(
			DISPLAY_INIT_SOUND_ERROR);
		return 0;
	}

	strcpy(g_current_mission_file,
	       g_flight_launch_args.arguments[FLIGHT_LAUNCH_ARG_MISSION_PATH]);
	return 1;
}

void xvt_flight_entry_cleanup(void)
{
	g_sw3d_skip_odd_scanlines = 0;
	if (g_sound_engine_started) {
		sound_shutdown_sound_engine();
	}
	g_sound_engine_started = 0;
	if (g_flight_devices_created) {
		dinput_shutdown();
	}
	if (g_game_session_started) {
		net_session_shutdown();
		xvt_network_session_end_flight();
	}
	g_game_session_started = 0;
	if (g_flight_devices_created && g_use_hardware3d != 0) {
		std3d_detach_and_release_z_buffer_surface();
		std3d_close();
		std3d_shutdown();
	}
	if (g_flight_devices_created && g_flight_fullscreen != 0) {
		if (g_flight_primary_surface) {
			flight_display_clear_surface(g_flight_primary_surface);
		}
		if (g_flight_page_flip != 0) {
			if (g_flight_back_buffer) {
				flight_display_clear_surface(
					g_flight_back_buffer);
			}
			if (g_flight_offscreen_surface) {
				flight_display_clear_surface(
					g_flight_offscreen_surface);
			}
		}
	}
	while (flight_surface_get_lock_count() > 0) {
		flight_surface_unlock();
	}
	if (g_flight_back_buffer) {
		g_flight_back_buffer->lpVtbl->Release(g_flight_back_buffer);
		g_flight_back_buffer = NULL;
	}
	if (g_flight_primary_surface != NULL) {
		g_flight_primary_surface->lpVtbl->Release(
			g_flight_primary_surface);
		g_flight_primary_surface = NULL;
	}
	if (g_flight_palette != NULL) {
		g_flight_palette->lpVtbl->Release(g_flight_palette);
		g_flight_palette = NULL;
	}
	if (g_flight_page_flip != 0 && g_flight_offscreen_surface != NULL) {
		g_flight_offscreen_surface->lpVtbl->Release(
			g_flight_offscreen_surface);
		g_flight_offscreen_surface = NULL;
	}
	g_flight_render_to_frontend = 1;
	g_use_hardware3d = 0;
	g_flight_devices_created = 0;
	g_flight_render_surface = NULL;
}
