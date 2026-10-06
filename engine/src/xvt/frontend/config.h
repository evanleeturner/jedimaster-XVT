#ifndef XVT_FRONTEND_CONFIG_H
#define XVT_FRONTEND_CONFIG_H

#include <stdint.h>

#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as uint8_t in the binary (IDB enum game_difficulty). */
typedef uint8_t game_difficulty;

enum {
	GAME_DIFFICULTY_EASY = 0x0,
	GAME_DIFFICULTY_MEDIUM = 0x1,
	GAME_DIFFICULTY_HARD = 0x2,
	GAME_DIFFICULTY_EASY_CHEAT = 0x3,
};

#pragma pack(push, 1)

/* The game's settings, saved in config.yaml. Each two-entry video setting holds
 * [0] for single player and [1] for multiplayer; the flight uses entry 1 when
 * its session has more than one player. */
struct game_config {
	/* Space backdrops: 0 off, 1 on. */
	uint8_t backdrop[2];
	/* Star field density: 0 low, 1 medium, 2 high. */
	uint8_t star_density[2];
	/* Space debris: 0 off, 1 on. */
	uint8_t debris[2];
	/* Local light sources: 0 off, 1 on. */
	uint8_t local_lights[2];
	/* Specular highlights: 0 off, 1 on. */
	uint8_t specular[2];
	/* Diffuse lighting: 0 off, 1 on. */
	uint8_t diffuse[2];
	/* Dithering: 0 off, 1 on. */
	uint8_t dither[2];
	/* Texture resolution: 0 low, 1 medium, 2 high. */
	uint8_t texture_res[2];
	/* Mip-mapping slider position, 0 to 19; 19 turns mip-mapping off in
	 * the flight. */
	uint8_t mipmap[2];
	/* Detail distance slider position, 0 to 19. */
	uint8_t lod[2];
	/* Flight screen mode: 0 320 by 240, 1 512 by 384, 2 640 by 480. The
	 * flight writes back the mode it got. */
	uint8_t screen_res[2];
	/* Flight view size: 0 320 by 240, 1 480 by 360, 2 640 by 480; the
	 * config screen keeps it at or under screen_res. */
	uint8_t window_size[2];
	/* Colors: 0 for 256, 1 for 16-bit color. The flight writes back what
	 * it got; turning on 3D hardware sets 1. */
	uint8_t color_depth_choice[2];
	/* Brightness slider position, 0 to 7. */
	uint8_t brightness[2];
	/* 3D hardware: 0 off, 1 on. The flight writes back whether it got
	 * it. */
	uint8_t use3d_hardware[2];
	/* Bilinear filtering: 0 off, 1 on; only offered with 3D hardware. */
	uint8_t bilinear[2];
	/* Network transport: NET_TRANSPORT_IPX, TCPIP, MODEM or SERIAL. */
	uint8_t network_type;
	char phone_number[64]; /* Number dialed for a direct modem game. */
	char ip_address[64];   /* Address to join for a TCP/IP game. */
	/* Name of the pilot to load at startup; config_write stores the
	 * current pilot's. */
	char last_pilot_name[13];
	uint8_t reserved_after_last_pilot_name; /* Never read or written by name. */
	char password[16];			/* Game session password. */
	/* 1 to play over the internet; saved as "async_flag". */
	uint8_t internet_play;
	/* Host's server update rate: 4, 6 or 8, picked low to high on the
	 * network page. */
	uint8_t server_update_rate;
	uint8_t sfx_exterior_enabled; /* Exterior flight sounds: 0 off, 1 on. */
	uint8_t sfx_interior_enabled; /* Cockpit sounds: 0 off, 1 on. */
	uint8_t sfx_engine_enabled;   /* Engine sound: 0 off, 1 on. */
	/* Pilot messages: 0 off, 1 some (the chance halved), 2 all. */
	uint8_t voice_pilot_level;
	/* Tactical officer messages: 0 off, 1 some, 2 all. */
	uint8_t voice_tactical_officer_level;
	uint8_t voice_commander_enabled; /* Commander messages: 0 off, 1 on. */
	/* Special mission messages: 0 off, 1 on. */
	uint8_t voice_special_enabled;
	/* Flight music: 0 off, 1 on; saved as "music". */
	uint8_t music_enabled;
	/* Frontend sound volume, 0 to 9; frontend sounds play at 12 times
	 * it. */
	uint8_t sfx_datapad_volume;
	uint8_t sfx_exterior_volume; /* Exterior sound volume, 0 to 9. */
	uint8_t sfx_interior_volume; /* Cockpit sound volume, 0 to 9. */
	uint8_t sfx_engine_volume;   /* Engine sound volume, 0 to 9. */
	uint8_t voice_volume;	     /* Voice volume, 0 to 9. */
	/* Music volume, 0 to 9; the CD plays at 0xFFFF times it divided by
	 * 9. */
	uint8_t music_volume;
	/* Frontend music (CD track 7): 0 off, 1 on; saved as
	 * "datapad_music". */
	uint8_t datapad_music_enabled;
	/* Action code of each joystick button 1 to 16 (entries 0 to 15) and
	 * hat direction (16 to 19), from the joystick action list. */
	uint8_t joy_buttons[20];
	/* Difficulty; the flight treats a value above hard as easy. */
	game_difficulty difficulty;
	uint8_t collisions;    /* Collisions: 0 off, 1 on. */
	uint8_t craft_jumping; /* Craft jumping: 0 off, 1 on. */
	/* Random variation of the mission: 0 off, 1 on. */
	uint8_t random_setup;
	/* Battle length, a battle_length; saved as "handicapping". */
	battle_length battle_length_index;
	uint8_t require_password; /* 1 when joining needs the password. */
	uint8_t in_progress_join; /* 1 lets players join a game in progress. */
	craft_selection_mode craft_selection; /* Who may choose craft. */
	uint8_t locate_players;		      /* Locate players: 0 off, 1 on. */
	/* Reinforcement rounds of the players' flight groups. */
	craft_wave_mode craft_waves;
	/* Mission time limit in minutes for a multiplayer flight; 255 by
	 * default. */
	uint8_t mission_time_limit;
	/* Team victory time limit in minutes, for a multiplayer flight;
	 * saved as "last_time_limit". */
	uint8_t last_team_time_limit_minutes;
	/* Random seed a multiplayer flight starts from. */
	unsigned int random_seed;
	/* AI opponents in a multiplayer flight: 0 off, 1 on. */
	uint8_t ai_opponents;
	/* Autobalance, or favor the Imperials, neither or the Rebels. */
	combat_balance_mode combat_balance;
	/* Whether a battle or campaign goes on after a mission; config.yaml
	 * keeps it as game.continue_sequence. */
	sequence_continuation_choice continue_battle_or_campaign;
	/* Frontend sounds: 0 off, 1 on; saved as "sfx_datapad". */
	uint8_t sfx_datapad_enabled;
	/* Help text on buttons: 0 off, 1 on; the help button toggles it. */
	uint8_t help_on;
	/* The four taunts a player sends in flight, edited on the taunts
	 * page. */
	char taunts[4][70];
};

#pragma pack(pop)
typedef char xvt_size_game_config[(sizeof(struct game_config) == 529) ? 1 : -1];

extern struct game_config g_game_config;
extern int g_config_connection_type_editable;

int config_options_datapad_update(int frame_counter);
void config_draw_video_option_rows(void);
void config_draw_screen_resolution_option_row(int is_multiplayer);
void config_draw_window_size_option_row(int config_index);
void config_draw_bits_per_pixel_option_row(int config_index);
void config_draw_brightness_option_row(int config_index);
void config_draw_debris_option_row(int config_index);
void config_draw_backdrop_option_row(int config_index);
void config_draw_star_density_option_row(int config_index);
void config_draw_level_of_detail_option_row(int config_index);
void config_draw_texture_resolution_option_row(int config_index);
void config_draw_dither_option_row(int config_index);
void config_draw_mipmap_option_row(int config_index);
void config_draw_local_lights_option_row(int config_index);
void config_draw_specular_option_row(int config_index);
void config_draw_diffuse_lighting_option_row(int config_index);
void config_draw_use3d_hardware_option_row(int config_index);
void config_draw_bilinear_option_row(int config_index);
void config_draw_two_choice_option_dimmed(uint8_t *value,
					  const struct RECT *rect,
					  frontend_string_id value_base_str_id);
void config_draw_two_choice_option(uint8_t *value, const struct RECT *rect,
				   frontend_string_id value_base_str_id);
void config_draw_two_choice_option_read_only(
	uint8_t *value, const struct RECT *rect,
	frontend_string_id value_base_str_id);
void config_draw_two_choice_option_impl(uint8_t *value, const struct RECT *rect,
					frontend_string_id value_base_str_id,
					int translucent_selection,
					int disable_input);
void config_draw_three_choice_option(uint8_t *value, const struct RECT *rect,
				     frontend_string_id value_base_str_id);
void config_draw_option_slider(uint8_t *value, struct RECT *rect,
			       int value_count,
			       frontend_string_id range_label_id,
			       int play_sound_on_change);
void config_load(void);
void config_write(void);
int config_update_navigation_and_restore_defaults(void);
void config_network_options_screen(void);
void config_draw_two_choice_option_read_only_opaque(
	const uint8_t *value, const struct RECT *rect,
	frontend_string_id value_base_str_id);
void config_draw_three_choice_option_read_only(
	const uint8_t *selected_option, const struct RECT *bar_rect,
	frontend_string_id first_option_string_id);
void config_sound_options_screen(void);
void config_joystick_remap_screen(void);
int config_load_joystick_action_dictionary(void);
uint8_t config_read_joystick_action_picker_key(void);
void config_draw_custom_taunts_page(void);
int credits_update_screen(int frame_counter);

#ifdef __cplusplus
}
#endif

#endif
