#ifndef XVT_AUDIO_FSFX_H
#define XVT_AUDIO_FSFX_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IDs loaded from WAVE\\SFXBLAST.LST, whose first entry occupies slot 4. */
enum FlightSoundId {
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
	FLIGHT_SOUND_HYPERSPACE_ENTER_ALLIANCE = 80,
	FLIGHT_SOUND_HYPERSPACE_EXIT_ALLIANCE = 81,
	FLIGHT_SOUND_HYPERSPACE_ENTER_EMPIRE = 83,
	FLIGHT_SOUND_HYPERSPACE_EXIT_EMPIRE = 84,
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

enum FlightVoiceSpeakerType {
	FLIGHT_VOICE_SPEAKER_SPECIAL = 0,
	FLIGHT_VOICE_SPEAKER_PILOT = 1,
	FLIGHT_VOICE_SPEAKER_TACTICAL = 2,
	FLIGHT_VOICE_SPEAKER_COMMANDER = 3,
};

enum TacticalVoiceCategory {
	TACTICAL_VOICE_STATUS = 1,
	TACTICAL_VOICE_ORDER = 2,
};

/* Offsets into the tactical-officer voice list loaded at SFX slot 696. */
enum TacticalMessageId {
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

extern uint8_t g_fsfxLoaded;

extern char g_fsfxSfxNameTable[838][24];
extern uint16_t g_fsfxMinDistanceOrRolloffBySfxSlot[96];
extern uint8_t g_fsfxBaseVolumeBySfxSlot[96];
extern uint8_t g_playerEngineLoopObjectType;
extern char g_currentMissionFile[128];

int fsfx_ClearSfxNameTable(void);
void fsfx_ResetFlightSfxState(void);
void fsfx_UnloadAllEffects_Thunk(void);
int fsfx_LoadSfxList(char* fileNameBuffer, uint16_t firstSoundId);
void fsfx_LoadMissionVoiceSfx(void);
void fsfx_StopHyperZoomImp(int playerIdx);
int fsfx_PlaySound(unsigned int soundId, int objOrMissionPointRef, int playerIdx);
int fsfx_triggerweaponsfx(unsigned int arg1, int arg2);
unsigned int fsfx_ComputeSourceVolume(int objOrMissionPointRef, unsigned int soundId);
int fsfx_ComputeSourcePan(int objOrMissionPointRef, int* volume);
int fsfx_UpdateTargetingTone(unsigned int toneState);
void fsfx_UpdateBeamSystemLoop(int active, int playerIdx);
void fsfx_UpdateIncomingMissileWarning(int warningState);
void fsfx_UpdateChaffLoop(void);
void fsfx_UpdatePlayerEngineLoop(void);
void fsfx_UpdateBeamEffectLoops(void);
void fsfx_UpdateFlightSfx(void);
int fsfx_speakorderack(int playerIdx, int speakerObjIdx, int voiceCategory, int responseIndex,
					   int targetObjIdx, uint16_t probability);
int fsfx_SpeakTacticalOfficerEvent(int voiceCategory, int messageId, int objIdx, uint16_t probability);
int fsfx_QueueCommanderVoiceCategory(int voiceCategory, int objectSignature);
int fsfx_SelectAvailableVoiceVariant(int voiceCategory, int waveNumber);
uint16_t fsfx_RandomIndex(uint16_t count);
int fsfx_IsVoiceQueueEmpty(void);
int fsfx_QueueVoiceSfx(int sfxSlot, char speakerType, char voiceCategory, char chainFlag,
					   uint16_t objectSerial);
void fsfx_UpdateVoiceQueue(void);
void fsfx_PruneStaleVoiceQueueEntries(void);
void fsfx_RemoveVoiceQueueEntryChain(unsigned int queueIndex);

#ifdef __cplusplus
}
#endif

#endif
