#ifndef XVT_FLIGHT_HUD_HUD_H
#define XVT_FLIGHT_HUD_HUD_H

#include "xvt/flight/hud/msg.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One HUD layout is a set of 144 instrument records loaded from the cockpit .INT files.
enum HudInstrumentTableDimensions {
	HUD_INSTRUMENT_SET_COUNT = 3,
	HUD_INSTRUMENTS_PER_SET = 144,
	HUD_INSTRUMENT_COUNT = HUD_INSTRUMENT_SET_COUNT * HUD_INSTRUMENTS_PER_SET,
};

enum { HUD_SHIELD_TEXT_COLOR_OFFSET = 10 };

enum HudInstrumentSetBaseIndex {
	HUD_COCKPIT_INSTRUMENT_BASE_INDEX = 0,
	HUD_MAP_INSTRUMENT_BASE_INDEX = HUD_INSTRUMENTS_PER_SET,
	HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX = 2 * HUD_INSTRUMENTS_PER_SET,
};

typedef enum HudViewState {
	HUD_VIEW_FORWARD = 0,
	HUD_VIEW_FULL_SCREEN = 18,
	HUD_VIEW_HUD_ONLY = 19,
	HUD_VIEW_TARGET_CAMERA = 20,
	HUD_VIEW_CRAFT_LIST = 21,
} HudViewState;

enum HudMfdElementIndex {
	HUD_MFD_MESSAGE_LOG_ELEMENT = 117,
	HUD_MFD_CRAFT_LIST_ELEMENT = 130,
	HUD_MFD_MAP_OR_COMMAND_ELEMENT = 131,
	HUD_MFD_GOALS_ELEMENT = 132,
	HUD_MFD_DAMAGE_ELEMENT = 133,
	HUD_MFD_SCOREBOARD_ELEMENT = 134,
};

struct HudCockpitResourceDescriptor {
	uint8_t enabled;
	char lfdName[9];
	uint16_t viewportOriginX;
	uint16_t viewportOriginY;
	uint16_t viewportWidth;
	uint16_t viewportHeight;
	int16_t projectionOffsetY;
	char displayName[16];
};

struct HudElementLayout {
	uint16_t x;
	uint16_t y;
	uint16_t selector;
	uint16_t colorIndex;
	uint16_t clipWidth;
	int16_t
		clipHeightOrForegroundColor; ///< Widget-specific .INT payload: clip height or foreground text color.
};

struct HudCockpitResource {
	int16_t memoryHandle;
	uint8_t* entries[3];
};

struct RadarEllipseClampLimit {
	uint8_t xLimit;
	uint8_t yLimit;
};

extern RadarEllipseClampLimit g_radarEllipseClampMode19Preset[37];
extern RadarEllipseClampLimit g_radarEllipseClampTable[37];
extern int g_radarEllipseClampCachedResolutionMode;
extern int16_t radarx;
extern int16_t radary;
extern uint16_t g_hudPanelSpriteDataHandle;
extern uint16_t g_flightIconFramesHandle;
extern uint16_t g_messageLogHandle;
extern HudCockpitResource g_hudCockpitResources[28];
extern HudCockpitResourceDescriptor g_hudCockpitResourceDescriptors[28];
extern char g_hudCockpitResourcePath[32];
extern char g_hudCockpitBasePath[32];
extern HudPanelSpriteFileInfo g_hudPanelSpriteFileInfo;
extern uint8_t g_hudPanelSetId;
extern uint8_t g_hudLoadedPanelSetId;
extern uint8_t g_flightDisplayRebuildPending;
extern uint8_t* g_hudCockpitResourceWriteCursor;
extern int g_hudCockpitResourcesLoaded;
extern int16_t g_hudCachedTargetObjectIdx;
extern const char* g_strWaypointNames[14];
extern const char* g_strMeshComponentNames[33];
extern const char* g_strCmdThreatDisplayText[18];
extern const uint8_t g_messageTextPrefixColorCodes[16];
extern const uint8_t g_messageSenderIffColorCodes[8];
extern const char* g_strThreatDisplayText[4];
extern const uint8_t g_hudShieldColors[22];
extern uint8_t g_lastShieldDamageSide;
extern uint8_t g_flightConfTickCounter;
extern int g_flightTickOverlayLastLoopTicks;
extern int g_flightTickOverlayWindowTicks;
extern int g_flightTickOverlaySampleCount;
extern int g_pingIndicator;
extern int g_lagIndicator;
extern int g_targetDescriptionMessageId;
extern HudInFlightMessageRecord g_systemMessagePane;
extern HudInFlightMessageRecord g_flightGroupMessagePane;
extern HudInFlightMessageRecord g_readyMessagePaneQueue[11];
extern uint8_t g_readyMessageQueueCount;
extern int g_radioMessageBackupEnabled;
extern uint16_t g_replayViewMode;
extern int g_systemMessageDisplayEnabled;

struct HudBeamSegmentOffset {
	uint16_t x;
	uint16_t y;
};

struct HudRadarBlipPoint {
	uint16_t x;
	uint16_t y;
	uint16_t color;
};

extern uint16_t g_radarBlipColor;
extern HudRadarBlipPoint* g_radarForeDrawBlips;
extern uint16_t g_radarForeBlipCount;
extern HudRadarBlipPoint* g_radarAftDrawBlips;
extern uint16_t g_radarAftBlipCount;
extern HudRadarBlipPoint* g_radarForeEraseBlips;
extern HudRadarBlipPoint* g_radarAftEraseBlips;
extern uint16_t g_radarForePrevBlipCount;
extern uint16_t g_radarAftPrevBlipCount;
extern uint8_t g_radarBlipBufferParity;
extern uint8_t g_radarTargetMarkerBackgroundSaved;
extern uint16_t g_radarTargetMarkerRestoreX;
extern uint16_t g_radarTargetMarkerRestoreY;
extern uint16_t g_radarTargetMarkerDrawX;
extern uint16_t g_radarTargetMarkerDrawY;

struct HudPanelSpriteFileInfo {
	char baseName[9];
	uint8_t spriteCount;
	uint8_t spriteCountAddend;
};

typedef enum CmdThreatStringId {
	CMD_THREAT_STR_DIST = 0x0,
	CMD_THREAT_STR_SHD = 0x1,
	CMD_THREAT_STR_HULL = 0x2,
	CMD_THREAT_STR_SYS = 0x3,
	CMD_THREAT_STR_TRG = 0x4,
	CMD_THREAT_STR_NO_CARGO = 0x5,
	CMD_THREAT_STR_THIS_CRAFT = 0x6,
	CMD_THREAT_STR_CURRENT_ORDERS = 0x7,
	CMD_THREAT_STR_NONE = 0x8,
	CMD_THREAT_STR_CURRENT_TARGET = 0x9,
	CMD_THREAT_STR_CURRENT_DESTINATION = 0xA,
	CMD_THREAT_STR_DISTANCE_FROM_TARGET = 0xB,
	CMD_THREAT_STR_DISTANCE_TO_DESTINATION = 0xC,
	CMD_THREAT_STR_TIME_REMAINING = 0xD,
	CMD_THREAT_STR_TIME_TO_TARGET = 0xE,
	CMD_THREAT_STR_TIME_TO_DESTINATION = 0xF,
	CMD_THREAT_STR_D = 0x10,
	CMD_THREAT_STR_L = 0x11,
} CmdThreatStringId;

typedef enum ThreatDisplayStringId {
	THREAT_DISPLAY_STR_LASER = 0x0,
	THREAT_DISPLAY_STR_ION = 0x1,
	THREAT_DISPLAY_STR_WARHEAD = 0x2,
	THREAT_DISPLAY_STR_BEAM = 0x3,
} ThreatDisplayStringId;

/* Stored as int32_t in the binary (IDB enum CockpitOverlayStringId). */
typedef int32_t CockpitOverlayStringId;

enum {
	COCKPIT_OVERLAY_STR_SPD = 0x0,
	COCKPIT_OVERLAY_STR_SPEED = 0x1,
	COCKPIT_OVERLAY_STR_THTL = 0x2,
	COCKPIT_OVERLAY_STR_THROTTLE = 0x3,
	COCKPIT_OVERLAY_STR_L = 0x4,
	COCKPIT_OVERLAY_STR_S = 0x5,
	COCKPIT_OVERLAY_STR_E = 0x6,
	COCKPIT_OVERLAY_STR_B = 0x7,
	COCKPIT_OVERLAY_STR_PLAYER = 0x8,
	COCKPIT_OVERLAY_STR_TEAM = 0x9,
	COCKPIT_OVERLAY_STR_SCORE = 0xA,
	COCKPIT_OVERLAY_STR_KILLS = 0xB,
	COCKPIT_OVERLAY_STR_CRAFT = 0xC,
	COCKPIT_OVERLAY_STR_TARGET = 0xD,
	COCKPIT_OVERLAY_STR_ORDERS = 0xE,
	COCKPIT_OVERLAY_STR_INSPECT = 0xF,
	COCKPIT_OVERLAY_STR_DESTROY = 0x10,
	COCKPIT_OVERLAY_STR_DISABLE = 0x11,
	COCKPIT_OVERLAY_STR_ATTACK = 0x12,
	COCKPIT_OVERLAY_STR_CAPTURE = 0x13,
	COCKPIT_OVERLAY_STR_BOARD = 0x14,
	COCKPIT_OVERLAY_STR_BONUS = 0x15,
	COCKPIT_OVERLAY_STR_PENALTY = 0x16,
	COCKPIT_OVERLAY_STR_F = 0x17,
	COCKPIT_OVERLAY_STR_R = 0x18,
	COCKPIT_OVERLAY_STR_EJECT = 0x19,
	COCKPIT_OVERLAY_STR_LARRY = 0x1A,
	COCKPIT_OVERLAY_STR_PETER = 0x1B,
	COCKPIT_OVERLAY_STR_ALBERT = 0x1C,
	COCKPIT_OVERLAY_STR_BRAD = 0x1D,
	COCKPIT_OVERLAY_STR_BUCKY = 0x1E,
	COCKPIT_OVERLAY_STR_JIM = 0x1F,
	COCKPIT_OVERLAY_STR_JAMES = 0x20,
	COCKPIT_OVERLAY_STR_MARK_W = 0x21,
	COCKPIT_OVERLAY_STR_MARK_S = 0x22,
	COCKPIT_OVERLAY_STR_WODEN = 0x23,
	COCKPIT_OVERLAY_STR_MAX = 0x24,
	COCKPIT_OVERLAY_STR_BILL = 0x25,
	COCKPIT_OVERLAY_STR_JOHN = 0x26,
	COCKPIT_OVERLAY_STR_KO = 0x27,
};

extern HudElementLayout g_hudElementLayouts[HUD_INSTRUMENT_COUNT];
extern int16_t g_hudElementStateCache[HUD_INSTRUMENT_COUNT];
extern uint8_t* g_hudPanelSpriteDataByIndex[265];
extern uint8_t* g_hudPanelSpriteDataWriteCursor;
extern uint16_t g_hudInstrumentSetBaseIndex;
extern uint16_t g_mfdMissionScoreboardBlitWidth;
extern uint16_t g_mfdMissionScoreboardBlitSourceY;
extern uint16_t g_mfdMissionScoreboardBlitHeight;
extern uint16_t g_mfdMissionScoreboardBlitSourceX;
extern uint16_t g_mfdMapBlitSourceY;
extern uint16_t g_mfdMapBlitWidth;
extern uint16_t g_mfdMapBlitHeight;
extern uint16_t g_mfdMapBlitSourceX;
extern uint16_t g_mfdCraftListBlitHeight;
extern uint16_t g_mfdCraftListBlitSourceX;
extern uint16_t g_mfdCraftListBlitSourceY;
extern uint16_t g_mfdCraftListBlitWidth;
extern uint16_t g_mfdGoalsBlitHeight;
extern uint16_t g_mfdDamageBlitSourceX;
extern uint16_t g_mfdGoalsBlitSourceX;
extern uint16_t g_mfdDamageBlitHeight;
extern uint16_t g_mfdGoalsBlitWidth;
extern uint16_t g_mfdDamageBlitWidth;
extern uint16_t g_mfdDamageBlitSourceY;
extern uint16_t g_mfdGoalsBlitSourceY;
extern uint8_t g_hudFullRedrawInProgress;
extern int g_readyMessagePaneLeft;
extern int g_readyMessagePaneTop;
extern int g_readyMessagePaneRight;
extern int g_readyMessagePaneBottom;
extern uint8_t g_hudBeamSegmentFadeByChargeStep[4];
extern const HudBeamSegmentOffset g_hudBeamSegmentOffsets480x360[9];
extern const HudBeamSegmentOffset g_hudBeamSegmentOffsets320x240[9];
extern const char g_countermeasureAmmoWidthText[4];
extern const char g_missionClockMinutesWidthText[4];
extern const uint8_t g_lfdPaletteResourceTypeTag[4];
extern uint8_t g_targetLockActive;
extern uint8_t g_hudViewportSpanMask2[480];
extern uint8_t g_hudViewportSpanMask0[480];
extern uint8_t g_hudViewportSpanMask1[480];
extern const char* g_strCockpitOverlayText[40];

void Hud_DrawBoxOverlayHW(int x, int y, int width, int height, int colorIdx, int depth);
int Hud_SetHudViewState(int hudViewState, int playerIdx);
void Hud_InitHUD(int playerIdx);
void Hud_RenderHud(int playerIdx);
void Hud_DrawHudTargetInsetIfEnabled(int playerIndex);
void Hud_DrawStaticCockpitText(uint16_t playerIdx);
void nullsub_6(int playerIdx);
void Hud_UpdateHUD(void);
void Hud_UpdateForwardPanel(void);
void Hud_DrawMapViewOverlay(void);
void Hud_UpdateCMDText(void);
void Hud_DrawRadarBlips(void);
void Hud_AddBlipToRadar(int16_t objIdx);
void Hud_UpdateTargetingComputerDisplay(void);
void Hud_AppendObjectDisplayName(uint16_t objectRef, int16_t displayFlags);
int Hud_MissionFG_GetCraftNumberIfShown(int flightGroupIdx, const CraftData* craft);
void Hud_DrawTargetDistance(int polarDistance);
void Hud_UpdateTargetingLockIndicator(void);
void Hud_DrawReticle3D(void);
void Hud_UpdateWarheadCnt(void);
void Hud_OutputWarheadCount(uint16_t warheadSlotIdx, uint16_t displaySlot, uint16_t warheadBank);
void Hud_DrawShieldStrength2D(void);
void Hud_DrawBeamStrength2D(void);
void Hud_UpdateSpeedPercent(void);
void Hud_UpdateThrottlePercent(void);
void Hud_UpdateMissionClockDisplay(void);
void Hud_DrawPowerSettings2D(void);
void Hud_DrawCachedSegmentedBar(uint16_t filledCount, uint16_t elementIdx, uint16_t segmentCount,
								int16_t yStep);
void Hud_UpdateThreatIndicators(int hudMode);
void Hud_UpdateCriticalHullShieldWarning(void);
void Hud_UpdateCountermeasureStatus(void);
void Hud_ClearUnavailableCraftSystemIndicators(void);
void Hud_UpdateCraftSystemStatusIndicators(void);
void Hud_DrawCmdTargetDetails(void);
void Hud_DrawCmdTargetStatusIndicators(void);
void Hud_DrawCachedSpriteElement(unsigned int elementIdx, unsigned int state);
void Hud_DrawCachedFadedSpriteElement(uint16_t elementIdx, int16_t state, int16_t fade);
void Hud_DrawCachedNumericElement(uint16_t elementIdx, int16_t value, uint16_t minDigits);
void Hud_LoadCockpitResources(void);
void Hud_LoadCockpitInterfaceFile(const char* basePath);
int16_t Hud_LoadAuxiliaryCockpitInterfaceFile(void);
void Hud_ForcePlayerViewState(int hudViewState, int playerIdx);
void Hud_RebuildDisplayForViewState(int hudViewState, int playerIdx);
void Hud_LoadCockpitLfdEntries(const char* lfdName, uint8_t** outEntries, unsigned int entryCount);
void Hud_LoadCockpitSpriteResources(unsigned int modelIndex);
void Hud_ReloadCockpitInterfaceFile(void);
void Hud_UpdateMfdPages(void);
void Hud_BlitSoftwareMfdPages(void);
void Hud_Update3DCrt(uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4, int16_t a5);
void Hud_DrawComponentMarkerBox(int x, int y, int width, int height, uint8_t colorIdx);
void Hud_PointCamera(uint16_t targetIdx, int16_t useHudLayoutScale, int playerIdx);
void Hud_ResetFlightMessagePanes(int forceExpireActiveMessages);
void Hud_ShiftReadyMessageQueueForReplacement(void);
void Hud_AdvanceReadyMessageQueue(void);
void Hud_ShowFlightMessagePane(int16_t paneType);
void Hud_SetupReadyMessagePaneText(void);
void Hud_SetFlightMessagePaneTimer(int16_t paneType, char lastChar);
void Hud_UpdateFlightMessagePanes(void);
void Hud_ClearReadyMessageQueue(void);
void Hud_AdvanceFlightMessagePaneTimers(void);
void Hud_DrawCraftNameFpsAndNetworkStatus(void);
uint16_t Hud_GetSystemMessagePaneState(void);
void Hud_BlitSoftwareHudTextPanes(void);
uint16_t Hud_MeasureFlightMessagePaneText(int16_t paneType);
void Hud_DrawBoxInXTrans(int x, int y, int width, int height, int colorIdx, int depth);
int16_t Hud_LoadPanelSpriteRecords(const char* fileName, uint16_t firstSpriteIndex, int16_t spriteCount,
								   uint16_t recordsToSkip);
int FlightIcon_LoadFrames(char* fileName, uint8_t* dataBuffer, uint8_t** framePointers);

#ifdef __cplusplus
}
#endif

#endif
