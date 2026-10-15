#include "xvt_runtime/runtime/launch_options.h"

#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"

void xvt_launch_options_apply_settings(
	struct game_config *game, const struct xvt_launch_settings *settings)
{
	if (settings->difficulty != XVT_LAUNCH_SETTING_UNSET) {
		game->difficulty = (game_difficulty)settings->difficulty;
	}
	if (settings->collisions != XVT_LAUNCH_SETTING_UNSET) {
		game->collisions = (uint8_t)settings->collisions;
	}
	if (settings->craft_jumping != XVT_LAUNCH_SETTING_UNSET) {
		game->craft_jumping = (uint8_t)settings->craft_jumping;
	}
	if (settings->random_setup != XVT_LAUNCH_SETTING_UNSET) {
		game->random_setup = (uint8_t)settings->random_setup;
	}
	if (settings->battle_length != XVT_LAUNCH_SETTING_UNSET) {
		game->battle_length_index =
			(battle_length)settings->battle_length;
	}
	if (settings->mission_time_limit != XVT_LAUNCH_SETTING_UNSET) {
		game->mission_time_limit =
			(uint8_t)settings->mission_time_limit;
	}
	if (settings->continue_sequence != XVT_LAUNCH_SETTING_UNSET) {
		game->continue_battle_or_campaign =
			(sequence_continuation_choice)
				settings->continue_sequence;
	}
}

void xvt_launch_options_type_rules(struct game_config *game, int directory)
{
	game->continue_battle_or_campaign = SEQUENCE_CONTINUE;
	if (game->difficulty == GAME_DIFFICULTY_EASY_CHEAT &&
	    directory != MISSION_DIRECTORY_CAMPAIGNS) {
		game->difficulty = GAME_DIFFICULTY_EASY;
	}
	if (game->random_setup == 2 && directory != MISSION_DIRECTORY_BATTLES) {
		game->random_setup = 0;
	}
	if (directory == MISSION_DIRECTORY_CAMPAIGNS) {
		game->random_setup = 0;
	}
}

void xvt_launch_options_network(
	struct game_config *game, const struct game_config *shipped,
	const struct xvt_launch_description *description)
{
	game->difficulty = shipped->difficulty;
	game->collisions = shipped->collisions;
	game->craft_jumping = shipped->craft_jumping;
	game->random_setup = shipped->random_setup;
	game->battle_length_index = shipped->battle_length_index;
	game->in_progress_join = shipped->in_progress_join;
	game->craft_selection = shipped->craft_selection;
	game->locate_players = shipped->locate_players;
	game->craft_waves = shipped->craft_waves;
	game->mission_time_limit = shipped->mission_time_limit;
	game->last_team_time_limit_minutes =
		shipped->last_team_time_limit_minutes;
	game->internet_play = shipped->internet_play;
	game->ai_opponents = shipped->ai_opponents;
	game->server_update_rate = shipped->server_update_rate;
	game->combat_balance = shipped->combat_balance;
	/* The type rules set the sixteenth, the sequence choice. */
	xvt_launch_options_type_rules(game, description->directory_id);
	xvt_launch_options_apply_settings(game, &description->settings);
	game->random_seed = description->seed;
}
