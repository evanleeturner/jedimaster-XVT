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

/* The game's settings, saved as keyword and value lines (config2.cfg in the
 * original build) or config.yaml (modern). Each two-entry video setting holds
 * [0] for single player and [1] for multiplayer; the flight uses entry 1 when
 * its session has more than one player. */
struct GameConfig {
	/* Space backdrops: 0 off, 1 on. */
	uint8_t backdrop[2];
	/* Star field density: 0 low, 1 medium, 2 high. */
	uint8_t starDensity[2];
	/* Space debris: 0 off, 1 on. */
	uint8_t debris[2];
	/* Local light sources: 0 off, 1 on. */
	uint8_t localLights[2];
	/* Specular highlights: 0 off, 1 on. */
	uint8_t specular[2];
	/* Diffuse lighting: 0 off, 1 on. */
	uint8_t diffuse[2];
	/* Dithering: 0 off, 1 on. */
	uint8_t dither[2];
	/* Texture resolution: 0 low, 1 medium, 2 high. */
	uint8_t textureRes[2];
	/* Mip-mapping slider position, 0 to 19; 19 turns mip-mapping off in
	 * the flight. */
	uint8_t mipmap[2];
	/* Detail distance slider position, 0 to 19. */
	uint8_t lod[2];
	/* Flight screen mode: 0 320 by 240, 1 512 by 384, 2 640 by 480. The
	 * flight writes back the mode it got. */
	uint8_t screenRes[2];
	/* Flight view size: 0 320 by 240, 1 480 by 360, 2 640 by 480; the
	 * config screen keeps it at or under screenRes. */
	uint8_t windowSize[2];
	/* Colors: 0 for 256, 1 for 16-bit color. The flight writes back what
	 * it got; turning on 3D hardware sets 1. */
	uint8_t colorDepthChoice[2];
	/* Brightness slider position, 0 to 7. */
	uint8_t brightness[2];
	/* 3D hardware: 0 off, 1 on. The flight writes back whether it got
	 * it. */
	uint8_t use3dHardware[2];
	/* Bilinear filtering: 0 off, 1 on; only offered with 3D hardware. */
	uint8_t bilinear[2];
	/* Network transport: NET_TRANSPORT_IPX, TCPIP, MODEM or SERIAL. */
	uint8_t networkType;
	char phoneNumber[64]; /* Number dialed for a direct modem game. */
	char ipAddress[64];   /* Address to join for a TCP/IP game. */
	/* Name of the pilot to load at startup; Config_Write stores the
	 * current pilot's. */
	char lastPilotName[13];
	uint8_t reservedAfterLastPilotName; /* Never read or written by name. */
	char password[16];		    /* Game session password. */
	/* 1 to play over the internet; saved as "async_flag". */
	uint8_t internetPlay;
	/* Host's server update rate: 4, 6 or 8, picked low to high on the
	 * network page. */
	uint8_t serverUpdateRate;
	uint8_t sfxExteriorEnabled; /* Exterior flight sounds: 0 off, 1 on. */
	uint8_t sfxInteriorEnabled; /* Cockpit sounds: 0 off, 1 on. */
	uint8_t sfxEngineEnabled;   /* Engine sound: 0 off, 1 on. */
	/* Pilot messages: 0 off, 1 some (the chance halved), 2 all. */
	uint8_t voicePilotLevel;
	/* Tactical officer messages: 0 off, 1 some, 2 all. */
	uint8_t voiceTacticalOfficerLevel;
	uint8_t voiceCommanderEnabled; /* Commander messages: 0 off, 1 on. */
	/* Special mission messages: 0 off, 1 on. */
	uint8_t voiceSpecialEnabled;
	/* Flight music: 0 off, 1 on; saved as "music". */
	uint8_t musicEnabled;
	/* Frontend sound volume, 0 to 9; frontend sounds play at 12 times
	 * it. */
	uint8_t sfxDatapadVolume;
	uint8_t sfxExteriorVolume; /* Exterior sound volume, 0 to 9. */
	uint8_t sfxInteriorVolume; /* Cockpit sound volume, 0 to 9. */
	uint8_t sfxEngineVolume;   /* Engine sound volume, 0 to 9. */
	uint8_t voiceVolume;	   /* Voice volume, 0 to 9. */
	/* Music volume, 0 to 9; the CD plays at 0xFFFF times it divided by
	 * 9. */
	uint8_t musicVolume;
	/* Frontend music (CD track 7): 0 off, 1 on; saved as
	 * "datapad_music". */
	uint8_t datapadMusicEnabled;
	/* Action code of each joystick button 1 to 16 (entries 0 to 15) and
	 * hat direction (16 to 19), from the joystick action list. */
	uint8_t joyButtons[20];
	/* Difficulty; the flight treats a value above hard as easy. */
	GameDifficulty difficulty;
	uint8_t collisions;   /* Collisions: 0 off, 1 on. */
	uint8_t craftJumping; /* Craft jumping: 0 off, 1 on. */
	/* Random variation of the mission: 0 off, 1 on. */
	uint8_t randomSetup;
	/* Battle length, a BattleLength; saved as "handicapping". */
	BattleLength battleLengthIndex;
	uint8_t requirePassword; /* 1 when joining needs the password. */
	uint8_t inProgressJoin;	 /* 1 lets players join a game in progress. */
	CraftSelectionMode craftSelection; /* Who may choose craft. */
	uint8_t locatePlayers;		   /* Locate players: 0 off, 1 on. */
	/* Reinforcement rounds of the players' flight groups. */
	CraftWaveMode craftWaves;
	/* Mission time limit in minutes for a multiplayer flight; 255 by
	 * default. */
	uint8_t missionTimeLimit;
	/* Team victory time limit in minutes, for a multiplayer flight;
	 * saved as "last_time_limit". */
	uint8_t lastTeamTimeLimitMinutes;
	/* Random seed a multiplayer flight starts from. */
	unsigned int randomSeed;
	/* AI opponents in a multiplayer flight: 0 off, 1 on. */
	uint8_t aiOpponents;
	/* Autobalance, or favor the Imperials, neither or the Rebels. */
	CombatBalanceMode combatBalance;
	/* Whether a battle or campaign goes on after a mission; not saved by
	 * the original build's Config_Write. */
	SequenceContinuationChoice continueBattleOrCampaign;
	/* Frontend sounds: 0 off, 1 on; saved as "sfx_datapad". */
	uint8_t sfxDatapadEnabled;
	/* Help text on buttons: 0 off, 1 on; the help button toggles it. */
	uint8_t helpOn;
	/* The four taunts a player sends in flight, edited on the taunts
	 * page. */
	char taunts[4][70];
};

#pragma pack(pop)
typedef char xvt_size_GameConfig[(sizeof(GameConfig) == 529) ? 1 : -1];

extern GameConfig g_gameConfig;
extern int g_configConnectionTypeEditable;

int Config_OptionsDatapadUpdate(int frameCounter);
void Config_DrawVideoOptionRows(void);
void Config_DrawScreenResolutionOptionRow(int isMultiplayer);
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
void Config_DrawTwoChoiceOptionDimmed(uint8_t *value, const RECT *rect,
				      FrontendStringId valueBaseStrId);
void Config_DrawTwoChoiceOption(uint8_t *value, const RECT *rect,
				FrontendStringId valueBaseStrId);
void Config_DrawTwoChoiceOptionReadOnly(uint8_t *value, const RECT *rect,
					FrontendStringId valueBaseStrId);
void Config_DrawTwoChoiceOptionImpl(uint8_t *value, const RECT *rect,
				    FrontendStringId valueBaseStrId,
				    int translucentSelection, int disableInput);
void Config_DrawThreeChoiceOption(uint8_t *value, const RECT *rect,
				  FrontendStringId valueBaseStrId);
void Config_DrawOptionSlider(uint8_t *value, RECT *rect, int valueCount,
			     FrontendStringId rangeLabelId,
			     int playSoundOnChange);
void Config_Load(void);
void Config_Write(void);
int Config_UpdateNavigationAndRestoreDefaults(void);
void Config_NetworkOptionsScreen(void);
void Config_DrawTwoChoiceOptionReadOnlyOpaque(const uint8_t *value,
					      const RECT *rect,
					      FrontendStringId valueBaseStrId);
void Config_DrawThreeChoiceOptionReadOnly(const uint8_t *selectedOption,
					  const RECT *barRect,
					  FrontendStringId firstOptionStringId);
void Config_SoundOptionsScreen(void);
void Config_JoystickRemapScreen(void);
int Config_LoadJoystickActionDictionary(void);
uint8_t Config_ReadJoystickActionPickerKey(void);
void Config_DrawCustomTauntsPage(void);
int Credits_UpdateScreen(int frameCounter);

#ifdef __cplusplus
}
#endif

#endif
