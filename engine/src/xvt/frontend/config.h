#ifndef XVT_FRONTEND_CONFIG_H
#define XVT_FRONTEND_CONFIG_H

#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as uint8_t in the binary (IDB enum GameDifficulty). */
typedef uint8_t GameDifficulty;

enum {
	GAME_DIFFICULTY_EASY = 0x0,
	GAME_DIFFICULTY_MEDIUM = 0x1,
	GAME_DIFFICULTY_HARD = 0x2,
	GAME_DIFFICULTY_EASY_CHEAT = 0x3,
};

#pragma pack(push, 1)

struct GameConfig {
	uint8_t backdrop[2];
	uint8_t starDensity[2];
	uint8_t debris[2];
	uint8_t localLights[2];
	uint8_t specular[2];
	uint8_t diffuse[2];
	uint8_t dither[2];
	uint8_t textureRes[2];
	uint8_t mipmap[2];
	uint8_t lod[2];
	uint8_t screenRes[2];
	uint8_t windowSize[2];
	uint8_t bpp[2];
	uint8_t brightness[2];
	uint8_t use3dHardware[2];
	uint8_t bilinear[2];
	uint8_t networkType;
	char phoneNumber[64];
	char ipAddress[64];
	char lastPilotName[13];
	uint8_t reservedAfterLastPilotName;
	char password[16];
	uint8_t asyncFlag;
	uint8_t serverUpdateRate;
	uint8_t sfxExteriorEnabled;
	uint8_t sfxInteriorEnabled;
	uint8_t sfxEngineEnabled;
	uint8_t voicePilotEnabled;
	uint8_t voiceTacticalOfficerEnabled;
	uint8_t voiceCommanderEnabled;
	uint8_t voiceSpecialEnabled;
	uint8_t musicEnabled;
	uint8_t sfxDatapadVolume;
	uint8_t sfxExteriorVolume;
	uint8_t sfxInteriorVolume;
	uint8_t sfxEngineVolume;
	uint8_t voiceVolume;
	uint8_t musicVolume;
	uint8_t datapadMusicEnabled;
	uint8_t joyButtons[20];
	GameDifficulty difficulty;
	uint8_t collisions;
	uint8_t craftJumping;
	uint8_t randomSetup;
	BattleLength battleLengthIndex;
	uint8_t requirePassword;
	uint8_t inProgressJoin;
	CraftSelectionMode craftSelection;
	uint8_t locatePlayers;
	CraftWaveMode craftWaves;
	uint8_t missionTimeLimit;
	uint8_t lastTeamTimeLimitMinutes;
	unsigned int randomSeed;
	uint8_t aiOpponents;
	CombatBalanceMode combatBalance;
	SequenceContinuationChoice continueBattleOrCampaign;
	uint8_t sfxDatapadEnabled;
	uint8_t helpOn;
	char taunts[4][70];
};

#pragma pack(pop)
typedef char xvt_size_GameConfig[(sizeof(GameConfig) == 529) ? 1 : -1];

extern GameConfig g_gameConfig;
extern int g_unusedFrontendConcourseHostLatch;

int Config_OptionsDatapadUpdate(int frameState);
void Config_DrawVideoOptionRows(void);
void Config_DrawScreenResolutionOptionRow(int configIndex);
void Config_DrawWindowSizeOptionRow(int configIndex);
void Config_DrawBitsPerPixelOptionRow(int configIndex);
void Config_DrawBrightnessOptionRow(int configIndex);
void Config_DrawDebrisOptionRow(int configIndex);
void Config_DrawBackdropOptionRow(int configIndex);
void Config_DrawStarDensityOptionRow(int configIndex);
void Config_DrawLevelOfDetailOptionRow(int configIndex);
void Config_DrawTextureResolutionOptionRow(int configIndex);
void Config_DrawDitherOptionRow(int configIndex);
void Config_DrawMipmapOptionRow(int configIndex);
void Config_DrawLocalLightsOptionRow(int configIndex);
void Config_DrawSpecularOptionRow(int configIndex);
void Config_DrawDiffuseLightingOptionRow(int configIndex);
void Config_DrawUse3dHardwareOptionRow(int configIndex);
void Config_DrawBilinearOptionRow(int configIndex);
void Config_DrawOptionCycleDisabled(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId);
void Config_DrawOptionCycle(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId);
void Config_DrawOptionCycleReadOnly(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId);
void Config_DrawOptionCycleImpl(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId,
								int translucentSelection, int disableInput);
void Config_DrawThreeChoiceOption(uint8_t* value, const RECT* rect, FrontendStringId valueBaseStrId);
void Config_DrawOptionSliderImpl(uint8_t* value, RECT* rect, int valueCount, FrontendStringId rangeLabelId,
								 int playSoundOnChange);
void Config_Load(void);
void Config_Write(void);
int Config_UpdateNavigationAndRestoreDefaults(void);
void Config_NetworkOptionsScreen(void);
void Config_DrawNetworkOptionCycleDisabled(const uint8_t* value, const RECT* rect,
										   FrontendStringId valueBaseStrId);
void Config_DrawThreeOptionBar(const uint8_t* selectedOption, const RECT* barRect,
							   FrontendStringId firstOptionStringId);
void Config_SoundOptionsScreen(void);
void Config_JoystickRemapScreen(void);
int Config_LoadJoystickActionDictionary(void);
uint8_t Config_ReadJoystickActionPickerKey(void);
void Config_DrawCustomTauntsPage(void);
int Config_CreditsScreen(int frameState);

#ifdef __cplusplus
}
#endif

#endif
