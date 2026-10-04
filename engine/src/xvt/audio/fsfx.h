#ifndef XVT_AUDIO_FSFX_H
#define XVT_AUDIO_FSFX_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IDs loaded from WAVE\\SFXBLAST.LST, whose first entry occupies slot 4. */
enum flight_sound_id {
	FLIGHT_SOUND_BOMB_1 = 15,
	FLIGHT_SOUND_MAGNETIC_PULSE = 17,
	FLIGHT_SOUND_CHAFF_TRIGGER = 18,
	FLIGHT_SOUND_COUNTERMEASURE_FLARE = 20,
	FLIGHT_SOUND_LARGE_EXPLOSION = 21,
	FLIGHT_SOUND_SMALL_EXPLOSION_FIRST = 22,
	FLIGHT_SOUND_BREAKUP_1 = 26,
	FLIGHT_SOUND_BREAKUP_2 = 27,
	FLIGHT_SOUND_LASER_IMPACT = 28,
	FLIGHT_SOUND_SHIELD_HIT = 29,
	FLIGHT_SOUND_INTERNAL_HIT = 30,
	FLIGHT_SOUND_HULL_HIT_1 = 31,
	FLIGHT_SOUND_HULL_HIT_2 = 32,
	FLIGHT_SOUND_SYSTEM_HIT = 33,
	FLIGHT_SOUND_CRITICAL_WARNING = 34,
	FLIGHT_SOUND_MISSION_TIMER_WARNING = 35,
	FLIGHT_SOUND_GENERAL_WARNING = 36,
	FLIGHT_SOUND_DANGER_WARNING = 37,
	FLIGHT_SOUND_MISSILE_LOCK_3 = 41,
	FLIGHT_SOUND_CONFIRM_BEEP = 42,
	FLIGHT_SOUND_SMALL_CLICK = 43,
	FLIGHT_SOUND_SETTING_OFF = 44,
	FLIGHT_SOUND_SETTING_VERY_LOW = 45,
	FLIGHT_SOUND_SETTING_LOW = 46,
	FLIGHT_SOUND_SETTING_MEDIUM = 47,
	FLIGHT_SOUND_SETTING_HIGH = 48,
	FLIGHT_SOUND_TARGET_SELECTED = 49,
	FLIGHT_SOUND_TRACTOR_FIRE = 52,
	FLIGHT_SOUND_JAMMING_FIRE = 55,
	FLIGHT_SOUND_DECOY_FIRE = 58,
	FLIGHT_SOUND_ENERGY_TRANSFER_FIRE = 60,
	FLIGHT_SOUND_TIE_FLYBY = 72,
	FLIGHT_SOUND_SHUTTLE_FLYBY = 73,
	FLIGHT_SOUND_X_WING_FLYBY = 74,
	FLIGHT_SOUND_Y_WING_FLYBY = 75,
	FLIGHT_SOUND_A_WING_FLYBY = 76,
	FLIGHT_SOUND_MILLENNIUM_FALCON_FLYBY = 77,
	FLIGHT_SOUND_ENGINE_WASH_CAPITAL = 78,
	FLIGHT_SOUND_ENGINE_WASH_OTHER = 79,
	FLIGHT_SOUND_HYPERSPACE_ENTER_IMPERIAL = 80,
	FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL = 81,
	FLIGHT_SOUND_HYPERSPACE_ENTER_NON_IMPERIAL = 83,
	FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL = 84,
	FLIGHT_SOUND_S_FOIL = 85,
	FLIGHT_SOUND_R2_HAPPY = 86,
	FLIGHT_SOUND_R2_WARNING = 87,
	FLIGHT_SOUND_R2_DANGER = 88,
	FLIGHT_SOUND_R2_HIT = 89,
	FLIGHT_SOUND_MESSAGE_READY = 90,
	FLIGHT_SOUND_INCOMING_ORDER = 91,
	FLIGHT_SOUND_WARNING_BEEP = 92,
	FLIGHT_SOUND_POWER_UP = 93,
	FLIGHT_SOUND_POWER_DOWN = 94,
};

enum flight_voice_speaker_type {
	FLIGHT_VOICE_SPEAKER_SPECIAL = 0,
	FLIGHT_VOICE_SPEAKER_PILOT = 1,
	FLIGHT_VOICE_SPEAKER_TACTICAL = 2,
	FLIGHT_VOICE_SPEAKER_COMMANDER = 3,
};

enum tactical_voice_category {
	TACTICAL_VOICE_STATUS = 1,
	TACTICAL_VOICE_ORDER = 2,
};

/* Offsets into the tactical-officer voice list loaded at SFX slot 696. */
enum tactical_message_id {
	TACTICAL_MSG_WITHDRAWING = 15,
	TACTICAL_MSG_SHIELDS_OUT = 16,
	TACTICAL_MSG_HULL_AT_75_PERCENT = 20,
	TACTICAL_MSG_HULL_AT_25_PERCENT = 21,
	TACTICAL_MSG_HULL_CRITICAL = 22,
	TACTICAL_MSG_UNKNOWN_ATTACKER = 24,
	TACTICAL_MSG_STARFIGHTER_ATTACKER = 25,
	TACTICAL_MSG_STARSHIP_ATTACKER = 26,
	TACTICAL_MSG_DESTROYED = 27,
	TACTICAL_MSG_DISABLED = 28,
	TACTICAL_MSG_REPAIRED_TARGET = 29,
	TACTICAL_MSG_BOARDING_STARTED_TARGET = 30,
	TACTICAL_MSG_CAPTURED_TARGET = 31,
	TACTICAL_MSG_BOARDING_STARTED_FRIENDLY = 37,
	TACTICAL_MSG_TRANSFER_COMPLETE = 38,
	TACTICAL_MSG_BOARDING_STARTED_HOSTILE = 39,
	TACTICAL_MSG_BOARDING_COMPLETE = 40,
	TACTICAL_MSG_MISSILE_ATTACKER = 43,
	TACTICAL_MSG_TORPEDO_ATTACKER = 44,
	TACTICAL_MSG_ROCKET_ATTACKER = 45,
	TACTICAL_MSG_SPACE_BOMB_ATTACKER = 46,
	TACTICAL_MSG_RESUPPLIES_ON_THE_WAY = 51,
	TACTICAL_MSG_FRIENDLY_CRAFT_DESTROYED = 57,
	TACTICAL_MSG_NO_REINFORCEMENTS_AVAILABLE = 75,
	TACTICAL_MSG_REINFORCEMENTS_ACKNOWLEDGED = 76,
	TACTICAL_MSG_REINFORCEMENTS_ALREADY_SENT = 77,
};

extern uint8_t g_fsfx_loaded;

extern char g_fsfx_sfx_name_table[838][24];
extern uint16_t g_fsfx_falloff_distance_by_sfx_slot[96];
extern uint8_t g_fsfx_base_volume_by_sfx_slot[96];
extern uint8_t g_player_engine_loop_object_type;
extern char g_current_mission_file[128];

int fsfx_clear_sfx_name_table(void);
void fsfx_reset_flight_sfx_state(void);
void fsfx_unload_all_effects_thunk(void);
int fsfx_load_sfx_list(char *file_name_buffer, uint16_t first_sound_id);
void fsfx_load_mission_voice_sfx(void);
void fsfx_stop_hyperspace_exit_sounds(int player_idx);
int fsfx_play_sound(unsigned int sound_id, int emitter_obj_idx, int player_idx);
int fsfx_triggerweaponsfx(unsigned int projectile_object_index, int player_idx);
unsigned int fsfx_compute_source_volume(int emitter_obj_idx,
					unsigned int sound_id);
int fsfx_compute_source_pan(int emitter_obj_idx, int *volume);
int fsfx_update_targeting_tone(unsigned int tone_state);
void fsfx_update_beam_system_loop(int active, int player_idx);
void fsfx_update_incoming_missile_warning(int warning_state);
void fsfx_update_chaff_loop(void);
void fsfx_update_player_engine_loop(void);
void fsfx_update_beam_effect_loops(void);
void fsfx_update_flight_sfx(void);
int fsfx_speak_wingman_event(int player_idx, int speaker_obj_idx,
			     int voice_category, int response_index,
			     int target_obj_idx, uint16_t probability);
int fsfx_speak_tactical_officer_event(int voice_category, int message_id,
				      int obj_idx, uint16_t probability);
int fsfx_queue_commander_voice_category(int voice_category,
					int object_signature);
int fsfx_select_available_voice_variant(int voice_category, int craft_ordinal);
uint16_t fsfx_random_index(uint16_t count);
int fsfx_is_voice_queue_empty(void);
int fsfx_queue_voice_sfx(int sfx_slot, char speaker_type, char voice_category,
			 char chain_flag, uint16_t object_signature);
void fsfx_update_voice_queue(void);
void fsfx_prune_stale_voice_queue_entries(void);
void fsfx_remove_voice_queue_entry_chain(unsigned int queue_index);

#ifdef __cplusplus
}
#endif

#endif
