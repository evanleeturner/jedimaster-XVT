#include "xvt/flight/flight.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/snapshot/world_state.h"
#endif

#include "xvt/assets/file.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/audio/music_cd.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/flight_render.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_alert.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/flight_player.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/dinput.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XVT_MODERN
/* The Windows message record PeekMessageA fills in Flight_PumpWindowMessages,
 * in the original build. */
struct FlightWin32Message {
	void *window; /* Window the message is for; not read by name. */
	/* Message number; Flight_PumpWindowMessages tests it. */
	uint32_t message;
	uint32_t wParam; /* First message argument; not read by name. */
	int32_t lParam;	 /* Second message argument; not read by name. */
	uint32_t time;	 /* Time the message was posted; not read by name. */
	int32_t pointX;	 /* Cursor X when posted; not read by name. */
	int32_t pointY;	 /* Cursor Y when posted; not read by name. */
};

__declspec(dllimport) int __stdcall UpdateWindow(void *hWnd);
__declspec(dllimport) void *__stdcall SetFocus(void *hWnd);
__declspec(dllimport) int32_t __stdcall
DefWindowProcA(void *hWnd, unsigned int Msg, uint32_t wParam, int32_t lParam);
__declspec(dllimport) void *__stdcall GetForegroundWindow(void);
__declspec(dllimport) int __stdcall SetForegroundWindow(void *hWnd);
__declspec(dllimport) int __stdcall ShowCursor(int show);
__declspec(dllimport) int __stdcall
PeekMessageA(struct FlightWin32Message *message, void *hWnd,
	     unsigned int filterMin, unsigned int filterMax,
	     unsigned int removeMessage);
__declspec(dllimport) int __stdcall
TranslateMessage(const struct FlightWin32Message *message);
__declspec(dllimport) int32_t __stdcall
DispatchMessageA(const struct FlightWin32Message *message);
#endif

/* Per graphics detail preset 0 to 3, the value Flight_ApplyGraphicsDetailPreset
 * gives g_graphicsDetailDistanceThreshold: 0x1000, 0x2000, 0x4000, 0x7FFF. */
// GLOBAL: XVT 0x527348
const uint16_t g_graphicsDetailDistanceThresholdByPreset[4] = {0x1000, 0x2000,
							       0x4000, 0x7FFF};
/* Per graphics detail preset, the value Flight_ApplyGraphicsDetailPreset gives
 * g_starGridDivisor: 2 at preset 0, else 1. */
// GLOBAL: XVT 0x527358
const uint16_t g_starGridDivisorByGraphicsDetailPreset[4] = {2, 1, 1, 1};
/* Per graphics detail preset, the value Flight_ApplyGraphicsDetailPreset gives
 * g_backdropsEnabled: 1 only at preset 3. */
// GLOBAL: XVT 0x527360
const uint16_t g_backdropsEnabledByGraphicsDetailPreset[4] = {0, 0, 0, 1};
/* Per graphics detail preset, the value Flight_ApplyGraphicsDetailPreset gives
 * g_debrisEnabled: 1 at presets 2 and 3. */
// GLOBAL: XVT 0x527368
const uint16_t g_debrisEnabledByGraphicsDetailPreset[4] = {0, 0, 1, 1};
/* Resolution mode to go back to when the flight ends. Nothing writes it, so it
 * stays FLIGHT_RESOLUTION_320X240; Flight_MainLoop and
 * XvtFlightTask_ReleaseMission hand it to FlightDisplay_ApplyResolutionMode,
 * which does nothing, when it differs from g_flightResolutionMode. */
// GLOBAL: XVT 0x5233F0
int g_preFlightResolutionMode = FLIGHT_RESOLUTION_320X240;
/* 20.0: the cap on the level-of-detail option plus 5 that g_lodDistanceScale is
 * worked out from (Flight_Main, XvtFlightEntry_ConfigureLodDistance). Never
 * written. */
// GLOBAL: XVT 0x518218
const float g_lodConfigMaxValue = 20.0f;
/* 0.04: multiplies the level-of-detail option plus 5 on the way to
 * g_lodDistanceScale. Never written. */
// GLOBAL: XVT 0x51821C
const float g_lodConfigScaleFactor = 0.04f;
/* 2.0: the level-of-detail and mipmap values are doubled, and above
 * g_lodConfigCurveThreshold become 1 over 2 less the value, before
 * g_lodDistanceScale and g_mipLodScale take 1 over the result. Never
 * written. */
// GLOBAL: XVT 0x518220
const float g_lodConfigCurveDouble = 2.0f;
/* 1.0: the value above which the level-of-detail and mipmap curves bend (see
 * g_lodConfigCurveDouble), and the numerator of the bend. Never written. */
// GLOBAL: XVT 0x518224
const float g_lodConfigCurveThreshold = 1.0f;
/* 1/19: multiplies the mipmap option on the way to g_mipLodScale; an option of
 * 19 turns mipmapping off instead (Flight_Main,
 * XvtFlightEntry_ConfigureMipmaps). Never written. */
// GLOBAL: XVT 0x518228
const float g_mipmapConfigScaleFactor = 0.052631579f;
/* Minutes into the flight music track (track 2) at which a flight's music may
 * start; flight start picks one of the four at random (GameRand2), with the
 * second from g_dynamicMusicInitialStartSecondChoices (Flight_MainLoop,
 * XvtFlightLoading_Runtime). Never written. */
// GLOBAL: XVT 0x523644
uint8_t g_dynamicMusicInitialStartMinuteChoices[4] = {0, 4, 8, 12};
/* Seconds added to the start minute picked from
 * g_dynamicMusicInitialStartMinuteChoices, same index. Never written. */
// GLOBAL: XVT 0x523648
uint8_t g_dynamicMusicInitialStartSecondChoices[4] = {0, 1, 40, 52};
/* "paiplan": the name flight start passes to pai_loadplans to load the AI plans
 * (Flight_MainLoop, XvtFlightLoading_Globals). */
// GLOBAL: XVT 0x5236BC
const char g_paiPlanResourceBaseName[8] = "paiplan";
/* 1 when the launch command line holds "traincourse": flight start then sets
 * the proving grounds craft type to 2 and level to 4 before the mission loads
 * (Flight_MainLoop, XvtFlightLoading_MissionSetup). Set by Flight_Main in the
 * original build and XvtFlightEntry_ReadLaunchSwitches in the modern one. */
// GLOBAL: XVT 0x527E98
int g_flightConfTrainCourse = 0;
/* Set to 1 when the launch command line starts with "/+" (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches); nothing reads it. */
// GLOBAL: XVT 0x527E9C
int g_unusedFlightCmdLinePlusSwitchFlag = 0;
/* 1 when the launch command line holds "nolauncher" (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches); nothing reads it. */
// GLOBAL: XVT 0x527EB0
int g_flightConfNoLauncher = 0;
/* Per keypad look key 1 to 9 (indexed by the key less keypad 1), the HUD view
 * state Flight_ProcessPlayerActions shows, added to the view's
 * hudAimXSnapState; a sum of 0 means the player's saved view. Entry 4, keypad
 * 5, is not used: that key has its own case. */
// GLOBAL: XVT 0x5272D1
const uint8_t g_hudViewStateOffsetByLookAction[9] = {3, 4, 5, 2, 16,
						     6, 1, 0, 7};
/* Per keypad look key 1 to 9, the hudAimY Flight_ProcessPlayerActions gives the
 * view: the look direction's turn (a full circle is 65,536). Entry 4 is not
 * used. */
// GLOBAL: XVT 0x5272E2
const int16_t g_hudAimYByLookAction[9] = {
	(int16_t)0xA000, (int16_t)0x8000, 0x6000, (int16_t)0xC000, 0,
	0x4000,		 (int16_t)0xE000, 0,	  0x2000,
};
/* Divisor of the starfield grid (FlightStarfield_Render): 4, 2 or 1 from the
 * star density option at launch (Flight_Main, XvtFlightEntry_Configure), then
 * from the graphics detail preset (Flight_ApplyGraphicsDetailPreset). Starts at
 * 1. */
// GLOBAL: XVT 0x5233FC
uint16_t g_starGridDivisor = 1;
/* 1 while the simulation runs ahead on predicted input in multiplayer, 0 while
 * it runs on confirmed input (a world message from the server, or solo play);
 * many functions read it to hold back effects that must happen once, such as
 * messages, sounds and the end of the mission. Seven functions write it:
 * Flight_MainLoop, Flight_RunMissionLoop and FlightSync_ApplyWorldMessagePacket
 * in the original build; XvtFlightLoading_Globals, XvtFlightFrame_StartAdvance,
 * XvtFlightFrame_Confirm and XvtFlightFrame_NetworkUpdate in the modern one. */
// GLOBAL: XVT 0x523410
int g_flightSimSideEffectsSuppressed = 0;
/* Which simulation pass fsfx_PlaySound lets play: 0, only a pass with side
 * effects on; 1, only a pass with them suppressed; 2, none. In multiplayer,
 * Flight_AdvanceOneStep sets 1 while it applies a local input frame for the
 * first time and 2 when it applies one again, and Flight_RunMissionLoop sets 1
 * while it draws the frame; both set 0 after. Five functions write it: those
 * two, and XvtFlightSim_Advance, XvtFlightFrame_Begin and XvtFlightFrame_Render
 * in the modern build. */
// GLOBAL: XVT 0x523414
int g_flightSfxSideEffectGate = 0;
/* 1 from a world checksum, taken when a world message asks for one, until every
 * player's checksum has matched or a resync replays the world messages; while
 * it is 1 a client keeps the server's world messages for that replay. Many
 * functions write it, chiefly FlightSync_ApplyWorldMessagePacket (1), the
 * checksum and resync handlers (0) and the modern build's frame and resync
 * code; flight start sets 0. */
// GLOBAL: XVT 0x52341C
int g_flightNetBufferWorldMessagesUntilChecksum = 0;
/* g_serverTickTime at the last world checksum; resync packets from another
 * epoch are dropped. Six functions write it: Flight_MainLoop (0 at flight
 * start) and FlightSync_ApplyWorldMessagePacket in the original build;
 * XvtFlightLoading_Globals, XvtFlightTask_StartWorld, XvtFlightFrame_Checksum
 * and XvtResync_FullApply in the modern one. */
// GLOBAL: XVT 0x523420
unsigned int g_flightNetWorldChecksumEpoch = 0;
/* 1 for internet play: the input clock runs 130 ticks ahead of the server
 * instead of 30, remote craft are drawn smoothed, and no predicted remote input
 * is queued. Copied from g_gameConfig.internetPlay at launch (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches), its only writers. */
// GLOBAL: XVT 0x523428
int g_internetPlayEnabled = 0;
/* Set to -1 by Flight_ResetUnusedResumeSlots, its only writer; nothing reads
 * it. */
// GLOBAL: XVT 0x51A84C
static int g_unusedFlightResumeResetSlot0;
/* Set to -1 by Flight_ResetUnusedResumeSlots, its only writer; nothing reads
 * it. */
// GLOBAL: XVT 0x51A850
static int g_unusedFlightResumeResetSlot1;
/* Per subsystem id, its CRAFT_SUBSYSTEM_FLAG_ bit in a craft's
 * workingSubsystems; the last two ids have none. Flight_UpdateTimers sets the
 * bit back when a repair ends. */
// GLOBAL: XVT 0x51BF60
const uint16_t g_subsystemIdToFlag[12] = {
	CRAFT_SUBSYSTEM_FLAG_ENGINES,
	CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS,
	CRAFT_SUBSYSTEM_FLAG_SHIELDS,
	CRAFT_SUBSYSTEM_FLAG_CANNONS,
	CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER,
	CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER,
	CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM,
	CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS,
	CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES,
	CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE,
	0,
	0,
};
/* Per subsystem id, the message argument that names it in IFMSG_086 ("ARG
 * system is ARG"), used by Flight_UpdateTimers and collide_damagecraft. */
// GLOBAL: XVT 0x51BF78
const uint8_t g_subsystemMessageArgById[CRAFT_SUBSYSTEM_COUNT] = {
	90, 91, 98, 92, 96, 100, 95, 101, 97, 99};
/* Per subsystem id, the simulated seconds a failed subsystem takes to repair:
 * collide_damagecraft and collide_laserhitcraft put it in the craft's
 * systemRepairSeconds, and Flight_UpdateTimers counts that down once per
 * second. The last two entries are 0. */
// GLOBAL: XVT 0x51BF88
const uint16_t g_subsystemRepairDuration[12] = {15, 20, 30, 25,	 35, 20,
						40, 20, 20, 100, 0,  0};
/* HUD features a damaging hit can knock out, one picked at random (GameRand() &
 * 15) by collide_damagecraft; each value is a bit of the craft's
 * installedHudFeatureMask. */
// GLOBAL: XVT 0x51BFA0
const uint16_t g_subsystemFailureHudMaskByRandomSlot[16] = {
	0x0200, 0x0010, 0x0020, 0x0002, 0x0400, 0x0180, 0x0010, 0x0008,
	0x0800, 0x0180, 0x0020, 0x0002, 0x1000, 0x0001, 0x0020, 0x0008,
};
/* Copy of the saved world state, as large as g_worldStateBuffer:
 * FlightSync_SnapshotWorldStateForReplay copies the saved state into it at each
 * world checksum, the copy a resync sends to a player whose checksum differs,
 * and a resync gathers a received state in it. Only
 * Flight_AllocWorldStateBuffers and Flight_FreeWorldStateBuffers set the
 * pointer. */
// GLOBAL: XVT 0x550880
uint8_t *g_worldStateDupBuffer;
/* Byte length of each region g_worldChecksum covers, 0 past the last. Written
 * by Flight_ChecksumWorldState in the original build and by the modern build's
 * snapshot and resync code. */
// GLOBAL: XVT 0x550840
unsigned int g_worldChecksumRegionLengths[16] = {0};
/* The saved world state, laid out as Flight_SaveWorldState writes it: saved at
 * flight start and after each world message is applied, and read back by
 * Flight_RestoreWorldState. Only Flight_AllocWorldStateBuffers
 * (Flight_CalculateWorldStateBufferSize bytes) and Flight_FreeWorldStateBuffers
 * set the pointer. */
// GLOBAL: XVT 0x550B88
uint8_t *g_worldStateBuffer;
/* Bytes in use in g_worldStateDupBuffer. Five functions write it:
 * FlightSync_SnapshotWorldStateForReplay, and in the original build
 * FlightSync_ApplyResyncAndReplayWorldMessages and
 * Flight_ApplyWorldStateObjectPresenceMap; in the modern one
 * XvtResync_FullApply and XvtSnapshot_ApplyPresenceMap. */
// GLOBAL: XVT 0x550B8C
int g_worldStateDupSize;
/* Memory handle of g_worldStateDupBuffer, 0 when none; only
 * Flight_AllocWorldStateBuffers and Flight_FreeWorldStateBuffers write it. */
// GLOBAL: XVT 0x550B94
uint16_t g_worldStateDupHandle = 0;
/* The world checksum: per region of the saved world state, up to 16, the sum of
 * its bytes, and 0 past the last region. Computed by Flight_ChecksumWorldState
 * (XvtSnapshot_Checksum in the modern build), sent between the players and
 * compared. */
// GLOBAL: XVT 0x550B98
unsigned int g_worldChecksum[16] = {0};
/* Bytes in use in g_worldStateBuffer. Four functions write it:
 * Flight_SaveWorldState and FlightSync_ApplyResyncAndReplayWorldMessages in the
 * original build, XvtSnapshot_Save and XvtResync_FullApply in the modern
 * one. */
// GLOBAL: XVT 0x550BD8
unsigned int g_worldStateSize;
/* Memory handle of g_worldStateBuffer, 0 when none; only
 * Flight_AllocWorldStateBuffers and Flight_FreeWorldStateBuffers write it. */
// GLOBAL: XVT 0x550BDC
uint16_t g_worldStateHandle = 0;
/* The frontend's main window, which flight draws in and takes messages for;
 * Flight_UpdateAndFocusMainWindow, its only writer, sets it. */
// GLOBAL: XVT 0x66DDD0
void *g_flightMainWindowHandle = NULL;
/* Model index of the craft Flight_UpdateCraftSteeringAndSpeed is working on,
 * its only writer; Flight_SlewObjectSpeedTowardTarget reads the model's
 * acceleration and deceleration rates through it. */
// GLOBAL: XVT 0x9A7394
uint16_t g_curCraftModelIndex = 0;
/* 1 when debris is drawn and the local debris slots are used. Set from the
 * debris option at launch (Flight_Main, XvtFlightEntry_Configure), then from
 * the graphics detail preset (Flight_ApplyGraphicsDetailPreset). */
// GLOBAL: XVT 0x9A73F4
uint8_t g_debrisEnabled = 0;
/* timeGetTime, in ms, of the last music update. Written by
 * Flight_UpdateDynamicMusicState and at flight start (Flight_MainLoop,
 * XvtFlightLoading_Runtime). */
// GLOBAL: XVT 0x9A7EC4
uint32_t g_dynamicMusicLastUpdateMs = 0;
/* 0 when the launch command line holds "novoice", else 1 (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches); the voice loading and queueing code reads
 * it. */
// GLOBAL: XVT 0x9A8C10
uint8_t g_flightConfVoiceEnabled = 0;
/* 0 when the launch command line holds "nomusic", else 1 (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches); nothing reads it. */
// GLOBAL: XVT 0x9D8C28
uint8_t g_flightConfMusicEnabled = 0;
/* 1 when the launch command line holds "nopilot" (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches): flight start then leaves the network
 * players of g_pilotData unmatched to the session's slots
 * (Mission_SyncPilotNetworkPlayersToSessionSlots), and Mission_Init reads it
 * too. */
// GLOBAL: XVT 0x9C8E40
int g_flightConfNoPilot = 0;
/* Cleared at flight start (Flight_MainLoop, XvtFlightLoading_Globals); nothing
 * else uses it. */
// GLOBAL: XVT 0x9C8E50
uint8_t g_unusedFlightRuntimeBlock[768] = {0};
/* Random bytes filled at mission setup (Flight_MainLoop,
 * XvtFlightLoading_MissionSetup): even entries 0 to 124, odd entries 0 to 3.
 * Nothing else reads it. */
// GLOBAL: XVT 0x9CD060
uint8_t g_flightNoiseTable[512] = {0};
/* First object slot of the range each player fills locally, up to
 * g_localDebrisSlotEnd, which the saved world state, the checksums and the
 * presence maps leave out. Set by Mission_Init and put back with the world
 * state (Flight_RestoreWorldState, XvtSnapshot_DecodePrefix). */
// GLOBAL: XVT 0x9A8E24
int g_localTransientSlotStart = 0;
/* 0 when the launch command line holds "nosfx", else 1 (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches); fsfx_PlaySound and the sound loops check
 * it. */
// GLOBAL: XVT 0x9D80C9
uint8_t g_flightConfSfxEnabled = 0;
/* Object the local player's beam is on, 0xFFFF while the beam is off; two
 * functions write it: laser_weaponsfire, and ProvingGrounds_DrawCourseObject
 * for a course object it draws. Damage_QueueCraftBillboardsForObjectType marks
 * that object's billboards as selected, and fsfx_UpdateBeamSystemLoop reads
 * it. */
// GLOBAL: XVT 0x9D8110
uint16_t g_localBeamTargetObjIdx = 0;
/* Ticks until g_targetProximityBlinkBit flips again: Flight_UpdateTimers, its
 * only writer, counts it down and restarts it at 118 or 14 by the bit's new
 * value and by whether the local player's target is nearer than 32 times its
 * type's bounds extent (the distance is shifted down 5 bits before the
 * test). */
// GLOBAL: XVT 0x9A8064
int16_t g_targetProximityBlinkTimer = 0;
/* Steps per simulated second at the current step's length: 236
 * (SIMULATION_TICKS_PER_SECOND) over g_elapsedTicks, at least 1. Many functions
 * write it, chiefly Flight_StepSimToTime and Flight_AdvanceOneStep (the
 * XvtFlightSim_ functions in the modern build) and the per-object updates,
 * which change it for each object and put it back. */
// GLOBAL: XVT 0x9D8112
uint16_t g_simStepsPerSecond = 0;
/* End, one past, of the local slot range that starts at
 * g_localTransientSlotStart; written the same way. */
// GLOBAL: XVT 0x9CD274
int g_localDebrisSlotEnd = 0;
/* Detail distance from the graphics detail preset
 * (g_graphicsDetailDistanceThresholdByPreset); only
 * Flight_ApplyGraphicsDetailPreset writes it, and nothing reads it. */
// GLOBAL: XVT 0x9E8F50
uint16_t g_graphicsDetailDistanceThreshold = 0;
/* 1 when the mission has no .pal file of its own (checked at flight start at 1
 * byte per pixel), so the palette is built from the colors of the loaded
 * models' textures. Three functions write it: Flight_MainLoop,
 * XvtFlightLoading_Palette and FeDiskIo_InitResources, which clears it unless
 * g_paletteGenerationEnabled. */
// GLOBAL: XVT 0x9E8F54
int g_generateMissionPalette = 0;
/* Set to 0 at flight start (Flight_MainLoop, XvtFlightLoading_Globals,
 * XvtFlightTask_StartWorld); nothing reads it. */
// GLOBAL: XVT 0x9E964C
int g_unusedFlightStartupObjectPassState = 0;
/* 1 when FVIEW_ComputeObjectViewMatrix turns the world light direction into
 * each object's space, 0 when it copies it unchanged. Five functions write it:
 * Flight_ApplyGraphicsDetailPreset (1), FlightView_Render, which clears it for
 * course obstacles, and the flight map, HUD 3D display and model preview
 * code. */
// GLOBAL: XVT 0x9EC476
uint8_t g_transformLightDirectionToObjectSpace = 0;
/* Ms left in the music track playing; when Flight_UpdateDynamicMusicState
 * counts it to 0 or below it starts the flight track (track 2) again. INT32_MAX
 * with no music. Written by Flight_UpdateDynamicMusicState and at flight start
 * (Flight_MainLoop, XvtFlightLoading_Runtime). */
// GLOBAL: XVT 0x9EC468
int g_dynamicMusicTrackRemainingMs = 0;
/* Length of the current simulation step, in ticks. Many functions write it,
 * chiefly Flight_StepSimToTime and Flight_AdvanceOneStep (the XvtFlightSim_
 * functions in the modern build) and the per-object updates, which change it
 * for each object and put it back. */
// GLOBAL: XVT 0x9EC5FE
uint16_t g_elapsedTicks = 0;
/* Tick the simulation has reached. Many functions write it, chiefly
 * Flight_StepSimToTime, Flight_AdvanceOneStep and Flight_RunMissionLoop in the
 * original build and the XvtFlightSim_ and XvtFlightFrame_ functions in the
 * modern one; the wait for the mission start sets 0. */
// GLOBAL: XVT 0x9E9658
int g_gameTime = 0;
/* -1, or the one object Flight_UpdateCraftSteeringAndSpeed and
 * Object_UpdateLifetimeAndMovement move alone while Flight_AdvanceOneStep
 * (XvtFlightSim_Advance) brings a player's craft up to an input frame's time.
 * Flight start sets -1. */
// GLOBAL: XVT 0x9EC5D0
int g_singleObjectUpdateOverrideIdx = -1;
/* The flight's shared countdown timers (see FlightGlobalCountdownTimers),
 * counted down by Flight_UpdateTimers and cleared by
 * Mission_InitFlightRuntimeState. */
// GLOBAL: XVT 0x9EC5E0
FlightGlobalCountdownTimers g_flightGlobalCountdownTimers = {0};
/* Players taking part (participationState 1 or 2): the session's player count
 * at flight start, then recounted by Flight_UpdateActivePlayerCount and
 * Flight_RecountPlayersAndCheckMissionEnd. Four functions write it: those two,
 * Flight_MainLoop and XvtFlightLoading_Globals. */
// GLOBAL: XVT 0x9FD394
int g_activeFlightPlayerCount;
/* Set to 0 at mission setup (Flight_MainLoop, XvtFlightLoading_MissionSetup);
 * nothing reads it. */
// GLOBAL: XVT 0x9FD390
uint8_t g_unusedFlightMessageRuntimeState = 0;
/* Players in the session at flight start (Flight_MainLoop,
 * XvtFlightLoading_Globals), put back with the world state; 1 means solo
 * play. */
// GLOBAL: XVT 0xA080F8
int g_flightPlayerCount = 0;
/* Time stamp of the newest local input frame applied in multiplayer, so one
 * applied again is known (g_flightSfxSideEffectGate 2). Four functions write
 * it: Flight_AdvanceOneStep and Flight_RunMissionLoop (0 at its start) in the
 * original build, XvtFlightSim_Advance and XvtFlightFrame_Begin in the modern
 * one. */
// GLOBAL: XVT 0xA0085C
int g_lastLocalReplayInputTimestamp = 0;
/* Per simulation update length in ticks, the frames that took it (the last
 * bucket counts 19 or more), counted only with the tickcounter launch option.
 * Written by Flight_RunMissionLoop, which clears it at its start, in the
 * original build and by XvtFlightFrame_Begin and XvtFlightFrame_Render in the
 * modern one. */
// GLOBAL: XVT 0x523650
unsigned int g_flightUpdateDurationHistogram[20] = {0};
/* Packet loss score behind g_packetDropIndicator: up 10 for each new drop on
 * the host link, down 1 per frame. It changes only while
 * g_flightPrevHostPacketDropCount is nonzero, which never happens, so it stays
 * 0. Written by Flight_RunMissionLoop in the original build and
 * XvtFlightFrame_Begin and XvtFlightFrame_UpdatePacketDropIndicator in the
 * modern one. */
// GLOBAL: XVT 0x556974
int g_flightPacketDropScore = 0;
/* Ticks between the last two step targets, from which the next is placed.
 * Written by Flight_RunMissionLoop in the original build and
 * XvtFlightFrame_Begin, XvtFlightFrame_StartAdvance and
 * XvtFlightFrame_NetworkUpdate in the modern one. */
// GLOBAL: XVT 0x556978
int g_predictedFrameDelta = 0;
/* Tick the simulation was last stepped to, 0 at the mission loop's start.
 * Written by Flight_RunMissionLoop in the original build and
 * XvtFlightFrame_Begin, XvtFlightFrame_Advance and XvtFlightFrame_NetworkUpdate
 * in the modern one. */
// GLOBAL: XVT 0x55697C
int g_flightLastStepTargetTimestamp = 0;
/* The host link's drop count (NetReliable_GetPeerPacketDropCountByDpid) at the
 * last frame, 0 at the mission loop's start. Its only update sits in the branch
 * taken when it is nonzero, so it stays 0 and g_packetDropIndicator with it.
 * Written by Flight_RunMissionLoop in the original build and
 * XvtFlightFrame_Begin and XvtFlightFrame_UpdatePacketDropIndicator in the
 * modern one. */
// GLOBAL: XVT 0x556980
int g_flightPrevHostPacketDropCount = 0;
/* Music track playing: 0 none, 2 the flight track, 3 to 7 an outcome track
 * Flight_UpdateDynamicMusicState picks by the local player's team goal status.
 * Written by Flight_UpdateDynamicMusicState and at flight start
 * (Flight_MainLoop, XvtFlightLoading_Runtime). */
// GLOBAL: XVT 0x9FD434
uint8_t g_dynamicMusicState = 0;
/* The flight's mission state (see FlightMissionState); saved and restored with
 * the world state. Many functions write it, chiefly the mission code, flight
 * start and Flight_UpdateTimers. */
// GLOBAL: XVT 0x9D6940
FlightMissionState g_flightMissionState = {0};
/* 1 once an outcome track has started (Flight_UpdateDynamicMusicState); flight
 * start clears it (Flight_MainLoop, XvtFlightLoading_Runtime). */
// GLOBAL: XVT 0xA004C8
uint8_t g_dynamicMusicOutcomeLatched = 0;
/* 1 when backdrops are drawn. Set from the backdrop option at launch
 * (Flight_Main, XvtFlightEntry_Configure), then from the graphics detail preset
 * (Flight_ApplyGraphicsDetailPreset). */
// GLOBAL: XVT 0xA080FC
uint8_t g_backdropsEnabled = 0;
/* Set to 0 at flight start (Flight_MainLoop, XvtFlightLoading_Globals); nothing
 * reads it. */
// GLOBAL: XVT 0xA081F0
int g_unusedFlightSessionResetState = 0;
/* A debug log file nothing opens: flight start sets NULL, and the flight's end
 * closes it when set (Flight_MainLoop, XvtFlightTask_ReleaseMission). */
// GLOBAL: XVT 0xA08210
XvtFile *g_unusedFlightDebugLogFile = NULL;
/* A byte saved and restored with the world state and folded into the live
 * checksum. Only the restores write it (Flight_RestoreWorldState,
 * XvtSnapshot_DecodePrefix), so it stays 0. */
// GLOBAL: XVT 0x9D8C20
uint8_t g_worldStateReservedByte = 0;
/* Debris slot count saved and restored with the world state and folded into the
 * checksums; Mission_Init sets it to 16. */
// GLOBAL: XVT 0x9A7B50
int g_worldStateDebrisSlotCount = 0;
/* A word saved and restored with the world state and folded into the live
 * checksum. Only the restores write it (Flight_RestoreWorldState,
 * XvtSnapshot_DecodePrefix), so it stays 0. */
// GLOBAL: XVT 0x9A73A4
int g_unusedWorldStateSerializedDword = 0;
/* 1 when the launch command line holds "newnet" (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches); the options sync with the other players
 * (FlightNet_SyncPlayerOptionsAndTaunts, XvtFlightNetwork_AcceptRoster) also
 * writes it, and the live checksum includes it. */
// GLOBAL: XVT 0xA08138
int g_flightConfNewNet = 0;
/* Set to 1 at launch (Flight_Main, XvtFlightEntry_ReadLaunchSwitches) and put
 * back with the world state; laser_fireplayerweapon reads it. */
// GLOBAL: XVT 0x523424
int g_laserFireTimestampTrackingEnabled = 0;
/* Never written, so it stays 0 and the early returns that test it
 * (Flight_UpdatePlayerStep, XvtFlightSim_UpdatePlayerStep,
 * XvtFlightControls_ThrottleEligible) never run. */
// GLOBAL: XVT 0xA07C70
uint8_t g_dormantFlightRegionSessionEarlyReturnFlag = 0;
/* Flipped by the Alt+M key in Flight_UpdatePlayerStep
 * (XvtFlightSim_UpdatePlayerStep in the modern build), its only writers;
 * nothing else reads it. */
// GLOBAL: XVT 0xA07CCE
uint8_t g_flightAltMToggle = 0;
/* Set to 0 at flight start (Flight_MainLoop, XvtFlightLoading_Globals); nothing
 * reads it. */
// GLOBAL: XVT 0x9FE734
int g_unusedFlightTransientResetState = 0;
/* Cleared at flight start (Flight_MainLoop, XvtFlightLoading_Globals); nothing
 * else uses it. */
// GLOBAL: XVT 0x9FE7A0
uint8_t g_unusedFlightNetworkBlock[48] = {0};
/* Copy of the local player's record taken when the options sync with the other
 * players fails at flight start (Flight_MainLoop,
 * XvtFlightTask_ReleaseMission); nothing reads it. */
// GLOBAL: XVT 0x9ECC60
PlayerData g_localPlayerSnapshotOnOptionsSyncFailure = {0};
/* 1 when the launch command line holds "inprogress": a client joining a flight
 * already under way (Flight_Main, XvtFlightEntry_ReadLaunchSwitches). Handed to
 * NetSession_InitGameSession. */
// GLOBAL: XVT 0x523640
int g_flightInProgressLaunch = 0;
/* The launch command line split into arguments (see FlightLaunchArgs), by
 * Flight_Main in the original build and XvtFlightEntry_Prepare in the modern
 * one. */
// GLOBAL: XVT 0x622CC0
FlightLaunchArgs g_flightLaunchArgs = {0};
/* 1 when the launch command line starts with '-' (Flight_Main,
 * XvtFlightEntry_ReadLaunchSwitches); nothing reads it. */
// GLOBAL: XVT 0x66DDE8
int g_flightStartedWithDashArg = 0;
/* timeGetTime, in ms, when sound setup began (Flight_Main,
 * XvtFlightEntry_CreateDevices); nothing reads it. */
// GLOBAL: XVT 0x66E1FC
uint32_t g_flightSoundInitStartTimeMs = 0;

/* Sets g_unusedFlightResumeResetSlot0 and g_unusedFlightResumeResetSlot1, which
 * nothing reads, to -1. Called when a pause ends: by Flight_UpdatePlayerStep in
 * the original build, XvtFlightSim_Resume in the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x410FE0
void Flight_ResetUnusedResumeSlots(void)
{
	g_unusedFlightResumeResetSlot0 = -1;
	g_unusedFlightResumeResetSlot1 = -1;
}

/* Counts down the flight's timers once per simulation step, by g_elapsedTicks:
 * every g_flightGlobalCountdownTimers entry (the modern build takes the
 * reference clock's elapsed ticks for entries 1 to 5 and 10 when the timing is
 * unlocked); with side effects on, each active player's
 * g_playerFlightTransientTimers; each player's pendingActionTimer and
 * beamFireCooldownTimer; and for each craft in the active region and each
 * object with character data, the AI think, maneuver and secondary maneuver
 * timers (the reference clock's ticks in the modern build), plus each craft's
 * weapon fire inhibit and countermeasure cooldown timers and its turrets'
 * retarget cooldowns. Each stops at 0 except the think timers and turret
 * cooldowns. When g_targetProximityBlinkTimer runs out it flips
 * g_targetProximityBlinkBit and restarts the timer; it then sets
 * g_renderObjectRef and g_renderTargetComponentIdx from the local player's
 * target. The rest runs once per 236 ticks, a simulated second: it advances
 * g_missionElapsedClock and counts g_missionCountdownClock down, and when a
 * mission time limit runs out with side effects on, sets every player's
 * participationState to 2 and missionEndPending; it gives the time warnings at
 * 2 minutes, 1 minute, 15 and 2 seconds; in melee and combat missions with more
 * than one player it starts the team victory time limit when one team is left
 * or a team's goal status allows it; for each player's craft with any working
 * subsystem, it counts down the systemRepairSeconds of the failed subsystem
 * with the lowest display slot and repairs it when they run out; it adds 1 to
 * every live object's secondsAlive; and it advances the HUD message panes. In
 * the original build, with no target, the blink test reads maxBoundsExtent
 * before any value is set. */
// FUNCTION: XVT 0x415C90
void Flight_UpdateTimers(void)
{
	int index;
	int playerIndex;
	int objectIndex;
	uint16_t *timer;

	timer = (uint16_t *)&g_flightGlobalCountdownTimers;
	for (index = 0; index < (int)(sizeof(g_flightGlobalCountdownTimers) /
				      sizeof(*timer));
	     ++index) {
		if (timer[index] != 0) {

#ifdef XVT_MODERN
			timer[index] -=
				XvtFlightTiming_IsUnlocked() &&
						((index >= 1 && index <= 5) ||
						 index == 10)
					? XvtFlightTiming_ReferenceElapsed()
					: g_elapsedTicks;
#else
			timer[index] -= g_elapsedTicks;
#endif

			if ((int16_t)timer[index] < 0) {
				timer[index] = 0;
			}
		}
	}

	if (g_flightSimSideEffectsSuppressed == 0) {
		for (index = 0;
		     index < (int)(sizeof(PlayerFlightTransientTimers) /
				   sizeof(uint16_t));
		     ++index) {
			for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
				uint16_t *playerTimer =
					(uint16_t
						 *)&g_playerFlightTransientTimers
						[playerIndex] +
					index;

				if (g_players[playerIndex].participationState !=
					    0 &&
				    *playerTimer != 0) {
					*playerTimer -= g_elapsedTicks;
					if ((int16_t)*playerTimer < 0) {
						*playerTimer = 0;
					}
				}
			}
		}
	}

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		if (g_players[playerIndex].pendingActionTimer != 0) {
			g_players[playerIndex].pendingActionTimer -=
				g_elapsedTicks;
			if (g_players[playerIndex].pendingActionTimer < 0) {
				g_players[playerIndex].pendingActionTimer = 0;
			}
		}
		if (g_players[playerIndex].beamFireCooldownTimer != 0) {
			g_players[playerIndex].beamFireCooldownTimer -=
				g_elapsedTicks;
			if (g_players[playerIndex].beamFireCooldownTimer < 0) {
				g_players[playerIndex].beamFireCooldownTimer =
					0;
			}
		}
	}

	for (objectIndex = g_activeRegionObjectSlotStart;
	     objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		if (g_objectTable[objectIndex].objectType != 0) {
			AiController *controller;

			g_curCraft = g_objectTable[objectIndex].mobj->pCraft;
			controller = &g_curCraft->aiController;
			if (controller->thinkTimer != 0)

#ifdef XVT_MODERN
				controller->thinkTimer -=
					XvtFlightTiming_ReferenceElapsed();
#else
				controller->thinkTimer -= g_elapsedTicks;
#endif

			if (controller->maneuverTimer != 0) {

#ifdef XVT_MODERN
				controller->maneuverTimer -=
					XvtFlightTiming_ReferenceElapsed();
#else
				controller->maneuverTimer -= g_elapsedTicks;
#endif

				if (controller->maneuverTimer < 0) {
					controller->maneuverTimer = 0;
				}
			}
			if (controller->secondaryManeuverTimer != 0) {

#ifdef XVT_MODERN
				controller->secondaryManeuverTimer -=
					XvtFlightTiming_ReferenceElapsed();
#else
				controller->secondaryManeuverTimer -=
					g_elapsedTicks;
#endif

				if (controller->secondaryManeuverTimer < 0) {
					controller->secondaryManeuverTimer = 0;
				}
			}

			if (g_curCraft->weaponFireInhibitTimer != 0) {
				uint16_t previousTimer =
					(uint16_t)g_curCraft
						->weaponFireInhibitTimer;

				g_curCraft->weaponFireInhibitTimer -=
					g_elapsedTicks;
				if ((uint16_t)
					    g_curCraft->weaponFireInhibitTimer >
				    previousTimer) {
					g_curCraft->weaponFireInhibitTimer = 0;
				}
			}
			if (g_curCraft->cmFireCooldownTimer != 0) {
				g_curCraft->cmFireCooldownTimer -=
					g_elapsedTicks;
				if ((int16_t)g_curCraft->cmFireCooldownTimer <
				    0) {
					g_curCraft->cmFireCooldownTimer = 0;
				}
			}

			for (index = 0; index < g_curCraft->laserSlotCount;
			     ++index) {
				TurretTargetState *targetState =
					&g_curCraft->turretTargetStates[index];

				if (targetState->retargetCooldownTimer > 0) {
					targetState->retargetCooldownTimer -=
						g_elapsedTicks;
				}
			}
		}
	}

	{
		int charDataSlot = g_mobileObjectCharDataSlotStart;

		if (g_mobileObjectCharDataSlotEnd > charDataSlot) {
			objectIndex = g_mobileObjectCharDataSlotStart;
			do {
				if (g_objectTable[objectIndex].objectType !=
				    0) {
					AiController *controller =
						&g_objectTable[objectIndex]
							 .mobj->pCharData
							 ->aiController;

					if (controller->thinkTimer != 0)

#ifdef XVT_MODERN
						controller->thinkTimer -=
							XvtFlightTiming_ReferenceElapsed();
#else
						controller->thinkTimer -=
							g_elapsedTicks;
#endif

					if (controller->maneuverTimer != 0) {

#ifdef XVT_MODERN
						controller->maneuverTimer -=
							XvtFlightTiming_ReferenceElapsed();
#else
						controller->maneuverTimer -=
							g_elapsedTicks;
#endif

						if (controller->maneuverTimer <
						    0) {
							controller
								->maneuverTimer =
								0;
						}
					}
					if (controller
						    ->secondaryManeuverTimer !=
					    0) {

#ifdef XVT_MODERN
						controller
							->secondaryManeuverTimer -=
							XvtFlightTiming_ReferenceElapsed();
#else
						controller
							->secondaryManeuverTimer -=
							g_elapsedTicks;
#endif

						if (controller
							    ->secondaryManeuverTimer <
						    0) {
							controller
								->secondaryManeuverTimer =
								0;
						}
					}
				}
				++objectIndex;
				++charDataSlot;
			} while (g_mobileObjectCharDataSlotEnd > charDataSlot);
		}
	}

	g_targetProximityBlinkTimer -= g_elapsedTicks;
	if (g_targetProximityBlinkTimer < 0) {
		int maxBoundsExtent;

#ifdef XVT_MODERN
		maxBoundsExtent = 0;
#endif
		g_targetProximityBlinkBit ^= 0x0400;
		if ((uint16_t)g_players[g_localPlayer].currentTargetObjectIdx !=
			    UINT16_MAX &&
		    g_players[g_localPlayer].objectIndex != -1) {
			int objectType;

			pai_ObjectRefDirectionToObjectRef(
				(uint16_t)g_players[g_localPlayer]
					.currentTargetObjectIdx,
				g_players[g_localPlayer].objectIndex);
			objectType =
				g_objectTable[(uint16_t)g_players[g_localPlayer]
						      .currentTargetObjectIdx]
					.objectType;
			maxBoundsExtent =
				g_objectTypeTable[objectType].maxBoundsExtent;
			trig2_polardistance >>= 5;
		}
		{
			int targetDistance = trig2_polardistance;

			if ((g_targetProximityBlinkBit & 0x0400) != 0) {
				if (targetDistance < maxBoundsExtent) {
					g_targetProximityBlinkTimer = 118;
				} else {
					g_targetProximityBlinkTimer = 14;
				}
			} else {
				g_targetProximityBlinkTimer = 14;
				if (targetDistance >= maxBoundsExtent) {
					g_targetProximityBlinkTimer = 118;
				}
			}
		}
	}

	g_renderObjectRef =
		(uint16_t)g_players[g_localPlayer].currentTargetObjectIdx |
		g_renderObjectRefFlags | g_targetProximityBlinkBit;
	g_renderTargetComponentIdx =
		(uint16_t)g_players[g_localPlayer].selectedTargetComponent;

	g_missionElapsedClock.subsecondTicks -= g_elapsedTicks;
	if (g_missionElapsedClock.subsecondTicks > 0) {
		return;
	}

	g_missionElapsedClock.subsecondTicks += SIMULATION_TICKS_PER_SECOND;
	if (++g_missionElapsedClock.seconds >= 60) {
		g_missionElapsedClock.seconds = 0;
		if (++g_missionElapsedClock.minutes >= 60) {
			g_missionElapsedClock.minutes = 0;
			if (++g_missionElapsedClock.hours >= 24) {
				g_missionElapsedClock.hours = 0;
			}
		}
	}

	if (g_missionCountdownClock.minutes != 0 ||
	    g_missionCountdownClock.seconds != 0) {
		if (--g_missionCountdownClock.seconds == UINT8_MAX) {
			g_missionCountdownClock.seconds = 59;
			if (--g_missionCountdownClock.minutes == UINT8_MAX) {
				g_missionCountdownClock.seconds = 0;
				g_missionCountdownClock.minutes = 0;
			}
		}
		if (g_flightMissionState.missionTimeLimitMinutes != 0 &&
		    g_missionCountdownClock.minutes == 0 &&
		    g_missionCountdownClock.seconds == 0 &&
		    g_flightSimSideEffectsSuppressed == 0) {
			for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
				g_players[playerIndex].participationState = 2;
			}
			g_flightMissionState.missionEndPending = 1;
		}
	}

	if (g_flightMissionState.missionTimeLimitMinutes != 0) {
		if (g_missionCountdownClock.minutes == 2 &&
		    g_missionCountdownClock.seconds == 0) {
			fsfx_PlaySound(FLIGHT_SOUND_MISSION_TIMER_WARNING, -1,
				       g_localPlayer);
			msg_emitInFlightMessage(
				IFMSG_201_MISSION_ENDS_IN_2_MINUTES,
				g_localPlayer);
		}
		if (g_missionCountdownClock.minutes == 1 &&
		    g_missionCountdownClock.seconds == 0) {
			fsfx_PlaySound(FLIGHT_SOUND_WARNING_BEEP, -1,
				       g_localPlayer);
			msg_emitInFlightMessage(
				IFMSG_202_MISSION_ENDS_IN_1_MINUTE,
				g_localPlayer);
		}
		if (g_missionCountdownClock.minutes == 0 &&
		    g_missionCountdownClock.seconds == 15) {
			int playerObjectIndex =
				g_players[g_localPlayer].objectIndex;
			int useRebelCraftSound =
				playerObjectIndex != -1 &&
				(g_objectTable[playerObjectIndex].objectType ==
					 1 ||
				 g_objectTable[playerObjectIndex].objectType ==
					 2 ||
				 g_objectTable[playerObjectIndex].objectType ==
					 3 ||
				 g_objectTable[playerObjectIndex].objectType ==
					 14 ||
				 g_objectTable[playerObjectIndex].objectType ==
					 4);
			if (useRebelCraftSound != 0) {
				fsfx_PlaySound(FLIGHT_SOUND_R2_WARNING, -1,
					       g_localPlayer);
			} else {
				fsfx_PlaySound(FLIGHT_SOUND_WARNING_BEEP, -1,
					       g_localPlayer);
				fsfx_PlaySound(FLIGHT_SOUND_WARNING_BEEP, -1,
					       g_localPlayer);
			}
		}
		if (g_missionCountdownClock.minutes == 0 &&
		    g_missionCountdownClock.seconds == 2) {
			msg_emitInFlightMessage(IFMSG_203_MISSION_TIME_EXPIRING,
						g_localPlayer);
		}
	}

	if ((unsigned int)g_flightMissionState
			    .maxConnectedPlayerCountThisMission > 1 &&
	    g_flightMissionState.teamVictoryTimeLimitMinutes != 0 &&
	    g_flightMissionState.teamVictoryTimeLimitStarted == 0) {
		if ((g_missionHeader.missionType == MISSION_TYPE_MELEE ||
		     g_missionHeader.missionType == MISSION_TYPE_COMBAT) &&
		    (g_missionCountdownClock.minutes >
			     g_flightMissionState.teamVictoryTimeLimitMinutes ||
		     (g_missionCountdownClock.minutes == 0 &&
		      g_missionCountdownClock.seconds == 0))) {
			uint8_t teamActive[10];
			int activeFlag = 1;
			int activeTeamCount;
			int activeTeam;

			memset(teamActive, 0, sizeof(teamActive));

			for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
				if (g_players[playerIndex].participationState ==
				    1) {
					teamActive[(uint16_t)g_players
							   [playerIndex]
								   .team] =
						activeFlag;
				}
			}
			if (g_missionHeader.missionType == MISSION_TYPE_MELEE) {
				for (objectIndex =
					     g_activeRegionObjectSlotStart;
				     objectIndex <
				     g_activeRegionCraftObjectSlotEnd;
				     ++objectIndex) {
					if (g_objectTable[objectIndex]
							    .objectType != 0 &&
					    g_missionFlightGroups
							    [g_objectTable[objectIndex]
								     .flightGroupIdx]
								    .fg
								    .playerNumber !=
						    0) {
						teamActive
							[g_missionFlightGroups
								 [g_objectTable[objectIndex]
									  .flightGroupIdx]
									 .fg
									 .team] =
								activeFlag;
					}
				}
			}
			activeTeamCount = 0;
			for (index = 0; index < 10; ++index) {
				if (teamActive[index] != 0) {
					++activeTeamCount;
					activeTeam = index;
				}
			}
			if (activeTeamCount == 1 &&
			    (g_missionHeader.missionType ==
				     MISSION_TYPE_MELEE ||
			     g_flightMissionState.runtime
					     .teamGoalStatus[activeTeam][0] ==
				     1 ||
			     g_flightMissionState.runtime
					     .teamGoalStatus[activeTeam][0] ==
				     2 ||
			     g_flightMissionState.runtime
					     .teamGoalStatus[activeTeam][1] ==
				     1)) {
				g_missionCountdownClock.seconds = 0;
				g_flightMissionState
					.teamVictoryTimeLimitStarted = 1;
				g_missionCountdownClock.minutes =
					g_flightMissionState
						.teamVictoryTimeLimitMinutes;
				g_flightMissionState.missionTimeLimitMinutes =
					g_flightMissionState
						.teamVictoryTimeLimitMinutes;
			}
		}
		if (g_flightMissionState.teamVictoryTimeLimitStarted == 0 &&
		    g_missionHeader.missionType == MISSION_TYPE_COMBAT &&
		    (g_flightMissionState.runtime.teamGoalStatus[0][0] == 1 ||
		     g_flightMissionState.runtime.teamGoalStatus[1][0] == 1)) {
			g_missionCountdownClock.seconds = 0;
			g_flightMissionState.teamVictoryTimeLimitStarted = 1;
			g_missionCountdownClock.minutes =
				g_flightMissionState
					.teamVictoryTimeLimitMinutes;
			g_flightMissionState.missionTimeLimitMinutes =
				g_flightMissionState
					.teamVictoryTimeLimitMinutes;
		}
	}

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		PlayerData *player = &g_players[playerIndex];

		if (player->participationState != 0 &&
		    player->objectIndex != -1) {
			uint16_t repairDisplaySlot = UINT16_MAX;
			uint16_t repairSystem = UINT16_MAX;
			CraftData *craft =
				g_objectTable[player->objectIndex].mobj->pCraft;

			if (craft->workingSubsystems != 0) {
				for (index = 0; index < CRAFT_SUBSYSTEM_COUNT;
				     ++index) {
					if (craft->systemHealth[index] == 0 &&
					    repairDisplaySlot >
						    craft->systemDisplaySlotBySystem
							    [index]) {
						repairDisplaySlot =
							craft->systemDisplaySlotBySystem
								[index];
						repairSystem = (uint16_t)index;
					}
				}
				for (index = 0; index < CRAFT_SUBSYSTEM_COUNT;
				     ++index) {
					if (craft->systemHealth[index] == 0 &&
					    repairSystem == index) {
						if (craft->systemRepairSeconds
							    [index] == 0) {
							craft->systemHealth
								[index] = 100;
							craft->workingSubsystems |=
								g_subsystemIdToFlag
									[index];
							g_msgArgTable[1] = 88;
							g_msgArgTable[0] =
								g_subsystemMessageArgById
									[index];
							msg_emitInFlightMessage(
								IFMSG_086_ARG_SYSTEM_IS_ARG,
								playerIndex);
						} else {
							--craft->systemRepairSeconds
								  [index];
						}
					}
				}
			}
		}
	}

	{
		int slotsWalked = 0;

		if (g_regionMainObjectSlotEnd > slotsWalked) {
			objectIndex = 0;
			do {
				if (g_objectTable[objectIndex].objectType !=
				    0) {
					++g_objectTable[objectIndex]
						  .mobj->secondsAlive;
				}
				++objectIndex;
				++slotsWalked;
			} while (g_regionMainObjectSlotEnd > slotsWalked);
		}
	}
	Hud_AdvanceFlightMessagePaneTimers();
}

/* Keeps the flight music going when music is on and the last mission had one
 * human player. While the flight track (2) plays and no outcome track has
 * started, it starts one when the local player's team goal status calls for it:
 * track 7, or 3 to 6 by mission type and the player's IFF, setting
 * g_dynamicMusicState and g_dynamicMusicOutcomeLatched. Otherwise it lowers
 * g_dynamicMusicTrackRemainingMs by the ms since the last call
 * (g_dynamicMusicLastUpdateMs) and starts the flight track again when it runs
 * out. */
// FUNCTION: XVT 0x4165B0
void Flight_UpdateDynamicMusicState(void)
{
	uint8_t trackNumber;
	uint16_t playerTeam;
	uint8_t primaryGoalStatus;
	uint32_t currentTimeMs;
	uint32_t elapsedMs;

	if (g_gameConfig.musicEnabled == 0 || g_gameConfig.musicVolume == 0 ||
	    g_pilotData.numHumanPlayersLastMission != 1) {
		return;
	}

	trackNumber = 0;
	if (g_dynamicMusicState == 2 && g_dynamicMusicOutcomeLatched == 0) {
		playerTeam = g_players[g_localPlayer].team;
		primaryGoalStatus = g_flightMissionState.runtime
					    .teamGoalStatus[playerTeam][0];
		if (primaryGoalStatus == 2 ||
		    g_flightMissionState.runtime
				    .teamGoalStatus[playerTeam][1] == 1) {
			trackNumber = 7;
		} else if (primaryGoalStatus == 1 &&
			   g_missionHeader.missionType != MISSION_TYPE_MELEE) {
			if (g_missionHeader.missionType ==
			    MISSION_TYPE_COMBAT) {
				trackNumber = g_players[g_localPlayer].iff == 1
						      ? 6
						      : 4;
			} else {
				trackNumber = g_players[g_localPlayer].iff == 1
						      ? 5
						      : 3;
			}
		}

		if (trackNumber != 0) {
			MusicCd_PlayTrackFromTime(trackNumber, 0, 0);
			g_dynamicMusicTrackRemainingMs =
				MusicCd_GetTrackLengthMs(trackNumber);
			g_dynamicMusicOutcomeLatched = 1;
			g_dynamicMusicState = trackNumber;
		}
	}

	if (trackNumber != 0) {
		return;
	}

	currentTimeMs = timeGetTime();
	elapsedMs = currentTimeMs - g_dynamicMusicLastUpdateMs;
	g_dynamicMusicLastUpdateMs = currentTimeMs;
	g_dynamicMusicTrackRemainingMs -= elapsedMs;
	if (g_dynamicMusicTrackRemainingMs <= 0) {
		MusicCd_PlayTrackFromTime(2, 0, 0);
		g_dynamicMusicTrackRemainingMs = MusicCd_GetTrackLengthMs(2);
		g_dynamicMusicState = 2;
	}
}

/* Returns g_worldStateDupBuffer. Only the original build calls this. */
// FUNCTION: XVT 0x416700
uint8_t *Flight_GetDuplicateWorldStateBuffer(void)
{
	return g_worldStateDupBuffer;
}

/* Returns g_worldStateDupSize. Only the original build calls this. */
// FUNCTION: XVT 0x416710
int Flight_GetDuplicateWorldStateSize(void) { return g_worldStateDupSize; }

/* Allocates the two world state buffers, Flight_CalculateWorldStateBufferSize
 * bytes each, and locks them: g_worldStateHandle and g_worldStateBuffer, then
 * g_worldStateDupHandle and g_worldStateDupBuffer. A failed allocation calls
 * FeDiskIo_FatalError with the not-enough-memory message; the modern build
 * returns after it. */
// FUNCTION: XVT 0x416720
void Flight_AllocWorldStateBuffers(void)
{
	size_t bufferSize;

	bufferSize = Flight_CalculateWorldStateBufferSize();
	g_worldStateHandle = Memory_AllocHandle(bufferSize, 0);
	if (g_worldStateHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return;
#endif
	}
	g_worldStateBuffer = Memory_LockHandle(g_worldStateHandle);

	g_worldStateDupHandle = Memory_AllocHandle(bufferSize, 0);
	if (g_worldStateDupHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return;
#endif
	}
	g_worldStateDupBuffer = Memory_LockHandle(g_worldStateDupHandle);
}

/* Frees both world state buffers (the modern build skips a handle of 0) and
 * sets g_worldStateHandle, g_worldStateBuffer, g_worldStateDupHandle and
 * g_worldStateDupBuffer to 0 or NULL. */
// FUNCTION: XVT 0x4167A0
void Flight_FreeWorldStateBuffers(void)
{
	unsigned int handle;

	handle = g_worldStateHandle;
#ifdef XVT_MODERN
	if (handle)
#endif
		Memory_FreeHandle((uint16_t)handle);
	g_worldStateHandle = 0;
	g_worldStateBuffer = NULL;
	handle = g_worldStateDupHandle;
#ifdef XVT_MODERN
	if (handle)
#endif
		Memory_FreeHandle((uint16_t)handle);
	g_worldStateDupHandle = 0;
	g_worldStateDupBuffer = NULL;
}

/* Writes the world state into g_worldStateBuffer and sets g_worldStateSize to
 * its length. Per object slot, main and static, except the local slots from
 * g_localTransientSlotStart to g_localDebrisSlotEnd: the type byte, and for a
 * live object its record, mobile object, craft data, warhead guidance and
 * character data, each pointer written as its offset into its pool plus 1, so
 * NULL stays 0 (the live pointers are put back after). Then the mission clocks,
 * header, flight group stats and flight groups, g_flightMissionState,
 * g_flightGlobalCountdownTimers, the mission file version, g_flightPlayerCount,
 * g_worldStateReservedByte, the pool sizes and slot ranges, the AI plan table
 * and count, g_unusedWorldStateSerializedDword, the built-in plan ids, the game
 * random state, the next object signature, g_laserFireTimestampTrackingEnabled
 * and g_players. Does not check the buffer's size. The modern build calls
 * XvtSnapshot_Save instead. */
// FUNCTION: XVT 0x4167F0
void Flight_SaveWorldState(void)
{
#ifdef XVT_MODERN
	XvtSnapshot_Save();
#else
	uint8_t *cursor;
	int objectIndex;

	cursor = g_worldStateBuffer;
	for (objectIndex = 0;
	     objectIndex <
	     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     ++objectIndex) {
		if (objectIndex >= g_localTransientSlotStart &&
		    objectIndex < g_localDebrisSlotEnd) {
			continue;
		}
		*cursor++ = g_objectTable[objectIndex].objectType;
		if (g_objectTable[objectIndex].objectType == 0) {
			continue;
		}

		if (g_objectTable[objectIndex].mobj != NULL) {
			g_objectTable[objectIndex].mobj =
				(MobileObject
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj -
					    ((uint8_t *)g_mobileObjectPoolBase -
					     (uint8_t *)NULL));
			g_objectTable[objectIndex].mobj =
				(MobileObject
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj +
					    1);
		}
		memcpy(cursor, &g_objectTable[objectIndex],
		       sizeof(ObjectRecord));
		cursor += sizeof(ObjectRecord);
		if (g_objectTable[objectIndex].mobj != NULL) {
			g_objectTable[objectIndex].mobj =
				(MobileObject
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj -
					    1);
			g_objectTable[objectIndex].mobj =
				(MobileObject
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj +
					    ((uint8_t *)g_mobileObjectPoolBase -
					     (uint8_t *)NULL));
		}
		if (g_objectTable[objectIndex].mobj == NULL) {
			continue;
		}

		if (g_objectTable[objectIndex].mobj->pCraft != NULL) {
			g_objectTable[objectIndex].mobj->pCraft =
				(CraftData *)((uint8_t *)
						      g_objectTable[objectIndex]
							      .mobj->pCraft -
					      ((uint8_t *)g_craftDataPoolBase -
					       (uint8_t *)NULL));
			g_objectTable[objectIndex].mobj->pCraft =
				(CraftData *)((uint8_t *)
						      g_objectTable[objectIndex]
							      .mobj->pCraft +
					      1);
		}
		if (g_objectTable[objectIndex].mobj->pWarheadGuidance != NULL) {
			g_objectTable[objectIndex].mobj->pWarheadGuidance =
				(WarheadGuidanceState
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj
							    ->pWarheadGuidance -
					    ((uint8_t *)
						     g_projectileGuidanceStates -
					     (uint8_t *)NULL));
			g_objectTable[objectIndex].mobj->pWarheadGuidance =
				(WarheadGuidanceState
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj
							    ->pWarheadGuidance +
					    1);
		}
		if (g_objectTable[objectIndex].mobj->pCharData != NULL) {
			g_objectTable[objectIndex].mobj->pCharData =
				(MobileObjectCharData
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj->pCharData -
					    ((uint8_t *)
						     g_mobileObjectCharDataPool -
					     (uint8_t *)NULL));
			g_objectTable[objectIndex].mobj->pCharData =
				(MobileObjectCharData
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj->pCharData +
					    1);
		}
		memcpy(cursor, g_objectTable[objectIndex].mobj,
		       sizeof(MobileObject));
		cursor += sizeof(MobileObject);
		if (g_objectTable[objectIndex].mobj->pCraft != NULL) {
			g_objectTable[objectIndex].mobj->pCraft =
				(CraftData *)((uint8_t *)
						      g_objectTable[objectIndex]
							      .mobj->pCraft -
					      1);
			g_objectTable[objectIndex].mobj->pCraft =
				(CraftData *)((uint8_t *)
						      g_objectTable[objectIndex]
							      .mobj->pCraft +
					      ((uint8_t *)g_craftDataPoolBase -
					       (uint8_t *)NULL));
		}
		if (g_objectTable[objectIndex].mobj->pWarheadGuidance != NULL) {
			g_objectTable[objectIndex].mobj->pWarheadGuidance =
				(WarheadGuidanceState
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj
							    ->pWarheadGuidance -
					    1);
			g_objectTable[objectIndex].mobj->pWarheadGuidance =
				(WarheadGuidanceState
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj
							    ->pWarheadGuidance +
					    ((uint8_t *)
						     g_projectileGuidanceStates -
					     (uint8_t *)NULL));
		}
		if (g_objectTable[objectIndex].mobj->pCharData != NULL) {
			g_objectTable[objectIndex].mobj->pCharData =
				(MobileObjectCharData
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj->pCharData -
					    1);
			g_objectTable[objectIndex].mobj->pCharData =
				(MobileObjectCharData
					 *)((uint8_t *)
						    g_objectTable[objectIndex]
							    .mobj->pCharData +
					    ((uint8_t *)
						     g_mobileObjectCharDataPool -
					     (uint8_t *)NULL));
		}

		if (g_objectTable[objectIndex].mobj->pCraft != NULL) {
			int linkIndex;

			for (linkIndex = 0; linkIndex < 16; ++linkIndex) {
				if (g_objectTable[objectIndex]
					    .mobj->pCraft
					    ->turretObjectLinks[linkIndex] !=
				    NULL) {
					g_objectTable[objectIndex]
						.mobj->pCraft
						->turretObjectLinks[linkIndex] =
						(ObjectRecord
							 *)((uint8_t *)g_objectTable[objectIndex]
								    .mobj
								    ->pCraft
								    ->turretObjectLinks
									    [linkIndex] -
							    ((uint8_t *)
								     g_objectTable -
							     (uint8_t *)NULL));
					g_objectTable[objectIndex]
						.mobj->pCraft
						->turretObjectLinks[linkIndex] =
						(ObjectRecord
							 *)((uint8_t *)g_objectTable[objectIndex]
								    .mobj
								    ->pCraft
								    ->turretObjectLinks
									    [linkIndex] +
							    1);
				}
			}
			if (g_objectTable[objectIndex]
				    .mobj->pCraft->effectiveAiObjectLink !=
			    NULL) {
				g_objectTable[objectIndex]
					.mobj->pCraft->effectiveAiObjectLink =
					(ObjectRecord
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pCraft
								    ->effectiveAiObjectLink -
						    ((uint8_t *)g_objectTable -
						     (uint8_t *)NULL));
				g_objectTable[objectIndex]
					.mobj->pCraft->effectiveAiObjectLink =
					(ObjectRecord
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pCraft
								    ->effectiveAiObjectLink +
						    1);
			}
			memcpy(cursor, g_objectTable[objectIndex].mobj->pCraft,
			       sizeof(CraftData));
			cursor += sizeof(CraftData);
			for (linkIndex = 0; linkIndex < 16; ++linkIndex) {
				if (g_objectTable[objectIndex]
					    .mobj->pCraft
					    ->turretObjectLinks[linkIndex] !=
				    NULL) {
					g_objectTable[objectIndex]
						.mobj->pCraft
						->turretObjectLinks[linkIndex] =
						(ObjectRecord
							 *)((uint8_t *)g_objectTable[objectIndex]
								    .mobj
								    ->pCraft
								    ->turretObjectLinks
									    [linkIndex] -
							    1);
					g_objectTable[objectIndex]
						.mobj->pCraft
						->turretObjectLinks[linkIndex] =
						(ObjectRecord
							 *)((uint8_t *)g_objectTable[objectIndex]
								    .mobj
								    ->pCraft
								    ->turretObjectLinks
									    [linkIndex] +
							    ((uint8_t *)
								     g_objectTable -
							     (uint8_t *)NULL));
				}
			}
			if (g_objectTable[objectIndex]
				    .mobj->pCraft->effectiveAiObjectLink !=
			    NULL) {
				g_objectTable[objectIndex]
					.mobj->pCraft->effectiveAiObjectLink =
					(ObjectRecord
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pCraft
								    ->effectiveAiObjectLink -
						    1);
				g_objectTable[objectIndex]
					.mobj->pCraft->effectiveAiObjectLink =
					(ObjectRecord
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pCraft
								    ->effectiveAiObjectLink +
						    ((uint8_t *)g_objectTable -
						     (uint8_t *)NULL));
			}
		}
		if (g_objectTable[objectIndex].mobj->pWarheadGuidance != NULL) {
			memcpy(cursor,
			       g_objectTable[objectIndex]
				       .mobj->pWarheadGuidance,
			       sizeof(WarheadGuidanceState));
			cursor += sizeof(WarheadGuidanceState);
		}
		if (g_objectTable[objectIndex].mobj->pCharData != NULL) {
			memcpy(cursor,
			       g_objectTable[objectIndex].mobj->pCharData,
			       sizeof(MobileObjectCharData));
			cursor += sizeof(MobileObjectCharData);
		}
	}

	memcpy(cursor, &g_missionElapsedClock, sizeof(g_missionElapsedClock));
	cursor += sizeof(g_missionElapsedClock);
	memcpy(cursor, &g_missionCountdownClock,
	       sizeof(g_missionCountdownClock));
	cursor += sizeof(g_missionCountdownClock);
	memcpy(cursor, &g_missionHeader, 162);
	cursor += 162;
	memcpy(cursor, g_missionFgStats,
	       294 * (int16_t)g_missionHeader.numFlightGroups);
	cursor += 294 * (int16_t)g_missionHeader.numFlightGroups;
	memcpy(cursor, g_missionFlightGroups,
	       1382 * (int16_t)g_missionHeader.numFlightGroups);
	cursor += 1382 * (int16_t)g_missionHeader.numFlightGroups;
	memcpy(cursor, &g_flightMissionState, 3376);
	cursor += 3376;
	memcpy(cursor, &g_flightGlobalCountdownTimers, 22);
	cursor += 22;
	memcpy(cursor, &g_missionFileVersion, sizeof(g_missionFileVersion));
	cursor += sizeof(g_missionFileVersion);
	memcpy(cursor, &g_flightPlayerCount, sizeof(g_flightPlayerCount));
	cursor += sizeof(g_flightPlayerCount);
	*cursor++ = g_worldStateReservedByte;

	memcpy(cursor, &g_craftDataPoolCapacity,
	       sizeof(g_craftDataPoolCapacity));
	cursor += sizeof(g_craftDataPoolCapacity);
	memcpy(cursor, &g_mobileObjectCharDataCount,
	       sizeof(g_mobileObjectCharDataCount));
	cursor += sizeof(g_mobileObjectCharDataCount);
	memcpy(cursor, &g_projectileObjectSlotsTotal,
	       sizeof(g_projectileObjectSlotsTotal));
	cursor += sizeof(g_projectileObjectSlotsTotal);
	memcpy(cursor, &g_debrisObjectSlotsTotal,
	       sizeof(g_debrisObjectSlotsTotal));
	cursor += sizeof(g_debrisObjectSlotsTotal);
	memcpy(cursor, &g_worldStateDebrisSlotCount,
	       sizeof(g_worldStateDebrisSlotCount));
	cursor += sizeof(g_worldStateDebrisSlotCount);
	memcpy(cursor, &g_regionMainObjectSlotStart,
	       sizeof(g_regionMainObjectSlotStart));
	cursor += sizeof(g_regionMainObjectSlotStart);
	memcpy(cursor, &g_activeRegionObjectSlotStart,
	       sizeof(g_activeRegionObjectSlotStart));
	cursor += sizeof(g_activeRegionObjectSlotStart);
	memcpy(cursor, &g_activeRegionCraftObjectSlotEnd,
	       sizeof(g_activeRegionCraftObjectSlotEnd));
	cursor += sizeof(g_activeRegionCraftObjectSlotEnd);
	memcpy(cursor, &g_mobileObjectCharDataSlotStart,
	       sizeof(g_mobileObjectCharDataSlotStart));
	cursor += sizeof(g_mobileObjectCharDataSlotStart);
	memcpy(cursor, &g_mobileObjectCharDataSlotEnd,
	       sizeof(g_mobileObjectCharDataSlotEnd));
	cursor += sizeof(g_mobileObjectCharDataSlotEnd);
	memcpy(cursor, &g_projectileObjectSlotStart,
	       sizeof(g_projectileObjectSlotStart));
	cursor += sizeof(g_projectileObjectSlotStart);
	memcpy(cursor, &g_projectileObjectSlotEnd,
	       sizeof(g_projectileObjectSlotEnd));
	cursor += sizeof(g_projectileObjectSlotEnd);
	memcpy(cursor, &g_debrisObjectSlotStart,
	       sizeof(g_debrisObjectSlotStart));
	cursor += sizeof(g_debrisObjectSlotStart);
	memcpy(cursor, &g_debrisObjectSlotEnd, sizeof(g_debrisObjectSlotEnd));
	cursor += sizeof(g_debrisObjectSlotEnd);
	memcpy(cursor, &g_explosionObjectSlotStart,
	       sizeof(g_explosionObjectSlotStart));
	cursor += sizeof(g_explosionObjectSlotStart);
	memcpy(cursor, &g_explosionObjectSlotEnd,
	       sizeof(g_explosionObjectSlotEnd));
	cursor += sizeof(g_explosionObjectSlotEnd);
	memcpy(cursor, &g_localTransientSlotStart,
	       sizeof(g_localTransientSlotStart));
	cursor += sizeof(g_localTransientSlotStart);
	memcpy(cursor, &g_localDebrisSlotEnd, sizeof(g_localDebrisSlotEnd));
	cursor += sizeof(g_localDebrisSlotEnd);
	memcpy(cursor, &g_regionMainObjectSlotEnd,
	       sizeof(g_regionMainObjectSlotEnd));
	cursor += sizeof(g_regionMainObjectSlotEnd);
	memcpy(cursor, &g_regionStaticObjectSlotCount,
	       sizeof(g_regionStaticObjectSlotCount));
	cursor += sizeof(g_regionStaticObjectSlotCount);
	memcpy(cursor, g_planTable, 21760);
	cursor += 21760;
	memcpy(cursor, &g_planCount, sizeof(g_planCount));
	cursor += sizeof(g_planCount);
	memcpy(cursor, &g_unusedWorldStateSerializedDword,
	       sizeof(g_unusedWorldStateSerializedDword));
	cursor += sizeof(g_unusedWorldStateSerializedDword);
	memcpy(cursor, g_builtinPlanIdByNameIndex, 256);
	cursor += 256;
	memcpy(cursor, &g_gameRandFeedbackState,
	       sizeof(g_gameRandFeedbackState));
	cursor += sizeof(g_gameRandFeedbackState);
	memcpy(cursor, &g_nextObjectSignature, sizeof(g_nextObjectSignature));
	cursor += sizeof(g_nextObjectSignature);
	memcpy(cursor, &g_laserFireTimestampTrackingEnabled,
	       sizeof(g_laserFireTimestampTrackingEnabled));
	cursor += sizeof(g_laserFireTimestampTrackingEnabled);
	memcpy(cursor, g_players, 11752);
	cursor += 11752;
	g_worldStateSize = (unsigned int)(cursor - g_worldStateBuffer);
#endif
}

/* Reads the world state back from g_worldStateBuffer in the order
 * Flight_SaveWorldState writes it, turning stored offsets back into pointers; a
 * slot saved empty has its record, mobile object, craft data, guidance and
 * character data cleared, with playerOwnerIdx -1 and the mobile object's IFF
 * 0xFF. The modern build calls XvtSnapshot_Restore instead. */
// FUNCTION: XVT 0x416E50
void Flight_RestoreWorldState(void)
{
#ifdef XVT_MODERN
	XvtSnapshot_Restore();
#else
	uint8_t *cursor;
	int objectIndex;

	cursor = g_worldStateBuffer;
	for (objectIndex = 0;
	     objectIndex <
	     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     ++objectIndex) {
		if (objectIndex >= g_localTransientSlotStart &&
		    objectIndex < g_localDebrisSlotEnd) {
			continue;
		}
		g_objectTable[objectIndex].objectType = *cursor++;
		if (g_objectTable[objectIndex].objectType != 0) {
			memcpy(&g_objectTable[objectIndex], cursor,
			       sizeof(ObjectRecord));
			cursor += sizeof(ObjectRecord);
			if (g_objectTable[objectIndex].mobj != NULL) {
				g_objectTable[objectIndex].mobj =
					(MobileObject
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj -
						    1);
				g_objectTable[objectIndex].mobj =
					(MobileObject
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj +
						    ((uint8_t *)
							     g_mobileObjectPoolBase -
						     (uint8_t *)NULL));
			}
			if (g_objectTable[objectIndex].mobj == NULL) {
				continue;
			}

			memcpy(g_objectTable[objectIndex].mobj, cursor,
			       sizeof(MobileObject));
			cursor += sizeof(MobileObject);
			if (g_objectTable[objectIndex].mobj->pCraft != NULL) {
				g_objectTable[objectIndex].mobj->pCraft =
					(CraftData *)((uint8_t *)g_objectTable
							      [objectIndex]
								      .mobj
								      ->pCraft -
						      1);
				g_objectTable[objectIndex].mobj->pCraft =
					(CraftData
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pCraft +
						    ((uint8_t *)
							     g_craftDataPoolBase -
						     (uint8_t *)NULL));
			}
			if (g_objectTable[objectIndex].mobj->pWarheadGuidance !=
			    NULL) {
				g_objectTable[objectIndex]
					.mobj->pWarheadGuidance =
					(WarheadGuidanceState
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pWarheadGuidance -
						    1);
				g_objectTable[objectIndex]
					.mobj->pWarheadGuidance =
					(WarheadGuidanceState
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pWarheadGuidance +
						    ((uint8_t *)
							     g_projectileGuidanceStates -
						     (uint8_t *)NULL));
			}
			if (g_objectTable[objectIndex].mobj->pCharData !=
			    NULL) {
				g_objectTable[objectIndex].mobj->pCharData =
					(MobileObjectCharData
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pCharData -
						    1);
				g_objectTable[objectIndex].mobj->pCharData =
					(MobileObjectCharData
						 *)((uint8_t *)g_objectTable
							    [objectIndex]
								    .mobj
								    ->pCharData +
						    ((uint8_t *)
							     g_mobileObjectCharDataPool -
						     (uint8_t *)NULL));
			}

			if (g_objectTable[objectIndex].mobj->pCraft != NULL) {
				int linkIndex;

				memcpy(g_objectTable[objectIndex].mobj->pCraft,
				       cursor, sizeof(CraftData));
				cursor += sizeof(CraftData);
				for (linkIndex = 0; linkIndex < 16;
				     ++linkIndex) {
					if (g_objectTable[objectIndex]
						    .mobj->pCraft
						    ->turretObjectLinks
							    [linkIndex] !=
					    NULL) {
						g_objectTable[objectIndex]
							.mobj->pCraft
							->turretObjectLinks
								[linkIndex] =
							(ObjectRecord
								 *)((uint8_t *)g_objectTable[objectIndex]
									    .mobj
									    ->pCraft
									    ->turretObjectLinks
										    [linkIndex] -
								    1);
						g_objectTable[objectIndex]
							.mobj->pCraft
							->turretObjectLinks
								[linkIndex] =
							(ObjectRecord
								 *)((uint8_t *)g_objectTable[objectIndex]
									    .mobj
									    ->pCraft
									    ->turretObjectLinks
										    [linkIndex] +
								    ((uint8_t *)
									     g_objectTable -
								     (uint8_t *)
									     NULL));
					}
				}
				if (g_objectTable[objectIndex]
					    .mobj->pCraft
					    ->effectiveAiObjectLink != NULL) {
					g_objectTable[objectIndex]
						.mobj->pCraft
						->effectiveAiObjectLink =
						(ObjectRecord
							 *)((uint8_t *)g_objectTable
								    [objectIndex]
									    .mobj
									    ->pCraft
									    ->effectiveAiObjectLink -
							    1);
					g_objectTable[objectIndex]
						.mobj->pCraft
						->effectiveAiObjectLink =
						(ObjectRecord
							 *)((uint8_t *)g_objectTable
								    [objectIndex]
									    .mobj
									    ->pCraft
									    ->effectiveAiObjectLink +
							    ((uint8_t *)
								     g_objectTable -
							     (uint8_t *)NULL));
				}
			}
			if (g_objectTable[objectIndex].mobj->pWarheadGuidance !=
			    NULL) {
				memcpy(g_objectTable[objectIndex]
					       .mobj->pWarheadGuidance,
				       cursor, sizeof(WarheadGuidanceState));
				cursor += sizeof(WarheadGuidanceState);
			}
			if (g_objectTable[objectIndex].mobj->pCharData !=
			    NULL) {
				memcpy(g_objectTable[objectIndex]
					       .mobj->pCharData,
				       cursor, sizeof(MobileObjectCharData));
				cursor += sizeof(MobileObjectCharData);
			}
		} else {
			memset(&g_objectTable[objectIndex], 0, 0x1f);
			g_objectTable[objectIndex].playerOwnerIdx = -1;
			if (g_objectTable[objectIndex].mobj != NULL) {
				memset(g_objectTable[objectIndex].mobj, 0,
				       0x8b);
				g_objectTable[objectIndex].mobj->iff = 0xff;
				if (g_objectTable[objectIndex].mobj->pCraft !=
				    NULL) {
					memset(g_objectTable[objectIndex]
						       .mobj->pCraft,
					       0, 0x412);
				}
				if (g_objectTable[objectIndex]
					    .mobj->pWarheadGuidance != NULL) {
					memset(g_objectTable[objectIndex]
						       .mobj->pWarheadGuidance,
					       0, sizeof(WarheadGuidanceState));
				}
				if (g_objectTable[objectIndex]
					    .mobj->pCharData != NULL) {
					memset(g_objectTable[objectIndex]
						       .mobj->pCharData,
					       0, sizeof(MobileObjectCharData));
				}
			}
		}
	}

	memcpy(&g_missionElapsedClock, cursor, sizeof(g_missionElapsedClock));
	cursor += sizeof(g_missionElapsedClock);
	memcpy(&g_missionCountdownClock, cursor,
	       sizeof(g_missionCountdownClock));
	cursor += sizeof(g_missionCountdownClock);
	memcpy(&g_missionHeader, cursor, sizeof(g_missionHeader));
	cursor += sizeof(g_missionHeader);
	memcpy(g_missionFgStats, cursor,
	       sizeof(*g_missionFgStats) * g_missionHeader.numFlightGroups);
	cursor += sizeof(*g_missionFgStats) * g_missionHeader.numFlightGroups;
	memcpy(g_missionFlightGroups, cursor,
	       sizeof(*g_missionFlightGroups) *
		       g_missionHeader.numFlightGroups);
	cursor += sizeof(*g_missionFlightGroups) *
		  g_missionHeader.numFlightGroups;
	memcpy(&g_flightMissionState, cursor, sizeof(g_flightMissionState));
	cursor += sizeof(g_flightMissionState);
	memcpy(&g_flightGlobalCountdownTimers, cursor,
	       sizeof(g_flightGlobalCountdownTimers));
	cursor += sizeof(g_flightGlobalCountdownTimers);
	g_missionFileVersion = *(uint16_t *)cursor;
	cursor += sizeof(g_missionFileVersion);
	g_flightPlayerCount = *(int32_t *)cursor;
	cursor += sizeof(g_flightPlayerCount);
	g_worldStateReservedByte = *cursor++;
	g_craftDataPoolCapacity = *(int *)cursor;
	cursor += sizeof(g_craftDataPoolCapacity);
	g_mobileObjectCharDataCount = *(int *)cursor;
	cursor += sizeof(g_mobileObjectCharDataCount);
	g_projectileObjectSlotsTotal = *(unsigned int *)cursor;
	cursor += sizeof(g_projectileObjectSlotsTotal);
	g_debrisObjectSlotsTotal = *(unsigned int *)cursor;
	cursor += sizeof(g_debrisObjectSlotsTotal);
	g_worldStateDebrisSlotCount = *(int *)cursor;
	cursor += sizeof(g_worldStateDebrisSlotCount);
	g_regionMainObjectSlotStart = *(int *)cursor;
	cursor += sizeof(g_regionMainObjectSlotStart);
	g_activeRegionObjectSlotStart = *(int *)cursor;
	cursor += sizeof(g_activeRegionObjectSlotStart);
	g_activeRegionCraftObjectSlotEnd = *(int *)cursor;
	cursor += sizeof(g_activeRegionCraftObjectSlotEnd);
	g_mobileObjectCharDataSlotStart = *(int *)cursor;
	cursor += sizeof(g_mobileObjectCharDataSlotStart);
	g_mobileObjectCharDataSlotEnd = *(int *)cursor;
	cursor += sizeof(g_mobileObjectCharDataSlotEnd);
	g_projectileObjectSlotStart = *(int *)cursor;
	cursor += sizeof(g_projectileObjectSlotStart);
	g_projectileObjectSlotEnd = *(int *)cursor;
	cursor += sizeof(g_projectileObjectSlotEnd);
	g_debrisObjectSlotStart = *(int *)cursor;
	cursor += sizeof(g_debrisObjectSlotStart);
	g_debrisObjectSlotEnd = *(int *)cursor;
	cursor += sizeof(g_debrisObjectSlotEnd);
	g_explosionObjectSlotStart = *(int *)cursor;
	cursor += sizeof(g_explosionObjectSlotStart);
	g_explosionObjectSlotEnd = *(unsigned int *)cursor;
	cursor += sizeof(g_explosionObjectSlotEnd);
	g_localTransientSlotStart = *(int *)cursor;
	cursor += sizeof(g_localTransientSlotStart);
	g_localDebrisSlotEnd = *(int *)cursor;
	cursor += sizeof(g_localDebrisSlotEnd);
	g_regionMainObjectSlotEnd = *(int *)cursor;
	cursor += sizeof(g_regionMainObjectSlotEnd);
	g_regionStaticObjectSlotCount = *(int *)cursor;
	cursor += sizeof(g_regionStaticObjectSlotCount);
	memcpy(g_planTable, cursor, sizeof(g_planTable));
	cursor += sizeof(g_planTable);
	g_planCount = *(int *)cursor;
	cursor += sizeof(g_planCount);
	g_unusedWorldStateSerializedDword = *(int *)cursor;
	cursor += sizeof(g_unusedWorldStateSerializedDword);
	memcpy(g_builtinPlanIdByNameIndex, cursor,
	       sizeof(g_builtinPlanIdByNameIndex));
	cursor += sizeof(g_builtinPlanIdByNameIndex);
	g_gameRandFeedbackState = *(int16_t *)cursor;
	cursor += sizeof(g_gameRandFeedbackState);
	g_nextObjectSignature = *(uint16_t *)cursor;
	cursor += sizeof(g_nextObjectSignature);
	g_laserFireTimestampTrackingEnabled = *(int *)cursor;
	cursor += sizeof(g_laserFireTimestampTrackingEnabled);
	memcpy(g_players, cursor, sizeof(g_players));
#endif
}

/* Returns the bytes Flight_SaveWorldState may write: the fixed part plus the
 * size of each flight group's stats and record, each static slot, each main
 * slot with its mobile object, each character data entry, each projectile
 * guidance and each craft data entry. The modern build returns
 * XvtSnapshot_CalculateSize. */
// FUNCTION: XVT 0x4173D0
size_t Flight_CalculateWorldStateBufferSize(void)
{
#ifdef XVT_MODERN
	return XvtSnapshot_CalculateSize();
#else
	int size;

	/* The fixed trailer contains 24 dwords, three words, and one byte around the fixed arrays. */
	size = (int)(2 * sizeof(MissionClock) + sizeof(MissionHeader) +
		     sizeof(FlightMissionState) +
		     sizeof(FlightGlobalCountdownTimers) + sizeof(g_planTable) +
		     sizeof(g_builtinPlanIdByNameIndex) + sizeof(g_players) +
		     24 * sizeof(uint32_t) + 3 * sizeof(uint16_t) +
		     sizeof(uint8_t) + sizeof(WarheadGuidanceState));
	size += (int)(sizeof(MissionFgRuntimeStats) +
		      sizeof(MissionFlightGroup)) *
		g_missionHeader.numFlightGroups;
	size += (int)(sizeof(uint8_t) + sizeof(ObjectRecord)) *
		g_regionStaticObjectSlotCount;
	size += (int)(sizeof(uint8_t) + sizeof(ObjectRecord) +
		      sizeof(MobileObject)) *
		g_regionMainObjectSlotEnd;
	size += (int)sizeof(MobileObjectCharData) *
		(int)g_mobileObjectCharDataCount;
	size += (int)sizeof(WarheadGuidanceState) *
		(int)g_projectileObjectSlotsTotal;
	size += (int)sizeof(CraftData) * g_craftDataPoolCapacity;
	return (size_t)size;
#endif
}

/* Computes the world checksum of the saved state in g_worldStateBuffer into
 * g_worldChecksum and g_worldChecksumRegionLengths: the byte sum of each
 * region, a region closing once it passes a sixteenth of g_worldStateSize,
 * unused entries set to 0; a last region no longer than that is left out. Of
 * each object's record, mobile object and craft data only the first bytes are
 * summed: the struct's size less the sizes of its pointer fields and its cached
 * move and orientation fields (for craft data, less its field_3F2 too, plus
 * 32). The arguments are ignored; the modern build passes them to
 * XvtSnapshot_Checksum instead. */
// FUNCTION: XVT 0x417450
void Flight_ChecksumWorldState(int unusedArg0, int unusedArg1)
{
#ifdef XVT_MODERN
	XvtSnapshot_Checksum(unusedArg0, unusedArg1);
#else
	uint8_t *cursor;
	uint8_t *regionStart;
	unsigned int checksum;
	int regionTargetSize;
	int checksumRegionIndex;
	int objectIndex;
	int objectCount;
	int bytesRemaining;
	int flightGroupCount;

	(void)unusedArg0;
	(void)unusedArg1;

	regionTargetSize = (int)g_worldStateSize >> 4;
	cursor = g_worldStateBuffer;
	regionStart = g_worldStateBuffer;
	checksum = 0;
	checksumRegionIndex = 0;
	objectIndex = 0;
	objectCount = g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	if (objectCount > 0) {
		do {
			if (g_localTransientSlotStart > objectIndex ||
			    g_localDebrisSlotEnd <= objectIndex) {
				uint8_t objectPresent;

				objectPresent = *cursor++;
				if (objectPresent != 0) {
					ObjectRecord *objectState;
					int objectDataBytes;

					objectState = (ObjectRecord *)cursor;
					objectDataBytes =
						sizeof(*objectState) -
						sizeof(objectState->mobj);
					do {
						checksum += *cursor++;
					} while (--objectDataBytes != 0);
					cursor = (uint8_t *)(objectState + 1);
					if (objectState->mobj != NULL) {
						MobileObject *mobileState;
						int mobileDataBytes;

						mobileState =
							(MobileObject *)cursor;
						mobileDataBytes =
							sizeof(*mobileState) -
							sizeof(mobileState
								       ->moveVectorDirty) -
							sizeof(mobileState
								       ->moveX) -
							sizeof(mobileState
								       ->moveY) -
							sizeof(mobileState
								       ->moveZ) -
							sizeof(mobileState
								       ->orientMatrixDirty) -
							sizeof(mobileState
								       ->cachedFwdX) -
							sizeof(mobileState
								       ->cachedFwdY) -
							sizeof(mobileState
								       ->cachedFwdZ) -
							sizeof(mobileState
								       ->cachedSideX) -
							sizeof(mobileState
								       ->cachedSideY) -
							sizeof(mobileState
								       ->cachedSideZ) -
							sizeof(mobileState
								       ->cachedUpX) -
							sizeof(mobileState
								       ->cachedUpY) -
							sizeof(mobileState
								       ->cachedUpZ) -
							sizeof(mobileState
								       ->pWarheadGuidance) -
							sizeof(mobileState
								       ->pCraft) -
							sizeof(mobileState
								       ->pCharData);
						do {
							checksum += *cursor++;
						} while (--mobileDataBytes !=
							 0);
						cursor =
							(uint8_t *)(mobileState +
								    1);
						if (mobileState->pCraft !=
						    NULL) {
							CraftData *craftState;
							int craftDataBytes;

							craftState =
								(CraftData *)
									cursor;
							craftDataBytes =
								sizeof(*craftState) -
								sizeof(craftState
									       ->field_3F2) -
								sizeof(craftState
									       ->turretObjectLinks) -
								sizeof(craftState
									       ->effectiveAiObjectLink) +
								32;
							do {
								checksum +=
									*cursor++;
							} while (
								--craftDataBytes !=
								0);
							cursor =
								(uint8_t *)(craftState +
									    1);
						}
						if (mobileState
							    ->pWarheadGuidance !=
						    NULL) {
							bytesRemaining = sizeof(
								WarheadGuidanceState);
							do {
								checksum +=
									*cursor++;
							} while (
								--bytesRemaining !=
								0);
						}
						if (mobileState->pCharData !=
						    NULL) {
							bytesRemaining = sizeof(
								MobileObjectCharData);
							do {
								checksum +=
									*cursor++;
							} while (
								--bytesRemaining !=
								0);
						}
					}
				}
				if (cursor - regionStart > regionTargetSize) {
					g_worldChecksumRegionLengths
						[checksumRegionIndex] =
							(unsigned int)(cursor -
								       regionStart);
					g_worldChecksum[checksumRegionIndex++] =
						checksum;
					checksum = 0;
					regionStart = cursor;
				}
			}
			++objectIndex;
		} while (objectCount > objectIndex);
	}

	bytesRemaining = 8;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 8;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = sizeof(MissionHeader);
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	flightGroupCount = (int16_t)g_missionHeader.numFlightGroups;
	bytesRemaining = 294 * flightGroupCount;
	if (bytesRemaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytesRemaining != 0);
	}
	if (cursor - regionStart > regionTargetSize) {
		g_worldChecksumRegionLengths[checksumRegionIndex] =
			(unsigned int)(cursor - regionStart);
		g_worldChecksum[checksumRegionIndex++] = checksum;
		checksum = 0;
		regionStart = cursor;
	}

	bytesRemaining = 1382 * flightGroupCount;
	if (bytesRemaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytesRemaining != 0);
	}
	if (cursor - regionStart > regionTargetSize) {
		g_worldChecksumRegionLengths[checksumRegionIndex] =
			(unsigned int)(cursor - regionStart);
		g_worldChecksum[checksumRegionIndex++] = checksum;
		checksum = 0;
		regionStart = cursor;
	}

	bytesRemaining = 3376;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	if (cursor - regionStart > regionTargetSize) {
		g_worldChecksumRegionLengths[checksumRegionIndex] =
			(unsigned int)(cursor - regionStart);
		g_worldChecksum[checksumRegionIndex++] = checksum;
		checksum = 0;
		regionStart = cursor;
	}

	bytesRemaining = 22;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 2;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	if (cursor - regionStart > regionTargetSize) {
		g_worldChecksumRegionLengths[checksumRegionIndex] =
			(unsigned int)(cursor - regionStart);
		g_worldChecksum[checksumRegionIndex++] = checksum;
		checksum = 0;
		regionStart = cursor;
	}

	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	checksum += *cursor++;
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 21760;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 256;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	if (cursor - regionStart > regionTargetSize) {
		g_worldChecksumRegionLengths[checksumRegionIndex] =
			(unsigned int)(cursor - regionStart);
		g_worldChecksum[checksumRegionIndex++] = checksum;
		checksum = 0;
		regionStart = cursor;
	}

	bytesRemaining = 2;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 2;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	bytesRemaining = 11752;
	do {
		checksum += *cursor++;
	} while (--bytesRemaining != 0);
	if (cursor - regionStart > regionTargetSize) {
		g_worldChecksumRegionLengths[checksumRegionIndex] =
			(unsigned int)(cursor - regionStart);
		g_worldChecksum[checksumRegionIndex++] = checksum;
	}
	if (checksumRegionIndex < 16) {
		memset(&g_worldChecksum[checksumRegionIndex], 0,
		       sizeof(g_worldChecksum[0]) * (16 - checksumRegionIndex));
		memset(&g_worldChecksumRegionLengths[checksumRegionIndex], 0,
		       sizeof(g_worldChecksumRegionLengths[0]) *
			       (16 - checksumRegionIndex));
	}
#endif
}

/* Returns worldStateSize divided by 124: the length of each resync segment.
 * Only the original build calls this. */
// FUNCTION: XVT 0x4178F0
int Flight_ComputeWorldStateResyncSegmentSize(int worldStateSize)
{
	return worldStateSize / 124;
}

/* Writes 125 segment checksums of a world state to outChecksums and returns
 * 125. Each segment is worldStateSize divided by 124 bytes (the whole state
 * when that is 0), each byte added and the sum rotated left 1 bit; a segment
 * past the end gets 0. Does not check the room at outChecksums. Only the
 * original build calls this. */
// FUNCTION: XVT 0x417900
int Flight_BuildWorldStateResyncSegmentChecksums(int *outChecksums,
						 uint8_t *worldState,
						 int worldStateSize)
{
	int remainingSize;
	int segmentSize;
	int segmentCount;

	remainingSize = worldStateSize;
	segmentSize = worldStateSize / 124;
	if (segmentSize == 0) {
		segmentSize = worldStateSize;
	}
	segmentCount = 125;
	do {
		uint32_t checksum = 0;
		int bytesInSegment;

		if (segmentSize > 0) {
			bytesInSegment = segmentSize;
			do {
				if (remainingSize != 0) {
					checksum += *worldState++;
					--remainingSize;
#ifdef XVT_MODERN
					checksum = (checksum << 1) |
						   (checksum >> 31);
#else
					checksum = _rotl(checksum, 1);
#endif
				}
			} while (--bytesInSegment != 0);
		}
		*outChecksums++ = (int)checksum;
	} while (--segmentCount != 0);

	return 125;
}

enum FlightWorldStatePresenceFlags {
	FLIGHT_WORLDSTATE_HAS_OBJECT = 0x01,
	FLIGHT_WORLDSTATE_HAS_MOBILE = 0x02,
	FLIGHT_WORLDSTATE_HAS_CRAFT = 0x04,
	FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE = 0x08,
	FLIGHT_WORLDSTATE_HAS_CHAR_DATA = 0x10,
	FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG = 0x80,
	FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK = 0x7F,
	FLIGHT_WORLDSTATE_MAX_EMPTY_RUN = 0x7E
};

/* Writes the presence map of a saved world state (laid out as
 * Flight_SaveWorldState writes it) to outMap and returns its length in bytes:
 * an int holding the object slot count, then for each saved slot a byte of
 * FLIGHT_WORLDSTATE_HAS_ flags for the blocks it holds, with a run of empty
 * slots, up to 126, written as one byte of 0x80 plus the run length. The modern
 * build returns XvtSnapshot_BuildPresenceMap. Only the original build calls
 * this. */
// FUNCTION: XVT 0x417960
int Flight_BuildWorldStateObjectPresenceMap(uint8_t *outMap,
					    uint8_t *worldState)
{
#ifdef XVT_MODERN
	return XvtSnapshot_BuildPresenceMap(outMap, worldState);
#else
	int emptyRunLength;
	uint8_t *mapStart;
	int objectIndex;

	mapStart = outMap;
	*(int *)outMap =
		g_regionStaticObjectSlotCount + g_regionMainObjectSlotEnd;
	outMap += sizeof(int);
	emptyRunLength = 0;
	objectIndex = 0;
	while (objectIndex <
	       g_regionStaticObjectSlotCount + g_regionMainObjectSlotEnd) {
		if (objectIndex < g_localTransientSlotStart ||
		    objectIndex >= g_localDebrisSlotEnd) {
			uint8_t componentFlags;

			componentFlags = 0;
			if (*worldState++ != 0) {
				const ObjectRecord *objectState;

				componentFlags = FLIGHT_WORLDSTATE_HAS_OBJECT;
				objectState = (const ObjectRecord *)worldState;
				worldState += sizeof(*objectState);
				if (objectState->mobj != NULL) {
					const MobileObject *mobileObjectState;

					componentFlags |=
						FLIGHT_WORLDSTATE_HAS_MOBILE;
					mobileObjectState =
						(const MobileObject *)
							worldState;
					worldState +=
						sizeof(*mobileObjectState);
					if (mobileObjectState->pCraft != NULL) {
						componentFlags |=
							FLIGHT_WORLDSTATE_HAS_CRAFT;
						worldState += sizeof(CraftData);
					}
					if (mobileObjectState
						    ->pWarheadGuidance !=
					    NULL) {
						componentFlags |=
							FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE;
						worldState += sizeof(
							WarheadGuidanceState);
					}
					if (mobileObjectState->pCharData !=
					    NULL) {
						componentFlags |=
							FLIGHT_WORLDSTATE_HAS_CHAR_DATA;
						worldState += sizeof(
							MobileObjectCharData);
					}
				}
			}

			if (componentFlags == 0) {
				++emptyRunLength;
				if (emptyRunLength >=
				    FLIGHT_WORLDSTATE_MAX_EMPTY_RUN) {
					*outMap++ =
						(uint8_t)(emptyRunLength |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					emptyRunLength = 0;
				}
			} else {
				if (emptyRunLength != 0) {
					*outMap++ =
						(uint8_t)(emptyRunLength |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					emptyRunLength = 0;
				}
				*outMap++ = componentFlags;
			}
		}
		++objectIndex;
	}

	if (emptyRunLength != 0) {
		*outMap++ = (uint8_t)(emptyRunLength |
				      FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
	}
	return (int)(outMap - mapStart);
#endif
}

#ifndef XVT_MODERN
#pragma function(memcpy)
#endif
/* Reshapes the state gathered in g_worldStateDupBuffer to match another
 * player's presence map: for each saved slot, up to the map's slot count, it
 * takes out the blocks (record, mobile object, craft data, guidance, character
 * data) the map says are absent and puts in zeroed ones it says are present,
 * moving the rest of the buffer, then sets g_worldStateDupSize. Does not check
 * the buffer has room for what it puts in; moving the rest up uses memcpy on
 * overlapping bytes. The modern build calls XvtSnapshot_ApplyPresenceMap. Only
 * the original build calls this. */
// FUNCTION: XVT 0x417A60
void Flight_ApplyWorldStateObjectPresenceMap(const uint8_t *presenceMap)
{
#ifdef XVT_MODERN
	XvtSnapshot_ApplyPresenceMap(presenceMap);
#else
	uint8_t *cursor;
	uint8_t *end;
	int mapSlotLimit;
	int emptyRunRemaining;
	int objectIndex;

	cursor = g_worldStateDupBuffer;
	end = &g_worldStateDupBuffer[g_worldStateDupSize];
	mapSlotLimit = *(const int *)presenceMap;
	presenceMap += sizeof(mapSlotLimit);
	emptyRunRemaining = 0;
	objectIndex = 0;
	while (objectIndex <
	       g_regionStaticObjectSlotCount + g_regionMainObjectSlotEnd) {
		if (g_localTransientSlotStart > objectIndex ||
		    g_localDebrisSlotEnd <= objectIndex) {
			int8_t presence;
			int8_t objectType;

			if (emptyRunRemaining != 0) {
				presence = 0;
				--emptyRunRemaining;
			} else {
				uint16_t emptyRunLength;

				if (mapSlotLimit > objectIndex) {
					presence = (int8_t)*presenceMap++;
				} else {
					break;
				}
				if (presence < 0) {
					emptyRunLength =
						presence &
						FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK;
					presence = 0;
					emptyRunRemaining = emptyRunLength - 1;
				}
			}

			objectType = *cursor++;
			if (objectType != 0) {
				if ((presence & FLIGHT_WORLDSTATE_HAS_OBJECT) !=
				    0) {
					const ObjectRecord *objectState;

					objectState =
						(const ObjectRecord *)cursor;
					cursor += sizeof(*objectState);
					if (objectState->mobj != NULL) {
						if ((presence &
						     FLIGHT_WORLDSTATE_HAS_MOBILE) !=
						    0) {
							const MobileObject
								*mobileState;

							mobileState =
								(const MobileObject
									 *)
									cursor;
							cursor += sizeof(
								*mobileState);
							if (mobileState
								    ->pCraft !=
							    NULL) {
								if ((presence &
								     FLIGHT_WORLDSTATE_HAS_CRAFT) !=
								    0) {
									cursor += sizeof(
										CraftData);
								} else {
									uint8_t *
										blockStart;

									blockStart =
										cursor;
									cursor += sizeof(
										CraftData);
									memcpy(blockStart,
									       cursor,
									       (size_t)(end -
											cursor));
									cursor =
										blockStart;
									end -= sizeof(
										CraftData);
								}
							} else if (
								(presence &
								 FLIGHT_WORLDSTATE_HAS_CRAFT) !=
								0) {
								uint8_t *
									blockStart;

								blockStart =
									cursor;
								cursor += sizeof(
									CraftData);
								memcpy(cursor,
								       blockStart,
								       (size_t)(end -
										blockStart));
								memset(blockStart,
								       0,
								       (size_t)(cursor -
										blockStart));
								end += sizeof(
									CraftData);
							}

							if (mobileState
								    ->pWarheadGuidance !=
							    NULL) {
								if ((presence &
								     FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE) !=
								    0) {
									cursor += sizeof(
										WarheadGuidanceState);
								} else {
									uint8_t *
										blockStart;

									blockStart =
										cursor;
									cursor += sizeof(
										WarheadGuidanceState);
									memcpy(blockStart,
									       cursor,
									       (size_t)(end -
											cursor));
									cursor =
										blockStart;
									end -= sizeof(
										WarheadGuidanceState);
								}
							} else if (
								(presence &
								 FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE) !=
								0) {
								uint8_t *
									blockStart;

								blockStart =
									cursor;
								cursor += sizeof(
									WarheadGuidanceState);
								memcpy(cursor,
								       blockStart,
								       (size_t)(end -
										blockStart));
								memset(blockStart,
								       0,
								       (size_t)(cursor -
										blockStart));
								end += sizeof(
									WarheadGuidanceState);
							}

							if (mobileState
								    ->pCharData !=
							    NULL) {
								if ((presence &
								     FLIGHT_WORLDSTATE_HAS_CHAR_DATA) !=
								    0) {
									cursor += sizeof(
										MobileObjectCharData);
								} else {
									uint8_t *
										blockStart;

									blockStart =
										cursor;
									cursor += sizeof(
										MobileObjectCharData);
									memcpy(blockStart,
									       cursor,
									       (size_t)(end -
											cursor));
									cursor =
										blockStart;
									end -= sizeof(
										MobileObjectCharData);
								}
							} else if (
								(presence &
								 FLIGHT_WORLDSTATE_HAS_CHAR_DATA) !=
								0) {
								uint8_t *
									blockStart;

								blockStart =
									cursor;
								cursor += sizeof(
									MobileObjectCharData);
								memcpy(cursor,
								       blockStart,
								       (size_t)(end -
										blockStart));
								memset(blockStart,
								       0,
								       (size_t)(cursor -
										blockStart));
								end += sizeof(
									MobileObjectCharData);
							}
						} else {
							uint8_t *blockStart;

							blockStart = cursor;
							cursor += sizeof(
								MobileObject);
							memcpy(blockStart,
							       cursor,
							       (size_t)(end -
									cursor));
							end -= sizeof(
								MobileObject);
							cursor = blockStart;
						}
					} else if (
						(presence &
						 FLIGHT_WORLDSTATE_HAS_MOBILE) !=
						0) {
						uint8_t *blockStart;

						blockStart = cursor;
						cursor += sizeof(MobileObject);
						memcpy(cursor, blockStart,
						       (size_t)(end -
								blockStart));
						memset(blockStart, 0,
						       (size_t)(cursor -
								blockStart));
						end += sizeof(MobileObject);
					}
				} else {
					uint8_t *blockStart;

					blockStart = cursor;
					cursor += sizeof(ObjectRecord);
					memcpy(blockStart, cursor,
					       (size_t)(end - cursor));
					end -= sizeof(ObjectRecord);
					cursor = blockStart;
				}
			} else if ((presence & FLIGHT_WORLDSTATE_HAS_OBJECT) !=
				   0) {
				uint8_t *blockStart;

				blockStart = cursor;
				cursor += sizeof(ObjectRecord);
				memcpy(cursor, blockStart,
				       (size_t)(end - blockStart));
				memset(blockStart, 0,
				       (size_t)(cursor - blockStart));
				end += sizeof(ObjectRecord);
			}
		}
		++objectIndex;
	}

	g_worldStateDupSize = (int)(end - g_worldStateDupBuffer);
#endif
}
#ifndef XVT_MODERN
#pragma intrinsic(memcpy)
#endif

/* Runs the simulation from g_gameTime to targetGameTime in steps of at most
 * g_netUpdateIntervalTicks ticks, setting g_elapsedTicks and
 * g_simStepsPerSecond for each. A step applies the players' input frames that
 * are due (Flight_AdvanceOneStep), then, outside the proving grounds, flight
 * group arrivals and the AI; then the timers, weapons, steering and speed,
 * debris recycling near the player (with debris on, outside the proving
 * grounds), collisions, object movement and special behavior, target checks,
 * participation, the mission logic, the message panes, the music and the
 * sounds, and adds the step to g_gameTime. It returns early when a mission end
 * is pending with side effects on. When the target is the current time it only
 * applies the due input frames. Only the original build calls this; its modern
 * arm hands the work to XvtFlightSim_StepToTime. */
// FUNCTION: XVT 0x417D70
void Flight_StepSimToTime(int targetGameTime)
{
#ifdef XVT_MODERN
	XvtFlightSim_StepToTime(targetGameTime);
#else
	enum { MINIMUM_SIM_STEP_TICKS = 1 };

	int gameTime;

	gameTime = g_gameTime;
	g_gunnerCollisionProbeCount = 0;
	for (;;) {
		g_elapsedTicks = (uint16_t)(targetGameTime - gameTime);
		if (g_elapsedTicks < MINIMUM_SIM_STEP_TICKS) {
			break;
		}
		if ((int)(uint16_t)g_elapsedTicks > g_netUpdateIntervalTicks) {
			g_elapsedTicks = (uint16_t)g_netUpdateIntervalTicks;
		}
		g_simStepsPerSecond = (uint16_t)(SIMULATION_TICKS_PER_SECOND /
						 (int)(uint16_t)g_elapsedTicks);
		/* MINIMUM_SIM_STEP_TICKS is reused below as a rate: at least one step per second. */
		if (g_simStepsPerSecond == 0) {
			g_simStepsPerSecond = MINIMUM_SIM_STEP_TICKS;
		}
		g_gameTime = gameTime;
		Flight_AdvanceOneStep(gameTime + (uint16_t)g_elapsedTicks);
		if (g_flightSimSideEffectsSuppressed == 0 &&
		    g_flightMissionState.missionEndPending == 1) {
			return;
		}

		if (g_flightMissionState.provingGroundsModeActive == 0) {
			Mission_UpdateFlightGroupArrivals();
			pai_UpdateAllCraftAI();
		}
		Flight_UpdateTimers();
		laser_weaponsfire();
		Flight_UpdateCraftSteeringAndSpeed();
		if (g_debrisEnabled != 0 &&
		    g_flightMissionState.provingGroundsModeActive == 0) {
			FlightObject_RecycleLocalDebrisNearPlayer();
		}
		collide_collisions();
		if (g_flightSimSideEffectsSuppressed == 0 &&
		    g_flightMissionState.missionEndPending == 1) {
			return;
		}

		Object_UpdateLifetimeAndMovement();
		FlightObject_UpdateSpecialBehavior();
		if (g_flightSimSideEffectsSuppressed == 0 &&
		    g_flightMissionState.missionEndPending == 1) {
			return;
		}

		Player_ValidateAllCurrentTargets();
		Player_UpdateParticipationState();
		if (g_flightSimSideEffectsSuppressed == 0 &&
		    g_flightMissionState.missionEndPending == 1) {
			return;
		}

		Mission_UpdateLogic();
		Hud_UpdateFlightMessagePanes();
		Flight_UpdateDynamicMusicState();
		if (g_fsfxLoaded != 0) {
			fsfx_UpdateVoiceQueue();
			fsfx_UpdateFlightSfx();
		}
		gameTime = g_gameTime;
		gameTime += (uint16_t)g_elapsedTicks;
		g_gameTime = gameTime;
		if (gameTime >= targetGameTime) {
			return;
		}
	}

	g_gameTime = gameTime;
	Flight_AdvanceOneStep(gameTime + (uint16_t)g_elapsedTicks);
#endif
}

/* Applies each active player's due input frames from g_inputHistory, up to
 * targetGameTime, through Flight_UpdatePlayerStep. A frame already applied and
 * not awaiting relay is removed, except in multiplayer with side effects
 * suppressed; a frame past the target is left, and so, with side effects on, is
 * one not from the server (inputSource other than 0). For a frame at or before
 * g_gameTime it first puts the player's craft back to the state saved after its
 * last applied frame, and sets g_gameTime back to that state's time; when the
 * craft has changed it skips the frame with side effects suppressed, else moves
 * the frame to 4 ticks past now. It then moves the craft alone
 * (g_singleObjectUpdateOverrideIdx) up to the frame's time and saves its state
 * in the player record, sets g_replayInputs for the player to the frame's input
 * (key and buttons cleared when it replays a past tick with side effects
 * suppressed) and g_elapsedTicks to the ticks since the player's last frame, at
 * least 4. g_elapsedTicks, g_simStepsPerSecond and g_gameTime are put back
 * after each frame; a player who stops taking part gets no more frames. Sets
 * g_flightSfxSideEffectGate for the local player in multiplayer. Only the
 * original build calls this; its modern arm hands the work to
 * XvtFlightSim_Advance. */
// FUNCTION: XVT 0x417F10
void Flight_AdvanceOneStep(int targetGameTime)
{
#ifdef XVT_MODERN
	XvtFlightSim_Advance(targetGameTime);
#else
	enum {
		PLAYER_COUNT = sizeof(g_inputFrameCount) /
			       sizeof(g_inputFrameCount[0]),
		MINIMUM_REPLAY_TICKS = 4,
	};

	int suppressSideEffects;
	int playerIdx;
	int savedElapsedTicks;
	int savedSimStepsPerSecond;

	suppressSideEffects =
		g_flightPlayerCount == 1 ? 1 : g_flightSimSideEffectsSuppressed;
	for (playerIdx = 0; playerIdx < PLAYER_COUNT; ++playerIdx) {
		InputFrame *frame;
		int frameIteration;
		int frameCount;

		if (g_players[playerIdx].participationState == 0) {
			continue;
		}

		frameCount = g_inputFrameCount[playerIdx];
		frame = g_inputHistory[playerIdx];
		for (frameIteration = 0; frameIteration < frameCount;
		     ++frameIteration, ++frame) {
			int savedGameTime;
			uint8_t participationState;

			if (!((suppressSideEffects != 0 &&
			       g_flightPlayerCount != 1) ||
			      frame->timestamp >
				      g_players[playerIdx].lockstepTimestamp ||
			      frame->awaitingRelay != 0)) {
				FlightSync_RemoveInputHistoryFrame(playerIdx,
								   frame);
				--frame;
				continue;
			}
			if (frame->timestamp > targetGameTime ||
			    (suppressSideEffects == 0 &&
			     frame->inputSource != 0)) {
				continue;
			}
			if (g_players[playerIdx].lockstepTimestamp >=
			    frame->timestamp) {
				continue;
			}

			savedGameTime = g_gameTime;
			if (g_gameTime >= frame->timestamp &&
			    g_players[playerIdx].objectIndex != -1) {
				ObjectRecord *object;
				MobileObject *mobileObject;

				object = &g_objectTable[g_players[playerIdx]
								.objectIndex];
				mobileObject = object->mobj;
				if (mobileObject == NULL ||
				    mobileObject->pCraft == NULL) {
					continue;
				}
				if (g_players[playerIdx].savedObjectSignature ==
					    object->objectSignature &&
				    g_players[playerIdx]
						    .savedAwaitingNewCraft ==
					    g_players[playerIdx]
						    .awaitingNewCraft) {
					if (mobileObject->simStateTimestamp >
					    g_players[playerIdx]
						    .lockstepTimestamp) {
						mobileObject
							->simStateTimestamp =
							g_players[playerIdx]
								.lockstepTimestamp;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.world_x =
							g_players[playerIdx]
								.savedX;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.world_y =
							g_players[playerIdx]
								.savedY;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.world_z =
							g_players[playerIdx]
								.savedZ;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.roll =
							g_players[playerIdx]
								.savedRoll;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.pitch =
							g_players[playerIdx]
								.savedPitch;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.yaw =
							g_players[playerIdx]
								.savedYaw;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj
								->lifetimeTimer =
							g_players[playerIdx]
								.savedLifetimeTimer;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj
								->rollImpulseRate =
							g_players[playerIdx]
								.savedRollImpulseRate;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj->speed =
							g_players[playerIdx]
								.savedSpeed;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj
								->speedRemainder =
							g_players[playerIdx]
								.savedSpeedRemainder;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj->pCraft
								->pitch =
							g_players[playerIdx]
								.savedPitch;
						g_objectTable[g_players[playerIdx]
								      .objectIndex]
							.mobj
							->orientMatrixDirty = 1;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj
								->moveVectorDirty =
							1;
					}
					if (g_gameTime >
					    g_objectTable[g_players[playerIdx]
								  .objectIndex]
						    .mobj->simStateTimestamp) {
						g_gameTime =
							g_objectTable
								[g_players[playerIdx]
									 .objectIndex]
									.mobj
									->simStateTimestamp;
					}
				} else {
					if (suppressSideEffects != 0) {
						continue;
					}
					frame->timestamp = g_gameTime +
							   MINIMUM_REPLAY_TICKS;
					g_players[playerIdx].lockstepTimestamp =
						g_gameTime;
				}
			}

			savedElapsedTicks = g_elapsedTicks;
			savedSimStepsPerSecond = g_simStepsPerSecond;
			if (g_players[playerIdx].objectIndex != -1) {
				g_singleObjectUpdateOverrideIdx =
					g_players[playerIdx].objectIndex;
				if (g_objectTable
					    [g_singleObjectUpdateOverrideIdx]
						    .mobj != NULL) {
					ObjectRecord *object;

					g_elapsedTicks =
						(uint16_t)(frame->timestamp -
							   g_gameTime);
					if (g_elapsedTicks == 0) {
						g_simStepsPerSecond =
							SIMULATION_TICKS_PER_SECOND;
					} else {
						g_simStepsPerSecond =
							(uint16_t)(SIMULATION_TICKS_PER_SECOND /
								   g_elapsedTicks);
					}
					if (g_simStepsPerSecond == 0) {
						g_simStepsPerSecond = 1;
					}
					Flight_UpdateCraftSteeringAndSpeed();
					Object_UpdateLifetimeAndMovement();
					g_objectTable
						[g_singleObjectUpdateOverrideIdx]
							.mobj
							->simStateTimestamp =
						frame->timestamp;
					object =
						&g_objectTable
							[g_singleObjectUpdateOverrideIdx];
					g_players[playerIdx].savedX =
						object->world_x;
					g_players[playerIdx].savedY =
						object->world_y;
					g_players[playerIdx].savedZ =
						object->world_z;
					g_players[playerIdx].savedRoll =
						object->roll;
					g_players[playerIdx].savedPitch =
						object->pitch;
					g_players[playerIdx].savedYaw =
						object->yaw;
					g_players[playerIdx]
						.savedLifetimeTimer =
						object->mobj->lifetimeTimer;
					g_players[playerIdx]
						.savedRollImpulseRate =
						object->mobj->rollImpulseRate;
					g_players[playerIdx].savedSpeed =
						object->mobj->speed;
					g_players[playerIdx]
						.savedSpeedRemainder =
						object->mobj->speedRemainder;
					g_players[playerIdx]
						.savedObjectSignature =
						object->objectSignature;
					g_players[playerIdx]
						.savedAwaitingNewCraft =
						g_players[playerIdx]
							.awaitingNewCraft;
				}
				g_singleObjectUpdateOverrideIdx = -1;
			}

			g_elapsedTicks = (uint16_t)(frame->timestamp -
						    g_players[playerIdx]
							    .lockstepTimestamp);
			if (g_elapsedTicks < MINIMUM_REPLAY_TICKS) {
				g_elapsedTicks = MINIMUM_REPLAY_TICKS;
			}
			if (g_elapsedTicks == 0) {
				g_simStepsPerSecond =
					SIMULATION_TICKS_PER_SECOND;
			} else {
				g_simStepsPerSecond =
					(uint16_t)(SIMULATION_TICKS_PER_SECOND /
						   g_elapsedTicks);
			}
			if (g_simStepsPerSecond == 0) {
				g_simStepsPerSecond = 1;
			}

			g_players[playerIdx].lockstepTimestamp =
				frame->timestamp;
			g_replayInputs[playerIdx] = frame->input;
			if (suppressSideEffects != 0 &&
			    frame->timestamp <= savedGameTime) {
				g_replayInputs[playerIdx].key = 0;
				g_replayInputs[playerIdx].keyMods = 0;
			}
			if (g_flightPlayerCount > 1 &&
			    g_localPlayer == playerIdx) {
				g_flightSfxSideEffectGate = 2;
				if (frame->timestamp >
				    g_lastLocalReplayInputTimestamp) {
					g_flightSfxSideEffectGate = 1;
					g_lastLocalReplayInputTimestamp =
						frame->timestamp;
				}
			}
			Flight_UpdatePlayerStep(playerIdx);
			g_elapsedTicks = (uint16_t)savedElapsedTicks;
			g_simStepsPerSecond = (uint16_t)savedSimStepsPerSecond;
			participationState =
				g_players[playerIdx].participationState;
			g_gameTime = savedGameTime;
			g_flightSfxSideEffectGate = 0;
			if (participationState == 0) {
				break;
			}
		}
	}
#endif
}

#ifndef XVT_MODERN
/* Runs one flight from setup to end; the argument is ignored. It resets the
 * network indicators, abort flags, sound names, remote smoothing and loading
 * progress; sets the resolution mode from the surface width; finds the local
 * player and the player counts; clears the replay inputs and the unused blocks;
 * takes the flight options from g_gameConfig into g_flightMissionState; seeds
 * the game random generators (from the configured seed in multiplayer, else
 * from the clock); clears g_players and marks the session's players as taking
 * part; loads the AI plans; sets up the display and palette, at 1 byte per
 * pixel from the mission's .pal file for entries 64 to 255, or
 * g_generateMissionPalette 1 when there is none; sets the proving grounds craft
 * and level for traincourse; resets input and fills g_flightNoiseTable; resets
 * the message log and MFD pages; loads the mission, its voices and resources;
 * fills the loading bar; sets up the flight runtime state; starts the flight
 * music at a random point; and starts the proving grounds course. When the
 * options sync with the other players fails it frees the flight resources,
 * keeps a copy of the local player, fades the music and returns. Otherwise it
 * saves the world state, draws the first frame, waits for the mission start and
 * runs Flight_RunMissionLoop, then frees the world state buffers, commits the
 * results, frees the resources, saves the player's record (Pilot_Save) and
 * fades the music. Only the original build has this function. */
// FUNCTION: XVT 0x4473C0
void Flight_MainLoop(int unused)
{
	enum {
		PLAYER_COUNT = sizeof(g_players) / sizeof(g_players[0]),
		PALETTE_COLOR_COUNT = 256,
		PALETTE_BYTES = PALETTE_COLOR_COUNT * sizeof(RgbTriplet),
		PALETTE_HALF_BYTES = PALETTE_BYTES / 2,
		PALETTE_LAST_COLOR_OFFSET = PALETTE_BYTES - sizeof(RgbTriplet),
		PALETTE_CHANNEL_MAX = 63,
		FLIGHT_RESOURCE_SCRATCH_BYTES = 1024,
		MISSION_EXTENSION_LENGTH = 3,
		MISSION_EXTENSION_FIRST = 0,
		MISSION_EXTENSION_SECOND = 1,
		MISSION_EXTENSION_THIRD = 2,
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

	uint8_t resourceScratch[FLIGHT_RESOURCE_SCRATCH_BYTES];
	int16_t abortPlayerIndex;
	int16_t disconnectPlayerIndex;
	int16_t connectPlayerIndex;
	int16_t paletteByteOffset;
	int16_t mfdIndex;
	int16_t missionExtensionOffset;
	char savedMissionExtensionPrefix[2];
	char savedMissionExtensionThird;
	int missionSyncSucceeded;
	(void)unused;

	Flight_PumpWindowMessages();
	g_packetDropIndicator = 0;
	g_lagIndicator = 0;
	g_sw3dSkipOddScanlines = 0;
	g_flightNetHostAbortReceived = 0;
	for (abortPlayerIndex = 0; abortPlayerIndex < PLAYER_COUNT;
	     ++abortPlayerIndex) {
		g_playerAbortFlags[abortPlayerIndex] = 0;
	}

	fsfx_ClearSfxNameTable();
	FlightSync_ResetRemotePlayerRenderSmoothing();
	FlightLoading_ResetProgressState();
	Time_ResetElapsedTicks();
	g_flightDisplaySurfacesActive = 1;
	g_flightDrawToHudLayer = 1;
	if (g_flightViewportInsetX == NO_VIEWPORT_INSET &&
	    g_surfaceWidth == 320) {
		g_flightResolutionMode = FLIGHT_RESOLUTION_320X240;
	} else if (g_flightViewportInsetX == NO_VIEWPORT_INSET &&
		   g_surfaceWidth == 480) {
		g_flightResolutionMode = FLIGHT_RESOLUTION_480X360;
	} else {
		g_flightResolutionMode = FLIGHT_RESOLUTION_640X480;
	}

	g_flightSimSideEffectsSuppressed = 0;
	g_unusedFlightSessionResetState = 0;
	g_unusedFlightTransientResetState = 0;
	g_localPlayer =
		NetSession_FindPlayerSlotByDpid(NetSession_GetLocalDplayId());
	g_activeFlightPlayerCount = NetSession_GetPlayerCount();
	g_flightPlayerCount = g_activeFlightPlayerCount;
	memset(g_replayInputs, 0, sizeof(g_replayInputs));
	memset(g_unusedFlightNetworkBlock, 0,
	       sizeof(g_unusedFlightNetworkBlock));
	memset(g_unusedFlightRuntimeBlock, 0,
	       sizeof(g_unusedFlightRuntimeBlock));
	memset(&g_currentInputFrame, 0, sizeof(g_currentInputFrame));
	g_remotePlayerRenderSmoothingEnabled = g_internetPlayEnabled;
	g_flightMissionState.connectedPlayerCount = g_activeFlightPlayerCount;
	g_flightMissionState.maxConnectedPlayerCountThisMission =
		g_activeFlightPlayerCount;

	if ((unsigned int)g_pilotData.numHumanPlayersLastMission > 1 &&
	    (unsigned int)g_pilotData.missionDirectoryId >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		g_flightMissionState.difficulty = GAME_DIFFICULTY_MEDIUM;
	} else {
		g_flightMissionState.difficulty = g_gameConfig.difficulty;
		if (g_flightMissionState.difficulty > GAME_DIFFICULTY_HARD) {
			g_flightMissionState.difficulty = GAME_DIFFICULTY_EASY;
		}
	}
	g_flightMissionState.collisionsEnabled = g_gameConfig.collisions;
	g_flightMissionState.craftJumpingEnabled = g_gameConfig.craftJumping;
	g_flightMissionState.randomVariationEnabled = g_gameConfig.randomSetup;
	g_flightMissionState.battleLengthIndex = g_gameConfig.battleLengthIndex;
	g_flightMissionState.locatePlayersEnabled = g_gameConfig.locatePlayers;
	g_flightMissionState.playerFlightGroupWaveMode =
		g_gameConfig.craftWaves;
	if (g_pilotData.numHumanPlayersLastMission > 1) {
		g_flightMissionState.missionTimeLimitMinutes =
			g_gameConfig.missionTimeLimit;
		g_flightMissionState.teamVictoryTimeLimitMinutes =
			g_gameConfig.lastTeamTimeLimitMinutes;
		g_flightMissionState.aiOpponentsEnabled =
			g_gameConfig.aiOpponents;
	} else {
		g_flightMissionState.missionTimeLimitMinutes = UINT8_MAX;
		g_flightMissionState.teamVictoryTimeLimitMinutes = 0;
		g_flightMissionState.aiOpponentsEnabled = 1;
	}
	g_flightMissionState.craftImpactBounceEnabled = 1;
	if ((unsigned int)g_pilotData.missionDirectoryId >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilotData.missionSequenceActive == 1) {
		g_flightMissionState.randomVariationEnabled = 0;
	}

	if (g_activeFlightPlayerCount != 1) {
		g_gameRandFeedbackState = (int16_t)g_gameConfig.randomSeed;
	} else {
		uint16_t randomSeed;

		randomSeed = (uint16_t)timeGetTime();
		randomSeed ^= RANDOM_SEED_XOR;
		g_gameRandFeedbackState = (int16_t)randomSeed;
	}
	{
		uint32_t randomTime;

		randomTime = timeGetTime();
		g_asteroidFieldRandSeed = (uint16_t)ASTEROID_FIELD_RANDOM_SEED;
		g_gameRand2FeedbackState =
			(uint16_t)(randomTime + g_gameRandFeedbackState);
	}

	memset(g_players, 0, sizeof(g_players));
	{
		int16_t resetPlayerIndex;

		for (resetPlayerIndex = 0; resetPlayerIndex < PLAYER_COUNT;
		     ++resetPlayerIndex) {
			g_inputFrameCount[resetPlayerIndex] = 0;
			g_playerConnected[resetPlayerIndex] = 1;
			g_flightNetWorldChecksumPeerStatus[resetPlayerIndex] =
				0;
			g_players[resetPlayerIndex].lockstepTimestamp = 0;
			g_players[resetPlayerIndex].nextEngineWashCheckTime = 0;
			g_players[resetPlayerIndex].field_5B5 = 0;
		}
	}
	for (disconnectPlayerIndex = 0; disconnectPlayerIndex < PLAYER_COUNT;
	     ++disconnectPlayerIndex) {
		g_players[disconnectPlayerIndex].participationState = 0;
	}
	for (connectPlayerIndex = 0;
	     connectPlayerIndex < g_activeFlightPlayerCount;
	     ++connectPlayerIndex) {
		g_players[connectPlayerIndex].participationState = 1;
	}

	g_flightNetBufferWorldMessagesUntilChecksum = 0;
	g_flightNetWorldChecksumEpoch = 0;
	g_singleObjectUpdateOverrideIdx = -1;
	if (g_flightConfNoPilot == 0) {
		Mission_SyncPilotNetworkPlayersToSessionSlots();
	}
	pai_loadplans((char *)g_paiPlanResourceBaseName);
	pai_cacheBuiltinPlanIds();
	g_hudCockpitResourcesLoaded = 0;
	g_flightSwRotSpriteCoeffCacheValid = 0;
	g_unusedFlightStartupObjectPassState = 0;
	g_unusedFlightDebugLogFile = NULL;
	g_flightSwRotSpriteSpanRunsEnabled = 1;

	FlightSurface_Lock();
	FlightDisplay_ConfigureResolutionState();
	FlightSurface_Unlock();
	FlightRender_TransitionHookStub();
	FlightSurface_Lock();
	FlightSw_InitFramebuffer();
	FlightSurface_Unlock();
	nullsub_11();
	FlightDisplay_Flip();
	FlightRender_ConfigureCallbacksForResolution(3);
	FeDiskIo_ReadAllBytesOrFatal(g_flightPaletteResourceFileName,
				     resourceScratch);
	/* Inside this loop MISSION_EXTENSION_FIRST, _SECOND and _THIRD are reused as the red, green and blue
	 * offsets in a palette triplet. */
	for (paletteByteOffset = 0; paletteByteOffset < PALETTE_HALF_BYTES;
	     paletteByteOffset += sizeof(RgbTriplet)) {
		uint8_t channel;

		channel = resourceScratch[paletteByteOffset +
					  MISSION_EXTENSION_FIRST] >>
			  2;
		resourceScratch[paletteByteOffset + MISSION_EXTENSION_FIRST] =
			resourceScratch[PALETTE_LAST_COLOR_OFFSET -
					paletteByteOffset +
					MISSION_EXTENSION_FIRST] >>
			2;
		resourceScratch[PALETTE_LAST_COLOR_OFFSET - paletteByteOffset +
				MISSION_EXTENSION_FIRST] = channel;
		channel = resourceScratch[paletteByteOffset +
					  MISSION_EXTENSION_SECOND] >>
			  2;
		resourceScratch[paletteByteOffset + MISSION_EXTENSION_SECOND] =
			resourceScratch[PALETTE_LAST_COLOR_OFFSET -
					paletteByteOffset +
					MISSION_EXTENSION_SECOND] >>
			2;
		resourceScratch[PALETTE_LAST_COLOR_OFFSET - paletteByteOffset +
				MISSION_EXTENSION_SECOND] = channel;
		channel = resourceScratch[paletteByteOffset +
					  MISSION_EXTENSION_THIRD] >>
			  2;
		resourceScratch[paletteByteOffset + MISSION_EXTENSION_THIRD] =
			resourceScratch[PALETTE_LAST_COLOR_OFFSET -
					paletteByteOffset +
					MISSION_EXTENSION_THIRD] >>
			2;
		resourceScratch[PALETTE_LAST_COLOR_OFFSET - paletteByteOffset +
				MISSION_EXTENSION_THIRD] = channel;
	}
	g_flightSetPaletteRangeFn((RgbTriplet *)resourceScratch, 0,
				  PALETTE_COLOR_COUNT);
	FlightPalette_ApplyToDisplay();
	FlightSurface_Lock();
	FeDiskIo_InitGlobalBuffers();
	FlightSurface_Unlock();
	FlightSurface_ClearToBlack();

	if (g_flightBytesPerPixel == 1) {
		missionExtensionOffset = (int)strlen(g_currentMissionFile) -
					 MISSION_EXTENSION_LENGTH;
		savedMissionExtensionPrefix[MISSION_EXTENSION_FIRST] =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_FIRST];
		savedMissionExtensionPrefix[MISSION_EXTENSION_SECOND] =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_SECOND];
		savedMissionExtensionThird =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_THIRD];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_FIRST] = 'p';
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_SECOND] = 'a';
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_THIRD] = 'l';
		if (FeDiskIo_OpenGlobalStream(g_currentMissionFile, "rb", 0,
					      0) == 0) {
			g_generateMissionPalette = 1;
		} else {
			g_generateMissionPalette = 0;
			FeDiskIo_CloseGlobalStream(0);
			FeDiskIo_ReadAllBytesOrFatal(g_currentMissionFile,
						     g_flightAuxBuffer);
			g_flightSetPaletteRangeFn(
				(RgbTriplet *)g_flightAuxBuffer,
				PALETTE_CHANNEL_MAX + 1,
				PALETTE_COLOR_COUNT -
					(PALETTE_CHANNEL_MAX + 1));
		}
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_FIRST] =
			savedMissionExtensionPrefix[MISSION_EXTENSION_FIRST];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_SECOND] =
			savedMissionExtensionPrefix[MISSION_EXTENSION_SECOND];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_THIRD] =
			savedMissionExtensionThird;
	}

	if (g_flightConfTrainCourse != 0) {
		g_flightMissionState.provingGroundsCraftType =
			PROVING_GROUNDS_DEFAULT_CRAFT;
		g_flightMissionState.provingGroundsLevel =
			PROVING_GROUNDS_DEFAULT_LEVEL;
	} else {
		g_flightMissionState.provingGroundsCraftType = 0;
		g_flightMissionState.provingGroundsLevel = 0;
	}
	FlightSurface_Lock();
	FlightInput_ResetRuntimeState();
	FlightSurface_Unlock();
	{
		int16_t noiseIndex;

		for (noiseIndex = 0;
		     noiseIndex < (int)sizeof(g_flightNoiseTable) - 1;
		     noiseIndex += 2) {
			do {
				g_flightNoiseTable[noiseIndex] =
					(uint8_t)(rand() & 0x7F);
			} while (g_flightNoiseTable[noiseIndex] >
				 NOISE_TABLE_VALUE_LIMIT);
			g_flightNoiseTable[noiseIndex + 1] =
				(uint8_t)(rand() & 3);
		}
	}

	g_messageLogTotalCount = 0;
	g_unusedFlightMessageRuntimeState = 0;
	g_worldLightDirectionX = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_worldLightDirectionY = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_worldLightDirectionZ = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_systemMessageDisplayEnabled = 1;
	g_readyMessagePaneLeft = -1;
	g_messageLogWriteIndex = UINT16_MAX;
	g_mfdActivePage = MFD_PAGE_NONE;
	g_mfdSecondaryPage = MFD_PAGE_NONE;
	g_mfdSavedActivePage = MFD_PAGE_NONE;
	g_mfdSavedSecondaryPage = MFD_PAGE_NONE;
	for (mfdIndex = 0; mfdIndex < (int)(sizeof(g_mfdPageStates) /
					    sizeof(g_mfdPageStates[0]));
	     ++mfdIndex) {
		g_savedMfdPageStates[mfdIndex] = MFD_PAGE_STATE_CLOSED;
		g_mfdPageStates[mfdIndex] = MFD_PAGE_STATE_CLOSED;
	}
	g_damageMfdCurrentSystemId = 0;

	FlightSurface_Lock();
	Mission_Init(g_currentMissionFile);
	FlightSurface_Unlock();
	fsfx_LoadMissionVoiceSfx();
	FeDiskIo_InitResources();
	if (g_generateMissionPalette != 0) {
		missionExtensionOffset = (int)strlen(g_currentMissionFile) -
					 MISSION_EXTENSION_LENGTH;
		savedMissionExtensionPrefix[MISSION_EXTENSION_FIRST] =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_FIRST];
		savedMissionExtensionPrefix[MISSION_EXTENSION_SECOND] =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_SECOND];
		savedMissionExtensionThird =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_THIRD];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_FIRST] = 'p';
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_SECOND] = 'a';
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_THIRD] = 'l';
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_FIRST] =
			savedMissionExtensionPrefix[MISSION_EXTENSION_FIRST];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_SECOND] =
			savedMissionExtensionPrefix[MISSION_EXTENSION_SECOND];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_THIRD] =
			savedMissionExtensionThird;
	}

	FlightLoading_DrawProgressToCompletion();
	FlightSurface_Lock();
	Mission_InitFlightRuntimeState();
	FlightSurface_Unlock();
	g_dynamicMusicOutcomeLatched = 0;
	if (g_gameConfig.musicEnabled != 0 && g_gameConfig.musicVolume != 0 &&
	    MusicCd_Initialize() != 0) {
		int musicChoice;
		uint16_t musicVolume;
		uint32_t musicUpdateMs;

		musicVolume = UINT16_MAX * g_gameConfig.musicVolume /
			      MUSIC_VOLUME_MAX_LEVEL;
		MusicCd_SetAuxVolume(musicVolume);
		musicChoice = GameRand2() & (MUSIC_START_CHOICE_COUNT - 1);
		MusicCd_PlayTrackFromTime(
			MUSIC_TRACK_FLIGHT,
			g_dynamicMusicInitialStartMinuteChoices[musicChoice],
			g_dynamicMusicInitialStartSecondChoices[musicChoice]);
		g_dynamicMusicTrackRemainingMs =
			MusicCd_GetTrackLengthMs(MUSIC_TRACK_FLIGHT);
		g_dynamicMusicTrackRemainingMs -=
			MILLISECONDS_PER_MINUTE *
			g_dynamicMusicInitialStartMinuteChoices[musicChoice];
		g_dynamicMusicTrackRemainingMs -=
			MILLISECONDS_PER_SECOND *
			g_dynamicMusicInitialStartSecondChoices[musicChoice];
		musicUpdateMs = timeGetTime();
		g_dynamicMusicState = MUSIC_TRACK_FLIGHT;
		g_dynamicMusicLastUpdateMs = musicUpdateMs;
	} else {
		g_dynamicMusicTrackRemainingMs = INT32_MAX;
		g_dynamicMusicState = 0;
	}

	if (g_flightMissionState.provingGroundsModeActive != 0) {
		ProvingGrounds_InitCourseObjects();
		ProvingGrounds_StartLevel(
			g_flightMissionState.provingGroundsLevel);
		if (g_flightMissionState.provingGroundsModeActive != 0 &&
		    g_flightMissionState.provingGroundsLevel > 1) {
			g_msgArgTable[0] =
				g_flightMissionState.provingGroundsLevel - 1;
			msg_emitInFlightMessage(
				IFMSG_197_ARG_10000_POINTS_AWARDED_FOR_PREVIOUS_LEVELS,
				g_localPlayer);
			g_flightMissionState.provingGroundsScore =
				PROVING_GROUNDS_SCORE_STEP_POINTS *
				(PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL *
					 g_flightMissionState
						 .provingGroundsLevel -
				 PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL);
		}
	}

	do {
		g_inputTimestamp += Time_ConsumeElapsedTicks();
	} while (g_inputTimestamp == 0);
	Object_RelinkMobileObjectPointers();
	missionSyncSucceeded = FlightNet_SyncPlayerOptionsAndTaunts();
	if (missionSyncSucceeded == 0) {
		g_flightDisplaySurfacesActive = 0;
		Sound_StopAllInstances();
		Sound_EmptyStub();
		FeDiskIo_FreeFlightResources();
		if (g_preFlightResolutionMode != g_flightResolutionMode) {
			FlightDisplay_ApplyResolutionMode(
				g_preFlightResolutionMode);
		}
		memcpy(&g_localPlayerSnapshotOnOptionsSyncFailure,
		       &g_players[g_localPlayer],
		       sizeof(g_localPlayerSnapshotOnOptionsSyncFailure));
		if (g_gameConfig.musicEnabled != 0 &&
		    g_gameConfig.musicVolume != 0) {
			uint16_t musicVolume;

			musicVolume = (uint16_t)(UINT16_MAX *
						 g_gameConfig.musicVolume /
						 MUSIC_VOLUME_MAX_LEVEL);
			MusicCd_FadeAuxVolume(musicVolume,
					      musicVolume / MUSIC_FADE_DIVISOR,
					      MUSIC_FADE_DURATION_MS);
		}
		MusicCd_CloseDevice();
		return;
	}

	{
		int16_t objectIndex;

		g_unusedFlightStartupObjectPassState = 0;
		for (objectIndex = 0; objectIndex < g_regionMainObjectSlotEnd;
		     ++objectIndex) {
			if (g_objectTable[objectIndex].mobj != NULL) {
				g_objectTable[objectIndex]
					.mobj->simStateTimestamp = 0;
			}
		}
	}
	Flight_AllocWorldStateBuffers();
	FlightSync_ClearBufferedWorldMessages();
	Flight_SaveWorldState();
	FlightView_RenderStartupFrame();
	NetSession_StubReturnTrue();
	if (FlightNet_WaitForMissionStart() != 0) {
		Flight_RunMissionLoop();
	}
	Flight_FreeWorldStateBuffers();
	if (g_unusedFlightDebugLogFile != NULL) {
		File_RawClose(g_unusedFlightDebugLogFile);
	}
	g_flightRenderTransitionHook();
	FeDiskIo_CommitFlightResults(0, 0);
	g_flightDisplaySurfacesActive = 0;
	Sound_StopAllInstances();
	FeDiskIo_FreeFlightResources();
	if (g_preFlightResolutionMode != g_flightResolutionMode) {
		FlightDisplay_ApplyResolutionMode(g_preFlightResolutionMode);
	}
	Pilot_Save(0);
	if (g_gameConfig.musicEnabled != 0 && g_gameConfig.musicVolume != 0) {
		uint16_t musicVolume;

		musicVolume = (uint16_t)(UINT16_MAX * g_gameConfig.musicVolume /
					 MUSIC_VOLUME_MAX_LEVEL);
		MusicCd_FadeAuxVolume(musicVolume,
				      musicVolume / MUSIC_FADE_DIVISOR,
				      MUSIC_FADE_DURATION_MS);
	}
	MusicCd_CloseDevice();
}
#endif

#ifndef XVT_MODERN
/* The original build's frame loop for one mission; it returns when the mission
 * is over. Each pass waits until the input clock (g_inputTimestamp) is at least
 * 8 ticks past g_gameTime and reads the network packets. A client leaves when
 * its host has been silent over 7,080 ticks. Once the mission has ended
 * (Flight_RecountPlayersAndCheckMissionEnd) it returns when the simulation has
 * caught up with the server, writing the message log first when
 * g_radioMessageBackupEnabled, or once the clock is 1,180 ticks past
 * g_gameTime. Solo, it places a step target from the last target plus
 * g_predictedFrameDelta, moved toward the clock, at least 4 ticks past
 * g_gameTime; samples the local input there; steps the simulation to it with
 * side effects on; and moves the server time with it. In multiplayer it first
 * steers the clock toward g_serverTickTime plus g_flightNetClockLeadTicks by a
 * sixteenth of the gap, at most an eighth of g_predictedFrameDelta (kept in
 * g_flightNetClockAdjustAccumTicks), jumps forward when behind the server, and
 * on a client more than 1,652 ticks further ahead than
 * g_flightNetClockLeadTicks shows the communication failure alert and waits for
 * the server, where ESC or a 7,080-tick silence makes the player leave and
 * returns. It then places the step target the same way, samples the local
 * input, queues predicted remote input and steps the simulation with side
 * effects suppressed. Each pass then sets g_lagIndicator, 0 to 3 as the clock
 * runs 472, 944 and 1,416 ticks beyond g_serverTickTime plus
 * g_flightNetClockLeadTicks, and g_packetDropIndicator, smooths the remote
 * craft, draws the frame (FlightView_RenderFrame) and plays the queued sounds.
 * With the tickcounter option it also fills g_flightUpdateDurationHistogram and
 * formats timing lines into buffers nothing shows. Only the original build has
 * this function. */
// FUNCTION: XVT 0x447F50
void Flight_RunMissionLoop(void)
{
	enum {
		MINIMUM_FRAME_ADVANCE_TICKS = 4,
		MINIMUM_FRAME_WAIT_TICKS = 8,
		FRAME_ADJUST_DIVISOR_SHIFT = 3,
		CLOCK_ADJUST_DIVISOR_SHIFT = 4,
		MAX_FINE_FRAME_ADJUSTMENT = 4,
		MISSION_END_WAIT_LIMIT_TICKS = 1180,
		LAG_LEVEL_1_TICKS = 472,
		LAG_LEVEL_2_TICKS = 944,
		LAG_LEVEL_3_TICKS = 1416,
		EXCESSIVE_CLOCK_LEAD_TICKS = 1652,
		HOST_TIMEOUT_TICKS = 7080,
		COUNTDOWN_INTERVAL_TICKS = 118,
		COUNTDOWN_HALF_SECONDS_THRESHOLD = 50,
		UPDATE_HISTOGRAM_BUCKETS = 20,
		LONG_UPDATE_TICKS = 8,
		PACKET_DROP_SCORE_STEP = 10,
		PACKET_DROP_LEVEL_2_SCORE = 10,
		PACKET_DROP_LEVEL_3_SCORE = 20,
		ALERT_BACKGROUND_COLOR = 0x34
	};

	int previousRemainingHalfSeconds;
	int frameStartTimestamp;
	int loopStartTimestamp;
	int simulationWarpTicks;
	char countdownText[80];
	char statusLine[256];
	char overlayLine[180];
	char tooFarAheadLogLine[180];
	char fellBehindLogLine[180];

	g_flightLastStepTargetTimestamp = 0;
	g_lastLocalReplayInputTimestamp = 0;
	g_flightSfxSideEffectGate = 0;
	g_flightPrevHostPacketDropCount = 0;
	g_flightPacketDropScore = 0;
	memset(g_flightUpdateDurationHistogram, 0,
	       sizeof(g_flightUpdateDurationHistogram));

	for (;;) {
		int networkUpdateTicks;
		int renderTicks;
		int updateTicks;
		int loopTicks;
		int frameTargetTimestamp;
		int savedInputTimestamp;
		int packetStartTimestamp;
		int missionEnded;

		simulationWarpTicks = 0;
		g_inputTimestamp += Time_ConsumeElapsedTicks();
		loopStartTimestamp = g_inputTimestamp;
		for (g_inputTimestamp += Time_ConsumeElapsedTicks();
		     g_inputTimestamp - g_gameTime < MINIMUM_FRAME_WAIT_TICKS;
		     g_inputTimestamp += Time_ConsumeElapsedTicks()) {
		}

		packetStartTimestamp = g_inputTimestamp;
		FlightNet_ProcessIncomingPackets();
		if (NetSession_IsLocalHost() == 0 &&
		    g_flightNetHostTimeoutElapsedTicks > HOST_TIMEOUT_TICKS) {
			g_players[g_localPlayer].participationState = 0;
			FlightNet_MarkPilotNetworkPlayerLeft(g_localPlayer);
			return;
		}
		g_inputTimestamp += Time_ConsumeElapsedTicks();
		networkUpdateTicks = g_inputTimestamp;
		missionEnded = Flight_RecountPlayersAndCheckMissionEnd();
		networkUpdateTicks -= packetStartTimestamp;

		if (missionEnded != 0) {
			if (g_gameTime == g_serverTickTime) {
				if (g_radioMessageBackupEnabled != 0) {
					msg_writeMessageLogFile();
					g_radioMessageBackupEnabled = 0;
				}
				return;
			}
			if (g_inputTimestamp - g_gameTime >
			    MISSION_END_WAIT_LIMIT_TICKS) {
				return;
			}
			for (g_inputTimestamp += Time_ConsumeElapsedTicks();
			     g_inputTimestamp - g_gameTime <
			     MINIMUM_FRAME_WAIT_TICKS;
			     g_inputTimestamp += Time_ConsumeElapsedTicks()) {
			}
			continue;
		}

		frameStartTimestamp = g_inputTimestamp;
		if (g_flightPlayerCount == 1) {
			int frameAdjustment;

			g_inputTimestamp += Time_ConsumeElapsedTicks();
			if (g_flightLastStepTargetTimestamp == 0) {
				frameTargetTimestamp = g_inputTimestamp;
			} else {
				frameTargetTimestamp =
					g_flightLastStepTargetTimestamp +
					g_predictedFrameDelta;
				if ((unsigned int)frameTargetTimestamp >=
				    (unsigned int)g_inputTimestamp) {
					if ((unsigned int)frameTargetTimestamp >
					    (unsigned int)g_inputTimestamp) {
						frameAdjustment =
							frameTargetTimestamp -
							g_inputTimestamp;
						if (frameAdjustment >
						    g_predictedFrameDelta >>
						    FRAME_ADJUST_DIVISOR_SHIFT) {
							frameAdjustment =
								g_predictedFrameDelta >>
								FRAME_ADJUST_DIVISOR_SHIFT;
						}
						if (frameAdjustment == 0) {
							frameAdjustment = 1;
						}
						if (frameAdjustment >
						    MAX_FINE_FRAME_ADJUSTMENT) {
							frameAdjustment =
								frameTargetTimestamp -
								g_inputTimestamp;
						}
						frameTargetTimestamp -=
							frameAdjustment;
					}
				} else {
					frameAdjustment = g_inputTimestamp -
							  frameTargetTimestamp;
					if (frameAdjustment >
					    g_predictedFrameDelta >>
					    FRAME_ADJUST_DIVISOR_SHIFT) {
						frameAdjustment =
							g_predictedFrameDelta >>
							FRAME_ADJUST_DIVISOR_SHIFT;
					}
					if (frameAdjustment == 0) {
						frameAdjustment = 1;
					}
					if (frameAdjustment >
					    MAX_FINE_FRAME_ADJUSTMENT) {
						frameAdjustment =
							g_inputTimestamp -
							frameTargetTimestamp;
					}
					frameTargetTimestamp += frameAdjustment;
				}
			}
			if (frameTargetTimestamp - g_gameTime <
			    MINIMUM_FRAME_ADVANCE_TICKS) {
				frameTargetTimestamp =
					g_gameTime +
					MINIMUM_FRAME_ADVANCE_TICKS;
			}
			g_predictedFrameDelta = frameTargetTimestamp -
						g_flightLastStepTargetTimestamp;
			savedInputTimestamp = g_inputTimestamp;
			g_inputTimestamp = frameTargetTimestamp;
			FlightNet_SampleLocalInput();
			g_flightSimSideEffectsSuppressed = 0;
			g_netUpdateIntervalTicks =
				g_inputTimestamp - g_gameTime;
			Flight_StepSimToTime(g_inputTimestamp);
			g_gameTime = g_inputTimestamp;
			g_serverTickTime = g_inputTimestamp;
			g_flightLastStepTargetTimestamp = g_inputTimestamp;
			g_inputTimestamp +=
				savedInputTimestamp - frameTargetTimestamp;
			Sound_FlushQueuedEffects();
		} else {
			int clockAdjustment;

			g_inputTimestamp += Time_ConsumeElapsedTicks();
			if (g_serverTickTime + g_flightNetClockLeadTicks >=
			    (unsigned int)g_inputTimestamp) {
				if (g_serverTickTime +
					    g_flightNetClockLeadTicks >
				    (unsigned int)g_inputTimestamp) {
					clockAdjustment =
						(g_serverTickTime +
						 g_flightNetClockLeadTicks -
						 g_inputTimestamp) >>
						CLOCK_ADJUST_DIVISOR_SHIFT;
					if (clockAdjustment == 0) {
						clockAdjustment = 1;
					}
					if (clockAdjustment >
					    g_predictedFrameDelta >>
					    FRAME_ADJUST_DIVISOR_SHIFT) {
						clockAdjustment =
							g_predictedFrameDelta >>
							FRAME_ADJUST_DIVISOR_SHIFT;
					}
					g_flightNetClockAdjustAccumTicks -=
						clockAdjustment;
					g_inputTimestamp += clockAdjustment;
				}
			} else {
				clockAdjustment = (g_inputTimestamp -
						   g_flightNetClockLeadTicks -
						   g_serverTickTime) >>
						  CLOCK_ADJUST_DIVISOR_SHIFT;
				if (clockAdjustment == 0) {
					clockAdjustment = 1;
				}
				if (clockAdjustment > g_predictedFrameDelta >>
				    FRAME_ADJUST_DIVISOR_SHIFT) {
					clockAdjustment =
						g_predictedFrameDelta >>
						FRAME_ADJUST_DIVISOR_SHIFT;
				}
				g_flightNetClockAdjustAccumTicks +=
					clockAdjustment;
				g_inputTimestamp -= clockAdjustment;
			}

			if (g_serverTickTime > g_inputTimestamp) {
				sprintf(fellBehindLogLine,
					"Fell Behind! tickcounter:%-7d serverticks:%-7d adjustment:%-4d\n",
					g_inputTimestamp, g_serverTickTime,
					g_serverTickTime +
						g_flightNetClockLeadTicks -
						g_inputTimestamp);
				clockAdjustment = g_serverTickTime +
						  g_flightNetClockLeadTicks -
						  g_inputTimestamp;
				g_inputTimestamp += clockAdjustment;
				g_flightNetClockAdjustAccumTicks -=
					clockAdjustment;
			}

			if (g_inputTimestamp - g_serverTickTime >
				    g_flightNetClockLeadTicks +
					    EXCESSIVE_CLOCK_LEAD_TICKS &&
			    NetSession_IsLocalHost() == 0) {
				int statusPulseTicks;
				unsigned int frameDelta;

				FlightNet_BroadcastPlayerDisconnected(
					g_localPlayer);
				sprintf(tooFarAheadLogLine,
					"Too far Ahead! tickcounter:%-7d serverticks:%-7d warp:%-4d\n",
					g_inputTimestamp, g_serverTickTime,
					g_inputTimestamp - g_serverTickTime);
				statusPulseTicks = 0;
				frameDelta = Time_ConsumeElapsedTicks();
				g_flightNetHostTimeoutElapsedTicks = 0;
				g_inputTimestamp += frameDelta;
				FlightAlert_SaveBoxBackground();
				strcpy(statusLine,
				       g_strDiskIoMessages
					       [DISK_IO_STR_COM_FAILURE_WAITING]);
				{
					char *playerName;

					playerName =
						FlightNet_ResolveResyncPlayerName();
					if (playerName != NULL) {
						strcat(statusLine, playerName);
					}
				}
				FlightAlert_DrawBox(1, statusLine,
						    ALERT_BACKGROUND_COLOR);

				do {
					int packetStartTimestamp;
					int remainingHalfSeconds;

					if (g_inputTimestamp -
						    g_serverTickTime <=
					    g_flightNetClockLeadTicks) {
						break;
					}
					if (FlightInput_HasKeyReady() != 0 &&
					    FlightInput_GetNextKey() ==
						    FLIGHT_KEY_ESCAPE) {
						g_flightMissionState
							.missionEndPending = 1;
						FlightNet_BroadcastPlayerAbort(
							g_localPlayer);
						g_playerAbortFlags
							[g_localPlayer] = 1;
						g_players[g_localPlayer]
							.participationState = 0;
						FlightNet_MarkPilotNetworkPlayerLeft(
							g_localPlayer);
						return;
					}

					packetStartTimestamp = g_inputTimestamp;
					FlightNet_ProcessIncomingPackets();
					statusPulseTicks -=
						packetStartTimestamp;
					g_inputTimestamp +=
						Time_ConsumeElapsedTicks();
					statusPulseTicks += g_inputTimestamp;
					g_flightNetHostTimeoutElapsedTicks +=
						g_inputTimestamp -
						packetStartTimestamp;
					g_inputTimestamp = packetStartTimestamp;
					if (g_flightNetHostTimeoutElapsedTicks >
					    HOST_TIMEOUT_TICKS) {
						g_flightMissionState
							.missionEndPending = 1;
						FlightNet_BroadcastPlayerAbort(
							g_localPlayer);
						g_playerAbortFlags
							[g_localPlayer] = 1;
						g_players[g_localPlayer]
							.participationState = 0;
						FlightNet_MarkPilotNetworkPlayerLeft(
							g_localPlayer);
						return;
					}
					if (statusPulseTicks >
					    SIMULATION_TICKS_PER_SECOND) {
						statusPulseTicks = 0;
						FlightNet_SendStillLoadingPulse();
					}

					remainingHalfSeconds =
						(HOST_TIMEOUT_TICKS -
						 g_flightNetHostTimeoutElapsedTicks) /
						COUNTDOWN_INTERVAL_TICKS;
					if (previousRemainingHalfSeconds !=
					    remainingHalfSeconds) {
						previousRemainingHalfSeconds =
							remainingHalfSeconds;
						if (remainingHalfSeconds >=
						    COUNTDOWN_HALF_SECONDS_THRESHOLD) {
							if ((remainingHalfSeconds &
							     1) != 0) {
								FlightAlert_DrawBox(
									3,
									g_strDiskIoMessages
										[DISK_IO_STR_ESC_DISCONNECT],
									ALERT_BACKGROUND_COLOR);
							} else {
								FlightAlert_DrawBox(
									3,
									g_strDiskIoMessages
										[DISK_IO_STR_RECOVERING_WAIT],
									ALERT_BACKGROUND_COLOR);
							}
						} else {
							sprintf(countdownText,
								g_strDiskIoMessages
									[DISK_IO_STR_DISCONNECT_COUNTDOWN],
								remainingHalfSeconds /
									2,
								5 * (remainingHalfSeconds &
								     1));
							FlightAlert_DrawBox(
								3,
								countdownText,
								ALERT_BACKGROUND_COLOR);
						}
					}
				} while (
					Flight_RecountPlayersAndCheckMissionEnd() ==
						0 ||
					g_gameTime != g_serverTickTime);

				FlightAlert_RestoreBoxBackground();
				Time_ConsumeElapsedTicks();
				if (Flight_RecountPlayersAndCheckMissionEnd() !=
					    0 &&
				    g_gameTime == g_serverTickTime) {
					return;
				}
				g_inputTimestamp = g_serverTickTime +
						   g_flightNetClockLeadTicks;
			}

			if ((unsigned int)g_inputTimestamp >
			    (unsigned int)g_gameTime) {
				int frameAdjustment;

				if (g_flightLastStepTargetTimestamp == 0) {
					frameTargetTimestamp = g_inputTimestamp;
				} else {
					frameTargetTimestamp =
						g_flightLastStepTargetTimestamp +
						g_predictedFrameDelta;
					if ((unsigned int)
						    frameTargetTimestamp >=
					    (unsigned int)g_inputTimestamp) {
						if ((unsigned int)
							    frameTargetTimestamp >
						    (unsigned int)
							    g_inputTimestamp) {
							frameAdjustment =
								frameTargetTimestamp -
								g_inputTimestamp;
							if (frameAdjustment >
							    g_predictedFrameDelta >>
							    FRAME_ADJUST_DIVISOR_SHIFT) {
								frameAdjustment =
									g_predictedFrameDelta >>
									FRAME_ADJUST_DIVISOR_SHIFT;
							}
							if (frameAdjustment ==
							    0) {
								frameAdjustment =
									1;
							}
							if (frameAdjustment >
							    MAX_FINE_FRAME_ADJUSTMENT) {
								frameAdjustment =
									frameTargetTimestamp -
									g_inputTimestamp;
							}
							frameTargetTimestamp -=
								frameAdjustment;
						}
					} else {
						frameAdjustment =
							g_inputTimestamp -
							frameTargetTimestamp;
						if (frameAdjustment >
						    g_predictedFrameDelta >>
						    FRAME_ADJUST_DIVISOR_SHIFT) {
							frameAdjustment =
								g_predictedFrameDelta >>
								FRAME_ADJUST_DIVISOR_SHIFT;
						}
						if (frameAdjustment == 0) {
							frameAdjustment = 1;
						}
						if (frameAdjustment >
						    MAX_FINE_FRAME_ADJUSTMENT) {
							frameAdjustment =
								g_inputTimestamp -
								frameTargetTimestamp;
						}
						frameTargetTimestamp +=
							frameAdjustment;
					}
				}
				if (frameTargetTimestamp - g_gameTime <
				    MINIMUM_FRAME_ADVANCE_TICKS) {
					frameTargetTimestamp =
						g_gameTime +
						MINIMUM_FRAME_ADVANCE_TICKS;
				}
				g_predictedFrameDelta =
					frameTargetTimestamp -
					g_flightLastStepTargetTimestamp;
				savedInputTimestamp = g_inputTimestamp;
				g_inputTimestamp = frameTargetTimestamp;
				FlightNet_SampleLocalInput();
				FlightSync_QueuePredictedRemoteInputFrames(
					g_predictedFrameDelta);
				g_flightSimSideEffectsSuppressed = 1;
				simulationWarpTicks =
					g_inputTimestamp - g_gameTime;
				Flight_StepSimToTime(g_inputTimestamp);
				g_gameTime = g_inputTimestamp;
				g_flightLastStepTargetTimestamp =
					g_inputTimestamp;
				g_inputTimestamp += savedInputTimestamp -
						    frameTargetTimestamp;
			}
		}

		g_inputTimestamp += Time_ConsumeElapsedTicks();
		updateTicks = g_inputTimestamp;
		updateTicks -= frameStartTimestamp;
		g_inputTimestamp += Time_ConsumeElapsedTicks();
		{
			int lagTicks;

			savedInputTimestamp = g_inputTimestamp;
			lagTicks = g_inputTimestamp -
				   g_flightNetClockLeadTicks - g_serverTickTime;
			if (lagTicks < LAG_LEVEL_1_TICKS) {
				g_lagIndicator = 0;
			} else if (lagTicks < LAG_LEVEL_2_TICKS) {
				g_lagIndicator = 1;
			} else if (lagTicks < LAG_LEVEL_3_TICKS) {
				g_lagIndicator = 2;
			} else {
				g_lagIndicator = 3;
			}
		}

		if (g_flightPrevHostPacketDropCount == 0) {
			g_packetDropIndicator = 0;
		} else {
			int hostDplayId;
			int hostDropCount;

			hostDplayId = NetSession_GetHostDplayId();
			hostDropCount =
				NetReliable_GetPeerPacketDropCountByDpid(
					hostDplayId);
			g_flightPacketDropScore +=
				PACKET_DROP_SCORE_STEP *
				(hostDropCount -
				 g_flightPrevHostPacketDropCount);
			if (g_flightPacketDropScore == 0) {
				g_packetDropIndicator = 0;
			} else if (g_flightPacketDropScore <
				   PACKET_DROP_LEVEL_2_SCORE) {
				g_packetDropIndicator = 1;
			} else if (g_flightPacketDropScore <
				   PACKET_DROP_LEVEL_3_SCORE) {
				g_packetDropIndicator = 2;
			} else {
				g_packetDropIndicator = 3;
			}
			g_flightPrevHostPacketDropCount = hostDropCount;
			if (g_flightPacketDropScore != 0) {
				--g_flightPacketDropScore;
			}
		}

		FlightSync_ApplyRemotePlayerRenderSmoothing();
		if (g_flightPlayerCount > 1) {
			g_flightSfxSideEffectGate = 1;
		}
		FlightView_RenderFrame();
		g_flightSfxSideEffectGate = 0;
		Sound_FlushQueuedEffects();
		FlightSync_CaptureSamplesAndRestorePoses();
		g_inputTimestamp += Time_ConsumeElapsedTicks();
		renderTicks = g_inputTimestamp;
		renderTicks -= savedInputTimestamp;
		loopTicks = g_inputTimestamp;
		loopTicks -= loopStartTimestamp;
		if (g_inputTimestamp == loopStartTimestamp) {
			loopTicks = 1;
		}

		if (g_flightConfTickCounterEnabled == 0) {
			g_flightTickOverlaySampleCount = 0;
			g_flightTickOverlayWindowTicks = 0;
		} else {
			unsigned int histogramTotal;
			int histogramIndex;
			int aiProjectileCount;
			int playerProjectileCount;
			int objectIndex;

			if (g_flightTickOverlayWindowTicks >
			    LAG_LEVEL_2_TICKS) {
				g_flightTickOverlaySampleCount = 0;
				g_flightTickOverlayWindowTicks = 0;
			}
			g_flightTickOverlayLastLoopTicks = loopTicks;
			g_flightTickOverlayWindowTicks += loopTicks;
			++g_flightTickOverlaySampleCount;
			sprintf(overlayLine,
				"R:%-2d U:%-2d N:%-2d O:%-2d T:%-2d FR:%-2d NOW:%-7dL:%-7dS:%-7dW:%-3dD:%-3dA%d\n",
				renderTicks, updateTicks, networkUpdateTicks,
				loopTicks - networkUpdateTicks - updateTicks -
					renderTicks,
				loopTicks,
				SIMULATION_TICKS_PER_SECOND / loopTicks,
				g_inputTimestamp, g_gameTime, g_serverTickTime,
				g_inputTimestamp - g_serverTickTime,
				g_flightNetClockLeadTicks,
				g_flightNetClockAdjustAccumTicks);
			if (updateTicks < 0) {
				updateTicks = 0;
			}
			if (updateTicks > UPDATE_HISTOGRAM_BUCKETS - 1) {
				++g_flightUpdateDurationHistogram
					[UPDATE_HISTOGRAM_BUCKETS - 1];
			} else {
				++g_flightUpdateDurationHistogram[updateTicks];
			}
			sprintf(g_missionDebugBuffer,
				"Raw  0:%2d  1:%2d  2:%2d  3:%2d  4:%2d  5:%2d  6:%2d  7:%2d  8:%2d   9:%2d\n",
				g_flightUpdateDurationHistogram[0],
				g_flightUpdateDurationHistogram[1],
				g_flightUpdateDurationHistogram[2],
				g_flightUpdateDurationHistogram[3],
				g_flightUpdateDurationHistogram[4],
				g_flightUpdateDurationHistogram[5],
				g_flightUpdateDurationHistogram[6],
				g_flightUpdateDurationHistogram[7],
				g_flightUpdateDurationHistogram[8],
				g_flightUpdateDurationHistogram[9]);
			sprintf(g_missionDebugBuffer,
				"Raw 10:%2d 11:%2d 12:%2d 13:%2d 14:%2d 15:%2d 16:%2d 17:%2d 18:%2d >18:%2d\n",
				g_flightUpdateDurationHistogram[10],
				g_flightUpdateDurationHistogram[11],
				g_flightUpdateDurationHistogram[12],
				g_flightUpdateDurationHistogram[13],
				g_flightUpdateDurationHistogram[14],
				g_flightUpdateDurationHistogram[15],
				g_flightUpdateDurationHistogram[16],
				g_flightUpdateDurationHistogram[17],
				g_flightUpdateDurationHistogram[18],
				g_flightUpdateDurationHistogram[19]);

			histogramTotal = 0;
			for (histogramIndex = 0;
			     histogramIndex < UPDATE_HISTOGRAM_BUCKETS;
			     ++histogramIndex) {
				histogramTotal +=
					g_flightUpdateDurationHistogram
						[histogramIndex];
			}
			if (histogramTotal != 0) {
				sprintf(g_missionDebugBuffer,
					"Pct  0:%2d  1:%2d  2:%2d  3:%2d  4:%2d  5:%2d  6:%2d  7:%2d  8:%2d   9:%2d\n",
					100 *
						g_flightUpdateDurationHistogram
							[0] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[1] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[2] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[3] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[4] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[5] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[6] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[7] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[8] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[9] /
						histogramTotal);
				sprintf(g_missionDebugBuffer,
					"Pct 10:%2d 11:%2d 12:%2d 13:%2d 14:%2d 15:%2d 16:%2d 17:%2d 18:%2d >18:%2d\n",
					100 *
						g_flightUpdateDurationHistogram
							[10] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[11] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[12] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[13] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[14] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[15] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[16] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[17] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[18] /
						histogramTotal,
					100 *
						g_flightUpdateDurationHistogram
							[19] /
						histogramTotal);
			}

			aiProjectileCount = 0;
			playerProjectileCount = 0;
			for (objectIndex = g_projectileObjectSlotStart;
			     objectIndex < g_projectileObjectSlotEnd;
			     ++objectIndex) {
				if (g_objectTable[objectIndex].objectType !=
				    0) {
					if (g_objectTable[objectIndex]
						    .genusId == 6) {
						++playerProjectileCount;
					} else {
						++aiProjectileCount;
					}
				}
			}
			if (updateTicks >= LONG_UPDATE_TICKS) {
				sprintf(g_missionDebugBuffer,
					"****** Long Update: %d ***** Warp: %d  *****  Player:  %d *****  AI:  %d\n",
					updateTicks, simulationWarpTicks,
					playerProjectileCount,
					aiProjectileCount);
			}
		}
	}
}
#endif

/* Counts the players taking part (participationState 1 or 2), stores the count
 * in g_activeFlightPlayerCount and returns it. */
// FUNCTION: XVT 0x448CD0
int Flight_UpdateActivePlayerCount(void)
{
	int activePlayerCount;
	int playerIndex;

	activePlayerCount = 0;
	for (playerIndex = 0; playerIndex < 8; playerIndex++) {
		if (g_players[playerIndex].participationState == 1 ||
		    g_players[playerIndex].participationState == 2) {
			activePlayerCount++;
		}
		g_activeFlightPlayerCount = activePlayerCount;
	}

	return activePlayerCount;
}

/* Recounts g_activeFlightPlayerCount as Flight_UpdateActivePlayerCount does,
 * sets g_flightMissionState.missionEndPending to 1 when no player is connected
 * (participationState 1) or the local player no longer takes part, and returns
 * missionEndPending. */
// FUNCTION: XVT 0x448D00
int Flight_RecountPlayersAndCheckMissionEnd(void)
{
	int playerIndex;
	int connectedCount;
	int connectedOrPendingCount;
	uint8_t connectedState;

	connectedCount = 0;
	connectedOrPendingCount = 0;
	for (playerIndex = 0; playerIndex < 8; playerIndex++) {
		connectedState = g_players[playerIndex].participationState;
		if (connectedState == 1 || connectedState == 2) {
			connectedOrPendingCount++;
		}
		if (connectedState == 1) {
			connectedCount++;
		}
		g_activeFlightPlayerCount = connectedOrPendingCount;
	}

	if (connectedCount == 0) {
		g_flightMissionState.missionEndPending = 1;
	}
	if (g_players[g_localPlayer].participationState == 0) {
		g_flightMissionState.missionEndPending = 1;
	}
	return g_flightMissionState.missionEndPending;
}

/* Returns a checksum of the live world, each part folded in by XOR and a left
 * rotation of 1 bit: the Flight_ChecksumBufferRotateXor checksum of every live
 * object's character data, mobile object, record and craft data, every
 * projectile's guidance, the mission clocks, flight group stats and records,
 * g_flightMissionState, the timers, the mission header, messages and global
 * goals, the AI plan table and order data, and each active player's record,
 * along with the slot ranges, pool sizes and other counters. The modern build
 * returns XvtSnapshot_LiveChecksum. */
// FUNCTION: XVT 0x462A90
int Flight_ComputeLiveWorldStateChecksum(void)
{
#ifdef XVT_MODERN
	return XvtSnapshot_LiveChecksum();
#else
	uint32_t checksum;
	uint32_t *checksumPtr;
	int firstSlot;
	int charDataIndex;
	int mobileObjectIndex;
	int objectIndex;
	int staticObjectIndex;
	int craftIndex;
	int projectileIndex;
	int flightGroupIndex;
	int goalIndex;
	int playerIndex;

	checksum = 0;
	checksumPtr = &checksum;
	firstSlot = g_objectSlotRangeByGenus[16].start;
	for (charDataIndex = 0;
	     charDataIndex < (int)g_mobileObjectCharDataCount;
	     charDataIndex++) {
		if (g_objectTable[firstSlot + charDataIndex].objectType != 0) {
			*checksumPtr ^= Flight_ChecksumBufferRotateXor(
				&g_mobileObjectCharDataPool[charDataIndex],
				0x4C);
			checksum = Flight_RotateChecksumLeft(*checksumPtr);
		}
	}

	for (mobileObjectIndex = 0;
	     mobileObjectIndex <
	     g_regionMainObjectSlotEnd - g_regionMainObjectSlotStart;
	     mobileObjectIndex++) {
		if (g_objectTable[mobileObjectIndex].objectType != 0) {
			*checksumPtr ^= Flight_ChecksumBufferRotateXor(
				&g_mobileObjectPoolBase[mobileObjectIndex],
				0x8B);
			checksum = Flight_RotateChecksumLeft(*checksumPtr);
		}
	}
	for (objectIndex = 0; objectIndex < g_regionMainObjectSlotEnd -
						    g_regionMainObjectSlotStart;
	     objectIndex++) {
		if (g_objectTable[objectIndex].objectType != 0) {
			*checksumPtr ^= Flight_ChecksumBufferRotateXor(
				&g_objectTable[objectIndex], 0x1F);
			checksum = Flight_RotateChecksumLeft(*checksumPtr);
		}
	}
	for (staticObjectIndex = g_regionMainObjectSlotEnd;
	     staticObjectIndex <
	     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     staticObjectIndex++) {
		if (g_objectTable[staticObjectIndex].objectType != 0) {
			*checksumPtr ^= Flight_ChecksumBufferRotateXor(
				&g_objectTable[staticObjectIndex], 0x1F);
			checksum = Flight_RotateChecksumLeft(*checksumPtr);
		}
	}

	firstSlot = g_objectSlotRangeByGenus[0].start;
	for (craftIndex = 0; craftIndex < g_craftDataPoolCapacity;
	     craftIndex++) {
		if (g_objectTable[firstSlot + craftIndex].objectType != 0) {
			*checksumPtr ^= Flight_ChecksumBufferRotateXor(
				&g_craftDataPoolBase[craftIndex], 0x412);
			checksum = Flight_RotateChecksumLeft(*checksumPtr);
		}
	}
	firstSlot = g_objectSlotRangeByGenus[6].start;
	for (projectileIndex = 0;
	     projectileIndex < (int)g_projectileObjectSlotsTotal;
	     projectileIndex++) {
		if (g_objectTable[firstSlot + projectileIndex].objectType !=
		    0) {
			*checksumPtr ^= Flight_ChecksumBufferRotateXor(
				&g_projectileGuidanceStates[projectileIndex],
				0xA);
			checksum = Flight_RotateChecksumLeft(*checksumPtr);
		}
	}

	*checksumPtr ^= Flight_ChecksumBufferRotateXor(
		&g_missionElapsedClock, sizeof(g_missionElapsedClock));
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= Flight_ChecksumBufferRotateXor(
		&g_missionCountdownClock, sizeof(g_missionCountdownClock));
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	for (flightGroupIndex = 0;
	     flightGroupIndex < (int16_t)g_missionHeader.numFlightGroups;
	     flightGroupIndex++) {
		*checksumPtr ^= Flight_ChecksumBufferRotateXor(
			&g_missionFgStats[flightGroupIndex], 0x126);
		checksum = Flight_RotateChecksumLeft(*checksumPtr);
	}

	*checksumPtr ^=
		Flight_ChecksumBufferRotateXor(&g_flightMissionState, 0xD30);
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_nextObjectSignature;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= Flight_ChecksumBufferRotateXor(
		&g_flightGlobalCountdownTimers, 0x16);
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= (uint32_t)(int16_t)g_missionFileVersion;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= Flight_ChecksumBufferRotateXor(&g_missionHeader, 0xA2);
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	for (flightGroupIndex = 0;
	     flightGroupIndex < (int16_t)g_missionHeader.numFlightGroups;
	     flightGroupIndex++) {
		*checksumPtr ^= Flight_ChecksumBufferRotateXor(
			&g_missionFlightGroups[flightGroupIndex], 0x562);
		checksum = Flight_RotateChecksumLeft(*checksumPtr);
	}
	*checksumPtr ^= Flight_ChecksumBufferRotateXor(
		g_missionMessages, sizeof(g_missionMessages));
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	for (goalIndex = 0; goalIndex < 10; goalIndex++) {
		*checksumPtr ^= Flight_ChecksumBufferRotateXor(
			g_missionGlobalGoals[goalIndex],
			sizeof(g_missionGlobalGoals[0]));
		checksum = Flight_RotateChecksumLeft(*checksumPtr);
	}

	*checksumPtr ^= g_activeFlightPlayerCount;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_worldStateReservedByte;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_craftDataPoolCapacity;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_mobileObjectCharDataCount;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_projectileObjectSlotsTotal;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_debrisObjectSlotsTotal;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_worldStateDebrisSlotCount;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_regionMainObjectSlotStart;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_activeRegionObjectSlotStart;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_activeRegionCraftObjectSlotEnd;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_mobileObjectCharDataSlotStart;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_mobileObjectCharDataSlotEnd;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_projectileObjectSlotStart;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_projectileObjectSlotEnd;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_debrisObjectSlotStart;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_debrisObjectSlotEnd;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_explosionObjectSlotStart;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_explosionObjectSlotEnd;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_localTransientSlotStart;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_localDebrisSlotEnd;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_regionMainObjectSlotEnd;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_regionStaticObjectSlotCount;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= Flight_ChecksumBufferRotateXor(g_planTable, 0x5500);
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_planCount;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^=
		Flight_ChecksumBufferRotateXor(g_planOrderData, 0x1FFFF);
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_unusedWorldStateSerializedDword;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= (uint16_t)g_gameRandFeedbackState;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);
	*checksumPtr ^= g_flightConfNewNet;
	checksum = Flight_RotateChecksumLeft(*checksumPtr);

	for (playerIndex = 0; playerIndex < 8; playerIndex++) {
		if (g_players[playerIndex].participationState != 0) {
			*checksumPtr ^= Flight_ChecksumBufferRotateXor(
				&g_players[playerIndex], 0x5BD);
			checksum = Flight_RotateChecksumLeft(*checksumPtr);
		}
	}
	return (int)checksum;
#endif
}

/* Returns a checksum of size bytes at data: each 4-byte word, then a 2-byte and
 * a 1-byte tail, XORed into the sum, which is rotated left 1 bit after each.
 * Returns 0 for size 0. */
// FUNCTION: XVT 0x4630C0
unsigned int Flight_ChecksumBufferRotateXor(const void *data, unsigned int size)
{
	const uint8_t *cursor;
	uint32_t checksum;
	uint32_t *checksumPtr;
#ifdef XVT_MODERN
	uint32_t word;
	uint32_t tailValue;
#endif
	unsigned int wordCount;

	cursor = (const uint8_t *)data;
	checksum = 0;
	checksumPtr = &checksum;
	if (size >= sizeof(uint32_t)) {
		wordCount = size / sizeof(uint32_t);
		size -= wordCount * sizeof(uint32_t);
		do {
#ifdef XVT_MODERN
			memcpy(&word, cursor, sizeof(word));
			*checksumPtr ^= word;
#else
			*checksumPtr ^= *(const uint32_t *)cursor;
#endif
			*checksumPtr =
				(*checksumPtr << 1) | (*checksumPtr >> 31);
			cursor += sizeof(uint32_t);
		} while (--wordCount != 0);
	}

	if (size >= sizeof(uint16_t)) {
#ifdef XVT_MODERN
		tailValue = 0;
		memcpy(&tailValue, cursor, sizeof(uint16_t));
		*checksumPtr ^= tailValue;
#else
		*checksumPtr ^= *(const uint16_t *)cursor;
#endif
		*checksumPtr = (*checksumPtr << 1) | (*checksumPtr >> 31);
		cursor += sizeof(uint16_t);
		size -= sizeof(uint16_t);
		if (size == 1) {
			*checksumPtr ^= *cursor;
			*checksumPtr =
				(*checksumPtr << 1) | (*checksumPtr >> 31);
		}
	} else if (size == 1) {
		*checksumPtr ^= *cursor;
		*checksumPtr = (*checksumPtr << 1) | (*checksumPtr >> 31);
	}

	return checksum;
}

/* One player's part of a simulation step; does nothing once a mission end is
 * pending. With side effects on, a camera whose focus object is gone goes back
 * to the player's craft in the forward view (or loses its focus in the map
 * view), and a map aim target that is gone is cleared. It applies the player's
 * input record (FlightInput_Read with the player's index) and latches it. For
 * the local player with side effects on it handles Shift+L (radio message
 * backup), Alt+B (brightness, at 1 byte per pixel), Alt+D (graphics detail
 * preset), Alt+I (skipping odd scan lines), Alt+M (g_flightAltMToggle), Alt+P
 * (pause, solo only, waiting for a key), Alt+S (system messages), Alt+V
 * (version message) and the screenshot key. A player awaiting a new craft whose
 * craft is gone gets the flight group's next craft, or stops taking part, and
 * the step ends there; outside hyperspace, button bit 0 fires and bit 1 picks a
 * target on a tap. Then the action keys (Flight_ProcessPlayerActions), or the
 * chat input while chatting, and the flight controls while the player takes
 * part. Only the original build calls this; its modern arm hands the work to
 * XvtFlightSim_UpdatePlayerStep. */
// FUNCTION: XVT 0x47A5E0
void Flight_UpdatePlayerStep(int playerIdx)
{
#ifdef XVT_MODERN
	XvtFlightSim_UpdatePlayerStep(playerIdx);
#else
	enum {
		PALETTED_BYTES_PER_PIXEL = 1,
		BRIGHTNESS_STEP_Q8 = 0x40,
		BRIGHTNESS_MIN_Q8 = 0x100,
		BRIGHTNESS_LIMIT_Q8 = 0x300,
		GRAPHICS_DETAIL_PRESET_COUNT = 4,
		GRAPHICS_DETAIL_MESSAGE_BASE = 102,
		FIRE_MODIFIER_MASK = 0xD,
		FIRE_MODIFIER = 1,
		TARGET_MODIFIER_MASK = 0xE,
		TARGET_MODIFIER = 2,
		TARGET_TAP_MAX_TICKS = 59,
		FLIGHT_INPUT_WAIT_FOR_ANY_KEY = -2,
	};

	int objectIndex;
	uint16_t savedKeyMods;
	uint16_t *keyModsHoldTimer;
	int16_t newTargetObjectIndex;

	if (g_flightMissionState.missionEndPending == 1) {
		return;
	}

	if (g_flightSimSideEffectsSuppressed == 0) {
		if (g_players[playerIdx].viewState.cameraFocusObjIdx !=
			    UINT16_MAX &&
		    g_objectTable[g_players[playerIdx]
					  .viewState.cameraFocusObjIdx]
				    .objectType == 0) {
			if (g_players[playerIdx].mapCameraState != 0) {
				g_players[playerIdx]
					.viewState.cameraFocusObjIdx =
					UINT16_MAX;
			} else {
				g_players[playerIdx].viewState.transitionTimer =
					0;
				g_players[playerIdx]
					.viewState.externalCameraActive = 0;
				g_players[playerIdx]
					.viewState.playerInputBlocked = 0;
				g_players[playerIdx]
					.viewState.cameraFocusObjIdx =
					(uint16_t)g_players[playerIdx]
						.objectIndex;
				Hud_SetHudViewState(HUD_VIEW_FORWARD,
						    playerIdx);
				g_players[playerIdx].viewState.hudAimX = 0;
				g_players[playerIdx].viewState.hudAimY = 0;
			}
		}
		if (g_players[playerIdx].mapCameraState != 0 &&
		    g_players[playerIdx].viewState.aimTargetIdx != UINT16_MAX &&
		    g_objectTable[g_players[playerIdx].viewState.aimTargetIdx]
				    .objectType == 0) {
			g_players[playerIdx].viewState.aimTargetIdx =
				UINT16_MAX;
		}
	}

	FlightInput_Read(playerIdx);
	FlightInput_LatchFlightControls();
	if (playerIdx == g_localPlayer) {
		if (g_flightSimSideEffectsSuppressed == 0) {
			switch (g_currentActionKey) {
			case FLIGHT_KEY_SHIFT_L:
				if (g_radioMessageBackupEnabled != 0) {
					g_radioMessageBackupEnabled = 0;
					msg_writeMessageLogFile();
					msg_emitInFlightMessage(
						IFMSG_402_RADIO_MESSAGE_BACKUP_TURNED_OFF,
						playerIdx);
				} else {
					g_radioMessageBackupEnabled = 1;
					msg_emitInFlightMessage(
						IFMSG_403_RADIO_MESSAGE_BACKUP_TURNED_ON,
						playerIdx);
				}
				break;
			case FLIGHT_KEY_ALT_B:
				if (g_flightBytesPerPixel ==
				    PALETTED_BYTES_PER_PIXEL) {
					g_flightBrightnessScaleQ8 +=
						BRIGHTNESS_STEP_Q8;
					if (g_flightBrightnessScaleQ8 ==
					    BRIGHTNESS_LIMIT_Q8) {
						g_flightBrightnessScaleQ8 =
							BRIGHTNESS_MIN_Q8;
					}
					g_flightResetPaletteFn();
					g_msgArgTable[0] =
						(uint16_t)(((unsigned int)(g_flightBrightnessScaleQ8 -
									   BRIGHTNESS_MIN_Q8) >>
							    6) +
							   1);
					msg_emitInFlightMessage(
						IFMSG_287_BRIGHTNESS_SET_TO_LEVEL_ARG,
						playerIdx);
					fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK,
						       -1, playerIdx);
				}
				break;
			case FLIGHT_KEY_ALT_D:
				++g_flightGraphicsDetailPreset;
				if (g_flightGraphicsDetailPreset >=
				    GRAPHICS_DETAIL_PRESET_COUNT) {
					g_flightGraphicsDetailPreset = 0;
				}
				Flight_ApplyGraphicsDetailPreset(
					g_flightGraphicsDetailPreset);
				msg_emitInFlightMessage(
					(InFlightMessageId)(g_flightGraphicsDetailPreset +
							    GRAPHICS_DETAIL_MESSAGE_BASE),
					playerIdx);
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				break;
			case FLIGHT_KEY_ALT_I:
				g_sw3dSkipOddScanlines =
					g_sw3dSkipOddScanlines == 0;
				break;
			case FLIGHT_KEY_ALT_M:
				if (g_flightAltMToggle != 0) {
					g_flightAltMToggle = 0;
				} else {
					g_flightAltMToggle = 1;
				}
				break;
			case FLIGHT_KEY_ALT_P:
				if (g_flightPlayerCount == 1) {
					g_inputTimestamp +=
						Time_ConsumeElapsedTicks();
					fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK,
						       -1, playerIdx);
					msg_emitInFlightMessage(
						IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE,
						playerIdx);
					g_flightDrawToHudLayer = 0;
					FlightSurface_Lock();
					Hud_BlitSoftwareHudTextPanes();
					FlightSurface_Unlock();
					FlightDisplay_Flip();
					g_flightDrawToHudLayer = 1;
					Sound_StopAllInstances();
					while (FlightInput_Read(
						       FLIGHT_INPUT_WAIT_FOR_ANY_KEY) ==
					       0) {
					}
					Time_ConsumeElapsedTicks();
					msg_emitInFlightMessage(
						IFMSG_002_MISSION_RESUMED,
						playerIdx);
					g_actionKey = 0;
					g_flightDisplayRebuildPending = 0;
					Flight_ResetUnusedResumeSlots();
				}
				break;
			case FLIGHT_KEY_ALT_S:
				if (g_systemMessageDisplayEnabled != 0) {
					g_systemMessageDisplayEnabled = 0;
					msg_emitInFlightMessage(
						IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF,
						playerIdx);
				} else {
					g_systemMessageDisplayEnabled = 1;
					msg_emitInFlightMessage(
						IFMSG_401_SYSTEM_MESSAGE_DISPLAYING_TURNED_ON,
						playerIdx);
				}
				break;
			case FLIGHT_KEY_ALT_V:
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				msg_emitInFlightMessage(
					IFMSG_000_X_WING_VS_TIE_FIGHTER_VER_1_10_05_11_97,
					playerIdx);
				break;
			case FLIGHT_KEY_SCREENSHOT:
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				FlightScreenshot_Capture();
				break;
			default:
				break;
			}
		}
	}

	if (g_flightRuntimeStateInitialized > 1 &&
	    g_dormantFlightRegionSessionEarlyReturnFlag != 0) {
		return;
	}

	if (g_players[playerIdx].awaitingNewCraft != 0) {
		if (g_flightSimSideEffectsSuppressed == 0) {
			objectIndex = g_players[playerIdx].objectIndex;
			if (objectIndex != -1 &&
			    g_objectTable[objectIndex].objectType == 0) {
				Mission_ProcessFlightGroupWaveCompletion(
					g_players[playerIdx]
						.boundFlightGroupIdx);
				if (Player_BindToAvailableCraft(
					    playerIdx, UINT32_MAX, 0, 0) != 0) {
					Player_EndFlightParticipation(
						playerIdx);
					Player_EmitRemotePlayerDepartedMessages(
						playerIdx);
				} else if (playerIdx == g_localPlayer) {
					msg_emitLocalPlayerCraftMessage(
						IFMSG_290_PREVIOUS_CRAFT_DESTROYED_NOW_PILOTING_ARG_ARG_ARG);
				}
			}
		}
		return;
	}

	if (g_players[playerIdx].hyperspacePhase == 0) {
		if ((g_flightKeyMods & FIRE_MODIFIER_MASK) == FIRE_MODIFIER &&
		    g_players[playerIdx].viewState.playerInputBlocked == 0 &&
		    g_players[playerIdx].mapCameraState == 0) {
			laser_fireplayerweapon(playerIdx);
		}

		savedKeyMods = g_players[playerIdx].savedKeyMods &
			       TARGET_MODIFIER_MASK;
		if ((g_flightKeyMods & TARGET_MODIFIER_MASK) ==
		    TARGET_MODIFIER) {
			keyModsHoldTimer =
				&g_players[playerIdx].keyModsHoldTimer;
			if (savedKeyMods == TARGET_MODIFIER) {
				*keyModsHoldTimer += g_elapsedTicks;
			} else {
				*keyModsHoldTimer = g_elapsedTicks;
			}
			g_players[playerIdx].savedKeyMods = g_flightKeyMods;
			if (*keyModsHoldTimer < TARGET_TAP_MAX_TICKS) {
				g_flightKeyMods &= (uint16_t)~TARGET_MODIFIER;
			}
		} else {
			if (savedKeyMods == TARGET_MODIFIER &&
			    g_players[playerIdx].keyModsHoldTimer <
				    TARGET_TAP_MAX_TICKS) {
				if (g_players[playerIdx].mapCameraState != 0) {
					newTargetObjectIndex =
						FlightMap_PickObjectNearestScreenCenter(
							playerIdx);
					if (newTargetObjectIndex != -1) {
						Player_SetTarget(
							newTargetObjectIndex,
							playerIdx);
					}
				} else if (
					g_flightMissionState
							.provingGroundsModeActive ==
						0 &&
					g_players[playerIdx]
							.viewState
							.playerInputBlocked ==
						0) {
					newTargetObjectIndex =
						Player_PickTargetInSight(
							playerIdx);
					if (newTargetObjectIndex != -1) {
						Player_SetTarget(
							newTargetObjectIndex,
							playerIdx);
					}
				}
			}
			g_players[playerIdx].savedKeyMods = g_flightKeyMods;
			g_players[playerIdx].keyModsHoldTimer = 0;
		}
	}

	if (g_players[playerIdx].chatRecipientMode ==
	    FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		Flight_ProcessPlayerActions(playerIdx);
	} else {
		FlightChat_HandleInput(playerIdx);
	}
	if (g_players[playerIdx].participationState != 0) {
		Player_UpdateFlightControlsAndCamera(playerIdx);
	}
#endif
}

/* Acts on the action key a player pressed this step (g_currentActionKey). In
 * hyperspace it lets H abort the jump while hyperspacePhase is 1, else advances
 * the jump (FlightObject_UpdatePlayerHyperspaceTransition), and returns. In the
 * proving grounds it drops A, E, R, T, U, Y and F5 to F7. Outside the map view
 * it handles the cockpit keys: throttle (Backspace, the brackets and backslash,
 * plus and minus, the throttle keys, Enter to match the target's speed), the
 * energy and shield transfers and presets, views and the keypad look keys, the
 * beam, countermeasures, hyperspace, craft jumping, overdrive, S-foils, warhead
 * selection, the external camera, and Alt+E, which records the craft as lost
 * and sets it breaking up; in the map view its own keys move the map camera.
 * Then, in both, Space answers the pending prompt (pendingActionId), and it
 * handles the messages, the radio orders to wingmen, the targeting keys, the
 * MFD pages, the map, quitting and the arrow keys. Does not check that the
 * player has a craft before keys such as Backspace use it. */
// FUNCTION: XVT 0x47AC10
void Flight_ProcessPlayerActions(int playerIdx)
{
	const int standardEnergyTransfer = 4;
	const int specialEnergyTransfer = 32;
	const int maxLaserCharge = 127;
	const int maxTransferIterations = 100;
	const int mapDefaultDistance = 512;
	const int mapOverviewDistance = 0x40000;
	const ObjectTypeId specialSlamCraftType = 12;
	int transferChargeUnits;
	unsigned int nearestObjectDistance;
	int energyTransferStep;
	int16_t departureObjectIndex;
	int objectIndex;
	CraftData *craft;
	objectIndex = g_players[playerIdx].objectIndex;
	if (objectIndex != -1) {
		craft = g_objectTable[objectIndex].mobj->pCraft;
	} else {
		craft = NULL;
	}

	if (g_players[playerIdx].hyperspacePhase != 0) {
		if (g_players[playerIdx].hyperspacePhase == 1 &&
		    g_currentActionKey == FLIGHT_KEY_H) {
			g_players[playerIdx].hyperspacePhase = 0;
			msg_emitInFlightMessage(
				IFMSG_107_HYPERSPACE_JUMP_ABORTED, playerIdx);
		} else {
			FlightObject_UpdatePlayerHyperspaceTransition(
				playerIdx);
		}
		return;
	}

	if (g_flightMissionState.provingGroundsModeActive != 0) {
		switch (g_currentActionKey) {
		case FLIGHT_KEY_A:
		case FLIGHT_KEY_E:
		case FLIGHT_KEY_R:
		case FLIGHT_KEY_T:
		case FLIGHT_KEY_U:
		case FLIGHT_KEY_Y:
		case FLIGHT_KEY_F5:
		case FLIGHT_KEY_F6:
		case FLIGHT_KEY_F7:
			g_currentActionKey = FLIGHT_KEY_NONE;
			break;
		default:
			break;
		}
	}

	if (g_players[playerIdx].mapCameraState == 0) {
		switch (g_currentActionKey) {
		case FLIGHT_KEY_BACKSPACE:
			craft->throttleSpeed = UINT16_MAX;
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			msg_emitInFlightMessage(
				IFMSG_122_THROTTLE_SET_TO_FULL_POWER,
				playerIdx);
			break;
		case FLIGHT_KEY_ENTER:
		case FLIGHT_KEY_PAD_ENTER:
			if ((g_mfdActivePage != MFD_PAGE_DAMAGE ||
			     playerIdx != g_localPlayer ||
			     g_flightPlayerCount != 1 ||
			     g_objectTable[objectIndex]
					     .mobj->pCraft
					     ->systemDisplaySlotBySystem
						     [g_damageMfdCurrentSystemId] ==
				     0) &&
			    g_players[playerIdx].currentTargetObjectIdx != -1) {
				MobileObject *targetMobile =
					g_objectTable
						[(uint16_t)g_players[playerIdx]
							 .currentTargetObjectIdx]
							.mobj;
				if (targetMobile == NULL) {
					craft->throttleSpeed = 0;
				} else {
					if (targetMobile->pCraft != NULL) {
						uint16_t targetSpeed =
							targetMobile->speed;
						uint16_t powerMargin =
							6 -
							craft->shieldRechargeLevel -
							craft->beamRechargeLevel -
							craft->laserRechargeLevel;
						uint16_t maxSpeed =
							craft->aiFlight
								.maxSpeedCache;
						if (powerMargin >= 0x8000u) {
							maxSpeed -= MATH2_fraction(
								-powerMargin
									<< 13,
								maxSpeed);
						} else {
							maxSpeed += MATH2_fraction(
								powerMargin
									<< 13,
								maxSpeed);
						}
						if (maxSpeed <= targetSpeed) {
							craft->throttleSpeed =
								UINT16_MAX;
							msg_emitInFlightMessage(
								IFMSG_280_TRYING_TO_MATCH_SPEED_WITH_TARGET_THROTTLE_SET_TO_FULL,
								playerIdx);
						} else {
							craft->throttleSpeed =
								MATH2_ratioQ16(
									targetSpeed,
									maxSpeed);
							msg_emitInFlightMessage(
								IFMSG_279_MATCHING_SPEED_WITH_TARGET,
								playerIdx);
						}
					} else {
						craft->throttleSpeed =
							UINT16_MAX;
					}
				}
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
			}
			break;
		case FLIGHT_KEY_QUOTES: {
			int maxShield;
			int shieldDeficit;
			int16_t remainingCharge = 0;
			int slotIndex;

			if ((craft->systemFlags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					maxShield = Craft_GetObjectMaxShield(
						(uint16_t)objectIndex);
					shieldDeficit = 2 * maxShield -
							craft->shieldEnergy[1] -
							craft->shieldEnergy[0];
					if (shieldDeficit < 0) {
						shieldDeficit = 0;
					}
					if (shieldDeficit != 0) {
						energyTransferStep =
							GetModelIndexFromType(
								g_objectTable
									[g_players[playerIdx]
										 .objectIndex]
										.objectType) ==
									GetModelIndexFromType(
										specialSlamCraftType)
								? specialEnergyTransfer
								: standardEnergyTransfer;
						for (slotIndex = 0;
						     slotIndex <
						     craft->laserSlotCount;
						     ++slotIndex) {
							if (craft
								    ->weaponSlots
									    [slotIndex]
								    .laserCharge >
							    0) {
								remainingCharge +=
									craft->weaponSlots
										[slotIndex]
											.laserCharge;
							}
						}
						if (remainingCharge != 0) {
							slotIndex = 0;
							do {
								int charge =
									craft->weaponSlots
										[slotIndex]
											.laserCharge;
								if (charge >
								    0) {
									--remainingCharge;
									craft->weaponSlots
										[slotIndex]
											.laserCharge =
										(int8_t)(charge -
											 1);
									if (craft->shieldEnergy
										    [0] <
									    maxShield) {
										int amount =
											craft->shieldEnergy[1] <
													maxShield
												? energyTransferStep /
													  2
												: energyTransferStep;
										craft->shieldEnergy
											[0] +=
											amount;
									}
									if (craft->shieldEnergy
										    [1] <
									    maxShield) {
										int amount =
											craft->shieldEnergy[0] <
													maxShield
												? energyTransferStep /
													  2
												: energyTransferStep;
										craft->shieldEnergy
											[1] +=
											amount;
									}
									if (craft->shieldEnergy
											    [0] >=
										    maxShield &&
									    craft->shieldEnergy
											    [1] >=
										    maxShield) {
										break;
									}
								}
								++slotIndex;
								if (slotIndex >=
								    craft->laserSlotCount) {
									slotIndex =
										0;
								}
							} while (
								remainingCharge !=
								0);
							fsfx_PlaySound(
								FLIGHT_SOUND_CONFIRM_BEEP,
								-1, playerIdx);
							msg_emitInFlightMessage(
								IFMSG_360_TRANSFERRING_ALL_LASER_ENERGY_TO_SHIELDS,
								playerIdx);
						} else {
							fsfx_PlaySound(
								FLIGHT_SOUND_SMALL_CLICK,
								-1, playerIdx);
						}

						switch (craft->shieldDistribMode) {
						case SHIELD_DISTRIBUTION_FULLY_FORWARD:
							craft->shieldEnergy
								[0] +=
								energyTransferStep;
							break;
						case SHIELD_DISTRIBUTION_FULLY_AFT:
							craft->shieldEnergy
								[1] +=
								energyTransferStep;
							break;
						default:
							craft->shieldEnergy
								[0] +=
								energyTransferStep /
								2;
							craft->shieldEnergy
								[1] +=
								energyTransferStep /
								2;
							break;
						}
					} else {
						fsfx_PlaySound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, playerIdx);
					}
				} else {
					g_msgArgTable[0] = 98;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
				}
			} else {
				msg_emitInFlightMessage(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					playerIdx);
			}
			break;
		}
		case FLIGHT_KEY_APOSTROPHE:
		case FLIGHT_KEY_SHIFT_F10: {
			int maxShield;
			int missingShieldEnergy;
			int slotIndex = 0;
			int16_t iteration = 0;

			if ((craft->systemFlags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					maxShield =
						2 *
						g_modelDefs
							[GetModelIndexFromType(
								 g_objectTable
									 [g_players[playerIdx]
										  .objectIndex]
										 .objectType)]
								.shieldStrength;
					missingShieldEnergy =
						2 * maxShield -
						craft->shieldEnergy[1] -
						craft->shieldEnergy[0];
					if (missingShieldEnergy < 0) {
						missingShieldEnergy = 0;
					}
					if (missingShieldEnergy > 800) {
						missingShieldEnergy = 800;
					}
					if (GetModelIndexFromType(
						    g_objectTable
							    [g_players[playerIdx]
								     .objectIndex]
								    .objectType) ==
					    GetModelIndexFromType(
						    specialSlamCraftType)) {
						energyTransferStep =
							specialEnergyTransfer;
						transferChargeUnits =
							missingShieldEnergy /
							energyTransferStep;
					} else {
						energyTransferStep =
							standardEnergyTransfer;
						transferChargeUnits =
							missingShieldEnergy /
							energyTransferStep;
					}
					if (transferChargeUnits != 0) {

						for (iteration = 0;
						     transferChargeUnits > 0;
						     ++iteration) {
							if (iteration >=
							    maxTransferIterations) {
								break;
							}
							if (craft
								    ->weaponSlots
									    [slotIndex]
								    .laserCharge >
							    0) {
								--craft->weaponSlots
									  [slotIndex]
										  .laserCharge;
								--transferChargeUnits;
								if (craft->shieldDistribMode ==
								    SHIELD_DISTRIBUTION_FULLY_FORWARD) {
									if (craft->shieldEnergy
										    [0] <
									    maxShield) {
										craft->shieldEnergy
											[0] +=
											energyTransferStep;
									} else if (
										craft->shieldEnergy
											[1] <
										maxShield) {
										craft->shieldEnergy
											[1] +=
											energyTransferStep;
									}
								} else if (
									craft->shieldDistribMode ==
									SHIELD_DISTRIBUTION_FULLY_AFT) {
									if (craft->shieldEnergy
										    [1] <
									    maxShield) {
										craft->shieldEnergy
											[1] +=
											energyTransferStep;
									} else if (
										craft->shieldEnergy
											[0] <
										maxShield) {
										craft->shieldEnergy
											[0] +=
											energyTransferStep;
									}
								} else {
									craft->shieldEnergy
										[0] +=
										energyTransferStep /
										2;
									craft->shieldEnergy
										[1] +=
										energyTransferStep /
										2;
								}
							}
							++slotIndex;
							if (slotIndex >=
							    craft->laserSlotCount) {
								slotIndex = 0;
							}
						}

						if (craft->shieldEnergy[0] >
						    maxShield) {
							craft->shieldEnergy[0] =
								maxShield;
						}
						if (craft->shieldEnergy[1] >
						    maxShield) {
							craft->shieldEnergy[1] =
								maxShield;
						}
						fsfx_PlaySound(
							FLIGHT_SOUND_CONFIRM_BEEP,
							-1, playerIdx);
						msg_emitInFlightMessage(
							IFMSG_132_TRANSFERRING_PARTIAL_POWER_FROM_CANNONS_TO_SHIELDS,
							playerIdx);
					} else {
						fsfx_PlaySound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, playerIdx);
					}
				} else {
					g_msgArgTable[0] = 98;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
				}
			} else {
				msg_emitInFlightMessage(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					playerIdx);
			}
			break;
		}
		case FLIGHT_KEY_SHIFT_9:
		case FLIGHT_KEY_SHIFT_0: {
			int presetIndex =
				g_currentActionKey != FLIGHT_KEY_SHIFT_9;
			g_players[playerIdx].throttlePreset[presetIndex] =
				(int16_t)craft->throttleSpeed;
			g_players[playerIdx].laserPreset[presetIndex] =
				craft->laserRechargeLevel;
			g_players[playerIdx].shieldPreset[presetIndex] =
				craft->shieldRechargeLevel;
			g_players[playerIdx].beamPreset[presetIndex] =
				craft->beamRechargeLevel;
			msg_emitInFlightMessage(
				IFMSG_123_CONFIGURATION_SAVED_TO_PRESET,
				playerIdx);
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			break;
		}
		case FLIGHT_KEY_PLUS:
		case FLIGHT_KEY_EQUAL:
		case FLIGHT_KEY_PAD_PLUS:
			FlightPlayer_IncreaseThrottleSpeed(2048, playerIdx);
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			break;
		case FLIGHT_KEY_MINUS:
		case FLIGHT_KEY_PAD_MINUS:
			FlightPlayer_DecreaseThrottleSpeed(2048, playerIdx);
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			break;
		case FLIGHT_KEY_PERIOD:
		case FLIGHT_KEY_PAD_DOT:
			if (g_flightSimSideEffectsSuppressed == 0) {
				if (g_players[playerIdx]
						    .viewState
						    .externalCameraActive ==
					    0 &&
				    g_players[playerIdx]
						    .viewState
						    .cameraFocusObjIdx ==
					    g_players[playerIdx].objectIndex) {
					if (g_players[playerIdx]
						    .viewState.hudStateLive !=
					    HUD_VIEW_HUD_ONLY) {
						g_players[playerIdx]
							.savedHudViewState =
							HUD_VIEW_HUD_ONLY;
						Hud_SetHudViewState(
							HUD_VIEW_HUD_ONLY,
							playerIdx);
					} else {
						g_players[playerIdx]
							.savedHudViewState =
							HUD_VIEW_FORWARD;
						Hud_SetHudViewState(
							HUD_VIEW_FORWARD,
							playerIdx);
					}
				}
				g_players[playerIdx].viewState.hudAimX = 0;
				g_players[playerIdx].viewState.hudAimY = 0;
			}
			break;
		case FLIGHT_KEY_0:
		case FLIGHT_KEY_9: {
			int presetIndex = g_currentActionKey != FLIGHT_KEY_9;
			craft->throttleSpeed =
				(uint16_t)g_players[playerIdx]
					.throttlePreset[presetIndex];
			craft->laserRechargeLevel =
				g_players[playerIdx].laserPreset[presetIndex];
			craft->shieldRechargeLevel =
				g_players[playerIdx].shieldPreset[presetIndex];
			craft->beamRechargeLevel =
				g_players[playerIdx].beamPreset[presetIndex];
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			break;
		}
		case FLIGHT_KEY_SEMICOLON:
		case FLIGHT_KEY_SHIFT_F9: {
			int16_t emptyChargeUnits = 0;
			int16_t shieldEnergyToRemove;
			int removedShieldEnergy;
			int slotIndex;
			int16_t iteration;

			if ((craft->systemFlags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				for (slotIndex = 0;
				     slotIndex < craft->laserSlotCount;
				     ++slotIndex) {
					emptyChargeUnits +=
						maxLaserCharge -
						craft->weaponSlots[slotIndex]
							.laserCharge;
				}
				energyTransferStep =
					GetModelIndexFromType(
						g_objectTable[objectIndex]
							.objectType) ==
							GetModelIndexFromType(
								specialSlamCraftType)
						? specialEnergyTransfer
						: standardEnergyTransfer;
				if (emptyChargeUnits > maxTransferIterations) {
					emptyChargeUnits =
						maxTransferIterations;
				}
				shieldEnergyToRemove =
					energyTransferStep * emptyChargeUnits;

				removedShieldEnergy = shieldEnergyToRemove;
				if (craft->shieldDistribMode ==
				    SHIELD_DISTRIBUTION_FULLY_FORWARD) {
					if (removedShieldEnergy >
					    craft->shieldEnergy[0]) {
						removedShieldEnergy =
							craft->shieldEnergy[0];
					}
					craft->shieldEnergy[0] -=
						removedShieldEnergy;
				} else if (craft->shieldDistribMode ==
					   SHIELD_DISTRIBUTION_FULLY_AFT) {
					if (removedShieldEnergy >
					    craft->shieldEnergy[1]) {
						removedShieldEnergy =
							craft->shieldEnergy[1];
					}
					craft->shieldEnergy[1] -=
						removedShieldEnergy;
				} else {
					int frontRemoved =
						shieldEnergyToRemove >> 1;
					int rearRemoved =
						shieldEnergyToRemove >> 1;
					if (frontRemoved >
					    craft->shieldEnergy[0]) {
						frontRemoved =
							craft->shieldEnergy[0];
					}
					craft->shieldEnergy[0] -= frontRemoved;
					if (shieldEnergyToRemove >
					    craft->shieldEnergy[1]) {
						rearRemoved =
							craft->shieldEnergy[1];
					}
					craft->shieldEnergy[1] -= rearRemoved;
					removedShieldEnergy =
						frontRemoved + rearRemoved;
				}

				transferChargeUnits = removedShieldEnergy /
						      energyTransferStep;
				if (transferChargeUnits != 0) {
					slotIndex = 0;
					for (iteration = 0;
					     transferChargeUnits > 0;
					     ++iteration) {
						if (iteration >=
						    maxTransferIterations) {
							break;
						}
						if (craft
							    ->weaponSlots
								    [slotIndex]
							    .laserCharge <
						    maxLaserCharge) {
							++craft
								  ->weaponSlots
									  [slotIndex]
								  .laserCharge;
						}
						--transferChargeUnits;
						++slotIndex;
						if (slotIndex >=
						    craft->laserSlotCount) {
							slotIndex = 0;
						}
					}
					fsfx_PlaySound(
						FLIGHT_SOUND_CONFIRM_BEEP, -1,
						playerIdx);
					msg_emitInFlightMessage(
						IFMSG_131_TRANSFERRING_PARTIAL_POWER_FROM_SHIELDS_TO_CANNON_SYSTEM,
						playerIdx);
				} else {
					fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK,
						       -1, playerIdx);
				}
			} else {
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				msg_emitInFlightMessage(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					playerIdx);
			}
			break;
		}
		case FLIGHT_KEY_SHIFT_B:
			if ((craft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) != 0) {
				if (g_players[playerIdx]
						    .currentTargetObjectIdx !=
					    -1 &&
				    g_players[playerIdx]
						    .currentTargetObjectIdx <
					    g_activeRegionCraftObjectSlotEnd) {
					int targetIndex =
						(uint16_t)g_players[playerIdx]
							.currentTargetObjectIdx;
					AiController *controller;
					g_curCraft = g_objectTable[targetIndex]
							     .mobj->pCraft;
					controller = &g_curCraft->aiController;
					pai_setupcraftcontext(
						(uint16_t)targetIndex);
					if ((strcmp(g_planTable
							    [controller
								     ->pendingPlanId]
								    .name,
						    "boardtogivepln") == 0 ||
					     strcmp(g_planTable
							    [controller
								     ->pendingPlanId]
								    .name,
						    "board3pln") == 0) &&
					    pai_CurrentOrderTargetsMatchObject(
						    (uint16_t)g_players[playerIdx]
							    .objectIndex) !=
						    0) {
						controller->candidateTargetIdx =
							(uint16_t)g_players
								[playerIdx]
									.objectIndex;
						msg_radioMessage(
							(uint16_t)targetIndex,
							(uint8_t *)g_curCraft,
							0x102, 0, 0);
						fsfx_SpeakTacticalOfficerEvent(
							TACTICAL_VOICE_STATUS,
							TACTICAL_MSG_RESUPPLIES_ON_THE_WAY,
							targetIndex,
							UINT16_MAX);
					} else {
						g_msgSenderIff =
							g_players[playerIdx]
								.iff;
						msg_emitInFlightMessage(
							IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
							playerIdx);
					}
				}
			} else {
				g_msgArgTable[0] = 101;
				g_msgArgTable[1] = 87;
				msg_emitInFlightMessage(
					IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
			}
			break;
		case FLIGHT_KEY_SHIFT_C:
			if ((craft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) != 0) {
				int16_t otherPlayerIdx;
				int attackerIndex = Player_FindAttackerOfTarget(
					(uint16_t)objectIndex,
					(int16_t)objectIndex);
				if (attackerIndex != -1) {
					Player_IssueAiWingmanTargetOrder(
						(uint16_t)attackerIndex, 0x96,
						6, playerIdx);
				}
				for (otherPlayerIdx = 0; otherPlayerIdx < 8;
				     ++otherPlayerIdx) {
					int otherObjectIndex;
					int hostile;
					CraftData *otherCraft;
					if (otherPlayerIdx == playerIdx ||
					    g_players[otherPlayerIdx]
							    .participationState !=
						    1) {
						continue;
					}
					otherObjectIndex =
						g_players[otherPlayerIdx]
							.objectIndex;
					if (otherObjectIndex == attackerIndex ||
					    otherObjectIndex == -1) {
						continue;
					}
					hostile =
						g_players[otherPlayerIdx]
								.team !=
							g_players[playerIdx]
								.team &&
						g_missionTeams[(uint16_t)g_players
								       [otherPlayerIdx]
									       .team]
								.allies[(uint16_t)g_players
										[playerIdx]
											.team] ==
							0;
					if (hostile ||
					    g_players[otherPlayerIdx]
							    .pendingActionId !=
						    0) {
						continue;
					}
					otherCraft =
						g_objectTable[otherObjectIndex]
							.mobj->pCraft;
					if (otherCraft == NULL ||
					    (otherCraft->workingSubsystems &
					     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) ==
						    0) {
						continue;
					}
					g_players[otherPlayerIdx]
						.pendingActionId = 5;
					g_players[otherPlayerIdx]
						.pendingActionIssuerPlayerIdx =
						(uint16_t)playerIdx;
					g_players[otherPlayerIdx]
						.pendingActionParam =
						(int16_t)(attackerIndex != -1
								  ? attackerIndex
								  : g_players[playerIdx]
									    .objectIndex);
					g_players[otherPlayerIdx]
						.pendingActionTimer = 1416;
					if (otherPlayerIdx == g_localPlayer) {
						fsfx_PlaySound(
							FLIGHT_SOUND_INCOMING_ORDER,
							-1, g_localPlayer);
						msg_addMessagePtr(
							0,
							NetSession_GetPlayerName(
								playerIdx));
						if (attackerIndex != -1) {
							msg_emitInFlightMessage(
								IFMSG_277_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ATTACKER,
								g_localPlayer);
						} else {
							msg_emitInFlightMessage(
								IFMSG_278_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ME,
								g_localPlayer);
						}
					}
				}
			} else {
				g_msgArgTable[0] = 101;
				g_msgArgTable[1] = 87;
				msg_emitInFlightMessage(
					IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
			}
			break;
		case FLIGHT_KEY_LEFT_BRACKET:
			craft->throttleSpeed = 21845;
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			msg_emitInFlightMessage(
				IFMSG_120_THROTTLE_SET_TO_1_3_POWER, playerIdx);
			break;
		case FLIGHT_KEY_BACKSLASH:
			craft->throttleSpeed = 0;
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			msg_emitInFlightMessage(
				IFMSG_119_THROTTLE_SET_TO_NO_POWER, playerIdx);
			break;
		case FLIGHT_KEY_RIGHT_BRACKET:
			craft->throttleSpeed = (uint16_t)-21846;
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
			msg_emitInFlightMessage(
				IFMSG_121_THROTTLE_SET_TO_2_3_POWER, playerIdx);
			break;
		case FLIGHT_KEY_B:
			if ((craft->systemFlags &
			     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
					if (craft->beamActive != 0) {
						craft->beamActive = 0;
						craft->beamOutput = 0;
						fsfx_PlaySound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, playerIdx);
						msg_emitInFlightMessage(
							(InFlightMessageId)(craft->beamTypeId +
									    242),
							playerIdx);
					} else if (craft->beamCharge != 0) {
						craft->beamActive = 1;
						craft->beamOutput = -1;
						if (g_players[playerIdx]
								    .currentTargetObjectIdx !=
							    -1 ||
						    craft->beamTypeId ==
							    BEAM_TYPE_DECOY) {
							msg_emitInFlightMessage(
								(InFlightMessageId)(craft->beamTypeId +
										    236),
								playerIdx);
						} else {
							msg_emitInFlightMessage(
								(InFlightMessageId)(craft->beamTypeId +
										    248),
								playerIdx);
						}

						switch (craft->beamTypeId) {
						case BEAM_TYPE_TRACTOR:
							fsfx_PlaySound(
								FLIGHT_SOUND_TRACTOR_FIRE,
								-1, playerIdx);
							break;
						case BEAM_TYPE_JAMMING:
							fsfx_PlaySound(
								FLIGHT_SOUND_JAMMING_FIRE,
								-1, playerIdx);
							break;
						case BEAM_TYPE_DECOY:
							fsfx_PlaySound(
								FLIGHT_SOUND_DECOY_FIRE,
								-1, playerIdx);
							break;
						case BEAM_TYPE_ENERGY_TRANSFER:
							fsfx_PlaySound(
								FLIGHT_SOUND_ENERGY_TRANSFER_FIRE,
								-1, playerIdx);
							break;
						default:
							break;
						}
					} else {
						fsfx_PlaySound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, playerIdx);
						msg_emitInFlightMessage(
							IFMSG_255_NO_ENERGY_FOR_BEAM_TO_ACTIVATE,
							playerIdx);
					}
				} else {
					fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK,
						       -1, playerIdx);
					g_msgArgTable[0] = 95;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
				}
			} else {
				msg_emitInFlightMessage(
					IFMSG_227_YOUR_CRAFT_DOES_NOT_HAVE_A_BEAM_SYSTEM,
					playerIdx);
			}
			break;
		case FLIGHT_KEY_C:
			if ((craft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) != 0) {
				if (craft->cmFireCooldownTimer == 0) {
					if (craft->cmTypeId !=
					    COUNTERMEASURE_TYPE_NONE) {
						if (craft->cmAmmoCount != 0) {
							if (craft->cmTypeId ==
							    COUNTERMEASURE_TYPE_CHAFF) {
								craft->chaffActiveSeconds +=
									10;
								msg_emitInFlightMessage(
									IFMSG_367_CHAFF_BURST_TRIGGERED,
									playerIdx);
								if (g_missionFlightGroups
										    [g_objectTable
											     [g_players[playerIdx]
												      .objectIndex]
												     .flightGroupIdx]
											    .fg
											    .status1 !=
									    21 &&
								    g_missionFlightGroups
										    [g_objectTable
											     [g_players[playerIdx]
												      .objectIndex]
												     .flightGroupIdx]
											    .fg
											    .status2 !=
									    21) {
									--craft->cmAmmoCount;
								}
								fsfx_PlaySound(
									FLIGHT_SOUND_CHAFF_TRIGGER,
									-1,
									playerIdx);
							} else if (
								laser_createcountermeasureprojectile(
									objectIndex,
									COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) !=
								-1) {
								msg_emitInFlightMessage(
									IFMSG_370_FLARE_FIRED,
									playerIdx);
								fsfx_PlaySound(
									FLIGHT_SOUND_COUNTERMEASURE_FLARE,
									-1,
									playerIdx);
							}
						} else {
							msg_emitInFlightMessage(
								(InFlightMessageId)(craft->cmTypeId +
										    362),
								playerIdx);
							fsfx_PlaySound(
								FLIGHT_SOUND_SMALL_CLICK,
								-1, playerIdx);
						}
					} else {
						msg_emitInFlightMessage(
							IFMSG_362_NO_COUNTERMEASURES_LOADED,
							playerIdx);
						fsfx_PlaySound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, playerIdx);
					}
				}
			} else {
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				g_msgArgTable[0] = 97;
				g_msgArgTable[1] = 89;
				msg_emitInFlightMessage(
					IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
			}
			break;
		case FLIGHT_KEY_H:
			Player_HandleHyperspaceCommand(craft, playerIdx);
			break;
		case FLIGHT_KEY_J:
			if (g_flightSimSideEffectsSuppressed == 0) {
				if (g_flightMissionState.craftJumpingEnabled !=
				    0) {
					if (Player_UnbindFromCurrentCraft(
						    playerIdx, 1, 1) != 0) {
						Player_BindToAvailableCraft(
							playerIdx,
							(uint16_t)objectIndex,
							0, 0);
						if (playerIdx ==
						    g_localPlayer) {
							msg_emitLocalPlayerCraftMessage(
								IFMSG_289_YOU_ARE_NOW_PILOTING_ARG_ARG_ARG);
						}
					} else {
						msg_emitInFlightMessage(
							IFMSG_293_NO_OTHER_CRAFT_TO_PILOT,
							playerIdx);
					}
				} else {
					msg_emitInFlightMessage(
						IFMSG_294_CRAFT_JUMPING_NOT_ENABLED_FOR_THIS_MISSION,
						playerIdx);
				}
			}
			break;
		case FLIGHT_KEY_N: {
			int16_t haveLaserEnergy = 0;
			unsigned int slotIndex;
			if (GetModelIndexFromType(
				    g_objectTable[objectIndex].objectType) ==
			    GetModelIndexFromType(specialSlamCraftType)) {
				craft->engineOverdriveOff =
					(uint16_t)~craft->engineOverdriveOff;
				if (craft->engineOverdriveOff == 0) {
					for (slotIndex = 0;
					     slotIndex < craft->laserSlotCount;
					     ++slotIndex) {
						if (craft
							    ->weaponSlots
								    [slotIndex]
							    .laserCharge > 0) {
							haveLaserEnergy = 1;
						}
					}
					if (haveLaserEnergy != 0) {
						msg_emitInFlightMessage(
							IFMSG_284_ENGINE_OVERDRIVE_BOOSTERS_ENGAGED,
							playerIdx);
						fsfx_PlaySound(
							FLIGHT_SOUND_POWER_UP,
							-1, playerIdx);
					} else {
						craft->engineOverdriveOff =
							UINT16_MAX;
						msg_emitInFlightMessage(
							IFMSG_286_ENGINE_OVERDRIVE_BOOSTERS_CANNOT_BE_ENGAGED,
							playerIdx);
						fsfx_PlaySound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, playerIdx);
					}
				} else {
					msg_emitInFlightMessage(
						IFMSG_285_ENGINE_OVERDRIVE_BOOSTERS_DISENGAGED,
						playerIdx);
					fsfx_PlaySound(FLIGHT_SOUND_POWER_DOWN,
						       -1, playerIdx);
				}
			} else {
				msg_emitInFlightMessage(
					IFMSG_229_YOUR_CRAFT_DOES_NOT_HAVE_A_SLAM_SYSTEM,
					playerIdx);
			}
			break;
		}
		case FLIGHT_KEY_S:
			if ((craft->systemFlags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					++craft->shieldDistribMode;
					if (craft->shieldDistribMode >
					    SHIELD_DISTRIBUTION_FULLY_AFT) {
						craft->shieldDistribMode =
							SHIELD_DISTRIBUTION_FULLY_FORWARD;
						Player_TransferShieldBankEnergy(
							0, 1, playerIdx);
					} else if (
						craft->shieldDistribMode ==
						SHIELD_DISTRIBUTION_FULLY_AFT) {
						Player_TransferShieldBankEnergy(
							1, 0, playerIdx);
					} else {
						uint16_t frontPercent = MATH2_longratioQ16(
							g_modelDefs
								[GetModelIndexFromType(
									 g_objectTable
										 [g_players[playerIdx]
											  .objectIndex]
											 .objectType)]
									.shieldStrength,
							Craft_GetObjectMaxShield(
								(uint16_t)g_players[playerIdx]
									.objectIndex));
						int16_t totalShield =
							craft->shieldEnergy[1] +
							craft->shieldEnergy[0];
						if (totalShield > 0) {
							craft->shieldEnergy
								[0] = MATH2_fraction(
								totalShield,
								frontPercent);
							craft->shieldEnergy[1] =
								totalShield -
								craft->shieldEnergy
									[0];
						}
					}
					msg_emitInFlightMessage(
						(InFlightMessageId)(craft->shieldDistribMode +
								    68),
						playerIdx);
					fsfx_PlaySound(
						FLIGHT_SOUND_CONFIRM_BEEP, -1,
						playerIdx);
				} else {
					fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK,
						       -1, playerIdx);
					g_msgArgTable[0] = 98;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
				}
			} else {
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				msg_emitInFlightMessage(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					playerIdx);
			}
			break;
		case FLIGHT_KEY_V:
			if (g_objectTable[g_players[playerIdx].objectIndex]
					    .objectType ==
				    CRAFT_SPECIES_X_WING ||
			    g_objectTable[g_players[playerIdx].objectIndex]
					    .objectType ==
				    CRAFT_SPECIES_B_WING) {
				g_objectTable[g_players[playerIdx].objectIndex]
					.mobj->pCraft->sFoilState ^= 2;
				g_objectTable[g_players[playerIdx].objectIndex]
					.mobj->pCraft->sFoilState |= 1;
				fsfx_PlaySound(FLIGHT_SOUND_S_FOIL, -1,
					       playerIdx);
				if ((g_objectTable[g_players[playerIdx]
							   .objectIndex]
					     .mobj->pCraft->sFoilState &
				     2) != 0) {
					msg_emitInFlightMessage(
						IFMSG_127_S_FOILS_CLOSING,
						playerIdx);
				} else {
					msg_emitInFlightMessage(
						IFMSG_126_S_FOILS_OPENING,
						playerIdx);
				}
			} else {
				msg_emitInFlightMessage(
					IFMSG_228_YOUR_CRAFT_DOES_NOT_HAVE_S_FOILS,
					playerIdx);
			}
			break;
		case FLIGHT_KEY_W: {
			uint8_t selection =
				g_players[playerIdx].selectedWeaponBank + 1;
			g_players[playerIdx].selectedWeaponBank = selection;
			if (g_players[playerIdx].selectedWeaponMode == 0) {
				if (craft->cannonGroupCount <= selection) {
					if (craft->warheadLauncherCount != 0) {
						int firstSlot =
							g_modelDefs[craft->modelIndex]
								.warheadLauncherFirstSlot
									[0];
						if (craft->weaponSlots[firstSlot]
								    .ammoCount +
							    craft->weaponSlots
								    [firstSlot +
								     1]
									    .ammoCount !=
						    0) {
							int8_t flags =
								craft->warheadLauncherFlags
									[0];
							g_players[playerIdx]
								.selectedWeaponMode =
								1;
							g_players[playerIdx]
								.missileLockState =
								0;
							craft->warheadLockTicks =
								0;
							if ((flags & 0x7F) !=
							    3) {
								if (flags < 0) {
									if (craft->weaponSlots
										    [firstSlot +
										     1]
											    .ammoCount <
									    craft->weaponSlots[firstSlot]
										    .ammoCount) {
										craft->warheadLauncherFlags
											[0] =
											(int8_t)(flags &
												 0x7F);
									}
								} else {
									if (craft->weaponSlots
										    [firstSlot +
										     1]
											    .ammoCount >
									    craft->weaponSlots[firstSlot]
										    .ammoCount) {
										craft->warheadLauncherFlags
											[0] =
											(int8_t)(flags |
												 0x80);
									}
								}
							}
						}
					}
					g_players[playerIdx]
						.selectedWeaponBank = 0;
				}
			} else if (craft->warheadLauncherCount <= selection) {
				if (craft->cannonGroupCount != 0) {
					g_players[playerIdx]
						.selectedWeaponMode = 0;
				}
				g_players[playerIdx].selectedWeaponBank = 0;
			}

			g_msgArgTable[1] = 87;
			if (g_players[playerIdx].selectedWeaponMode != 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) !=
				    0) {
					int warheadKind = ObjectType_GetWarheadKindIndex(
						craft->warheadSlotTypeIds
							[g_players[playerIdx]
								 .selectedWeaponBank]);
					msg_emitInFlightMessage(
						(InFlightMessageId)(warheadKind +
								    8),
						playerIdx);
					fsfx_PlaySound(
						FLIGHT_SOUND_SETTING_MEDIUM, -1,
						playerIdx);
				} else {
					g_msgArgTable[0] = 94;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
					fsfx_PlaySound(FLIGHT_SOUND_SETTING_OFF,
						       -1, playerIdx);
				}
			} else if ((craft->workingSubsystems &
				    CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				msg_emitInFlightMessage(
					(InFlightMessageId)(g_players[playerIdx]
								    .selectedWeaponBank +
							    3),
					playerIdx);
				fsfx_PlaySound(
					g_players[playerIdx].selectedWeaponBank !=
							0
						? FLIGHT_SOUND_SETTING_LOW
						: FLIGHT_SOUND_SETTING_VERY_LOW,
					-1, playerIdx);
			} else {
				g_msgArgTable[0] = g_players[playerIdx]
							   .selectedWeaponBank +
						   92;
				msg_emitInFlightMessage(
					IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
				fsfx_PlaySound(FLIGHT_SOUND_SETTING_OFF, -1,
					       playerIdx);
			}
			break;
		}
		case FLIGHT_KEY_X:
			if (g_players[playerIdx].selectedWeaponMode == 0) {
				if (g_modelDefs[GetModelIndexFromType(
							g_objectTable[objectIndex]
								.objectType)]
					    .laserGroupSlotCount
						    [g_players[playerIdx]
							     .selectedWeaponBank] !=
				    1) {
					uint16_t linkMode =
						craft->laserState.linkMode
							[g_players[playerIdx]
								 .selectedWeaponBank] +
						1;
					if (linkMode > 3) {
						linkMode = 1;
					}
					if (g_modelDefs[GetModelIndexFromType(
								g_objectTable
									[g_players[playerIdx]
										 .objectIndex]
										.objectType)]
							    .laserGroupSlotCount
								    [g_players[playerIdx]
									     .selectedWeaponBank] !=
						    4 &&
					    linkMode == 2) {
						linkMode = 3;
					}
					craft->laserState.linkMode
						[g_players[playerIdx]
							 .selectedWeaponBank] =
						(uint8_t)linkMode;
					craft->laserState.nextSlot
						[g_players[playerIdx]
							 .selectedWeaponBank] =
						g_modelDefs[GetModelIndexFromType(
								    g_objectTable
									    [g_players[playerIdx]
										     .objectIndex]
										    .objectType)]
							.laserGroupFirstSlot
								[g_players[playerIdx]
									 .selectedWeaponBank];
					if (playerIdx == g_localPlayer) {
						msg_emitInFlightMessage(
							(InFlightMessageId)(linkMode +
									    4),
							playerIdx);
					}
				}
			} else {
				uint16_t warheadKind;
				craft->warheadLauncherFlags
					[g_players[playerIdx]
						 .selectedWeaponBank] ^= 2;
				warheadKind = ObjectType_GetWarheadKindIndex(
					craft->warheadSlotTypeIds
						[g_players[playerIdx]
							 .selectedWeaponBank]);
				if ((craft->warheadLauncherFlags
					     [g_players[playerIdx]
						      .selectedWeaponBank] &
				     2) != 0) {
					msg_emitInFlightMessage(
						(InFlightMessageId)(warheadKind +
								    28),
						playerIdx);
				} else {
					msg_emitInFlightMessage(
						(InFlightMessageId)(warheadKind +
								    18),
						playerIdx);
				}
			}
			fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
				       playerIdx);
			break;
		case FLIGHT_KEY_Z:
			if (g_flightSimSideEffectsSuppressed == 0) {
				PlayerViewState *view =
					&g_players[playerIdx].viewState;
				if (view->transitionTimer != 0) {
					view->transitionTimer = 0;
					view->cameraFocusObjIdx =
						(uint16_t)g_players[playerIdx]
							.objectIndex;
					view->externalCameraActive = 0;
					view->playerInputBlocked = 0;
					if (playerIdx == g_localPlayer) {
						g_hudCachedTargetObjectIdx = -2;
						g_renderObjectRefFlags = 0;
					}
					Hud_SetHudViewState(
						g_players[playerIdx].savedHudViewState ==
								HUD_VIEW_HUD_ONLY
							? HUD_VIEW_HUD_ONLY
							: HUD_VIEW_FORWARD,
						playerIdx);
					view->hudAimX = 0;
					view->hudAimY = 0;
				} else if (g_players[playerIdx]
						   .currentTargetObjectIdx ==
					   -1) {
					msg_emitInFlightMessage(
						IFMSG_223_NO_CRAFT_TARGETED,
						playerIdx);
				} else {
					if (view->externalCameraActive == 0) {
						view->savedHudStateByte =
							view->hudStateLive;
						view->savedHudAimX =
							view->hudAimX;
						view->savedHudAimY =
							view->hudAimY;
					}
					view->externalCameraActive = 1;
					view->transitionTimer = 1;
					view->cameraFocusObjIdx =
						(uint16_t)g_players[playerIdx]
							.currentTargetObjectIdx;
					if (playerIdx == g_localPlayer) {
						g_renderObjectRefFlags = 1024;
					}
					Player_UpdateHudViewForCameraFocus(
						playerIdx);
				}
			}
			break;
		case FLIGHT_KEY_ALT_E:
			if (g_flightSimSideEffectsSuppressed == 0) {
				if (g_flightMissionState
					    .provingGroundsModeActive != 0) {
					g_flightMissionState.missionEndPending =
						1;
					g_players[playerIdx]
						.participationState = 2;
				} else {
					Player_SaveCraftSettings(playerIdx);
					if (g_players[playerIdx]
							    .hyperspacePhase !=
						    0 ||
					    g_replayViewMode != 0 ||
					    g_players[playerIdx]
							    .mapCameraState !=
						    0) {
						fsfx_PlaySound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, playerIdx);
					} else {
						int sourcePlayerIdx =
							Mission_RecordPlayerCraftLoss(
								objectIndex, 1);
						int tumbleRate =
							(GameRand() & 0x3FFF) +
							0x2000;
						Player_StartPostDestructionState(
							playerIdx, UINT32_MAX,
							sourcePlayerIdx);
						while (tumbleRate >
						       g_modelDefs[craft->modelIndex]
							       .maxTumbleAngle) {
							tumbleRate >>= 1;
						}
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj
								->rollImpulseRate =
							(int16_t)tumbleRate;
						craft->objectKind =
							CRAFT_OBJECT_KIND_BREAKING_UP;
						g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj
								->lifetimeTimer =
							472;
					}
				}
			}
			break;
		case FLIGHT_KEY_ALT_1:
			if (g_players[playerIdx].mapCameraState != 0) {
				Player_SetTarget(
					FlightMap_PickObjectNearestScreenCenter(
						playerIdx),
					playerIdx);
			} else if (g_flightMissionState
						   .provingGroundsModeActive ==
					   0 &&
				   g_players[playerIdx]
						   .viewState
						   .playerInputBlocked == 0) {
				Player_SetTarget(
					Player_PickTargetInSight(playerIdx),
					playerIdx);
			}
			break;
		case FLIGHT_KEY_ALT_2:
			if (g_players[playerIdx].viewState.playerInputBlocked ==
			    0) {
				laser_fireplayerweapon(playerIdx);
			}
			break;
		case FLIGHT_KEY_PAD_0:
			if (g_flightSimSideEffectsSuppressed == 0 &&
			    (g_players[playerIdx].viewState.hudStateLive < 16 ||
			     g_players[playerIdx]
					     .viewState.externalCameraActive !=
				     0)) {
				PlayerViewState *view =
					&g_players[playerIdx].viewState;
				view->hudAimXSnapState ^= 8;
				view->hudAimX = view->hudAimXSnapState << 10;
				if (view->externalCameraActive == 0 &&
				    view->cameraFocusObjIdx ==
					    g_players[playerIdx].objectIndex) {
					Hud_SetHudViewState(view->hudStateLive ^
								    8,
							    playerIdx);
				}
			}
			break;
		case FLIGHT_KEY_PAD_1:
		case FLIGHT_KEY_PAD_2:
		case FLIGHT_KEY_PAD_3:
		case FLIGHT_KEY_PAD_4:
		case FLIGHT_KEY_PAD_6:
		case FLIGHT_KEY_PAD_7:
		case FLIGHT_KEY_PAD_8:
		case FLIGHT_KEY_PAD_9:
			if (g_flightSimSideEffectsSuppressed == 0) {
				int lookIndex =
					g_currentActionKey - FLIGHT_KEY_PAD_1;
				PlayerViewState *view =
					&g_players[playerIdx].viewState;
				if (view->externalCameraActive == 0 &&
				    view->cameraFocusObjIdx ==
					    g_players[playerIdx].objectIndex) {
					int hudState =
						view->hudAimXSnapState +
						g_hudViewStateOffsetByLookAction
							[lookIndex];
					if (hudState == 0) {
						hudState =
							g_players[playerIdx]
								.savedHudViewState;
					}
					Hud_SetHudViewState((uint8_t)hudState,
							    playerIdx);
				}
				view->hudAimX = view->hudAimXSnapState << 10;
				view->hudAimY =
					g_hudAimYByLookAction[lookIndex];
			}
			break;
		case FLIGHT_KEY_PAD_5:
			if (g_flightSimSideEffectsSuppressed == 0) {
				g_players[playerIdx].viewState.hudAimX = 0x4000;
				g_players[playerIdx].viewState.hudAimY = 0;
				if (g_players[playerIdx]
						    .viewState
						    .externalCameraActive ==
					    0 &&
				    g_players[playerIdx]
						    .viewState
						    .cameraFocusObjIdx ==
					    g_players[playerIdx].objectIndex) {
					Hud_SetHudViewState(16, playerIdx);
				}
			}
			break;
		case FLIGHT_KEY_F8:
			if ((craft->systemFlags &
			     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
					if (++craft->beamRechargeLevel >=
					    POWER_RECHARGE_LEVEL_COUNT) {
						craft->beamRechargeLevel =
							POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
					}
					msg_emitInFlightMessage(
						(InFlightMessageId)(craft->beamRechargeLevel +
								    81),
						playerIdx);
					fsfx_PlaySound(
						(unsigned int)(craft->beamRechargeLevel +
							       FLIGHT_SOUND_SETTING_OFF),
						-1, playerIdx);
				} else {
					g_msgArgTable[0] = 95;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
					fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK,
						       -1, playerIdx);
				}
			} else {
				msg_emitInFlightMessage(
					IFMSG_227_YOUR_CRAFT_DOES_NOT_HAVE_A_BEAM_SYSTEM,
					playerIdx);
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
			}
			break;
		case FLIGHT_KEY_F9:
			if (++craft->laserRechargeLevel >=
			    POWER_RECHARGE_LEVEL_COUNT) {
				craft->laserRechargeLevel =
					POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
			}
			if (playerIdx == g_localPlayer) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
					msg_emitInFlightMessage(
						(InFlightMessageId)(craft->laserRechargeLevel +
								    71),
						playerIdx);
				} else {
					g_msgArgTable[0] = 92;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
				}
				fsfx_PlaySound(
					(unsigned int)(craft->laserRechargeLevel +
						       FLIGHT_SOUND_SETTING_OFF),
					-1, playerIdx);
			}
			break;
		case FLIGHT_KEY_F10:
			if ((craft->systemFlags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					if (++craft->shieldRechargeLevel >=
					    POWER_RECHARGE_LEVEL_COUNT) {
						craft->shieldRechargeLevel =
							POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
					}
					msg_emitInFlightMessage(
						(InFlightMessageId)(craft->shieldRechargeLevel +
								    76),
						playerIdx);
					fsfx_PlaySound(
						(unsigned int)(craft->shieldRechargeLevel +
							       FLIGHT_SOUND_SETTING_OFF),
						-1, playerIdx);
				} else {
					g_msgArgTable[0] = 98;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
					fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK,
						       -1, playerIdx);
				}
			} else {
				msg_emitInFlightMessage(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					playerIdx);
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
			}
			break;
		case FLIGHT_KEY_THROTTLE_1:
		case FLIGHT_KEY_THROTTLE_2:
		case FLIGHT_KEY_THROTTLE_3:
		case FLIGHT_KEY_THROTTLE_4:
			craft->throttleSpeed =
				(uint16_t)((g_currentActionKey + 6) << 12);
			break;
		case FLIGHT_KEY_THROTTLE_6:
		case FLIGHT_KEY_THROTTLE_7:
		case FLIGHT_KEY_THROTTLE_8:
		case FLIGHT_KEY_THROTTLE_9:
		case FLIGHT_KEY_THROTTLE_10:
			craft->throttleSpeed =
				(uint16_t)((g_currentActionKey + 7) << 12);
			break;
		case FLIGHT_KEY_THROTTLE_11:
		case FLIGHT_KEY_THROTTLE_12:
		case FLIGHT_KEY_THROTTLE_13:
		case FLIGHT_KEY_THROTTLE_14:
			craft->throttleSpeed =
				(uint16_t)((g_currentActionKey + 8) << 12);
			break;
		default:
			break;
		}
	} else {
		switch (g_currentActionKey) {
		case FLIGHT_KEY_C:
			if (g_players[playerIdx].mapCameraState > 1 &&
			    g_players[playerIdx].currentTargetObjectIdx != -1) {
				g_players[playerIdx].viewState.cameraWorldX =
					g_objectTable
						[(uint16_t)g_players[playerIdx]
							 .currentTargetObjectIdx]
							.world_x;
				g_players[playerIdx].viewState.cameraWorldY =
					g_objectTable
						[(uint16_t)g_players[playerIdx]
							 .currentTargetObjectIdx]
							.world_y;
			}
			break;
		case FLIGHT_KEY_Z:
			if (g_players[playerIdx].mapCameraState > 1) {
				if (g_players[playerIdx]
					    .currentTargetObjectIdx != -1) {
					g_players[playerIdx]
						.viewState.cameraWorldX =
						g_objectTable
							[(uint16_t)g_players[playerIdx]
								 .currentTargetObjectIdx]
								.world_x;
					g_players[playerIdx]
						.viewState.cameraWorldY =
						g_objectTable
							[(uint16_t)g_players[playerIdx]
								 .currentTargetObjectIdx]
								.world_y;
					g_players[playerIdx]
						.viewState.cameraWorldZ =
						g_objectTable
							[(uint16_t)g_players[playerIdx]
								 .currentTargetObjectIdx]
								.world_z;
					g_players[playerIdx]
						.viewState.cameraWorldZ +=
						16 *
						g_objectTypeTable
							[g_objectTable
								 [(uint16_t)g_players
									  [playerIdx]
										  .currentTargetObjectIdx]
									 .objectType]
								.maxBoundsExtent;
				}
			} else if (g_players[playerIdx]
					   .viewState.cameraFocusObjIdx !=
				   UINT16_MAX) {
				g_players[playerIdx].viewState.cameraDistance =
					g_objectTypeTable
						[g_objectTable
							 [g_players[playerIdx]
								  .viewState
								  .cameraFocusObjIdx]
								 .objectType]
							.maxBoundsExtent +
					mapDefaultDistance;
			} else if (g_players[playerIdx]
					   .currentTargetObjectIdx != -1) {
				g_players[playerIdx].viewState.cameraWorldX =
					g_objectTable
						[(uint16_t)g_players[playerIdx]
							 .currentTargetObjectIdx]
							.world_x;
				g_players[playerIdx].viewState.cameraWorldY =
					g_objectTable
						[(uint16_t)g_players[playerIdx]
							 .currentTargetObjectIdx]
							.world_y;
				g_players[playerIdx].viewState.cameraWorldZ =
					g_objectTable
						[(uint16_t)g_players[playerIdx]
							 .currentTargetObjectIdx]
							.world_z;
				g_players[playerIdx].viewState.cameraDistance =
					g_objectTypeTable
						[g_objectTable
							 [(uint16_t)g_players[playerIdx]
								  .currentTargetObjectIdx]
								 .objectType]
							.maxBoundsExtent +
					mapDefaultDistance;
				FVIEW_BuildCameraOrient(
					0,
					g_players[playerIdx]
						.viewState.viewPitch,
					g_players[playerIdx].viewState.viewYaw,
					0, 0, 0, NULL);
				g_players[playerIdx].viewState.cameraWorldX -=
					Math_MulQ15(g_players[playerIdx]
							    .viewState
							    .cameraDistance,
						    g_camMatR2_X);
				g_players[playerIdx].viewState.cameraWorldY -=
					Math_MulQ15(g_players[playerIdx]
							    .viewState
							    .cameraDistance,
						    g_camMatR2_Y);
				g_players[playerIdx].viewState.cameraWorldZ -=
					Math_MulQ15(g_players[playerIdx]
							    .viewState
							    .cameraDistance,
						    g_camMatR2_Z);
			}
			break;
		case FLIGHT_KEY_PAD_MINUS:
			g_players[playerIdx].viewState.aimTargetIdx =
				UINT16_MAX;
			break;
		case FLIGHT_KEY_PAD_PLUS:
			if (g_players[playerIdx].currentTargetObjectIdx != -1) {
				g_players[playerIdx].viewState.aimTargetIdx =
					(uint16_t)g_players[playerIdx]
						.currentTargetObjectIdx;
				if (g_players[playerIdx].mapCameraState > 1) {
					g_players[playerIdx].mapCameraState = 1;
					g_players[playerIdx].viewState.hudAimX =
						0;
				}
			}
			break;
		default:
			break;
		}
	}

	switch (g_currentActionKey) {
	case FLIGHT_KEY_TAB:
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
		} else if (g_activeFlightPlayerCount != 1 &&
			   g_players[playerIdx].chatRecipientMode ==
				   FLIGHT_CHAT_RECIPIENT_INACTIVE) {
			g_players[playerIdx].chatRecipientMode =
				FLIGHT_CHAT_RECIPIENT_TEAM;
			g_players[playerIdx].msgLength = 0;
			g_players[playerIdx].msgText[0] = '_';
			g_players[playerIdx].msgText[1] = '\0';
			msg_addMessagePtr(0, g_players[playerIdx].msgText);
			msg_emitInFlightMessage(IFMSG_375_TEAM_MESSAGE_ARG,
						playerIdx);
		}
		return;
	case FLIGHT_KEY_SPACE:
		switch (g_players[playerIdx].pendingActionId) {
		case 0:
			if (g_players[playerIdx].mapCameraState != 0) {
				if (g_players[playerIdx].mapCameraState == 1) {
					if (g_players[playerIdx]
						    .viewState
						    .cameraFocusObjIdx !=
					    UINT16_MAX) {
						g_players[playerIdx]
							.viewState
							.cameraDistance = collide_roughdistance3d(
							g_players[playerIdx]
									.viewState
									.cameraWorldX -
								g_objectTable
									[g_players[playerIdx]
										 .viewState
										 .cameraFocusObjIdx]
										.world_x,
							g_players[playerIdx]
									.viewState
									.cameraWorldY -
								g_objectTable
									[g_players[playerIdx]
										 .viewState
										 .cameraFocusObjIdx]
										.world_y,
							g_players[playerIdx]
									.viewState
									.cameraWorldZ -
								g_objectTable
									[g_players[playerIdx]
										 .viewState
										 .cameraFocusObjIdx]
										.world_z);
						g_players[playerIdx]
							.viewState
							.cameraWorldX =
							g_objectTable
								[g_players[playerIdx]
									 .viewState
									 .cameraFocusObjIdx]
									.world_x;
						g_players[playerIdx]
							.viewState
							.cameraWorldY =
							g_objectTable
								[g_players[playerIdx]
									 .viewState
									 .cameraFocusObjIdx]
									.world_y;
					} else if (g_players[playerIdx]
							   .viewState
							   .aimTargetIdx !=
						   UINT16_MAX) {
						g_players[playerIdx]
							.viewState
							.cameraDistance = collide_roughdistance3d(
							g_players[playerIdx]
									.viewState
									.cameraWorldX -
								g_objectTable
									[g_players[playerIdx]
										 .viewState
										 .aimTargetIdx]
										.world_x,
							g_players[playerIdx]
									.viewState
									.cameraWorldY -
								g_objectTable
									[g_players[playerIdx]
										 .viewState
										 .aimTargetIdx]
										.world_y,
							g_players[playerIdx]
									.viewState
									.cameraWorldZ -
								g_objectTable
									[g_players[playerIdx]
										 .viewState
										 .aimTargetIdx]
										.world_z);
						g_players[playerIdx]
							.viewState
							.cameraWorldX =
							g_objectTable
								[g_players[playerIdx]
									 .viewState
									 .aimTargetIdx]
									.world_x;
						g_players[playerIdx]
							.viewState
							.cameraWorldY =
							g_objectTable
								[g_players[playerIdx]
									 .viewState
									 .aimTargetIdx]
									.world_y;
						g_players[playerIdx]
							.viewState
							.cameraFocusObjIdx =
							g_players[playerIdx]
								.viewState
								.aimTargetIdx;
					} else if (
						g_players[playerIdx]
							.currentTargetObjectIdx !=
						-1) {
						g_players[playerIdx]
							.viewState
							.cameraDistance = collide_roughdistance3d(
							g_players[playerIdx]
									.viewState
									.cameraWorldX -
								g_objectTable
									[(uint16_t)g_players
										 [playerIdx]
											 .currentTargetObjectIdx]
										.world_x,
							g_players[playerIdx]
									.viewState
									.cameraWorldY -
								g_objectTable
									[(uint16_t)g_players
										 [playerIdx]
											 .currentTargetObjectIdx]
										.world_y,
							g_players[playerIdx]
									.viewState
									.cameraWorldZ -
								g_objectTable
									[(uint16_t)g_players
										 [playerIdx]
											 .currentTargetObjectIdx]
										.world_z);
						g_players[playerIdx]
							.viewState
							.cameraWorldX =
							g_objectTable
								[(uint16_t)g_players
									 [playerIdx]
										 .currentTargetObjectIdx]
									.world_x;
						g_players[playerIdx]
							.viewState
							.cameraWorldY =
							g_objectTable
								[(uint16_t)g_players
									 [playerIdx]
										 .currentTargetObjectIdx]
									.world_y;
						g_players[playerIdx]
							.viewState
							.cameraFocusObjIdx =
							(uint16_t)g_players[playerIdx]
								.currentTargetObjectIdx;
					} else {
						g_players[playerIdx]
							.viewState
							.cameraDistance =
							mapOverviewDistance;
						g_players[playerIdx]
							.viewState
							.cameraWorldZ =
							mapOverviewDistance;
					}
					g_players[playerIdx]
						.viewState.aimTargetIdx =
						UINT16_MAX;
				} else if (g_players[playerIdx]
						   .currentTargetObjectIdx !=
					   -1) {
					g_players[playerIdx]
						.viewState.cameraFocusObjIdx =
						(uint16_t)g_players[playerIdx]
							.currentTargetObjectIdx;
					g_players[playerIdx]
						.viewState
						.cameraDistance = collide_roughdistance3d(
						g_players[playerIdx]
								.viewState
								.cameraWorldX -
							g_objectTable
								[(uint16_t)g_players
									 [playerIdx]
										 .currentTargetObjectIdx]
									.world_x,
						g_players[playerIdx]
								.viewState
								.cameraWorldY -
							g_objectTable
								[(uint16_t)g_players
									 [playerIdx]
										 .currentTargetObjectIdx]
									.world_y,
						g_players[playerIdx]
								.viewState
								.cameraWorldZ -
							g_objectTable
								[(uint16_t)g_players
									 [playerIdx]
										 .currentTargetObjectIdx]
									.world_z);
				}
				g_players[playerIdx].mapCameraState ^= 0x80;
			}
			break;
		case 1:
		case 5:
			if (g_players[playerIdx].mapCameraState == 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) !=
				    0) {
					g_players[playerIdx]
						.currentTargetObjectIdx =
						g_players[playerIdx]
							.pendingActionParam;
					if (g_players[playerIdx]
						    .viewState
						    .transitionTimer != 0) {
						g_players[playerIdx]
							.viewState
							.cameraFocusObjIdx =
							(uint16_t)g_players[playerIdx]
								.pendingActionParam;
					}
					g_players[playerIdx].missileLockState =
						0;
					craft->warheadLockTicks = 0;
					if (g_players[playerIdx]
							    .currentTargetObjectIdx ==
						    -1 ||
					    g_objectTable[(uint16_t)g_players[playerIdx]
								  .currentTargetObjectIdx]
							    .objectType == 0) {
						msg_emitInFlightMessage(
							IFMSG_265_OBJECT_DESTROYED,
							playerIdx);
					} else {
						msg_emitInFlightMessage(
							IFMSG_264_OBJECT_TARGETED,
							playerIdx);
					}
					if (g_players[playerIdx]
						    .pendingActionIssuerPlayerIdx ==
					    g_localPlayer) {
						if (g_players[playerIdx]
							    .pendingActionId ==
						    1) {
							msg_radioMessage(
								(uint16_t)g_players
									[playerIdx]
										.objectIndex,
								(uint8_t *)g_objectTable
									[g_players[playerIdx]
										 .objectIndex]
										.mobj
										->pCraft,
								154, 4, 0);
						} else {
							msg_radioMessage(
								(uint16_t)g_players
									[playerIdx]
										.objectIndex,
								(uint8_t *)g_objectTable
									[g_players[playerIdx]
										 .objectIndex]
										.mobj
										->pCraft,
								150, 6, 0);
						}
					}
				} else {
					g_msgArgTable[0] = 96;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
				}
			}
			break;
		case 2:
			if (g_flightSimSideEffectsSuppressed == 0) {
				departureObjectIndex =
					g_players[playerIdx].pendingActionParam;
				if (departureObjectIndex == -1) {
					if (g_players[playerIdx]
						    .participationState == 1) {
						int connectedCount = 0;
						int16_t otherPlayerIdx;
						for (otherPlayerIdx = 0;
						     otherPlayerIdx < 8;
						     ++otherPlayerIdx) {
							if (g_players[otherPlayerIdx]
								    .participationState ==
							    1) {
								++connectedCount;
							}
						}
						if (connectedCount > 1) {
							if (g_players[playerIdx]
								    .objectIndex !=
							    -1) {
								fsfx_UpdateBeamSystemLoop(
									0,
									playerIdx);
								fsfx_UpdateIncomingMissileWarning(
									0);
							}
							Player_UnbindFromCurrentCraft(
								playerIdx, 0,
								1);
						}
						Player_EndFlightParticipation(
							playerIdx);
						if (playerIdx !=
						    g_localPlayer) {
							msg_addMessagePtr(
								0,
								NetSession_GetPlayerName(
									playerIdx));
							msg_emitInFlightMessage(
								IFMSG_380_ARG_HAS_QUIT_THE_MISSION,
								g_localPlayer);
						}
						if (g_missionHeader.missionType ==
							    MISSION_TYPE_MELEE &&
						    g_pilotData.numHumanPlayersLastMission ==
							    1 &&
						    g_flightMissionState.runtime.teamGoalStatus
								    [(uint16_t)g_players
									     [playerIdx]
										     .team]
								    [0] != 1) {
							g_players[playerIdx]
								.missionStats
								.missionScore -=
								2000;
							g_flightMissionState
								.runtime
								.teamScores
									[TEAM_SCORE_MISSION]
									[(uint16_t)g_players
										 [playerIdx]
											 .team] -=
								2000;
						}
					} else {
						if (playerIdx ==
						    g_localPlayer) {
							g_flightMissionState
								.missionEndPending =
								1;
						}
						g_players[playerIdx]
							.participationState = 0;
						Flight_UpdateActivePlayerCount();
						if (NetSession_GetHostDplayId() !=
						    g_players[playerIdx]
							    .network
							    .directPlayId) {
							FlightNet_MarkPilotNetworkPlayerLeft(
								playerIdx);
						}
						if (playerIdx ==
							    g_localPlayer &&
						    NetSession_IsLocalHost() !=
							    0) {
							FlightNet_BroadcastHostSessionAbort();
						}
					}
				} else {
					uint16_t flightGroupIdx =
						g_players[playerIdx]
							.boundFlightGroupIdx;
					uint16_t mothership = pai_FindMothershipObject(
						g_missionFlightGroups[flightGroupIdx]
							.fg
							.departureMothership);
					Mission_RecordCraftOutcome(
						(uint16_t)objectIndex,
						flightGroupIdx,
						(uint16_t)(mothership == (uint16_t)departureObjectIndex
								   ? FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT
								   : FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT));
					if (g_missionHeader.missionType !=
						    MISSION_TYPE_MELEE &&
					    g_flightMissionState
							    .playerFlightGroupWaveMode ==
						    1 &&
					    g_missionFlightGroups[flightGroupIdx]
							    .fg.numberOfWaves !=
						    99) {
						g_players[playerIdx]
							.missionStats
							.missionScore +=
							40 *
							g_modelDefs
								[GetModelIndexFromType(
									 g_objectTable[objectIndex]
										 .objectType)]
									.craftPointValue;
					}
					if (g_players[playerIdx].objectIndex !=
					    -1) {
						fsfx_UpdateBeamSystemLoop(
							0, playerIdx);
						fsfx_UpdateIncomingMissileWarning(
							0);
					}
					g_objectTable[objectIndex].objectType =
						0;
					Player_SaveCraftSettings(playerIdx);
					Craft_FreeLinkedObjects(craft);
					Mission_ProcessFlightGroupWaveCompletion(
						flightGroupIdx);
					if (Player_BindToAvailableCraft(
						    playerIdx, UINT32_MAX, 0,
						    0) != 0) {
						Player_EndFlightParticipation(
							playerIdx);
						Player_EmitRemotePlayerDepartedMessages(
							playerIdx);
					} else if (playerIdx == g_localPlayer) {
						msg_emitLocalPlayerCraftMessage(
							IFMSG_292_PREVIOUS_CRAFT_ENTERED_HANGAR_NOW_PILOTING_ARG_ARG_ARG);
					}
				}
			}
			break;
		case 3:
			g_flightMissionState.runtime.teamReinforcementsCalled
				[(uint16_t)g_players[playerIdx].team] = 1;
			g_flightMissionState.runtime.teamScores
				[TEAM_SCORE_BONUS]
				[(uint16_t)g_players[playerIdx].team] -= 5000;
			if (g_players[g_localPlayer].iff ==
			    g_players[playerIdx].iff) {
				msg_emitInFlightMessage(
					IFMSG_231_REQUEST_FOR_REINFORCEMENTS_ACKNOWLEDGED,
					playerIdx);
				fsfx_SpeakTacticalOfficerEvent(
					TACTICAL_VOICE_ORDER,
					TACTICAL_MSG_REINFORCEMENTS_ACKNOWLEDGED,
					-1, UINT16_MAX);
			}
			break;
		case 4:
			if (g_players[playerIdx].mapCameraState == 0) {
				if ((craft->workingSubsystems &
				     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) !=
				    0) {
					if (g_players[playerIdx]
						    .currentTargetObjectIdx ==
					    g_players[playerIdx]
						    .pendingActionParam) {
						int targetIndex = Player_FindNearestEnemyFighter(
							playerIdx,
							(uint16_t)g_players[playerIdx]
								.pendingActionParam);
						if (targetIndex == -1) {
							g_players[playerIdx]
								.currentTargetObjectIdx =
								-1;
						} else {
							Player_SetTarget(
								targetIndex,
								playerIdx);
						}
						if (g_players[playerIdx]
							    .pendingActionParam !=
						    -1) {
							msg_emitInFlightMessage(
								g_objectTable[(uint16_t)g_players
										      [playerIdx]
											      .pendingActionParam]
											.objectType !=
										0
									? IFMSG_266_OBJECT_IGNORED
									: IFMSG_265_OBJECT_DESTROYED,
								playerIdx);
							if (g_players[playerIdx]
								    .pendingActionIssuerPlayerIdx ==
							    g_localPlayer) {
								msg_radioMessage(
									(uint16_t)g_players
										[playerIdx]
											.objectIndex,
									(uint8_t *)g_objectTable
										[g_players[playerIdx]
											 .objectIndex]
											.mobj
											->pCraft,
									0x9B, 5,
									0);
							}
						}
					}
				} else {
					g_msgArgTable[0] = 96;
					g_msgArgTable[1] = 87;
					msg_emitInFlightMessage(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
				}
			}
			break;
		case 6:
			if (g_players[playerIdx].mapCameraState == 0) {
				if (g_players[playerIdx]
					    .pendingActionIssuerPlayerIdx ==
				    g_localPlayer) {
					msg_radioMessage(
						(uint16_t)g_players[playerIdx]
							.objectIndex,
						(uint8_t *)g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj->pCraft,
						0x95, 1, 0);
				}
				Player_HandleHyperspaceCommand(craft,
							       playerIdx);
			}
			break;
		case 7:
			if (g_players[playerIdx].mapCameraState == 0) {
				craft->throttleSpeed = 0;
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				msg_emitInFlightMessage(
					IFMSG_267_WAITING_THROTTLE_SET_TO_NO_POWER,
					playerIdx);
				if (g_players[playerIdx]
					    .pendingActionIssuerPlayerIdx ==
				    g_localPlayer) {
					msg_radioMessage(
						(uint16_t)g_players[playerIdx]
							.objectIndex,
						(uint8_t *)g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj->pCraft,
						0x98, 2, 0);
				}
			}
			break;
		case 8:
			if (g_players[playerIdx].mapCameraState == 0) {
				craft->throttleSpeed = UINT16_MAX;
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
				msg_emitInFlightMessage(
					IFMSG_268_RESUMING_THROTTLE_SET_TO_FULL_POWER,
					playerIdx);
				if (g_players[playerIdx]
					    .pendingActionIssuerPlayerIdx ==
				    g_localPlayer) {
					msg_radioMessage(
						(uint16_t)g_players[playerIdx]
							.objectIndex,
						(uint8_t *)g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj->pCraft,
						0x99, 3, 0);
				}
			}
			break;
		case 9:
			if (g_players[playerIdx].mapCameraState == 0) {
				int targetIndex =
					Player_FindNearestEnemyFighter(
						playerIdx,
						(uint16_t)g_players[playerIdx]
							.pendingActionParam);
				if (targetIndex == -1) {
					g_players[playerIdx]
						.currentTargetObjectIdx = -1;
				} else {
					Player_SetTarget(targetIndex,
							 playerIdx);
				}
				msg_emitInFlightMessage(
					IFMSG_264_OBJECT_TARGETED, playerIdx);
				if (g_players[playerIdx]
					    .pendingActionIssuerPlayerIdx ==
				    g_localPlayer) {
					msg_radioMessage(
						(uint16_t)g_players[playerIdx]
							.objectIndex,
						(uint8_t *)g_objectTable
							[g_players[playerIdx]
								 .objectIndex]
								.mobj->pCraft,
						0x97, 7, 0);
				}
			}
			break;
		default:
			break;
		}
		g_players[playerIdx].pendingActionId = 0;
		return;

	case FLIGHT_KEY_STAR:
	case FLIGHT_KEY_PAD_STAR:
		if (g_flightSimSideEffectsSuppressed == 0 &&
		    g_replayViewMode == 0) {
			if (g_players[playerIdx].mapCameraState != 0) {
				g_players[playerIdx]
					.viewState.cameraFocusObjIdx =
					UINT16_MAX;
			} else if (g_players[playerIdx]
					   .viewState.externalCameraActive !=
				   0) {
				g_players[playerIdx]
					.viewState.playerInputBlocked =
					g_players[playerIdx]
						.viewState.playerInputBlocked ==
					0;
				fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					       playerIdx);
			} else {
				fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1,
					       playerIdx);
			}
		}
		return;
	case FLIGHT_KEY_COMMA: {
		int targetIndex = g_players[playerIdx].currentTargetObjectIdx;
		if (targetIndex != -1 &&
		    targetIndex < g_activeRegionCraftObjectSlotEnd) {
			CraftData *targetCraft =
				g_objectTable[(uint16_t)targetIndex]
					.mobj->pCraft;
			uint16_t meshCount =
				g_objectTable[(uint16_t)targetIndex]
							.objectType >= 73
					? ModelMesh_GetObjectTypeMeshCount(
						  g_objectTable
							  [(uint16_t)
								   targetIndex]
								  .objectType)
					: g_objectTypeMeshCache
						  [g_objectTable
							   [(uint16_t)
								    targetIndex]
								   .objectType]
							  .meshCount;
			int16_t attempt;
			for (attempt = 0; attempt < meshCount; ++attempt) {
				++g_players[playerIdx].selectedTargetComponent;
				if ((uint16_t)g_players[playerIdx]
					    .selectedTargetComponent >=
				    meshCount) {
					g_players[playerIdx]
						.selectedTargetComponent = 0;
				}
				if (targetCraft->componentState
						    [(uint16_t)g_players[playerIdx]
							     .selectedTargetComponent] ==
					    0 &&
				    Craft_IsSelectableDamageComponentMesh(
					    g_objectTable
						    [(uint16_t)g_players[playerIdx]
							     .currentTargetObjectIdx]
							    .objectType,
					    (uint16_t)g_players[playerIdx]
						    .selectedTargetComponent) &&
				    targetCraft->componentHp
						    [(uint16_t)g_players[playerIdx]
							     .selectedTargetComponent] !=
					    0) {
					break;
				}
			}
			fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
				       playerIdx);
		}
		return;
	}
	case FLIGHT_KEY_LESS_THAN: {
		int targetIndex = g_players[playerIdx].currentTargetObjectIdx;
		if (targetIndex != -1 &&
		    targetIndex < g_activeRegionCraftObjectSlotEnd) {
			CraftData *targetCraft =
				g_objectTable[(uint16_t)targetIndex]
					.mobj->pCraft;
			uint16_t meshCount =
				g_objectTable[(uint16_t)targetIndex]
							.objectType >= 73
					? ModelMesh_GetObjectTypeMeshCount(
						  g_objectTable
							  [(uint16_t)
								   targetIndex]
								  .objectType)
					: g_objectTypeMeshCache
						  [g_objectTable
							   [(uint16_t)
								    targetIndex]
								   .objectType]
							  .meshCount;
			int16_t attempt;
			for (attempt = 0; attempt < meshCount; ++attempt) {
				--g_players[playerIdx].selectedTargetComponent;
				if ((uint16_t)g_players[playerIdx]
					    .selectedTargetComponent ==
				    UINT16_MAX) {
					g_players[playerIdx]
						.selectedTargetComponent =
						(int16_t)(meshCount - 1);
				}
				if (targetCraft->componentState
						    [(uint16_t)g_players[playerIdx]
							     .selectedTargetComponent] ==
					    0 &&
				    Craft_IsSelectableDamageComponentMesh(
					    g_objectTable
						    [(uint16_t)g_players[playerIdx]
							     .currentTargetObjectIdx]
							    .objectType,
					    (uint16_t)g_players[playerIdx]
						    .selectedTargetComponent) &&
				    targetCraft->componentHp
						    [(uint16_t)g_players[playerIdx]
							     .selectedTargetComponent] !=
					    0) {
					break;
				}
			}
			fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
				       playerIdx);
		}
		return;
	}
	case FLIGHT_KEY_SLASH:
	case FLIGHT_KEY_PAD_SLASH:
		if (g_flightSimSideEffectsSuppressed == 0) {
			PlayerViewState *view = &g_players[playerIdx].viewState;
			if (g_players[playerIdx].mapCameraState != 0) {
				int targetIndex =
					g_players[playerIdx]
						.currentTargetObjectIdx;
				if (targetIndex != -1) {
					view->cameraFocusObjIdx =
						(uint16_t)targetIndex;
					view->cameraDistance =
						4 *
						g_objectTypeTable
							[g_objectTable[targetIndex]
								 .objectType]
								.maxBoundsExtent;
					if (g_players[playerIdx]
						    .mapCameraState > 1) {
						g_players[playerIdx]
							.mapCameraState = 1;
						view->hudAimX = 0;
					}
				}
			} else if (view->transitionTimer == 0) {
				view->externalCameraActive =
					view->externalCameraActive == 0;
				if (view->externalCameraActive != 0 &&
				    view->cameraFocusObjIdx ==
					    g_players[playerIdx].objectIndex) {
					view->savedHudStateByte =
						view->hudStateLive;
					view->savedHudAimX = view->hudAimX;
					view->savedHudAimY = view->hudAimY;
				}
				Player_UpdateHudViewForCameraFocus(playerIdx);
			}
		}
		return;
	case FLIGHT_KEY_1:
	case FLIGHT_KEY_2:
	case FLIGHT_KEY_3:
	case FLIGHT_KEY_4: {
		int16_t dstPlayerIdx;
		int tauntIndex = g_currentActionKey - FLIGHT_KEY_1;
		msg_addMessagePtr(0, NetSession_GetPlayerName(playerIdx));
		msg_addMessagePtr(1, g_playerTauntText[playerIdx][tauntIndex]);
		for (dstPlayerIdx = 0; dstPlayerIdx < 8; ++dstPlayerIdx) {
			if (g_players[dstPlayerIdx].participationState != 0) {
				g_msgSenderIff = 3;
				msg_emitInFlightMessage(IFMSG_374_FROM_ARG_ARG,
							dstPlayerIdx);
			}
		}
		msg_emitInFlightMessage(IFMSG_378_MESSAGE_SENT, playerIdx);
		return;
	}
	case FLIGHT_KEY_SHIFT_A: {
		int targetIndex = g_players[playerIdx].currentTargetObjectIdx;
		int16_t otherPlayerIdx;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		if (targetIndex == -1) {
			return;
		}
		if (craft != NULL &&
		    craft->playerCommandAvoidTargetObjIdx == targetIndex) {
			craft->playerCommandAvoidTargetObjIdx = UINT16_MAX;
		}
		Player_IssueAiWingmanTargetOrder((uint16_t)targetIndex, 0x9A, 4,
						 playerIdx);
		for (otherPlayerIdx = 0; otherPlayerIdx < 8; ++otherPlayerIdx) {
			CraftData *otherCraft;
			if (otherPlayerIdx == playerIdx ||
			    g_players[otherPlayerIdx].participationState != 1 ||
			    g_players[otherPlayerIdx].team !=
				    g_players[playerIdx].team ||
			    g_players[otherPlayerIdx].objectIndex == -1 ||
			    g_players[otherPlayerIdx].objectIndex ==
				    (uint16_t)targetIndex ||
			    g_players[otherPlayerIdx].pendingActionId != 0) {
				continue;
			}
			otherCraft = g_objectTable[g_players[otherPlayerIdx]
							   .objectIndex]
					     .mobj->pCraft;
			if (otherCraft == NULL ||
			    (otherCraft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0) {
				continue;
			}
			g_players[otherPlayerIdx].pendingActionId = 1;
			g_players[otherPlayerIdx].pendingActionParam =
				(int16_t)targetIndex;
			g_players[otherPlayerIdx].pendingActionIssuerPlayerIdx =
				(uint16_t)playerIdx;
			g_players[otherPlayerIdx].pendingActionTimer = 1416;
			if (otherPlayerIdx == g_localPlayer) {
				fsfx_PlaySound(FLIGHT_SOUND_INCOMING_ORDER, -1,
					       g_localPlayer);
				msg_addMessagePtr(
					0, NetSession_GetPlayerName(playerIdx));
				msg_emitInFlightMessage(
					IFMSG_270_FROM_ARG_ATTACK_MY_TARGET_HIT_SPACE_TO_TARGET,
					g_localPlayer);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_E: {
		int16_t otherPlayerIdx;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		if (Player_CanRadioCommandCraft(playerIdx) != 0) {
			int targetIndex = (uint16_t)g_players[playerIdx]
						  .currentTargetObjectIdx;
			AiController *controller;
			g_curCraft = g_objectTable[targetIndex].mobj->pCraft;
			controller = &g_curCraft->aiController;
			if (strcmp(g_planTable[controller->pendingPlanId].name,
				   "craftwaitforgopln") == 0) {
				controller->pendingPlanId =
					controller->savedPlanId;
				pai_setupcraftcontext((uint16_t)targetIndex);
				pai_ApplyPendingPlanTargetAndManeuver(
					(uint16_t)targetIndex);
			}
			controller->candidateTargetIdx = AI_TARGET_ABORT;
			msg_radioMessage((uint16_t)targetIndex,
					 (uint8_t *)g_curCraft, 0x97, 7, 0);
			return;
		}
		for (otherPlayerIdx = 0; otherPlayerIdx < 8; ++otherPlayerIdx) {
			if (g_players[otherPlayerIdx].objectIndex !=
			    (uint16_t)g_players[playerIdx]
				    .currentTargetObjectIdx) {
				continue;
			}
			if (g_players[otherPlayerIdx].team ==
			    g_players[playerIdx].team) {
				g_players[otherPlayerIdx].pendingActionId = 9;
				g_players[otherPlayerIdx]
					.pendingActionIssuerPlayerIdx =
					(uint16_t)playerIdx;
				g_players[otherPlayerIdx].pendingActionTimer =
					1416;
				if (otherPlayerIdx == g_localPlayer) {
					fsfx_PlaySound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_localPlayer);
					msg_addMessagePtr(
						0, NetSession_GetPlayerName(
							   playerIdx));
					msg_emitInFlightMessage(
						IFMSG_275_FROM_ARG_EVADE_HIT_SPACE_TO_TARGET_ATTACKER,
						g_localPlayer);
				}
			} else {
				g_msgSenderIff = g_players[playerIdx].iff;
				msg_emitInFlightMessage(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					playerIdx);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_G: {
		int16_t otherPlayerIdx;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		if (Player_CanRadioCommandCraft(playerIdx) != 0) {
			int targetIndex = (uint16_t)g_players[playerIdx]
						  .currentTargetObjectIdx;
			g_curCraft = g_objectTable[targetIndex].mobj->pCraft;
			if (strcmp(g_planTable[g_curCraft->aiController
						       .pendingPlanId]
					   .name,
				   "craftwaitforgopln") == 0) {
				g_curCraft->aiController.pendingPlanId =
					g_curCraft->aiController.savedPlanId;
				pai_setupcraftcontext((uint16_t)targetIndex);
				pai_ApplyPendingPlanTargetAndManeuver(
					(uint16_t)targetIndex);
				msg_radioMessage((uint16_t)targetIndex,
						 (uint8_t *)g_curCraft, 0x99, 3,
						 0);
			}
			return;
		}
		for (otherPlayerIdx = 0; otherPlayerIdx < 8; ++otherPlayerIdx) {
			if (g_players[otherPlayerIdx].objectIndex !=
			    (uint16_t)g_players[playerIdx]
				    .currentTargetObjectIdx) {
				continue;
			}
			if (g_players[otherPlayerIdx].team ==
			    g_players[playerIdx].team) {
				g_players[otherPlayerIdx].pendingActionId = 8;
				g_players[otherPlayerIdx]
					.pendingActionIssuerPlayerIdx =
					(uint16_t)playerIdx;
				g_players[otherPlayerIdx].pendingActionTimer =
					1416;
				if (otherPlayerIdx == g_localPlayer) {
					fsfx_PlaySound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_localPlayer);
					msg_addMessagePtr(
						0, NetSession_GetPlayerName(
							   playerIdx));
					msg_emitInFlightMessage(
						IFMSG_274_FROM_ARG_GO_AHEAD_HIT_SPACE_TO_PROCEED_WITH_MISSION,
						g_localPlayer);
				}
			} else {
				g_msgSenderIff = g_players[playerIdx].iff;
				msg_emitInFlightMessage(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					playerIdx);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_H: {
		int16_t otherPlayerIdx;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		if (Player_CanRadioCommandCraft(playerIdx) != 0) {
			int targetIndex = (uint16_t)g_players[playerIdx]
						  .currentTargetObjectIdx;
			ObjectRecord *target = &g_objectTable[targetIndex];
			AiController *controller;
			g_curCraft = target->mobj->pCraft;
			controller = &g_curCraft->aiController;
			if (strcmp(g_planTable[controller->pendingPlanId].name,
				   "flyhomeevadepln") != 0 &&
			    strcmp(g_planTable[controller->pendingPlanId].name,
				   "starshipintohyperpln") != 0) {
				if (g_curCraft->aiFlight.missionAbortedFlag ==
				    0) {
					++g_missionFgStats[target->flightGroupIdx]
						  .outcomeCount
							  [FLIGHT_GROUP_OUTCOME_ABORTED];
					if (g_missionFlightGroups
						    [target->flightGroupIdx]
							    .fg
							    .specialCargoCraft ==
					    g_curCraft->craftOrdinal) {
						g_missionFgStats[target->flightGroupIdx]
							.specialCargoOutcome
								[FLIGHT_GROUP_OUTCOME_ABORTED] =
							1;
					}
				}
				g_curCraft->aiFlight.missionAbortedFlag = 1;
				controller->pendingPlanId =
					pai_FindPlanIdByNameOrZero(
						target->genusId ==
								CRAFT_GENUS_STARSHIP
							? "starshipintohyperpln"
							: "flyhomeevadepln");
				pai_setupcraftcontext((uint16_t)targetIndex);
				pai_ApplyPendingPlanTargetAndManeuver(
					(uint16_t)targetIndex);
			}
			msg_radioMessage((uint16_t)targetIndex,
					 (uint8_t *)g_curCraft, 0x95, 1, 0);
			return;
		}
		for (otherPlayerIdx = 0; otherPlayerIdx < 8; ++otherPlayerIdx) {
			if (g_players[otherPlayerIdx].objectIndex !=
			    (uint16_t)g_players[playerIdx]
				    .currentTargetObjectIdx) {
				continue;
			}
			if (g_players[otherPlayerIdx].team ==
			    g_players[playerIdx].team) {
				g_players[otherPlayerIdx].pendingActionId = 6;
				g_players[otherPlayerIdx]
					.pendingActionIssuerPlayerIdx =
					(uint16_t)playerIdx;
				g_players[otherPlayerIdx].pendingActionTimer =
					1416;
				if (otherPlayerIdx == g_localPlayer) {
					fsfx_PlaySound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_localPlayer);
					msg_addMessagePtr(
						0, NetSession_GetPlayerName(
							   playerIdx));
					msg_emitInFlightMessage(
						IFMSG_272_FROM_ARG_HEAD_HOME_HIT_SPACE_TO_COMPLY,
						g_localPlayer);
				}
			} else {
				g_msgSenderIff = g_players[playerIdx].iff;
				msg_emitInFlightMessage(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					playerIdx);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_I: {
		int targetIndex = g_players[playerIdx].currentTargetObjectIdx;
		int16_t otherPlayerIdx;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		if (targetIndex == -1) {
			return;
		}
		if (craft != NULL) {
			craft->playerCommandAvoidTargetObjIdx =
				(uint16_t)targetIndex;
		}
		Player_IssueAiWingmanTargetOrder((uint16_t)targetIndex, 0x9B, 5,
						 playerIdx);
		for (otherPlayerIdx = 0; otherPlayerIdx < 8; ++otherPlayerIdx) {
			CraftData *otherCraft;
			if (otherPlayerIdx == playerIdx ||
			    g_players[otherPlayerIdx].participationState != 1 ||
			    g_players[otherPlayerIdx].team !=
				    g_players[playerIdx].team ||
			    g_players[otherPlayerIdx].objectIndex == -1 ||
			    g_players[otherPlayerIdx].currentTargetObjectIdx !=
				    targetIndex ||
			    g_players[otherPlayerIdx].currentTargetObjectIdx ==
				    g_players[otherPlayerIdx].objectIndex ||
			    g_players[otherPlayerIdx].pendingActionId != 0) {
				continue;
			}
			otherCraft = g_objectTable[g_players[otherPlayerIdx]
							   .objectIndex]
					     .mobj->pCraft;
			if (otherCraft == NULL ||
			    (otherCraft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0) {
				continue;
			}
			g_players[otherPlayerIdx].pendingActionId = 4;
			g_players[otherPlayerIdx].pendingActionParam =
				(int16_t)targetIndex;
			g_players[otherPlayerIdx].pendingActionIssuerPlayerIdx =
				(uint16_t)playerIdx;
			g_players[otherPlayerIdx].pendingActionTimer = 1416;
			if (otherPlayerIdx == g_localPlayer) {
				fsfx_PlaySound(FLIGHT_SOUND_INCOMING_ORDER, -1,
					       g_localPlayer);
				msg_addMessagePtr(
					0, NetSession_GetPlayerName(playerIdx));
				msg_emitInFlightMessage(
					IFMSG_271_FROM_ARG_IGNORE_MY_TARGET_HIT_SPACE_TO_IGNORE,
					g_localPlayer);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_P: {
		uint16_t candidate =
			g_players[playerIdx].currentTargetObjectIdx;
		int16_t remaining = g_activeRegionCraftObjectSlotEnd -
				    g_activeRegionObjectSlotStart;
		int16_t newTarget = -1;
		while (remaining-- != 0) {
			ObjectRecord *object;
			int owner;
			int team;
			int hostile;
			if (++candidate >= g_activeRegionCraftObjectSlotEnd) {
				candidate = g_activeRegionObjectSlotStart;
			}
			object = &g_objectTable[candidate];
			if (object->objectType == 0 ||
			    object->genusId == CRAFT_GENUS_EXPLOSION ||
			    Object_HasActiveDecoyBeam((uint16_t)candidate) !=
				    0) {
				continue;
			}
			owner = object->playerOwnerIdx;
			if (owner == -1 || owner == playerIdx) {
				continue;
			}
			team = g_missionFlightGroups[object->flightGroupIdx]
				       .fg.team;
			hostile = team != (uint16_t)g_players[playerIdx].team &&
				  g_missionTeams[(uint16_t)g_players[playerIdx]
							 .team]
						  .allies[team] == 0;
			if (g_flightMissionState.locatePlayersEnabled == 0 &&
			    hostile) {
				continue;
			}
			if (object->mobj->family != 0 ||
			    (object->mobj->pCraft->objectKind !=
				     CRAFT_OBJECT_KIND_BREAKING_UP &&
			     object->mobj->pCraft->objectKind !=
				     CRAFT_OBJECT_KIND_EXPLODING)) {
				newTarget = candidate;
				break;
			}
		}
		if (newTarget != -1) {
			Player_SetTarget(newTarget, playerIdx);
		} else if (g_flightMissionState.locatePlayersEnabled == 0) {
			msg_emitInFlightMessage(
				IFMSG_225_AUTO_LOCATING_OF_PLAYERS_NOT_ENABLED_FOR_THIS_MISSION,
				playerIdx);
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_R: {
		int targetIndex;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		targetIndex = g_players[playerIdx].currentTargetObjectIdx;
		if (targetIndex != -1 &&
		    targetIndex < g_activeRegionCraftObjectSlotEnd) {
			ObjectRecord *target =
				&g_objectTable[(uint16_t)targetIndex];
			int playerTeam = (uint16_t)g_players[playerIdx].team;
			int team = g_missionFlightGroups[target->flightGroupIdx]
					   .fg.team;
			if (playerTeam == team ||
			    g_missionTeams[playerTeam].allies[team] != 0) {
				if (target->playerOwnerIdx == -1) {
					g_curCraft = target->mobj->pCraft;
					msg_reportmessage(
						(uint16_t)targetIndex,
						g_curCraft,
						g_planReportMessageIdByPlanId
							[g_curCraft
								 ->aiController
								 .pendingPlanId]);
				} else {
					int16_t otherPlayerIdx;
					for (otherPlayerIdx = 0;
					     otherPlayerIdx < 8;
					     ++otherPlayerIdx) {
						if (otherPlayerIdx !=
							    playerIdx &&
						    g_players[otherPlayerIdx]
								    .participationState ==
							    1 &&
						    g_players[otherPlayerIdx]
								    .team ==
							    g_players[playerIdx]
								    .team &&
						    g_players[otherPlayerIdx]
								    .objectIndex ==
							    (uint16_t)
								    targetIndex &&
						    otherPlayerIdx ==
							    g_localPlayer) {
							fsfx_PlaySound(
								FLIGHT_SOUND_INCOMING_ORDER,
								-1,
								g_localPlayer);
							msg_addMessagePtr(
								0,
								NetSession_GetPlayerName(
									playerIdx));
							msg_emitInFlightMessage(
								IFMSG_276_FROM_ARG_REPORT_IN,
								g_localPlayer);
						}
					}
				}
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_S: {
		uint16_t flightGroupIdx;
		uint8_t reinforcementAvailable = 0;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		if (g_players[playerIdx].pendingActionId != 0) {
			return;
		}
		for (flightGroupIdx = 0;
		     flightGroupIdx < g_missionHeader.numFlightGroups;
		     ++flightGroupIdx) {
			if ((g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[0]
					     .triggers[0]
					     .condition == 20 &&
			     g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[0]
					     .triggers[0]
					     .variable ==
				     (uint16_t)g_players[playerIdx].team) ||
			    (g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[0]
					     .triggers[1]
					     .condition == 20 &&
			     g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[0]
					     .triggers[1]
					     .variable ==
				     (uint16_t)g_players[playerIdx].team)) {
				reinforcementAvailable = 1;
			}
			if ((g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[1]
					     .triggers[0]
					     .condition == 20 &&
			     g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[1]
					     .triggers[0]
					     .variable ==
				     (uint16_t)g_players[playerIdx].team) ||
			    (g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[1]
					     .triggers[1]
					     .condition == 20 &&
			     g_missionFlightGroups[flightGroupIdx]
					     .fg.arrivalTriggers[1]
					     .triggers[1]
					     .variable ==
				     (uint16_t)g_players[playerIdx].team)) {
				reinforcementAvailable = 1;
			}
		}
		g_msgSenderIff = g_players[playerIdx].iff;
		if (reinforcementAvailable == 0) {
			if (g_players[g_localPlayer].iff ==
			    g_players[playerIdx].iff) {
				msg_emitInFlightMessage(
					IFMSG_230_NO_REINFORCEMENTS_AVAILABLE,
					playerIdx);
				fsfx_SpeakTacticalOfficerEvent(
					TACTICAL_VOICE_ORDER,
					TACTICAL_MSG_NO_REINFORCEMENTS_AVAILABLE,
					-1, UINT16_MAX);
			}
		} else if (g_flightMissionState.runtime.teamReinforcementsCalled
				   [(uint16_t)g_players[playerIdx].team] == 0) {
			if (g_players[g_localPlayer].iff ==
			    g_players[playerIdx].iff) {
				msg_emitInFlightMessage(
					IFMSG_233_HIT_SPACE_TO_CONFIRM_REINFORCEMENT_REQUEST,
					playerIdx);
			}
			g_players[playerIdx].pendingActionId = 3;
			g_players[playerIdx].pendingActionTimer = 1888;
		} else {
			if (g_players[g_localPlayer].iff ==
			    g_players[playerIdx].iff) {
				msg_emitInFlightMessage(
					IFMSG_232_REINFORCEMENTS_ALREADY_SENT_NO_MORE_AVAILABLE,
					playerIdx);
				fsfx_SpeakTacticalOfficerEvent(
					TACTICAL_VOICE_ORDER,
					TACTICAL_MSG_REINFORCEMENTS_ALREADY_SENT,
					-1, UINT16_MAX);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_W: {
		int16_t otherPlayerIdx;
		if (g_players[playerIdx].mapCameraState == 0 &&
		    (craft == NULL ||
		     (craft->workingSubsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msgArgTable[0] = 101;
			g_msgArgTable[1] = 87;
			msg_emitInFlightMessage(IFMSG_086_ARG_SYSTEM_IS_ARG,
						playerIdx);
			return;
		}
		if (Player_CanRadioCommandCraft(playerIdx) != 0) {
			int targetIndex = (uint16_t)g_players[playerIdx]
						  .currentTargetObjectIdx;
			AiController *controller;
			g_curCraft = g_objectTable[targetIndex].mobj->pCraft;
			controller = &g_curCraft->aiController;
			if (strcmp(g_planTable[controller->pendingPlanId].name,
				   "craftwaitforgopln") != 0 &&
			    strcmp(g_planTable[controller->pendingPlanId].name,
				   "intohyperspacepln") != 0 &&
			    strcmp(g_planTable[controller->pendingPlanId].name,
				   "outofhyperspacepln") != 0) {
				controller->savedPlanId =
					controller->pendingPlanId;
				controller->pendingPlanId =
					pai_FindPlanIdByNameOrZero(
						g_objectTable[targetIndex]
									.genusId ==
								CRAFT_GENUS_STARSHIP
							? "starshipwaitforgopln"
							: "craftwaitforgopln");
				pai_setupcraftcontext((uint16_t)targetIndex);
				pai_ApplyPendingPlanTargetAndManeuver(
					(uint16_t)targetIndex);
				msg_radioMessage((uint16_t)targetIndex,
						 (uint8_t *)g_curCraft, 0x98, 2,
						 0);
			}
			return;
		}
		for (otherPlayerIdx = 0; otherPlayerIdx < 8; ++otherPlayerIdx) {
			if (g_players[otherPlayerIdx].objectIndex !=
			    (uint16_t)g_players[playerIdx]
				    .currentTargetObjectIdx) {
				continue;
			}
			if (g_players[otherPlayerIdx].team ==
			    g_players[playerIdx].team) {
				g_players[otherPlayerIdx].pendingActionId = 7;
				g_players[otherPlayerIdx]
					.pendingActionIssuerPlayerIdx =
					(uint16_t)playerIdx;
				g_players[otherPlayerIdx].pendingActionTimer =
					1416;
				if (otherPlayerIdx == g_localPlayer) {
					fsfx_PlaySound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_localPlayer);
					msg_addMessagePtr(
						0, NetSession_GetPlayerName(
							   playerIdx));
					msg_emitInFlightMessage(
						IFMSG_273_FROM_ARG_WAIT_FOR_ORDERS_HIT_SPACE_TO_WAIT_FOR_ORDERS,
						g_localPlayer);
				}
			} else {
				g_msgSenderIff = g_players[playerIdx].iff;
				msg_emitInFlightMessage(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					playerIdx);
			}
		}
		return;
	}
	case FLIGHT_KEY_A:
		Player_SetTarget(
			Player_FindAttackerOfTarget(
				g_players[playerIdx].currentTargetObjectIdx,
				g_players[playerIdx].objectIndex),
			playerIdx);
		return;
	case FLIGHT_KEY_E: {
		uint16_t candidate =
			g_players[playerIdx].currentTargetObjectIdx;
		int16_t remaining = g_activeRegionCraftObjectSlotEnd -
				    g_activeRegionObjectSlotStart;
		int16_t attacker = -1;
		if (g_players[playerIdx].objectIndex == -1) {
			return;
		}
		while (remaining-- != 0) {
			ObjectRecord *object;
			CraftData *candidateCraft;
			if (++candidate >= g_activeRegionCraftObjectSlotEnd) {
				candidate = g_activeRegionObjectSlotStart;
			}
			object = &g_objectTable[candidate];
			if (object->objectType == 0 ||
			    candidate == g_players[playerIdx].objectIndex ||
			    object->genusId == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			candidateCraft = object->mobj->pCraft;
			if (candidateCraft->workingSubsystems == 0 ||
			    candidateCraft->objectKind !=
				    CRAFT_OBJECT_KIND_ACTIVE ||
			    Object_HasActiveDecoyBeam((uint16_t)candidate) !=
				    0) {
				continue;
			}
			if (object->playerOwnerIdx == -1) {
				int maneuver = candidateCraft->aiController
						       .maneuverMode;
				if (candidateCraft->aiController.targetObjIdx ==
					    objectIndex &&
				    (maneuver == 12 || maneuver == 23)) {
					attacker = candidate;
					break;
				}
			} else {
				int recentAttacker =
					(uint16_t)craft->lastAttackerObjIdx ==
						candidate &&
					(uint16_t)Mission_ClockToSeconds(
						g_missionElapsedClock.hours,
						g_missionElapsedClock.minutes,
						g_missionElapsedClock.seconds) -
							craft->lastHitMissionSecond <
						5;
				int owner = object->playerOwnerIdx;
				int team = g_missionFlightGroups
						   [object->flightGroupIdx]
							   .fg.team;
				int hostile =
					team != (uint16_t)g_players[playerIdx]
							.team &&
					g_missionTeams[(uint16_t)g_players
							       [playerIdx]
								       .team]
							.allies[team] == 0;
				if (recentAttacker ||
				    ((uint16_t)g_players[owner]
						     .currentTargetObjectIdx ==
					     objectIndex &&
				     hostile)) {
					attacker = candidate;
					break;
				}
			}
		}
		Player_SetTarget(attacker, playerIdx);
		return;
	}
	case FLIGHT_KEY_D:
		if (playerIdx == g_localPlayer &&
		    g_flightSimSideEffectsSuppressed == 0 &&
		    g_players[playerIdx].mapCameraState == 0) {
			if (FlightPlayer_HasDisabledSubsystem() != 0) {
				Mfd_TogglePage(MFD_PAGE_DAMAGE);
			} else {
				msg_emitInFlightMessage(
					IFMSG_397_ALL_SYSTEMS_OPERATIONAL,
					g_localPlayer);
			}
		}
		return;
	case FLIGHT_KEY_SHIFT_F:
		if (playerIdx == g_localPlayer &&
		    g_flightSimSideEffectsSuppressed == 0) {
			int playerObjectIndex =
				g_players[playerIdx].objectIndex;
			if (playerObjectIndex != -1 &&
			    (g_objectTable[playerObjectIndex]
				     .mobj->pCraft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
				g_msgArgTable[0] = 96;
				g_msgArgTable[1] = 87;
				msg_emitInFlightMessage(
					IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
			} else {
				Mfd_TogglePage(MFD_PAGE_HOSTILE_CRAFT);
			}
		}
		return;
	case FLIGHT_KEY_F:
		if (playerIdx == g_localPlayer &&
		    g_flightSimSideEffectsSuppressed == 0) {
			if (g_players[playerIdx].objectIndex == -1 ||
			    (g_objectTable[g_players[playerIdx].objectIndex]
				     .mobj->pCraft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) != 0) {
				if (g_players[playerIdx].mapCameraState == 0) {
					Mfd_TogglePage(MFD_PAGE_FRIENDLY_CRAFT);
				}
			} else {
				g_msgArgTable[0] = 96;
				g_msgArgTable[1] = 87;
				msg_emitInFlightMessage(
					IFMSG_086_ARG_SYSTEM_IS_ARG, playerIdx);
			}
		}
		return;
	case FLIGHT_KEY_G:
		if (playerIdx == g_localPlayer &&
		    g_flightSimSideEffectsSuppressed == 0) {
			Mfd_TogglePage(MFD_PAGE_GOALS);
		}
		return;
	case FLIGHT_KEY_H:
		if (g_players[playerIdx].mapCameraState != 0 &&
		    g_flightSimSideEffectsSuppressed == 0 &&
		    playerIdx == g_localPlayer) {
			Mfd_TogglePage(MFD_PAGE_MAP_HELP);
		}
		return;
	case FLIGHT_KEY_I: {
		int projectileIndex;
		int nearestProjectile = -1;
		nearestObjectDistance = UINT32_MAX;
		for (projectileIndex = g_projectileObjectSlotStart;
		     projectileIndex < g_projectileObjectSlotEnd;
		     ++projectileIndex) {
			ObjectRecord *projectile =
				&g_objectTable[projectileIndex];
			WarheadGuidanceState *guidance;
			if (projectile->objectType == 0 ||
			    (projectile->genusId !=
				     CRAFT_GENUS_PLAYER_PROJECTILE &&
			     projectile->genusId !=
				     CRAFT_GENUS_OTHER_PROJECTILE) ||
			    g_projectileTypeData.warheadClass
					    [projectile->objectType -
					     PROJECTILE_OBJECT_TYPE_FIRST] ==
				    0) {
				continue;
			}
			guidance = projectile->mobj->pWarheadGuidance;
			if (guidance == NULL ||
			    guidance->targetObjIdx !=
				    g_players[playerIdx].objectIndex) {
				continue;
			}
			pai_ObjectRefDirectionToObjectRef(
				g_players[playerIdx].objectIndex,
				projectileIndex);
			if ((unsigned int)trig2_polardistance <
			    nearestObjectDistance) {
				nearestProjectile = projectileIndex;
				nearestObjectDistance = trig2_polardistance;
			}
		}
		if (nearestProjectile == -1) {
			for (projectileIndex = g_projectileObjectSlotStart;
			     projectileIndex < g_projectileObjectSlotEnd;
			     ++projectileIndex) {
				ObjectRecord *projectile =
					&g_objectTable[projectileIndex];
				WarheadGuidanceState *guidance;
				MobileObject *targetMobile;
				int targetTeam;
				int playerTeam;
				if (projectile->objectType == 0 ||
				    (projectile->genusId !=
					     CRAFT_GENUS_PLAYER_PROJECTILE &&
				     projectile->genusId !=
					     CRAFT_GENUS_OTHER_PROJECTILE) ||
				    g_projectileTypeData.warheadClass
						    [projectile->objectType -
						     PROJECTILE_OBJECT_TYPE_FIRST] ==
					    0) {
					continue;
				}
				guidance = projectile->mobj->pWarheadGuidance;
				if (guidance == NULL ||
				    guidance->targetObjIdx == UINT16_MAX) {
					continue;
				}
				targetMobile =
					g_objectTable[guidance->targetObjIdx]
						.mobj;
				if (targetMobile == NULL ||
				    targetMobile->family != 0) {
					continue;
				}
				targetTeam = targetMobile->team;
				playerTeam =
					(uint16_t)g_players[playerIdx].team;
				if (targetTeam != playerTeam &&
				    g_missionTeams[targetTeam]
						    .allies[playerTeam] == 0) {
					continue;
				}
				pai_ObjectRefDirectionToObjectRef(
					g_players[playerIdx].objectIndex,
					projectileIndex);
				if ((unsigned int)trig2_polardistance <
				    nearestObjectDistance) {
					nearestProjectile = projectileIndex;
					nearestObjectDistance =
						trig2_polardistance;
				}
			}
		}
		Player_SetTarget(nearestProjectile, playerIdx);
		return;
	}
	case FLIGHT_KEY_K:
		if (playerIdx == g_localPlayer &&
		    g_flightSimSideEffectsSuppressed == 0) {
			Mfd_TogglePage(MFD_PAGE_SCOREBOARD);
		}
		return;
	case FLIGHT_KEY_L:
		if (playerIdx == g_localPlayer &&
		    g_flightSimSideEffectsSuppressed == 0) {
			Mfd_TogglePage(MFD_PAGE_MESSAGE_LOG);
		}
		return;
	case FLIGHT_KEY_SHIFT_M:
	case FLIGHT_KEY_M:
		if (g_flightSimSideEffectsSuppressed == 0 &&
		    g_players[playerIdx].hyperspacePhase == 0) {
			if (g_players[playerIdx].mapCameraState != 0) {
				if (g_players[playerIdx].participationState !=
				    2) {
					int16_t savedMapCameraState;
					Mission_ProcessFlightGroupWaveCompletion(
						g_players[playerIdx]
							.boundFlightGroupIdx);
					savedMapCameraState =
						g_players[playerIdx]
							.mapCameraState;
					g_players[playerIdx].mapCameraState = 0;
					if (Player_BindToAvailableCraft(
						    playerIdx, UINT32_MAX,
						    g_players[playerIdx]
							    .boundObjectSignature,
						    0) == 0) {
						g_players[playerIdx]
							.mapCameraState = 0;
					} else {
						g_players[playerIdx]
							.mapCameraState = (uint8_t)
							savedMapCameraState;
					}
				}
			} else {
				int16_t targetIndex =
					g_players[playerIdx]
						.currentTargetObjectIdx;
				fsfx_UpdateBeamSystemLoop(0, playerIdx);
				fsfx_UpdateIncomingMissileWarning(0);
				if (targetIndex == -1) {
					targetIndex =
						(int16_t)g_players[playerIdx]
							.objectIndex;
				}
				Player_UnbindFromCurrentCraft(
					playerIdx, 0,
					g_currentActionKey != FLIGHT_KEY_M &&
						g_missionHeader.missionType !=
							MISSION_TYPE_MELEE);
				g_players[playerIdx].mapCameraState = UINT8_MAX;
				Hud_SetHudViewState(HUD_VIEW_CRAFT_LIST,
						    playerIdx);
				g_players[playerIdx]
					.viewState.playerInputBlocked = 1;
				g_players[playerIdx]
					.viewState.externalCameraActive = 1;
				g_players[playerIdx].viewState.cameraDistance =
					mapOverviewDistance;
				if (targetIndex != -1) {
					Player_SetTarget(targetIndex,
							 playerIdx);
				}
				g_players[playerIdx]
					.viewState.cameraFocusObjIdx =
					UINT16_MAX;
				g_players[playerIdx].viewState.aimTargetIdx =
					UINT16_MAX;
				g_players[playerIdx].viewState.cameraWorldZ =
					mapOverviewDistance;
				if (g_players[playerIdx]
					    .currentTargetObjectIdx != -1) {
					g_players[playerIdx]
						.viewState.cameraWorldX =
						g_objectTable
							[(uint16_t)g_players[playerIdx]
								 .currentTargetObjectIdx]
								.world_x;
					g_players[playerIdx]
						.viewState.cameraWorldY =
						g_objectTable
							[(uint16_t)g_players[playerIdx]
								 .currentTargetObjectIdx]
								.world_y;
					g_players[playerIdx]
						.viewState.cameraDistance =
						16 *
						g_objectTypeTable
							[g_objectTable
								 [(uint16_t)g_players
									  [playerIdx]
										  .currentTargetObjectIdx]
									 .objectType]
								.maxBoundsExtent;
				}
				g_players[playerIdx].pendingActionTimer = 0;
				g_players[playerIdx].pendingActionId = 0;
				fsfx_UpdatePlayerEngineLoop();
				fsfx_UpdateChaffLoop();
				fsfx_UpdateBeamEffectLoops();
			}
		}
		return;
	case FLIGHT_KEY_O: {
		int16_t targetIndex = Player_FindNearestObjective(0, playerIdx);
		if (targetIndex != -1) {
			Player_SetTarget(targetIndex, playerIdx);
		} else {
			targetIndex = Player_FindNearestObjective(2, playerIdx);
			if (targetIndex != -1) {
				Player_SetTarget(targetIndex, playerIdx);
			}
		}
		return;
	}
	case FLIGHT_KEY_P: {
		int candidate;
		int remaining;
		int nearestTarget = -1;
		nearestObjectDistance = UINT32_MAX;
		if (g_flightMissionState.locatePlayersEnabled == 0) {
			return;
		}
		candidate = g_players[playerIdx].currentTargetObjectIdx;
		remaining = g_activeRegionCraftObjectSlotEnd -
			    g_activeRegionObjectSlotStart - 1;
		while (remaining-- >= 0) {
			ObjectRecord *object;
			int team;
			int hostile;
			if (++candidate >= g_activeRegionCraftObjectSlotEnd) {
				candidate = g_activeRegionObjectSlotStart;
			}
			object = &g_objectTable[candidate];
			if (object->objectType == 0 ||
			    object->playerOwnerIdx == -1 ||
			    object->playerOwnerIdx == playerIdx ||
			    object->genusId == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			team = g_missionFlightGroups[object->flightGroupIdx]
				       .fg.team;
			hostile = team != (uint16_t)g_players[playerIdx].team &&
				  g_missionTeams[(uint16_t)g_players[playerIdx]
							 .team]
						  .allies[team] == 0;
			if (!hostile || Object_HasActiveDecoyBeam(
						(uint16_t)candidate) != 0) {
				continue;
			}
			if (object->mobj->family == 0 &&
			    (object->mobj->pCraft->objectKind ==
				     CRAFT_OBJECT_KIND_BREAKING_UP ||
			     object->mobj->pCraft->objectKind ==
				     CRAFT_OBJECT_KIND_EXPLODING)) {
				continue;
			}
			Player_ComputePolarToObjectRef(playerIdx, candidate);
			if ((unsigned int)trig2_polardistance <
			    nearestObjectDistance) {
				nearestTarget = candidate;
				nearestObjectDistance = trig2_polardistance;
			}
		}
		Player_SetTarget(nearestTarget, playerIdx);
		return;
	}
	case FLIGHT_KEY_Q:
		if (g_players[playerIdx].participationState == 1) {
			fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
				       playerIdx);
			fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
				       playerIdx);
			if (g_missionHeader.missionType == MISSION_TYPE_MELEE &&
			    g_pilotData.numHumanPlayersLastMission == 1 &&
			    g_flightMissionState.runtime.teamGoalStatus
					    [(uint16_t)g_players[playerIdx]
						     .team][0] != 1) {
				fsfx_PlaySound(FLIGHT_SOUND_DANGER_WARNING, -1,
					       playerIdx);
				msg_emitInFlightMessage(
					IFMSG_216_LEAVING_NOW_IS_2000_POINT_PENALTY_PRESS_SPACE_TO_QUIT_ANYWAY,
					playerIdx);
			} else {
				msg_emitInFlightMessage(
					IFMSG_215_PRESS_SPACE_TO_END_MISSION,
					playerIdx);
			}
			g_players[playerIdx].pendingActionId = 2;
			g_players[playerIdx].pendingActionParam = -1;
			g_players[playerIdx].pendingActionTimer = 1888;
		} else {
			msg_emitInFlightMessage(
				IFMSG_384_YOU_MUST_WAIT_UNTIL_THE_OTHER_PLAYERS_ARE_FINISHED,
				playerIdx);
		}
		return;
	case FLIGHT_KEY_R:
		Player_SetTarget(
			Player_FindNearestEnemyFighter(playerIdx, UINT16_MAX),
			playerIdx);
		return;
	case FLIGHT_KEY_T:
		if (g_players[playerIdx].currentTargetObjectIdx != -1) {
			Player_SetTarget(
				Player_CycleTargetAnyIFF(
					g_players[playerIdx]
						.currentTargetObjectIdx,
					1, playerIdx),
				playerIdx);
		} else {
			Player_SetTarget(
				Player_CycleTargetAnyIFF(
					g_players[playerIdx].targetCycleStart,
					1, playerIdx),
				playerIdx);
		}
		return;
	case FLIGHT_KEY_Y:
		if (g_players[playerIdx].currentTargetObjectIdx != -1) {
			Player_SetTarget(
				Player_CycleTargetAnyIFF(
					g_players[playerIdx]
						.currentTargetObjectIdx,
					-1, playerIdx),
				playerIdx);
		} else {
			Player_SetTarget(
				Player_CycleTargetAnyIFF(
					g_players[playerIdx].targetCycleStart,
					-1, playerIdx),
				playerIdx);
		}
		return;
	case FLIGHT_KEY_U: {
		int candidate;
		int newestTarget = -1;
		uint16_t newestAge = UINT16_MAX;
		for (candidate = g_activeRegionObjectSlotStart;
		     candidate < g_activeRegionCraftObjectSlotEnd;
		     ++candidate) {
			ObjectRecord *object = &g_objectTable[candidate];
			CraftData *candidateCraft;
			if (object->objectType == 0 ||
			    candidate == objectIndex ||
			    object->genusId == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			candidateCraft = object->mobj->pCraft;
			if (candidateCraft->leader_obj_idx != UINT8_MAX ||
			    (candidateCraft->objectKind !=
				     CRAFT_OBJECT_KIND_ACTIVE &&
			     candidateCraft->objectKind !=
				     CRAFT_OBJECT_KIND_DISABLED &&
			     candidateCraft->objectKind !=
				     CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE) ||
			    Object_HasActiveDecoyBeam((uint16_t)candidate) !=
				    0) {
				continue;
			}
			if (newestAge > object->mobj->secondsAlive) {
				newestTarget = candidate;
				newestAge = object->mobj->secondsAlive;
			}
		}
		Player_SetTarget(newestTarget, playerIdx);
		return;
	}
	case FLIGHT_KEY_ALT_C:
		g_players[playerIdx].currentTargetObjectIdx = -1;
		if (g_players[playerIdx].viewState.transitionTimer != 0) {
			PlayerViewState *view = &g_players[playerIdx].viewState;
			view->transitionTimer = 0;
			view->externalCameraActive = 0;
			view->playerInputBlocked = 0;
			view->cameraFocusObjIdx =
				(uint16_t)g_players[playerIdx].objectIndex;
			if (playerIdx == g_localPlayer) {
				g_hudCachedTargetObjectIdx = -2;
				g_renderObjectRefFlags = 0;
			}
			Hud_SetHudViewState(HUD_VIEW_FORWARD, playerIdx);
			view->hudAimX = 0;
			view->hudAimY = 0;
		}
		return;
	case FLIGHT_KEY_ALT_Q:
		if (g_players[playerIdx].participationState != 1) {
			fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
				       playerIdx);
			fsfx_PlaySound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
				       playerIdx);
			fsfx_PlaySound(FLIGHT_SOUND_DANGER_WARNING, -1,
				       playerIdx);
			if (NetSession_GetHostDplayId() ==
			    g_players[playerIdx].network.directPlayId) {
				msg_emitInFlightMessage(
					IFMSG_219_WARNING_YOU_ARE_THE_HOST_PRESSING_SPACE_WILL_ABORT_THIS_GAME,
					playerIdx);
			} else {
				msg_emitInFlightMessage(
					IFMSG_218_WARNING_PRESSING_SPACE_WILL_DISCONNECT_FROM_THE_HOST,
					playerIdx);
			}
			g_players[playerIdx].pendingActionId = 2;
			g_players[playerIdx].pendingActionParam = -1;
			g_players[playerIdx].pendingActionTimer = 1888;
		}
		return;
	case FLIGHT_KEY_F1:
		if (g_players[playerIdx].currentTargetObjectIdx != -1) {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx]
						.currentTargetObjectIdx,
					1, playerIdx, 2, 5),
				playerIdx);
		} else {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx].targetCycleStart,
					1, playerIdx, 2, 5),
				playerIdx);
		}
		return;
	case FLIGHT_KEY_F2:
		if (g_players[playerIdx].currentTargetObjectIdx != -1) {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx]
						.currentTargetObjectIdx,
					-1, playerIdx, 2, 5),
				playerIdx);
		} else {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx].targetCycleStart,
					-1, playerIdx, 2, 5),
				playerIdx);
		}
		return;
	case FLIGHT_KEY_F3:
		if (g_players[playerIdx].currentTargetObjectIdx != -1) {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx]
						.currentTargetObjectIdx,
					1, playerIdx, 3, 5),
				playerIdx);
		} else {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx].targetCycleStart,
					1, playerIdx, 3, 5),
				playerIdx);
		}
		return;
	case FLIGHT_KEY_F4:
		if (g_players[playerIdx].currentTargetObjectIdx != -1) {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx]
						.currentTargetObjectIdx,
					-1, playerIdx, 3, 5),
				playerIdx);
		} else {
			Player_SetTarget(
				Player_CycleTarget(
					g_players[playerIdx].targetCycleStart,
					-1, playerIdx, 3, 5),
				playerIdx);
		}
		return;
	case FLIGHT_KEY_F5:
	case FLIGHT_KEY_F6:
	case FLIGHT_KEY_F7: {
		int targetIndex = g_players[playerIdx]
					  .targetPresetSlot[g_currentActionKey -
							    FLIGHT_KEY_F5];
		if (targetIndex != -1 &&
		    g_objectTable[targetIndex].objectType != 0 &&
		    Object_HasActiveDecoyBeam((uint16_t)targetIndex) == 0) {
			Player_SetTarget(targetIndex, playerIdx);
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_F5:
	case FLIGHT_KEY_SHIFT_F6:
	case FLIGHT_KEY_SHIFT_F7:
		if (g_players[playerIdx].currentTargetObjectIdx == -1) {
			fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
		} else {
			g_players[playerIdx]
				.targetPresetSlot[g_currentActionKey -
						  FLIGHT_KEY_SHIFT_F5] =
				g_players[playerIdx].currentTargetObjectIdx;
			fsfx_PlaySound(FLIGHT_SOUND_TARGET_SELECTED, -1,
				       playerIdx);
		}
		return;
	case FLIGHT_KEY_LEFT:
	case FLIGHT_KEY_RIGHT: {
		int16_t page;
		int16_t pageFound = 0;
		if (playerIdx != g_localPlayer ||
		    g_flightSimSideEffectsSuppressed != 0) {
			return;
		}
		if (g_mfdActivePage == MFD_PAGE_NONE) {
			for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT;
			     ++page) {
				if (g_mfdPageStates[page] ==
				    MFD_PAGE_STATE_OPEN) {
					pageFound = 1;
					break;
				}
			}
			if (pageFound != 0) {
				g_mfdActivePage = page;
			}
			return;
		}
		for (page = g_mfdActivePage + 1; page <= MFD_PAGE_COUNT;
		     ++page) {
			if (page == MFD_PAGE_COUNT) {
				page = MFD_PAGE_SCOREBOARD;
			}
			if (g_mfdPageStates[page] == MFD_PAGE_STATE_OPEN) {
				pageFound = 1;
				break;
			}
		}
		if (pageFound != 0 && page != g_mfdActivePage) {
			g_mfdSecondaryPage = g_mfdActivePage;
			g_mfdActivePage = page;
		}
		return;
	}
	default:
		return;
	}
}

/* Sets g_graphicsDetailDistanceThreshold, g_starGridDivisor, g_backdropsEnabled
 * and g_debrisEnabled from the preset's entry in the ByPreset tables, and
 * g_transformLightDirectionToObjectSpace to 1; returns g_debrisEnabled. Does
 * not check that the preset is 0 to 3. */
// FUNCTION: XVT 0x484160
char Flight_ApplyGraphicsDetailPreset(uint16_t preset)
{
	g_graphicsDetailDistanceThreshold =
		g_graphicsDetailDistanceThresholdByPreset[preset];
	g_starGridDivisor = g_starGridDivisorByGraphicsDetailPreset[preset];
	g_backdropsEnabled =
		(uint8_t)g_backdropsEnabledByGraphicsDetailPreset[preset];
	g_debrisEnabled =
		(uint8_t)g_debrisEnabledByGraphicsDetailPreset[preset];
	g_transformLightDirectionToObjectSpace = 1;
	return (char)g_debrisEnabled;
}

#ifndef XVT_MODERN
/* A placeholder: the original WinMain is not rebuilt, and this returns 0. Only
 * the original build has it, and no engine code calls it. */
// FUNCTION: XVT 0x4A9B80
int WinMain(void *hInstance, void *hPrevInstance, char *lpCmdLine, int nShowCmd)
{
	/* Original WinMain; the modern port never calls it (host shell owns the loop). */
	(void)hInstance;
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nShowCmd;

	/* TODO: Reimplement WinMain @ 0x4A9B80. */
	return 0;
}
#endif

#ifndef XVT_MODERN
/* Runs a flight from the launch command line; returns 1 after it ends, 0 when
 * it cannot start (no command line, too few arguments, no network session, no
 * display or no sound). It loads the configuration; sets g_flightConfFlicker by
 * whether flicker.txt exists, g_laserFireTimestampTrackingEnabled and
 * g_internetPlayEnabled; reads the launch options found anywhere in the line
 * ("traincourse", "nopilot", "dinput", "sfx", "music", "voice", "tickcounter"
 * and "mipmaps", each with a "no" form, "inprogress", "newnet", "nolauncher",
 * "fullscreen" and "pageflip" with their "no" forms, '-' or "/+" at the start);
 * takes the main window; splits the line at spaces, '~' quoting text with
 * spaces, into the 7 g_flightLaunchArgs arguments; starts the network session;
 * takes the display, detail and lighting options for solo or multiplayer from
 * g_gameConfig; sets up the display (FlightDisplay_Init) and writes the mode it
 * got back into the configuration; starts DirectInput and sound (error 13 when
 * sound fails); then runs the mission named in the first argument
 * (Flight_MainLoop), shuts sound, input, network and 3D down, clears and
 * releases the surfaces and palette, and hands drawing back to the frontend
 * (g_flightRenderToFrontend 1). Only the original build has this function. */
// FUNCTION: XVT 0x4A9C00
int Flight_Main(char *missionCmdLine)
{
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

	XvtFile *flickerFile;
	NetworkTransportType networkType;
	const char *connectionAddress;
	int argumentCount;
	int argumentIndex;
	int commandLineOffset;
	int quotedArgument;
	int brightnessLimit;
	char *optionMatch;

	ModelPreview_FreeResources();
	g_flightRenderToFrontend = 0;
	if (missionCmdLine == NULL) {
		return 0;
	}

	Config_Load();
	flickerFile = File_RawOpen("flicker.txt", "r");
	if (flickerFile != NULL) {
		File_RawClose(flickerFile);
		g_flightConfFlicker = 0;
	} else {
		g_flightConfFlicker = 1;
	}

	g_laserFireTimestampTrackingEnabled = 1;
	g_internetPlayEnabled = g_gameConfig.internetPlay;
	optionMatch = strstr(missionCmdLine, "traincourse");
	g_flightConfTrainCourse = 1;
	if (optionMatch == NULL) {
		g_flightConfTrainCourse = 0;
	}
	optionMatch = strstr(missionCmdLine, "nopilot");
	g_flightConfNoPilot = 1;
	if (optionMatch == NULL) {
		g_flightConfNoPilot = 0;
	}
	if (strstr(missionCmdLine, "nodinput") != NULL) {
		g_flightConfDirectInput = 0;
	} else if (strstr(missionCmdLine, "dinput") != NULL) {
		g_flightConfDirectInput = 1;
	} else {
		g_flightConfDirectInput = 1;
	}
	if (strstr(missionCmdLine, "nosfx") != NULL) {
		g_flightConfSfxEnabled = 0;
	} else if (strstr(missionCmdLine, "sfx") != NULL) {
		g_flightConfSfxEnabled = 1;
	} else {
		g_flightConfSfxEnabled = 1;
	}
	if (strstr(missionCmdLine, "nomusic") != NULL) {
		g_flightConfMusicEnabled = 0;
	} else if (strstr(missionCmdLine, "music") != NULL) {
		g_flightConfMusicEnabled = 1;
	} else {
		g_flightConfMusicEnabled = 1;
	}
	if (strstr(missionCmdLine, "novoice") != NULL) {
		g_flightConfVoiceEnabled = 0;
	} else if (strstr(missionCmdLine, "voice") != NULL) {
		g_flightConfVoiceEnabled = 1;
	} else {
		g_flightConfVoiceEnabled = 1;
	}
	if (strstr(missionCmdLine, "notickcounter") != NULL) {
		g_flightConfTickCounterEnabled = 0;
	} else if (strstr(missionCmdLine, "tickcounter") != NULL) {
		g_flightConfTickCounterEnabled = 1;
	} else {
		g_flightConfTickCounterEnabled = 0;
	}
	if (strstr(missionCmdLine, "nomipmaps") != NULL) {
		g_mipmappingEnabled = 0;
	} else if (strstr(missionCmdLine, "mipmaps") != NULL) {
		g_mipmappingEnabled = 1;
	} else {
		g_mipmappingEnabled = 1;
	}
	optionMatch = strstr(missionCmdLine, "inprogress");
	g_flightInProgressLaunch = 1;
	if (optionMatch == NULL) {
		g_flightInProgressLaunch = 0;
	}
	optionMatch = strstr(missionCmdLine, "newnet");
	g_flightConfNewNet = 1;
	if (optionMatch == NULL) {
		g_flightConfNewNet = 0;
	}
	optionMatch = strstr(missionCmdLine, "nolauncher");
	g_flightConfNoLauncher = 1;
	if (optionMatch == NULL) {
		g_flightConfNoLauncher = 0;
	}
	if (strstr(missionCmdLine, "nofullscreen") != NULL) {
		g_flightFullscreen = 0;
	} else if (strstr(missionCmdLine, "fullscreen") != NULL) {
		g_flightFullscreen = 1;
	}
	if (strstr(missionCmdLine, "nopageflip") != NULL) {
		g_flightPageFlip = 0;
	} else if (strstr(missionCmdLine, "pageflip") != NULL) {
		g_flightPageFlip = 1;
	}
	if (missionCmdLine[0] == '-') {
		g_flightStartedWithDashArg = 1;
	} else if (missionCmdLine[0] == '/' && missionCmdLine[1] == '+') {
		g_unusedFlightCmdLinePlusSwitchFlag = 1;
	}

	if (Flight_UpdateAndFocusMainWindow() == 0) {
		return 0;
	}

	commandLineOffset = 0;
	quotedArgument = 0;
	argumentCount = BUILTIN_ARGUMENT_COUNT;
	g_flightLaunchArgs.programName = "xtie";
	g_flightLaunchArgs.sentinel = "/trebla";
	if (missionCmdLine[0] != '\0') {
		for (argumentIndex = 0; argumentIndex < PARSED_ARGUMENT_COUNT;
		     ++argumentIndex) {
			g_flightLaunchArgs.arguments[argumentIndex] =
				&missionCmdLine[commandLineOffset];
			while (1) {
				char character;

				character = missionCmdLine[commandLineOffset];
				if (character == ' ') {
					if (quotedArgument != 1) {
						break;
					}
				} else if (character == '\0') {
					break;
				}
				if (character == '~') {
					if (quotedArgument != 0) {
						quotedArgument = 0;
						missionCmdLine
							[commandLineOffset] =
								'\0';
						++commandLineOffset;
					} else {
						quotedArgument = 1;
						++commandLineOffset;
						g_flightLaunchArgs.arguments
							[argumentIndex] =
							&missionCmdLine
								[commandLineOffset];
					}
				} else {
					++commandLineOffset;
				}
			}
			++argumentCount;
			if (missionCmdLine[commandLineOffset] == '\0') {
				break;
			}
			missionCmdLine[commandLineOffset] = '\0';
			++commandLineOffset;
			if (missionCmdLine[commandLineOffset] == '\0') {
				break;
			}
		}
	}
	if (argumentCount < REQUIRED_ARGUMENT_COUNT) {
		return 0;
	}

	networkType = (NetworkTransportType)g_gameConfig.networkType;
	switch (networkType) {
	case NET_TRANSPORT_TCPIP:
		connectionAddress = g_gameConfig.ipAddress;
		break;
	case NET_TRANSPORT_MODEM:
		connectionAddress = g_gameConfig.phoneNumber;
		break;
	default:
	case NET_TRANSPORT_IPX:
		connectionAddress = NULL;
		break;
	}
	if (NetSession_InitGameSession(
		    g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_FORMAL_NAME],
		    g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_PILOT_NAME],
		    atoi(g_flightLaunchArgs
				 .arguments[FLIGHT_LAUNCH_ARG_IS_HOST]),
		    g_flightLaunchArgs
			    .arguments[FLIGHT_LAUNCH_ARG_MP_GAME_NAME],
		    networkType,
		    atoi(g_flightLaunchArgs
				 .arguments[FLIGHT_LAUNCH_ARG_NUM_PLAYERS]),
		    g_flightInProgressLaunch, connectionAddress) == 0) {
		NetSession_Shutdown();
		return 0;
	}

	g_flightBrightnessScaleQ8 =
		(g_gameConfig.brightness[NetSession_GetPlayerCount() > 1] +
		 BRIGHTNESS_CONFIG_OFFSET)
		<< BRIGHTNESS_CONFIG_SHIFT;
	brightnessLimit = BRIGHTNESS_SCALE_MIN;
	if ((unsigned int)g_flightBrightnessScaleQ8 < BRIGHTNESS_SCALE_MIN) {
		g_flightBrightnessScaleQ8 = brightnessLimit;
	} else {
		brightnessLimit = BRIGHTNESS_SCALE_MAX;
		if ((unsigned int)g_flightBrightnessScaleQ8 >
		    BRIGHTNESS_SCALE_MAX) {
			g_flightBrightnessScaleQ8 = brightnessLimit;
		}
	}
	g_backdropsEnabled =
		g_gameConfig.backdrop[NetSession_GetPlayerCount() > 1];
	g_debrisEnabled = g_gameConfig.debris[NetSession_GetPlayerCount() > 1];
	switch (g_gameConfig.starDensity[NetSession_GetPlayerCount() > 1]) {
	case 0:
		g_starGridDivisor = STAR_GRID_DIVISOR_LOW_DENSITY;
		break;
	case 1:
		g_starGridDivisor = STAR_GRID_DIVISOR_MEDIUM_DENSITY;
		break;
	case 2:
		g_starGridDivisor = STAR_GRID_DIVISOR_HIGH_DENSITY;
		break;
	default:
		break;
	}
	g_useHardware3D =
		g_gameConfig.use3dHardware[NetSession_GetPlayerCount() > 1];
	g_bilinearEnabled =
		g_gameConfig.bilinear[NetSession_GetPlayerCount() > 1];
	{
		int bppConfigValue;

		bppConfigValue =
			g_gameConfig
				.colorDepthChoice[NetSession_GetPlayerCount() >
						  1];
		switch (bppConfigValue) {
		case DISPLAY_CONFIG_LOW:
			g_flightBytesPerPixel = PALETTED_BYTES_PER_PIXEL;
			break;
		case DISPLAY_CONFIG_MEDIUM:
			g_flightBytesPerPixel = HIGH_COLOR_BYTES_PER_PIXEL;
			break;
		default:
			g_flightBytesPerPixel = PALETTED_BYTES_PER_PIXEL;
			break;
		}
	}
	NetSession_GetPlayerCount();
	{
		int lodConfigValue;

		lodConfigValue =
			g_gameConfig.lod[NetSession_GetPlayerCount() > 1] +
			LOD_CONFIG_OFFSET;
		g_lodDistanceScale = (float)lodConfigValue;
		if (g_lodDistanceScale > g_lodConfigMaxValue) {
			g_lodDistanceScale = (float)LOD_CONFIG_MAX_VALUE;
		}
		g_lodDistanceScale =
			g_lodDistanceScale * g_lodConfigScaleFactor;
		g_lodDistanceScale =
			g_lodDistanceScale * g_lodConfigCurveDouble;
		if (g_lodDistanceScale > g_lodConfigCurveThreshold) {
			g_lodDistanceScale =
				g_lodConfigCurveThreshold /
				(g_lodConfigCurveDouble - g_lodDistanceScale);
		}
		g_forcedLodLevel = 0;
		g_lodDistanceScale = (float)LOD_SCALE_INVERSION_NUMERATOR /
				     g_lodDistanceScale;
	}
	{
		int mipmapConfigOption;

		mipmapConfigOption =
			g_gameConfig.mipmap[NetSession_GetPlayerCount() > 1];
		if (mipmapConfigOption != MIPMAPPING_DISABLED_VALUE) {
			int64_t mipmapConfigValue;

			mipmapConfigValue =
				g_gameConfig
					.mipmap[NetSession_GetPlayerCount() >
						1];
			g_mipLodScale = (float)mipmapConfigValue;
			g_mipLodScale =
				g_mipLodScale * g_mipmapConfigScaleFactor;
			g_mipLodScale = g_mipLodScale * g_lodConfigCurveDouble;
			if (g_mipLodScale > g_lodConfigCurveThreshold) {
				g_mipLodScale = g_lodConfigCurveThreshold /
						(g_lodConfigCurveDouble -
						 g_mipLodScale);
			}
			g_mipmappingEnabled = 1;
			g_mipLodScale = (float)LOD_SCALE_INVERSION_NUMERATOR /
					g_mipLodScale;
		} else {
			g_mipmappingEnabled = 0;
		}
	}
	switch (g_gameConfig.textureRes[NetSession_GetPlayerCount() > 1]) {
	case 0:
		g_textureResolutionLevel = 0;
		break;
	case 1:
		g_textureResolutionLevel = 1;
		break;
	default:
		g_textureResolutionLevel = 2;
		break;
	}
	{
		int localLightsEnabled;

		localLightsEnabled =
			g_gameConfig
				.localLights[NetSession_GetPlayerCount() > 1];
		g_localLightsEnabled = 1;
		if (localLightsEnabled == 0) {
			g_localLightsEnabled = 0;
		}
	}
	{
		int specularEnabled;

		specularEnabled =
			g_gameConfig.specular[NetSession_GetPlayerCount() > 1];
		g_specularEnabled = 1;
		if (specularEnabled == 0) {
			g_specularEnabled = 0;
		}
	}
	{
		int diffuseLightingEnabled;

		diffuseLightingEnabled =
			g_gameConfig.diffuse[NetSession_GetPlayerCount() > 1];
		g_dirLightingEnabled = 1;
		if (diffuseLightingEnabled == 0) {
			g_dirLightingEnabled = 0;
		}
	}
	{
		int ditheringEnabled;

		ditheringEnabled =
			g_gameConfig.dither[NetSession_GetPlayerCount() > 1];
		g_ditheringEnabled = 1;
		if (ditheringEnabled == 0) {
			g_ditheringEnabled = 0;
		}
	}
	switch (g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1]) {
	case DISPLAY_CONFIG_LOW:
		g_displayModeWidth = DISPLAY_WIDTH_LOW;
		g_displayModeHeight = DISPLAY_HEIGHT_LOW;
		break;
	case DISPLAY_CONFIG_MEDIUM:
		g_displayModeWidth = DISPLAY_WIDTH_MEDIUM;
		g_displayModeHeight = DISPLAY_HEIGHT_MEDIUM;
		break;
	default:
		g_displayModeWidth = DISPLAY_WIDTH_HIGH;
		g_displayModeHeight = DISPLAY_HEIGHT_HIGH;
		break;
	}
	switch (g_gameConfig.windowSize[NetSession_GetPlayerCount() > 1]) {
	case DISPLAY_CONFIG_LOW:
		g_surfaceWidth = DISPLAY_WIDTH_LOW;
		g_surfaceHeight = DISPLAY_HEIGHT_LOW;
		break;
	case DISPLAY_CONFIG_MEDIUM:
		g_surfaceWidth = WINDOW_WIDTH_MEDIUM;
		g_surfaceHeight = WINDOW_HEIGHT_MEDIUM;
		break;
	default:
		g_surfaceWidth = DISPLAY_WIDTH_HIGH;
		g_surfaceHeight = DISPLAY_HEIGHT_HIGH;
		break;
	}
	g_renderTargetWidth = g_displayModeWidth;
	g_requestedFlightBytesPerPixel = g_flightBytesPerPixel;
	g_requestedFlightHardware3D = g_useHardware3D;
	if (FlightDisplay_Init() == 0) {
		return 0;
	}

	switch (g_displayModeWidth) {
	case DISPLAY_WIDTH_LOW:
		g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1] =
			DISPLAY_CONFIG_LOW;
		break;
	case DISPLAY_WIDTH_MEDIUM:
		g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1] =
			DISPLAY_CONFIG_MEDIUM;
		break;
	case DISPLAY_WIDTH_HIGH:
		g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1] =
			DISPLAY_CONFIG_HIGH;
		break;
	default:
		break;
	}
	switch (g_flightBytesPerPixel) {
	case PALETTED_BYTES_PER_PIXEL:
		g_gameConfig.colorDepthChoice[NetSession_GetPlayerCount() > 1] =
			DISPLAY_CONFIG_LOW;
		break;
	case HIGH_COLOR_BYTES_PER_PIXEL:
		g_gameConfig.colorDepthChoice[NetSession_GetPlayerCount() > 1] =
			DISPLAY_CONFIG_MEDIUM;
		break;
	default:
		break;
	}
	g_gameConfig.use3dHardware[NetSession_GetPlayerCount() > 1] =
		(uint8_t)g_useHardware3D;
	DebugPrintf("Init Dinput\n");
	if (g_flightConfDirectInput != 0 && DInput_Init() == 0) {
		g_flightConfDirectInput = 0;
	}
	DebugPrintf("Init Dsound\n");
	g_flightSoundInitStartTimeMs = timeGetTime();
	if (Sound_Init_Sound_Engine(g_flightMainWindowHandle) == 0) {
		FlightDisplay_CleanupAndReportError(DISPLAY_INIT_SOUND_ERROR);
		return 0;
	}

	strcpy(g_currentMissionFile,
	       g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_MISSION_PATH]);
	Flight_MainLoop(0);
	g_sw3dSkipOddScanlines = 0;
	Sound_Shutdown_Sound_Engine();
	if (g_flightConfDirectInput != 0) {
		DInput_Shutdown();
	}
	NetSession_Shutdown();
	if (g_useHardware3D != 0) {
		std3D_DetachAndReleaseZBufferSurface();
		std3D_Close();
		std3D_Shutdown();
	}
	if (g_flightFullscreen != 0) {
		FlightDisplay_ClearSurface(g_flightPrimarySurface);
		if (g_flightPageFlip != 0) {
			FlightDisplay_ClearSurface(g_flightBackBuffer);
			FlightDisplay_ClearSurface(g_flightOffscreenSurface);
		}
	}
	if (g_flightPrimarySurface != NULL) {
		g_flightPrimarySurface->lpVtbl->Release(g_flightPrimarySurface);
		g_flightPrimarySurface = NULL;
	}
	if (g_flightPalette != NULL) {
		g_flightPalette->lpVtbl->Release(g_flightPalette);
		g_flightPalette = NULL;
	}
	if (g_flightPageFlip != 0 && g_flightOffscreenSurface != NULL) {
		g_flightOffscreenSurface->lpVtbl->Release(
			g_flightOffscreenSurface);
		g_flightOffscreenSurface = NULL;
	}
	g_flightRenderToFrontend = 1;
	g_useHardware3D = 0;
	return 1;
}
#endif

/* Sets g_flightMainWindowHandle to the frontend's main window and returns 1;
 * the original build also updates the window and gives it the focus. */
// FUNCTION: XVT 0x4AA6F0
int Flight_UpdateAndFocusMainWindow(void)
{
#ifdef XVT_MODERN
	g_flightMainWindowHandle = FrontendDisplay_GetMainWindowHandle();
#else
	UpdateWindow(g_flightMainWindowHandle =
			     FrontendDisplay_GetMainWindowHandle());
	SetFocus(g_flightMainWindowHandle);
#endif
	return 1;
}

/* In the original build, brings the main window to the front when it is not
 * there (resetting an 8-bit palette, FlightPalette_ResetIf8Bit, and hiding the
 * cursor), then takes one waiting window message and dispatches it unless it is
 * 0x06, 0x08, 0x1C, 0x1F or 0x86. Returns the dispatch's result, the message
 * number when not dispatched, or 0 with no message. The modern build returns
 * 0. */
// FUNCTION: XVT 0x4AA720
int32_t Flight_PumpWindowMessages(void)
{
#ifdef XVT_MODERN
	return 0;
#else
	struct FlightWin32Message message;
	int32_t result;

	if (GetForegroundWindow() != g_flightMainWindowHandle) {
		SetForegroundWindow(g_flightMainWindowHandle);
		FlightPalette_ResetIf8Bit();
		while (ShowCursor(0) >= 0) {
		}
	}

	result = PeekMessageA(&message, NULL, 0, 0, 1);
	if (result != 0) {
		result = (int32_t)message.message;
		if (message.message != 0x1C && message.message != 0x08 &&
		    message.message != 0x06 && message.message != 0x1F &&
		    message.message != 0x86) {
			TranslateMessage(&message);
			result = DispatchMessageA(&message);
		}
	}
	return result;
#endif
}

/* Flight's part of the window procedure, called by FrontendDisplay_WndProc:
 * resets an 8-bit palette on message 0x311 (FlightPalette_ResetIf8Bit), and in
 * the original build passes message 0x0F to DefWindowProcA and returns its
 * result. Returns 0 otherwise. */
// FUNCTION: XVT 0x4AA7B0
int32_t Flight_WndProc(void *hWnd, unsigned int Msg, uint32_t wParam,
		       int32_t lParam)
{
	if (Msg == 0x311) {
		FlightPalette_ResetIf8Bit();
	}
#ifndef XVT_MODERN
	if (Msg == 0x0f) {
		return DefWindowProcA(hWnd, Msg, wParam, lParam);
	}
#else
	(void)hWnd;
	(void)wParam;
	(void)lParam;
#endif
	return 0;
}

/* Steers each craft in the active region's craft slots, or only
 * g_singleObjectUpdateOverrideIdx when that is set, and sets its speed, over
 * g_elapsedTicks plus the ticks its own state lags g_gameTime
 * (simStateTimestamp) when that is set, else over g_elapsedTicks. An AI craft
 * (no player owner) with working systems and not held by a beam rolls, pitches
 * and turns toward its AI controller's targets at its flight rates, banking as
 * it turns; pitching through a loop turns its yaw and roll half a circle.
 * Climbs end and dives pull out (Flight_UpdateDivePulloutPitchTarget). An
 * active craft's speed moves toward its commanded speed or its throttle's share
 * of a top speed that rises or falls with the energy settings and doubles
 * unless engineOverdriveOff is set; a player's throttle counts only with
 * working engines. Breaking up and exploding craft slow to their top speed,
 * disabled craft slow by 20 per simulated second, and craft entering hyperspace
 * speed up by 50, 200 or 500 per simulated second. A changed orientation is
 * marked for recomputing, and a carried object takes the craft's orientation.
 * Sets g_curCraft and g_curCraftModelIndex, and puts g_elapsedTicks and
 * g_simStepsPerSecond back at the end. The modern build steers with its own
 * integration when the timing is unlocked. */
// FUNCTION: XVT 0x4ACE80
void Flight_UpdateCraftSteeringAndSpeed(void)
{
	AiController *controller;
	int playerOwner;
	int simulationRate;
	int savedSimStepsPerSecond;
	uint16_t objectIndex;
	int overrideProcessed;
	int savedElapsedTicks;
	uint16_t throttleFraction;
	uint16_t oldPitch;
	uint16_t oldYaw;
	uint16_t oldRoll;
	int objectIdx;

	simulationRate = SIMULATION_TICKS_PER_SECOND;
	savedSimStepsPerSecond = g_simStepsPerSecond;
	objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
	overrideProcessed = 0;
	savedElapsedTicks = g_elapsedTicks;
	for (; objectIndex < g_activeRegionCraftObjectSlotEnd; ++objectIndex) {
		if (g_singleObjectUpdateOverrideIdx != -1) {
			if (overrideProcessed != 0) {
				break;
			}
			overrideProcessed = 1;
			objectIndex = (uint16_t)g_singleObjectUpdateOverrideIdx;
		}
		g_simStepsPerSecond = (uint16_t)savedSimStepsPerSecond;
		g_elapsedTicks = (uint16_t)savedElapsedTicks;
		objectIdx = objectIndex;
		if (g_objectTable[objectIdx].mobj != NULL) {
			if (g_objectTable[objectIdx].mobj->simStateTimestamp !=
			    0) {
				g_elapsedTicks =
					(uint16_t)(g_elapsedTicks + g_gameTime -
						   g_objectTable[objectIdx]
							   .mobj
							   ->simStateTimestamp);
				if (g_elapsedTicks == 0) {
					continue;
				}
				g_simStepsPerSecond =
					(uint16_t)(SIMULATION_TICKS_PER_SECOND /
						   g_elapsedTicks);
				if (g_simStepsPerSecond == 0) {
					g_simStepsPerSecond = 1;
				}
			}
		}
		if (g_objectTable[objectIdx].objectType == 0 ||
		    g_objectTable[objectIdx].mobj->family != 0) {
			continue;
		}

		throttleFraction = 0;
		g_curCraft = g_objectTable[objectIdx].mobj->pCraft;
		controller = &g_curCraft->aiController;
		oldPitch = g_objectTable[objectIdx].pitch;
		oldYaw = g_objectTable[objectIdx].yaw;
		oldRoll = g_objectTable[objectIdx].roll;
		g_curCraftModelIndex = g_curCraft->modelIndex;
		playerOwner = g_objectTable[objectIdx].playerOwnerIdx;
#ifdef XVT_MODERN
		if (XvtFlightTiming_IsUnlocked()) {
			if (playerOwner != -1 ||
			    !g_curCraft->workingSubsystems ||
			    g_curCraft->beamEffectAccum[1]) {
				XvtFlightIntegration_Clear(objectIdx,
							   XVT_INTEGRATE_ROLL);
				XvtFlightIntegration_Clear(objectIdx,
							   XVT_INTEGRATE_PITCH);
				XvtFlightIntegration_Clear(objectIdx,
							   XVT_INTEGRATE_TURN);
				XvtFlightIntegration_Clear(objectIdx,
							   XVT_INTEGRATE_BANK);
			}

			if (g_curCraft->aiFlight.rollState < 1 ||
			    g_curCraft->aiFlight.rollState > 3) {
				XvtFlightIntegration_Clear(objectIdx,
							   XVT_INTEGRATE_ROLL);
			}
			if (g_curCraft->aiFlight.pitchState != 1 &&
			    g_curCraft->aiFlight.pitchState != 2) {
				XvtFlightIntegration_Clear(objectIdx,
							   XVT_INTEGRATE_PITCH);
			}
			if (!g_curCraft->aiFlight.turnState ||
			    controller->targetXYAngle ==
				    g_objectTable[objectIdx].yaw) {
				XvtFlightIntegration_Clear(objectIdx,
							   XVT_INTEGRATE_TURN);
			}
		}
#endif
		if (playerOwner != -1) {
			if ((g_curCraft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_ENGINES) != 0) {
				throttleFraction = g_curCraft->throttleSpeed;
			}
		} else if (g_curCraft->workingSubsystems != 0) {
			throttleFraction = g_curCraft->throttleSpeed;
		}

		if (playerOwner == -1 && g_curCraft->workingSubsystems != 0 &&
		    g_curCraft->beamEffectAccum[1] == 0) {
			uint8_t rollState;

			rollState = g_curCraft->aiFlight.rollState;
			if (rollState >= 1 && rollState <= 3) {
				uint16_t rollDelta;
				uint16_t rawRollStep;
				uint16_t rollStep;

				rollDelta = (uint16_t)(controller->targetRoll -
						       g_objectTable[objectIdx]
							       .roll);
				rawRollStep =
					(uint16_t)((uint16_t)g_elapsedTicks *
						   (uint16_t)g_curCraft
							   ->aiFlight.rollRate /
						   simulationRate);
				rollStep = (uint16_t)MATH2_fraction(
					rawRollStep,
					(uint16_t)
						g_curCraft->aiFlight.rollAccel);
				rollStep =
					(uint16_t)(2 *
						   MATH2_fraction(
							   rollStep,
							   g_curCraft->aiFlight
								   .rollStep));
#ifdef XVT_MODERN
				if (XvtFlightTiming_IsUnlocked()) {
					rollStep =
						(uint16_t)(2 *
							   XvtFlightIntegration_Steer(
								   objectIdx,
								   XVT_INTEGRATE_ROLL,
								   g_curCraft
									   ->aiFlight
									   .rollRate,
								   g_curCraft
									   ->aiFlight
									   .rollAccel,
								   g_curCraft
									   ->aiFlight
									   .rollStep,
								   (int16_t)(controller
										     ->targetRoll -
									     g_objectTable[objectIdx]
										     .roll) <
										   0
									   ? -1
									   : 1));
				}
#endif
				if (g_curCraft->aiFlight.rollState != 3) {
					if (rollDelta < 0x8000u) {
						if (rollDelta <= rollStep) {
							g_objectTable[objectIdx]
								.roll =
								controller
									->targetRoll;
							g_curCraft->aiFlight
								.rollState = 4;
						} else {
							g_objectTable[objectIdx]
								.roll +=
								rollStep;
						}
					} else if ((uint16_t)-rollDelta >
						   rollStep) {
						g_objectTable[objectIdx].roll -=
							rollStep;
					} else {
						g_objectTable[objectIdx].roll =
							controller->targetRoll;
						g_curCraft->aiFlight.rollState =
							4;
					}
				} else if (controller->targetRoll < 0x8000u) {
					g_objectTable[objectIdx].roll +=
						rollStep;
				} else {
					g_objectTable[objectIdx].roll -=
						rollStep;
				}
			}

			if (g_curCraft->aiFlight.pitchState != 0) {
				uint16_t pitchDelta;
				uint16_t rawPitchStep;
				uint16_t pitchStep;

				pitchDelta =
					(uint16_t)(controller->targetZAngle -
						   g_curCraft->pitch);
				if (pitchDelta >= 0x8000u) {
					pitchDelta = (uint16_t)-pitchDelta;
				}
				rawPitchStep =
					(uint16_t)((uint16_t)g_elapsedTicks *
						   (uint16_t)
							   g_curCraft->aiFlight
								   .pitchRate /
						   simulationRate);
				pitchStep = (uint16_t)MATH2_fraction(
					rawPitchStep,
					(uint16_t)g_curCraft->aiFlight
						.pitchAccel);
				pitchStep = (uint16_t)MATH2_fraction(
					pitchStep,
					g_curCraft->aiFlight.pitchStepScale);
#ifdef XVT_MODERN
				if (XvtFlightTiming_IsUnlocked() &&
				    g_curCraft->aiFlight.pitchState <= 2) {
					pitchStep = (uint16_t)(XvtFlightIntegration_Steer(
						objectIdx, XVT_INTEGRATE_PITCH,
						g_curCraft->aiFlight.pitchRate,
						g_curCraft->aiFlight.pitchAccel,
						g_curCraft->aiFlight
							.pitchStepScale,
						g_curCraft->aiFlight.pitchState ==
								1
							? -1
							: 1));
				}
#endif
				(void)MATH2_fraction(pitchStep, 0x8000u);
				if (g_curCraft->aiFlight.pitchState == 1) {
					if (pitchStep < pitchDelta ||
					    g_curCraft->aiFlight
							    .pitchThroughLoop !=
						    0) {
						g_curCraft->pitch -= pitchStep;
						if (g_curCraft->pitch >=
						    0xE000u) {
							g_curCraft->pitch =
								(uint16_t)-g_curCraft
									->pitch;
							g_objectTable[objectIdx]
								.yaw -= 0x8000u;
							g_objectTable[objectIdx]
								.roll -=
								0x8000u;
							g_curCraft->aiFlight
								.pitchThroughLoop =
								0;
							g_curCraft->aiFlight
								.pitchState = 2;
						}
					} else {
						g_curCraft->pitch =
							controller
								->targetZAngle;
						g_curCraft->aiFlight
							.pitchState = 3;
					}
				} else if (g_curCraft->aiFlight.pitchState ==
					   2) {
					if (pitchStep < pitchDelta ||
					    g_curCraft->aiFlight
							    .pitchThroughLoop !=
						    0) {
						g_curCraft->pitch += pitchStep;
						if (g_curCraft->pitch >=
						    0x8000u) {
							g_curCraft->pitch =
								(uint16_t)-g_curCraft
									->pitch;
							g_objectTable[objectIdx]
								.yaw -= 0x8000u;
							g_objectTable[objectIdx]
								.roll -=
								0x8000u;
							g_curCraft->aiFlight
								.pitchThroughLoop =
								0;
							g_curCraft->aiFlight
								.pitchState = 1;
						}
					} else {
						g_curCraft->pitch =
							controller
								->targetZAngle;
						g_curCraft->aiFlight
							.pitchState = 3;
					}
				}
			}

			if (g_curCraft->objectKind !=
				    CRAFT_OBJECT_KIND_DISABLED &&
			    g_curCraft->aiFlight.turnState >= 1) {
				uint16_t turnDelta;

				turnDelta =
					(uint16_t)(controller->targetXYAngle -
						   g_objectTable[objectIdx]
							   .yaw);
				if (turnDelta != 0) {
					uint16_t rawTurnStep;
					uint16_t turnStep;

					rawTurnStep =
						(uint16_t)((uint16_t)
								   g_elapsedTicks *
							   (uint16_t)g_curCraft
								   ->aiFlight
								   .turnRate /
							   simulationRate);
					turnStep = (uint16_t)MATH2_fraction(
						rawTurnStep,
						(uint16_t)g_curCraft->aiFlight
							.turnAccel);
					turnStep = (uint16_t)MATH2_fraction(
						turnStep,
						g_curCraft->aiFlight.turnStep);
#ifdef XVT_MODERN
					if (XvtFlightTiming_IsUnlocked()) {
						turnStep = (uint16_t)(XvtFlightIntegration_Steer(
							objectIdx,
							XVT_INTEGRATE_TURN,
							g_curCraft->aiFlight
								.turnRate,
							g_curCraft->aiFlight
								.turnAccel,
							g_curCraft->aiFlight
								.turnStep,
							(int16_t)turnDelta < 0
								? -1
								: 1));
					}
#endif
					if (turnDelta < 0x8000u &&
					    turnStep < turnDelta) {
						g_objectTable[objectIdx].yaw +=
							turnStep;
					} else if (
						turnDelta >= 0x8000u &&
						turnStep <
							(uint16_t)-turnDelta) {
						g_objectTable[objectIdx].yaw -=
							turnStep;
					} else {
						g_objectTable[objectIdx].yaw =
							controller
								->targetXYAngle;
						turnStep = 0;
						g_curCraft->aiFlight.turnState =
							3;
					}
					if (g_curCraft->aiFlight.rollState ==
						    0 ||
					    g_curCraft->aiFlight.rollState ==
						    4) {
						int16_t bank;

						bank = (int16_t)MATH2_fraction(
							turnStep,
							g_modelDefs[g_curCraftModelIndex]
								.autoBankFactor);
#ifdef XVT_MODERN
						if (XvtFlightTiming_IsUnlocked()) {
							unsigned fraction =
								g_modelDefs[g_curCraftModelIndex]
									.autoBankFactor;
							int signedBank = XvtFlightIntegration_Rate(
								objectIdx,
								XVT_INTEGRATE_BANK,
								(int16_t)turnDelta <
										0
									? -(int)turnStep
									: turnStep,
								fraction == UINT16_MAX
									? 65536u
									: fraction,
								65536);
							bank = (int16_t)(signedBank < 0
										 ? -signedBank
										 : signedBank);
						}
#endif
						if (turnDelta < 0x8000u) {
							g_objectTable[objectIdx]
								.roll -= bank;
						} else {
							g_objectTable[objectIdx]
								.roll += bank;
						}
					}
				}
			}
		}

#ifdef XVT_MODERN
		if (XvtFlightTiming_ReferenceDue()) {
			XvtFlightClock decisionClock =
				XvtFlightTiming_EnterReference();
#endif
			if (g_objectTable[objectIdx].playerOwnerIdx == -1 &&
			    g_curCraft->aiFlight.climbState == 1 &&
			    g_curCraft->workingSubsystems != 0 &&
			    g_curCraft->beamEffectAccum[1] == 0 &&
			    controller->aimPointZ <=
				    g_objectTable[objectIdx].world_z) {
				g_curCraft->aiFlight.climbState = 0;
				g_curCraft->pitch = 0x4000;
			}
			if (g_objectTable[objectIdx].playerOwnerIdx == -1 &&
			    g_curCraft->aiFlight.diveState == 1 &&
			    g_curCraft->workingSubsystems != 0 &&
			    g_curCraft->beamEffectAccum[1] == 0) {
				Flight_UpdateDivePulloutPitchTarget(objectIdx);
			}

#ifdef XVT_MODERN
			XvtFlightTiming_RestoreClock(decisionClock);
		}
#endif

		switch (g_curCraft->objectKind) {
		case CRAFT_OBJECT_KIND_ACTIVE: {
			uint16_t commandedSpeed;
			uint16_t maxSpeed;
			int16_t speedBias;

			g_objectTable[objectIdx].pitch = g_curCraft->pitch;
			commandedSpeed = g_curCraft->commandedSpeed;
			if (commandedSpeed == 0 ||
			    throttleFraction != UINT16_MAX) {
				maxSpeed = g_curCraft->aiFlight.maxSpeedCache;
				speedBias =
					6 -
					(uint8_t)g_curCraft
						->shieldRechargeLevel -
					(uint8_t)g_curCraft->beamRechargeLevel -
					(uint8_t)g_curCraft->laserRechargeLevel;
				if (g_objectTable[objectIdx].objectType == 7) {
					maxSpeed += speedBias *
						    MATH2_fraction(maxSpeed,
								   0x1000u);
				} else if (g_objectTable[objectIdx]
							   .objectType == 5 &&
					   speedBias > 0) {
					maxSpeed += speedBias *
						    MATH2_fraction(maxSpeed,
								   0x3000u);
				} else {
					maxSpeed += speedBias *
						    MATH2_fraction(maxSpeed,
								   0x2000u);
				}
				commandedSpeed = (uint16_t)MATH2_fraction(
					maxSpeed, throttleFraction);
				if (g_curCraft->engineOverdriveOff == 0) {
					commandedSpeed += commandedSpeed;
				}
			}
			if (commandedSpeed <
			    g_objectTable[objectIdx].mobj->speed) {
				if ((unsigned int)(g_objectTable[objectIdx]
							   .mobj->speed -
						   commandedSpeed) < 200) {
					Flight_SlewObjectSpeedTowardTarget(
						objectIdx, commandedSpeed, 1,
						throttleFraction);
				} else {
					Flight_DecelerateObjectSpeed(
						objectIdx,
						(unsigned int)(g_objectTable[objectIdx]
								       .mobj
								       ->speed -
							       commandedSpeed) /
							3u);
				}
			} else {
				Flight_SlewObjectSpeedTowardTarget(
					objectIdx, commandedSpeed, 1,
					throttleFraction);
			}
			break;
		}
		case CRAFT_OBJECT_KIND_UNKNOWN_1:
		case CRAFT_OBJECT_KIND_BREAKING_UP:
		case CRAFT_OBJECT_KIND_EXPLODING:
			if (g_objectTable[objectIdx].mobj->speed >
			    (unsigned int)(uint16_t)
				    g_curCraft->aiFlight.maxSpeedCache) {
				Flight_DecelerateObjectSpeed(
					objectIdx,
					(g_objectTable[objectIdx].mobj->speed -
					 (uint16_t)g_curCraft->aiFlight
						 .maxSpeedCache) /
						3);
			}
			/* fall through */
		case CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE:
			g_curCraft->aiFlight.climbState = 0;
			g_curCraft->aiFlight.diveState = 0;
			g_curCraft->aiFlight.rollState = 0;
			g_curCraft->aiFlight.pitchState = 0;
			break;
		case CRAFT_OBJECT_KIND_DISABLED:
			if (g_objectTable[objectIdx].mobj->speed > 0) {
				Flight_DecelerateObjectSpeed(objectIdx, 20);
			}
			break;
		case CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE:
			if (g_curCraft->workingSubsystems != 0) {
				if (controller->secondaryManeuverTimer != 0) {
					Flight_AccelerateObjectSpeed(objectIdx,
								     50);
				} else if (controller->maneuverTimer != 0) {
					Flight_AccelerateObjectSpeed(objectIdx,
								     200);
				} else {
					Flight_AccelerateObjectSpeed(objectIdx,
								     500);
				}
			} else {
				if (g_objectTable[objectIdx].mobj->speed > 0) {
					Flight_DecelerateObjectSpeed(objectIdx,
								     20);
				}
				if (g_objectTable[objectIdx].mobj->speed == 0) {
					g_curCraft->objectKind =
						CRAFT_OBJECT_KIND_ACTIVE;
				}
			}
			break;
		default:
			break;
		}

		g_curCraft->yaw = g_objectTable[objectIdx].yaw;
		if (g_objectTable[objectIdx].pitch != oldPitch ||
		    g_objectTable[objectIdx].yaw != oldYaw ||
		    g_objectTable[objectIdx].roll != oldRoll) {
			g_objectTable[objectIdx].mobj->moveVectorDirty = 1;
			g_objectTable[objectIdx].mobj->orientMatrixDirty = 1;
		}
		if (g_curCraft->carriedObjectIndex != UINT16_MAX) {
			uint16_t carriedObjectIndex;

			carriedObjectIndex = g_curCraft->carriedObjectIndex;
			if (g_objectTable[carriedObjectIndex].mobj != NULL) {
				g_objectTable[carriedObjectIndex].pitch =
					g_objectTable[objectIdx].pitch;
				g_objectTable[carriedObjectIndex].yaw =
					g_objectTable[objectIdx].yaw;
				g_objectTable[carriedObjectIndex].roll =
					g_objectTable[objectIdx].roll;
				g_objectTable[carriedObjectIndex]
					.mobj->pCraft->pitch =
					g_curCraft->pitch;
				g_objectTable[carriedObjectIndex]
					.mobj->moveVectorDirty = 1;
				g_objectTable[carriedObjectIndex]
					.mobj->orientMatrixDirty = 1;
			}
		}
	}
	g_simStepsPerSecond = (uint16_t)savedSimStepsPerSecond;
	g_elapsedTicks = (uint16_t)savedElapsedTicks;
}

/* Moves an object's speed toward targetSpeed by one step's worth: when below,
 * it accelerates by at most a quarter of the craft model's accelRate (at least
 * 1) plus the rest of it times throttleFraction over 65,536, tripled unless
 * engineOverdriveOff is set; when above and allowDecel is 1, it decelerates by
 * at most a quarter of decelRate plus the rest times 65,535 less
 * throttleFraction over 65,536. The rates are per simulated second
 * (Flight_AccelerateObjectSpeed, Flight_DecelerateObjectSpeed). The model is
 * g_curCraftModelIndex's. */
// FUNCTION: XVT 0x4AD880
void Flight_SlewObjectSpeedTowardTarget(unsigned int objectIdx, int targetSpeed,
					int allowDecel, int throttleFraction)
{
	uint32_t speedDelta;

	speedDelta = targetSpeed;
	speedDelta -= g_objectTable[objectIdx].mobj->speed;
	if (speedDelta == 0) {
		return;
	}

	if (speedDelta < 0x8000u) {
		uint32_t step;

		step = (uint16_t)MATH2_fraction(
			g_modelDefs[g_curCraftModelIndex].accelRate, 0x4000u);
		if (step == 0) {
			step = 1;
		}
		step += (uint16_t)MATH2_fraction(
			(uint16_t)(g_modelDefs[g_curCraftModelIndex].accelRate -
				   step),
			throttleFraction);
		if (g_objectTable[objectIdx].mobj->pCraft->engineOverdriveOff ==
		    0) {
			step *= 3;
		}
		if (speedDelta >= step) {
			speedDelta = step;
		}
		Flight_AccelerateObjectSpeed(objectIdx, speedDelta);
	} else if (allowDecel == 1) {
		uint32_t step;

		step = (uint16_t)MATH2_fraction(
			g_modelDefs[g_curCraftModelIndex].decelRate, 0x4000u);
		if (step == 0) {
			step = 1;
		}
		step += (uint16_t)MATH2_fraction(
			(uint16_t)(g_modelDefs[g_curCraftModelIndex].decelRate -
				   step),
			(uint16_t)(0xFFFFu - throttleFraction));
		speedDelta = -speedDelta;
		if (speedDelta >= step) {
			speedDelta = step;
		}
		Flight_DecelerateObjectSpeed(objectIdx, speedDelta);
	}
}

/* Adds accelerationPerSecond times g_elapsedTicks over 236 to an object's mobj
 * speed, carrying the fraction in speedRemainder (units of 1/65,536), and caps
 * the speed at 3,600. Does not check that the object has a mobile object. */
// FUNCTION: XVT 0x4AD9E0
void Flight_AccelerateObjectSpeed(int objectIdx, int accelerationPerSecond)
{
	uint32_t product;
	uint32_t wholeQuotient;
	uint16_t wholeDelta;
	uint16_t fracDelta;
	uint16_t *speedRemainder;
	uint16_t oldRemainder;

	product = (uint32_t)(uint16_t)g_elapsedTicks *
		  (uint32_t)accelerationPerSecond;
	wholeQuotient = product / 236u;
	wholeDelta = (uint16_t)wholeQuotient;
	fracDelta = (uint16_t)(((product - wholeQuotient * 236u) << 16) / 236u);

	speedRemainder = &g_objectTable[objectIdx].mobj->speedRemainder;
	oldRemainder = *speedRemainder;
	*speedRemainder = (uint16_t)(oldRemainder + fracDelta);
	if (g_objectTable[objectIdx].mobj->speedRemainder < oldRemainder) {
		++g_objectTable[objectIdx].mobj->speed;
	}
	g_objectTable[objectIdx].mobj->speed += wholeDelta;
	if (g_objectTable[objectIdx].mobj->speed > 3600u) {
		g_objectTable[objectIdx].mobj->speed = 3600;
	}
}

/* Takes decelerationPerSecond times g_elapsedTicks over 236 from an object's
 * mobj speed, carrying the fraction in speedRemainder, and sets 0 when the
 * speed would go below 0. Does not check that the object has a mobile
 * object. */
// FUNCTION: XVT 0x4ADA90
void Flight_DecelerateObjectSpeed(int objectIdx, int decelerationPerSecond)
{
	uint32_t product;
	uint32_t wholeQuotient;
	uint16_t wholeDelta;
	uint16_t fracDelta;
	uint16_t *speedRemainder;
	uint16_t oldRemainder;

	product = (uint32_t)(uint16_t)g_elapsedTicks *
		  (uint32_t)decelerationPerSecond;
	wholeQuotient = product / 236u;
	wholeDelta = (uint16_t)wholeQuotient;
	fracDelta = (uint16_t)(((product - wholeQuotient * 236u) << 16) / 236u);

	speedRemainder = &g_objectTable[objectIdx].mobj->speedRemainder;
	oldRemainder = *speedRemainder;
	*speedRemainder = oldRemainder;
	*speedRemainder -= fracDelta;
	if (g_objectTable[objectIdx].mobj->speedRemainder > oldRemainder) {
		--g_objectTable[objectIdx].mobj->speed;
	}
	g_objectTable[objectIdx].mobj->speed -= wholeDelta;
	if (g_objectTable[objectIdx].mobj->speed > 0x8000u) {
		g_objectTable[objectIdx].mobj->speed = 0;
	}
}

/* For an AI craft in a dive: at 256 or less above its AI controller's aimPointZ
 * it levels out (craft pitch 0x4000, pitchState 0, diveState 2). Otherwise,
 * once its height above that point is no more than minus its vertical move
 * component (moveZ) times g_simStepsPerSecond, times 3 for genus 0 and 2 for
 * others, and its pitch is above 0x4000, it aims the pitch halfway back to
 * level (targetZAngle, pitchState 1). Recomputes the move vector when it is out
 * of date. Works on g_curCraft, which must be the object's craft. */
// FUNCTION: XVT 0x4ADB50
void Flight_UpdateDivePulloutPitchTarget(int objectIdx)
{
	AiController *controller;
	int altitudeDelta;
	ObjectRecord *object;
	int moveZ;
	int projectedMovement;

	controller = &g_curCraft->aiController;
	object = &g_objectTable[objectIdx];
	altitudeDelta = object->world_z - controller->aimPointZ;
	if (altitudeDelta < 0 || altitudeDelta <= 0x100) {
		g_curCraft->pitch = 0x4000;
		g_curCraft->aiFlight.pitchState = 0;
		g_curCraft->aiFlight.diveState = 2;
	} else {
		if (object->mobj->moveVectorDirty != 0) {
			FVIEW_calcrotatemove(object->pitch, object->yaw,
					     object);
		}
		moveZ = object->mobj->moveZ;
		projectedMovement = moveZ;
		projectedMovement *= g_simStepsPerSecond;
		if (object->genusId == 0) {
			projectedMovement *= 3;
		} else {
			projectedMovement *= 2;
		}
		if (altitudeDelta <= -projectedMovement &&
		    g_curCraft->pitch > 0x4000u) {
			controller->targetZAngle =
				(uint16_t)(((g_curCraft->pitch - 0x4000) >> 1) +
					   0x4000);
			g_curCraft->aiFlight.pitchState = 1;
		}
	}
}
