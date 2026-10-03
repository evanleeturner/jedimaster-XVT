#include "xvt/flight/hud/hud.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/cockpit_instruments.h"
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"
#endif

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_render.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/config.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/memory.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#ifndef XVT_MODERN
int(_fileno)(XvtFile *stream);
long _filelength(int fileDescriptor);
#endif

#ifndef XVT_MODERN
typedef struct Msvc42CrtFilePrefix {
	/* Never read or written; puts flags at byte 12. */
	uint8_t reserved[12];
	/* Stream flags; the original build's loaders read bit 0x10 as end of
	 * file. */
	int flags;
} Msvc42CrtFilePrefix;
#endif

typedef struct LfdEntryHeader {
	uint8_t resourceType[4]; /* Type tag; PLTT marks a palette. */
	char resourceName[8];	 /* Read from the file; nothing uses it. */
	uint32_t dataSize;	 /* Bytes of data after the header. */
} LfdEntryHeader;

enum { PANEL_BOX_SPAN_SCRATCH_SIZE = 2048 };

/* Pixels of one corner stroke in the box color, which
 * Hud_DrawDepthTestedBoxCorners, its only user, fills from the box's left x
 * when that is positive (twice that many bytes in at 16 bits) and draws
 * from. */
// GLOBAL: XVT 0x6122D8
uint8_t g_panelBoxSpanScratch[PANEL_BOX_SPAN_SCRATCH_SIZE] = {0};
/* 1 after the targeting computer sees a new target, telling the next
 * Hud_Update3DCrt of the target inset to copy the inset's span mask again.
 * Hud_UpdateTargetingComputerDisplay sets it to 0 each time it draws and to 1
 * on a target change; Hud_DrawHudTargetInsetIfEnabled passes it on. */
// GLOBAL: XVT 0x521550
uint16_t g_hudTargetInsetMaskRefreshPending = 0;

/* Memory handle of the panel sprite data, HUD_PANEL_SPRITE_BUFFER_BYTES
 * (120,000) long, allocated by FeDiskIo_InitGlobalBuffers and freed and set to
 * 0 by FeDiskIo_FreeFlightResources; the modern build's XvtFlightLoading_Reset
 * also sets it to 0. Hud_RebuildDisplayForViewState starts
 * g_hudPanelSpriteDataWriteCursor at its memory. */
// GLOBAL: XVT 0x9D8A50
uint16_t g_hudPanelSpriteDataHandle = 0;
/* Memory handle of the message log, MESSAGE_LOG_BUFFER_BYTES (32,000) long,
 * allocated by FeDiskIo_InitGlobalBuffers and freed and set to 0 by
 * FeDiskIo_FreeFlightResources; the modern build's XvtFlightLoading_Reset also
 * sets it to 0. msg_emitInFlightMessage and Mfd_DrawMessageLogPage lock it into
 * g_messageLogRecords. */
// GLOBAL: XVT 0x9EC5FC
uint16_t g_messageLogHandle = 0;
/* Memory handle of the flight icon frames and their pointers, allocated by
 * FeDiskIo_InitGlobalBuffers, which also locks it and loads the frames, and
 * freed and set to 0 by FeDiskIo_FreeFlightResources; the modern build's
 * XvtFlightLoading_Reset also sets it to 0. */
// GLOBAL: XVT 0xA07CCC
uint16_t g_flightIconFramesHandle = 0;
/* Per view, the memory handle and the three LFD entries (cockpit image,
 * viewport span mask, palette) of its cockpit image.
 * Hud_LoadCockpitSpriteResources fills the views whose descriptor has
 * resourceRef 1 and sets the others' handle to 0;
 * Hud_RebuildDisplayForViewState loads a view's entries into
 * g_flightScratchScreenBuffer when they are not loaded.
 * FeDiskIo_FreeFlightResources frees the handles. */
// GLOBAL: XVT 0xA08A10
HudCockpitResource g_hudCockpitResources[28] = {{0}};
/* Per view (HudViewState), the cockpit image, viewport and name read by
 * Hud_LoadCockpitInterfaceFile from the cockpit's .INT file, its only
 * writer. */
// GLOBAL: XVT 0xA08610
HudCockpitResourceDescriptor g_hudCockpitResourceDescriptors[28] = {{0}};
/* Path of the last cockpit file opened: the .INT files and the .LFD files.
 * Written by Hud_LoadCockpitInterfaceFile,
 * Hud_LoadAuxiliaryCockpitInterfaceFile, Hud_LoadCockpitLfdEntries and
 * Hud_LoadCockpitSpriteResources with strcpy and strcat, which do not check its
 * 32 bytes. */
// GLOBAL: XVT 0xA08BD0
char g_hudCockpitResourcePath[32] = {0};
/* Base path of the cockpit or panel file being loaded: the resolution's cockpit
 * folder and a name, with ".PNL" when Hud_RebuildDisplayForViewState reads
 * panel sprites. Written by Hud_LoadCockpitResources and
 * Hud_RebuildDisplayForViewState with strcpy and strcat, which do not check its
 * 32 bytes. */
// GLOBAL: XVT 0xA08310
char g_hudCockpitBasePath[32] = {0};
/* Panel sprite file name and sprite counts read from the cockpit's .INT file by
 * Hud_LoadCockpitInterfaceFile, its only writer. */
// GLOBAL: XVT 0xA08BC0
HudPanelSpriteFileInfo g_hudPanelSpriteFileInfo = {{0}, 0, 0};
/* Target the targeting computer last drew, an object index, or -1 for none.
 * Hud_UpdateTargetingComputerDisplay and Hud_DrawCmdTargetDetails record the
 * target; Hud_InitHUD and Hud_UpdateCraftSystemStatusIndicators set -1;
 * Flight_ProcessPlayerActions and Player_ValidateCurrentTargets set -2, and
 * collide_collisions and paiman_boardmaneuver -3, to force a redraw. After any
 * of -1 to -3 the targeting computer also redraws its labels. */
// GLOBAL: XVT 0xA08C74
int16_t g_hudCachedTargetObjectIdx = 0;
/* Panel sprite set the cockpit wants; set to 0 by Hud_LoadCockpitResources and
 * Hud_ReloadCockpitInterfaceFile, and nothing sets another value.
 * Hud_RebuildDisplayForViewState reloads the panel sprites while it differs
 * from g_hudLoadedPanelSetId. */
// GLOBAL: XVT 0xA0813E
uint8_t g_hudPanelSetId = 0;
/* Where Hud_LoadCockpitLfdEntries writes the next LFD entry; it advances past
 * each one. Set to a view's memory by Hud_LoadCockpitSpriteResources and to
 * g_flightScratchScreenBuffer by Hud_RebuildDisplayForViewState. */
// GLOBAL: XVT 0xA08370
uint8_t *g_hudCockpitResourceWriteCursor = NULL;
/* 1 once Hud_LoadCockpitSpriteResources has loaded the cockpit images. Set to 0
 * by FeDiskIo_InitGlobalBuffers and at flight start (Flight_MainLoop in the
 * original build, XvtFlightLoading_Globals in the modern one);
 * Mission_InitFlightRuntimeState loads the cockpit while it is 0. */
// GLOBAL: XVT 0x9D8C10
int g_hudCockpitResourcesLoaded = 0;
/* Panel sprite set last loaded by Hud_RebuildDisplayForViewState, which sets it
 * to g_hudPanelSetId; Mission_InitFlightRuntimeState sets 0xFF so the next
 * rebuild reloads. */
// GLOBAL: XVT 0x9D113E
uint8_t g_hudLoadedPanelSetId = 0;
/* Set to 1 by Mission_InitFlightRuntimeState and to 0 by
 * Hud_RebuildDisplayForViewState and by Flight_UpdatePlayerStep in the original
 * build or XvtFlightSim_Resume in the modern one. Nothing reads it. */
// GLOBAL: XVT 0xA00730
uint8_t g_flightDisplayRebuildPending = 0;
/* Names of the waypoints, by waypoint index, filled by
 * StringTable_LoadGameStrings; Hud_AppendObjectDisplayName reads them for
 * references of 0x8000 and up. */
// GLOBAL: XVT 0xA08380
const char *g_strWaypointNames[14] = {0};
/* Names of target components by mesh type, filled by
 * StringTable_LoadGameStrings; entry 32 (MESH_COMPONENT_32_DASHES) is the
 * dashes shown for no object. */
// GLOBAL: XVT 0xA08BF0
const char *g_strMeshComponentNames[33] = {0};
/* Start of each panel sprite in the panel sprite data, by sprite number;
 * Hud_LoadPanelSpriteRecords, its only writer, fills it. A layout's selector
 * picks the first sprite of a widget here. */
// GLOBAL: XVT 0x9ED240
uint8_t *g_hudPanelSpriteDataByIndex[265] = {0};
/* Where Hud_LoadPanelSpriteRecords writes the next sprite byte; it advances
 * past each sprite. Hud_RebuildDisplayForViewState starts it at the memory of
 * g_hudPanelSpriteDataHandle before reloading. */
// GLOBAL: XVT 0xA082A0
uint8_t *g_hudPanelSpriteDataWriteCursor = NULL;
/* The HUD instrument layouts: three sets of 144 (cockpit, HUD-only, craft
 * list), each element's position and widget values.
 * Hud_LoadCockpitInterfaceFile reads the first two sets and
 * Hud_LoadAuxiliaryCockpitInterfaceFile the third. At run time Hud_InitHUD and
 * Hud_UpdateCriticalHullShieldWarning use layout 127's
 * clipHeightOrForegroundColor as a counter, Hud_DrawCmdTargetDetails stores
 * label widths in the clipWidth of layouts 104 to 107, and
 * Hud_RebuildDisplayForViewState sets layout 396's selector. */
// GLOBAL: XVT 0xA08CA0
HudElementLayout g_hudElementLayouts[HUD_INSTRUMENT_COUNT] = {{0}};
/* Per HUD element, the value or state it was last drawn with, so it is redrawn
 * only on a change; writers set -1 or -2 to force a redraw. Many functions
 * write it, chiefly the Hud_DrawCached... functions and Hud_InitHUD, which sets
 * -2 everywhere and -3 for the MFD page elements; the modern build's
 * XvtCockpitReadouts_BeginTarget sets entries 102 and 103 to -2. */
// GLOBAL: XVT 0xA0A1E0
int16_t g_hudElementStateCache[HUD_INSTRUMENT_COUNT] = {0};
/* Index in g_hudElementLayouts of the current view's set: 0 for the cockpit
 * set, 144 for the HUD-only set, 288 for the craft list set. Only
 * Hud_RebuildDisplayForViewState writes it. */
// GLOBAL: XVT 0xA08374
uint16_t g_hudInstrumentSetBaseIndex = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
/* Palette index of the radar blip being added; only Hud_AddBlipToRadar writes
 * it, and an IFF it does not list keeps the last value. */
// GLOBAL: XVT 0xA08368
uint16_t g_radarBlipColor = 0;
/* Blip list the current radar frame fills for the fore radar: one of
 * g_radarForeBlipBufferA and B, chosen by Hud_DrawRadarBlips each frame. */
// GLOBAL: XVT 0xA08A04
HudRadarBlipPoint *g_radarForeDrawBlips = NULL;
/* Blips in g_radarForeDrawBlips, 0 to 47. Set to 0 by Hud_InitHUD and at the
 * start of each Hud_DrawRadarBlips; Hud_AddBlipToRadar raises it. */
// GLOBAL: XVT 0xA08A02
uint16_t g_radarForeBlipCount = 0;
/* Blip list the current radar frame fills for the aft radar: one of
 * g_radarAftBlipBufferA and B, chosen by Hud_DrawRadarBlips each frame. */
// GLOBAL: XVT 0xA08340
HudRadarBlipPoint *g_radarAftDrawBlips = NULL;
/* Blips in g_radarAftDrawBlips, 0 to 47. Set to 0 by Hud_InitHUD and at the
 * start of each Hud_DrawRadarBlips; Hud_AddBlipToRadar raises it. */
// GLOBAL: XVT 0xA08C7E
uint16_t g_radarAftBlipCount = 0;
/* Fore radar blips of the frame before, which Hud_DrawRadarBlips erases: the
 * buffer not in g_radarForeDrawBlips. */
// GLOBAL: XVT 0xA08360
HudRadarBlipPoint *g_radarForeEraseBlips = NULL;
/* Aft radar blips of the frame before, which Hud_DrawRadarBlips erases: the
 * buffer not in g_radarAftDrawBlips. */
// GLOBAL: XVT 0xA08BB0
HudRadarBlipPoint *g_radarAftEraseBlips = NULL;
/* Fore blips drawn the frame before, the count Hud_DrawRadarBlips erases; only
 * that function writes it. */
// GLOBAL: XVT 0xA083BA
uint16_t g_radarForePrevBlipCount = 0;
/* Aft blips drawn the frame before, the count Hud_DrawRadarBlips erases; only
 * that function writes it. */
// GLOBAL: XVT 0xA08344
uint16_t g_radarAftPrevBlipCount = 0;
/* Which radar buffers draw this frame: 1 draws into the A buffers and erases
 * the B ones, 0 the reverse. Hud_DrawRadarBlips flips it each frame;
 * Hud_InitHUD sets 0. */
// GLOBAL: XVT 0xA08364
uint8_t g_radarBlipBufferParity = 0;
/* 1 while the radar target marker is drawn and its background saved, so the
 * next Hud_DrawRadarBlips restores it first. Set by Hud_DrawRadarBlips; set to
 * 0 by it with no target and by Hud_InitHUD. */
// GLOBAL: XVT 0xA0837A
uint8_t g_radarTargetMarkerBackgroundSaved = 0;
/* First of the two fore radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA083C0
HudRadarBlipPoint g_radarForeBlipBufferA[48] = {{0}};
/* Second of the two fore radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA084E0
HudRadarBlipPoint g_radarForeBlipBufferB[48] = {{0}};
/* First of the two aft radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA0A540
HudRadarBlipPoint g_radarAftBlipBufferA[48] = {{0}};
/* Second of the two aft radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA0A660
HudRadarBlipPoint g_radarAftBlipBufferB[48] = {{0}};
/* Width, in pixels, of the scoreboard page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 112 at 320x240, 224 at 640x480, 168 at
 * 480x360. Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08366
uint16_t g_mfdMissionScoreboardBlitWidth = 0;
/* Source top edge, in pixels, of the scoreboard page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 109 at 320x240,
 * 219 at 640x480, 164 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA0836A
uint16_t g_mfdMissionScoreboardBlitSourceY = 0;
/* Height, in pixels, of the scoreboard page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 70 at 320x240, 140 at 640x480, 105 at
 * 480x360. Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA0836C
uint16_t g_mfdMissionScoreboardBlitHeight = 0;
/* Source left edge, in pixels, of the scoreboard page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 2 at 320x240, 4
 * at 640x480, 3 at 480x360. Only Hud_InitHUD writes it, for the local player's
 * resolution. */
// GLOBAL: XVT 0xA0836E
uint16_t g_mfdMissionScoreboardBlitSourceX = 0;
/* Source top edge, in pixels, of the map view page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 186 at 320x240,
 * 373 at 640x480, 279 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08376
uint16_t g_mfdMapBlitSourceY = 0;
/* Width, in pixels, of the map view page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 111 at 320x240, 223 at 640x480, 167 at
 * 480x360. Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08378
uint16_t g_mfdMapBlitWidth = 0;
/* Height, in pixels, of the map view page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 52 at 320x240, 104 at 640x480, 78 at
 * 480x360. Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA083B8
uint16_t g_mfdMapBlitHeight = 0;
/* Source left edge, in pixels, of the map view page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 121 at 320x240,
 * 242 at 640x480, 181 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA083BC
uint16_t g_mfdMapBlitSourceX = 0;
/* Height, in pixels, of the craft list page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 47 at 320x240, 94 at 640x480, 70 at
 * 480x360. Hud_InitHUD writes it for the local player's resolution, or from
 * layout 130 of the current set while the map camera is on;
 * Hud_RebuildDisplayForViewState sets the resolution's value again when leaving
 * the craft list view. */
// GLOBAL: XVT 0xA08604
uint16_t g_mfdCraftListBlitHeight = 0;
/* Source left edge, in pixels, of the craft list page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 153 at 320x240,
 * 306 at 640x480, 229 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08606
uint16_t g_mfdCraftListBlitSourceX = 0;
/* Source top edge, in pixels, of the craft list page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 53 at 320x240,
 * 107 at 640x480, 80 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08608
uint16_t g_mfdCraftListBlitSourceY = 0;
/* Width, in pixels, of the craft list page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 112 at 320x240, 225 at 640x480, 168 at
 * 480x360. Hud_InitHUD writes it for the local player's resolution, or from
 * layout 130 of the current set while the map camera is on;
 * Hud_RebuildDisplayForViewState sets the resolution's value again when leaving
 * the craft list view. */
// GLOBAL: XVT 0xA08A00
uint16_t g_mfdCraftListBlitWidth = 0;
/* Height, in pixels, of the goals page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 53 at 320x240, 107 at 640x480, 80 at
 * 480x360. Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C80
uint16_t g_mfdGoalsBlitHeight = 0;
/* Source left edge, in pixels, of the damage page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 153 at 320x240,
 * 306 at 640x480, 229 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C82
uint16_t g_mfdDamageBlitSourceX = 0;
/* Source left edge, in pixels, of the goals page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 6 at 320x240,
 * 12 at 640x480, 9 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C84
uint16_t g_mfdGoalsBlitSourceX = 0;
/* Height, in pixels, of the damage page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 80 at 320x240, 160 at 640x480, 120 at
 * 480x360. Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C8A
uint16_t g_mfdDamageBlitHeight = 0;
/* Width, in pixels, of the goals page area that Hud_BlitSoftwareMfdPages copies
 * from g_flightOffscreenBuffer: 141 at 320x240, 282 at 640x480, 211 at 480x360.
 * Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C8C
uint16_t g_mfdGoalsBlitWidth = 0;
/* Width, in pixels, of the damage page area that Hud_BlitSoftwareMfdPages
 * copies from g_flightOffscreenBuffer: 100 at 320x240, 200 at 640x480, 150 at
 * 480x360. Only Hud_InitHUD writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C8E
uint16_t g_mfdDamageBlitWidth = 0;
/* Source top edge, in pixels, of the damage page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 103 at 320x240,
 * 206 at 640x480, 154 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C90
uint16_t g_mfdDamageBlitSourceY = 0;
/* Source top edge, in pixels, of the goals page area that
 * Hud_BlitSoftwareMfdPages copies from g_flightOffscreenBuffer: 53 at 320x240,
 * 107 at 640x480, 80 at 480x360. Only Hud_InitHUD writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C92
uint16_t g_mfdGoalsBlitSourceY = 0;
/* Palette indices of a beam segment by the charge it holds, in thirds of 1,000
 * from empty (entry 0) to full (entry 3). Nothing writes it;
 * Hud_DrawBeamStrength2D reads it. */
// GLOBAL: XVT 0x521588
uint8_t g_hudBeamSegmentColorByChargeStep[4] = {0x30, 0x2D, 0x31, 0x32};
/* Offset in pixels of each of the nine beam segments from layout 51 at
 * 480x360. */
// GLOBAL: XVT 0x521590
const HudBeamSegmentOffset g_hudBeamSegmentOffsets480x360[9] = {
	{14, 14}, {12, 12}, {10, 10}, {9, 9}, {7, 7},
	{5, 5},	  {4, 4},   {2, 2},   {0, 0},
};
/* Sprite levels 0..10 include hit flash; text uses offset 10 plus levels 0..9. */
/* Palette shifts of the shield sprites by level 0 to 10 (10 is the hit flash),
 * then text colors by level 0 to 9 from HUD_SHIELD_TEXT_COLOR_OFFSET on, for
 * Hud_DrawShieldStrength2D. */
// GLOBAL: XVT 0x521570
const uint8_t g_hudShieldColors[22] = {
	0x2c, 0x34, 0x35, 0x36, 0x38, 0x39, 0x3a, 0x3c, 0x3d, 0x3e, 0x2e,
	0x2e, 0x37, 0x37, 0x37, 0x3b, 0x3b, 0x3b, 0x3f, 0x3f, 0x3f, 0x2e,
};
/* Shield side, 0 front or 1 rear, that took the last hit; collide_damagecraft,
 * its only writer, sets it, and Hud_DrawShieldStrength2D flashes that side
 * while shieldHitFlashTimer runs. */
// GLOBAL: XVT 0x9FE7D0
uint8_t g_lastShieldDamageSide = 0;
/* Offset in pixels of each of the nine beam segments from layout 51 at
 * 320x240. */
// GLOBAL: XVT 0x5215B8
const HudBeamSegmentOffset g_hudBeamSegmentOffsets320x240[9] = {
	{11, 11}, {10, 10}, {8, 8}, {7, 7}, {6, 6},
	{4, 4},	  {3, 3},   {2, 2}, {0, 0},
};
/* 1 while a laser cannon due to fire would hit the target, or with warheads
 * selected while missileLockState is 2. Hud_DrawReticle3D sets it to 0 and then
 * to 1 on a hit; Hud_UpdateTargetingLockIndicator sets it for warheads and
 * reads it for lasers. */
// GLOBAL: XVT 0xA0A1D0
uint8_t g_targetLockActive = 0;
/* 1 while Hud_InitHUD, its only writer, redraws the HUD;
 * ProvingGrounds_DrawStatusPanel reads it. */
// GLOBAL: XVT 0x9D8B68
uint8_t g_hudFullRedrawInProgress = 0;
/* Span mask of the craft list view's target inset, 480 bytes at 640x480 and
 * 480x360 and 200 at 320x240, read from the craft list's .INT file by
 * Hud_LoadAuxiliaryCockpitInterfaceFile; Hud_Update3DCrt copies it into the
 * viewport's mask. */
// GLOBAL: XVT 0x556720
uint8_t g_hudCraftListInsetSpanMask[480] = {0};
/* Span mask of the cockpit view's target inset, 480 bytes at 640x480 and
 * 480x360 and 200 at 320x240, read by Hud_LoadCockpitInterfaceFile;
 * Hud_Update3DCrt copies it into the viewport's mask. */
// GLOBAL: XVT 0x556540
uint8_t g_hudCockpitInsetSpanMask[480] = {0};
/* Span mask of the HUD-only view's target inset, 480 bytes at 640x480 and
 * 480x360 and 200 at 320x240, read by Hud_LoadCockpitInterfaceFile;
 * Hud_Update3DCrt copies it into the viewport's mask. */
// GLOBAL: XVT 0x556360
uint8_t g_hudOnlyViewInsetSpanMask[480] = {0};
/* Cockpit overlay strings by CockpitOverlayStringId (labels, scoreboard
 * headings, goal words, EJECT and the strings after it), filled by
 * StringTable_LoadGameStrings. */
// GLOBAL: XVT 0xA0A130
const char *g_strCockpitOverlayText[40] = {0};
/* Target camera and targeting computer strings by CmdThreatStringId, filled by
 * StringTable_LoadGameStrings. */
// GLOBAL: XVT 0xA0A0E0
const char *g_strCmdThreatDisplayText[18] = {0};
/* Armament labels by ThreatDisplayStringId (laser, ion, warhead, beam), filled
 * by StringTable_LoadGameStrings; Hud_DrawCmdTargetStatusIndicators draws
 * them. */
// GLOBAL: XVT 0xA08350
const char *g_strThreatDisplayText[4] = {0};
/* Color codes, for FlightText_SetColor, of a message by its pane type byte
 * (entries 0 to 8), and of a type 1 message by its digit 0 to 3 (entries 8 to
 * 11); read by Hud_ShowFlightMessagePane and Mfd_DrawMessageLogPage. */
// GLOBAL: XVT 0x5240A0
const uint8_t g_messageTextPrefixColorCodes[16] = {
	0x42, 0x4A, 0x46, 0x4E, 0x52, 0x45, 0x42, 0x52,
	0x4A, 0x52, 0x46, 0x4E, 0x4A, 0x4E, 0,	  0,
};
/* Color codes, for FlightText_SetColor, of a type 2 message by its sender's
 * IFF; read by Hud_ShowFlightMessagePane and Mfd_DrawMessageLogPage. */
// GLOBAL: XVT 0x5240B0
const uint8_t g_messageSenderIffColorCodes[8] = {0x52, 0x4A, 0x46, 0x4E,
						 0x4A, 0x4E, 0,	   0};
/* 1 when the mission command line asks for the frame rate overlay with
 * "tickcounter"; set from it by Flight_Main in the original build and
 * XvtFlightEntry_ReadLaunchSwitches in the modern one. */
// GLOBAL: XVT 0x5235E0
uint8_t g_flightConfTickCounterEnabled = 0;
/* Ticks the last flight loop took, for the frame rate overlay; written by
 * Flight_RunMissionLoop in the original build and XvtFlightFrame_Render in the
 * modern one, only while the overlay is on. */
// GLOBAL: XVT 0x9A8D90
int g_flightTickOverlayLastLoopTicks = 0;
/* Ticks summed over the overlay's sampling window; reset to 0 while the overlay
 * is off or once the sum passes LAG_LEVEL_2_TICKS (944). Written by
 * Flight_RunMissionLoop in the original build and XvtFlightFrame_Render in the
 * modern one. */
// GLOBAL: XVT 0x9A7BA0
int g_flightTickOverlayWindowTicks = 0;
/* Loops counted in g_flightTickOverlayWindowTicks; reset with it. Written by
 * Flight_RunMissionLoop in the original build and XvtFlightFrame_Render in the
 * modern one. */
// GLOBAL: XVT 0xA0829C
int g_flightTickOverlaySampleCount = 0;
/* Packet drop level, 0 to 3, shown by the status line's packet drop mark; 0
 * hides it. Set by Flight_RunMissionLoop in the original build and
 * XvtFlightFrame_UpdatePacketDropIndicator in the modern one; flight start sets
 * 0. */
// GLOBAL: XVT 0x9A8BFC
int g_packetDropIndicator = 0;
/* Lag level, 0 to 3, shown by the status line's lag mark; 0 hides it. Set by
 * Flight_RunMissionLoop in the original build and
 * XvtFlightFrame_UpdateLagIndicator in the modern one; flight start sets 0. */
// GLOBAL: XVT 0x9EC45C
int g_lagIndicator = 0;

/* The system message pane's message (pane types 3, 4 and 7), its
 * stateOrMessageId 0xFFFF while empty. msg_emitInFlightMessage fills it;
 * Hud_ShowFlightMessagePane marks it shown, Hud_UpdateFlightMessagePanes and
 * Hud_ResetFlightMessagePanes empty it, Hud_AdvanceFlightMessagePaneTimers ages
 * it. */
// GLOBAL: XVT 0x5569F8
HudInFlightMessageRecord g_systemMessagePane;
/* The flight group message pane's message (pane type 8), its stateOrMessageId
 * 0xFFFF while empty. msg_emitInFlightMessage fills it;
 * Hud_ShowFlightMessagePane marks it shown, Hud_UpdateFlightMessagePanes and
 * Hud_ResetFlightMessagePanes empty it, Hud_AdvanceFlightMessagePaneTimers ages
 * it. */
// GLOBAL: XVT 0x556A50
HudInFlightMessageRecord g_flightGroupMessagePane;
/* The ready message pane: slot 0 is the message shown, its stateOrMessageId
 * 0xFFFF while empty; slots 1 to g_readyMessageQueueCount wait in order, and
 * slot 10 can take a message that is never shown. Filled by
 * msg_emitInFlightMessage and moved by Hud_ShiftReadyMessageQueueForReplacement
 * and Hud_AdvanceReadyMessageQueue. */
// GLOBAL: XVT 0x9A6FF0
HudInFlightMessageRecord g_readyMessagePaneQueue[11];
/* Messages waiting behind slot 0 of g_readyMessagePaneQueue, 0 to 9. Raised by
 * msg_emitInFlightMessage and Hud_ShiftReadyMessageQueueForReplacement, lowered
 * by Hud_AdvanceReadyMessageQueue, set to 0 by Hud_ResetFlightMessagePanes,
 * Hud_ClearReadyMessageQueue and Mission_InitFlightRuntimeState. */
// GLOBAL: XVT 0x9D77F8
uint8_t g_readyMessageQueueCount;
/* Nonzero when the next Hud_UpdateFlightMessagePanes must expire all three
 * panes; Hud_ResetFlightMessagePanes raises it, and
 * Hud_UpdateFlightMessagePanes sets it back to 0. */
// GLOBAL: XVT 0x556AA4
int g_flightMessagePanesForceExpire = 0;
/* Slot 0's stateOrMessageId as Hud_ResetFlightMessagePanes, its only writer,
 * last found it before emptying the pane. Nothing reads it. */
// GLOBAL: XVT 0x9D7686
static uint16_t g_unusedReadyMessagePaneInitialState = 0;
/* Goal phrase (a message id) of the last target description shown, which
 * Hud_UpdateFlightMessagePanes compares to decide whether to show it again.
 * Written by msg_BuildTargetDescription when it emits, by
 * Hud_UpdateFlightMessagePanes, and by Hud_ResetFlightMessagePanes, which sets
 * 331, the blank message. */
// GLOBAL: XVT 0x9D7694
int g_targetDescriptionMessageId = 0;
/* 1 while radio messages are backed up: msg_writeMessageLogFile then runs when
 * the message log wraps, at mission end, and when the player turns the backup
 * off. The player's Shift+L turns it on and off (Flight_UpdatePlayerStep in the
 * original build, XvtFlightSim_UpdatePlayerStep in the modern one); mission end
 * and Hud_ResetFlightMessagePanes set 0. */
// GLOBAL: XVT 0x9ECC3C
int g_radioMessageBackupEnabled = 0;
/* Read as a replay view switch by msg_emitInFlightMessage, Hud_Update3DCrt and
 * several other functions. Nothing writes it, so it stays 0. */
// GLOBAL: XVT 0xA00860
uint16_t g_replayViewMode = 0;
/* 1 while system messages (pane types 3, 4 and 7) are shown. Flight start sets
 * 1 (Flight_MainLoop in the original build, XvtFlightLoading_MissionSetup in
 * the modern one); the player step toggles it (Flight_UpdatePlayerStep in the
 * original build, XvtFlightSim_UpdatePlayerStep in the modern one). */
// GLOBAL: XVT 0x9A7B54
int g_systemMessageDisplayEnabled = 0;
/* The ready pane's left edge on g_flightOffscreenBuffer: 42 at 320x240, 85 at
 * 640x480, 63 at 480x360, set by Hud_ResetFlightMessagePanes when it finds -1.
 * Flight start sets -1 (Flight_MainLoop in the original build,
 * XvtFlightLoading_MissionSetup in the modern one), so the pane rectangles are
 * set once per flight. */
// GLOBAL: XVT 0x5235DC
int g_readyMessagePaneLeft = -1;
/* The ready pane's top edge on g_flightOffscreenBuffer: 3 at 320x240, 6 at
 * 640x480, 4 at 480x360, set the first time Hud_ResetFlightMessagePanes runs in
 * a flight, its only writer. */
// GLOBAL: XVT 0x9D1148
int g_readyMessagePaneTop = 0;
/* The ready pane's right edge, exclusive, on g_flightOffscreenBuffer: 277 at
 * 320x240, 505 at 640x480, 378 at 480x360, set the first time
 * Hud_ResetFlightMessagePanes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A7EC0
int g_readyMessagePaneRight = 0;
/* The ready pane's bottom edge, exclusive, on g_flightOffscreenBuffer: 26 at
 * 320x240, 53 at 640x480, 39 at 480x360, set the first time
 * Hud_ResetFlightMessagePanes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A7B44
int g_readyMessagePaneBottom = 0;
/* The system message pane's left edge on g_flightOffscreenBuffer: 42 at
 * 320x240, 85 at 640x480, 63 at 480x360, set the first time
 * Hud_ResetFlightMessagePanes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A7390
int g_systemMessagePaneLeft = 0;
/* The system message pane's top edge on g_flightOffscreenBuffer: 32 at 320x240,
 * 64 at 640x480, 48 at 480x360, set the first time Hud_ResetFlightMessagePanes
 * runs in a flight, its only writer. */
// GLOBAL: XVT 0x9D12F8
int g_systemMessagePaneTop = 0;
/* The system message pane's right edge, exclusive, on g_flightOffscreenBuffer:
 * 277 at 320x240, 555 at 640x480, 415 at 480x360, set the first time
 * Hud_ResetFlightMessagePanes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A8D98
int g_systemMessagePaneRight = 0;
/* The system message pane's bottom edge, exclusive, on g_flightOffscreenBuffer:
 * 37 at 320x240, 75 at 640x480, 56 at 480x360, set the first time
 * Hud_ResetFlightMessagePanes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A8D94
int g_systemMessagePaneBottom = 0;
/* The flight group message pane's left edge on g_flightOffscreenBuffer: 75 at
 * 320x240, 150 at 640x480, 112 at 480x360, set the first time
 * Hud_ResetFlightMessagePanes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9EC470
int g_flightGroupMessagePaneLeft = 0;
/* The flight group message pane's top edge on g_flightOffscreenBuffer: 44 at
 * 320x240, 89 at 640x480, 66 at 480x360, set the first time
 * Hud_ResetFlightMessagePanes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9ECC38
int g_flightGroupMessagePaneTop = 0;
/* The flight group message pane's right edge, exclusive, on
 * g_flightOffscreenBuffer: 265 at 320x240, 530 at 640x480, 397 at 480x360, set
 * the first time Hud_ResetFlightMessagePanes runs in a flight, its only
 * writer. */
// GLOBAL: XVT 0x9D12F4
int g_flightGroupMessagePaneRight = 0;
/* The flight group message pane's bottom edge, exclusive, on
 * g_flightOffscreenBuffer: 49 at 320x240, 100 at 640x480, 74 at 480x360, set
 * the first time Hud_ResetFlightMessagePanes runs in a flight, its only
 * writer. */
// GLOBAL: XVT 0x9D6934
int g_flightGroupMessagePaneBottom = 0;
/* Text measured for the width of a three-digit field. */
// GLOBAL: XVT 0x5215E0
const char g_threeDigitWidthText[4] = "000";
/* Text measured for the width of the clock's minutes and colon. */
// GLOBAL: XVT 0x52168C
const char g_missionClockMinutesWidthText[4] = "00:";
/* Type tag of an LFD palette entry, which Hud_LoadCockpitLfdEntries
 * converts. */
// GLOBAL: XVT 0x521564
const uint8_t g_lfdPaletteResourceTypeTag[4] = {'P', 'L', 'T', 'T'};

/* Draws a box marker in the flight view for the hardware renderer: eight
 * one-pixel strokes along the corners of the box at x, y (relative to the
 * viewport), each an eighth of the box's width or height, at least 3 and at
 * most the box's size, clipped to the viewport. The strokes go into
 * g_flightVertexBuffer and g_triBuffer as opaque quads in palette color
 * colorIdx (g_swPalette times 4), depth-tested and written at depth, at least
 * 1, through g_invDepthProjScale; the batch is flushed first when 32 vertices
 * or 16 triangles would not fit. A 4 by 4 box at depth 1 is instead filled in
 * software with colorIdx through g_flightFillRectClippedFn, leaving the text
 * clip on the viewport. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40C430
void Hud_DrawBoxOverlayHW(int x, int y, int width, int height, int colorIdx,
			  int depth)
{

	enum {
		MARKER_SIZE = 4,
		MIN_CORNER_LENGTH = 3,
		QUAD_VERTEX_COUNT = 4,
		QUAD_TRIANGLE_COUNT = 2,
		MAX_BOX_VERTEX_COUNT = 32,
		MAX_BOX_TRIANGLE_COUNT = 16,
	};

	const Std3DRenderStateFlags renderFlags = STD3D_RS_Z_COMPARE_ENABLE |
						  STD3D_RS_Z_WRITE_ENABLE |
						  STD3D_RS_MONO_DISABLE;
	uint8_t savedTextBackgroundColor;
	uint16_t markerX;
	uint16_t markerY;
	uint32_t color;
	float depthValue;
	int boxWidth;
	int boxHeight;
	int adjustedDepth;
	int boxTop;
	int right;
	int bottom;
	int cornerWidth;
	int cornerHeight;
	int start;
	int end;
	int vertexIndex;

	boxWidth = width;
	adjustedDepth = depth;
	if (adjustedDepth == 1 && boxWidth == MARKER_SIZE &&
	    height == MARKER_SIZE) {
		FlightSurface_Lock();
		savedTextBackgroundColor = g_flightTextBgColor;
		g_flightTextBgColor = (uint8_t)colorIdx;
		markerX = (uint16_t)(g_flightVpX + x);
		markerY = (uint16_t)(g_flightVpY + y);
		FlightText_SetClipRect(g_flightVpX, g_flightVpY,
				       g_flightVpX + g_flightVpWidth,
				       g_flightVpY + g_flightVpHeight);
		g_flightFillRectClippedFn(markerX, markerY,
					  markerX + MARKER_SIZE,
					  markerY + MARKER_SIZE, 1);
		g_flightTextBgColor = savedTextBackgroundColor;
		FlightSurface_Unlock();
		return;
	}
	boxHeight = height;

	if (g_d3dVertexCount + MAX_BOX_VERTEX_COUNT > g_maxBatchVerts ||
	    g_d3dTriangleCount + MAX_BOX_TRIANGLE_COUNT > g_maxBatchTris) {
		Math_SetFpuExtendedPrecisionMode();
		std3D_StartScene();
		std3D_LockExecuteBuffer();
		std3D_AddVertices(g_flightVertexBuffer, g_d3dVertexCount);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_triBuffer,
				   (unsigned int)g_d3dTriangleCount);
		std3D_ExecuteBuffer();
		std3D_EndScene();
		Math_SetFpuSinglePrecisionMode();
		g_d3dTriangleCount = 0;
		g_d3dVertexCount = 0;
	}

	color = 4 * (g_swPalette[colorIdx].b +
		     ((g_swPalette[colorIdx].g +
		       ((g_swPalette[colorIdx].r - 64) << 8))
		      << 8));
	boxTop = y;
	right = x + boxWidth;
	bottom = boxTop + boxHeight;
	cornerWidth = boxWidth >> 3;
	cornerHeight = boxHeight >> 3;
	if (cornerWidth < MIN_CORNER_LENGTH) {
		cornerWidth = MIN_CORNER_LENGTH;
	}
	if (cornerHeight < MIN_CORNER_LENGTH) {
		cornerHeight = MIN_CORNER_LENGTH;
	}
	if (cornerWidth > boxWidth) {
		cornerWidth = boxWidth;
	}
	if (cornerHeight > boxHeight) {
		cornerHeight = boxHeight;
	}
	if (adjustedDepth < 1) {
		adjustedDepth = 1;
	}
	depthValue = g_renderUnitFloat /
		     ((float)adjustedDepth * g_invDepthProjScale +
		      g_renderUnitFloat);
	if (g_std3DZCompareCap == 2) {
		depthValue = g_renderUnitFloat - depthValue;
	}

	if (boxTop >= 0 && boxTop < g_flightVpHeight) {
		start = x;
		end = x + cornerWidth;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpWidth) {
			end = g_flightVpWidth - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)boxTop;
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)boxTop;
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(boxTop + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(boxTop + 1);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
		start = right - cornerWidth;
		end = right;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpWidth) {
			end = g_flightVpWidth - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)boxTop;
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)boxTop;
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(boxTop + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(boxTop + 1);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
	}

	if (bottom >= 0 && bottom < g_flightVpHeight) {
		start = x;
		end = x + cornerWidth;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpWidth) {
			end = g_flightVpWidth - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)(bottom);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)(bottom);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(bottom + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(bottom + 1);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
		start = right - cornerWidth;
		end = right + 1;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpWidth) {
			end = g_flightVpWidth - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)(bottom);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)(bottom);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(bottom + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(bottom + 1);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
	}

	if (x >= 0 && x < g_flightVpWidth) {
		start = boxTop;
		end = boxTop + cornerHeight;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpHeight) {
			end = g_flightVpHeight - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(x);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(x);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(x + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(x + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(start);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
		start = bottom - cornerHeight;
		end = bottom;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpHeight) {
			end = g_flightVpHeight - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(x);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(x);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(x + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(x + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(start);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
	}

	if (right >= 0 && right < g_flightVpWidth) {
		start = boxTop;
		end = boxTop + cornerHeight;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpHeight) {
			end = g_flightVpHeight - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(right);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(right);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(right + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(right + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(start);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
		start = bottom - cornerHeight;
		end = bottom;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flightVpHeight) {
			end = g_flightVpHeight - 1;
		}
		if (end > start) {
			g_flightVertexBuffer[g_d3dVertexCount].sx =
				g_flightVpOriginX + (float)(right);
			g_flightVertexBuffer[g_d3dVertexCount].sy =
				g_flightVpOriginY + (float)(start);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sx =
				g_flightVpOriginX + (float)(right);
			g_flightVertexBuffer[g_d3dVertexCount + 1].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sx =
				g_flightVpOriginX + (float)(right + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 2].sy =
				g_flightVpOriginY + (float)(end);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sx =
				g_flightVpOriginX + (float)(right + 1);
			g_flightVertexBuffer[g_d3dVertexCount + 3].sy =
				g_flightVpOriginY + (float)(start);
			for (vertexIndex = 0; vertexIndex < QUAD_VERTEX_COUNT;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.sz = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.rhw = depthValue;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tu = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.tv = 0.0f;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = color;
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.specular = 0;
			}
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 1;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_triBuffer[g_d3dTriangleCount].v0 = g_d3dVertexCount;
			g_triBuffer[g_d3dTriangleCount].v1 =
				g_d3dVertexCount + 2;
			g_triBuffer[g_d3dTriangleCount].v2 =
				g_d3dVertexCount + 3;
			g_triBuffer[g_d3dTriangleCount].texture = NULL;
			g_triBuffer[g_d3dTriangleCount++].flags = renderFlags;
			g_d3dVertexCount += QUAD_VERTEX_COUNT;
		}
	}
}

/* Switches playerIdx to HUD view hudViewState and returns 1, or returns 0 for
 * the local player when g_hudCockpitResourceDescriptors has no resource for
 * that view. Returns 1 at once when it is already the live view. Otherwise sets
 * viewState.hudStateLive and hudStateMirror; for the local player it also
 * rebuilds the display with Hud_RebuildDisplayForViewState, shows it with every
 * surface lock released, and resets the palette. Does not check hudViewState
 * against the 28 descriptors. */
// FUNCTION: XVT 0x427720
int Hud_SetHudViewState(int hudViewState, int playerIdx)
{
	int localPlayer;
	int savedLockCount;
	int remainingLocks;

	localPlayer = g_localPlayer;
	if (playerIdx == localPlayer &&
	    g_hudCockpitResourceDescriptors[hudViewState].resourceRef == 0) {
		return 0;
	}
	if (g_players[playerIdx].viewState.hudStateLive == hudViewState) {
		return 1;
	}

	g_players[playerIdx].viewState.hudStateLive = (uint8_t)hudViewState;
	if (playerIdx == localPlayer) {
		FlightRender_InvokeTransitionHook(1);
		FlightSurface_Lock();
		Hud_RebuildDisplayForViewState(hudViewState, playerIdx);
		FlightSurface_Unlock();

		savedLockCount = FlightSurface_GetLockCount();
		if (savedLockCount > 0) {
			for (remainingLocks = savedLockCount;
			     remainingLocks != 0; --remainingLocks) {
				FlightSurface_Unlock();
			}
		}
		FlightDisplay_BlitRenderSurface();
		FlightDisplay_Flip();
		FlightDisplay_BlitRenderSurface();
		if (savedLockCount > 0) {
			do {
				FlightSurface_Lock();
				--savedLockCount;
			} while (savedLockCount != 0);
		}
		FlightRender_ResetPalette(1);
	}
	g_players[playerIdx].viewState.hudStateMirror = (uint8_t)hudViewState;
	return 1;
}

/* Prepares the HUD of the local player for a full redraw; does nothing for
 * another player. Sets the MFD page blit rectangles (g_mfdGoalsBlit...,
 * g_mfdDamageBlit..., g_mfdCraftListBlit..., g_mfdMissionScoreboardBlit...,
 * g_mfdMapBlit...) for g_flightResolutionMode, the craft list one from its
 * layout while mapCameraState is not 0. Sets every entry of
 * g_hudElementStateCache to -2 (invalid, so it redraws), the MFD page elements
 * 117 and 130 to 134 of each set to -3 (inactive); sets layout 127's
 * clipHeightOrForegroundColor to 1, g_hudCachedTargetObjectIdx to -1 and the
 * radar counts, parity and marker flag to 0. Then refreshes the craft system
 * indicators, draws the HUD (Hud_RenderHud) and its static text.
 * g_hudFullRedrawInProgress is 1 while it runs. */
// FUNCTION: XVT 0x438A30
void Hud_InitHUD(int playerIdx)
{
	enum {
		CRITICAL_WARNING_LAYOUT_INDEX = 127,
		HUD_ELEMENT_STATE_INVALID = -2,
		HUD_ELEMENT_STATE_INACTIVE = -3,
	};

	uint16_t instrumentSet;
	uint16_t instrumentIndex;

	g_hudFullRedrawInProgress = 1;
	if (g_localPlayer == playerIdx) {
		switch (g_flightResolutionMode) {
		case FLIGHT_RESOLUTION_320X240:
			g_mfdGoalsBlitSourceX = 6;
			g_mfdGoalsBlitWidth = 141;
			g_mfdDamageBlitSourceY = 103;
			g_mfdDamageBlitWidth = 100;
			g_mfdDamageBlitHeight = 80;
			g_mfdGoalsBlitSourceY = 53;
			g_mfdCraftListBlitHeight = 47;
			g_mfdMissionScoreboardBlitSourceX = 2;
			g_mfdMissionScoreboardBlitSourceY = 109;
			g_mfdGoalsBlitHeight = 53;
			g_mfdMissionScoreboardBlitHeight = 70;
			g_mfdMapBlitSourceX = 121;
			g_mfdMapBlitSourceY = 186;
			g_mfdMapBlitWidth = 111;
			g_mfdMapBlitHeight = 52;
			g_mfdDamageBlitSourceX = 153;
			g_mfdCraftListBlitSourceX = 153;
			g_mfdCraftListBlitSourceY = 53;
			g_mfdCraftListBlitWidth = 112;
			g_mfdMissionScoreboardBlitWidth = 112;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_mfdGoalsBlitSourceX = 12;
			g_mfdGoalsBlitWidth = 282;
			g_mfdDamageBlitSourceY = 206;
			g_mfdDamageBlitWidth = 200;
			g_mfdDamageBlitHeight = 160;
			g_mfdGoalsBlitSourceY = 107;
			g_mfdCraftListBlitWidth = 225;
			g_mfdCraftListBlitHeight = 94;
			g_mfdMissionScoreboardBlitSourceX = 4;
			g_mfdMissionScoreboardBlitSourceY = 219;
			g_mfdMissionScoreboardBlitWidth = 224;
			g_mfdMissionScoreboardBlitHeight = 140;
			g_mfdMapBlitSourceX = 242;
			g_mfdMapBlitSourceY = 373;
			g_mfdMapBlitWidth = 223;
			g_mfdMapBlitHeight = 104;
			g_mfdGoalsBlitHeight = 107;
			g_mfdDamageBlitSourceX = 306;
			g_mfdCraftListBlitSourceX = 306;
			g_mfdCraftListBlitSourceY = 107;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_mfdGoalsBlitSourceX = 9;
			g_mfdGoalsBlitWidth = 211;
			g_mfdDamageBlitSourceY = 154;
			g_mfdDamageBlitWidth = 150;
			g_mfdDamageBlitHeight = 120;
			g_mfdGoalsBlitSourceY = 80;
			g_mfdCraftListBlitHeight = 70;
			g_mfdMissionScoreboardBlitSourceX = 3;
			g_mfdMissionScoreboardBlitSourceY = 164;
			g_mfdGoalsBlitHeight = 80;
			g_mfdMissionScoreboardBlitHeight = 105;
			g_mfdMapBlitSourceX = 181;
			g_mfdMapBlitSourceY = 279;
			g_mfdMapBlitWidth = 167;
			g_mfdMapBlitHeight = 78;
			g_mfdDamageBlitSourceX = 229;
			g_mfdCraftListBlitSourceX = 229;
			g_mfdCraftListBlitSourceY = 80;
			g_mfdCraftListBlitWidth = 168;
			g_mfdMissionScoreboardBlitWidth = 168;
			break;
		default:
			break;
		}

		if (g_players[g_localPlayer].mapCameraState != 0) {
			g_mfdCraftListBlitWidth =
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_CRAFT_LIST_ELEMENT]
						.clipWidth;
			g_mfdCraftListBlitHeight =
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_CRAFT_LIST_ELEMENT]
						.clipHeightOrForegroundColor;
		}

		for (instrumentSet = 0;
		     instrumentSet < HUD_INSTRUMENT_SET_COUNT;
		     ++instrumentSet) {
			for (instrumentIndex = 0;
			     instrumentIndex < HUD_INSTRUMENTS_PER_SET;
			     ++instrumentIndex) {
				if (instrumentIndex >=
					    HUD_MFD_CRAFT_LIST_ELEMENT &&
				    instrumentIndex <=
					    HUD_MFD_SCOREBOARD_ELEMENT) {
					g_hudElementStateCache
						[HUD_INSTRUMENTS_PER_SET *
							 instrumentSet +
						 instrumentIndex] =
							HUD_ELEMENT_STATE_INACTIVE;
				} else {
					g_hudElementStateCache
						[HUD_INSTRUMENTS_PER_SET *
							 instrumentSet +
						 instrumentIndex] =
							HUD_ELEMENT_STATE_INVALID;
				}
				if (instrumentIndex ==
				    HUD_MFD_MESSAGE_LOG_ELEMENT) {
					g_hudElementStateCache
						[HUD_INSTRUMENTS_PER_SET *
							 instrumentSet +
						 instrumentIndex] =
							HUD_ELEMENT_STATE_INACTIVE;
				}
			}
		}

		g_hudElementLayouts[CRITICAL_WARNING_LAYOUT_INDEX]
			.clipHeightOrForegroundColor = 1;
		g_hudCachedTargetObjectIdx = -1;
		g_radarForeBlipCount = 0;
		g_radarTargetMarkerBackgroundSaved = 0;
		g_radarAftBlipCount = 0;
		g_radarBlipBufferParity = 0;
		Hud_ClearUnavailableCraftSystemIndicators();
		if (g_flightSimSideEffectsSuppressed == 0) {
			Hud_UpdateCraftSystemStatusIndicators();
		}
		Hud_RenderHud(g_localPlayer);
		Hud_DrawStaticCockpitText(g_localPlayer);
		Hud_InitHUDEndStub(g_localPlayer);
	}
	g_hudFullRedrawInProgress = 0;
}

/* Draws the local player's HUD for the current view; another player gets
 * nothing drawn. While awaiting a new craft or with the mission ending, it only
 * stops the incoming missile warning. In the map view it draws the map overlay;
 * in the forward cockpit Hud_UpdateHUD, in the HUD-only view
 * Hud_UpdateHudOnlyView, in the target camera Hud_UpdateCMDText, in any other
 * view the MFD pages; views other than the forward and HUD-only ones also call
 * fsfx_UpdateTargetingTone(0) to stop the targeting tone. Every view but the
 * first case then updates the critical hull and shield warning. The modern
 * build records the instruments for its renderer around it. */
// FUNCTION: XVT 0x438DF0
void Hud_RenderHud(int playerIdx)
{
	uint8_t hudState;

#ifdef XVT_MODERN
	XvtCockpitInstruments_BeginUpdate(playerIdx);
#endif

	if (playerIdx == g_localPlayer) {
		if (g_players[g_localPlayer].awaitingNewCraft != 0 ||
		    g_flightMissionState.missionEndPending != 0) {
			FlightSurface_Unlock();
			fsfx_UpdateIncomingMissileWarning(0);
			FlightSurface_Lock();
		} else if (g_players[playerIdx].mapCameraState != 0) {
			Hud_DrawMapViewOverlay();
			FlightSurface_Unlock();
			fsfx_UpdateTargetingTone(0);
			FlightSurface_Lock();
			Hud_UpdateCriticalHullShieldWarning();
		} else {
			hudState =
				g_players[g_localPlayer].viewState.hudStateLive;
			if (hudState == HUD_VIEW_FORWARD) {
				Hud_UpdateHUD();
				Hud_UpdateCriticalHullShieldWarning();
			} else if (hudState == HUD_VIEW_HUD_ONLY) {
				Hud_UpdateHudOnlyView();
				Hud_UpdateCriticalHullShieldWarning();
			} else if (hudState == HUD_VIEW_TARGET_CAMERA) {
				Hud_UpdateCMDText();
				FlightSurface_Unlock();
				fsfx_UpdateTargetingTone(0);
				FlightSurface_Lock();
				Hud_UpdateCriticalHullShieldWarning();
			} else {
				FlightSurface_Unlock();
				fsfx_UpdateTargetingTone(0);
				Hud_UpdateMfdPages();
				FlightSurface_Lock();
				Hud_UpdateCriticalHullShieldWarning();
			}
		}
	}
#ifdef XVT_MODERN
	XvtCockpit_RefreshInstruments(playerIdx);
#endif
}

/* Draws the 3D image of the local player's target into the target inset (layout
 * 2 of the current set) through Hud_Update3DCrt, passing
 * g_hudTargetInsetMaskRefreshPending. Draws nothing while awaiting a new craft
 * or with the mission ending, or without a target. In the map view that is all
 * it needs; otherwise only the forward and HUD-only views draw it, and only
 * while bit 0 of the craft's activeHudFeatureMask, the targeting computer, is
 * set. */
// FUNCTION: XVT 0x438EF0
void Hud_DrawHudTargetInsetIfEnabled(int playerIndex)
{
	enum {
		TARGET_INSET_LAYOUT_INDEX = 2,
		INVALID_TARGET_OBJECT_INDEX = -1,
		TARGET_INSET_FEATURE_MASK = 1,
	};

	uint8_t hudState;

#ifdef XVT_MODERN
	XvtRenderDraw_Scope(XVT_SCOPE_COCKPIT);
#endif

	if (playerIndex == g_localPlayer &&
	    g_players[g_localPlayer].awaitingNewCraft == 0 &&
	    g_flightMissionState.missionEndPending == 0) {
		if (g_players[playerIndex].mapCameraState != 0) {
			if (g_players[playerIndex].currentTargetObjectIdx ==
			    INVALID_TARGET_OBJECT_INDEX) {
				return;
			}
			Hud_Update3DCrt(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_INSET_LAYOUT_INDEX]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_INSET_LAYOUT_INDEX]
						.y,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_INSET_LAYOUT_INDEX]
						.selector,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_INSET_LAYOUT_INDEX]
						.colorIndexOrWidgetParam,
				g_hudTargetInsetMaskRefreshPending);
			return;
		} else {
			hudState =
				g_players[playerIndex].viewState.hudStateLive;
			if (hudState != HUD_VIEW_FORWARD &&
			    hudState != HUD_VIEW_HUD_ONLY) {
				return;
			}
			if (g_players[playerIndex].currentTargetObjectIdx ==
			    INVALID_TARGET_OBJECT_INDEX) {
				return;
			}
			if ((g_objectTable[g_players[playerIndex].objectIndex]
				     .mobj->pCraft->damageStats
				     .activeHudFeatureMask &
			     TARGET_INSET_FEATURE_MASK) == 0) {
				return;
			}
		}

		Hud_Update3DCrt(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    TARGET_INSET_LAYOUT_INDEX]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    TARGET_INSET_LAYOUT_INDEX]
				.y,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    TARGET_INSET_LAYOUT_INDEX]
				.selector,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    TARGET_INSET_LAYOUT_INDEX]
				.colorIndexOrWidgetParam,
			g_hudTargetInsetMaskRefreshPending);
	}
}

/* Draws the labels that stay put on the local player's cockpit or HUD-only
 * view; nothing in other views or for another player. The power labels (L, S,
 * E, B) at layouts 122 to 125 show when their layout is set and the craft has
 * the matching HUD feature and, for S and B, shields or a beam system. With HUD
 * feature 0x40 it draws the speed and throttle labels (the short forms when the
 * layout's selector is 4 or less) and the throttle's "%"; with 0x20 the F and R
 * shield labels. Then the craft's name and status, centered in layout 126, and
 * in the forward view the mission clock's ":" at layout 46. Leaves
 * g_flightTextShadowEnabled at 0 in the forward view and font tier 0 or 2. */
// FUNCTION: XVT 0x439030
void Hud_DrawStaticCockpitText(uint16_t playerIdx)
{
	CraftData *craft;
	uint16_t layoutIndex;
	uint16_t systemFlags;
	uint16_t featureMask;
	uint16_t textWidth;

	if (g_localPlayer != playerIdx) {
		return;
	}
	if (g_players[playerIdx].viewState.hudStateLive != HUD_VIEW_FORWARD &&
	    g_players[playerIdx].viewState.hudStateLive != HUD_VIEW_HUD_ONLY) {
		return;
	}
	craft = g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft;
	systemFlags = craft->systemFlags;
	featureMask = craft->damageStats.activeHudFeatureMask;

	FlightText_SetFontTier(0);
	FlightText_SetColor(0x2F);
	for (layoutIndex = 122; layoutIndex < 126; ++layoutIndex) {
		if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					layoutIndex]
			    .selector == 0) {
			continue;
		}
		switch (layoutIndex) {
		case 122:
			if ((featureMask & 0x0200) == 0) {
				continue;
			}
			break;
		case 123:
			if ((systemFlags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) == 0 ||
			    (featureMask & 0x0800) == 0) {
				continue;
			}
			break;
		case 124:
			if ((featureMask & 0x0400) == 0) {
				continue;
			}
			break;
		case 125:
			if ((systemFlags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) ==
				    0 ||
			    (featureMask & 0x1000) == 0) {
				continue;
			}
			break;
		}
		FlightText_SetBackgroundColor(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    layoutIndex]
				.colorIndexOrWidgetParam);
		FlightText_SetCursor(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    layoutIndex]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    layoutIndex]
				.y);
		FlightText_SetClipRect(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    layoutIndex]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    layoutIndex]
				.y,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    layoutIndex]
					.x +
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 layoutIndex]
						.clipWidth,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    layoutIndex]
					.y +
				g_flightFontLineHeight);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(
			(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_POWER_LABEL_FIRST +
						layoutIndex - 122),
			g_strCockpitOverlayText[layoutIndex - 122 +
						COCKPIT_OVERLAY_STR_L],
			XVT_COCKPIT_ALIGN_LEFT);
#endif
		FlightText_DrawString(
			g_strCockpitOverlayText[layoutIndex - 122 +
						COCKPIT_OVERLAY_STR_L]);
	}

	if ((featureMask & 0x40) != 0) {
		if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 121]
			    .selector != 0) {
			if (g_hudInstrumentSetBaseIndex !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				FlightText_SetColor(0x4A);
			}
			FlightText_SetBackgroundColor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 121]
						.colorIndexOrWidgetParam);
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 121]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 121]
						.y);
			FlightText_SetClipRect(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 121]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 121]
						.y,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 121]
							.x +
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 121]
							.clipWidth,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 121]
							.y +
					g_flightFontLineHeight);
			if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						121]
				    .selector <= 4) {
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_SPEED_LABEL,
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_SPD],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_SPD]);
			} else {
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_SPEED_LABEL,
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_SPEED],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_SPEED]);
			}
		}
		if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 120]
			    .selector != 0) {
			if (g_hudInstrumentSetBaseIndex !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				FlightText_SetColor(0x4A);
			}
			FlightText_SetBackgroundColor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 120]
						.colorIndexOrWidgetParam);
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 120]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 120]
						.y);
			FlightText_SetClipRect(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 120]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 120]
						.y,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 120]
							.x +
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 120]
							.clipWidth,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 120]
							.y +
					g_flightFontLineHeight);
			if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						120]
				    .selector <= 4) {
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_THROTTLE_LABEL,
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_THTL],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_THTL]);
			} else {
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_THROTTLE_LABEL,
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_THROTTLE],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(
					g_strCockpitOverlayText
						[COCKPIT_OVERLAY_STR_THROTTLE]);
			}
		}
		if (g_hudInstrumentSetBaseIndex !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			FlightText_SetFontTier(0);
		} else {
			FlightText_SetFontTier(2);
		}
		textWidth =
			FlightText_MeasureStringWidth(g_threeDigitWidthText);
		FlightText_SetColor(0x4A);
		FlightText_SetBackgroundColor(0x2C);
		FlightText_SetClipRect(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 41]
					.x +
				textWidth,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 41].y,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 41]
					.x +
				textWidth + FlightText_MeasureStringWidth("%"),
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 41]
					.y +
				g_flightFontLineHeight);
		FlightText_SetCursor(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 41]
					.x +
				textWidth,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 41]
				.y);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_THROTTLE_PERCENT,
					   "%", XVT_COCKPIT_ALIGN_LEFT);
#endif
		g_flightDrawCharFn('%');
	}

	if ((featureMask & 0x20) != 0) {
		for (layoutIndex = 0; layoutIndex <= 1; ++layoutIndex) {
			if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						128 + layoutIndex]
				    .selector == 0) {
				continue;
			}
			FlightText_SetClipRect(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 128 +
					 layoutIndex]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 128 +
					 layoutIndex]
						.y,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 128 + layoutIndex]
							.x +
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 128 + layoutIndex]
							.clipWidth,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 128 + layoutIndex]
							.y +
					g_flightFontLineHeight);
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 128 +
					 layoutIndex]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 128 +
					 layoutIndex]
						.y);
			FlightText_SetColor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 128 +
					 layoutIndex]
						.clipHeightOrForegroundColor);
			FlightText_SetBackgroundColor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 128 +
					 layoutIndex]
						.colorIndexOrWidgetParam);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_SHIELD_LABEL_FIRST +
							layoutIndex),
				g_strCockpitOverlayText[layoutIndex +
							COCKPIT_OVERLAY_STR_F],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(
				g_strCockpitOverlayText[layoutIndex +
							COCKPIT_OVERLAY_STR_F]);
		}
	}

	FlightText_SetBackgroundColor(0x2C);
	FlightText_SetFontTier(0);
	if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
		Hud_AppendObjectDisplayName(
			(uint16_t)g_players[g_localPlayer].objectIndex, 7);
	} else {
		Hud_AppendObjectDisplayName(
			(uint16_t)g_players[g_localPlayer].objectIndex, 3);
	}
	FlightText_SetCursor(
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].x,
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].y);
	FlightText_SetClipRect(
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].x,
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].y,
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].x +
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126]
				.clipWidth,
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].y +
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126]
				.clipHeightOrForegroundColor);
	g_flightFillClipRectFn();
#ifdef XVT_MODERN
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS,
				   g_flightTextScratchBuffer,
				   XVT_COCKPIT_ALIGN_CENTER);
#endif
	FlightText_DrawStringCentered(g_flightTextScratchBuffer);
	if (g_players[playerIdx].viewState.hudStateLive == HUD_VIEW_FORWARD) {
		FlightText_SetFontTier(2);
		FlightText_SetClipRect(0, 0, g_screenWidth, g_screenHeight);
		FlightText_SetBackgroundColor(0x40);
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
			FlightText_SetColor(0x4D);
		} else {
			FlightText_SetColor(0x4E);
		}
		g_flightTextShadowEnabled = 0;
		FlightText_SetCursor(
			g_hudElementLayouts[46].x +
				FlightText_MeasureStringWidth("00"),
			g_hudElementLayouts[46].y);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CLOCK_SEPARATOR,
					   ":", XVT_COCKPIT_ALIGN_LEFT);
#endif
		g_flightDrawCharFn(':');
	}
}

/* Does nothing; Hud_InitHUD calls it last. */
// FUNCTION: XVT 0x439700
void Hud_InitHUDEndStub(int playerIdx) { (void)playerIdx; }

/* Updates the forward cockpit view's instruments for one frame: radar, reticle,
 * lock indicator, targeting computer, warheads, shields, beam, mission clock,
 * speed, throttle, power settings, threats, countermeasures, MFD pages and the
 * craft name and status line. In an X-wing, Y-wing, A-wing, Z-95 or B-wing it
 * also draws the shield distribution sprite (layout 51) with HUD feature 0x20
 * and the S-foil sprite (layout 45), state 1 while the 0x2 bit of sFoilState
 * is clear, each when its layout is placed. */
// FUNCTION: XVT 0x439710
void Hud_UpdateHUD(void)
{
	enum {
		SHIELD_DISTRIBUTION_ELEMENT = 51,
		S_FOIL_STATE_ELEMENT = 45,
		SHIELD_DISPLAY_FEATURE_MASK = 0x20,
		S_FOIL_CLOSED_MASK = 2,
	};

	int objectIndex;
	int isRebelFighter;
	CraftData *craft;
	uint16_t sFoilIndicatorState;

	FlightText_SetFontTier(2);
	Hud_DrawRadarBlips();
	Hud_DrawReticle3D();
	Hud_UpdateTargetingLockIndicator();
	Hud_UpdateTargetingComputerDisplay();
	Hud_UpdateWarheadCnt();
	Hud_DrawShieldStrength2D();
	Hud_DrawBeamStrength2D();
	Hud_UpdateMissionClockDisplay();

	objectIndex = g_players[g_localPlayer].objectIndex;
	isRebelFighter =
		objectIndex != -1 &&
		(g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_X_WING ||
		 g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_Y_WING ||
		 g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_A_WING ||
		 g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_Z_95_HEADHUNTER ||
		 g_objectTable[objectIndex].objectType == CRAFT_SPECIES_B_WING);
	if (isRebelFighter) {
		craft = g_objectTable[objectIndex].mobj->pCraft;
		if ((craft->damageStats.activeHudFeatureMask &
		     SHIELD_DISPLAY_FEATURE_MASK) != 0 &&
		    (uint16_t)g_hudElementLayouts[SHIELD_DISTRIBUTION_ELEMENT]
					    .x +
				    (uint16_t)g_hudElementLayouts
					    [SHIELD_DISTRIBUTION_ELEMENT]
						    .y !=
			    0) {
			Hud_DrawCachedSpriteElement(
				SHIELD_DISTRIBUTION_ELEMENT,
				(uint8_t)craft->shieldDistribMode);
		}
	}

	Hud_UpdateSpeedPercent();
	Hud_UpdateThrottlePercent();
	Hud_DrawPowerSettings2D();
	Hud_UpdateThreatIndicators(0);
	Hud_UpdateCountermeasureStatus();

	objectIndex = g_players[g_localPlayer].objectIndex;
	isRebelFighter =
		objectIndex != -1 &&
		(g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_X_WING ||
		 g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_Y_WING ||
		 g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_A_WING ||
		 g_objectTable[objectIndex].objectType ==
			 CRAFT_SPECIES_Z_95_HEADHUNTER ||
		 g_objectTable[objectIndex].objectType == CRAFT_SPECIES_B_WING);
	if (isRebelFighter &&
	    g_hudElementLayouts[S_FOIL_STATE_ELEMENT].x != 0) {
		sFoilIndicatorState =
			(g_objectTable[objectIndex].mobj->pCraft->sFoilState &
			 S_FOIL_CLOSED_MASK) == 0;
		Hud_DrawCachedSpriteElement(S_FOIL_STATE_ELEMENT,
					    sFoilIndicatorState);
	}

	Hud_UpdateMfdPages();
	Hud_DrawCraftNameFpsAndNetworkStatus();
}

/* Updates the HUD-only view's instruments for one frame: as Hud_UpdateHUD
 * without the mission clock, countermeasures, shield distribution and S-foils,
 * and with the threat indicators in mode 1. */
// FUNCTION: XVT 0x4398B0
void Hud_UpdateHudOnlyView(void)
{
	Hud_DrawRadarBlips();
	Hud_DrawReticle3D();
	Hud_UpdateTargetingLockIndicator();
	Hud_UpdateWarheadCnt();
	Hud_UpdateThreatIndicators(1);
	FlightText_SetFontTier(2);
	Hud_UpdateTargetingComputerDisplay();
	Hud_DrawShieldStrength2D();
	Hud_DrawBeamStrength2D();
	Hud_UpdateSpeedPercent();
	Hud_UpdateThrottlePercent();
	Hud_DrawPowerSettings2D();
	Hud_UpdateMfdPages();
	Hud_DrawCraftNameFpsAndNetworkStatus();
}

/* Updates the map view's text for the local player: the targeting computer and
 * MFD pages every call, and the "following" and "tracking" lines (layouts 120
 * and 121 of the current set) when the camera focus object or aim target
 * changed since g_hudElementStateCache last recorded it. Each line is redrawn
 * centered on a cleared field with the object's name, or dashes for none, and
 * the new index is recorded. */
// FUNCTION: XVT 0x439900
void Hud_DrawMapViewOverlay(void)
{
	enum {
		CAMERA_FOCUS_LAYOUT = 120,
		AIM_TARGET_LAYOUT = 121,
		DEFAULT_SCREEN_WIDTH = 320,
		DEFAULT_SCREEN_HEIGHT = 200,
		OBJECT_DISPLAY_FLAGS = 3,
	};

	uint16_t objectIdx;
	const char *objectName;
	char text[80];

	Hud_UpdateTargetingComputerDisplay();
	Hud_UpdateMfdPages();
	if ((uint16_t)g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
					     CAMERA_FOCUS_LAYOUT] !=
	    g_players[g_localPlayer].viewState.cameraFocusObjIdx) {
		FlightSurface_Lock();
		FlightSw_SetRenderTarget(NULL, DEFAULT_SCREEN_WIDTH,
					 DEFAULT_SCREEN_HEIGHT, 0);
		FlightText_SetFontTier(2);
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		FlightText_SetColor('N');
		FlightText_SetClipRect(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    CAMERA_FOCUS_LAYOUT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    CAMERA_FOCUS_LAYOUT]
				.y,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    CAMERA_FOCUS_LAYOUT]
					.x +
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 CAMERA_FOCUS_LAYOUT]
						.clipWidth,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    CAMERA_FOCUS_LAYOUT]
					.y +
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 CAMERA_FOCUS_LAYOUT]
						.clipHeightOrForegroundColor);
		FlightText_SetCursor(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    CAMERA_FOCUS_LAYOUT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    CAMERA_FOCUS_LAYOUT]
				.y);
		g_flightFillClipRectFn();

		objectIdx =
			g_players[g_localPlayer].viewState.cameraFocusObjIdx;
		if (objectIdx != UINT16_MAX) {
			Hud_AppendObjectDisplayName(objectIdx,
						    OBJECT_DISPLAY_FLAGS);
			objectName = g_flightTextScratchBuffer;
		} else {
			objectName = g_strMeshComponentNames
				[MESH_COMPONENT_32_DASHES];
		}
		strcpy(text, objectName);
		FlightText_SetScratch(g_strMapRoomText[MAP_ROOM_STR_FOLLOWING]);
		FlightText_AppendScratchChar(' ');
		FlightText_AppendScratchString(text);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_MAP_FOLLOWING,
					   g_flightTextScratchBuffer,
					   XVT_COCKPIT_ALIGN_CENTER);
#endif
		FlightText_DrawStringCentered(g_flightTextScratchBuffer);
		FlightSurface_Unlock();
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       CAMERA_FOCUS_LAYOUT] =
			g_players[g_localPlayer].viewState.cameraFocusObjIdx;
	}

	if ((uint16_t)g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
					     AIM_TARGET_LAYOUT] !=
	    g_players[g_localPlayer].viewState.aimTargetIdx) {
		FlightSurface_Lock();
		FlightSw_SetRenderTarget(NULL, DEFAULT_SCREEN_WIDTH,
					 DEFAULT_SCREEN_HEIGHT, 0);
		FlightText_SetFontTier(2);
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		FlightText_SetColor('R');
		FlightText_SetClipRect(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    AIM_TARGET_LAYOUT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    AIM_TARGET_LAYOUT]
				.y,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    AIM_TARGET_LAYOUT]
					.x +
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 AIM_TARGET_LAYOUT]
						.clipWidth,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    AIM_TARGET_LAYOUT]
					.y +
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 AIM_TARGET_LAYOUT]
						.clipHeightOrForegroundColor);
		FlightText_SetCursor(
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    AIM_TARGET_LAYOUT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    AIM_TARGET_LAYOUT]
				.y);
		g_flightFillClipRectFn();

		objectIdx = g_players[g_localPlayer].viewState.aimTargetIdx;
		if (objectIdx != UINT16_MAX) {
			Hud_AppendObjectDisplayName(objectIdx,
						    OBJECT_DISPLAY_FLAGS);
			objectName = g_flightTextScratchBuffer;
		} else {
			objectName = g_strMeshComponentNames
				[MESH_COMPONENT_32_DASHES];
		}
		strcpy(text, objectName);
		FlightText_SetScratch(g_strMapRoomText[MAP_ROOM_STR_TRACKING]);
		FlightText_AppendScratchChar(' ');
		FlightText_AppendScratchString(text);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_MAP_TRACKING,
					   g_flightTextScratchBuffer,
					   XVT_COCKPIT_ALIGN_CENTER);
#endif
		FlightText_DrawStringCentered(g_flightTextScratchBuffer);
		FlightSurface_Unlock();
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       AIM_TARGET_LAYOUT] =
			g_players[g_localPlayer].viewState.aimTargetIdx;
	}
}

/* Updates the target camera (CMD) view: when g_hudElementStateCache entry 143
 * reads 0xFFFE, it updates the MFD pages, draws the four headers at layouts 139
 * to 142 (shield, hull, distance and a component name, each followed by ":") in
 * each layout's color, and sets that entry to 0. 0xFFFE is the -2 Hud_InitHUD
 * writes. Then it draws the target's details and status indicators every
 * call. */
// FUNCTION: XVT 0x439C60
void Hud_UpdateCMDText(void)
{
	enum {
		CMD_TEXT_LAYOUT_FIRST = 139,
		CMD_TEXT_LAYOUT_END = 143,
		CMD_TEXT_CACHE_INDEX = 143,
		MESH_COMPONENT_NAME = 17,
		COLOR_MFD_BACKGROUND = 0x2C,
		HUD_ELEMENT_STATE_INVALID = UINT16_MAX - 1,
	};

	uint16_t layoutIndex;

	if (((const uint16_t *)g_hudElementStateCache)[CMD_TEXT_CACHE_INDEX] ==
	    HUD_ELEMENT_STATE_INVALID) {
		Hud_UpdateMfdPages();
		FlightText_SetBackgroundColor(COLOR_MFD_BACKGROUND);
		FlightText_SetFontTier(2);
		for (layoutIndex = CMD_TEXT_LAYOUT_FIRST;
		     layoutIndex < CMD_TEXT_LAYOUT_END; ++layoutIndex) {
			FlightText_SetCursor(
				g_hudElementLayouts[layoutIndex].x,
				g_hudElementLayouts[layoutIndex].y);
			FlightText_SetColor(g_hudElementLayouts[layoutIndex]
						    .colorIndexOrWidgetParam);
			switch (layoutIndex - CMD_TEXT_LAYOUT_FIRST) {
			case 0:
				FlightText_SetScratch(
					g_strCmdThreatDisplayText[1]);
				break;
			case 1:
				FlightText_SetScratch(
					g_strCmdThreatDisplayText[2]);
				break;
			case 2:
				FlightText_SetScratch(
					g_strCmdThreatDisplayText[0]);
				break;
			case 3:
				FlightText_SetScratch(
					g_strMeshComponentNames
						[MESH_COMPONENT_NAME]);
				break;
			default:
				break;
			}
			FlightText_AppendScratchChar(':');
			FlightText_SetClipRect(
				g_hudElementLayouts[layoutIndex].x,
				g_hudElementLayouts[layoutIndex].y,
				g_hudElementLayouts[layoutIndex].x +
					FlightText_MeasureStringWidth(
						g_flightTextScratchBuffer),
				g_hudElementLayouts[layoutIndex].y +
					g_flightFontLineHeight);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_CMD_HEADER_FIRST +
							layoutIndex -
							CMD_TEXT_LAYOUT_FIRST),
				g_flightTextScratchBuffer,
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(g_flightTextScratchBuffer);
		}
		g_hudElementStateCache[CMD_TEXT_CACHE_INDEX] = 0;
	}
	Hud_DrawCmdTargetDetails();
	Hud_DrawCmdTargetStatusIndicators();
}

/* Redraws the radar for the local player's craft when it has both radar HUD
 * features (0x80 and 0x100). Moves the blip counts to the previous counts,
 * swaps the draw and erase buffers by g_radarBlipBufferParity, and adds a blip
 * through Hud_AddBlipToRadar for each radar-visible object: craft other than
 * the player's that are the target or are neither breaking up, exploding nor
 * hidden by a decoy beam; projectiles; and static region objects. Then it
 * restores the target marker's background, erases the previous blips, draws the
 * new ones, draws the target marker when there is a target (setting
 * g_radarTargetMarkerBackgroundSaved), and flips the parity. */
// FUNCTION: XVT 0x439D90
void Hud_DrawRadarBlips(void)
{
	uint16_t playerObjectIdx = g_players[g_localPlayer].objectIndex;
	CraftData *playerCraft = g_objectTable[playerObjectIdx].mobj->pCraft;
	int objectIdx;

	if ((playerCraft->damageStats.activeHudFeatureMask & 0x80) == 0 ||
	    (playerCraft->damageStats.activeHudFeatureMask & 0x100) == 0) {
		return;
	}

	g_radarForePrevBlipCount = g_radarForeBlipCount;
	g_radarAftPrevBlipCount = g_radarAftBlipCount;
	g_radarForeBlipCount = 0;
	g_radarAftBlipCount = 0;
	g_radarTargetMarkerRestoreX = g_radarTargetMarkerDrawX;
	g_radarTargetMarkerRestoreY = g_radarTargetMarkerDrawY;
	if (g_radarBlipBufferParity != 0) {
		g_radarForeEraseBlips = g_radarForeBlipBufferB;
		g_radarForeDrawBlips = g_radarForeBlipBufferA;
		g_radarAftEraseBlips = g_radarAftBlipBufferB;
		g_radarAftDrawBlips = g_radarAftBlipBufferA;
	} else {
		g_radarForeEraseBlips = g_radarForeBlipBufferA;
		g_radarForeDrawBlips = g_radarForeBlipBufferB;
		g_radarAftEraseBlips = g_radarAftBlipBufferA;
		g_radarAftDrawBlips = g_radarAftBlipBufferB;
	}

	for (objectIdx = g_activeRegionObjectSlotStart;
	     objectIdx < g_activeRegionCraftObjectSlotEnd; ++objectIdx) {
		ObjectRecord *object = &g_objectTable[objectIdx];
		if (objectIdx != playerObjectIdx &&
		    (g_objectTypeTable[object->objectType].behaviorFlags & 1) !=
			    0) {
			CraftData *craft = object->mobj->pCraft;
			if (g_players[g_localPlayer].currentTargetObjectIdx ==
				    objectIdx ||
			    (!Object_HasActiveDecoyBeam((uint16_t)objectIdx) &&
			     craft->objectKind !=
				     CRAFT_OBJECT_KIND_BREAKING_UP &&
			     craft->objectKind !=
				     CRAFT_OBJECT_KIND_EXPLODING)) {
				Hud_AddBlipToRadar((int16_t)objectIdx);
			}
		}
	}
	for (objectIdx = g_projectileObjectSlotStart;
	     objectIdx < g_projectileObjectSlotEnd; ++objectIdx) {
		if ((g_objectTypeTable[g_objectTable[objectIdx].objectType]
			     .behaviorFlags &
		     1) != 0) {
			Hud_AddBlipToRadar((int16_t)objectIdx);
		}
	}
	for (objectIdx = g_regionMainObjectSlotEnd;
	     objectIdx <
	     g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount;
	     ++objectIdx) {
		if ((g_objectTypeTable[g_objectTable[objectIdx].objectType]
			     .behaviorFlags &
		     1) != 0) {
			Hud_AddBlipToRadar((int16_t)objectIdx);
		}
	}

	if (g_radarTargetMarkerBackgroundSaved != 0) {
		g_flightRestoreRadarTargetMarkerFn();
	}
	if (g_radarForePrevBlipCount != 0) {
		g_flightDrawPointArrayMaskedFn(
			(uint16_t *)&g_radarForeEraseBlips->x,
			g_radarForePrevBlipCount);
	}
	if (g_radarForeBlipCount != 0) {
		g_flightDrawPointArrayFn((uint16_t *)&g_radarForeDrawBlips->x,
					 g_radarForeBlipCount);
	}
	if (g_radarAftPrevBlipCount != 0) {
		g_flightDrawPointArrayMaskedFn(
			(uint16_t *)&g_radarAftEraseBlips->x,
			g_radarAftPrevBlipCount);
	}
	if (g_radarAftBlipCount != 0) {
		g_flightDrawPointArrayFn((uint16_t *)&g_radarAftDrawBlips->x,
					 g_radarAftBlipCount);
	}
#ifdef XVT_MODERN
	XvtCockpitInstruments_CompleteRadar();
#endif

	if (g_players[g_localPlayer].currentTargetObjectIdx == -1) {
		g_radarTargetMarkerBackgroundSaved = 0;
	} else {
		g_flightDrawRadarTargetMarkerFn();
		g_radarTargetMarkerBackgroundSaved = 1;
	}
	g_radarBlipBufferParity ^= 1;
}

/* Adds one radar blip for objIdx. Takes its offset from the local player's
 * craft along the craft's forward, side and up vectors, recomputing them first
 * when marked dirty; ahead of the craft it goes to the fore radar (layout 0 of
 * the current set), behind to the aft one (layout 1), at the point
 * MATH2_getradarcoord gives in radarx and radary, with y kept at 0 or more.
 * g_radarBlipColor is 47 with no mobile object or for a satellite, blinks
 * between 59 and 55 for a projectile, else follows the IFF: 63, 55, 51, 59, 55,
 * 211 for 0 to 5, and keeps the last blip's color for any other. Past 61,083
 * and 122,166 in rough distance it steps the color down by 1 or 2 (up for 211;
 * 46 and 45 for 47). Each list holds 48; past that the last entry is
 * overwritten. Sets the target marker position when objIdx is the target. */
// FUNCTION: XVT 0x43A0C0
void Hud_AddBlipToRadar(int16_t objIdx)
{
	int playerObjectIdx = g_players[g_localPlayer].objectIndex;
	int objectIdx = (uint16_t)objIdx;
	int deltaX = g_objectTable[objectIdx].world_x -
		     g_objectTable[playerObjectIdx].world_x;
	int deltaY = g_objectTable[objectIdx].world_y -
		     g_objectTable[playerObjectIdx].world_y;
	int deltaZ = g_objectTable[objectIdx].world_z -
		     g_objectTable[playerObjectIdx].world_z;
	int up;
	int side;
	int16_t frontBlip;
	int forward;
	MobileObject *mobileObject;
	int fwdZProduct;
	int fwdXProduct;
	int fwdYProduct;
	int sideZProduct;
	int sideYProduct;
	int sideXProduct;
	int upXProduct;
	int upYProduct;
	int upZProduct;
	int sideZ;

	if (g_objectTable[playerObjectIdx].mobj->orientMatrixDirty != 0) {
		FVIEW_calcrotatemove(g_objectTable[playerObjectIdx].pitch,
				     g_objectTable[playerObjectIdx].yaw,
				     &g_objectTable[playerObjectIdx]);
		FVIEW_calcrotateorient(g_objectTable[playerObjectIdx].roll, 0,
				       &g_objectTable[playerObjectIdx]);
	}
	fwdXProduct = Math_MulQ15(
		deltaX, g_objectTable[playerObjectIdx].mobj->cachedFwdX);
	fwdYProduct = Math_MulQ15(
		deltaY, g_objectTable[playerObjectIdx].mobj->cachedFwdY);
	forward = fwdXProduct + fwdYProduct;
	fwdZProduct = Math_MulQ15(
		deltaZ, g_objectTable[playerObjectIdx].mobj->cachedFwdZ);
	forward += fwdZProduct;
	sideXProduct = Math_MulQ15(
		deltaX, g_objectTable[playerObjectIdx].mobj->cachedSideX);
	sideYProduct = Math_MulQ15(
		deltaY, g_objectTable[playerObjectIdx].mobj->cachedSideY);
	side = sideXProduct + sideYProduct;
	sideZ = g_objectTable[playerObjectIdx].mobj->cachedSideZ;
	sideZProduct = Math_MulQ15(deltaZ, sideZ);
	side += sideZProduct;
	upXProduct = Math_MulQ15(
		deltaX, g_objectTable[playerObjectIdx].mobj->cachedUpX);
	upYProduct = Math_MulQ15(
		deltaY, g_objectTable[playerObjectIdx].mobj->cachedUpY);
	up = upXProduct + upYProduct;
	upZProduct = Math_MulQ15(
		deltaZ, g_objectTable[playerObjectIdx].mobj->cachedUpZ);
	up += upZProduct;
	up = -up;
	frontBlip = 1;
	if (forward < 0) {
		forward = -forward;
		frontBlip = 0;
	}

	mobileObject = g_objectTable[objectIdx].mobj;
	if (mobileObject == NULL) {
		g_radarBlipColor = 47;
	} else if (g_objectTable[objectIdx].genusId == CRAFT_GENUS_SATELLITE) {
		g_radarBlipColor = 47;
	} else if (mobileObject->family == 1) {
		if (((g_missionElapsedClock.subsecondTicks / 4) & 1) != 0) {
			g_radarBlipColor = 59;
		} else {
			g_radarBlipColor = 55;
		}
	} else {
		switch (mobileObject->iff) {
		case 0:
			g_radarBlipColor = 63;
			break;
		case 1:
		case 4:
			g_radarBlipColor = 55;
			break;
		case 2:
			g_radarBlipColor = 51;
			break;
		case 3:
			g_radarBlipColor = 59;
			break;
		case 5:
			g_radarBlipColor = 211;
			break;
		default:
			break;
		}
	}

	pai_ObjectRefUpdateRoughDistance(g_players[g_localPlayer].objectIndex,
					 objectIdx);
	if (g_lastRoughDistance > 122166) {
		if (g_radarBlipColor == 47) {
			g_radarBlipColor = 45;
		} else if (g_radarBlipColor == 211) {
			g_radarBlipColor += 2;
		} else {
			g_radarBlipColor -= 2;
		}
	} else if (g_lastRoughDistance > 61083) {
		if (g_radarBlipColor == 47) {
			g_radarBlipColor = 46;
		} else if (g_radarBlipColor == 211) {
			++g_radarBlipColor;
		} else {
			--g_radarBlipColor;
		}
	}

	MATH2_getradarcoord(side, up, forward);
	if (frontBlip != 0) {
		radarx += g_hudElementLayouts[g_hudInstrumentSetBaseIndex].x;
		radary += g_hudElementLayouts[g_hudInstrumentSetBaseIndex].y;
		if (radary < 0) {
			radary = 0;
		}
		g_radarForeDrawBlips[g_radarForeBlipCount].x = (uint16_t)radarx;
		g_radarForeDrawBlips[g_radarForeBlipCount].y = (uint16_t)radary;
		g_radarForeDrawBlips[g_radarForeBlipCount].color =
			g_radarBlipColor;
#ifdef XVT_MODERN
		XvtCockpitInstruments_RecordRadar(objectIdx, 1,
						  g_radarForeBlipCount, radarx,
						  radary, g_radarBlipColor);
#endif
		++g_radarForeBlipCount;
		if (g_radarForeBlipCount == 48) {
			--g_radarForeBlipCount;
		}
	} else {
		radarx +=
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 1].x;
		radary +=
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 1].y;
		if (radary < 0) {
			radary = 0;
		}
		g_radarAftDrawBlips[g_radarAftBlipCount].x = (uint16_t)radarx;
		g_radarAftDrawBlips[g_radarAftBlipCount].y = (uint16_t)radary;
		g_radarAftDrawBlips[g_radarAftBlipCount].color =
			g_radarBlipColor;
#ifdef XVT_MODERN
		XvtCockpitInstruments_RecordRadar(objectIdx, 0,
						  g_radarAftBlipCount, radarx,
						  radary, g_radarBlipColor);
#endif
		++g_radarAftBlipCount;
		if (g_radarAftBlipCount == 48) {
			--g_radarAftBlipCount;
		}
	}

	if (g_players[g_localPlayer].currentTargetObjectIdx == objIdx) {
		g_radarTargetMarkerDrawX = (uint16_t)radarx;
		g_radarTargetMarkerDrawY = (uint16_t)radary;
	}
}

/* Draws the local player's targeting computer for one frame. In the proving
 * grounds it draws ProvingGrounds_DrawStatusPanel at the target inset's corner
 * instead. Outside the map view it draws nothing without HUD feature bit 0, and
 * with the targeting computer subsystem out it draws the cover panel sprite and
 * stops. When the target differs from g_hudCachedTargetObjectIdx it records it,
 * sets g_hudTargetInsetMaskRefreshPending, marks the target elements of
 * g_hudElementStateCache for redraw, draws the labels when there was no target
 * before, and draws the new name, or the cover panel when there is no target
 * now.
 *
 * With a target it then draws every frame: the name, with the owning player's
 * name added when it may be shown; shields, the mean of the two shield values
 * against the model's strength; hull, what is left of hullMax; systems, what is
 * left of the model's system strength, at most 25 while weapon fire is
 * inhibited; and the distance, to the selected component for a starship or
 * platform. For a target that is not a fighter it shows the cargo (unknown
 * until identified) and the selected component, and stops. For a fighter flown
 * by the AI it shows the first word of its plan's report and its target; for
 * one flown by a player in a combat mission, hyperspace, attack or patrol
 * status and the target or first waypoint. Leaves g_flightTextShadowEnabled at
 * 0. A status message with no space in its first 40 characters draws whatever
 * the word buffer held. */
// FUNCTION: XVT 0x43A5E0
void Hud_UpdateTargetingComputerDisplay(void)
{
	enum {
		TARGET_INSET_ELEMENT = 2,
		TARGET_PANEL_ELEMENT = 69,
		TARGET_SYSTEM_VALUE_ELEMENT = 82,
		TARGET_DISTANCE_VALUE_ELEMENT = 83,
		TARGET_DISTANCE_FRACTION_ELEMENT = 84,
		TARGET_SHIELD_VALUE_ELEMENT = 85,
		TARGET_HULL_VALUE_ELEMENT = 86,
		TARGET_CARGO_ELEMENT = 87,
		TARGET_UNUSED_ELEMENT = 88,
		TARGET_DETAIL_ELEMENT = 89,
		TARGET_ALT_PANEL_ELEMENT = 108,
		TARGET_SYSTEM_LABEL_ELEMENT = 111,
		TARGET_DISTANCE_LABEL_ELEMENT = 112,
		TARGET_SHIELD_LABEL_ELEMENT = 113,
		TARGET_HULL_LABEL_ELEMENT = 114,
		TARGET_NAME_ELEMENT = 115,
		TARGETING_COMPUTER_FEATURE_MASK = 1,
		HIDDEN_TARGET_DISPLAY_FLAGS = 1,
		NORMAL_TARGET_DISPLAY_FLAGS = 3,
		SHORT_TARGET_DISPLAY_FLAGS = 2,
		PERCENTAGE_SCALE = 0x28F,
		MAX_STATUS_FIRST_WORD_LENGTH = 40,
		NO_SELECTED_COMPONENT = 50,
		WAYPOINT_ZERO_OBJECT_REF = 0x8000,
	};

	int localObjectIndex;
	uint16_t currentTargetObjectIndex;
	int16_t previousTargetObjectIndex;
	uint8_t mapCameraState;
	bool drawTargetDisplay;
	bool useLeftAlignedDetails;
	ObjectRecord *targetObject;
	MobileObject *targetMobileObject;
	CraftData *targetCraft;
	unsigned int shieldPercentage;
	unsigned int hullPercentage;
	unsigned int systemPercentage;
	uint16_t left;
	uint16_t top;
	uint16_t dirtyState = UINT16_MAX;
	uint16_t invalidState = UINT16_MAX - 1;
	char statusFirstWord[64];

#ifdef XVT_MODERN
	XvtCockpitReadouts_BeginTarget(0);
#endif

	g_flightTextShadowEnabled = 0;
	left = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
				   TARGET_INSET_ELEMENT]
		       .x;
	top = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
				  TARGET_INSET_ELEMENT]
		      .y;
	if (g_flightMissionState.provingGroundsModeActive != 0) {
#ifdef XVT_MODERN
		XvtCockpitReadouts_HideTarget();
#endif
		ProvingGrounds_DrawStatusPanel(left, top);
		return;
	}

	drawTargetDisplay = true;
	mapCameraState = g_players[g_localPlayer].mapCameraState;
	if (mapCameraState == 0) {
		if ((g_objectTable[g_players[g_localPlayer].objectIndex]
			     .mobj->pCraft->damageStats.activeHudFeatureMask &
		     TARGETING_COMPUTER_FEATURE_MASK) == 0) {
			drawTargetDisplay = false;
		}
	}

	localObjectIndex = g_players[g_localPlayer].objectIndex;
	useLeftAlignedDetails =
		localObjectIndex != -1 &&
		(g_objectTable[localObjectIndex].objectType == 1 ||
		 g_objectTable[localObjectIndex].objectType == 2 ||
		 g_objectTable[localObjectIndex].objectType == 3 ||
		 g_objectTable[localObjectIndex].objectType == 14 ||
		 g_objectTable[localObjectIndex].objectType == 4);

	if (mapCameraState == 0) {
		if (!drawTargetDisplay) {
#ifdef XVT_MODERN
			XvtCockpitReadouts_HideTarget();
			XvtCockpitText_ClearTargetFields();
#endif
			return;
		}
		if ((g_objectTable[localObjectIndex]
			     .mobj->pCraft->workingSubsystems &
		     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
			if (g_hudInstrumentSetBaseIndex ==
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				if (useLeftAlignedDetails) {
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordTargetCover(
						TARGET_ALT_PANEL_ELEMENT);
#endif
					Hud_DrawCachedSpriteElement(
						TARGET_ALT_PANEL_ELEMENT, 0);
				} else {
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordTargetCover(
						TARGET_PANEL_ELEMENT);
#endif
					Hud_DrawCachedSpriteElement(
						TARGET_PANEL_ELEMENT, 0);
				}
			} else {
#ifdef XVT_MODERN
				XvtCockpitReadouts_RecordTargetCover(
					g_hudInstrumentSetBaseIndex +
					TARGET_PANEL_ELEMENT);
#endif
				Hud_DrawCachedSpriteElement(
					g_hudInstrumentSetBaseIndex +
						TARGET_PANEL_ELEMENT,
					0);
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SYSTEM_LABEL_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_HULL_LABEL_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SHIELD_LABEL_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SYSTEM_VALUE_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SHIELD_VALUE_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_HULL_VALUE_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_ALT_PANEL_ELEMENT] = dirtyState;
			}
			drawTargetDisplay = false;
		}
	}
	if (!drawTargetDisplay) {
#ifdef XVT_MODERN
		XvtCockpitReadouts_HideTarget();
		XvtCockpitText_ClearTargetFields();
#endif
		return;
	}

	if (g_hudInstrumentSetBaseIndex != HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		FlightText_SetFontTier(0);
	} else {
		FlightText_SetFontTier(2);
	}
	g_hudTargetInsetMaskRefreshPending = 0;
	currentTargetObjectIndex =
		(uint16_t)g_players[g_localPlayer].currentTargetObjectIdx;
	if (currentTargetObjectIndex != (uint16_t)g_hudCachedTargetObjectIdx) {
#ifdef XVT_MODERN
		XvtCockpitText_ClearTargetFields();
#endif
		previousTargetObjectIndex = g_hudCachedTargetObjectIdx;
		g_hudTargetInsetMaskRefreshPending = 1;
		g_hudCachedTargetObjectIdx = (int16_t)currentTargetObjectIndex;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_PANEL_ELEMENT] = dirtyState;
		g_hudElementStateCache[TARGET_PANEL_ELEMENT] = dirtyState;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_SYSTEM_VALUE_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[TARGET_SYSTEM_VALUE_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_DISTANCE_VALUE_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[TARGET_DISTANCE_VALUE_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_DISTANCE_FRACTION_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[TARGET_DISTANCE_FRACTION_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_SHIELD_VALUE_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[TARGET_SHIELD_VALUE_ELEMENT] =
			dirtyState;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_HULL_VALUE_ELEMENT] = dirtyState;
		g_hudElementStateCache[TARGET_HULL_VALUE_ELEMENT] = dirtyState;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_CARGO_ELEMENT] = dirtyState;
		g_hudElementStateCache[TARGET_CARGO_ELEMENT] = dirtyState;
#ifdef XVT_MODERN
		XvtCockpitText_ClearField(XVT_COCKPIT_TEXT_TARGET_CARGO);
#endif
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_UNUSED_ELEMENT] = dirtyState;
		g_hudElementStateCache[TARGET_UNUSED_ELEMENT] = dirtyState;
		g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
				       TARGET_DETAIL_ELEMENT] = dirtyState;
		g_hudElementStateCache[TARGET_DETAIL_ELEMENT] = dirtyState;
#ifdef XVT_MODERN
		XvtCockpitText_ClearField(XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
		if (useLeftAlignedDetails ||
		    g_hudInstrumentSetBaseIndex !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
					       TARGET_ALT_PANEL_ELEMENT] =
				dirtyState;
			g_hudElementStateCache[TARGET_ALT_PANEL_ELEMENT] =
				dirtyState;
			if (g_hudInstrumentSetBaseIndex !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				FlightText_SetFontTier(0);
			} else {
				FlightText_SetFontTier(2);
			}
		} else {
			FlightText_SetFontTier(2);
		}
		FlightText_SetBackgroundColor(0x30);
		FlightText_SetClearLineBackground(1);

		if (previousTargetObjectIndex == -1 ||
		    previousTargetObjectIndex == -2 ||
		    previousTargetObjectIndex == -3) {
			uint16_t percentWidth;

			if (useLeftAlignedDetails ||
			    g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240) {
				FlightText_SetColor(0x46);
			} else {
				FlightText_SetColor(0x45);
			}
			FlightText_SetClipRect(0, 0, g_screenWidth,
					       g_screenHeight);
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_DISTANCE_LABEL_ELEMENT]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_DISTANCE_LABEL_ELEMENT]
						.y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_RANGE_LABEL,
				g_strCmdThreatDisplayText[CMD_THREAT_STR_DIST],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(
				g_strCmdThreatDisplayText[CMD_THREAT_STR_DIST]);
			FlightText_SetCursor(
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_DISTANCE_VALUE_ELEMENT]
							.x +
					FlightText_MeasureStringWidth("00"),
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_DISTANCE_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_RANGE_SEPARATOR, ".",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flightDrawCharFn('.');
			if (useLeftAlignedDetails) {
				FlightText_SetFontTier(0);
			}
			percentWidth = FlightText_MeasureStringWidth(
				g_threeDigitWidthText);
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SHIELD_LABEL_ELEMENT]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SHIELD_LABEL_ELEMENT]
						.y);
			if (useLeftAlignedDetails ||
			    g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240) {
				FlightText_SetColor(0x46);
			} else {
				FlightText_SetColor(0x45);
			}
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_SHIELD_LABEL,
				g_strCmdThreatDisplayText[CMD_THREAT_STR_SHD],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(
				g_strCmdThreatDisplayText[CMD_THREAT_STR_SHD]);
			FlightText_SetCursor(
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_SHIELD_VALUE_ELEMENT]
							.x +
					percentWidth,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SHIELD_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_SHIELD_PERCENT, "%",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flightDrawCharFn('%');
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_HULL_LABEL_ELEMENT]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_HULL_LABEL_ELEMENT]
						.y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_HULL_LABEL,
				g_strCmdThreatDisplayText[CMD_THREAT_STR_HULL],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(
				g_strCmdThreatDisplayText[CMD_THREAT_STR_HULL]);
			FlightText_SetCursor(
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_HULL_VALUE_ELEMENT]
							.x +
					percentWidth,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_HULL_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_HULL_PERCENT, "%",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flightDrawCharFn('%');
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SYSTEM_LABEL_ELEMENT]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SYSTEM_LABEL_ELEMENT]
						.y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_SYSTEM_LABEL,
				g_strCmdThreatDisplayText[CMD_THREAT_STR_SYS],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(
				g_strCmdThreatDisplayText[CMD_THREAT_STR_SYS]);
			FlightText_SetCursor(
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_SYSTEM_VALUE_ELEMENT]
							.x +
					percentWidth,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SYSTEM_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_SYSTEM_PERCENT, "%",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flightDrawCharFn('%');
			if (useLeftAlignedDetails &&
			    g_hudInstrumentSetBaseIndex ==
				    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				FlightText_SetFontTier(2);
			}
		}

		if (currentTargetObjectIndex != UINT16_MAX) {
			uint16_t targetObjectType;

			targetObject = &g_objectTable[currentTargetObjectIndex];
			targetObjectType = targetObject->objectType;
			targetMobileObject = targetObject->mobj;
			targetCraft = targetMobileObject != NULL
					      ? targetMobileObject->pCraft
					      : NULL;
			left = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						   TARGET_NAME_ELEMENT]
				       .x;
			top = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						  TARGET_NAME_ELEMENT]
				      .y;
			FlightText_SetClipRect(
				left, top,
				left + g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_NAME_ELEMENT]
							.clipWidth,
				top + g_flightFontLineHeight + 1);
			g_flightFillClipRectFn();
			if (g_missionHeader.missionType == MISSION_TYPE_MELEE &&
			    g_flightPlayerCount > 1) {
				int16_t displayFlags =
					NORMAL_TARGET_DISPLAY_FLAGS;
				if (g_objectTable[currentTargetObjectIndex]
						    .mobj != NULL &&
				    targetCraft != NULL &&
				    g_flightMissionState.locatePlayersEnabled ==
					    0) {
					int playerTeam;

					playerTeam =
						(uint16_t)
							g_players[g_localPlayer]
								.team;
					if (targetCraft->identifiedOrderByTeam
						    [playerTeam] == 0) {
						int flightGroupIndex;
						int team;
						int hostile;

						flightGroupIndex =
							g_objectTable[currentTargetObjectIndex]
								.flightGroupIdx;
						team = g_missionFlightGroups
							       [flightGroupIndex]
								       .fg.team;
						hostile = 0;
						if (team != playerTeam) {
							hostile =
								g_missionTeams[playerTeam]
									.allies[team] ==
								0;
						}
						if (hostile == 1 &&
						    g_missionFlightGroups[flightGroupIndex]
								    .fg
								    .playerNumber !=
							    0) {
							displayFlags =
								HIDDEN_TARGET_DISPLAY_FLAGS;
						}
					}
				}
				Hud_AppendObjectDisplayName(
					currentTargetObjectIndex, displayFlags);
			} else {
				Hud_AppendObjectDisplayName(
					currentTargetObjectIndex,
					NORMAL_TARGET_DISPLAY_FLAGS);
			}
			targetObject = &g_objectTable[currentTargetObjectIndex];
			if (targetObject->playerOwnerIdx != -1) {
				if (g_flightMissionState.locatePlayersEnabled !=
					    0 ||
				    targetCraft->identifiedOrderByTeam
						    [(uint16_t)g_players
							     [g_localPlayer]
								     .team] !=
					    0 ||
				    !(g_missionFlightGroups[targetObject
								    ->flightGroupIdx]
							      .fg.team ==
						      (uint16_t)g_players
							      [g_localPlayer]
								      .team
					      ? 0
					      : g_missionTeams[(uint16_t)g_players
								       [g_localPlayer]
									       .team]
								.allies[g_missionFlightGroups
										[targetObject
											 ->flightGroupIdx]
											.fg
											.team] ==
							0)) {
					FlightText_AppendScratchChar('-');
					FlightText_AppendScratchString(
						NetSession_GetPlayerName(
							g_objectTable[currentTargetObjectIndex]
								.playerOwnerIdx));
				}
			}
			FlightText_SetCursor(left, top);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_TARGET_NAME,
						   g_flightTextScratchBuffer,
						   XVT_COCKPIT_ALIGN_CENTER);
#endif
			FlightText_DrawStringCentered(
				g_flightTextScratchBuffer);

			if (g_projectileObjectSlotStart <=
				    currentTargetObjectIndex &&
			    g_projectileObjectSlotEnd >
				    currentTargetObjectIndex &&
			    g_projectileTypeData.warheadClass
					    [targetObjectType -
					     PROJECTILE_OBJECT_TYPE_FIRST] !=
				    0) {
				WarheadGuidanceState *guidance =
					g_objectTable
						[(uint16_t)g_players[g_localPlayer]
							 .currentTargetObjectIdx]
							.mobj->pWarheadGuidance;
				if (guidance->homingTier == 0 ||
				    guidance->targetObjIdx == UINT16_MAX) {
					FlightText_SetScratch(
						g_strMeshComponentNames
							[MESH_COMPONENT_32_DASHES]);
				} else if (g_objectTable[guidance->targetObjIdx]
						   .playerOwnerIdx ==
					   g_localPlayer) {
					FlightText_SetScratch(
						g_strCmdThreatDisplayText
							[CMD_THREAT_STR_THIS_CRAFT]);
				} else {
					Hud_AppendObjectDisplayName(
						guidance->targetObjIdx,
						SHORT_TARGET_DISPLAY_FLAGS);
				}
				left = g_hudElementLayouts
					       [g_hudInstrumentSetBaseIndex +
						TARGET_DETAIL_ELEMENT]
						       .x;
				top = g_hudElementLayouts
					      [g_hudInstrumentSetBaseIndex +
					       TARGET_DETAIL_ELEMENT]
						      .y;
				FlightText_SetClipRect(
					left, top,
					left + g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.clipWidth,
					top + g_flightFontLineHeight + 1);
				g_flightFillClipRectFn();
				FlightText_SetCursor(left, top);
				FlightText_SetColor(0x4E);
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_TARGET_DETAIL,
					g_flightTextScratchBuffer,
					XVT_COCKPIT_ALIGN_RIGHT);
#endif
				FlightText_DrawStringRightAligned(
					g_flightTextScratchBuffer);
			}
		} else {
			if (g_hudInstrumentSetBaseIndex !=
			    HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX) {
				if (useLeftAlignedDetails ||
				    g_players[g_localPlayer].mapCameraState !=
					    0) {
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordTargetCover(
						g_hudInstrumentSetBaseIndex +
						TARGET_ALT_PANEL_ELEMENT);
#endif
					Hud_DrawCachedSpriteElement(
						g_hudInstrumentSetBaseIndex +
							TARGET_ALT_PANEL_ELEMENT,
						0);
				} else {
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordTargetCover(
						g_hudInstrumentSetBaseIndex +
						TARGET_PANEL_ELEMENT);
#endif
					Hud_DrawCachedSpriteElement(
						g_hudInstrumentSetBaseIndex +
							TARGET_PANEL_ELEMENT,
						0);
				}
			} else {
#ifdef XVT_MODERN
				XvtCockpitReadouts_RecordTargetCover(
					g_hudInstrumentSetBaseIndex +
					TARGET_PANEL_ELEMENT);
#endif
				Hud_DrawCachedSpriteElement(
					g_hudInstrumentSetBaseIndex +
						TARGET_PANEL_ELEMENT,
					0);
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SYSTEM_LABEL_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_HULL_LABEL_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SHIELD_LABEL_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SYSTEM_VALUE_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_SHIELD_VALUE_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_HULL_VALUE_ELEMENT] =
						dirtyState;
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 TARGET_ALT_PANEL_ELEMENT] = dirtyState;
			}
		}
	}

	if (g_players[g_localPlayer].currentTargetObjectIdx == -1) {
		return;
	}

	FlightText_SetBackgroundColor(0x30);
	left = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
				   TARGET_NAME_ELEMENT]
		       .x;
	top = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
				  TARGET_NAME_ELEMENT]
		      .y;
	FlightText_SetClipRect(
		left, top,
		left + g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					   TARGET_NAME_ELEMENT]
				.clipWidth,
		top + g_flightFontLineHeight + 1);
	g_flightFillClipRectFn();
	if (g_missionHeader.missionType == MISSION_TYPE_MELEE &&
	    g_flightPlayerCount > 1) {
		int16_t displayFlags = NORMAL_TARGET_DISPLAY_FLAGS;
		if (g_objectTable[(uint16_t)g_players[g_localPlayer]
					  .currentTargetObjectIdx]
				    .mobj != NULL &&
		    g_objectTable[(uint16_t)g_players[g_localPlayer]
					  .currentTargetObjectIdx]
				    .mobj->pCraft != NULL &&
		    g_flightMissionState.locatePlayersEnabled == 0) {
			CraftData *displayCraft =
				g_objectTable[(uint16_t)g_players[g_localPlayer]
						      .currentTargetObjectIdx]
					.mobj->pCraft;
			int playerTeam;

			playerTeam = (uint16_t)g_players[g_localPlayer].team;
			if (displayCraft->identifiedOrderByTeam[playerTeam] ==
			    0) {
				int flightGroupIndex =
					g_objectTable
						[(uint16_t)g_players[g_localPlayer]
							 .currentTargetObjectIdx]
							.flightGroupIdx;
				int team;
				int hostile;

				team = g_missionFlightGroups[flightGroupIndex]
					       .fg.team;
				hostile = 0;
				if (team != playerTeam) {
					hostile = g_missionTeams[playerTeam]
							  .allies[team] == 0;
				}
				if (hostile == 1 &&
				    g_missionFlightGroups[flightGroupIndex]
						    .fg.playerNumber != 0) {
					displayFlags =
						HIDDEN_TARGET_DISPLAY_FLAGS;
				}
			}
		}
		Hud_AppendObjectDisplayName((uint16_t)g_players[g_localPlayer]
						    .currentTargetObjectIdx,
					    displayFlags);
	} else {
		Hud_AppendObjectDisplayName((uint16_t)g_players[g_localPlayer]
						    .currentTargetObjectIdx,
					    NORMAL_TARGET_DISPLAY_FLAGS);
	}
	targetObject = &g_objectTable[(uint16_t)g_players[g_localPlayer]
					      .currentTargetObjectIdx];
	if (targetObject->playerOwnerIdx != -1) {
		if (g_flightMissionState.locatePlayersEnabled != 0 ||
		    targetObject->mobj->pCraft->identifiedOrderByTeam
				    [(uint16_t)g_players[g_localPlayer].team] !=
			    0 ||
		    !(g_missionFlightGroups[targetObject->flightGroupIdx]
					      .fg.team ==
				      (uint16_t)g_players[g_localPlayer].team
			      ? 0
			      : g_missionTeams[(uint16_t)
						       g_players[g_localPlayer]
							       .team]
						.allies[g_missionFlightGroups
								[targetObject
									 ->flightGroupIdx]
									.fg
									.team] ==
					0)) {
			FlightText_AppendScratchString(" (");
			FlightText_AppendScratchString(NetSession_GetPlayerName(
				g_objectTable[(uint16_t)g_players[g_localPlayer]
						      .currentTargetObjectIdx]
					.playerOwnerIdx));
			FlightText_AppendScratchChar(')');
		}
	}
	FlightText_SetCursor(left, top);
#ifdef XVT_MODERN
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_TARGET_NAME,
				   g_flightTextScratchBuffer,
				   XVT_COCKPIT_ALIGN_CENTER);
#endif
	FlightText_DrawStringCentered(g_flightTextScratchBuffer);

	targetCraft = NULL;
	targetMobileObject = g_objectTable[(uint16_t)g_players[g_localPlayer]
						   .currentTargetObjectIdx]
				     .mobj;
	if (targetMobileObject != NULL) {
		targetCraft = targetMobileObject->pCraft;
	}
	if (targetMobileObject == NULL) {
		shieldPercentage = 0;
	} else {
		unsigned int shieldTotal;
		unsigned int shieldAverage;
		unsigned int maxShield;

		if (targetCraft == NULL ||
		    targetCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP ||
		    targetCraft->objectKind == CRAFT_OBJECT_KIND_EXPLODING) {
			maxShield = 0;
		} else {
			shieldTotal = targetCraft->shieldEnergy[0] +
				      targetCraft->shieldEnergy[1];
			shieldAverage = shieldTotal >> 1;
			maxShield = 2 * g_modelDefs[targetCraft->modelIndex]
						.shieldStrength;
		}
		if (maxShield != 0) {
			shieldPercentage = (uint16_t)MATH2_longratioQ16(
				shieldAverage, maxShield);
			shieldPercentage =
				2 * (shieldPercentage / PERCENTAGE_SCALE);
			if (shieldTotal != 0 && shieldPercentage == 0) {
				shieldPercentage = 1;
			}
		} else {
			shieldPercentage = 0;
		}
	}
	if (useLeftAlignedDetails) {
		FlightText_SetFontTier(0);
	}
	Hud_DrawCachedNumericElement(g_hudInstrumentSetBaseIndex +
					     TARGET_SHIELD_VALUE_ELEMENT,
				     (int16_t)shieldPercentage, 1);
	if (useLeftAlignedDetails &&
	    g_hudInstrumentSetBaseIndex == HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		FlightText_SetFontTier(2);
	}

	if ((uint16_t)g_players[g_localPlayer].currentTargetObjectIdx <
		    g_activeRegionCraftObjectSlotEnd &&
	    targetCraft != NULL) {
		if (targetCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP ||
		    targetCraft->objectKind == CRAFT_OBJECT_KIND_EXPLODING) {
			hullPercentage = 0;
		} else if (targetCraft->hullDamage > targetCraft->hullMax) {
			hullPercentage = 1;
		} else {
			hullPercentage = (uint16_t)MATH2_longratioQ16(
				targetCraft->hullMax - targetCraft->hullDamage,
				targetCraft->hullMax);
			hullPercentage = hullPercentage / PERCENTAGE_SCALE;
			if (hullPercentage == 0) {
				hullPercentage = 1;
			}
		}
	} else {
		hullPercentage = 100;
	}
	if (useLeftAlignedDetails) {
		FlightText_SetFontTier(0);
	}
	Hud_DrawCachedNumericElement(g_hudInstrumentSetBaseIndex +
					     TARGET_HULL_VALUE_ELEMENT,
				     hullPercentage, 1);
	if (useLeftAlignedDetails &&
	    g_hudInstrumentSetBaseIndex == HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		FlightText_SetFontTier(2);
	}

	targetObject = &g_objectTable[(uint16_t)g_players[g_localPlayer]
					      .currentTargetObjectIdx];
	if (targetObject->mobj != NULL) {
		if (targetCraft == NULL ||
		    targetCraft->objectKind == CRAFT_OBJECT_KIND_BREAKING_UP ||
		    targetCraft->objectKind == CRAFT_OBJECT_KIND_EXPLODING) {
			systemPercentage = 0;
		} else if (targetCraft->objectKind ==
				   CRAFT_OBJECT_KIND_BREAKING_UP ||
			   targetCraft->objectKind ==
				   CRAFT_OBJECT_KIND_EXPLODING ||
			   targetCraft->workingSubsystems == 0) {
			systemPercentage = 0;
		} else {
			uint16_t systemStrength =
				g_modelDefs[targetCraft->modelIndex]
					.systemStrength;
			uint16_t subsystemDamage =
				(uint16_t)targetCraft->subsystemDamage;
			if (systemStrength <= subsystemDamage) {
				systemPercentage = 0;
			} else {
				systemPercentage = MATH2_ratioQ16(
					systemStrength - subsystemDamage,
					systemStrength);
				systemPercentage /= PERCENTAGE_SCALE;
			}
			if (systemPercentage > 25 &&
			    targetCraft->weaponFireInhibitTimer != 0) {
				systemPercentage = 25;
			}
		}
	} else {
		systemPercentage =
			targetObject->typeSpecificWord == 0 ? 0 : 100;
	}
	if (useLeftAlignedDetails) {
		FlightText_SetFontTier(0);
	}
	Hud_DrawCachedNumericElement(g_hudInstrumentSetBaseIndex +
					     TARGET_SYSTEM_VALUE_ELEMENT,
				     (int16_t)systemPercentage, 1);
	if (useLeftAlignedDetails &&
	    g_hudInstrumentSetBaseIndex == HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		FlightText_SetFontTier(2);
	}

	if (g_players[g_localPlayer].mapCameraState == 0 &&
	    (g_objectTable[(uint16_t)g_players[g_localPlayer]
				   .currentTargetObjectIdx]
			     .genusId == CRAFT_GENUS_STARSHIP ||
	     g_objectTable[(uint16_t)g_players[g_localPlayer]
				   .currentTargetObjectIdx]
			     .genusId == CRAFT_GENUS_PLATFORM)) {
		Object_DirectionAndDistanceToMeshCenter(
			g_players[g_localPlayer].objectIndex,
			(uint16_t)g_players[g_localPlayer]
				.currentTargetObjectIdx,
			(uint16_t)g_players[g_localPlayer]
				.selectedTargetComponent);
	} else {
		Player_ComputePolarToObjectRef(
			g_localPlayer, (uint16_t)g_players[g_localPlayer]
					       .currentTargetObjectIdx);
	}
	Hud_DrawTargetDistance(trig2_polardistance);

	targetObject = &g_objectTable[(uint16_t)g_players[g_localPlayer]
					      .currentTargetObjectIdx];
	if (targetObject->genusId != CRAFT_GENUS_STARFIGHTER) {
		uint16_t cargoState = 2;
		const char *cargoText =
			g_strMeshComponentNames[MESH_COMPONENT_32_DASHES];
		int8_t componentDisplayState;

		if ((uint16_t)g_players[g_localPlayer].currentTargetObjectIdx <
			    g_activeRegionCraftObjectSlotEnd &&
		    targetCraft != NULL &&
		    g_objectTable[(uint16_t)g_players[g_localPlayer]
					  .currentTargetObjectIdx]
				    .mobj->family == 0) {
			if (targetCraft->identifiedOrderByTeam
					    [(uint16_t)g_players[g_localPlayer]
						     .team] == 0 ||
			    targetCraft->objectKind ==
				    CRAFT_OBJECT_KIND_BREAKING_UP ||
			    targetCraft->objectKind ==
				    CRAFT_OBJECT_KIND_EXPLODING) {
				cargoState = 0;
				cargoText = g_strUnknown;
			} else {
				cargoState = 1;
				cargoText = targetCraft->specialCargoName;
				if (targetCraft->specialCargoName[0] == '\0') {
					cargoState = 2;
					cargoText = g_strCmdThreatDisplayText
						[CMD_THREAT_STR_NO_CARGO];
				}
			}
		}
		if (cargoState !=
		    g_hudElementStateCache[TARGET_CARGO_ELEMENT]) {
			g_hudElementStateCache[TARGET_CARGO_ELEMENT] =
				cargoState;
			left = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						   TARGET_CARGO_ELEMENT]
				       .x;
			top = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						  TARGET_CARGO_ELEMENT]
				      .y;
			FlightText_SetClipRect(
				left, top,
				left + g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_CARGO_ELEMENT]
							.clipWidth,
				top + g_flightFontLineHeight + 1);
			g_flightFillClipRectFn();
			FlightText_SetCursor(left, top);
			FlightText_SetColor(0x46);
			if (!useLeftAlignedDetails) {
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_TARGET_CARGO,
					cargoText, XVT_COCKPIT_ALIGN_RIGHT);
#endif
				FlightText_DrawStringRightAligned(cargoText);
			} else {
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_TARGET_CARGO,
					cargoText, XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(cargoText);
			}
		}

		componentDisplayState = 0;
		if (targetMobileObject == NULL) {
			componentDisplayState = 1;
		} else if (targetCraft != NULL) {
			componentDisplayState = 2;
		}
		if (componentDisplayState > 0) {
			uint16_t selectedComponent = NO_SELECTED_COMPONENT;
			if (componentDisplayState == 2) {
				selectedComponent =
					g_players[g_localPlayer]
						.selectedTargetComponent;
			}
			FlightText_SetColor(0x4E);
			if (selectedComponent !=
			    g_hudElementStateCache[TARGET_DETAIL_ELEMENT]) {
				g_hudElementStateCache[TARGET_DETAIL_ELEMENT] =
					selectedComponent;
				left = g_hudElementLayouts
					       [g_hudInstrumentSetBaseIndex +
						TARGET_DETAIL_ELEMENT]
						       .x;
				top = g_hudElementLayouts
					      [g_hudInstrumentSetBaseIndex +
					       TARGET_DETAIL_ELEMENT]
						      .y;
				FlightText_SetClipRect(
					left, top,
					left + g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.clipWidth,
					top + g_flightFontLineHeight + 1);
				g_flightFillClipRectFn();
				FlightText_SetCursor(left, top);
				if (selectedComponent ==
				    NO_SELECTED_COMPONENT) {
#ifdef XVT_MODERN
					XvtCockpitText_RecordField(
						XVT_COCKPIT_TEXT_TARGET_DETAIL,
						g_strMeshComponentNames
							[MESH_COMPONENT_32_DASHES],
						XVT_COCKPIT_ALIGN_RIGHT);
#endif
					FlightText_DrawStringRightAligned(
						g_strMeshComponentNames
							[MESH_COMPONENT_32_DASHES]);
				} else {
					int meshIndex =
						(uint16_t)g_players[g_localPlayer]
							.selectedTargetComponent;
					int objectType =
						g_objectTable
							[(uint16_t)g_players
								 [g_localPlayer]
									 .currentTargetObjectIdx]
								.objectType;
					uint16_t meshType;

					if (objectType < 73) {
						if (meshIndex < 0) {
							meshType = 0;
						} else {
							int meshCount =
								g_objectTypeMeshCache
									[objectType]
										.meshCount;
							if (meshIndex >=
							    meshCount) {
								meshIndex =
									meshCount -
									1;
							}
							meshType =
								g_objectTypeMeshCache[objectType]
									.meshTypes
										[meshIndex];
						}
					} else {
						meshType =
							ModelMesh_GetObjectTypeMeshType(
								objectType,
								meshIndex);
					}
					if (targetObject->genusId ==
						    CRAFT_GENUS_STARFIGHTER &&
					    meshType ==
						    MESH_COMPONENT_07_BRIDGE) {
						meshType =
							MESH_COMPONENT_26_COCKPIT;
					}
					if ((targetObject->objectType == 38 ||
					     targetObject->objectType == 39) &&
					    meshType ==
						    MESH_COMPONENT_07_BRIDGE) {
						meshType =
							MESH_COMPONENT_26_COCKPIT;
					}
#ifdef XVT_MODERN
					XvtCockpitText_RecordField(
						XVT_COCKPIT_TEXT_TARGET_DETAIL,
						g_strMeshComponentNames
							[meshType],
						XVT_COCKPIT_ALIGN_RIGHT);
#endif
					FlightText_DrawStringRightAligned(
						g_strMeshComponentNames
							[meshType]);
				}
			}
		}
		return;
	}

	{
		int16_t ownershipDisplayMode;

		if (g_activeRegionCraftObjectSlotEnd <=
			    (uint16_t)g_players[g_localPlayer]
				    .currentTargetObjectIdx ||
		    targetObject->mobj == NULL) {
			ownershipDisplayMode = 0;
		} else if (targetObject->playerOwnerIdx == -1) {
			ownershipDisplayMode = 1;
			if (g_missionHeader.missionType == MISSION_TYPE_MELEE &&
			    g_flightPlayerCount > 1 && targetCraft != NULL &&
			    g_flightMissionState.locatePlayersEnabled == 0) {
				int playerTeam;

				playerTeam =
					(uint16_t)g_players[g_localPlayer].team;
				if (targetCraft->identifiedOrderByTeam
					    [playerTeam] == 0) {
					int team;
					int hostile;

					team = g_missionFlightGroups
						       [targetObject
								->flightGroupIdx]
							       .fg.team;
					hostile =
						team == playerTeam
							? 0
							: g_missionTeams[playerTeam]
									  .allies[team] ==
								  0;
					if (hostile == 1 &&
					    g_missionFlightGroups
							    [targetObject
								     ->flightGroupIdx]
								    .fg
								    .playerNumber !=
						    0) {
						ownershipDisplayMode = 0;
					}
				}
			}
		} else {
			ownershipDisplayMode =
				g_missionHeader.missionType ==
						MISSION_TYPE_COMBAT
					? -1
					: 0;
		}

		if (ownershipDisplayMode == 1) {
			MobileObject *orderMobileObject = targetObject->mobj;
			CraftData *orderCraft = orderMobileObject->pCraft;
			AiController *aiController;
			uint16_t displayPlanId;
			uint16_t aiTargetObjectIndex;

			if (orderCraft == NULL) {
				return;
			}
			aiController = &orderCraft->aiController;
			displayPlanId = aiController->pendingPlanId;
			if (orderCraft->workingSubsystems == 0) {
				displayPlanId =
					(uint16_t)pai_FindPlanIdByNameOrZero(
						"disabledpln");
			} else if (orderMobileObject->speed == 0) {
				const char *planName =
					g_planTable[aiController->pendingPlanId]
						.name;
				if (strcmp(planName, "flyhomepln") == 0 ||
				    strcmp(planName, "followhomepln") == 0 ||
				    strcmp(planName, "flyhomeevadepln") == 0 ||
				    strcmp(planName, "followhomeevadepln") ==
					    0 ||
				    strcmp(planName, "enterhangarpln") == 0 ||
				    strcmp(planName, "exithangarpln") == 0 ||
				    strcmp(planName, "intohyperspacepln") ==
					    0 ||
				    strcmp(planName, "outofhyperspacepln") ==
					    0 ||
				    strcmp(planName, "starshipintohyperpln") ==
					    0 ||
				    strcmp(planName, "starshipfollowhomepln") ==
					    0) {
					displayPlanId = (uint16_t)
						pai_FindPlanIdByNameOrZero(
							"waitpln");
				}
			}
			if (displayPlanId !=
			    g_hudElementStateCache[TARGET_CARGO_ELEMENT]) {
				const char *statusText;
				const char *statusChar;
				uint16_t wordLength;

				g_hudElementStateCache[TARGET_CARGO_ELEMENT] =
					displayPlanId;
				left = g_hudElementLayouts
					       [g_hudInstrumentSetBaseIndex +
						TARGET_CARGO_ELEMENT]
						       .x;
				top = g_hudElementLayouts
					      [g_hudInstrumentSetBaseIndex +
					       TARGET_CARGO_ELEMENT]
						      .y;
				FlightText_SetClipRect(
					left, top,
					left + g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_CARGO_ELEMENT]
								.clipWidth,
					top + g_flightFontLineHeight + 1);
				g_flightFillClipRectFn();
				FlightText_SetCursor(left, top);
				FlightText_SetColor(0x46);
				statusText = g_strInFlightMessages
					[g_planReportMessageIdByPlanId
						 [displayPlanId]];
				statusChar = statusText;
				for (wordLength = 0;
				     wordLength < MAX_STATUS_FIRST_WORD_LENGTH;
				     ++wordLength, ++statusChar) {
					if (*statusChar == ' ') {
						strncpy(statusFirstWord,
							statusText, wordLength);
						statusFirstWord[wordLength] =
							'\0';
						break;
					}
				}
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_TARGET_CARGO,
					statusFirstWord,
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(statusFirstWord);
			}

			aiTargetObjectIndex = aiController->targetObjIdx;
			if (aiTargetObjectIndex == 255 ||
			    aiTargetObjectIndex == UINT16_MAX) {
				if (g_hudElementStateCache
					    [TARGET_DETAIL_ELEMENT] !=
				    invalidState) {
					g_hudElementStateCache
						[TARGET_DETAIL_ELEMENT] =
							invalidState;
#ifdef XVT_MODERN
					XvtCockpitText_ClearField(
						XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
					FlightText_SetClipRect(
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.y,
						g_hudElementLayouts
								[g_hudInstrumentSetBaseIndex +
								 TARGET_DETAIL_ELEMENT]
									.x +
							g_hudElementLayouts
								[g_hudInstrumentSetBaseIndex +
								 TARGET_DETAIL_ELEMENT]
									.clipWidth,
						g_hudElementLayouts
								[g_hudInstrumentSetBaseIndex +
								 TARGET_DETAIL_ELEMENT]
									.y +
							g_flightFontLineHeight +
							1);
					g_flightFillClipRectFn();
				}
			} else if (aiTargetObjectIndex !=
				   g_hudElementStateCache
					   [TARGET_DETAIL_ELEMENT]) {
				g_hudElementStateCache[TARGET_DETAIL_ELEMENT] =
					aiTargetObjectIndex;
				left = g_hudElementLayouts
					       [g_hudInstrumentSetBaseIndex +
						TARGET_DETAIL_ELEMENT]
						       .x;
				top = g_hudElementLayouts
					      [g_hudInstrumentSetBaseIndex +
					       TARGET_DETAIL_ELEMENT]
						      .y;
				FlightText_SetClipRect(
					left, top,
					left + g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.clipWidth,
					top + g_flightFontLineHeight + 1);
				g_flightFillClipRectFn();
				FlightText_SetCursor(left, top);
				Hud_AppendObjectDisplayName(
					aiTargetObjectIndex,
					NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_TARGET_DETAIL,
					g_flightTextScratchBuffer,
					XVT_COCKPIT_ALIGN_RIGHT);
#endif
				FlightText_DrawStringRightAligned(
					g_flightTextScratchBuffer);
			}
			return;
		}

		if (ownershipDisplayMode == 0) {
			FlightText_SetBackgroundColor(0x30);
			if (g_hudElementStateCache[TARGET_CARGO_ELEMENT] !=
			    invalidState) {
				g_hudElementStateCache[TARGET_CARGO_ELEMENT] =
					invalidState;
#ifdef XVT_MODERN
				XvtCockpitText_ClearField(
					XVT_COCKPIT_TEXT_TARGET_CARGO);
#endif
				FlightText_SetClipRect(
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_CARGO_ELEMENT]
							.x,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_CARGO_ELEMENT]
							.y,
					g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_CARGO_ELEMENT]
								.x +
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_CARGO_ELEMENT]
								.clipWidth,
					g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_CARGO_ELEMENT]
								.y +
						g_flightFontLineHeight + 1);
				g_flightFillClipRectFn();
			}
			if (g_hudElementStateCache[TARGET_DETAIL_ELEMENT] !=
			    invalidState) {
				g_hudElementStateCache[TARGET_DETAIL_ELEMENT] =
					invalidState;
#ifdef XVT_MODERN
				XvtCockpitText_ClearField(
					XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
				FlightText_SetClipRect(
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_DETAIL_ELEMENT]
							.x,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 TARGET_DETAIL_ELEMENT]
							.y,
					g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.x +
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.clipWidth,
					g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.y +
						g_flightFontLineHeight + 1);
				g_flightFillClipRectFn();
			}
		} else {
			int playerOwnerIndex = targetObject->playerOwnerIdx;
			if (g_players[playerOwnerIndex].hyperspacePhase != 0) {
				if (g_hudElementStateCache
					    [TARGET_CARGO_ELEMENT] !=
				    IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA) {
					const char *statusChar;
					uint16_t wordLength;

					g_hudElementStateCache[TARGET_CARGO_ELEMENT] =
						IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA;
					left = g_hudElementLayouts
						       [g_hudInstrumentSetBaseIndex +
							TARGET_CARGO_ELEMENT]
							       .x;
					top = g_hudElementLayouts
						      [g_hudInstrumentSetBaseIndex +
						       TARGET_CARGO_ELEMENT]
							      .y;
					FlightText_SetClipRect(
						left, top,
						left + g_hudElementLayouts
								[g_hudInstrumentSetBaseIndex +
								 TARGET_CARGO_ELEMENT]
									.clipWidth,
						top + g_flightFontLineHeight +
							1);
					g_flightFillClipRectFn();
					FlightText_SetCursor(left, top);
					FlightText_SetColor(0x46);
					statusChar = g_strInFlightMessages
						[IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA];
					for (wordLength = 0;
					     wordLength <
					     MAX_STATUS_FIRST_WORD_LENGTH;
					     ++wordLength, ++statusChar) {
						if (*statusChar == ' ') {
							strncpy(statusFirstWord,
								g_strInFlightMessages
									[IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA],
								wordLength);
							statusFirstWord
								[wordLength] =
									'\0';
							break;
						}
					}
#ifdef XVT_MODERN
					XvtCockpitText_RecordField(
						XVT_COCKPIT_TEXT_TARGET_CARGO,
						statusFirstWord,
						XVT_COCKPIT_ALIGN_LEFT);
#endif
					FlightText_DrawString(statusFirstWord);
				}
				if (g_hudElementStateCache
					    [TARGET_DETAIL_ELEMENT] !=
				    dirtyState) {
					g_hudElementStateCache
						[TARGET_DETAIL_ELEMENT] =
							dirtyState;
#ifdef XVT_MODERN
					XvtCockpitText_ClearField(
						XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
					FlightText_SetClipRect(
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 TARGET_DETAIL_ELEMENT]
								.y,
						g_hudElementLayouts
								[g_hudInstrumentSetBaseIndex +
								 TARGET_DETAIL_ELEMENT]
									.x +
							g_hudElementLayouts
								[g_hudInstrumentSetBaseIndex +
								 TARGET_DETAIL_ELEMENT]
									.clipWidth,
						g_hudElementLayouts
								[g_hudInstrumentSetBaseIndex +
								 TARGET_DETAIL_ELEMENT]
									.y +
							g_flightFontLineHeight +
							1);
					g_flightFillClipRectFn();
				}
			} else {
				uint16_t ownerTargetObjectIndex =
					(uint16_t)g_players[playerOwnerIndex]
						.currentTargetObjectIdx;
				if (ownerTargetObjectIndex != UINT16_MAX) {
					if (ownerTargetObjectIndex !=
					    g_hudElementStateCache
						    [TARGET_CARGO_ELEMENT]) {
						const char *statusChar;
						uint16_t wordLength;

						g_hudElementStateCache
							[TARGET_CARGO_ELEMENT] =
								ownerTargetObjectIndex;
						left = g_hudElementLayouts
							       [g_hudInstrumentSetBaseIndex +
								TARGET_CARGO_ELEMENT]
								       .x;
						top = g_hudElementLayouts
							      [g_hudInstrumentSetBaseIndex +
							       TARGET_CARGO_ELEMENT]
								      .y;
						FlightText_SetClipRect(
							left, top,
							left + g_hudElementLayouts
									[g_hudInstrumentSetBaseIndex +
									 TARGET_CARGO_ELEMENT]
										.clipWidth,
							top + g_flightFontLineHeight +
								1);
						g_flightFillClipRectFn();
						FlightText_SetCursor(left, top);
						FlightText_SetColor(0x46);
						statusChar = g_strInFlightMessages
							[IFMSG_165_ATTACKING_TARGET];
						for (wordLength = 0;
						     wordLength <
						     MAX_STATUS_FIRST_WORD_LENGTH;
						     ++wordLength,
						    ++statusChar) {
							if (*statusChar ==
							    ' ') {
								strncpy(statusFirstWord,
									g_strInFlightMessages
										[IFMSG_165_ATTACKING_TARGET],
									wordLength);
								statusFirstWord
									[wordLength] =
										'\0';
								break;
							}
						}
#ifdef XVT_MODERN
						XvtCockpitText_RecordField(
							XVT_COCKPIT_TEXT_TARGET_CARGO,
							statusFirstWord,
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						FlightText_DrawString(
							statusFirstWord);
					}
					if (ownerTargetObjectIndex !=
					    g_hudElementStateCache
						    [TARGET_DETAIL_ELEMENT]) {
						g_hudElementStateCache
							[TARGET_DETAIL_ELEMENT] =
								ownerTargetObjectIndex;
						left = g_hudElementLayouts
							       [g_hudInstrumentSetBaseIndex +
								TARGET_DETAIL_ELEMENT]
								       .x;
						top = g_hudElementLayouts
							      [g_hudInstrumentSetBaseIndex +
							       TARGET_DETAIL_ELEMENT]
								      .y;
						FlightText_SetClipRect(
							left, top,
							left + g_hudElementLayouts
									[g_hudInstrumentSetBaseIndex +
									 TARGET_DETAIL_ELEMENT]
										.clipWidth,
							top + g_flightFontLineHeight +
								1);
						g_flightFillClipRectFn();
						FlightText_SetCursor(left, top);
						Hud_AppendObjectDisplayName(
							(uint16_t)g_players
								[g_objectTable
									 [(uint16_t)g_players
										  [g_localPlayer]
											  .currentTargetObjectIdx]
										 .playerOwnerIdx]
									.currentTargetObjectIdx,
							NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
						XvtCockpitText_RecordField(
							XVT_COCKPIT_TEXT_TARGET_DETAIL,
							g_flightTextScratchBuffer,
							XVT_COCKPIT_ALIGN_RIGHT);
#endif
						FlightText_DrawStringRightAligned(
							g_flightTextScratchBuffer);
					}
				} else {
					if (g_hudElementStateCache
						    [TARGET_CARGO_ELEMENT] !=
					    IFMSG_160_PATROLLING) {
						const char *statusChar;
						uint16_t wordLength;

						g_hudElementStateCache
							[TARGET_CARGO_ELEMENT] =
								IFMSG_160_PATROLLING;
						left = g_hudElementLayouts
							       [g_hudInstrumentSetBaseIndex +
								TARGET_CARGO_ELEMENT]
								       .x;
						top = g_hudElementLayouts
							      [g_hudInstrumentSetBaseIndex +
							       TARGET_CARGO_ELEMENT]
								      .y;
						FlightText_SetClipRect(
							left, top,
							left + g_hudElementLayouts
									[g_hudInstrumentSetBaseIndex +
									 TARGET_CARGO_ELEMENT]
										.clipWidth,
							top + g_flightFontLineHeight +
								1);
						g_flightFillClipRectFn();
						FlightText_SetCursor(left, top);
						FlightText_SetColor(0x46);
						statusChar = g_strInFlightMessages
							[IFMSG_160_PATROLLING];
						for (wordLength = 0;
						     wordLength <
						     MAX_STATUS_FIRST_WORD_LENGTH;
						     ++wordLength,
						    ++statusChar) {
							if (*statusChar ==
							    ' ') {
								strncpy(statusFirstWord,
									g_strInFlightMessages
										[IFMSG_160_PATROLLING],
									wordLength);
								statusFirstWord
									[wordLength] =
										'\0';
								break;
							}
						}
#ifdef XVT_MODERN
						XvtCockpitText_RecordField(
							XVT_COCKPIT_TEXT_TARGET_CARGO,
							statusFirstWord,
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						FlightText_DrawString(
							statusFirstWord);
					}
					if ((uint16_t)g_hudElementStateCache
						    [TARGET_DETAIL_ELEMENT] !=
					    WAYPOINT_ZERO_OBJECT_REF) {
						g_hudElementStateCache
							[TARGET_DETAIL_ELEMENT] =
								(int16_t)
									WAYPOINT_ZERO_OBJECT_REF;
						left = g_hudElementLayouts
							       [g_hudInstrumentSetBaseIndex +
								TARGET_DETAIL_ELEMENT]
								       .x;
						top = g_hudElementLayouts
							      [g_hudInstrumentSetBaseIndex +
							       TARGET_DETAIL_ELEMENT]
								      .y;
						FlightText_SetClipRect(
							left, top,
							left + g_hudElementLayouts
									[g_hudInstrumentSetBaseIndex +
									 TARGET_DETAIL_ELEMENT]
										.clipWidth,
							top + g_flightFontLineHeight +
								1);
						g_flightFillClipRectFn();
						FlightText_SetCursor(left, top);
						Hud_AppendObjectDisplayName(
							WAYPOINT_ZERO_OBJECT_REF,
							NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
						XvtCockpitText_RecordField(
							XVT_COCKPIT_TEXT_TARGET_DETAIL,
							g_flightTextScratchBuffer,
							XVT_COCKPIT_ALIGN_RIGHT);
#endif
						FlightText_DrawStringRightAligned(
							g_flightTextScratchBuffer);
					}
				}
			}
		}
	}
}

/* Despite the name, it does not append: it empties g_flightTextScratchBuffer
 * and writes objectRef's display name there. For a craft, displayFlags bit 0
 * adds the model's short name, bit 1 the flight group's name and the craft
 * number when Hud_MissionFG_GetCraftNumberIfShown gives one (at most 999), and
 * bit 2 a ":" after the first part; bits 0 and 1 together put ": " there.
 * Another mobile object gets only its warhead, satellite, mine, probe or buoy
 * name, under bit 0. Each part starts with a 0xFE color escape chosen by IFF:
 * Q, I, E, U or M before the first part and R, J, F, V or N before the group,
 * for IFF 0, 1 or 4, 2, 5 and any other; an object with no mobile object takes
 * its group's IFF and shows IFF 5 with V. A reference of 0x8000 or more is
 * waypoint objectRef - 0x8000, written after a C escape when bit 0 is set, else
 * left empty. Object type 0 gives the dashes of g_strMeshComponentNames[32]. */
// FUNCTION: XVT 0x43C290
void Hud_AppendObjectDisplayName(uint16_t objectRef, int16_t displayFlags)
{
	int objectIndex;
	ObjectRecord *object;
	uint16_t objectType;
	int8_t iff;
	MobileObject *mobileObject;
	CraftData *craft;
	int flightGroupIdx;
	int16_t craftNumber;
	uint8_t staticIff;
	uint16_t hundredsDigit;
	uint16_t tensDigit;
	uint16_t onesDigit;

	g_flightTextScratchBuffer[0] = 0;
	if (objectRef >= 0x8000) {
		if ((displayFlags & 1) != 0) {
			FlightText_AppendScratchChar(254);
			FlightText_AppendScratchChar(67);
			FlightText_AppendScratchString(g_strWaypointNames[(
				uint16_t)(objectRef + 0x8000)]);
		}
		return;
	}

	objectIndex = objectRef;
	object = &g_objectTable[objectIndex];
	if (g_objectTable[objectIndex].mobj != NULL) {
		objectType = object->objectType;
		if (objectType != 0) {
			FlightText_AppendScratchChar(254);
			iff = g_objectTable[objectIndex].mobj->iff;
			if (iff == 0) {
				FlightText_AppendScratchChar(81);
			} else if (iff == 1 || iff == 4) {
				FlightText_AppendScratchChar(73);
			} else if (iff == 2) {
				FlightText_AppendScratchChar(69);
			} else if (iff == 5) {
				FlightText_AppendScratchChar(85);
			} else {
				FlightText_AppendScratchChar(77);
			}

			mobileObject = g_objectTable[objectIndex].mobj;
			if (mobileObject->family == 0) {
				craft = mobileObject->pCraft;
				if ((displayFlags & 1) != 0) {
					FlightText_AppendScratchString(
						g_modelDefs[craft->modelIndex]
							.name);
				}
				if ((displayFlags & 4) != 0) {
					FlightText_AppendScratchChar(58);
				} else if ((displayFlags & 3) == 3) {
					FlightText_AppendScratchChar(58);
					FlightText_AppendScratchChar(32);
				}
				if ((displayFlags & 2) != 0) {
					FlightText_AppendScratchChar(254);
					iff = g_objectTable[objectIndex]
						      .mobj->iff;
					if (iff == 0) {
						FlightText_AppendScratchChar(
							82);
					} else if (iff == 1 || iff == 4) {
						FlightText_AppendScratchChar(
							74);
					} else if (iff == 2) {
						FlightText_AppendScratchChar(
							70);
					} else if (iff == 5) {
						FlightText_AppendScratchChar(
							86);
					} else {
						FlightText_AppendScratchChar(
							78);
					}
					flightGroupIdx =
						g_objectTable[objectIndex]
							.flightGroupIdx;
					FlightText_AppendScratchString(
						g_missionFlightGroups
							[flightGroupIdx]
								.fg.name);
					craftNumber =
						Hud_MissionFG_GetCraftNumberIfShown(
							flightGroupIdx, craft);
					if (craftNumber != 0) {
						FlightText_AppendScratchChar(
							32);
						if ((uint16_t)craftNumber >=
						    1000) {
							craftNumber = 999;
						}
						if ((uint16_t)craftNumber >=
						    100) {
							hundredsDigit =
								(uint16_t)
									craftNumber /
								100;
							tensDigit =
								(uint16_t)
									craftNumber %
								100 / 10;
							onesDigit =
								(uint16_t)
									craftNumber %
								100 % 10;
							FlightText_AppendScratchChar(
								hundredsDigit +
								48);
							FlightText_AppendScratchChar(
								tensDigit + 48);
							FlightText_AppendScratchChar(
								onesDigit + 48);
						} else if (
							(uint16_t)craftNumber >=
							10) {
							tensDigit =
								(uint16_t)
									craftNumber /
								10;
							onesDigit =
								(uint16_t)
									craftNumber %
								10;
							FlightText_AppendScratchChar(
								tensDigit + 48);
							FlightText_AppendScratchChar(
								onesDigit + 48);
						} else {
							FlightText_AppendScratchChar(
								craftNumber +
								48);
						}
					}
				}
			} else if ((displayFlags & 1) != 0) {
				if (objectType >= 0x8f && objectType <= 0x9b) {
					FlightText_AppendScratchString(
						g_strWarheadNames[objectType -
								  0x8f]);
				} else if (objectType >= 0x46 &&
					   objectType <= 0x54) {
					FlightText_AppendScratchString(
						g_strSatMineProbeBuoyPilotNames
							[objectType - 0x46]);
				}
			}
			return;
		}
	} else {
		objectType = object->objectType;
		if (objectType != 0) {
			FlightText_AppendScratchChar(254);
			staticIff =
				g_missionFlightGroups[g_objectTable[objectIndex]
							      .flightGroupIdx]
					.fg.iff;
			if (staticIff == 0) {
				FlightText_AppendScratchChar(81);
			} else if (staticIff == 1 || staticIff == 4) {
				FlightText_AppendScratchChar(73);
			} else if (staticIff == 2) {
				FlightText_AppendScratchChar(69);
			} else if (staticIff == 5) {
				FlightText_AppendScratchChar(86);
			} else {
				FlightText_AppendScratchChar(77);
			}
			if ((displayFlags & 1) != 0 && objectType >= 0x46 &&
			    objectType <= 0x55) {
				FlightText_AppendScratchString(
					g_strSatMineProbeBuoyPilotNames
						[objectType - 0x46]);
			}
			if ((displayFlags & 4) != 0) {
				FlightText_AppendScratchChar(58);
			} else if ((displayFlags & 3) == 3) {
				FlightText_AppendScratchChar(58);
				FlightText_AppendScratchChar(32);
			}
			if ((displayFlags & 2) != 0) {
				FlightText_AppendScratchChar(254);
				if (staticIff == 0) {
					FlightText_AppendScratchChar(82);
				} else if (staticIff == 1 || staticIff == 4) {
					FlightText_AppendScratchChar(74);
				} else if (staticIff == 2) {
					FlightText_AppendScratchChar(70);
				} else if (staticIff == 5) {
					FlightText_AppendScratchChar(86);
				} else {
					FlightText_AppendScratchChar(78);
				}
				FlightText_AppendScratchString(
					g_missionFlightGroups
						[g_objectTable[objectIndex]
							 .flightGroupIdx]
							.fg.name);
			}
			return;
		}
	}
	FlightText_SetScratch(g_strMeshComponentNames[32]);
}

/* Returns craft's craftIndexInGroup, or 0 when flight group flightGroupIdx
 * turns its craft numbering off, or has one craft, no further arrivals and no
 * global unit. */
// FUNCTION: XVT 0x43C740
int Hud_MissionFG_GetCraftNumberIfShown(int flightGroupIdx,
					const CraftData *craft)
{
	if (g_missionFlightGroups[flightGroupIdx].fg.disableWaveNumbering ==
		    1 ||
	    (g_missionFlightGroups[flightGroupIdx].fg.globalUnit == 0 &&
	     g_missionFlightGroups[flightGroupIdx].fg.numberOfCraft == 1 &&
	     g_missionFlightGroups[flightGroupIdx].fg.numberOfWaves == 0)) {
		return 0;
	}

	return craft->craftIndexInGroup;
}

/* Draws a distance on the targeting computer with two decimals: polarDistance *
 * 161 / 65,536 in hundredths, at most 99.99; the whole part in element 83 and
 * the hundredths, two digits, in element 84 of the current set. Does not check
 * polarDistance * 161 for overflow. */
// FUNCTION: XVT 0x43C890
void Hud_DrawTargetDistance(int polarDistance)
{
	uint16_t distanceHundredths;
	uint16_t wholeDistance;

	distanceHundredths = (uint32_t)(polarDistance * 161) >> 16;
	if (distanceHundredths >= 10000) {
		distanceHundredths = 9999;
	}

	wholeDistance = distanceHundredths / 100;
	Hud_DrawCachedNumericElement(g_hudInstrumentSetBaseIndex + 83,
				     wholeDistance, 1);
	Hud_DrawCachedNumericElement(g_hudInstrumentSetBaseIndex + 84,
				     distanceHundredths - wholeDistance * 100,
				     2);
}

/* Draws the lock indicator (element 52 of the current set) and sets the
 * targeting tone to the same state. With selectedWeaponMode 0 the state is 4
 * while g_targetLockActive is set, else 0; with warheads it is missileLockState
 * plus 1 with a target, else 1, and g_targetLockActive becomes 1 only while
 * missileLockState is 2. */
// FUNCTION: XVT 0x43C900
void Hud_UpdateTargetingLockIndicator(void)
{
	uint16_t indicatorState;

	if (g_players[g_localPlayer].selectedWeaponMode == 0) {
		indicatorState = g_targetLockActive == 0 ? 0 : 4;
	} else {
		if (g_players[g_localPlayer].currentTargetObjectIdx != -1) {
			indicatorState =
				g_players[g_localPlayer].missileLockState + 1;
		} else {
			indicatorState = 1;
		}
		g_targetLockActive = 1;
		if (g_players[g_localPlayer].missileLockState != 2) {
			g_targetLockActive = 0;
		}
	}

	Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 52,
				    indicatorState);
	FlightSurface_Unlock();
	fsfx_UpdateTargetingTone(indicatorState);
	FlightSurface_Lock();
}

/* Despite the name, nothing here is 3D: it draws each laser cannon's charge,
 * fire and lock sprites for the local player's craft. In the forward view with
 * HUD features 2 and 4 it draws the charge bar of each cannon placed in the
 * current set (element 3 plus the cannon): ten sprites 3, 4 or 6 pixels apart
 * by resolution, right to left when the layout's colorIndexOrWidgetParam is
 * set; up to half charge the charged ones are in state 1 over 0, above it in
 * state 2 over 1. Outside the cockpit set it draws a number from the charge
 * instead. From the bank's link mode and next cannon it works out whether the
 * cannon fires next and draws that at element 53 plus the cannon; for an
 * X-wing, Y-wing, A-wing, Z-95 or B-wing also at element 11 plus the cannon,
 * and the lock state at element 61 plus the cannon. The lock state is 2 when
 * the targeting computer works and collide_WouldShotHitTarget says a cannon due
 * to fire would hit the target. Sets g_targetLockActive to 0 first and to 1 on
 * any such hit; returns at once for a craft with no cannons. */
// FUNCTION: XVT 0x43C9A0
void Hud_DrawReticle3D(void)
{
	enum {
		LASER_CHARGE_HALF = 64,
		LASER_CHARGE_SCALE = 6,
		LASER_CHARGE_SEGMENT_COUNT = 10,
		LASER_CHARGE_DENOMINATOR = 127,
		LASER_PERCENT_SCALE = 655,
		LASER_CHARGE_ELEMENT_BASE = 3,
		LASER_SELECTION_ELEMENT_BASE = 11,
		LASER_READY_ELEMENT_BASE = 53,
		LASER_LOCK_ELEMENT_BASE = 61,
		DEFAULT_HUD_WIDTH = 320,
		DEFAULT_HUD_HEIGHT = 200,
	};

	CraftData *craft;
	uint8_t *laserGroupLastSlot;
	uint16_t laserSlotCount;
	uint16_t laserSlot;

	g_targetLockActive = 0;
	craft = g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft;
	laserSlot = 0;
	laserSlotCount = craft->laserSlotCount;
	if (laserSlotCount == 0) {
		return;
	}

	laserGroupLastSlot = g_modelDefs[craft->modelIndex].laserGroupLastSlot;
	for (; laserSlotCount > laserSlot; ++laserSlot) {
		uint16_t laserBank;
		int layoutIndex;
		int x;
		int y;
		uint16_t selector;
		int16_t charge;
		int16_t chargedSegmentState = 0;
		uint16_t readyState;
		uint16_t lockState;
		int objectIndex;
		int isRebelFighter;

		laserBank = laserSlot > *laserGroupLastSlot;
		layoutIndex = g_hudInstrumentSetBaseIndex + laserSlot;
		x = (int16_t)g_hudElementLayouts[layoutIndex +
						 LASER_CHARGE_ELEMENT_BASE]
			    .x;
		y = g_hudElementLayouts[layoutIndex + LASER_CHARGE_ELEMENT_BASE]
			    .y;
		if (x + y == 0 && g_hudInstrumentSetBaseIndex ==
					  HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			continue;
		}

		selector = g_hudElementLayouts[layoutIndex +
					       LASER_CHARGE_ELEMENT_BASE]
				   .selector;
		charge = craft->weaponSlots[laserSlot].laserCharge;
		if (g_players[g_localPlayer].viewState.hudStateLive ==
			    HUD_VIEW_FORWARD &&
		    (craft->damageStats.activeHudFeatureMask & 2) != 0 &&
		    (craft->damageStats.activeHudFeatureMask & 4) != 0) {
			uint16_t chargedSegmentCount;
			int16_t emptySegmentState;

			if (charge > 0 && (craft->workingSubsystems &
					   CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				++charge;
				if (charge <= LASER_CHARGE_HALF) {
					emptySegmentState = 0;
					chargedSegmentState = 1;
				} else {
					emptySegmentState = 1;
					chargedSegmentState = 2;
					charge -= LASER_CHARGE_HALF;
				}
				chargedSegmentCount =
					(uint16_t)charge / LASER_CHARGE_SCALE;
				if (chargedSegmentCount >=
				    LASER_CHARGE_SEGMENT_COUNT + 1) {
					chargedSegmentCount =
						LASER_CHARGE_SEGMENT_COUNT;
				}
			} else {
				chargedSegmentCount = 0;
				emptySegmentState = 0;
				chargedSegmentState = 0;
			}

			if ((uint16_t)g_hudElementStateCache
				    [layoutIndex + LASER_CHARGE_ELEMENT_BASE] !=
			    chargedSegmentCount) {
				int16_t xStep;
				uint16_t reverseDirection;

				g_hudElementStateCache
					[layoutIndex +
					 LASER_CHARGE_ELEMENT_BASE] =
						chargedSegmentCount;
				xStep = 3;
				if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240) {
					if (g_flightResolutionMode ==
					    FLIGHT_RESOLUTION_480X360) {
						xStep = 4;
					} else {
						xStep = 6;
					}
				}
				reverseDirection = 0;
				if (g_hudElementLayouts
					    [layoutIndex +
					     LASER_CHARGE_ELEMENT_BASE]
						    .colorIndexOrWidgetParam !=
				    0) {
					xStep = -xStep;
					reverseDirection = 1;
				}

				if (g_hudInstrumentSetBaseIndex !=
				    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
					if (selector != 0) {
						uint16_t chargePercent =
							MATH2_ratioQ16(
								(uint16_t)
									charge,
								LASER_CHARGE_DENOMINATOR) /
							LASER_PERCENT_SCALE;

						FlightSw_SetRenderTarget(
							g_flightOffscreenBuffer,
							g_screenWidth,
							g_screenHeight,
							g_screenWidth *
								g_flightBytesPerPixel);
						FlightText_SetFontTier(2);
						FlightText_SetClipRect(
							0, 0, g_screenWidth,
							g_screenHeight);
						FlightText_SetCursor(x, y);
						FlightText_SetBackgroundColor(
							0);
						FlightText_SetColor(0x4A);
#ifdef XVT_MODERN
						XvtCockpitReadouts_RecordNumber(
							(XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LASER_FIRST +
									     laserSlot),
							chargePercent, selector,
							1);
#endif
						FlightText_DrawDecimalNumber(
							chargePercent, selector,
							1);
						FlightSw_SetRenderTarget(
							NULL, DEFAULT_HUD_WIDTH,
							DEFAULT_HUD_HEIGHT, 0);
					}
				} else {
					uint16_t segment;

					for (segment = 0;
					     segment <
					     LASER_CHARGE_SEGMENT_COUNT;
					     ++segment) {
						uint16_t spriteState =
							segment < chargedSegmentCount
								? chargedSegmentState
								: emptySegmentState;

						g_flightBlitSpriteFn(
							g_hudPanelSpriteDataByIndex
								[selector +
								 spriteState],
							x + xStep * segment, y,
							253, reverseDirection);
					}
				}
			}
		}

		/* lockState first holds this cannon's fire-ready state, drawn on the ready indicator; after that draw
		 * it becomes the target lock state. */
		lockState = 0;
		if (charge > 0 && (craft->workingSubsystems &
				   CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
			if (g_players[g_localPlayer].selectedWeaponMode == 0 &&
			    g_players[g_localPlayer].selectedWeaponBank ==
				    laserBank) {
				switch (craft->laserState.linkMode[laserBank]) {
				case 0:
				case 4:
					readyState = 0;
					break;
				case 1:
					readyState =
						craft->laserState.nextSlot
									[laserBank] ==
								laserSlot
							? 3
							: 1;
					break;
				case 2:
					if (craft->laserState.nextSlot
							    [laserBank] ==
						    laserSlot ||
					    (laserSlotCount >= 4 &&
					     (int)craft->laserState.nextSlot
								     [laserBank] -
							     laserSlot ==
						     -2)) {
						readyState = 3;
					} else {
						readyState = 1;
					}
					break;
				case 3:
					readyState = 3;
					break;
				default:
					readyState = 0;
					break;
				}
				lockState = readyState;
				if (readyState == 3 &&
				    craft->laserState.fireCooldownTicks
						    [laserBank] != 0) {
					readyState = 5;
					lockState = 2;
				}
			} else {
				readyState = 1;
			}
		} else {
			readyState = 0;
		}

		if (chargedSegmentState == 2) {
			++readyState;
		}

		objectIndex = g_players[g_localPlayer].objectIndex;
		isRebelFighter = objectIndex != -1 &&
				 (g_objectTable[objectIndex].objectType == 1 ||
				  g_objectTable[objectIndex].objectType == 2 ||
				  g_objectTable[objectIndex].objectType == 3 ||
				  g_objectTable[objectIndex].objectType == 14 ||
				  g_objectTable[objectIndex].objectType == 4);
		if (isRebelFighter &&
		    g_players[g_localPlayer].viewState.hudStateLive ==
			    HUD_VIEW_FORWARD &&
		    (craft->damageStats.activeHudFeatureMask & 2) != 0 &&
		    (craft->damageStats.activeHudFeatureMask & 4) != 0 &&
		    (uint16_t)g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						  laserSlot +
						  LASER_SELECTION_ELEMENT_BASE]
					    .x +
				    (uint16_t)g_hudElementLayouts
					    [g_hudInstrumentSetBaseIndex +
					     laserSlot +
					     LASER_SELECTION_ELEMENT_BASE]
						    .y !=
			    0) {
			Hud_DrawCachedSpriteElement(
				g_hudInstrumentSetBaseIndex + laserSlot +
					LASER_SELECTION_ELEMENT_BASE,
				readyState);
		}

		Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex +
						    laserSlot +
						    LASER_READY_ELEMENT_BASE,
					    lockState);
		if ((craft->workingSubsystems &
		     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) != 0 &&
		    lockState == 3) {
			uint16_t targetObjectIndex =
				g_players[g_localPlayer].currentTargetObjectIdx;

			if (targetObjectIndex != UINT16_MAX &&
			    (uint16_t)collide_WouldShotHitTarget(
				    g_players[g_localPlayer].objectIndex,
				    targetObjectIndex, laserSlot) != 0) {
				g_targetLockActive = 1;
				lockState = 2;
			} else {
				lockState = 1;
			}
		} else if ((craft->workingSubsystems &
			    CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
			lockState = 0;
		} else if (lockState == 2) {
			lockState = 1;
		}

		if (lockState == 3) {
			lockState = 0;
		}
#ifdef XVT_MODERN
		XvtCockpitInstruments_RecordLaserLock(laserSlot, lockState);
#endif

		objectIndex = g_players[g_localPlayer].objectIndex;
		isRebelFighter = objectIndex != -1 &&
				 (g_objectTable[objectIndex].objectType == 1 ||
				  g_objectTable[objectIndex].objectType == 2 ||
				  g_objectTable[objectIndex].objectType == 3 ||
				  g_objectTable[objectIndex].objectType == 14 ||
				  g_objectTable[objectIndex].objectType == 4);
		if (isRebelFighter) {
			Hud_DrawCachedSpriteElement(
				g_hudInstrumentSetBaseIndex + laserSlot +
					LASER_LOCK_ELEMENT_BASE,
				lockState);
		}
	}
}

/* Draws the local player's warhead counts when HUD feature 8 is on and the
 * model has warhead launchers: the first and last slots of launcher group 0 in
 * display slots 0 and 1, and for the missile boat those of group 1 in slots 2
 * and 3. */
// FUNCTION: XVT 0x43D010
void Hud_UpdateWarheadCnt(void)
{
	ObjectRecord *object;
	int16_t firstLauncherSlotCount;

	object = &g_objectTable[g_players[g_localPlayer].objectIndex];
	if ((object->mobj->pCraft->damageStats.activeHudFeatureMask & 8) != 0) {
		firstLauncherSlotCount =
			g_modelDefs[GetModelIndexFromType(object->objectType)]
				.warheadLauncherSlotCount[0];
		if ((uint16_t)(firstLauncherSlotCount +
			       g_modelDefs
				       [GetModelIndexFromType(
						g_objectTable
							[g_players[g_localPlayer]
								 .objectIndex]
								.objectType)]
					       .warheadLauncherSlotCount[1]) !=
		    0) {
			Hud_OutputWarheadCount(
				g_modelDefs
					[GetModelIndexFromType(
						 g_objectTable
							 [g_players[g_localPlayer]
								  .objectIndex]
								 .objectType)]
						.warheadLauncherFirstSlot[0],
				0, 0);
			Hud_OutputWarheadCount(
				g_modelDefs
					[GetModelIndexFromType(
						 g_objectTable
							 [g_players[g_localPlayer]
								  .objectIndex]
								 .objectType)]
						.warheadLauncherLastSlot[0],
				1, 0);

			if (GetModelIndexFromType(CRAFT_SPECIES_MISSILE_BOAT) ==
			    GetModelIndexFromType(
				    g_objectTable[g_players[g_localPlayer]
							  .objectIndex]
					    .objectType)) {
				Hud_OutputWarheadCount(
					g_modelDefs
						[GetModelIndexFromType(
							 g_objectTable
								 [g_players[g_localPlayer]
									  .objectIndex]
									 .objectType)]
							.warheadLauncherFirstSlot
								[1],
					2, 1);
				Hud_OutputWarheadCount(
					g_modelDefs
						[GetModelIndexFromType(
							 g_objectTable
								 [g_players[g_localPlayer]
									  .objectIndex]
									 .objectType)]
							.warheadLauncherLastSlot
								[1],
					3, 1);
			}
		}
	}
}

/* Draws the warhead count of weapon slot warheadSlotIdx (0 with no launchers)
 * for display slot displaySlot, and its launcher's selection state: 0 with no
 * warheads or the launcher out, 1 with lasers selected or another bank; with
 * bank warheadBank selected, 2 for both slots when the low bits of its
 * warheadLauncherFlags are 3, else 2 for the slot whose side matches bit 7 and
 * 1 for the other. In the cockpit set the count is drawn at layout 27 plus the
 * slot when it changed, two digits for the missile boat else one, and the state
 * as a sprite at element 19 plus the slot (an X-wing, Y-wing, A-wing, Z-95 or
 * B-wing shows state 2 as 4). In other sets it draws the count every call while
 * layout 27 plus the slot has a selector, in color code 0x52 for state 2 else
 * 0x4A, at (slot * (g_flightFontDigitWidth + 1) + 2, 2) on
 * g_flightOffscreenBuffer, from where Hud_BlitSoftwareMfdPages copies it, or
 * clears that cell for 0. Records the count in g_hudElementStateCache and
 * leaves g_flightTextShadowEnabled at 0 when it draws. */
// FUNCTION: XVT 0x43D2B0
void Hud_OutputWarheadCount(uint16_t warheadSlotIdx, uint16_t displaySlot,
			    uint16_t warheadBank)
{
	CraftData *craft;
	uint16_t warheadCount;
	uint16_t selectionState;
	uint8_t launcherFlags;
	ModelIndex craftModelIndex;
	int objectIndex;
	int isRebelFighter;
	int localPlayer;
	MobileObject **playerMobileObject;

	if (g_hudInstrumentSetBaseIndex != HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth,
					 g_screenHeight,
					 g_screenWidth * g_flightBytesPerPixel);
		localPlayer = g_localPlayer;
		playerMobileObject =
			&g_objectTable[g_players[localPlayer].objectIndex].mobj;
		craft = (*playerMobileObject)->pCraft;
		if (craft->warheadLauncherCount == 0) {
			warheadCount = 0;
		} else {
			warheadCount =
				craft->weaponSlots[warheadSlotIdx].ammoCount;
		}
		g_hudElementStateCache[displaySlot + 27 +
				       g_hudInstrumentSetBaseIndex] =
			(int16_t)warheadCount;

		if (warheadCount != 0) {
			craft = (*playerMobileObject)->pCraft;
			if ((craft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) == 0) {
				selectionState = 0;
			} else if (g_players[localPlayer].selectedWeaponMode ==
				   0) {
				selectionState = 1;
			} else {
				if (warheadBank !=
				    g_players[localPlayer].selectedWeaponBank) {
					selectionState = 1;
				} else {
					launcherFlags =
						(uint8_t)craft->warheadLauncherFlags
							[g_players[localPlayer]
								 .selectedWeaponBank];
					if ((launcherFlags & 0x7F) == 3) {
						selectionState = 2;
					} else {
						selectionState =
							((displaySlot & 1) ==
							 (launcherFlags >> 7)) +
							1;
					}
				}
			}
		} else {
			selectionState = 0;
		}

		if (warheadCount != 0) {
			FlightText_SetFontTier(2);
			FlightText_SetClipRect(0, 0, g_screenWidth,
					       g_screenHeight);
			FlightText_SetCursor(
				displaySlot * (g_flightFontDigitWidth + 1) + 2,
				2);
			FlightText_SetBackgroundColor(
				g_flightTransparentColorIndex);
			if (selectionState == 2) {
				FlightText_SetColor(0x52);
			} else {
				FlightText_SetColor(0x4A);
			}
			g_flightTextShadowEnabled = 0;
			if (g_hudElementLayouts[displaySlot + 27 +
						g_hudInstrumentSetBaseIndex]
				    .selector != 0) {
				FlightText_SetFontTier(0);
				craftModelIndex = GetModelIndexFromType(
					g_objectTable[g_players[g_localPlayer]
							      .objectIndex]
						.objectType);
				if (GetModelIndexFromType(
					    CRAFT_SPECIES_MISSILE_BOAT) ==
				    craftModelIndex) {
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordNumber(
						(XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
								     displaySlot),
						warheadCount, 2, 1);
#endif
					FlightText_DrawDecimalNumber(
						warheadCount, 2, 1);
				} else {
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordNumber(
						(XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
								     displaySlot),
						warheadCount, 1, 1);
#endif
					FlightText_DrawDecimalNumber(
						warheadCount, 1, 1);
				}
			}
		} else {
			FlightText_SetFontTier(2);
			FlightText_SetClipRect(
				displaySlot * (g_flightFontDigitWidth + 1) + 2,
				2,
				g_flightFontDigitWidth +
					displaySlot *
						(g_flightFontDigitWidth + 1) +
					3,
				g_flightFontLineHeight + 3);
#ifdef XVT_MODERN
			XvtCockpitReadouts_ClearLauncher(displaySlot);
#endif
			FlightText_SetBackgroundColor(
				g_flightTransparentColorIndex);
			g_flightFillClipRectFn();
		}

		FlightSw_SetRenderTarget(NULL, 320, 200, 0);
		return;
	}

	craft = g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft;
	if (craft->warheadLauncherCount == 0) {
		warheadCount = 0;
	} else {
		warheadCount = craft->weaponSlots[warheadSlotIdx].ammoCount;
	}
	if (g_hudElementStateCache[displaySlot + 27 +
				   g_hudInstrumentSetBaseIndex] !=
	    (int16_t)warheadCount) {
		g_hudElementStateCache[displaySlot + 27 +
				       g_hudInstrumentSetBaseIndex] =
			(int16_t)warheadCount;
		FlightText_SetFontTier(2);
		FlightText_SetClipRect(0, 0, g_screenWidth, g_screenHeight);
		FlightText_SetCursor(
			g_hudElementLayouts[displaySlot + 27 +
					    g_hudInstrumentSetBaseIndex]
				.x,
			g_hudElementLayouts[displaySlot + 27 +
					    g_hudInstrumentSetBaseIndex]
				.y);
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		FlightText_SetColor(0x4A);
		g_flightTextShadowEnabled = 0;
		craftModelIndex = GetModelIndexFromType(
			g_objectTable[g_players[g_localPlayer].objectIndex]
				.objectType);
		if (GetModelIndexFromType(CRAFT_SPECIES_MISSILE_BOAT) ==
		    craftModelIndex) {
#ifdef XVT_MODERN
			XvtCockpitReadouts_RecordNumber(
				(XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
						     displaySlot),
				warheadCount, 2, 1);
#endif
			FlightText_DrawDecimalNumber(warheadCount, 2, 1);
		} else {
#ifdef XVT_MODERN
			XvtCockpitReadouts_RecordNumber(
				(XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
						     displaySlot),
				warheadCount, 1, 1);
#endif
			FlightText_DrawDecimalNumber(warheadCount, 1, 1);
		}
	}

	if (warheadCount != 0) {
		craft = g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj->pCraft;
		if ((craft->workingSubsystems &
		     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) == 0) {
			selectionState = 0;
		} else if (g_players[g_localPlayer].selectedWeaponMode == 0) {
			selectionState = 1;
		} else {
			if (warheadBank !=
			    g_players[g_localPlayer].selectedWeaponBank) {
				selectionState = 1;
			} else {
				launcherFlags =
					(uint8_t)craft->warheadLauncherFlags
						[g_players[g_localPlayer]
							 .selectedWeaponBank];
				if ((launcherFlags & 0x7F) == 3) {
					selectionState = 2;
				} else {
					selectionState =
						((displaySlot & 1) ==
						 (launcherFlags >> 7)) +
						1;
				}
			}
		}
	} else {
		selectionState = 0;
	}

	objectIndex = g_players[g_localPlayer].objectIndex;
	isRebelFighter = objectIndex != -1 &&
			 (g_objectTable[objectIndex].objectType == 1 ||
			  g_objectTable[objectIndex].objectType == 2 ||
			  g_objectTable[objectIndex].objectType == 3 ||
			  g_objectTable[objectIndex].objectType == 14 ||
			  g_objectTable[objectIndex].objectType == 4);
	if (isRebelFighter && selectionState == 2) {
		selectionState = 4;
	}
	Hud_DrawCachedSpriteElement(displaySlot + 19, selectionState);
}

/* Draws the local player's front and rear shields (shieldEnergy 0 and 1, 0
 * while the shield system is out) against half of Craft_GetObjectMaxShield, and
 * the hull, when HUD feature 0x20 is on. Where the layout's
 * colorIndexOrWidgetParam is not 0xFFFF, each side is two faded sprites
 * (elements 35 and 36 front, 37 and 38 rear) colored from g_hudShieldColors by
 * a level 0 to 9 of the charge up to full and of the charge above it; while
 * shieldHitFlashTimer runs, the side in g_lastShieldDamageSide shows level 10
 * in place of its overcharge level, or of its main level when it has none.
 * Otherwise each side is a percent, above 100 when overcharged, drawn when it
 * changes in the matching text color. The hull sprite, element 39, is 3 while
 * hullHitFlashTimer runs, else 2, 1 or 0 as hullDamage passes each third of
 * hullMax. */
// FUNCTION: XVT 0x43D800
void Hud_DrawShieldStrength2D(void)
{
	CraftData *craft = g_objectTable[g_players[g_localPlayer].objectIndex]
				   .mobj->pCraft;
	int shield;
	int maxShield;

	if ((craft->damageStats.activeHudFeatureMask & 0x20u) == 0) {
		return;
	}

	shield = craft->shieldEnergy[0];
	if (shield < 0) {
		shield = 0;
	}
	if (!(craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_SHIELDS)) {
		shield = 0;
	}
	maxShield =
		Craft_GetObjectMaxShield(g_players[g_localPlayer].objectIndex) /
		2;
	if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 35]
		    .colorIndexOrWidgetParam != 0xFFFFu) {
		uint16_t shieldRatioQ16;
		uint16_t strengthLevel;
		uint16_t shieldPercent;
		int16_t primaryFade;
		int16_t secondaryFade;
		if (maxShield <= shield) {
			strengthLevel = 9;
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)(shield - maxShield),
				(unsigned int)maxShield);
			shieldPercent =
				MATH2_longfraction(9, (uint16_t)shieldRatioQ16);
		} else {
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)shield, (unsigned int)maxShield);
			strengthLevel =
				MATH2_longfraction(9, (uint16_t)shieldRatioQ16);
			shieldPercent = 0;
		}
		if (g_playerFlightTransientTimers[g_localPlayer]
				    .shieldHitFlashTimer != 0 &&
		    g_lastShieldDamageSide == 0) {
			if (shieldPercent == 0) {
				strengthLevel = 10;
			} else {
				shieldPercent = 10;
			}
		}
		primaryFade =
			strengthLevel != 0
				? g_hudElementLayouts
					  [g_hudInstrumentSetBaseIndex + 35]
						  .clipWidth
				: -1;
		secondaryFade =
			shieldPercent != 0
				? g_hudElementLayouts
					  [g_hudInstrumentSetBaseIndex + 36]
						  .clipWidth
				: -1;
		Hud_DrawCachedFadedSpriteElement(
			(uint16_t)(g_hudInstrumentSetBaseIndex + 35),
			(int16_t)g_hudShieldColors[strengthLevel], primaryFade);
		Hud_DrawCachedFadedSpriteElement(
			(uint16_t)(g_hudInstrumentSetBaseIndex + 36),
			(int16_t)g_hudShieldColors[shieldPercent],
			secondaryFade);
	} else {
		uint16_t strengthLevel;
		uint16_t shieldRatioQ16;
		uint16_t shieldPercent;
		if (maxShield <= shield) {
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)(shield - maxShield),
				(unsigned int)maxShield);
			strengthLevel = 9;
			shieldPercent =
				(uint16_t)(shieldRatioQ16 / 0x28Fu + 100);
		} else {
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)shield, (unsigned int)maxShield);
			strengthLevel =
				MATH2_longfraction(9, (uint16_t)shieldRatioQ16);
			shieldPercent = (uint16_t)(shieldRatioQ16 / 0x28Fu);
		}
		if ((uint16_t)
			    g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
						   35] != shieldPercent) {
			FlightText_SetFontTier(2);
			FlightText_SetClipRect(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 35]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 35]
						.y,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 35]
							.x +
					FlightText_MeasureStringWidth("123%"),
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 35]
							.y +
					g_flightFontLineHeight);
			FlightText_SetBackgroundColor(0x40);
			g_flightFillClipRectFn();
			FlightText_SetColor(
				g_hudShieldColors[HUD_SHIELD_TEXT_COLOR_OFFSET +
						  strengthLevel]);
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 35]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 35]
						.y);
			FlightText_FormatScratchInt(shieldPercent);
			FlightText_AppendScratchChar('%');
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_SHIELD_FORE,
						   g_flightTextScratchBuffer,
						   XVT_COCKPIT_ALIGN_RIGHT);
#endif
			FlightText_DrawStringRightAligned(
				g_flightTextScratchBuffer);
			g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
					       35] = (int16_t)shieldPercent;
		}
	}
	craft = g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft;
	shield = craft->shieldEnergy[1];
	if (shield < 0) {
		shield = 0;
	}
	if (!(craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_SHIELDS)) {
		shield = 0;
	}
	maxShield =
		Craft_GetObjectMaxShield(g_players[g_localPlayer].objectIndex) /
		2;
	if (g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 37]
		    .colorIndexOrWidgetParam != 0xFFFFu) {
		uint16_t shieldRatioQ16;
		uint16_t strengthLevel;
		uint16_t secondaryLevel;
		int16_t primaryFade;
		int16_t secondaryFade;
		if (maxShield <= shield) {
			strengthLevel = 9;
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)(shield - maxShield),
				(unsigned int)maxShield);
			secondaryLevel =
				MATH2_longfraction(9, (uint16_t)shieldRatioQ16);
		} else {
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)shield, (unsigned int)maxShield);
			strengthLevel =
				MATH2_longfraction(9, (uint16_t)shieldRatioQ16);
			secondaryLevel = 0;
		}
		if (g_playerFlightTransientTimers[g_localPlayer]
				    .shieldHitFlashTimer != 0 &&
		    g_lastShieldDamageSide == 1) {
			if (secondaryLevel == 0) {
				strengthLevel = 10;
			} else {
				secondaryLevel = 10;
			}
		}
		primaryFade =
			strengthLevel != 0
				? g_hudElementLayouts
					  [g_hudInstrumentSetBaseIndex + 37]
						  .clipWidth
				: -1;
		secondaryFade =
			secondaryLevel != 0
				? g_hudElementLayouts
					  [g_hudInstrumentSetBaseIndex + 38]
						  .clipWidth
				: -1;
		Hud_DrawCachedFadedSpriteElement(
			(uint16_t)(g_hudInstrumentSetBaseIndex + 37),
			(int16_t)g_hudShieldColors[strengthLevel], primaryFade);
		Hud_DrawCachedFadedSpriteElement(
			(uint16_t)(g_hudInstrumentSetBaseIndex + 38),
			(int16_t)g_hudShieldColors[secondaryLevel],
			secondaryFade);
	} else {
		uint16_t secondaryLevel;
		uint16_t strengthLevel;
		uint16_t shieldRatioQ16;
		if (maxShield <= shield) {
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)(shield - maxShield),
				(unsigned int)maxShield);
			strengthLevel = 9;
			secondaryLevel =
				(uint16_t)(shieldRatioQ16 / 0x28Fu + 100);
		} else {
			shieldRatioQ16 = MATH2_longratioQ16(
				(unsigned int)shield, (unsigned int)maxShield);
			strengthLevel =
				MATH2_longfraction(9, (uint16_t)shieldRatioQ16);
			secondaryLevel = (uint16_t)(shieldRatioQ16 / 0x28Fu);
		}
		if ((uint16_t)
			    g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
						   37] != secondaryLevel) {
			FlightText_SetFontTier(2);
			FlightText_SetClipRect(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 37]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 37]
						.y,
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 37]
							.x +
					FlightText_MeasureStringWidth("123%"),
				g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 37]
							.y +
					g_flightFontLineHeight);
			FlightText_SetBackgroundColor(0x40);
			g_flightFillClipRectFn();
			FlightText_SetColor(
				g_hudShieldColors[HUD_SHIELD_TEXT_COLOR_OFFSET +
						  strengthLevel]);
			FlightText_SetCursor(
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 37]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 37]
						.y);
			FlightText_FormatScratchInt(secondaryLevel);
			FlightText_AppendScratchChar('%');
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_SHIELD_AFT,
						   g_flightTextScratchBuffer,
						   XVT_COCKPIT_ALIGN_RIGHT);
#endif
			FlightText_DrawStringRightAligned(
				g_flightTextScratchBuffer);
			g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
					       37] = (int16_t)secondaryLevel;
		}
	}

	{
		uint16_t hullState;
		unsigned int hullThird;
		if (g_playerFlightTransientTimers[g_localPlayer]
			    .hullHitFlashTimer != 0) {
			hullState = 3;
		} else {
			craft = g_objectTable[g_players[g_localPlayer]
						      .objectIndex]
					.mobj->pCraft;
			hullThird = craft->hullMax / 3;
			if (!hullThird) {
				hullState = 2;
			} else {
				hullState = craft->hullDamage / hullThird;
				if ((uint16_t)hullState > 2) {
					hullState = 2;
				}
				hullState = 2 - hullState;
			}
		}
		Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 39,
					    hullState);
	}
}

#if 0
static void Hud_DrawShieldStrength2D_legacy(void) {
	unsigned int objectIndex = g_players[g_localPlayer].objectIndex;
	CraftData* craft = g_objectTable[objectIndex].mobj->pCraft;
	unsigned int maxShield = Craft_GetObjectMaxShield(objectIndex) / 2;
	int shieldValues[2] = { craft->shieldEnergy[0], craft->shieldEnergy[1] };
	unsigned int side;

	if ((craft->damageStats.activeHudFeatureMask & 0x20u) == 0)
		return;

	for (side = 0; side < 2; ++side) {
		unsigned int elementIndex = g_hudInstrumentSetBaseIndex + 35 + side * 2;
		int shield = shieldValues[side];
		unsigned int percentage;
		unsigned int strengthLevel;
		unsigned int secondaryLevel;
		int16_t primaryFade;
		int16_t secondaryFade;
		if (shield < 0)
			shield = 0;
		if ((craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_SHIELDS) == 0)
			shield = 0;

		if (g_hudElementLayouts[elementIndex].colorIndexOrWidgetParam == 0xFFFFu) {
			if ((unsigned int)shield < maxShield) {
				percentage = MATH2_longratioQ16((unsigned int)shield, maxShield);
				strengthLevel = MATH2_longfraction(9, (uint16_t)percentage);
				secondaryLevel = percentage / 0x28Fu;
			} else {
				percentage = MATH2_longratioQ16((unsigned int)shield - maxShield, maxShield);
				strengthLevel = 9;
				secondaryLevel = percentage / 0x28Fu + 100;
			}
			if ((uint16_t)g_hudElementStateCache[elementIndex] != secondaryLevel) {
				RECT clipRect;
				FlightText_SetFontTier(2);
				clipRect.left = g_hudElementLayouts[elementIndex].x;
				clipRect.top = g_hudElementLayouts[elementIndex].y;
				clipRect.right = clipRect.left + FlightText_MeasureStringWidth("123%");
				clipRect.bottom = clipRect.top + g_flightFontLineHeight;
				FlightText_SetClipRect(clipRect.left, clipRect.top, clipRect.right, clipRect.bottom);
				FlightText_SetBackgroundColor(0x40);
				g_flightFillClipRectFn();
				FlightText_SetColor(g_hudShieldColors[HUD_SHIELD_TEXT_COLOR_OFFSET + strengthLevel]);
				FlightText_SetCursor(g_hudElementLayouts[elementIndex].x,
									 g_hudElementLayouts[elementIndex].y);
				FlightText_FormatScratchInt(secondaryLevel);
				FlightText_AppendScratchChar('%');
				FlightText_DrawStringRightAligned(g_flightTextScratchBuffer);
				g_hudElementStateCache[elementIndex] = (int16_t)secondaryLevel;
			}
		} else {
			if ((unsigned int)shield < maxShield) {
				percentage = MATH2_longratioQ16((unsigned int)shield, maxShield);
				strengthLevel = MATH2_longfraction(9, (uint16_t)percentage);
				secondaryLevel = 0;
			} else {
				strengthLevel = 9;
				percentage = MATH2_longratioQ16((unsigned int)shield - maxShield, maxShield);
				secondaryLevel = MATH2_longfraction(9, (uint16_t)percentage);
			}
			if (g_playerFlightTransientTimers[g_localPlayer].shieldHitFlashTimer != 0 &&
				g_lastShieldDamageSide == (int)side) {
				if (secondaryLevel != 0)
					secondaryLevel = 10;
				else
					strengthLevel = 10;
			}
			primaryFade = strengthLevel != 0 ? g_hudElementLayouts[elementIndex].clipWidth : -1;
			secondaryFade = secondaryLevel != 0 ? g_hudElementLayouts[elementIndex + 1].clipWidth : -1;
			Hud_DrawCachedFadedSpriteElement(
				(uint16_t)elementIndex, g_hudShieldColors[strengthLevel], primaryFade);
			Hud_DrawCachedFadedSpriteElement((uint16_t)(elementIndex + 1),
											 g_hudShieldColors[secondaryLevel],
											 secondaryFade);
		}
	}

	{
		unsigned int hullState;
		if (g_playerFlightTransientTimers[g_localPlayer].hullHitFlashTimer != 0) {
			hullState = 3;
		} else {
			unsigned int hullThird = craft->hullMax / 3;
			if (hullThird != 0) {
				hullState = craft->hullDamage / hullThird;
				if (hullState > 2)
					hullState = 2;
				hullState = 2 - hullState;
			} else {
				hullState = 2;
			}
		}
		Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 39, hullState);
	}
}
#endif

/* Draws the local player's beam when HUD feature 0x10 is on: the beam-active
 * sprite (element 116 of the current set), then, when beamCharge (0 while the
 * beam system is out) differs from g_hudElementStateCache[51], the nine
 * segments of layout 51 of the current set, placed by resolution. Each segment
 * holds 1,000 of charge: full ones in the fourth color of
 * g_hudBeamSegmentColorByChargeStep, the partly charged one by thirds of 1,000,
 * empty ones in the first color; a segment in the first color is drawn without
 * fading. */
// FUNCTION: XVT 0x43DE10
void Hud_DrawBeamStrength2D(void)
{
	uint16_t layoutIndex;
	CraftData *craft;
	int16_t beamStrength;
	int16_t beamSystemAvailable;
	uint16_t beamActiveState;
	int originalBeamStrength;
	int16_t segmentIndex;
	uint16_t segmentColor;
	int16_t clampedStrength;
	uint16_t x;
	uint16_t y;

	layoutIndex = g_hudInstrumentSetBaseIndex + 51;
	craft = g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft;
	if ((craft->damageStats.activeHudFeatureMask & 0x10) == 0) {
		return;
	}

	beamStrength = craft->beamCharge;
	if (beamStrength < 0) {
		beamStrength = 0;
	}
	beamSystemAvailable =
		craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	if (beamSystemAvailable == 0) {
		beamStrength = 0;
	}
	beamActiveState = craft->beamActive != 0;
	if (beamSystemAvailable == 0) {
		beamActiveState = 0;
	}
	Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 116,
				    beamActiveState);

	originalBeamStrength = beamStrength;
	if ((uint16_t)g_hudElementStateCache[51] == beamStrength) {
		return;
	}
	g_hudElementStateCache[51] = beamStrength;

	segmentIndex = 0;
	do {
		if (200 * (5 * segmentIndex + 5) < originalBeamStrength) {
			segmentColor = g_hudBeamSegmentColorByChargeStep[3];
		} else {
			clampedStrength = beamStrength;
			if (beamStrength < 0) {
				segmentColor =
					g_hudBeamSegmentColorByChargeStep[0];
			} else {
				if (beamStrength > 1000) {
					clampedStrength = 1000;
				}
				segmentColor = g_hudBeamSegmentColorByChargeStep
					[clampedStrength / 333];
			}
		}

		x = g_hudElementLayouts[layoutIndex].x;
		y = g_hudElementLayouts[layoutIndex].y;
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480) {
			x += 3 * (8 - segmentIndex);
			y += 3 * (8 - segmentIndex);
		} else if (g_flightResolutionMode ==
			   FLIGHT_RESOLUTION_480X360) {
			x += g_hudBeamSegmentOffsets480x360[segmentIndex].x;
			y += g_hudBeamSegmentOffsets480x360[segmentIndex].y;
		} else {
			x += g_hudBeamSegmentOffsets320x240[segmentIndex].x;
			y += g_hudBeamSegmentOffsets320x240[segmentIndex].y;
		}

		beamStrength -= 1000;
		g_flightBlitSpriteFadedFn(
			g_hudPanelSpriteDataByIndex
				[g_hudElementLayouts
					 [g_hudInstrumentSetBaseIndex + 51]
						 .selector +
				 segmentIndex],
			x, y,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 51]
				.colorIndexOrWidgetParam,
			(int8_t)segmentColor,
			g_hudBeamSegmentColorByChargeStep[0] == segmentColor
				? 0
				: g_hudElementLayouts
					  [g_hudInstrumentSetBaseIndex + 51]
						  .clipWidth);
		++segmentIndex;
	} while (segmentIndex < 9);
}

/* Draws the local player's speed in element 40 of the current set when HUD
 * feature 0x40 is on: speed * 0x71C7 / 65,536, rounded. */
// FUNCTION: XVT 0x43E050
void Hud_UpdateSpeedPercent(void)
{
	int16_t speedPercent;

	if ((g_objectTable[g_players[g_localPlayer].objectIndex]
		     .mobj->pCraft->damageStats.activeHudFeatureMask &
	     0x40) != 0) {
		FlightText_SetBackgroundColor(0x40);
		speedPercent = MATH2_fraction(
			g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj->speed,
			0x71C7);
		if (g_hudInstrumentSetBaseIndex !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			FlightText_SetFontTier(0);
		} else {
			FlightText_SetFontTier(2);
		}
		Hud_DrawCachedNumericElement(g_hudInstrumentSetBaseIndex + 40,
					     speedPercent, 1);
	}
}

/* Draws the local player's throttle in element 41 of the current set when HUD
 * feature 0x40 is on: throttleSpeed / 655, doubled while engineOverdriveOff is
 * 0. */
// FUNCTION: XVT 0x43E110
void Hud_UpdateThrottlePercent(void)
{
	MobileObject *mobileObject;
	CraftData *craft;
	int16_t throttlePercent;

	if ((g_objectTable[g_players[g_localPlayer].objectIndex]
		     .mobj->pCraft->damageStats.activeHudFeatureMask &
	     0x40) != 0) {
		FlightText_SetBackgroundColor(0x40);
		mobileObject =
			g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj;
		craft = mobileObject->pCraft;
		throttlePercent = craft->throttleSpeed / 0x28F;
		if (craft->engineOverdriveOff == 0) {
			throttlePercent *= 2;
		}
		if (g_hudInstrumentSetBaseIndex !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			FlightText_SetFontTier(0);
		} else {
			FlightText_SetFontTier(2);
		}
		Hud_DrawCachedNumericElement(g_hudInstrumentSetBaseIndex + 41,
					     throttlePercent, 1);
	}
}

/* Draws the mission clock at layout 46 when its whole seconds differ from
 * g_hudElementStateCache[46]: the countdown clock in the proving grounds or
 * with a time limit, minutes zero-padded, else the elapsed clock, minutes
 * space-padded; seconds always two digits. Leaves g_flightTextShadowEnabled at
 * 0 when it draws. */
// FUNCTION: XVT 0x43E1F0
void Hud_UpdateMissionClockDisplay(void)
{
	uint8_t seconds;
	int16_t minuteSeconds;
	int16_t totalSeconds;

	if (g_flightMissionState.provingGroundsModeActive != 0 ||
	    g_flightMissionState.missionTimeLimitMinutes != 0) {
		minuteSeconds = 60 * g_missionCountdownClock.minutes;
		seconds = g_missionCountdownClock.seconds;
	} else {
		minuteSeconds = 60 * g_missionElapsedClock.minutes;
		seconds = g_missionElapsedClock.seconds;
	}
	totalSeconds = minuteSeconds + seconds;
	if (g_hudElementStateCache[46] != totalSeconds) {
		g_hudElementStateCache[46] = totalSeconds;
		FlightText_SetFontTier(2);
		FlightText_SetClipRect(0, 0, g_screenWidth, g_screenHeight);
		FlightText_SetBackgroundColor(0x40);
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
			FlightText_SetColor(0x4D);
		} else {
			FlightText_SetColor(0x4E);
		}
		g_flightTextShadowEnabled = 0;
		FlightText_SetCursor(g_hudElementLayouts[46].x,
				     g_hudElementLayouts[46].y);

		if (g_flightMissionState.provingGroundsModeActive != 0 ||
		    g_flightMissionState.missionTimeLimitMinutes != 0) {
#ifdef XVT_MODERN
			XvtCockpitReadouts_RecordNumber(
				XVT_COCKPIT_NUMBER_CLOCK_MINUTES,
				g_missionCountdownClock.minutes, 2, 2);
#endif
			FlightText_DrawDecimalNumber(
				g_missionCountdownClock.minutes, 2, 2);
		} else {
#ifdef XVT_MODERN
			XvtCockpitReadouts_RecordNumber(
				XVT_COCKPIT_NUMBER_CLOCK_MINUTES,
				g_missionElapsedClock.minutes, 2, 1);
#endif
			FlightText_DrawDecimalNumber(
				g_missionElapsedClock.minutes, 2, 1);
		}

		if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
			FlightText_SetCursor(
				g_hudElementLayouts[46].x +
					FlightText_MeasureStringWidth(
						g_missionClockMinutesWidthText),
				g_hudElementLayouts[46].y);
		} else {
			FlightText_SetCursor(
				g_hudElementLayouts[46].x +
					FlightText_MeasureStringWidth(
						g_missionClockMinutesWidthText) +
					1,
				g_hudElementLayouts[46].y);
		}

		if (g_flightMissionState.provingGroundsModeActive != 0 ||
		    g_flightMissionState.missionTimeLimitMinutes != 0) {
#ifdef XVT_MODERN
			XvtCockpitReadouts_RecordNumber(
				XVT_COCKPIT_NUMBER_CLOCK_SECONDS,
				g_missionCountdownClock.seconds, 2, 2);
#endif
			FlightText_DrawDecimalNumber(
				g_missionCountdownClock.seconds, 2, 2);
		} else {
#ifdef XVT_MODERN
			XvtCockpitReadouts_RecordNumber(
				XVT_COCKPIT_NUMBER_CLOCK_SECONDS,
				g_missionElapsedClock.seconds, 2, 2);
#endif
			FlightText_DrawDecimalNumber(
				g_missionElapsedClock.seconds, 2, 2);
		}
	}
}

/* Draws the local player's power bars. An X-wing, Y-wing, A-wing, Z-95 or
 * B-wing shows engine, shield and laser levels (engine 8 minus the other two)
 * as 4-segment bars, doubled outside the cockpit set, the engine bar with twice
 * as many segments; other craft show laser, shield and beam levels times 3 and
 * the engine level (8 minus the laser level, minus each of the shield and beam
 * levels less 2 when fitted) as 12-segment bars. Each bar needs its HUD
 * feature: laser 0x200, engine 0x400, shields 0x800, beam 0x1000. */
// FUNCTION: XVT 0x43E390
void Hud_DrawPowerSettings2D(void)
{
	int objectIndex;
	ObjectRecord *object;
	CraftData *craft;
	int usesCompactPowerDisplay;
	int16_t yStep;
	uint16_t activeHudFeatureMask;
	uint16_t segmentCount;
	uint16_t laserPower;
	uint16_t shieldPower;
	uint16_t enginePower;
	uint16_t systemFlags;

	objectIndex = g_players[g_localPlayer].objectIndex;
	object = &g_objectTable[objectIndex];
	craft = object->mobj->pCraft;
	usesCompactPowerDisplay = 0;
	if (objectIndex != -1) {
		if (object->objectType == 1 || object->objectType == 2 ||
		    object->objectType == 3 || object->objectType == 14 ||
		    object->objectType == 4) {
			usesCompactPowerDisplay = 1;
		}
	}

	if (!usesCompactPowerDisplay) {
		yStep = 2;
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240) {
			yStep = g_flightResolutionMode ==
						FLIGHT_RESOLUTION_480X360
					? 4
					: 6;
		}
		if ((craft->damageStats.activeHudFeatureMask & 0x200) != 0) {
			Hud_DrawCachedSegmentedBar(
				3 * (uint8_t)craft->laserRechargeLevel,
				g_hudInstrumentSetBaseIndex + 43, 12, yStep);
		}

		craft = g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj->pCraft;
		if ((craft->damageStats.activeHudFeatureMask & 0x800) != 0 &&
		    (craft->systemFlags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
			Hud_DrawCachedSegmentedBar(
				3 * (uint8_t)craft->shieldRechargeLevel,
				g_hudInstrumentSetBaseIndex + 44, 12, yStep);
		}

		craft = g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj->pCraft;
		if ((craft->damageStats.activeHudFeatureMask & 0x1000) != 0 &&
		    (craft->systemFlags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) !=
			    0) {
			Hud_DrawCachedSegmentedBar(
				3 * (uint8_t)craft->beamRechargeLevel,
				g_hudInstrumentSetBaseIndex + 45, 12, yStep);
		}

		craft = g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj->pCraft;
		if ((craft->damageStats.activeHudFeatureMask & 0x400) != 0) {
			enginePower =
				(uint16_t)(8 -
					   (uint8_t)craft->laserRechargeLevel);
			systemFlags = craft->systemFlags;
			if ((systemFlags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				enginePower =
					(uint16_t)(enginePower -
						   (uint8_t)craft
							   ->shieldRechargeLevel +
						   2);
			}
			if ((systemFlags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) !=
			    0) {
				enginePower =
					(uint16_t)(enginePower -
						   (uint8_t)craft
							   ->beamRechargeLevel +
						   2);
			}
			Hud_DrawCachedSegmentedBar(
				enginePower, g_hudInstrumentSetBaseIndex + 42,
				12, yStep);
		}
	} else {
		activeHudFeatureMask = craft->damageStats.activeHudFeatureMask;
		if ((activeHudFeatureMask & 0xE00) == 0) {
			return;
		}

		segmentCount = 0;
		yStep = 0;
		switch (g_flightResolutionMode) {
		case FLIGHT_RESOLUTION_320X240:
			yStep = 2;
			segmentCount = 4;
			break;
		case FLIGHT_RESOLUTION_640X480:
			yStep = 5;
			segmentCount = 4;
			break;
		case FLIGHT_RESOLUTION_480X360:
			yStep = 3;
			segmentCount = 4;
			break;
		default:
			break;
		}

		laserPower = (uint8_t)craft->laserRechargeLevel;
		shieldPower = (uint8_t)craft->shieldRechargeLevel;
		enginePower = (uint16_t)(8 - shieldPower - laserPower);
		if (g_hudInstrumentSetBaseIndex !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			segmentCount *= 2;
			laserPower *= 2;
			shieldPower *= 2;
			enginePower *= 2;
		}
		if ((activeHudFeatureMask & 0x400) != 0) {
			Hud_DrawCachedSegmentedBar(
				enginePower, g_hudInstrumentSetBaseIndex + 42,
				2 * segmentCount, yStep);
		}
		if ((activeHudFeatureMask & 0x800) != 0) {
			Hud_DrawCachedSegmentedBar(
				shieldPower, g_hudInstrumentSetBaseIndex + 44,
				segmentCount, 2 * yStep);
		}
		if ((activeHudFeatureMask & 0x200) != 0) {
			Hud_DrawCachedSegmentedBar(
				laserPower, g_hudInstrumentSetBaseIndex + 43,
				segmentCount, 2 * yStep);
		}
	}
}

/* Draws a bar of segmentCount sprites from elementIdx's layout upward, yStep
 * pixels apart, the first filledCount from sprite selector + 1 and the rest
 * from sprite selector, when filledCount differs from the element's entry in
 * g_hudElementStateCache, which it then records. */
// FUNCTION: XVT 0x43E6F0
void Hud_DrawCachedSegmentedBar(uint16_t filledCount, uint16_t elementIdx,
				uint16_t segmentCount, int16_t yStep)
{
	uint16_t segmentIndex;
	uint16_t x;
	uint16_t y;
	uint16_t selector;
	uint16_t spriteOffset;

	if ((uint16_t)g_hudElementStateCache[elementIdx] == filledCount) {
		return;
	}

	segmentIndex = 0;
	g_hudElementStateCache[elementIdx] = (int16_t)filledCount;
	x = g_hudElementLayouts[elementIdx].x;
	y = g_hudElementLayouts[elementIdx].y;
	selector = g_hudElementLayouts[elementIdx].selector;
	if (segmentCount == segmentIndex) {
		return;
	}

	do {
		spriteOffset = segmentIndex < filledCount;
		g_flightBlitSpriteFn(
			g_hudPanelSpriteDataByIndex[selector + spriteOffset], x,
			y, 253, 0);
		y = (uint16_t)(y - yStep);
		++segmentIndex;
	} while (segmentIndex < segmentCount);
}

/* Draws the threat indicators of the current set from the active craft around
 * the local player: attack (element 90) when an AI craft attacks it with linked
 * laser cannons (projectile types 0x89 or 0x8B), or a player's craft hit it
 * within the last 5 mission seconds or aims lasers at it from rough distance
 * under 0x10000; turret lasers (91) when a craft's working turret targets it;
 * beam (92), the beam type of a charged beam on it. Then the incoming warhead
 * warning (93): 2 when any lock on it exceeds 944 warheadLockTicks, blinking
 * while any lock is building, else 0, and the missile warning sound to match.
 * hudMode is ignored. */
// FUNCTION: XVT 0x43E790
void Hud_UpdateThreatIndicators(int hudMode)
{
	int objectIdx;
	uint16_t beamThreat;
	uint16_t attackThreat;
	uint16_t laserThreat;
	int playerObjectIdx;
	int16_t maxWarheadLock;
	uint16_t warningState;

	(void)hudMode;

	attackThreat = 0;
	laserThreat = 0;
	beamThreat = 0;
	playerObjectIdx = g_players[g_localPlayer].objectIndex;
	for (objectIdx = g_activeRegionObjectSlotStart;
	     objectIdx < g_activeRegionCraftObjectSlotEnd; ++objectIdx) {
		CraftData *craft;

		if (g_objectTable[objectIdx].objectType == 0 ||
		    g_objectTable[objectIdx].mobj->family != 0) {
			continue;
		}
		craft = g_objectTable[objectIdx].mobj->pCraft;
		if (craft->workingSubsystems == 0 ||
		    craft->objectKind != CRAFT_OBJECT_KIND_ACTIVE) {
			continue;
		}
		if (g_objectTable[objectIdx].playerOwnerIdx == -1) {
			AiController *ai = &craft->aiController;

			if (ai->targetObjIdx == playerObjectIdx &&
			    (ai->maneuverMode == AI_MANEUVER_MODE_ATTACK ||
			     ai->maneuverMode ==
				     AI_MANEUVER_MODE_ROCKET_ATTACK)) {
				int cannon;

				for (cannon = 0;
				     cannon < craft->cannonGroupCount;
				     ++cannon) {
					if ((craft->laserState.projectileTypeId
							     [cannon] == 0x8B ||
					     craft->laserState.projectileTypeId
							     [cannon] ==
						     0x89) &&
					    craft->laserState
							    .linkMode[cannon] !=
						    0) {
						attackThreat = 1;
					}
				}
				if (craft->beamActive != 0 &&
				    craft->beamCharge != 0 &&
				    craft->beamTypeId != BEAM_TYPE_NONE &&
				    (uint8_t)craft->beamTypeId <
					    BEAM_TYPE_DECOY) {
					beamThreat = (uint8_t)craft->beamTypeId;
				}
			}
		} else {
			CraftData *playerCraft =
				g_objectTable[playerObjectIdx].mobj->pCraft;
			int playerOwnerIdx;

			if (playerCraft->lastAttackerObjIdx == objectIdx &&
			    (uint16_t)Mission_ClockToSeconds(
				    g_missionElapsedClock.hours,
				    g_missionElapsedClock.minutes,
				    g_missionElapsedClock.seconds) -
					    playerCraft->lastHitMissionSecond <
				    5) {
				attackThreat = 1;
			}
			playerOwnerIdx =
				g_objectTable[objectIdx].playerOwnerIdx;
			if ((uint16_t)g_players[playerOwnerIdx]
					    .currentTargetObjectIdx ==
				    playerObjectIdx &&
			    g_players[playerOwnerIdx].selectedWeaponMode == 0) {
				pai_ObjectRefUpdateRoughDistance(
					objectIdx, playerObjectIdx);
				if (g_lastRoughDistance < 0x10000 &&
				    Targeting_TestAimCone(playerObjectIdx, 0,
							  playerOwnerIdx)) {
					attackThreat = 1;
				}
			}
			if ((uint16_t)g_players[playerOwnerIdx]
					    .currentTargetObjectIdx ==
				    playerObjectIdx &&
			    craft->beamActive != 0 && craft->beamCharge != 0 &&
			    craft->beamTypeId != BEAM_TYPE_NONE &&
			    (uint8_t)craft->beamTypeId < BEAM_TYPE_DECOY) {
				beamThreat = (uint8_t)craft->beamTypeId;
			}
		}
		if (g_missionFlightGroups[g_objectTable[objectIdx]
						  .flightGroupIdx]
			    .fg.status1 != 5) {
			int slot;

			for (slot = 0; slot < craft->laserSlotCount; ++slot) {
				if (craft->weaponSlots[slot].projectileTypeId ==
					    2 &&
				    craft->turretTargetStates[slot]
						    .targetObjIdx ==
					    playerObjectIdx &&
				    craft->componentHp
						    [g_modelDefs[craft->modelIndex]
							     .weaponHardpoints
								     [slot]
							     .meshIdx] != 0) {
					laserThreat = 1;
				}
			}
			if (craft->beamActive != 0 && craft->beamCharge != 0 &&
			    (uint16_t)craft->beamTargetObjIdx ==
				    playerObjectIdx &&
			    craft->beamTypeId != BEAM_TYPE_NONE &&
			    (uint8_t)craft->beamTypeId < BEAM_TYPE_DECOY) {
				beamThreat = (uint8_t)craft->beamTypeId;
			}
		}
	}
	Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 90,
				    attackThreat);
	Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 91,
				    laserThreat);
	Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 92,
				    beamThreat);
	maxWarheadLock = 0;
	for (objectIdx = g_activeRegionObjectSlotStart;
	     objectIdx < g_activeRegionCraftObjectSlotEnd; ++objectIdx) {
		CraftData *craft;
		int playerOwnerIdx;

		if (g_objectTable[objectIdx].objectType == 0 ||
		    g_objectTable[objectIdx].mobj->family != 0) {
			continue;
		}
		craft = g_objectTable[objectIdx].mobj->pCraft;
		if (craft->workingSubsystems == 0 ||
		    craft->objectKind != CRAFT_OBJECT_KIND_ACTIVE) {
			continue;
		}
		playerOwnerIdx = g_objectTable[objectIdx].playerOwnerIdx;
		if (playerOwnerIdx == -1) {
			AiController *ai = &craft->aiController;

			if (ai->targetObjIdx == playerObjectIdx &&
			    ai->maneuverMode ==
				    AI_MANEUVER_MODE_ROCKET_ATTACK) {
				int16_t warheadLockTicks =
					craft->warheadLockTicks;

				if (maxWarheadLock < warheadLockTicks) {
					maxWarheadLock = warheadLockTicks;
				}
			}
		} else if ((uint16_t)g_players[playerOwnerIdx]
					   .currentTargetObjectIdx ==
				   playerObjectIdx &&
			   g_players[playerOwnerIdx].selectedWeaponMode != 0) {
			int16_t warheadLockTicks = craft->warheadLockTicks;

			if (maxWarheadLock < warheadLockTicks) {
				maxWarheadLock = warheadLockTicks;
			}
		}
	}
	if (maxWarheadLock > 944) {
		warningState = 2;
	} else if (maxWarheadLock > 0) {
		warningState =
			(uint16_t)((g_missionElapsedClock.subsecondTicks / 59) &
				   1);
	} else {
		warningState = 0;
	}
	Hud_DrawCachedSpriteElement(g_hudInstrumentSetBaseIndex + 93,
				    warningState);
#ifdef XVT_MODERN
	XvtCockpitInstruments_RecordThreats(attackThreat, laserThreat,
					    beamThreat, warningState);
#endif
	FlightSurface_Unlock();
	fsfx_UpdateIncomingMissileWarning(warningState);
	FlightSurface_Lock();
}

/* Runs the critical warning of an X-wing, Y-wing, A-wing, Z-95 or B-wing whose
 * layout 50 is placed. With shields under 100 in total and hullDamage in the
 * last third of hullMax, the warning blinks; on each lit call it plays
 * FLIGHT_SOUND_CRITICAL_WARNING and steps layout 127's
 * clipHeightOrForegroundColor, used here as a counter that wraps back to 1 at
 * 0x2F0. In the forward view it draws sprite 50 and, unless the counter is 0,
 * EJECT centered in layout 127 in the layout's colors; once the counter passes
 * 0x200 the box is cleared first and the text moves on, every 16 steps, through
 * the 14 strings that follow EJECT in g_strCockpitOverlayText. */
// FUNCTION: XVT 0x43EC40
void Hud_UpdateCriticalHullShieldWarning(void)
{
	int objectIdx;
	int supportedCraft;
	CraftData *craft;
	unsigned int shieldEnergy;
	unsigned int hullThird;
	unsigned int hullDamageLevel;
	uint16_t warningState;
	unsigned int warningTextIdx;

	objectIdx = g_players[g_localPlayer].objectIndex;
	supportedCraft =
		objectIdx != -1 && (g_objectTable[objectIdx].objectType == 1 ||
				    g_objectTable[objectIdx].objectType == 2 ||
				    g_objectTable[objectIdx].objectType == 3 ||
				    g_objectTable[objectIdx].objectType == 14 ||
				    g_objectTable[objectIdx].objectType == 4);
	if (supportedCraft == 0 ||
	    (uint16_t)g_hudElementLayouts[50].y +
			    (uint16_t)g_hudElementLayouts[50].x ==
		    0) {
		return;
	}

	craft = g_objectTable[objectIdx].mobj->pCraft;
	shieldEnergy = craft->shieldEnergy[0] + craft->shieldEnergy[1];
	hullThird = craft->hullMax / 3;
	hullDamageLevel = 2;
	if (hullThird != 0) {
		hullDamageLevel = craft->hullDamage / hullThird;
	}
	if (shieldEnergy < 100 && hullDamageLevel == 2) {
		warningState = (g_missionElapsedClock.subsecondTicks / 59) & 1;
		if (warningState != 0) {
			FlightSurface_Unlock();
			fsfx_PlaySound(FLIGHT_SOUND_CRITICAL_WARNING, -1,
				       g_localPlayer);
			FlightSurface_Lock();
			++g_hudElementLayouts[127].clipHeightOrForegroundColor;
			if ((uint16_t)g_hudElementLayouts[127]
				    .clipHeightOrForegroundColor >= 0x2F0) {
				g_hudElementLayouts[127]
					.clipHeightOrForegroundColor = 1;
			}
		}
	} else {
		warningState = 0;
	}

	if (g_players[g_localPlayer].viewState.hudStateLive !=
	    HUD_VIEW_FORWARD) {
		return;
	}
	Hud_DrawCachedSpriteElement(50, warningState);
	if (g_hudElementLayouts[127].clipHeightOrForegroundColor == 0) {
		return;
	}

	FlightText_SetFontTier(0);
	FlightText_SetClipRect(
		g_hudElementLayouts[127].x, g_hudElementLayouts[127].y,
		g_hudElementLayouts[127].x + g_hudElementLayouts[127].clipWidth,
		g_hudElementLayouts[127].y + g_flightFontLineHeight);
	FlightText_SetCursor(g_hudElementLayouts[127].x,
			     g_hudElementLayouts[127].y);
	FlightText_SetColor(warningState +
			    g_hudElementLayouts[127].colorIndexOrWidgetParam);
	FlightText_SetBackgroundColor(
		(uint16_t)g_hudElementLayouts[127].selector +
		(warningState == 0 ? 0 : 2));
	warningTextIdx =
		(uint16_t)g_hudElementLayouts[127].clipHeightOrForegroundColor;
	if (warningTextIdx > 0x200) {
		warningTextIdx -= 0x200;
		g_flightFillClipRectFn();
		warningTextIdx >>= 4;
	} else {
		warningTextIdx = 0;
	}
#ifdef XVT_MODERN
	XvtCockpitText_RecordField(
		XVT_COCKPIT_TEXT_CRITICAL_WARNING,
		g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_EJECT +
					warningTextIdx],
		XVT_COCKPIT_ALIGN_CENTER);
#endif
	FlightText_DrawStringCentered(
		g_strCockpitOverlayText[COCKPIT_OVERLAY_STR_EJECT +
					warningTextIdx]);
}

/* In the cockpit set, draws the local player's countermeasure count, three
 * digits at layout 48 when it changed, and the chaff sprite (element 47), 1
 * while chaffActiveSeconds is not 0. */
// FUNCTION: XVT 0x43EE80
void Hud_UpdateCountermeasureStatus(void)
{
	uint16_t countermeasureCount;
	uint16_t left;
	uint16_t y;

	countermeasureCount =
		g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft->cmAmmoCount;
	if (g_hudInstrumentSetBaseIndex == HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		if ((uint16_t)
			    g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
						   48] != countermeasureCount) {
			left = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						   48]
				       .x;
			y = g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
						48]
				    .y;
			if (left + y == 0) {
				return;
			}

			FlightText_SetFontTier(2);
			FlightText_SetClipRect(
				left, y,
				left + FlightText_MeasureStringWidth(
					       g_threeDigitWidthText),
				y + g_flightFontLineHeight);
			FlightText_SetCursor(left, y);
			FlightText_SetBackgroundColor(0x2C);
			g_flightFillClipRectFn();
			FlightText_SetColor(0x4E);
#ifdef XVT_MODERN
			XvtCockpitReadouts_RecordNumber(
				XVT_COCKPIT_NUMBER_COUNTERMEASURES,
				countermeasureCount, 3, 1);
#endif
			FlightText_DrawDecimalNumber(countermeasureCount, 3, 1);
			g_hudElementStateCache[g_hudInstrumentSetBaseIndex +
					       48] = countermeasureCount;
		}

		if ((uint8_t)g_objectTable[g_players[g_localPlayer].objectIndex]
			    .mobj->pCraft->chaffActiveSeconds != 0) {
			Hud_DrawCachedSpriteElement(47, 1);
		} else {
			Hud_DrawCachedSpriteElement(47, 0);
		}
	}
}

/* Outside the map view, in the forward and HUD-only views of a craft other than
 * an X-wing, Y-wing, A-wing, Z-95, B-wing or TIE fighter, draws elements 109
 * and 110 of the current set in state 0 when the craft has no beam system and
 * element 108 when it has no shields. */
// FUNCTION: XVT 0x43F010
void Hud_ClearUnavailableCraftSystemIndicators(void)
{
	ObjectRecord *object;
	int objectIndex;
	uint8_t objectType;
	int excludedCraft;
	uint8_t hudState;
	unsigned int instrumentBaseIndex;

	if (g_players[g_localPlayer].mapCameraState == 0) {
		objectIndex = g_players[g_localPlayer].objectIndex;
		excludedCraft =
			objectIndex != -1 &&
			((objectType = g_objectTable[objectIndex].objectType) ==
				 CRAFT_SPECIES_X_WING ||
			 objectType == CRAFT_SPECIES_Y_WING ||
			 objectType == CRAFT_SPECIES_A_WING ||
			 objectType == CRAFT_SPECIES_Z_95_HEADHUNTER ||
			 objectType == CRAFT_SPECIES_B_WING);

		if (!excludedCraft) {
			object = &g_objectTable[objectIndex];
			if (object->objectType != CRAFT_SPECIES_TIE_FIGHTER) {
				hudState = g_players[g_localPlayer]
						   .viewState.hudStateLive;
				if (hudState == 0 || hudState == 19) {
					instrumentBaseIndex =
						g_hudInstrumentSetBaseIndex;
					if ((object->mobj->pCraft->systemFlags &
					     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) ==
					    0) {
						Hud_DrawCachedSpriteElement(
							instrumentBaseIndex +
								109,
							0);
						Hud_DrawCachedSpriteElement(
							instrumentBaseIndex +
								110,
							0);
					}
					if ((g_objectTable
						     [g_players[g_localPlayer]
							      .objectIndex]
							     .mobj->pCraft
							     ->systemFlags &
					     CRAFT_SUBSYSTEM_FLAG_SHIELDS) ==
					    0) {
						Hud_DrawCachedSpriteElement(
							instrumentBaseIndex +
								108,
							0);
					}
				}
			}
		}
	}
}

/* Draws the HUD feature status sprites for the 13 features the local player's
 * craft has installed, at element 69 plus the feature's bit: state 13 for a
 * feature that is out, else 0. In the forward view the elements are those of
 * the cockpit set, and an X-wing, Y-wing, A-wing, Z-95 or B-wing draws only the
 * features that are out, in state 0. In the HUD-only view features 1 to 3 are
 * skipped, and features 4 and 12 for those craft. Sets
 * g_hudCachedTargetObjectIdx to -1 in both views, so the targeting computer
 * redraws; other views draw nothing. */
// FUNCTION: XVT 0x43F140
void Hud_UpdateCraftSystemStatusIndicators(void)
{
	uint8_t hudState;
	uint16_t featureIndex;
	uint16_t featureMask;
	uint16_t indicatorState;
	int objectIndex;
	uint8_t objectType;
	int excludedCraft;
	CraftData *craft;

	hudState = g_players[g_localPlayer].viewState.hudStateLive;
	if (hudState == 0) {
		featureMask = 1;
		for (featureIndex = 0; featureIndex < 13;
		     featureMask *= 2, ++featureIndex) {
			objectIndex = g_players[g_localPlayer].objectIndex;
			excludedCraft =
				objectIndex != -1 &&
				((objectType = g_objectTable[objectIndex]
						       .objectType) ==
					 CRAFT_SPECIES_X_WING ||
				 objectType == CRAFT_SPECIES_Y_WING ||
				 objectType == CRAFT_SPECIES_A_WING ||
				 objectType == CRAFT_SPECIES_Z_95_HEADHUNTER ||
				 objectType == CRAFT_SPECIES_B_WING);

			if (!excludedCraft) {
				indicatorState =
					(featureMask &
					 g_objectTable[objectIndex]
						 .mobj->pCraft->damageStats
						 .activeHudFeatureMask) == 0
						? 13
						: 0;
			} else if ((featureMask &
				    g_objectTable[objectIndex]
					    .mobj->pCraft->damageStats
					    .activeHudFeatureMask) != 0) {
				continue;
			} else {
				indicatorState = 0;
			}

			if ((featureMask & g_objectTable[objectIndex]
						   .mobj->pCraft->damageStats
						   .installedHudFeatureMask) !=
			    0) {
				Hud_DrawCachedSpriteElement(featureIndex + 69,
							    indicatorState);
			}
		}
		g_hudCachedTargetObjectIdx = -1;
		return;
	}

	if (hudState != 19) {
		return;
	}

	featureMask = 1;
	for (featureIndex = 0; featureIndex < 13;
	     featureMask *= 2, ++featureIndex) {
		switch (featureMask) {
		case 2:
		case 4:
		case 8:
			continue;

		case 16:
		case 4096:
			objectIndex = g_players[g_localPlayer].objectIndex;
			excludedCraft =
				objectIndex != -1 &&
				((objectType = g_objectTable[objectIndex]
						       .objectType) ==
					 CRAFT_SPECIES_X_WING ||
				 objectType == CRAFT_SPECIES_Y_WING ||
				 objectType == CRAFT_SPECIES_A_WING ||
				 objectType == CRAFT_SPECIES_Z_95_HEADHUNTER ||
				 objectType == CRAFT_SPECIES_B_WING);
			if (excludedCraft) {
				continue;
			}
			break;

		default:
			break;
		}

		craft = g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj->pCraft;
		indicatorState = (featureMask &
				  craft->damageStats.activeHudFeatureMask) == 0
					 ? 13
					 : 0;
		if ((featureMask &
		     craft->damageStats.installedHudFeatureMask) != 0) {
			uint16_t baseIndex = g_hudInstrumentSetBaseIndex;
			Hud_DrawCachedSpriteElement(
				baseIndex + featureIndex + 69, indicatorState);
		}
	}
	g_hudCachedTargetObjectIdx = -1;
}

/* Draws the target camera (CMD) view's text about the local player's target:
 * name, range, cargo, and for a craft its orders, the order's target or
 * destination, the range to it and the time to reach it. On a target change it
 * records the target in g_hudCachedTargetObjectIdx, marks the CMD entries of
 * g_hudElementStateCache for redraw, draws the name, and clears the panel
 * (layout 143) for a target outside the craft slots. With a target it draws the
 * range label when there was none before, the range as whole.hundredths (polar
 * distance * 161 / 65,536, at most 99.99), and the cargo, unknown until the
 * team identifies the craft.
 *
 * For a craft target that is not an unidentified hostile in a melee with
 * several players: its plan's report as its orders (with disabled and stopped
 * craft shown as for the targeting computer), the order target (a player's own
 * target for a player's craft, none while disabled or waiting), the range to
 * it, and the time: the order range over 18 times the speed, or for a stopped
 * craft its maneuverTimer in simulated seconds under board2pln and waitpln,
 * else 00:00 at zero range or unknown. Each part is redrawn when it changes;
 * the order range only when its hundredths change. Stores label widths in the
 * clipWidth of layouts 104 to 107, leaves trig2_polardistance multiplied by
 * 161, and leaves g_flightTextShadowEnabled at 0. */
// FUNCTION: XVT 0x43F390
void Hud_DrawCmdTargetDetails(void)
{
	enum {
		CMD_TARGET_NAME_ELEMENT = 94,
		CMD_CARGO_ELEMENT = 95,
		CMD_RANGE_ELEMENT = 96,
		CMD_RANGE_FRACTION_CACHE = 97,
		CMD_ORDERS_ELEMENT = 104,
		CMD_ORDER_TARGET_ELEMENT = 105,
		CMD_ORDER_RANGE_ELEMENT = 106,
		CMD_ORDER_TIME_ELEMENT = 107,
		CMD_RANGE_LABEL_ELEMENT = 141,
		CMD_PANEL_BOUNDS_ELEMENT = 143,
		NORMAL_TARGET_DISPLAY_FLAGS = 3,
		HIDDEN_TARGET_DISPLAY_FLAGS = 1,
		HUD_CACHE_DIRTY = -1,
		HUD_CACHE_INVALID = -2,
		DISTANCE_FIXED_SCALE = 161,
		DISTANCE_DECIMAL_SCALE = 100,
		MAX_DISPLAY_DISTANCE = 9999,
		DISTANCE_PER_SPEED_SECOND = 18,
		SECONDS_PER_MINUTE = 60,
		NO_AI_TARGET = 255,
	};

	uint16_t panelLeft;
	uint16_t panelRight;
	int16_t previousTargetObjectIdx;
	uint16_t currentTargetObjectIdx;

#ifdef XVT_MODERN
	XvtCockpitReadouts_BeginTarget(1);
#endif

	g_flightTextShadowEnabled = 0;
	FlightText_SetBackgroundColor(0x2C);
	FlightText_SetFontTier(2);
	panelLeft = g_hudElementLayouts[CMD_PANEL_BOUNDS_ELEMENT].x;
	panelRight = panelLeft +
		     g_hudElementLayouts[CMD_PANEL_BOUNDS_ELEMENT].clipWidth;
	previousTargetObjectIdx = g_hudCachedTargetObjectIdx;

	if (g_players[g_localPlayer].currentTargetObjectIdx !=
	    g_hudCachedTargetObjectIdx) {
		uint16_t left;
		uint16_t top;
		uint16_t dirtyState;
		uint16_t invalidState;

#ifdef XVT_MODERN
		XvtCockpitText_ClearTargetFields();
#endif

		previousTargetObjectIdx = g_hudCachedTargetObjectIdx;
		g_hudCachedTargetObjectIdx =
			g_players[g_localPlayer].currentTargetObjectIdx;
		dirtyState = UINT16_MAX;
		invalidState = UINT16_MAX - 1;
		g_hudElementStateCache[CMD_CARGO_ELEMENT] = dirtyState;
		g_hudElementStateCache[CMD_RANGE_ELEMENT] = dirtyState;
		g_hudElementStateCache[CMD_RANGE_FRACTION_CACHE] = dirtyState;
		g_hudElementStateCache[CMD_ORDERS_ELEMENT] = dirtyState;
		g_hudElementStateCache[CMD_ORDER_TARGET_ELEMENT] = invalidState;
		g_hudElementStateCache[CMD_ORDER_RANGE_ELEMENT] = invalidState;
		g_hudElementStateCache[CMD_ORDER_TIME_ELEMENT] = dirtyState;

		left = g_hudElementLayouts[CMD_TARGET_NAME_ELEMENT].x;
		top = g_hudElementLayouts[CMD_TARGET_NAME_ELEMENT].y;
		FlightText_SetClipRect(
			left, top,
			left + g_hudElementLayouts[CMD_TARGET_NAME_ELEMENT]
					.clipWidth,
			top + g_flightFontLineHeight + 1);
		g_flightFillClipRectFn();
		FlightText_SetCursor(left, top);
		FlightText_SetClearLineBackground(1);

		currentTargetObjectIdx = (uint16_t)g_players[g_localPlayer]
						 .currentTargetObjectIdx;
		if (currentTargetObjectIdx != dirtyState) {
			ObjectRecord *targetObject;
			MobileObject *targetMobileObject;
			CraftData *targetCraft;

			targetObject = &g_objectTable[currentTargetObjectIdx];
			targetMobileObject = targetObject->mobj;
			targetCraft = targetMobileObject != NULL
					      ? targetMobileObject->pCraft
					      : NULL;
			if (g_missionHeader.missionType == MISSION_TYPE_MELEE &&
			    g_flightPlayerCount > 1) {
				int16_t displayFlags;

				displayFlags = NORMAL_TARGET_DISPLAY_FLAGS;
				if (targetMobileObject != NULL &&
				    targetCraft != NULL &&
				    g_flightMissionState.locatePlayersEnabled ==
					    0) {
					int playerTeam;

					playerTeam =
						(uint16_t)
							g_players[g_localPlayer]
								.team;
					if (targetCraft->identifiedOrderByTeam
						    [playerTeam] == 0) {
						int flightGroupIdx;
						int team;
						int hostile;

						flightGroupIdx =
							targetObject
								->flightGroupIdx;
						team = g_missionFlightGroups
							       [flightGroupIdx]
								       .fg.team;
						hostile =
							team == playerTeam
								? 0
								: g_missionTeams[playerTeam]
										  .allies[team] ==
									  0;
						if (hostile == 1 &&
						    g_missionFlightGroups[flightGroupIdx]
								    .fg
								    .playerNumber !=
							    0) {
							displayFlags =
								HIDDEN_TARGET_DISPLAY_FLAGS;
						}
					}
				}
				Hud_AppendObjectDisplayName(
					currentTargetObjectIdx, displayFlags);
			} else {
				Hud_AppendObjectDisplayName(
					currentTargetObjectIdx,
					NORMAL_TARGET_DISPLAY_FLAGS);
			}
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_TARGET_NAME,
						   g_flightTextScratchBuffer,
						   XVT_COCKPIT_ALIGN_CENTER);
#endif
			FlightText_DrawStringCentered(
				g_flightTextScratchBuffer);
		}

		if ((uint16_t)g_hudCachedTargetObjectIdx >=
		    g_activeRegionCraftObjectSlotEnd) {
			FlightText_SetClipRect(
				panelLeft,
				g_hudElementLayouts[CMD_PANEL_BOUNDS_ELEMENT].y,
				panelRight,
				g_hudElementLayouts[CMD_PANEL_BOUNDS_ELEMENT]
						.y +
					g_hudElementLayouts[CMD_PANEL_BOUNDS_ELEMENT]
						.clipHeightOrForegroundColor);
			g_flightFillClipRectFn();
		}
	}

	if (g_players[g_localPlayer].currentTargetObjectIdx != -1) {
		uint16_t left;
		uint16_t top;
		uint16_t distance;
		uint16_t wholeDistance;
		uint16_t fractionalDistance;
		int16_t cargoState;
		const char *cargoText;

		left = g_hudElementLayouts[CMD_RANGE_ELEMENT].x;
		top = g_hudElementLayouts[CMD_RANGE_ELEMENT].y;
		if (previousTargetObjectIdx == -1) {
			FlightText_SetCursor((uint16_t)g_hudElementLayouts
						     [CMD_RANGE_LABEL_ELEMENT]
							     .x,
					     (uint16_t)g_hudElementLayouts
						     [CMD_RANGE_LABEL_ELEMENT]
							     .y);
			FlightText_SetColor(0x49);
			FlightText_DrawString(
				g_strCmdThreatDisplayText[CMD_THREAT_STR_DIST]);
		}
		Player_ComputePolarToObjectRef(
			g_localPlayer, (uint16_t)g_hudCachedTargetObjectIdx);
		FlightText_SetClipRect(
			left, top,
			left + FlightText_MeasureStringWidth("00.00"),
			top + g_flightFontLineHeight + 1);
		FlightText_SetColor(0x4A);
		trig2_polardistance *= DISTANCE_FIXED_SCALE;
		distance = (uint16_t)(trig2_polardistance >> 16);
		if (distance >= MAX_DISPLAY_DISTANCE + 1) {
			distance = MAX_DISPLAY_DISTANCE;
		}
		wholeDistance = distance / DISTANCE_DECIMAL_SCALE;
		fractionalDistance =
			distance - wholeDistance * DISTANCE_DECIMAL_SCALE;
		if (wholeDistance !=
			    (uint16_t)
				    g_hudElementStateCache[CMD_RANGE_ELEMENT] ||
		    fractionalDistance != (uint16_t)g_hudElementStateCache
						  [CMD_RANGE_FRACTION_CACHE]) {
			g_hudElementStateCache[CMD_RANGE_ELEMENT] =
				(int16_t)wholeDistance;
			g_hudElementStateCache[CMD_RANGE_FRACTION_CACHE] =
				(int16_t)fractionalDistance;
			FlightText_SetCursor(left, top);
			g_flightFillClipRectFn();
			if (fractionalDistance < 10) {
				sprintf(g_flightTextScratchBuffer, "%ld.0%ld",
					(long)wholeDistance,
					(long)fractionalDistance);
			} else {
				sprintf(g_flightTextScratchBuffer, "%ld.%ld",
					(long)wholeDistance,
					(long)fractionalDistance);
			}
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CMD_RANGE,
						   g_flightTextScratchBuffer,
						   XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(g_flightTextScratchBuffer);
		}

		cargoState = 2;
		cargoText = g_strCmdThreatDisplayText[CMD_THREAT_STR_NO_CARGO];
		currentTargetObjectIdx = (uint16_t)g_players[g_localPlayer]
						 .currentTargetObjectIdx;
		if (currentTargetObjectIdx < g_activeRegionCraftObjectSlotEnd) {
			MobileObject *targetMobileObject;

			targetMobileObject =
				g_objectTable[currentTargetObjectIdx].mobj;
			if (targetMobileObject->family == 0) {
				CraftData *targetCraft;

				targetCraft = targetMobileObject->pCraft;
				if (targetCraft->identifiedOrderByTeam
					    [(uint16_t)g_players[g_localPlayer]
						     .team] != 0) {
					cargoState = 1;
					cargoText =
						targetCraft->specialCargoName;
					if (cargoText[0] == '\0') {
						cargoState = 2;
						cargoText = g_strCmdThreatDisplayText
							[CMD_THREAT_STR_NO_CARGO];
					}
				} else {
					cargoState = 0;
					cargoText = g_strUnknown;
				}
			}
		}
		if (cargoState != g_hudElementStateCache[CMD_CARGO_ELEMENT]) {
			g_hudElementStateCache[CMD_CARGO_ELEMENT] = cargoState;
			left = g_hudElementLayouts[CMD_CARGO_ELEMENT].x;
			top = g_hudElementLayouts[CMD_CARGO_ELEMENT].y;
			FlightText_SetClipRect(
				left, top,
				left + g_hudElementLayouts[CMD_CARGO_ELEMENT]
						.clipWidth,
				top + g_flightFontLineHeight + 1);
			g_flightFillClipRectFn();
			FlightText_SetCursor(left, top);
			FlightText_SetColor(0x46);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_TARGET_CARGO, cargoText,
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(cargoText);
		}
	}

	currentTargetObjectIdx =
		(uint16_t)g_players[g_localPlayer].currentTargetObjectIdx;
	if (currentTargetObjectIdx < g_activeRegionCraftObjectSlotEnd &&
	    g_players[g_localPlayer].currentTargetObjectIdx != -1) {
		MobileObject *targetMobileObject;
		CraftData *targetCraft;
		AiController *controller;
		int displayPlanId;

		targetMobileObject = g_objectTable[currentTargetObjectIdx].mobj;
		targetCraft = targetMobileObject->pCraft;
		controller = &targetCraft->aiController;
		if (g_missionHeader.missionType == MISSION_TYPE_MELEE &&
		    g_flightPlayerCount > 1 && targetMobileObject != NULL &&
		    targetCraft != NULL &&
		    g_flightMissionState.locatePlayersEnabled == 0) {
			int playerTeam;

			playerTeam = (uint16_t)g_players[g_localPlayer].team;
			if (targetCraft->identifiedOrderByTeam[playerTeam] ==
			    0) {
				int team;
				int hostile;

				team = g_missionFlightGroups
					       [g_objectTable
							[currentTargetObjectIdx]
								.flightGroupIdx]
						       .fg.team;
				hostile =
					team == playerTeam
						? 0
						: g_missionTeams[playerTeam]
								  .allies[team] ==
							  0;
				if (hostile == 1) {
					return;
				}
			}
		}

		displayPlanId = controller->pendingPlanId;
		if (targetCraft->workingSubsystems == 0) {
			displayPlanId =
				pai_FindPlanIdByNameOrZero("disabledpln");
		} else if (targetMobileObject->speed == 0) {
			const char *planName;

			planName = g_planTable[displayPlanId].name;
			if (strcmp(planName, "flyhomepln") == 0 ||
			    strcmp(planName, "followhomepln") == 0 ||
			    strcmp(planName, "flyhomeevadepln") == 0 ||
			    strcmp(planName, "followhomeevadepln") == 0 ||
			    strcmp(planName, "enterhangarpln") == 0 ||
			    strcmp(planName, "exithangarpln") == 0 ||
			    strcmp(planName, "intohyperspacepln") == 0 ||
			    strcmp(planName, "outofhyperspacepln") == 0 ||
			    strcmp(planName, "starshipintohyperpln") == 0 ||
			    strcmp(planName, "starshipfollowhomepln") == 0) {
				displayPlanId =
					pai_FindPlanIdByNameOrZero("waitpln");
			}
		}

		if ((uint16_t)g_hudElementStateCache[CMD_ORDERS_ELEMENT] !=
		    displayPlanId) {
			uint16_t left;
			uint16_t top;
			const char *timeLabel;

			g_hudElementStateCache[CMD_ORDERS_ELEMENT] =
				(int16_t)displayPlanId;
			left = g_hudElementLayouts[CMD_ORDERS_ELEMENT].x;
			top = g_hudElementLayouts[CMD_ORDERS_ELEMENT].y;
			FlightText_SetClipRect(left, top, panelRight,
					       top + g_flightFontLineHeight +
						       1);
			g_flightFillClipRectFn();
			FlightText_SetCursor(left, top);
			if (g_flightResolutionMode ==
			    FLIGHT_RESOLUTION_320X240) {
				FlightText_SetColor(0x45);
			} else {
				FlightText_SetColor(0x46);
			}
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_CMD_ORDERS_LABEL,
				g_strCmdThreatDisplayText
					[CMD_THREAT_STR_CURRENT_ORDERS],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(
				g_strCmdThreatDisplayText
					[CMD_THREAT_STR_CURRENT_ORDERS]);
			if (g_hudElementLayouts[CMD_ORDERS_ELEMENT].clipWidth ==
			    0) {
				g_hudElementLayouts[CMD_ORDERS_ELEMENT]
					.clipWidth = FlightText_MeasureStringWidth(
					g_strCmdThreatDisplayText
						[CMD_THREAT_STR_CURRENT_ORDERS]);
			}
			FlightText_SetCursor(
				(uint16_t)left + (uint16_t)g_hudElementLayouts
							 [CMD_ORDERS_ELEMENT]
								 .clipWidth,
				g_flightCursorY);
			FlightText_SetColor(0x4E);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_CMD_ORDERS,
				g_strInFlightMessages
					[g_planReportMessageIdByPlanId
						 [displayPlanId]],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(
				g_strInFlightMessages
					[g_planReportMessageIdByPlanId
						 [displayPlanId]]);

			left = g_hudElementLayouts[CMD_ORDER_TIME_ELEMENT].x;
			top = g_hudElementLayouts[CMD_ORDER_TIME_ELEMENT].y;
			FlightText_SetClipRect(left, top, panelRight,
					       top + g_flightFontLineHeight +
						       1);
			g_flightFillClipRectFn();
			FlightText_SetCursor(left, top);
			if (g_flightResolutionMode ==
			    FLIGHT_RESOLUTION_320X240) {
				FlightText_SetColor(0x45);
			} else {
				FlightText_SetColor(0x46);
			}
			if (g_objectTable[(uint16_t)g_players[g_localPlayer]
						  .currentTargetObjectIdx]
				    .mobj->speed == 0) {
				timeLabel = g_strCmdThreatDisplayText
					[CMD_THREAT_STR_TIME_REMAINING];
			} else if (controller->targetObjIdx < 0x8000) {
				timeLabel = g_strCmdThreatDisplayText
					[CMD_THREAT_STR_TIME_TO_TARGET];
			} else {
				timeLabel = g_strCmdThreatDisplayText
					[CMD_THREAT_STR_TIME_TO_DESTINATION];
			}
			FlightText_SetScratch(timeLabel);
			g_hudElementLayouts[CMD_ORDER_TIME_ELEMENT].clipWidth =
				FlightText_MeasureStringWidth(
					g_flightTextScratchBuffer);
#ifdef XVT_MODERN
			XvtCockpitReadouts_ClearOrderTime();
			XvtCockpitText_ClearField(
				XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
			XvtCockpitText_RecordField(
				XVT_COCKPIT_TEXT_CMD_TIME_LABEL,
				g_flightTextScratchBuffer,
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(g_flightTextScratchBuffer);
		}

		{
			uint16_t orderTargetObjectIdx;
			unsigned int targetDistance;
			uint16_t valueLeft;
			uint16_t valueTop;
			uint16_t distanceWhole;
			uint16_t distanceFraction;

			if (g_objectTable[currentTargetObjectIdx]
				    .playerOwnerIdx != -1) {
				orderTargetObjectIdx =
					(uint16_t)g_players
						[g_objectTable
							 [currentTargetObjectIdx]
								 .playerOwnerIdx]
							.currentTargetObjectIdx;
			} else {
				orderTargetObjectIdx = controller->targetObjIdx;
			}
			if (targetCraft->workingSubsystems == 0) {
				orderTargetObjectIdx = UINT16_MAX;
			}
			if (strcmp(g_planTable[displayPlanId].name,
				   "waitpln") == 0) {
				orderTargetObjectIdx = UINT16_MAX;
			}

			if (orderTargetObjectIdx !=
			    (uint16_t)g_hudElementStateCache
				    [CMD_ORDER_TARGET_ELEMENT]) {
				const char *targetLabel;
				const char *distanceLabel;
				uint16_t left;
				uint16_t top;

				g_hudElementStateCache
					[CMD_ORDER_TARGET_ELEMENT] =
						(int16_t)orderTargetObjectIdx;
				if (g_flightResolutionMode ==
				    FLIGHT_RESOLUTION_320X240) {
					FlightText_SetColor(0x45);
				} else {
					FlightText_SetColor(0x46);
				}
				left = g_hudElementLayouts
					       [CMD_ORDER_TARGET_ELEMENT]
						       .x;
				top = g_hudElementLayouts
					      [CMD_ORDER_TARGET_ELEMENT]
						      .y;
				FlightText_SetClipRect(
					left, top, panelRight,
					top + g_flightFontLineHeight);
				g_flightFillClipRectFn();
				FlightText_SetCursor(left, top);
				if (orderTargetObjectIdx < 0x8000) {
					targetLabel = g_strCmdThreatDisplayText
						[CMD_THREAT_STR_CURRENT_TARGET];
				} else {
					targetLabel = g_strCmdThreatDisplayText
						[CMD_THREAT_STR_CURRENT_DESTINATION];
				}
				FlightText_SetScratch(targetLabel);
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_CMD_TARGET_LABEL,
					g_flightTextScratchBuffer,
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(
					g_flightTextScratchBuffer);
				g_hudElementLayouts[CMD_ORDER_TARGET_ELEMENT]
					.clipWidth =
					FlightText_MeasureStringWidth(
						g_flightTextScratchBuffer);

				left = g_hudElementLayouts
					       [CMD_ORDER_RANGE_ELEMENT]
						       .x;
				top = g_hudElementLayouts
					      [CMD_ORDER_RANGE_ELEMENT]
						      .y;
				FlightText_SetClipRect(
					left, top, panelRight,
					top + g_flightFontLineHeight);
				g_flightFillClipRectFn();
				FlightText_SetCursor(left, top);
				if (orderTargetObjectIdx < 0x8000) {
					distanceLabel = g_strCmdThreatDisplayText
						[CMD_THREAT_STR_DISTANCE_FROM_TARGET];
				} else {
					distanceLabel = g_strCmdThreatDisplayText
						[CMD_THREAT_STR_DISTANCE_TO_DESTINATION];
				}
				FlightText_SetScratch(distanceLabel);
#ifdef XVT_MODERN
				XvtCockpitReadouts_ClearOrderRange();
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_CMD_RANGE_LABEL,
					g_flightTextScratchBuffer,
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				FlightText_DrawString(
					g_flightTextScratchBuffer);
				g_hudElementLayouts[CMD_ORDER_RANGE_ELEMENT]
					.clipWidth =
					FlightText_MeasureStringWidth(
						g_flightTextScratchBuffer);

				valueLeft = g_hudElementLayouts
						    [CMD_ORDER_TARGET_ELEMENT]
							    .x +
					    g_hudElementLayouts
						    [CMD_ORDER_TARGET_ELEMENT]
							    .clipWidth;
				valueTop = g_hudElementLayouts
						   [CMD_ORDER_TARGET_ELEMENT]
							   .y;
				FlightText_SetClipRect(
					valueLeft, valueTop, panelRight,
					valueTop + g_flightFontLineHeight + 1);
				g_flightFillClipRectFn();
				FlightText_SetCursor(valueLeft, valueTop);
				if (orderTargetObjectIdx == NO_AI_TARGET ||
				    orderTargetObjectIdx == UINT16_MAX) {
#ifdef XVT_MODERN
					XvtCockpitText_RecordField(
						XVT_COCKPIT_TEXT_CMD_TARGET,
						g_strCmdThreatDisplayText
							[CMD_THREAT_STR_NONE],
						XVT_COCKPIT_ALIGN_LEFT);
#endif
					FlightText_DrawString(
						g_strCmdThreatDisplayText
							[CMD_THREAT_STR_NONE]);
				} else {
					Hud_AppendObjectDisplayName(
						orderTargetObjectIdx,
						NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
					XvtCockpitText_RecordField(
						XVT_COCKPIT_TEXT_CMD_TARGET,
						g_flightTextScratchBuffer,
						XVT_COCKPIT_ALIGN_LEFT);
#endif
					FlightText_DrawString(
						g_flightTextScratchBuffer);
				}
			}

			targetDistance = 0;
			if (orderTargetObjectIdx != UINT16_MAX) {
				uint16_t distance;

				if (g_objectTable[currentTargetObjectIdx]
					    .playerOwnerIdx != -1) {
					pai_ObjectRefDirectionToObjectRef(
						currentTargetObjectIdx,
						orderTargetObjectIdx);
				} else {
					trig2_ctop(
						controller->aimPointX -
							g_objectTable
								[currentTargetObjectIdx]
									.world_x,
						controller->aimPointY -
							g_objectTable
								[currentTargetObjectIdx]
									.world_y,
						controller->aimPointZ -
							g_objectTable
								[currentTargetObjectIdx]
									.world_z);
				}
				valueLeft = g_hudElementLayouts
						    [CMD_ORDER_RANGE_ELEMENT]
							    .x +
					    g_hudElementLayouts
						    [CMD_ORDER_RANGE_ELEMENT]
							    .clipWidth;
				valueTop = g_hudElementLayouts
						   [CMD_ORDER_RANGE_ELEMENT]
							   .y;
				targetDistance =
					(unsigned int)trig2_polardistance;
				FlightText_SetClipRect(
					valueLeft, valueTop, panelRight,
					valueTop + g_flightFontLineHeight + 1);
				FlightText_SetColor(0x4A);
				trig2_polardistance *= DISTANCE_FIXED_SCALE;
				distance =
					(uint16_t)(trig2_polardistance >> 16);
				if (distance >= MAX_DISPLAY_DISTANCE + 1) {
					distance = MAX_DISPLAY_DISTANCE;
				}
				distanceWhole =
					distance / DISTANCE_DECIMAL_SCALE;
				distanceFraction =
					distance -
					distanceWhole * DISTANCE_DECIMAL_SCALE;
				if (distanceFraction !=
				    (uint16_t)g_hudElementStateCache
					    [CMD_ORDER_RANGE_ELEMENT]) {
					g_hudElementStateCache
						[CMD_ORDER_RANGE_ELEMENT] =
							(int16_t)
								distanceFraction;
					FlightText_SetCursor(valueLeft,
							     valueTop);
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordNumber(
						XVT_COCKPIT_NUMBER_ORDER_RANGE,
						distanceWhole, 2, 1);
#endif
					FlightText_DrawDecimalNumber(
						distanceWhole, 2, 1);
#ifdef XVT_MODERN
					XvtCockpitText_RecordField(
						XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR,
						".", XVT_COCKPIT_ALIGN_LEFT);
#endif
					g_flightDrawCharFn('.');
#ifdef XVT_MODERN
					XvtCockpitReadouts_RecordNumber(
						XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION,
						distanceFraction, 2, 2);
#endif
					FlightText_DrawDecimalNumber(
						distanceFraction, 2, 2);
				}
			}

			valueLeft =
				g_hudElementLayouts[CMD_ORDER_TIME_ELEMENT].x +
				g_hudElementLayouts[CMD_ORDER_TIME_ELEMENT]
					.clipWidth;
			valueTop =
				g_hudElementLayouts[CMD_ORDER_TIME_ELEMENT].y;
			FlightText_SetClipRect(
				valueLeft, valueTop, panelRight,
				valueTop + g_flightFontLineHeight + 1);
			FlightText_SetColor(0x52);
			FlightText_SetCursor(valueLeft, valueTop);
			if (g_objectTable[currentTargetObjectIdx].mobj->speed ==
			    0) {
				uint16_t totalSeconds;
				uint16_t minutes;
				uint16_t seconds;

				if (strcmp(g_planTable[controller
							       ->pendingPlanId]
						   .name,
					   "board2pln") != 0 &&
				    strcmp(g_planTable[controller
							       ->pendingPlanId]
						   .name,
					   "waitpln") != 0) {
					if (targetDistance == 0) {
						g_hudElementStateCache
							[CMD_ORDER_TIME_ELEMENT] =
								0;
						g_flightFillClipRectFn();
#ifdef XVT_MODERN
						XvtCockpitText_ClearField(
							XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
#endif
#ifdef XVT_MODERN
						XvtCockpitReadouts_RecordNumber(
							XVT_COCKPIT_NUMBER_ORDER_MINUTES,
							0, 2, 1);
#endif
						FlightText_DrawDecimalNumber(
							0, 2, 1);
#ifdef XVT_MODERN
						XvtCockpitText_RecordField(
							XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
							":",
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						g_flightDrawCharFn(':');
#ifdef XVT_MODERN
						XvtCockpitReadouts_RecordNumber(
							XVT_COCKPIT_NUMBER_ORDER_SECONDS,
							0, 2, 2);
#endif
						FlightText_DrawDecimalNumber(
							0, 2, 2);
					} else {
#ifdef XVT_MODERN
						XvtCockpitReadouts_ClearOrderTime();
						XvtCockpitText_RecordField(
							XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN,
							g_strUnknown,
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						FlightText_DrawString(
							g_strUnknown);
					}
					return;
				}
				totalSeconds =
					(uint16_t)(controller->maneuverTimer /
						   SIMULATION_TICKS_PER_SECOND);
				minutes = totalSeconds / SECONDS_PER_MINUTE;
				seconds = totalSeconds -
					  minutes * SECONDS_PER_MINUTE;
				if (seconds ==
				    (uint16_t)g_hudElementStateCache
					    [CMD_ORDER_TIME_ELEMENT]) {
					return;
				}
				g_hudElementStateCache[CMD_ORDER_TIME_ELEMENT] =
					(int16_t)seconds;
				g_flightFillClipRectFn();
#ifdef XVT_MODERN
				XvtCockpitText_ClearField(
					XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
#endif
#ifdef XVT_MODERN
				XvtCockpitReadouts_RecordNumber(
					XVT_COCKPIT_NUMBER_ORDER_MINUTES,
					minutes, 2, 1);
#endif
				FlightText_DrawDecimalNumber(minutes, 2, 1);
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
					":", XVT_COCKPIT_ALIGN_LEFT);
#endif
				g_flightDrawCharFn(':');
#ifdef XVT_MODERN
				XvtCockpitReadouts_RecordNumber(
					XVT_COCKPIT_NUMBER_ORDER_SECONDS,
					seconds, 2, 2);
#endif
				FlightText_DrawDecimalNumber(seconds, 2, 2);
			} else {
				uint16_t totalSeconds;
				uint16_t minutes;
				uint16_t seconds;
				uint16_t distancePerSecond;

				distancePerSecond =
					(uint16_t)(DISTANCE_PER_SPEED_SECOND *
						   g_objectTable
							   [currentTargetObjectIdx]
								   .mobj
								   ->speed);
				totalSeconds = (uint16_t)(targetDistance /
							  distancePerSecond);
				minutes = totalSeconds / SECONDS_PER_MINUTE;
				seconds = totalSeconds -
					  minutes * SECONDS_PER_MINUTE;
				if (seconds ==
				    (uint16_t)g_hudElementStateCache
					    [CMD_ORDER_TIME_ELEMENT]) {
					return;
				}
				g_hudElementStateCache[CMD_ORDER_TIME_ELEMENT] =
					(int16_t)seconds;
				g_flightFillClipRectFn();
#ifdef XVT_MODERN
				XvtCockpitText_ClearField(
					XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
#endif
#ifdef XVT_MODERN
				XvtCockpitReadouts_RecordNumber(
					XVT_COCKPIT_NUMBER_ORDER_MINUTES,
					minutes, 2, 1);
#endif
				FlightText_DrawDecimalNumber(minutes, 2, 1);
#ifdef XVT_MODERN
				XvtCockpitText_RecordField(
					XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
					":", XVT_COCKPIT_ALIGN_LEFT);
#endif
				g_flightDrawCharFn(':');
#ifdef XVT_MODERN
				XvtCockpitReadouts_RecordNumber(
					XVT_COCKPIT_NUMBER_ORDER_SECONDS,
					seconds, 2, 2);
#endif
				FlightText_DrawDecimalNumber(seconds, 2, 2);
			}
		}
	}
}

/* Draws the target camera view's status of the local player's target: shield
 * and hull percents (elements 102 and 103) and the armament sprites 98 to 101
 * for lasers, ion cannons, warheads and beam. Each shows 1 when the target
 * carries the weapon: lasers and ion cannons blink between 1 and 2 while
 * linked, warheads while a lock builds (an AI craft's count only during a
 * rocket attack), and the beam shows 2 while active and charged. While a state
 * is not 0 its label from g_strThreatDisplayText is drawn at layouts 135 to
 * 138, plain for 1 and inverted for 2. A target outside the craft slots shows
 * zeros. The hull shows 100 when under 1 percent is left, and the label check
 * reads g_hudElementStateCache 135 to 138, which nothing here writes, so the
 * labels redraw on every call. */
// FUNCTION: XVT 0x440140
void Hud_DrawCmdTargetStatusIndicators(void)
{
	int currentTargetObjectIdx;
	CraftData *craft;
	int16_t y;
	uint16_t width;

	currentTargetObjectIdx =
		(uint16_t)g_players[g_localPlayer].currentTargetObjectIdx;

	{
		unsigned int shieldPercent;
		if (g_activeRegionCraftObjectSlotEnd > currentTargetObjectIdx) {
			unsigned int shield;
			unsigned int maxShield;
			unsigned int shieldRatioQ16;
			craft = g_objectTable[currentTargetObjectIdx]
					.mobj->pCraft;
			shield = (unsigned int)(craft->shieldEnergy[0] +
						craft->shieldEnergy[1]);
			maxShield = Craft_GetObjectMaxShield(
				g_players[g_localPlayer]
					.currentTargetObjectIdx);
			shield >>= 1;
			if (maxShield != 0) {
				shieldRatioQ16 =
					MATH2_longratioQ16(shield, maxShield);
				shieldRatioQ16 &= 0xFFFFu;
				shieldPercent = 2 * (shieldRatioQ16 / 0x28F);
			} else {
				shieldPercent = 0;
			}
		} else {
			shieldPercent = 0;
		}
		Hud_DrawCachedNumericElement(0x66, shieldPercent, 1);
	}

	{
		unsigned int hullPercent;
		unsigned int hullRatioQ16;
		if (g_activeRegionCraftObjectSlotEnd > currentTargetObjectIdx) {
			craft = g_objectTable[currentTargetObjectIdx]
					.mobj->pCraft;
			if (craft->objectKind !=
				    CRAFT_OBJECT_KIND_BREAKING_UP &&
			    craft->objectKind != CRAFT_OBJECT_KIND_EXPLODING) {
				if (craft->hullMax < craft->hullDamage) {
					hullPercent = 1;
				} else {
					hullRatioQ16 =
						(uint16_t)MATH2_longratioQ16(
							craft->hullMax -
								craft->hullDamage,
							craft->hullMax);
					hullRatioQ16 &= 0xFFFFu;
					hullPercent = hullRatioQ16 / 0x28F;
					if (hullPercent == 0) {
						hullPercent = 100;
					}
				}
			} else {
				hullPercent = 0;
			}
		} else {
			hullPercent = 0;
		}
		Hud_DrawCachedNumericElement(0x67, hullPercent, 1);
	}

	{
		uint16_t laserState;
		uint16_t i;
		laserState = 0;
		if (g_activeRegionCraftObjectSlotEnd > currentTargetObjectIdx) {
			uint8_t cannonClassCount;
			i = 0;
			cannonClassCount = craft->cannonGroupCount;
			if (cannonClassCount != 0) {
				do {
					uint8_t projectileType;
					projectileType =
						craft->laserState
							.projectileTypeId[i];
					if (projectileType ==
						    PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER ||
					    projectileType ==
						    PROJECTILE_OBJECT_TYPE_REBEL_LASER) {
						laserState = 1;
						if (craft->laserState
							    .linkMode[i] != 0) {
							laserState =
								(uint16_t)(((g_missionElapsedClock
										     .subsecondTicks /
									     59) &
									    1) +
									   1);
						}
					}
					++i;
				} while (i < cannonClassCount);
			}
		}
#ifdef XVT_MODERN
		XvtCockpitReadouts_RecordArmament(0, laserState);
#endif
		Hud_DrawCachedSpriteElement(0x62, laserState);
		if (laserState != 0 &&
		    (uint16_t)g_hudElementStateCache[135] != laserState) {
			int16_t bottom;
			if (laserState == 1) {
				FlightText_SetColor(
					g_hudElementLayouts[135]
						.colorIndexOrWidgetParam);
				FlightText_SetBackgroundColor(0x2C);
			} else {
				FlightText_SetColor(0x2C);
				FlightText_SetBackgroundColor(
					g_hudElementLayouts[135]
						.colorIndexOrWidgetParam +
					1);
			}
			y = g_hudElementLayouts[135].y;
			bottom = g_hudElementLayouts[135].y +
				 g_flightFontLineHeight;
			width = FlightText_MeasureStringWidth(
				g_strThreatDisplayText[0]);
			width += g_hudElementLayouts[135].x;
			FlightText_SetClipRect(g_hudElementLayouts[135].x, y,
					       width, bottom);
			FlightText_SetCursor(g_hudElementLayouts[135].x,
					     g_hudElementLayouts[135].y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							0),
				g_strThreatDisplayText[0],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(g_strThreatDisplayText[0]);
		}
	}

	{
		uint16_t ionState;
		uint16_t i;
		ionState = 0;
		if (g_activeRegionCraftObjectSlotEnd > currentTargetObjectIdx) {
			uint8_t cannonClassCount;
			i = 0;
			cannonClassCount = craft->cannonGroupCount;
			while (i < cannonClassCount) {
				if (craft->laserState.projectileTypeId[i] ==
				    PROJECTILE_OBJECT_TYPE_ION_LASER) {
					ionState = 1;
					if (craft->laserState.linkMode[i] !=
					    0) {
						ionState =
							(uint16_t)(((g_missionElapsedClock
									     .subsecondTicks /
								     59) &
								    1) +
								   1);
					}
				}
				++i;
			}
		}
#ifdef XVT_MODERN
		XvtCockpitReadouts_RecordArmament(1, ionState);
#endif
		Hud_DrawCachedSpriteElement(0x63, ionState);
		if (ionState != 0 &&
		    (uint16_t)g_hudElementStateCache[136] != ionState) {
			int16_t bottom;
			if (ionState == 1) {
				FlightText_SetColor(
					g_hudElementLayouts[136]
						.colorIndexOrWidgetParam);
				FlightText_SetBackgroundColor(0x2C);
			} else {
				FlightText_SetColor(0x2C);
				FlightText_SetBackgroundColor(
					g_hudElementLayouts[136]
						.colorIndexOrWidgetParam +
					1);
			}
			y = g_hudElementLayouts[136].y;
			bottom = g_hudElementLayouts[136].y +
				 g_flightFontLineHeight;
			width = FlightText_MeasureStringWidth(
				g_strThreatDisplayText[1]);
			width += g_hudElementLayouts[136].x;
			FlightText_SetClipRect(g_hudElementLayouts[136].x, y,
					       width, bottom);
			FlightText_SetCursor(g_hudElementLayouts[136].x,
					     g_hudElementLayouts[136].y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							1),
				g_strThreatDisplayText[1],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(g_strThreatDisplayText[1]);
		}
	}

	{
		uint16_t warheadState;
		uint16_t i;
		warheadState = 0;
		if (g_activeRegionCraftObjectSlotEnd > currentTargetObjectIdx) {
			if (g_objectTable[currentTargetObjectIdx]
				    .playerOwnerIdx == -1) {
				if (craft->aiController.maneuverMode ==
				    AI_MANEUVER_MODE_ROCKET_ATTACK) {
					uint8_t warheadLauncherCount;
					i = 0;
					warheadLauncherCount =
						craft->warheadLauncherCount;
					if (warheadLauncherCount != 0) {
						do {
							if (craft->warheadSlotTypeIds
								    [i] != 0) {
								warheadState =
									1;
								if (craft->warheadLockTicks >
								    0) {
									warheadState =
										(uint16_t)(((g_missionElapsedClock
												     .subsecondTicks /
											     59) &
											    1) +
											   1);
								}
							}
							++i;
						} while (i <
							 warheadLauncherCount);
					}
				}
			} else {
				uint8_t warheadLauncherCount;
				i = 0;
				warheadLauncherCount =
					craft->warheadLauncherCount;
				if (warheadLauncherCount != 0) {
					do {
						if (craft->warheadSlotTypeIds
							    [i] != 0) {
							warheadState = 1;
							if (craft->warheadLockTicks >
							    0) {
								warheadState =
									(uint16_t)(((g_missionElapsedClock
											     .subsecondTicks /
										     59) &
										    1) +
										   1);
							}
						}
						++i;
					} while (i < warheadLauncherCount);
				}
			}
		}
#ifdef XVT_MODERN
		XvtCockpitReadouts_RecordArmament(2, warheadState);
#endif
		Hud_DrawCachedSpriteElement(0x64, warheadState);
		if (warheadState != 0 &&
		    (uint16_t)g_hudElementStateCache[137] != warheadState) {
			int16_t bottom;
			if (warheadState == 1) {
				FlightText_SetColor(
					g_hudElementLayouts[137]
						.colorIndexOrWidgetParam);
				FlightText_SetBackgroundColor(0x2C);
			} else {
				FlightText_SetColor(0x2C);
				FlightText_SetBackgroundColor(
					g_hudElementLayouts[137]
						.colorIndexOrWidgetParam +
					1);
			}
			y = g_hudElementLayouts[137].y;
			bottom = g_hudElementLayouts[137].y +
				 g_flightFontLineHeight;
			width = FlightText_MeasureStringWidth(
				g_strThreatDisplayText[2]);
			width += g_hudElementLayouts[137].x;
			FlightText_SetClipRect(g_hudElementLayouts[137].x, y,
					       width, bottom);
			FlightText_SetCursor(g_hudElementLayouts[137].x,
					     g_hudElementLayouts[137].y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							2),
				g_strThreatDisplayText[2],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(g_strThreatDisplayText[2]);
		}
	}

	{
		uint16_t beamState;
		beamState = 0;
		if (g_activeRegionCraftObjectSlotEnd > currentTargetObjectIdx &&
		    craft->beamTypeId != BEAM_TYPE_NONE) {
			beamState = 1;
			if (craft->beamActive != 0 && craft->beamCharge != 0) {
				beamState = 2;
			}
		}
#ifdef XVT_MODERN
		XvtCockpitReadouts_RecordArmament(3, beamState);
#endif
		Hud_DrawCachedSpriteElement(0x65, beamState);
		if (beamState != 0 &&
		    (uint16_t)g_hudElementStateCache[138] != beamState) {
			int16_t bottom;
			if (beamState == 1) {
				FlightText_SetColor(
					g_hudElementLayouts[138]
						.colorIndexOrWidgetParam);
				FlightText_SetBackgroundColor(0x2C);
			} else {
				FlightText_SetColor(0x2C);
				FlightText_SetBackgroundColor(
					g_hudElementLayouts[138]
						.colorIndexOrWidgetParam +
					1);
			}
			y = g_hudElementLayouts[138].y;
			bottom = g_hudElementLayouts[138].y +
				 g_flightFontLineHeight;
			width = FlightText_MeasureStringWidth(
				g_strThreatDisplayText[3]);
			width += g_hudElementLayouts[138].x;
			FlightText_SetClipRect(g_hudElementLayouts[138].x, y,
					       width, bottom);
			FlightText_SetCursor(g_hudElementLayouts[138].x,
					     g_hudElementLayouts[138].y);
#ifdef XVT_MODERN
			XvtCockpitText_RecordField(
				(XvtCockpitTextFieldId)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							3),
				g_strThreatDisplayText[3],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			FlightText_DrawString(g_strThreatDisplayText[3]);
		}
	}
}

/* Blits sprite selector + state of elementIdx's layout at its position, with
 * its colorIndexOrWidgetParam as the transparent color, when state differs from
 * the element's entry in g_hudElementStateCache, which it then records. */
// FUNCTION: XVT 0x440760
void Hud_DrawCachedSpriteElement(unsigned int elementIdx, unsigned int state)
{
	if ((uint16_t)g_hudElementStateCache[elementIdx] != state) {
		g_hudElementStateCache[elementIdx] = (int16_t)state;
		g_flightBlitSpriteFn(
			g_hudPanelSpriteDataByIndex
				[(uint16_t)g_hudElementLayouts[elementIdx]
					 .selector +
				 state],
			g_hudElementLayouts[elementIdx].x,
			g_hudElementLayouts[elementIdx].y,
			g_hudElementLayouts[elementIdx].colorIndexOrWidgetParam,
			0);
	}
}

/* Blits the first sprite of elementIdx's layout with palette shift state and
 * fade amount fade, when state differs from the element's entry in
 * g_hudElementStateCache, which it then records; a change of fade alone draws
 * nothing. */
// FUNCTION: XVT 0x4407D0
void Hud_DrawCachedFadedSpriteElement(uint16_t elementIdx, int16_t state,
				      int16_t fade)
{
	if (g_hudElementStateCache[elementIdx] != state) {
		g_hudElementStateCache[elementIdx] = state;
		g_flightBlitSpriteFadedFn(
			g_hudPanelSpriteDataByIndex
				[g_hudElementLayouts[elementIdx].selector],
			g_hudElementLayouts[elementIdx].x,
			g_hudElementLayouts[elementIdx].y,
			g_hudElementLayouts[elementIdx].colorIndexOrWidgetParam,
			(int8_t)state, fade);
	}
}

/* Draws value in elementIdx's layout, in as many digits as its selector and at
 * least minDigits, over a cleared field, when value differs from the element's
 * entry in g_hudElementStateCache, which it then records. The target's system,
 * shield and hull values (elements 82, 85, 86, 102 and 103 of any set) are
 * drawn in color code 74 at 20 or less and 78 at 50 or less; the speed and
 * throttle (40 and 41) in 82 while engineOverdriveOff is 0; anything else in
 * the layout's colorIndexOrWidgetParam. */
// FUNCTION: XVT 0x440840
void Hud_DrawCachedNumericElement(uint16_t elementIdx, int16_t value,
				  uint16_t minDigits)
{
	uint16_t normalizedElementIdx;
	uint16_t selector;

	normalizedElementIdx = elementIdx;
	if (g_hudElementStateCache[elementIdx] == value) {
		return;
	}
	g_hudElementStateCache[elementIdx] = value;
	selector = g_hudElementLayouts[elementIdx].selector;
	FlightText_SetClipRect(g_hudElementLayouts[elementIdx].x,
			       g_hudElementLayouts[elementIdx].y,
			       g_hudElementLayouts[elementIdx].x +
				       selector * g_flightFontDigitWidth + 2,
			       g_hudElementLayouts[elementIdx].y +
				       g_flightFontLineHeight);
	g_flightFillClipRectFn();

	if (elementIdx > HUD_INSTRUMENTS_PER_SET) {
		normalizedElementIdx =
			elementIdx -
			HUD_INSTRUMENTS_PER_SET * ((uint16_t)(elementIdx - 1) /
						   HUD_INSTRUMENTS_PER_SET);
	}
	if ((uint16_t)value <= 20 &&
	    (normalizedElementIdx == 85 || normalizedElementIdx == 82 ||
	     normalizedElementIdx == 102 || normalizedElementIdx == 86 ||
	     normalizedElementIdx == 103)) {
		FlightText_SetColor(74);
	} else if ((uint16_t)value <= 50 &&
		   (normalizedElementIdx == 85 || normalizedElementIdx == 82 ||
		    normalizedElementIdx == 86 || normalizedElementIdx == 102 ||
		    normalizedElementIdx == 103)) {
		FlightText_SetColor(78);
	} else if ((normalizedElementIdx == 41 || normalizedElementIdx == 40) &&
		   g_objectTable[g_players[g_localPlayer].objectIndex]
				   .mobj->pCraft->engineOverdriveOff == 0) {
		FlightText_SetColor(82);
	} else {
		FlightText_SetColor(g_hudElementLayouts[elementIdx]
					    .colorIndexOrWidgetParam);
	}
	FlightText_SetCursor(g_hudElementLayouts[elementIdx].x,
			     g_hudElementLayouts[elementIdx].y);
#ifdef XVT_MODERN
	XvtCockpitReadouts_RecordCachedNumber(elementIdx, value, minDigits);
#endif
	FlightText_DrawDecimalNumber(value, selector, minDigits);
}

/* Loads the local player's cockpit: sets g_hudCockpitBasePath to the
 * resolution's cockpit folder and the model's cockpitResourceName, sets
 * g_hudPanelSetId to 0, reads its .INT file and the craft list's, and loads the
 * cockpit image resources. Does not check the name's length. */
// FUNCTION: XVT 0x4409D0
void Hud_LoadCockpitResources(void)
{
	char cockpitResourceName[16];
	const char *sourceName;
	ModelIndex modelIndex;
	unsigned int nameIndex;

	strcpy(g_hudCockpitBasePath, g_hudCockpitResolutionDirectory);
	modelIndex = GetModelIndexFromType(
		g_objectTable[g_players[g_localPlayer].objectIndex].objectType);
	sourceName = g_modelDefs[modelIndex].cockpitResourceName;
	for (nameIndex = 0; sourceName[nameIndex] != '\0'; ++nameIndex) {
		cockpitResourceName[nameIndex] = sourceName[nameIndex];
	}
	cockpitResourceName[nameIndex] = sourceName[nameIndex];
	strcat(g_hudCockpitBasePath, cockpitResourceName);
	g_hudPanelSetId = 0;
	Hud_LoadCockpitInterfaceFile(g_hudCockpitBasePath);
	Hud_LoadAuxiliaryCockpitInterfaceFile();
	modelIndex = GetModelIndexFromType(
		g_objectTable[g_players[g_localPlayer].objectIndex].objectType);
	Hud_LoadCockpitSpriteResources(modelIndex);
}

/* Reads basePath plus ".INT", named in g_hudCockpitResourcePath, into the HUD
 * tables: the 28 g_hudCockpitResourceDescriptors, the first 288
 * g_hudElementLayouts (the cockpit and HUD-only sets),
 * g_hudPanelSpriteFileInfo, and the cockpit and HUD-only inset span masks, 480
 * bytes each at 640x480 and 480x360 and 200 at 320x240. Reads go through
 * FeDiskIo_ReadWithRetryPrompt. The modern build records the cockpit for its
 * renderer. Does not check the path's length. */
// FUNCTION: XVT 0x440B10
void Hud_LoadCockpitInterfaceFile(const char *basePath)
{
	XvtFile *stream;

	strcpy(g_hudCockpitResourcePath, basePath);
	strcat(g_hudCockpitResourcePath, ".INT");
	FeDiskIo_OpenGlobalStream(g_hudCockpitResourcePath, "rb", 1, 0);
	stream = g_stream;
	FeDiskIo_ReadWithRetryPrompt(g_hudCockpitResourceDescriptors,
				     sizeof(HudCockpitResourceDescriptor), 28,
				     stream);
	FeDiskIo_ReadWithRetryPrompt(
		g_hudElementLayouts, sizeof(HudElementLayout),
		HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX, stream);
	FeDiskIo_ReadWithRetryPrompt(&g_hudPanelSpriteFileInfo,
				     sizeof(HudPanelSpriteFileInfo), 1, stream);
	if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480) {
		FeDiskIo_ReadWithRetryPrompt(g_hudCockpitInsetSpanMask, 480, 1,
					     stream);
		FeDiskIo_ReadWithRetryPrompt(g_hudOnlyViewInsetSpanMask, 480, 1,
					     stream);
	} else if (g_flightResolutionMode == FLIGHT_RESOLUTION_480X360) {
		FeDiskIo_ReadWithRetryPrompt(g_hudCockpitInsetSpanMask, 480, 1,
					     stream);
		FeDiskIo_ReadWithRetryPrompt(g_hudOnlyViewInsetSpanMask, 480, 1,
					     stream);
	} else {
		FeDiskIo_ReadWithRetryPrompt(g_hudCockpitInsetSpanMask, 200, 1,
					     stream);
		FeDiskIo_ReadWithRetryPrompt(g_hudOnlyViewInsetSpanMask, 200, 1,
					     stream);
	}
	FeDiskIo_CloseGlobalStream(0);
#ifdef XVT_MODERN
	XvtRenderAssets_CaptureCockpit(0);
#endif
}

/* Reads the craft list view's .INT file, the one named by
 * g_hudCockpitResourceDescriptors[HUD_VIEW_CRAFT_LIST] in the resolution's
 * cockpit folder: its 144 layouts into the third set of g_hudElementLayouts and
 * g_hudCraftListInsetSpanMask, 480 or 200 bytes by resolution. Returns what
 * FeDiskIo_CloseGlobalStream returns. */
// FUNCTION: XVT 0x440C50
int16_t Hud_LoadAuxiliaryCockpitInterfaceFile(void)
{
	XvtFile *stream;

	strcpy(g_hudCockpitResourcePath, g_hudCockpitResolutionDirectory);
	strcat(g_hudCockpitResourcePath,
	       g_hudCockpitResourceDescriptors[HUD_VIEW_CRAFT_LIST].lfdName);
	strcat(g_hudCockpitResourcePath, ".INT");
	FeDiskIo_OpenGlobalStream(g_hudCockpitResourcePath, "rb", 1, 0);
	stream = g_stream;
	FeDiskIo_ReadWithRetryPrompt(
		&g_hudElementLayouts[HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX],
		sizeof(HudElementLayout), HUD_INSTRUMENTS_PER_SET, stream);
	if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480) {
		FeDiskIo_ReadWithRetryPrompt(g_hudCraftListInsetSpanMask, 480,
					     1, stream);
	} else if (g_flightResolutionMode == FLIGHT_RESOLUTION_480X360) {
		FeDiskIo_ReadWithRetryPrompt(g_hudCraftListInsetSpanMask, 480,
					     1, stream);
	} else {
		FeDiskIo_ReadWithRetryPrompt(g_hudCraftListInsetSpanMask, 200,
					     1, stream);
	}
#ifdef XVT_MODERN
	XvtRenderAssets_CaptureCockpit(1);
#endif
	return FeDiskIo_CloseGlobalStream(0);
}

/* Switches playerIdx to hudViewState even when it is already showing, by
 * setting hudStateLive to 0xFF first, then for the local player resets the
 * message panes without expiring them. */
// FUNCTION: XVT 0x440D60
void Hud_ForcePlayerViewState(int hudViewState, int playerIdx)
{
	g_players[playerIdx].viewState.hudStateLive = UINT8_MAX;
	Hud_SetHudViewState(hudViewState, playerIdx);
	if (playerIdx == g_localPlayer) {
		Hud_ResetFlightMessagePanes(0);
	}
}

/* Redraws the local player's cockpit for view hudViewState; does nothing for
 * another player. The view's resourceRef picks the cockpit image: below 0x80
 * its own, from 0x80 another, from 0xC0 another drawn mirrored; the full-screen
 * view always takes its own. Reloads the panel sprites (.PNL) when
 * g_hudPanelSetId differs from g_hudLoadedPanelSetId, adding the craft list's
 * first sprite to layout 396 once. For a view with a cockpit it loads the
 * image's LFD entries into g_flightScratchScreenBuffer if they are not loaded,
 * sets palette entries 0 to 63 from it, clears the surface, blits the image,
 * and sets the flight viewport, span mask and g_projOffsetY from the view's
 * descriptor; the full-screen view gets the whole surface and offset 0.
 *
 * Then it saves the MFD pages when leaving the forward or HUD-only view, closes
 * them when leaving the craft list, and sets g_hudInstrumentSetBaseIndex and
 * the pages for the new view: the HUD-only set, with the saved pages unless
 * coming from the forward view; the craft list set, with the map help and
 * friendly craft pages; the cockpit set for every other view. The full-screen
 * view closes the pages while the external camera is on, the target camera view
 * always; the remaining cockpit views, unless coming from the HUD-only view,
 * get the saved pages back. The craft list, the target camera and those cockpit
 * views also reset the message panes with expiry. Then Hud_InitHUD, a palette
 * reset, and for resource 17 the view's displayName at layout 49. Sets
 * g_flightInitialTextureCacheFlushPending to 1 and
 * g_flightDisplayRebuildPending to 0. The modern build resets and refreshes its
 * captured cockpit. */
// FUNCTION: XVT 0x440DA0
void Hud_RebuildDisplayForViewState(int hudViewState, int playerIdx)
{
	enum {
		HUD_VIEW_RESOURCE_NAME = 17,
		HUD_RESOURCE_REFERENCE = 0x80,
		HUD_RESOURCE_MIRRORED_REFERENCE = 0xC0,
		HUD_AUXILIARY_PANEL_LAYOUT = 396,
		HUD_RESOURCE_NAME_LAYOUT = 49,
		PALETTE_COCKPIT_COLOR_COUNT = 0x40,
	};

	uint8_t mirrorHorizontal;
	int cockpitX;
	unsigned int resourceIndex;
	unsigned int viewportDescriptorIndex;
	unsigned int baseOffset;
	unsigned int pageIndex;
	int resourceLoaded;
	unsigned int textY;

	if (g_localPlayer != playerIdx) {
		return;
	}

#ifdef XVT_MODERN
	XvtCockpitText_ResetFields();
	XvtCockpitReadouts_Reset();
	XvtCockpitPages_ResetWorking();
	XvtCockpitMessages_ResetWorking();
#endif
	mirrorHorizontal = 0;
	cockpitX = 0;
	resourceIndex =
		g_hudCockpitResourceDescriptors[hudViewState].resourceRef;
	if (hudViewState != HUD_VIEW_FULL_SCREEN) {
		if (resourceIndex >= HUD_RESOURCE_MIRRORED_REFERENCE) {
			resourceIndex -= HUD_RESOURCE_MIRRORED_REFERENCE;
			mirrorHorizontal = 1;
			cockpitX = (int)(g_screenWidth - 1);
		} else if (resourceIndex < HUD_RESOURCE_REFERENCE) {
			resourceIndex = (unsigned int)hudViewState;
		} else {
			resourceIndex -= HUD_RESOURCE_REFERENCE;
		}
	} else {
		resourceIndex = (unsigned int)hudViewState;
	}

	if (g_hudLoadedPanelSetId != g_hudPanelSetId) {
		g_hudPanelSpriteDataWriteCursor = (uint8_t *)Memory_LockHandle(
			g_hudPanelSpriteDataHandle);
		Memory_UnlockHandle(g_hudPanelSpriteDataHandle);
		strcpy(g_hudCockpitBasePath, g_hudCockpitResolutionDirectory);
		strcat(g_hudCockpitBasePath, g_hudPanelSpriteFileInfo.baseName);
		strcat(g_hudCockpitBasePath, ".PNL");
		Hud_LoadPanelSpriteRecords(
			g_hudCockpitBasePath, 0,
			g_hudPanelSpriteFileInfo.spriteCountAddend +
				g_hudPanelSpriteFileInfo.spriteCount,
			0);
		g_hudLoadedPanelSetId = g_hudPanelSetId;

		if (g_hudElementLayouts[HUD_AUXILIARY_PANEL_LAYOUT].selector ==
		    0) {
			strcpy(g_hudCockpitBasePath,
			       g_hudCockpitResolutionDirectory);
			strcat(g_hudCockpitBasePath,
			       g_hudCockpitResourceDescriptors
				       [HUD_VIEW_CRAFT_LIST]
					       .lfdName);
			strcat(g_hudCockpitBasePath, ".PNL");
			Hud_LoadPanelSpriteRecords(
				g_hudCockpitBasePath,
				g_hudPanelSpriteFileInfo.spriteCountAddend +
					g_hudPanelSpriteFileInfo.spriteCount +
					1,
				1, 0);
			g_hudElementLayouts[HUD_AUXILIARY_PANEL_LAYOUT]
				.selector =
				g_hudPanelSpriteFileInfo.spriteCountAddend +
				g_hudPanelSpriteFileInfo.spriteCount + 1;
		}
	}

	FlightRender_InvokeTransitionHook(1);
	if (g_players[g_localPlayer].viewState.hudStateLive !=
	    HUD_VIEW_FULL_SCREEN) {
		resourceLoaded =
			g_hudCockpitResources[resourceIndex].memoryHandle !=
				0 &&
			g_hudCockpitResourcesLoaded != 0;
		if (resourceLoaded == 0) {
			g_hudCockpitResourceWriteCursor =
				g_flightScratchScreenBuffer;
			Hud_LoadCockpitLfdEntries(
				g_hudCockpitResourceDescriptors[resourceIndex]
					.lfdName,
				g_hudCockpitResources[resourceIndex].entries,
				sizeof(g_hudCockpitResources[resourceIndex]
					       .entries) /
					sizeof(g_hudCockpitResources
						       [resourceIndex]
							       .entries[0]));
		}
		g_flightSetPaletteRangeFn(
			(RgbTriplet *)g_hudCockpitResources[resourceIndex]
				.entries[2],
			0, PALETTE_COCKPIT_COLOR_COUNT);
		FlightText_SetClipRect(0, 0, g_surfaceWidth, g_surfaceHeight);
		g_flightTextBgColor = g_flightBackgroundColorIndex;
		g_flightFillClipRectFn();
		g_flightBlitSpriteFn(
			g_hudCockpitResources[resourceIndex].entries[0],
			cockpitX, 0, 0, mirrorHorizontal);

		viewportDescriptorIndex = mirrorHorizontal == 1
						  ? (unsigned int)hudViewState
						  : resourceIndex;
		baseOffset = g_flightComputePixelOffsetFn(
			g_hudCockpitResourceDescriptors[viewportDescriptorIndex]
				.viewportOriginX,
			g_hudCockpitResourceDescriptors[viewportDescriptorIndex]
				.viewportOriginY);
		SetFlightViewport(
			g_hudCockpitResourceDescriptors[viewportDescriptorIndex]
				.viewportWidth,
			g_hudCockpitResourceDescriptors[viewportDescriptorIndex]
				.viewportHeight,
			g_flightViewportMode, baseOffset);
		FlightSw_CopyViewportSpanMaskRle(
			g_hudCockpitResources[resourceIndex].entries[1],
			g_flightVpWidth, g_flightVpHeight, mirrorHorizontal);
		g_projOffsetY =
			g_hudCockpitResourceDescriptors[viewportDescriptorIndex]
				.projectionOffsetY;
	} else {
		FlightText_SetClipRect(0, 0, g_surfaceWidth, g_surfaceHeight);
		g_flightTextBgColor = g_flightBackgroundColorIndex;
		g_flightFillClipRectFn();
		SetFlightViewport(g_surfaceWidth, g_surfaceHeight,
				  g_flightViewportMode, 0);
		FlightSw_BuildFullViewportSpanMaskRle(
			(uint16_t)g_surfaceWidth,
			(unsigned int)g_surfaceHeight);
		g_projOffsetY = 0;
	}

	if (g_players[playerIdx].viewState.hudStateMirror ==
		    HUD_VIEW_HUD_ONLY ||
	    g_players[playerIdx].viewState.hudStateMirror == HUD_VIEW_FORWARD) {
		g_mfdSavedSecondaryPage = g_mfdSecondaryPage;
		g_mfdSavedActivePage = g_mfdActivePage;
		for (pageIndex = MFD_PAGE_SCOREBOARD;
		     pageIndex < MFD_PAGE_COUNT; ++pageIndex) {
			g_savedMfdPageStates[pageIndex] =
				g_mfdPageStates[pageIndex];
		}
	}

	if (g_players[playerIdx].viewState.hudStateMirror ==
	    HUD_VIEW_CRAFT_LIST) {
		switch (g_flightResolutionMode) {
		case FLIGHT_RESOLUTION_320X240:
			g_mfdCraftListBlitWidth = 112;
			g_mfdCraftListBlitHeight = 47;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_mfdCraftListBlitWidth = 225;
			g_mfdCraftListBlitHeight = 94;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_mfdCraftListBlitWidth = 168;
			g_mfdCraftListBlitHeight = 70;
			break;
		}
		for (pageIndex = MFD_PAGE_SCOREBOARD;
		     pageIndex < MFD_PAGE_COUNT; ++pageIndex) {
			if (g_mfdPageStates[pageIndex] !=
			    MFD_PAGE_STATE_CLOSED) {
				g_mfdPageStates[pageIndex] =
					MFD_PAGE_STATE_CLOSING;
			}
		}
		g_mfdActivePage = MFD_PAGE_NONE;
		g_mfdSecondaryPage = MFD_PAGE_NONE;
		Hud_UpdateMfdPages();
	}

	if (hudViewState == HUD_VIEW_FULL_SCREEN) {
		g_hudInstrumentSetBaseIndex = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
		if (g_players[playerIdx].viewState.externalCameraActive != 0) {
			for (pageIndex = MFD_PAGE_SCOREBOARD;
			     pageIndex < MFD_PAGE_COUNT; ++pageIndex) {
				if (g_mfdPageStates[pageIndex] !=
				    MFD_PAGE_STATE_CLOSED) {
					g_mfdPageStates[pageIndex] =
						MFD_PAGE_STATE_CLOSING;
				}
			}
			g_mfdActivePage = MFD_PAGE_NONE;
			g_mfdSecondaryPage = MFD_PAGE_NONE;
		}
	} else if (hudViewState == HUD_VIEW_HUD_ONLY) {
		if (g_players[playerIdx].viewState.hudStateMirror !=
		    HUD_VIEW_FORWARD) {
			g_mfdSecondaryPage = g_mfdSavedSecondaryPage;
			g_mfdActivePage = g_mfdSavedActivePage;
			for (pageIndex = MFD_PAGE_SCOREBOARD;
			     pageIndex < MFD_PAGE_COUNT; ++pageIndex) {
				g_mfdPageStates[pageIndex] =
					g_savedMfdPageStates[pageIndex];
			}
		}
		g_hudInstrumentSetBaseIndex =
			HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX;
	} else if (hudViewState == HUD_VIEW_CRAFT_LIST) {
		for (pageIndex = MFD_PAGE_SCOREBOARD;
		     pageIndex < MFD_PAGE_COUNT; ++pageIndex) {
			if (g_mfdPageStates[pageIndex] !=
			    MFD_PAGE_STATE_CLOSED) {
				g_mfdPageStates[pageIndex] =
					MFD_PAGE_STATE_CLOSING;
			}
		}
		g_hudInstrumentSetBaseIndex =
			HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX;
		++g_mfdPageStates[MFD_PAGE_MAP_HELP];
		++g_mfdPageStates[MFD_PAGE_FRIENDLY_CRAFT];
		g_mfdActivePage = MFD_PAGE_FRIENDLY_CRAFT;
		g_mfdSecondaryPage = MFD_PAGE_FRIENDLY_CRAFT;
		Hud_ResetFlightMessagePanes(1);
	} else if (hudViewState == HUD_VIEW_TARGET_CAMERA) {
		for (pageIndex = MFD_PAGE_SCOREBOARD;
		     pageIndex < MFD_PAGE_COUNT; ++pageIndex) {
			if (g_mfdPageStates[pageIndex] !=
			    MFD_PAGE_STATE_CLOSED) {
				g_mfdPageStates[pageIndex] =
					MFD_PAGE_STATE_CLOSING;
			}
		}
		g_hudInstrumentSetBaseIndex = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
		g_mfdActivePage = MFD_PAGE_NONE;
		g_mfdSecondaryPage = MFD_PAGE_NONE;
		Hud_ResetFlightMessagePanes(1);
	} else {
		g_hudInstrumentSetBaseIndex = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
		if (g_players[playerIdx].viewState.hudStateMirror !=
		    HUD_VIEW_HUD_ONLY) {
			Hud_ResetFlightMessagePanes(1);
			g_mfdSecondaryPage = g_mfdSavedSecondaryPage;
			g_mfdActivePage = g_mfdSavedActivePage;
			for (pageIndex = MFD_PAGE_SCOREBOARD;
			     pageIndex < MFD_PAGE_COUNT; ++pageIndex) {
				g_mfdPageStates[pageIndex] =
					g_savedMfdPageStates[pageIndex];
			}
		}
	}

	Hud_InitHUD(playerIdx);
	g_flightInitialTextureCacheFlushPending = 1;
	FlightRender_ResetPalette(1);
	g_flightDisplayRebuildPending = 0;
	if (resourceIndex == HUD_VIEW_RESOURCE_NAME) {
		FlightText_SetFontTier(2);
		textY = 0;
		textY = (uint16_t)g_hudElementLayouts[HUD_RESOURCE_NAME_LAYOUT]
				.y;
		FlightText_SetClipRect(
			g_hudElementLayouts[HUD_RESOURCE_NAME_LAYOUT].x,
			(int)textY,
			g_hudElementLayouts[HUD_RESOURCE_NAME_LAYOUT].x +
				g_hudElementLayouts[HUD_RESOURCE_NAME_LAYOUT]
					.clipWidth,
			(int)(textY + g_flightFontLineHeight + 1));
		FlightText_SetBackgroundColor(PALETTE_COCKPIT_COLOR_COUNT);
		g_flightFillClipRectFn();
		FlightText_SetColor(PALETTE_COCKPIT_COLOR_COUNT + 3);
		FlightText_SetCursor(0, textY);
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(
			XVT_COCKPIT_TEXT_RESOURCE_NAME,
			g_hudCockpitResourceDescriptors[g_players[g_localPlayer]
								.viewState
								.hudStateLive]
				.displayName,
			XVT_COCKPIT_ALIGN_CENTER);
#endif
		FlightText_DrawStringCentered(
			g_hudCockpitResourceDescriptors[g_players[g_localPlayer]
								.viewState
								.hudStateLive]
				.displayName);
	}
#ifdef XVT_MODERN
	XvtCockpit_RefreshInstruments(playerIdx);
#endif
}

/* Reads entryCount entries of lfdName's .LFD file, in the resolution's cockpit
 * folder, to g_hudCockpitResourceWriteCursor and advances it, storing where
 * each entry's data starts in outEntries. Each entry is a 16-byte header (type
 * tag, name, data size) and its data; a PLTT (palette) entry has every byte
 * divided by 4 and its pointer moved 2 bytes in. Does not check the space at
 * the cursor or the sizes in the file. The modern build registers the entries
 * for its renderer. */
// FUNCTION: XVT 0x441550
void Hud_LoadCockpitLfdEntries(const char *lfdName, uint8_t **outEntries,
			       unsigned int entryCount)
{
	XvtFile *stream;
	uint8_t **outputEntry;
	int16_t isPalette;
	uint16_t paletteTagIndex;
	size_t dataSize;
	uint16_t entryIndex;
	LfdEntryHeader header;

	strcpy(g_hudCockpitResourcePath, g_hudCockpitResolutionDirectory);
	strcat(g_hudCockpitResourcePath, lfdName);
	strcat(g_hudCockpitResourcePath, ".LFD");
	FeDiskIo_OpenGlobalStream(g_hudCockpitResourcePath, "rb", 1, 0);
	stream = g_stream;
	entryIndex = 0;
	while (entryIndex < entryCount) {
		outputEntry = &outEntries[entryIndex];
		*outputEntry = g_hudCockpitResourceWriteCursor;
		isPalette = 1;
		FeDiskIo_ReadWithRetryPrompt(&header, sizeof(header), 1,
					     stream);
		paletteTagIndex = 0;
		while (paletteTagIndex < 4) {
			if (g_lfdPaletteResourceTypeTag[paletteTagIndex] !=
			    header.resourceType[paletteTagIndex]) {
				isPalette = 0;
			}
			++paletteTagIndex;
		}
		dataSize = header.dataSize;
		FeDiskIo_ReadWithRetryPrompt(g_hudCockpitResourceWriteCursor,
					     dataSize, 1, stream);
		if (isPalette == 0) {
			g_hudCockpitResourceWriteCursor += dataSize;
		} else {
			while (dataSize-- != 0) {
				*g_hudCockpitResourceWriteCursor >>= 2;
				++g_hudCockpitResourceWriteCursor;
			}
		}
		if (isPalette != 0) {
			*outputEntry += 2;
		}
		++entryIndex;
	}
	FeDiskIo_CloseGlobalStream(0);
#ifdef XVT_MODERN
	XvtRenderAssets_RegisterLfd(g_hudCockpitResourcePath, outEntries);
#endif
}

/* Loads the cockpit images: for each of the 28 views whose descriptor has
 * resourceRef 1 it allocates a memory handle the size of the view's .LFD file
 * (a failure is fatal) and reads the file's three entries into it; a second
 * pass reads every one again after all are allocated. Other views get handle 0.
 * Sets g_hudCockpitResourcesLoaded to 1. modelIndex is ignored. */
// FUNCTION: XVT 0x4416F0
void Hud_LoadCockpitSpriteResources(unsigned int modelIndex)
{
	uint16_t resourceIndex;

	(void)modelIndex;

	for (resourceIndex = 0;
	     resourceIndex < (uint16_t)(sizeof(g_hudCockpitResources) /
					sizeof(g_hudCockpitResources[0]));
	     ++resourceIndex) {
		uint16_t memoryHandle;
		size_t fileSize;

		g_hudCockpitResources[resourceIndex].memoryHandle = 0;
		if (g_hudCockpitResourceDescriptors[resourceIndex]
			    .resourceRef == 1) {
			strcpy(g_hudCockpitResourcePath,
			       g_hudCockpitResolutionDirectory);
			strcat(g_hudCockpitResourcePath,
			       g_hudCockpitResourceDescriptors[resourceIndex]
				       .lfdName);
			strcat(g_hudCockpitResourcePath, ".LFD");
			FeDiskIo_OpenGlobalStream(g_hudCockpitResourcePath,
						  "rb", 1, 0);
			if (g_stream != NULL) {
#ifdef XVT_MODERN
				File_RawSeek((XvtFile *)g_stream, 0, SEEK_END);
				fileSize = (size_t)File_RawTell(
					(XvtFile *)g_stream);
#else
				fileSize = (size_t)_filelength(
					(_fileno)((XvtFile *)g_stream));
#endif
				FeDiskIo_CloseGlobalStream(0);
				FeDiskIo_UnlockGlobalBuffers();
				memoryHandle = Memory_AllocHandle(fileSize, 0);
				if (memoryHandle == 0) {
					FeDiskIo_FatalError(
						FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
				}
				FeDiskIo_LockGlobalBuffers();
				if (memoryHandle != 0) {
					g_hudCockpitResources[resourceIndex]
						.memoryHandle =
						(int16_t)memoryHandle;
					g_hudCockpitResourceWriteCursor =
						(uint8_t *)Memory_LockHandle(
							memoryHandle);
					Memory_UnlockHandle(memoryHandle);
					Hud_LoadCockpitLfdEntries(
						g_hudCockpitResourceDescriptors
							[resourceIndex]
								.lfdName,
						g_hudCockpitResources
							[resourceIndex]
								.entries,
						sizeof(g_hudCockpitResources
							       [resourceIndex]
								       .entries) /
							sizeof(g_hudCockpitResources[resourceIndex]
								       .entries[0]));
				}
			}
		}
	}

	for (resourceIndex = 0;
	     resourceIndex < (uint16_t)(sizeof(g_hudCockpitResources) /
					sizeof(g_hudCockpitResources[0]));
	     ++resourceIndex) {
		uint16_t memoryHandle;

		if (g_hudCockpitResourceDescriptors[resourceIndex]
			    .resourceRef == 1) {
			strcpy(g_hudCockpitResourcePath,
			       g_hudCockpitResolutionDirectory);
			strcat(g_hudCockpitResourcePath,
			       g_hudCockpitResourceDescriptors[resourceIndex]
				       .lfdName);
			strcat(g_hudCockpitResourcePath, ".LFD");
			memoryHandle =
				(uint16_t)g_hudCockpitResources[resourceIndex]
					.memoryHandle;
			if (memoryHandle != 0) {
				g_hudCockpitResourceWriteCursor =
					(uint8_t *)Memory_LockHandle(
						memoryHandle);
				Memory_UnlockHandle(memoryHandle);
				Hud_LoadCockpitLfdEntries(
					g_hudCockpitResourceDescriptors
						[resourceIndex]
							.lfdName,
					g_hudCockpitResources[resourceIndex]
						.entries,
					sizeof(g_hudCockpitResources
						       [resourceIndex]
							       .entries) /
						sizeof(g_hudCockpitResources
							       [resourceIndex]
								       .entries[0]));
			}
		}
	}
	g_hudCockpitResourcesLoaded = 1;
}

/* Calls g_flightRenderTransitionHook, sets g_hudPanelSetId to 0 and reads the
 * .INT file again through g_hudCockpitResourcePath, passing that path as the
 * base name: Hud_LoadCockpitInterfaceFile copies it onto itself and adds
 * ".INT" after the extension of the last file opened, so the name gets a
 * second extension. Nothing calls this. */
// FUNCTION: XVT 0x4419B0
void Hud_ReloadCockpitInterfaceFile(void)
{
	g_flightRenderTransitionHook();
	g_hudPanelSetId = 0;
	Hud_LoadCockpitInterfaceFile(g_hudCockpitResourcePath);
}

/* Draws the MFD pages that need it: first each page closing
 * (MFD_PAGE_STATE_CLOSING) once more, which then becomes closed; then every
 * page whose state is not closed. Each draw records the page's state in its
 * element of g_hudElementStateCache (scoreboard 134, goals 132, message log
 * 117, damage 133 when Damage_DisplayMfdPage returns nonzero, friendly craft
 * 130, hostile craft and map help 131). */
// FUNCTION: XVT 0x4419D0
void Hud_UpdateMfdPages(void)
{
	uint16_t page;
	int16_t pageState;
	unsigned int pageIndex;

	for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT; ++page) {
		pageIndex = page;
		pageState = g_mfdPageStates[pageIndex];
		switch (pageIndex) {
		case MFD_PAGE_SCOREBOARD:
			if (pageState == MFD_PAGE_STATE_CLOSING) {
				Mfd_DrawMissionScoreboardPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_SCOREBOARD_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_GOALS:
			if (pageState == MFD_PAGE_STATE_CLOSING) {
				Mfd_DrawMissionGoalsPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_GOALS_ELEMENT] = pageState;
			}
			break;
		case MFD_PAGE_MESSAGE_LOG:
			if (pageState == MFD_PAGE_STATE_CLOSING) {
				Mfd_DrawMessageLogPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_MESSAGE_LOG_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_DAMAGE:
			if (pageState == MFD_PAGE_STATE_CLOSING) {
				Damage_DisplayMfdPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_DAMAGE_ELEMENT] = pageState;
			}
			break;
		case MFD_PAGE_HOSTILE_CRAFT:
			if (pageState == MFD_PAGE_STATE_CLOSING) {
				Mfd_DrawCraftListPage(1);
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_FRIENDLY_CRAFT:
			if (pageState == MFD_PAGE_STATE_CLOSING) {
				Mfd_DrawCraftListPage(0);
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_CRAFT_LIST_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_MAP_HELP:
			if (pageState == MFD_PAGE_STATE_CLOSING) {
				Mfd_DrawMapHelpPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						pageState;
			}
			break;
		default:
			break;
		}
		if (pageState == MFD_PAGE_STATE_CLOSING) {
			g_mfdPageStates[pageIndex] = MFD_PAGE_STATE_CLOSED;
		}
	}

	for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT; ++page) {
		pageIndex = page;
		pageState = g_mfdPageStates[pageIndex];
		switch (pageIndex) {
		case MFD_PAGE_SCOREBOARD:
			if (pageState) {
				Mfd_DrawMissionScoreboardPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_SCOREBOARD_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_GOALS:
			if (pageState) {
				Mfd_DrawMissionGoalsPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_GOALS_ELEMENT] = pageState;
			}
			break;
		case MFD_PAGE_MESSAGE_LOG:
			if (pageState) {
				Mfd_DrawMessageLogPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_MESSAGE_LOG_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_DAMAGE:
			if (pageState) {
				if (Damage_DisplayMfdPage() != 0) {
					g_hudElementStateCache
						[g_hudInstrumentSetBaseIndex +
						 HUD_MFD_DAMAGE_ELEMENT] =
							pageState;
				}
			}
			break;
		case MFD_PAGE_HOSTILE_CRAFT:
			if (pageState) {
				Mfd_DrawCraftListPage(1);
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_FRIENDLY_CRAFT:
			if (pageState) {
				Mfd_DrawCraftListPage(0);
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_CRAFT_LIST_ELEMENT] =
						pageState;
			}
			break;
		case MFD_PAGE_MAP_HELP:
			if (pageState) {
				Mfd_DrawMapHelpPage();
				g_hudElementStateCache
					[g_hudInstrumentSetBaseIndex +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						pageState;
			}
			break;
		default:
			break;
		}
	}
}

/* Copies each open MFD page from g_flightOffscreenBuffer onto the flight
 * surface, skipping pixels of g_flightTransparentColorIndex, with the
 * g_mfd...Blit rectangles: the scoreboard, goals and hostile craft pages to the
 * map element while the map camera is on, else to their own; damage only
 * outside the map, map help only in it, friendly craft always. In the HUD-only
 * set with HUD feature 8 it also copies each warhead count drawn by
 * Hud_OutputWarheadCount to layout 27 plus the launcher. The modern build
 * latches the pages for its renderer. */
// FUNCTION: XVT 0x441C30
void Hud_BlitSoftwareMfdPages(void)
{
	int16_t pageState;
	uint16_t page;
	CraftData *craft;
	int modelIndex;
	uint16_t launcherIndex;
	uint16_t launcherCount;
	int layoutIndex;
	int16_t selector;

	for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT; ++page) {
		pageState = g_mfdPageStates[page];
		switch (page) {
		case MFD_PAGE_SCOREBOARD:
			if (pageState != MFD_PAGE_STATE_CLOSED) {
				if (g_players[g_localPlayer].mapCameraState !=
				    0) {
					FlightSw_BlitRectToFlightSurface(
						g_flightOffscreenBuffer,
						g_flightTransparentColorIndex,
						g_mfdMapBlitSourceX,
						g_mfdMapBlitSourceY,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.y,
						g_mfdMapBlitWidth,
						g_mfdMapBlitHeight,
						g_flightBytesPerPixel *
							g_screenWidth);
				} else {
					FlightSw_BlitRectToFlightSurface(
						g_flightOffscreenBuffer,
						g_flightTransparentColorIndex,
						g_mfdMissionScoreboardBlitSourceX,
						g_mfdMissionScoreboardBlitSourceY,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_SCOREBOARD_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_SCOREBOARD_ELEMENT]
								.y,
						g_mfdMissionScoreboardBlitWidth,
						g_mfdMissionScoreboardBlitHeight,
						g_flightBytesPerPixel *
							g_screenWidth);
				}
			}
			break;
		case MFD_PAGE_GOALS:
			if (pageState != MFD_PAGE_STATE_CLOSED) {
				if (g_players[g_localPlayer].mapCameraState !=
				    0) {
					FlightSw_BlitRectToFlightSurface(
						g_flightOffscreenBuffer,
						g_flightTransparentColorIndex,
						g_mfdMapBlitSourceX,
						g_mfdMapBlitSourceY,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.y,
						g_mfdMapBlitWidth,
						g_mfdMapBlitHeight,
						g_flightBytesPerPixel *
							g_screenWidth);
				} else {
					FlightSw_BlitRectToFlightSurface(
						g_flightOffscreenBuffer,
						g_flightTransparentColorIndex,
						g_mfdGoalsBlitSourceX,
						g_mfdGoalsBlitSourceY,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_GOALS_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_GOALS_ELEMENT]
								.y,
						g_mfdGoalsBlitWidth,
						g_mfdGoalsBlitHeight,
						g_flightBytesPerPixel *
							g_screenWidth);
				}
			}
			break;
		case MFD_PAGE_DAMAGE:
			if (pageState != MFD_PAGE_STATE_CLOSED &&
			    g_players[g_localPlayer].mapCameraState == 0) {
				FlightSw_BlitRectToFlightSurface(
					g_flightOffscreenBuffer,
					g_flightTransparentColorIndex,
					g_mfdDamageBlitSourceX,
					g_mfdDamageBlitSourceY,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 HUD_MFD_DAMAGE_ELEMENT]
							.x,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 HUD_MFD_DAMAGE_ELEMENT]
							.y,
					g_mfdDamageBlitWidth,
					g_mfdDamageBlitHeight,
					g_flightBytesPerPixel * g_screenWidth);
			}
			break;
		case MFD_PAGE_HOSTILE_CRAFT:
			if (pageState != MFD_PAGE_STATE_CLOSED) {
				if (g_players[g_localPlayer].mapCameraState !=
				    0) {
					FlightSw_BlitRectToFlightSurface(
						g_flightOffscreenBuffer,
						g_flightTransparentColorIndex,
						g_mfdMapBlitSourceX,
						g_mfdMapBlitSourceY,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.y,
						g_mfdMapBlitWidth,
						g_mfdMapBlitHeight,
						g_flightBytesPerPixel *
							g_screenWidth);
				} else {
					FlightSw_BlitRectToFlightSurface(
						g_flightOffscreenBuffer,
						g_flightTransparentColorIndex,
						g_mfdCraftListBlitSourceX,
						g_mfdCraftListBlitSourceY,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_CRAFT_LIST_ELEMENT]
								.x,
						g_hudElementLayouts
							[g_hudInstrumentSetBaseIndex +
							 HUD_MFD_CRAFT_LIST_ELEMENT]
								.y,
						g_mfdCraftListBlitWidth,
						g_mfdCraftListBlitHeight,
						g_flightBytesPerPixel *
							g_screenWidth);
				}
			}
			break;
		case MFD_PAGE_FRIENDLY_CRAFT:
			if (pageState != MFD_PAGE_STATE_CLOSED) {
				FlightSw_BlitRectToFlightSurface(
					g_flightOffscreenBuffer,
					g_flightTransparentColorIndex,
					g_mfdCraftListBlitSourceX,
					g_mfdCraftListBlitSourceY,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 HUD_MFD_CRAFT_LIST_ELEMENT]
							.x,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 HUD_MFD_CRAFT_LIST_ELEMENT]
							.y,
					g_mfdCraftListBlitWidth,
					g_mfdCraftListBlitHeight,
					g_flightBytesPerPixel * g_screenWidth);
			}
			break;
		case MFD_PAGE_MAP_HELP:
			if (pageState != MFD_PAGE_STATE_CLOSED &&
			    g_players[g_localPlayer].mapCameraState != 0) {
				FlightSw_BlitRectToFlightSurface(
					g_flightOffscreenBuffer,
					g_flightTransparentColorIndex,
					g_mfdMapBlitSourceX,
					g_mfdMapBlitSourceY,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
							.x,
					g_hudElementLayouts
						[g_hudInstrumentSetBaseIndex +
						 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
							.y,
					g_mfdMapBlitWidth, g_mfdMapBlitHeight,
					g_flightBytesPerPixel * g_screenWidth);
			}
			break;
		default:
			break;
		}
	}

	if (g_hudInstrumentSetBaseIndex ==
	    HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX) {
		craft = g_objectTable[g_players[g_localPlayer].objectIndex]
				.mobj->pCraft;
		if ((craft->damageStats.activeHudFeatureMask & 8) != 0) {
			modelIndex = craft->modelIndex;
			FlightText_SetFontTier(0);
			launcherIndex = 0;
			launcherCount = g_modelDefs[modelIndex]
						.warheadLauncherSlotCount[1] +
					g_modelDefs[modelIndex]
						.warheadLauncherSlotCount[0];
			while (launcherIndex < launcherCount) {
				layoutIndex = g_hudInstrumentSetBaseIndex +
					      launcherIndex;
				selector = g_hudElementLayouts[layoutIndex + 27]
						   .selector;
				if (selector != 0) {
#ifdef XVT_MODERN
					XvtCockpit_LatchLauncher(
						launcherIndex,
						g_hudElementLayouts
							[layoutIndex + 27]
								.x,
						g_hudElementLayouts
							[layoutIndex + 27]
								.y,
						(g_flightFontDigitWidth + 1) *
							selector,
						g_flightFontLineHeight);
#endif
					FlightSw_BlitRectToFlightSurface(
						g_flightOffscreenBuffer,
						g_flightTransparentColorIndex,
						(g_flightFontDigitWidth + 1) *
								launcherIndex +
							2,
						2,
						g_hudElementLayouts
							[layoutIndex + 27]
								.x,
						g_hudElementLayouts
							[layoutIndex + 27]
								.y,
						(g_flightFontDigitWidth + 1) *
							selector,
						g_flightFontLineHeight,
						g_flightBytesPerPixel *
							g_screenWidth);
				}
				++launcherIndex;
			}
		}
	}
#ifdef XVT_MODERN
	XvtCockpit_LatchPages();
#endif
}

/* Renders the local player's target into a small viewport at screenX, screenY:
 * copies the current set's inset span mask into the viewport when
 * refreshSpanMask is set, points the camera at the target (Hud_PointCamera),
 * draws the target model (craft lit and with damage billboards, projectiles, or
 * mines to debris), the visible projectiles fired by the target, and outside
 * the map those fired by the player, and visible explosions, on background
 * color 48. With targetBoxEnabled, a 4-pixel box marks the selected component
 * of a craft target that is not a fighter. Restores the camera position,
 * g_projOffsetY, g_renderObjectRef and the viewport, and sets
 * g_flightBackgroundColorIndex to the transparent color. Writes the scene
 * globals it uses, among them g_camRelWorldX, g_viewSpaceX, g_rotatedX and
 * g_curCraft. */
// FUNCTION: XVT 0x4422C0
void Hud_Update3DCrt(uint16_t screenX, uint16_t screenY, uint16_t width,
		     uint16_t height, int16_t refreshSpanMask)
{
	enum {
		LOW_RESOLUTION_MASK_SIZE = 200,
		HIGH_RESOLUTION_MASK_SIZE = 480,
		TARGET_BOX_RENDER_FLAG = 0x0200,
		TARGET_CROSS_RENDER_FLAG = 0x0400,
		CRT_RENDER_COLOR = 48,
		COMPONENT_MARKER_SIZE = 4,
		COMPONENT_MARKER_HALF_SIZE = COMPONENT_MARKER_SIZE / 2,
		COMPONENT_MARKER_COLOR = 0xCE,
	};

	int savedProjOffsetY;
	int savedCameraWorldX;
	int savedCameraWorldY;
	int savedCameraWorldZ;
	uint16_t savedRenderObjectRef;
	unsigned int baseOffset;
	int componentRelX;
	int componentRelY;
	int componentRelZ;
	uint16_t targetObjectIdx;
	ObjectRecord *targetObject;
	MobileObject *targetMobileObject;
	uint16_t objectIndex;

#ifdef XVT_MODERN
	XvtRenderDraw_Scope(XVT_SCOPE_CRT);
#endif
	savedProjOffsetY = g_projOffsetY;
	savedCameraWorldX = g_players[g_localPlayer].viewState.cameraWorldX;
	savedRenderObjectRef = g_renderObjectRef;
	savedCameraWorldY = g_players[g_localPlayer].viewState.cameraWorldY;
	savedCameraWorldZ = g_players[g_localPlayer].viewState.cameraWorldZ;
	g_projOffsetY = 0;
	baseOffset =
		(unsigned int)g_flightComputePixelOffsetFn(screenX, screenY);
	PushFlightViewport(width, height, refreshSpanMask, baseOffset);

	if (refreshSpanMask != 0) {
		unsigned int hudInstrumentBaseIndex;
		uint8_t *destinationMask;
		const uint8_t *sourceMask;
		uint16_t maskIndex;

		destinationMask = &g_flightAuxBuffer[g_viewportSpanMaskOffset];
		hudInstrumentBaseIndex = g_hudInstrumentSetBaseIndex;
		if (hudInstrumentBaseIndex !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			if (hudInstrumentBaseIndex ==
			    HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX) {
				sourceMask = g_hudOnlyViewInsetSpanMask;
			} else if (hudInstrumentBaseIndex ==
				   HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX) {
				sourceMask = g_hudCraftListInsetSpanMask;
			} else {
				sourceMask = g_hudCockpitInsetSpanMask;
			}
		} else {
			sourceMask = g_hudCockpitInsetSpanMask;
		}
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
			for (maskIndex = LOW_RESOLUTION_MASK_SIZE;
			     maskIndex != 0; --maskIndex) {
				destinationMask[maskIndex - 1] =
					sourceMask[maskIndex - 1];
			}
		} else {
			for (maskIndex = HIGH_RESOLUTION_MASK_SIZE;
			     maskIndex != 0; --maskIndex) {
				destinationMask[maskIndex - 1] =
					sourceMask[maskIndex - 1];
			}
		}
	}

	if (g_useHardware3D != 0) {
		std3D_FillZBufferFromViewportMask();
	}
	Hud_PointCamera(g_players[g_localPlayer].currentTargetObjectIdx, 1,
			g_localPlayer);
#ifdef XVT_MODERN
	XvtRenderCapture_Crt(screenX, screenY, width, height, refreshSpanMask);
#endif
	if (g_players[g_localPlayer].targetBoxEnabled != 0) {
		g_renderObjectRef |= TARGET_BOX_RENDER_FLAG;
	} else {
		g_renderObjectRef |= TARGET_CROSS_RENDER_FLAG;
	}
	g_flightBackgroundColorIndex = CRT_RENDER_COLOR;
	RenderScene_Initialize(1);
	g_sceneBillboardQueueCount = 0;
	g_camRelWorldX =
		g_worldLocX - g_players[g_localPlayer].viewState.cameraWorldX;
	g_camRelWorldY =
		g_worldLocY - g_players[g_localPlayer].viewState.cameraWorldY;
	g_camRelWorldZ =
		g_worldLocZ - g_players[g_localPlayer].viewState.cameraWorldZ;

	if (g_players[g_localPlayer].targetBoxEnabled != 0) {
		targetObjectIdx = (uint16_t)g_players[g_localPlayer]
					  .currentTargetObjectIdx;
		if (targetObjectIdx < g_activeRegionCraftObjectSlotEnd) {
			g_rotatedX = ModelMesh_GetComponentFocusX(
				g_objectTable[(uint16_t)g_players[g_localPlayer]
						      .currentTargetObjectIdx]
					.objectType,
				(uint16_t)g_players[g_localPlayer]
					.selectedTargetComponent);
			g_rotatedY = ModelMesh_GetComponentFocusZ(
				g_objectTable[(uint16_t)g_players[g_localPlayer]
						      .currentTargetObjectIdx]
					.objectType,
				(uint16_t)g_players[g_localPlayer]
					.selectedTargetComponent);
			g_rotatedZ = -ModelMesh_GetComponentFocusY(
				g_objectTable[(uint16_t)g_players[g_localPlayer]
						      .currentTargetObjectIdx]
					.objectType,
				(uint16_t)g_players[g_localPlayer]
					.selectedTargetComponent);
			pai_RotateLocalVectorToWorldScratch(
				&g_objectTable
					[(uint16_t)g_players[g_localPlayer]
						 .currentTargetObjectIdx],
				g_rotatedX, g_rotatedY, g_rotatedZ);
			componentRelX =
				g_rotatedX + g_worldLocX -
				g_players[g_localPlayer].viewState.cameraWorldX;
			componentRelY =
				g_rotatedY + g_worldLocY -
				g_players[g_localPlayer].viewState.cameraWorldY;
			componentRelZ =
				g_rotatedZ + g_worldLocZ -
				g_players[g_localPlayer].viewState.cameraWorldZ;
		}
	}

	g_viewSpaceX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY,
					      g_camRelWorldZ);
	g_viewSpaceY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY,
					      g_camRelWorldZ);
	g_viewSpaceDepth = TRANSFM2_CamMatDotRow2(
		g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
	targetObjectIdx =
		(uint16_t)g_players[g_localPlayer].currentTargetObjectIdx;
	targetObject = &g_objectTable[targetObjectIdx];
	targetMobileObject = targetObject->mobj;
	if (targetMobileObject != NULL) {
		switch (targetObject->genusId) {
		case CRAFT_GENUS_STARFIGHTER:
		case CRAFT_GENUS_TRANSPORT:
		case CRAFT_GENUS_UTILITY_VEHICLE:
		case CRAFT_GENUS_FREIGHTER:
		case CRAFT_GENUS_STARSHIP:
		case CRAFT_GENUS_PLATFORM:
			g_curCraft = targetMobileObject->pCraft;
			FVIEW_SetObjectTransform(
				targetObject->roll, targetObject->pitch,
				targetObject->yaw, 0,
				&g_objectTable
					[(uint16_t)g_players[g_localPlayer]
						 .currentTargetObjectIdx]);
			FlightLight_SetupObjectLightingByIndex(
				(uint16_t)g_players[g_localPlayer]
					.currentTargetObjectIdx);
			Damage_QueueCraftBillboards(
				g_players[g_localPlayer]
					.currentTargetObjectIdx);
			RenderScene_DrawObjectModel(
				&g_objectTable
					[(uint16_t)g_players[g_localPlayer]
						 .currentTargetObjectIdx]);
			break;
		case CRAFT_GENUS_PLAYER_PROJECTILE:
		case CRAFT_GENUS_OTHER_PROJECTILE:
			FVIEW_SetObjectTransform(
				targetObject->roll, targetObject->pitch,
				targetObject->yaw, 0,
				&g_objectTable
					[(uint16_t)g_players[g_localPlayer]
						 .currentTargetObjectIdx]);
			SceneBillboard_DrawRollAlignedObjectModel(
				g_players[g_localPlayer]
					.currentTargetObjectIdx);
			break;
		default:
			break;
		}
	} else if (targetObject->genusId >= CRAFT_GENUS_MINE &&
		   targetObject->genusId <= CRAFT_GENUS_SMALL_DEBRIS) {
		FVIEW_SetObjectTransform(targetObject->roll,
					 targetObject->pitch, targetObject->yaw,
					 0, NULL);
		g_transformLightDirectionToObjectSpace = 1;
		RenderNonCraftSceneObject(targetObjectIdx);
	}

	for (objectIndex = 0; objectIndex < g_regionMainObjectSlotEnd;
	     ++objectIndex) {
		int objectTableIndex;
		ObjectRecord *object;
		int genusId;
		uint16_t objectType;

		if (objectIndex == g_localTransientSlotStart &&
		    (g_debrisEnabled == 0 ||
		     g_flightMissionState.provingGroundsModeActive != 0)) {
			objectIndex = (uint16_t)g_localDebrisSlotEnd;
			if (objectIndex == g_regionMainObjectSlotEnd) {
				break;
			}
		}
		if (g_players[g_localPlayer].viewState.cameraFocusObjIdx !=
			    objectIndex ||
		    g_players[g_localPlayer].viewState.externalCameraActive !=
			    0 ||
		    g_replayViewMode != 0) {
			objectTableIndex = objectIndex;
			object = &g_objectTable[objectTableIndex];
			objectType = g_objectTable[objectTableIndex].objectType;
			if (objectType != 0) {
				g_currentObjectBoundsExtent =
					g_objectTypeTable[objectType]
						.maxBoundsExtent;
				genusId = object->genusId;
				if (genusId >= CRAFT_GENUS_PLAYER_PROJECTILE) {
					if (object->genusId <=
					    CRAFT_GENUS_OTHER_PROJECTILE) {
						if (g_players[g_localPlayer]
							    .mapCameraState !=
						    0) {
							if (object->mobj->sourceObjIdx !=
								    g_players[g_localPlayer]
									    .currentTargetObjectIdx ||
							    FlightView_ProjectAndTestSphereVisible(
								    objectIndex,
								    g_currentObjectBoundsExtent) ==
								    0) {
								continue;
							}
						} else if (
							(g_players[g_localPlayer]
									 .currentTargetObjectIdx !=
								 object->mobj
									 ->sourceObjIdx &&
							 g_players[g_localPlayer]
									 .objectIndex !=
								 object->mobj
									 ->sourceObjIdx) ||
							FlightView_ProjectAndTestSphereVisible(
								objectIndex,
								g_currentObjectBoundsExtent) ==
								0) {
							continue;
						}
						FVIEW_SetObjectTransform(
							g_objectTable
								[objectTableIndex]
									.roll,
							g_objectTable
								[objectTableIndex]
									.pitch,
							g_objectTable
								[objectTableIndex]
									.yaw,
							0,
							&g_objectTable
								[objectTableIndex]);
						SceneBillboard_DrawRollAlignedObjectModel(
							objectIndex);
					} else if (
						genusId ==
							CRAFT_GENUS_EXPLOSION &&
						FlightView_ProjectAndTestSphereVisible(
							objectIndex,
							g_currentObjectBoundsExtent) !=
							0) {
						FVIEW_SetObjectTransform(
							g_objectTable
								[objectTableIndex]
									.roll,
							g_objectTable
								[objectTableIndex]
									.pitch,
							g_objectTable
								[objectTableIndex]
									.yaw,
							0,
							&g_objectTable
								[objectTableIndex]);
						SceneBillboard_DrawOrQueueObject(
							objectIndex);
					}
				}
			}
		}
	}

	sw3d_DrawVisibleFacesToSurface();
	if (g_sceneBillboardQueueCount != 0) {
		g_flightSwRotSpriteCoeffCacheValid = 0;
		SceneBillboard_RenderQueuedTextured(0);
		g_flightSwRotSpriteCoeffCacheValid = 0;
	}
	if (g_players[g_localPlayer].targetBoxEnabled != 0) {
		targetObjectIdx = (uint16_t)g_players[g_localPlayer]
					  .currentTargetObjectIdx;
		if (targetObjectIdx < g_activeRegionCraftObjectSlotEnd &&
		    g_objectTable[targetObjectIdx].genusId !=
			    CRAFT_GENUS_STARFIGHTER) {
			int markerX;
			int markerY;

			g_viewSpaceX = TRANSFM2_CamMatDotRow0(
				componentRelX, componentRelY, componentRelZ);
#ifdef XVT_MODERN
			XvtRenderCapture_CrtMarker(componentRelX, componentRelY,
						   componentRelZ);
#endif
			g_viewSpaceY = TRANSFM2_CamMatDotRow1(
				componentRelX, componentRelY, componentRelZ);
			g_viewSpaceDepth = TRANSFM2_CamMatDotRow2(
				componentRelX, componentRelY, componentRelZ);
			markerX = TRANSFM2_ProjectScreenX(g_viewSpaceX,
							  g_viewSpaceDepth);
			markerY = TRANSFM2_ProjectScreenY(g_viewSpaceY,
							  g_viewSpaceDepth);
			Hud_DrawComponentMarkerBox(
				markerX - COMPONENT_MARKER_HALF_SIZE,
				markerY - COMPONENT_MARKER_HALF_SIZE,
				COMPONENT_MARKER_SIZE, COMPONENT_MARKER_SIZE,
				COMPONENT_MARKER_COLOR);
		}
	}

	RenderScene_UnlockBuffers();
	g_flightBackgroundColorIndex = g_flightTransparentColorIndex;
	PopFlightViewport();
	g_renderObjectRef = savedRenderObjectRef;
	g_players[g_localPlayer].viewState.cameraWorldX = savedCameraWorldX;
	g_players[g_localPlayer].viewState.cameraWorldY = savedCameraWorldY;
	g_players[g_localPlayer].viewState.cameraWorldZ = savedCameraWorldZ;
	g_projOffsetY = savedProjOffsetY;
#ifdef XVT_MODERN
	XvtRenderDraw_Scope(XVT_SCOPE_COCKPIT);
#endif
}

/* Draws box corners at depth 1 through Hud_DrawDepthTestedBoxCorners. */
// FUNCTION: XVT 0x442BC0
void Hud_DrawComponentMarkerBox(int x, int y, int width, int height,
				uint8_t colorIdx)
{
	enum { COMPONENT_MARKER_DEPTH = 1 };

	Hud_DrawDepthTestedBoxCorners(x, y, width, height, colorIdx,
				      COMPONENT_MARKER_DEPTH);
}

/* Places playerIdx's camera to look at targetIdx, an object or mission point:
 * turns it toward the target from the map camera or the player's craft
 * (FVIEW_BuildCameraOrient), then sets viewState.cameraWorldX, Y and Z back
 * from the target along the view by a distance in proportion to its size, the
 * mean of its model's two largest bounds or its type's maxBoundsExtent, divided
 * by the inset layout's colorIndexOrWidgetParam when useHudLayoutScale is set,
 * else by 60, 100 or 144 for the player's resolution, and a quarter more at
 * 640x480. Writes g_worldLocX, Y and Z, the camera matrix and the trig2_ctop
 * results. */
// FUNCTION: XVT 0x442BF0
void Hud_PointCamera(uint16_t targetIdx, int16_t useHudLayoutScale,
		     int playerIdx)
{
	enum {
		CAMERA_SCALE_LIMIT = 0x3FFF,
		CAMERA_AIM_CENTER_Q16 = 0x4000,
		CAMERA_DIVISOR_LOW = 60,
		CAMERA_DIVISOR_HIGH = 100,
		CAMERA_DIVISOR_DEFAULT = 144,
		CAMERA_OFFSET_COUNT = 3,
	};

	int deltaX;
	int deltaY;
	int deltaZ;
	int scaledX;
	int scaledY;
	int scaledZ;
	int cameraX;
	int cameraY;
	int cameraZ;
	int maxExtent;
	unsigned int scale;
	uint16_t scaleShift;
	uint16_t cameraDivisor;
	uint16_t resolutionMode;
	ModelIndex modelIndex;

	Mission_ResolveObjectOrMissionPointWorldLoc(targetIdx, 0);
	if (g_players[playerIdx].mapCameraState != 0) {
		deltaX = (int32_t)((uint32_t)g_worldLocX -
				   (uint32_t)g_players[playerIdx]
					   .viewState.cameraWorldX);
		deltaY = (int32_t)((uint32_t)g_worldLocY -
				   (uint32_t)g_players[playerIdx]
					   .viewState.cameraWorldY);
		deltaZ = (int32_t)((uint32_t)g_worldLocZ -
				   (uint32_t)g_players[playerIdx]
					   .viewState.cameraWorldZ);
	} else {
		ObjectRecord *playerObject =
			&g_objectTable[g_players[playerIdx].objectIndex];
		deltaX = (int32_t)((uint32_t)g_worldLocX -
				   (uint32_t)playerObject->world_x);
		deltaY = (int32_t)((uint32_t)g_worldLocY -
				   (uint32_t)playerObject->world_y);
		deltaZ = (int32_t)((uint32_t)g_worldLocZ -
				   (uint32_t)playerObject->world_z);
	}
	scaledX = (int32_t)((uint32_t)deltaX * 2u);
	scaledY = (int32_t)((uint32_t)deltaY * 2u);
	{
		int16_t highY = (int16_t)(scaledY >> 16);
		int16_t highX;
		int16_t highZ;
		scaledZ = (int32_t)((uint32_t)deltaZ * 2u);
		highX = (int16_t)(scaledX >> 16);
		highZ = (int16_t)(scaledZ >> 16);
		if ((highX & 0x8000) != 0) {
			highX = (int16_t)-highX;
		}
		if ((highY & 0x8000) != 0) {
			highY = (int16_t)-highY;
		}
		if ((highZ & 0x8000) != 0) {
			highZ = (int16_t)-highZ;
		}
		do {
			highX = (int16_t)((uint16_t)highX >> 1);
			scaledX >>= 1;
			scaledY >>= 1;
			scaledZ >>= 1;
			highY = (int16_t)((uint16_t)highY >> 1);
			highZ = (int16_t)((uint16_t)highZ >> 1);
		} while (highX != 0 || highY != 0 || highZ != 0);
	}
	scaledX >>= 1;
	scaledY >>= 1;
	scaledZ >>= 1;

	if (g_players[playerIdx].mapCameraState != 0) {
		FVIEW_BuildCameraOrient(
			g_players[playerIdx].viewState.viewRoll,
			g_players[playerIdx].viewState.viewPitch,
			g_players[playerIdx].viewState.viewYaw, 0, 0, 0, NULL);
		cameraX = Math_Dot3Q15Wrapped(
			(int16_t)scaledX, (int16_t)scaledY, (int16_t)scaledZ,
			g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z);
		cameraY = Math_Dot3Q15Wrapped(
			(int16_t)scaledX, (int16_t)scaledY, (int16_t)scaledZ,
			g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z);
		cameraZ = Math_Dot3Q15Wrapped(
			(int16_t)scaledX, (int16_t)scaledY, (int16_t)scaledZ,
			g_camMatR1_X, g_camMatR1_Y, g_camMatR1_Z);
		trig2_ctop(cameraX, cameraY, cameraZ);
		FVIEW_BuildCameraOrient(
			g_players[playerIdx].viewState.viewRoll,
			g_players[playerIdx].viewState.viewPitch,
			g_players[playerIdx].viewState.viewYaw, 0,
			(int16_t)(CAMERA_AIM_CENTER_Q16 - trig2_pitch),
			trig2_xyangle, NULL);
	} else {
		ObjectRecord *playerObject =
			&g_objectTable[g_players[playerIdx].objectIndex];
		if (playerObject->mobj->orientMatrixDirty != 0) {
			FVIEW_calcrotatemove(
				playerObject->pitch, playerObject->yaw,
				&g_objectTable[g_players[playerIdx]
						       .objectIndex]);
			FVIEW_calcrotateorient(
				g_objectTable[g_players[playerIdx].objectIndex]
					.roll,
				0,
				&g_objectTable[g_players[playerIdx]
						       .objectIndex]);
		}
		cameraX = Math_Dot3Q15Wrapped(
			(int16_t)scaledX, (int16_t)scaledY, (int16_t)scaledZ,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedSideX,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedSideY,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedSideZ);
		cameraY = Math_Dot3Q15Wrapped(
			(int16_t)scaledX, (int16_t)scaledY, (int16_t)scaledZ,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedFwdX,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedFwdY,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedFwdZ);
		cameraZ = Math_Dot3Q15Wrapped(
			(int16_t)scaledX, (int16_t)scaledY, (int16_t)scaledZ,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedUpX,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedUpY,
			g_objectTable[g_players[playerIdx].objectIndex]
				.mobj->cachedUpZ);
		trig2_ctop(cameraX, cameraY, cameraZ);
		FVIEW_BuildCameraOrient(
			g_objectTable[g_players[playerIdx].objectIndex].roll,
			g_objectTable[g_players[playerIdx].objectIndex].pitch,
			g_objectTable[g_players[playerIdx].objectIndex].yaw, 0,
			(int16_t)(CAMERA_AIM_CENTER_Q16 - trig2_pitch),
			trig2_xyangle, NULL);
	}

	{
		ObjectRecord *target = &g_objectTable[targetIdx];
		if (target->mobj != NULL && target->mobj->pCraft != NULL) {
			int16_t boundY;
			int16_t boundX;
			int largestA;
			int largestB;
			modelIndex = target->mobj->pCraft->modelIndex;
			boundY = g_modelDefs[modelIndex].boundSizeY;
			boundX = g_modelDefs[modelIndex].boundSizeX;
			if (boundX <= boundY &&
			    boundX <= g_modelDefs[modelIndex].boundSizeZ) {
				largestA = boundY;
				largestB = g_modelDefs[modelIndex].boundSizeZ;
			} else if (boundX >= boundY &&
				   g_modelDefs[modelIndex].boundSizeZ >=
					   boundY) {
				largestA = boundX;
				largestB = g_modelDefs[modelIndex].boundSizeZ;
			} else {
				largestA = boundX;
				largestB = boundY;
			}
			maxExtent =
				(int)((unsigned int)(largestA + largestB) >> 1)
				<< g_modelDefs[modelIndex].boundSizeShift;
		} else {
			maxExtent = g_objectTypeTable[target->objectType]
					    .maxBoundsExtent;
		}
	}
	if (useHudLayoutScale != 0) {
		cameraDivisor =
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 2]
				.colorIndexOrWidgetParam;
	} else {
		resolutionMode =
			g_players[playerIdx].network.flightResolutionMode;
		cameraDivisor =
			resolutionMode == FLIGHT_RESOLUTION_320X240
				? CAMERA_DIVISOR_LOW
				: (resolutionMode == FLIGHT_RESOLUTION_480X360
					   ? CAMERA_DIVISOR_HIGH
					   : CAMERA_DIVISOR_DEFAULT);
	}
	resolutionMode = g_players[playerIdx].network.flightResolutionMode;
	scaleShift = (resolutionMode == FLIGHT_RESOLUTION_640X480 ||
		      resolutionMode == FLIGHT_RESOLUTION_480X360)
			     ? 9
			     : 8;
	scale = ((unsigned int)maxExtent << scaleShift) /
		(unsigned int)cameraDivisor;
	scaleShift = 0;
	while (scale > CAMERA_SCALE_LIMIT) {
		scale >>= 1;
		++scaleShift;
	}
	if (resolutionMode == FLIGHT_RESOLUTION_640X480) {
		scale = ((uint16_t)scale >> 2) + (uint16_t)scale;
	}
	scale = (uint16_t)scale;
	g_players[playerIdx].viewState.cameraWorldX =
		Math_MulQ15((int)scale, g_camMatR2_X);
	g_players[playerIdx].viewState.cameraWorldY =
		Math_MulQ15((int)scale, g_camMatR2_Y);
	g_players[playerIdx].viewState.cameraWorldZ =
		Math_MulQ15((int)scale, g_camMatR2_Z);
	if (scaleShift != 0) {
		g_players[playerIdx].viewState.cameraWorldX =
			(int32_t)((uint32_t)g_players[playerIdx]
					  .viewState.cameraWorldX
				  << scaleShift);
		g_players[playerIdx].viewState.cameraWorldY =
			(int32_t)((uint32_t)g_players[playerIdx]
					  .viewState.cameraWorldY
				  << scaleShift);
		g_players[playerIdx].viewState.cameraWorldZ =
			(int32_t)((uint32_t)g_players[playerIdx]
					  .viewState.cameraWorldZ
				  << scaleShift);
	}
	g_players[playerIdx].viewState.cameraWorldX =
		(int32_t)((uint32_t)g_worldLocX -
			  (uint32_t)g_players[playerIdx]
				  .viewState.cameraWorldX);
	g_players[playerIdx].viewState.cameraWorldY =
		(int32_t)((uint32_t)g_worldLocY -
			  (uint32_t)g_players[playerIdx]
				  .viewState.cameraWorldY);
	g_players[playerIdx].viewState.cameraWorldZ =
		(int32_t)((uint32_t)g_worldLocZ -
			  (uint32_t)g_players[playerIdx]
				  .viewState.cameraWorldZ);
}

/* Resets the three message panes. The first time in a flight (while
 * g_readyMessagePaneLeft is -1) it sets the ready, system and flight group pane
 * rectangles for the resolution and clears them on g_flightOffscreenBuffer;
 * later calls with forceExpireActiveMessages set raise
 * g_flightMessagePanesForceExpire instead. Then it redraws the craft name and
 * status line, empties the three panes and the ready queue, sets
 * g_radioMessageBackupEnabled to 0, g_targetDescriptionMessageId to 331 (the
 * blank message) and g_unusedReadyMessagePaneInitialState to slot 0's id. The
 * modern build resets its message capture. */
// FUNCTION: XVT 0x450260
void Hud_ResetFlightMessagePanes(int forceExpireActiveMessages)
{

	if (g_readyMessagePaneLeft == -1) {
		switch (g_flightResolutionMode) {
		case FLIGHT_RESOLUTION_320X240:
			g_readyMessagePaneLeft = 42;
			g_readyMessagePaneRight = 277;
			g_systemMessagePaneLeft = 42;
			g_systemMessagePaneRight = 277;
			g_readyMessagePaneTop = 3;
			g_readyMessagePaneBottom = 26;
			g_systemMessagePaneTop = 32;
			g_systemMessagePaneBottom = 37;
			g_flightGroupMessagePaneLeft = 75;
			g_flightGroupMessagePaneTop = 44;
			g_flightGroupMessagePaneRight = 265;
			g_flightGroupMessagePaneBottom = 49;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_readyMessagePaneTop = 6;
			g_readyMessagePaneRight = 505;
			g_readyMessagePaneBottom = 53;
			g_systemMessagePaneTop = 64;
			g_systemMessagePaneRight = 555;
			g_systemMessagePaneBottom = 75;
			g_flightGroupMessagePaneLeft = 150;
			g_flightGroupMessagePaneTop = 89;
			g_flightGroupMessagePaneRight = 530;
			g_flightGroupMessagePaneBottom = 100;
			g_readyMessagePaneLeft = 85;
			g_systemMessagePaneLeft = 85;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_readyMessagePaneTop = 4;
			g_readyMessagePaneRight = 378;
			g_readyMessagePaneBottom = 39;
			g_systemMessagePaneTop = 48;
			g_systemMessagePaneRight = 415;
			g_systemMessagePaneBottom = 56;
			g_flightGroupMessagePaneLeft = 112;
			g_flightGroupMessagePaneTop = 66;
			g_flightGroupMessagePaneRight = 397;
			g_flightGroupMessagePaneBottom = 74;
			g_readyMessagePaneLeft = 63;
			g_systemMessagePaneLeft = 63;
			break;
		}

		FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth,
					 g_screenHeight,
					 g_screenWidth * g_flightBytesPerPixel);
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		FlightText_SetClipRect(
			g_readyMessagePaneLeft, g_readyMessagePaneTop,
			g_readyMessagePaneRight, g_readyMessagePaneBottom);
		g_flightFillClipRectFn();
		FlightText_SetClipRect(
			g_systemMessagePaneLeft, g_systemMessagePaneTop,
			g_systemMessagePaneRight, g_systemMessagePaneBottom);
		g_flightFillClipRectFn();
		FlightText_SetClipRect(g_flightGroupMessagePaneLeft,
				       g_flightGroupMessagePaneTop,
				       g_flightGroupMessagePaneRight,
				       g_flightGroupMessagePaneBottom);
		g_flightFillClipRectFn();
	} else if (forceExpireActiveMessages != 0) {
		++g_flightMessagePanesForceExpire;
	}

	FlightSurface_Lock();
	FlightSw_SetRenderTarget(NULL, 320, 200, 0);
	FlightText_SetWordWrap(0);
	FlightText_SetClearLineBackground(0);
	FlightText_SetFontTier(1);
	Hud_DrawCraftNameFpsAndNetworkStatus();
	FlightText_SetColor(0x43);
	g_unusedReadyMessagePaneInitialState =
		g_readyMessagePaneQueue[0].stateOrMessageId;
	g_radioMessageBackupEnabled = 0;
	g_readyMessageQueueCount = 0;
	g_targetDescriptionMessageId = 331;
	g_readyMessagePaneQueue[0].stateOrMessageId = UINT16_MAX;
	g_systemMessagePane.stateOrMessageId = UINT16_MAX;
	g_flightGroupMessagePane.stateOrMessageId = UINT16_MAX;
	FlightSurface_Unlock();
#ifdef XVT_MODERN
	XvtCockpitMessages_ResetWorking();
#endif
}

/* Makes room in slot 0 of g_readyMessagePaneQueue for a new message: when the
 * message there has been shown fewer than 2 times and is under a simulated
 * second old, moves every entry up one, so it waits behind the new one, and
 * raises g_readyMessageQueueCount, at most 9. Otherwise does nothing and the
 * message in slot 0 is overwritten. */
// FUNCTION: XVT 0x450BC0
void Hud_ShiftReadyMessageQueueForReplacement(void)
{
	uint8_t oldPendingCount;
	uint16_t destinationIndex;
	uint8_t newPendingCount;

	if (g_readyMessagePaneQueue[0].showCount < 2 &&
	    g_readyMessagePaneQueue[0].ageSeconds == 0) {
		oldPendingCount = g_readyMessageQueueCount;
		destinationIndex = oldPendingCount + 1;
		if (destinationIndex != 0) {
			do {
				g_readyMessagePaneQueue[destinationIndex] =
					g_readyMessagePaneQueue
						[destinationIndex - 1];
				destinationIndex--;
			} while (destinationIndex != 0);
		}

		newPendingCount = oldPendingCount + 1;
		g_readyMessageQueueCount = newPendingCount;
		if (newPendingCount >= 10) {
			g_readyMessageQueueCount = newPendingCount - 1;
		}
	}
}

/* Moves the waiting messages of g_readyMessagePaneQueue down one, so the next
 * one is in slot 0, and lowers g_readyMessageQueueCount. Does not check for an
 * empty queue, where the count wraps to 255. */
// FUNCTION: XVT 0x450C30
void Hud_AdvanceReadyMessageQueue(void)
{
	uint8_t oldPendingCount = g_readyMessageQueueCount;
	uint16_t destinationIndex = 0;

	if (oldPendingCount != 0) {
		do {
			g_readyMessagePaneQueue[destinationIndex] =
				g_readyMessagePaneQueue[destinationIndex + 1];
			++destinationIndex;
		} while (destinationIndex < oldPendingCount);
	}

	g_readyMessageQueueCount = oldPendingCount - 1;
}

/* Draws the message of pane paneType onto g_flightOffscreenBuffer: types 3, 4
 * and 7 in the system pane, 8 in the flight group pane, others in the ready
 * pane from slot 0. Returns at once when slot 0 is empty and the type is not a
 * system or flight group one. Plays slot 0's voice when it has one, has not
 * been shown and the voice option is on, whichever pane is drawn. The system
 * and flight group panes are cleared, their text centered in font tier 1 and
 * their state set to 1. For the ready pane, message 374 plays
 * FLIGHT_SOUND_MESSAGE_READY, and while the message log page is open the
 * message is not drawn, only finished and counted as shown; else
 * Hud_SetupReadyMessagePaneText.
 *
 * A first byte below 9 picks the color from g_messageTextPrefixColorCodes; type
 * 1 followed by a digit 0 to 3 takes entries 8 to 11 instead, and type 2 the
 * color of the sender's IFF; any other start is color 0x42. Up to 70 characters
 * are drawn; "[" and "]" step g_flightTextColorIndex to highlight and back, and
 * a 0xFE escape and its byte are skipped. Then Hud_FinishFlightMessagePane and
 * the pane's showCount rises. */
// FUNCTION: XVT 0x450C90
void Hud_ShowFlightMessagePane(int16_t paneType)
{

	char *text;
	uint16_t paneWidth;
	uint16_t textWidth;
	uint8_t prefix;
	uint8_t currentChar;
	uint16_t visibleChars;
	char lastChar;

#ifdef XVT_MODERN
	lastChar = 0;
#endif

	if (g_readyMessagePaneQueue[0].stateOrMessageId == UINT16_MAX &&
	    paneType != 3 && paneType != 8 && paneType != 4 && paneType != 7) {
		return;
	}

	FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth,
				 g_screenHeight,
				 g_flightBytesPerPixel * g_screenWidth);
	if (g_readyMessagePaneQueue[0].voiceSfxId != 0 &&
	    g_readyMessagePaneQueue[0].showCount == 0 &&
	    g_gameConfig.voiceSpecialEnabled != 0) {
		fsfx_QueueVoiceSfx(g_readyMessagePaneQueue[0].voiceSfxId, 0, 0,
				   0, 0xFFFFu);
	}

	if (paneType == 3 || paneType == 4 || paneType == 7) {
		FlightText_SetFontTier(1);
		text = g_systemMessagePane.text;
		g_systemMessagePane.stateOrMessageId = 1;
		FlightText_SetClipRect(
			g_systemMessagePaneLeft, g_systemMessagePaneTop,
			g_systemMessagePaneRight, g_systemMessagePaneBottom);
		paneWidth = g_systemMessagePaneRight - g_systemMessagePaneLeft;
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		g_flightFillClipRectFn();
		paneWidth >>= 1;
		textWidth = Hud_MeasureFlightMessagePaneText(paneType);
		textWidth >>= 1;
		paneWidth -= textWidth;
		FlightText_SetCursor(g_systemMessagePaneLeft + paneWidth,
				     g_systemMessagePaneTop);
	} else if (paneType == 8) {
		FlightText_SetFontTier(1);
		text = g_flightGroupMessagePane.text;
		g_flightGroupMessagePane.stateOrMessageId = 1;
		FlightText_SetClipRect(g_flightGroupMessagePaneLeft,
				       g_flightGroupMessagePaneTop,
				       g_flightGroupMessagePaneRight,
				       g_flightGroupMessagePaneBottom);
		paneWidth = g_flightGroupMessagePaneRight -
			    g_flightGroupMessagePaneLeft;
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		g_flightFillClipRectFn();
		paneWidth >>= 1;
		textWidth = Hud_MeasureFlightMessagePaneText(paneType);
		textWidth >>= 1;
		paneWidth -= textWidth;
		FlightText_SetCursor(g_flightGroupMessagePaneLeft + paneWidth,
				     g_flightGroupMessagePaneTop);
	} else {
		if (g_readyMessagePaneQueue[0].stateOrMessageId == 374) {
			fsfx_PlaySound(FLIGHT_SOUND_MESSAGE_READY, -1,
				       g_localPlayer);
		}
		if (g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] !=
		    MFD_PAGE_STATE_CLOSED) {
			Hud_FinishFlightMessagePane(
				g_readyMessagePaneQueue[0].paneType, lastChar);
			++g_readyMessagePaneQueue[0].showCount;
			FlightSw_SetRenderTarget(NULL, 320, 200, 0);
			return;
		}
		text = g_readyMessagePaneQueue[0].text;
		Hud_SetupReadyMessagePaneText();
	}

#ifdef XVT_MODERN
	XvtCockpitMessages_BeginMessage(paneType);
#endif
	prefix = (uint8_t)*text;
	if (prefix < 9) {
		++text;
		FlightText_SetColor(g_messageTextPrefixColorCodes[prefix]);
		if (prefix == 1) {
			currentChar = (uint8_t)*text;
			if (currentChar >= '0' && currentChar <= '3') {
				++text;
				FlightText_SetColor(
					g_messageTextPrefixColorCodes
						[currentChar - '(']);
			}
		} else if (prefix == 2) {
			FlightText_SetColor(
				g_messageSenderIffColorCodes
					[g_readyMessagePaneQueue[0].senderIff]);
		}
	} else {
		FlightText_SetColor(0x42);
	}

	visibleChars = 0;
	while (*text != '\0' && visibleChars < 70) {
		currentChar = (uint8_t)*text;
		if (currentChar == '[') {
			if (g_flightTextColorIndex == 0xD4) {
				--g_flightTextColorIndex;
			} else {
				++g_flightTextColorIndex;
			}
			++text;
		} else if (currentChar == ']') {
			if (g_flightTextColorIndex == 0xD3) {
				++g_flightTextColorIndex;
			} else {
				--g_flightTextColorIndex;
			}
			++text;
		} else if (currentChar == 0xFE) {
			text += 2;
		} else {
			++visibleChars;
			g_flightDrawCharFn(currentChar);
			lastChar = *text;
			++text;
		}
	}

#ifdef XVT_MODERN
	XvtCockpitMessages_RecordReveal(visibleChars);
#endif
	if (prefix == 3 || prefix == 4 || prefix == 7) {
		Hud_FinishFlightMessagePane(g_systemMessagePane.paneType,
					    lastChar);
		++g_systemMessagePane.showCount;
	} else if (prefix == 8) {
		Hud_FinishFlightMessagePane(g_flightGroupMessagePane.paneType,
					    lastChar);
		++g_flightGroupMessagePane.showCount;
	} else {
		Hud_FinishFlightMessagePane(g_readyMessagePaneQueue[0].paneType,
					    lastChar);
		++g_readyMessagePaneQueue[0].showCount;
	}
	FlightSw_SetRenderTarget(NULL, 320, 200, 0);
}

/* Prepares to draw the ready pane: font tier 1, a cleared pane with the
 * transparent background, shadow on in color 0x40, color 0x43, and the cursor
 * where slot 0's text is centered. Sets g_flightTextShadowEnabled to 1. */
// FUNCTION: XVT 0x451060
void Hud_SetupReadyMessagePaneText(void)
{
	uint16_t paneWidth;
	uint16_t textWidth;

	FlightText_SetFontTier(1);
	FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
	g_flightTextShadowEnabled = 1;
	FlightText_SetShadowColor(0x40);
	FlightText_SetClipRect(g_readyMessagePaneLeft, g_readyMessagePaneTop,
			       g_readyMessagePaneRight,
			       g_readyMessagePaneBottom);
	g_flightFillClipRectFn();
	paneWidth = g_readyMessagePaneRight - g_readyMessagePaneLeft;
	textWidth = Hud_MeasureFlightMessagePaneText(0);
	paneWidth >>= 1;
	textWidth >>= 1;
	paneWidth -= textWidth;
	FlightText_SetCursor(g_readyMessagePaneLeft + paneWidth,
			     g_readyMessagePaneTop);
	FlightText_SetColor(0x43);
}

/* Ends a drawn message: while the message log page is closed it adds "." unless
 * the text ended in "?", "!", ":", " " or ".", and clears the rest of the line.
 * Then sets how long the pane stays, in ticks of g_playerFlightTransientTimers:
 * the system pane 472 for types 3 and 7 and 1,888 for type 4; the flight group
 * pane 1,888; the ready pane 354 while messages wait, else 1,416 for types 1
 * and 2 and 1,652 for others. Redraws the craft name and status line and leaves
 * font tier 2. */
// FUNCTION: XVT 0x451100
void Hud_FinishFlightMessagePane(int16_t paneType, char lastChar)
{
	if (g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] == MFD_PAGE_STATE_CLOSED) {
		if (lastChar != '?' && lastChar != '!' && lastChar != ':' &&
		    lastChar != ' ' && lastChar != '.') {
			g_flightDrawCharFn('.');
		}
		FlightText_SetClearLineBackground(1);
		g_flightDrawCharFn('\n');
		FlightText_SetClearLineBackground(0);
	}
	if (paneType == 3 || paneType == 7) {
		g_playerFlightTransientTimers[g_localPlayer]
			.systemMessagePaneTimer = 472;
	} else if (paneType == 8) {
		g_playerFlightTransientTimers[g_localPlayer]
			.flightGroupMessagePaneTimer = 1888;
	} else if (paneType == 4) {
		g_playerFlightTransientTimers[g_localPlayer]
			.systemMessagePaneTimer = 1888;
	} else if (g_readyMessageQueueCount != 0) {
		g_playerFlightTransientTimers[g_localPlayer]
			.readyMessagePaneTimer = 354;
	} else if (paneType == 2 || paneType == 1) {
		g_playerFlightTransientTimers[g_localPlayer]
			.readyMessagePaneTimer = 1416;
	} else {
		g_playerFlightTransientTimers[g_localPlayer]
			.readyMessagePaneTimer = 1652;
	}
#ifdef XVT_MODERN
	XvtCockpitMessages_EndMessage();
#endif
	Hud_DrawCraftNameFpsAndNetworkStatus();
	FlightText_SetFontTier(2);
}

/* Expires the message panes. While the message log page is closed,
 * a ready message whose timer ran out (or all, with
 * g_flightMessagePanesForceExpire) gives way to the next waiting one or the
 * pane is cleared and slot 0 emptied; the system and flight group panes are
 * cleared and emptied when their timers run out or on a forced expiry. When the
 * target description timer is 0 and the player has a target whose description
 * changed and is actionable, it emits the description in the forward, HUD-only
 * and target camera views and restarts the timer at 1,180 ticks. Sets each
 * active player's pendingActionId to 0 once its pendingActionTimer is 0, and
 * clears g_flightMessagePanesForceExpire. */
// FUNCTION: XVT 0x451210
void Hud_UpdateFlightMessagePanes(void)
{

	int localPlayer = g_localPlayer;
	const int messageSurfaceWidth = 320;
	const int messageSurfaceHeight = 200;

	if (((g_playerFlightTransientTimers[localPlayer]
			      .readyMessagePaneTimer == 0 &&
	      g_readyMessagePaneQueue[0].stateOrMessageId != UINT16_MAX) ||
	     g_flightMessagePanesForceExpire != 0) &&
	    g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] == MFD_PAGE_STATE_CLOSED) {
		if (g_readyMessageQueueCount != 0) {
			Hud_AdvanceReadyMessageQueue();
			Hud_ShowFlightMessagePane(
				g_readyMessagePaneQueue[0].paneType);
		} else {
			FlightText_SetBackgroundColor(
				g_flightTransparentColorIndex);
			FlightSw_SetRenderTarget(g_flightOffscreenBuffer,
						 g_screenWidth, g_screenHeight,
						 g_screenWidth *
							 g_flightBytesPerPixel);
			FlightText_SetClipRect(g_readyMessagePaneLeft,
					       g_readyMessagePaneTop,
					       g_readyMessagePaneRight,
					       g_readyMessagePaneBottom);
			g_flightFillClipRectFn();
			FlightSw_SetRenderTarget(NULL, messageSurfaceWidth,
						 messageSurfaceHeight, 0);
			g_readyMessagePaneQueue[0].stateOrMessageId =
				UINT16_MAX;
#ifdef XVT_MODERN
			XvtCockpitMessages_Clear(XVT_COCKPIT_MESSAGE_READY);
#endif
		}
		localPlayer = g_localPlayer;
	}
	if ((g_playerFlightTransientTimers[localPlayer]
			     .systemMessagePaneTimer == 0 &&
	     g_systemMessagePane.stateOrMessageId != UINT16_MAX) ||
	    g_flightMessagePanesForceExpire != 0) {
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth,
					 g_screenHeight,
					 g_screenWidth * g_flightBytesPerPixel);
		FlightText_SetClipRect(
			g_systemMessagePaneLeft, g_systemMessagePaneTop,
			g_systemMessagePaneRight, g_systemMessagePaneBottom);
		g_flightFillClipRectFn();
		FlightSw_SetRenderTarget(NULL, messageSurfaceWidth,
					 messageSurfaceHeight, 0);
		g_systemMessagePane.stateOrMessageId = UINT16_MAX;
#ifdef XVT_MODERN
		XvtCockpitMessages_Clear(XVT_COCKPIT_MESSAGE_SYSTEM);
#endif
		localPlayer = g_localPlayer;
	}
	if ((g_playerFlightTransientTimers[localPlayer]
			     .flightGroupMessagePaneTimer == 0 &&
	     g_flightGroupMessagePane.stateOrMessageId != UINT16_MAX) ||
	    g_flightMessagePanesForceExpire != 0) {
		FlightText_SetBackgroundColor(g_flightTransparentColorIndex);
		FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth,
					 g_screenHeight,
					 g_screenWidth * g_flightBytesPerPixel);
		FlightText_SetClipRect(g_flightGroupMessagePaneLeft,
				       g_flightGroupMessagePaneTop,
				       g_flightGroupMessagePaneRight,
				       g_flightGroupMessagePaneBottom);
		g_flightFillClipRectFn();
		FlightSw_SetRenderTarget(NULL, messageSurfaceWidth,
					 messageSurfaceHeight, 0);
		g_flightGroupMessagePane.stateOrMessageId = UINT16_MAX;
#ifdef XVT_MODERN
		XvtCockpitMessages_Clear(XVT_COCKPIT_MESSAGE_FLIGHT_GROUP);
#endif
		localPlayer = g_localPlayer;
	}
	if (g_playerFlightTransientTimers[localPlayer]
			    .targetDescriptionRefreshTimer == 0 &&
	    g_players[localPlayer].currentTargetObjectIdx != -1) {
		if (g_targetDescriptionMessageId !=
		    msg_BuildTargetDescription(
			    g_players[localPlayer].currentTargetObjectIdx,
			    localPlayer, 0, 0)) {
			if (msg_BuildTargetDescription(
				    g_players[g_localPlayer]
					    .currentTargetObjectIdx,
				    g_localPlayer, 0, 1) != 0) {
				uint8_t hudState =
					g_players[g_localPlayer]
						.viewState.hudStateLive;
				if (hudState == 19 || hudState == 0 ||
				    hudState == 20) {
					g_targetDescriptionMessageId =
						msg_BuildTargetDescription(
							g_players[g_localPlayer]
								.currentTargetObjectIdx,
							g_localPlayer, 1, 0);
					g_playerFlightTransientTimers[g_localPlayer]
						.targetDescriptionRefreshTimer =
						1180;
				}
			}
		}
	}
	{
		int playerIndex;
		for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
			if (g_players[playerIndex].participationState != 0 &&
			    g_players[playerIndex].pendingActionTimer == 0) {
				g_players[playerIndex].pendingActionId = 0;
			}
		}
	}
	if (g_flightMessagePanesForceExpire != 0) {
		g_flightMessagePanesForceExpire = 0;
	}
}

/* Also zeroes the system pane's timer, so the next Hud_UpdateFlightMessagePanes clears any system message. */
/* Empties g_readyMessagePaneQueue and its count without clearing the drawn
 * pane. */
// FUNCTION: XVT 0x451560
void Hud_ClearReadyMessageQueue(void)
{
	g_readyMessageQueueCount = 0;
	g_readyMessagePaneQueue[0].stateOrMessageId = UINT16_MAX;
	g_playerFlightTransientTimers[g_localPlayer].systemMessagePaneTimer = 0;
}

/* Raises the ageSeconds of each pane that holds a message. Flight_UpdateTimers
 * calls it in the part that runs once per simulated second. */
// FUNCTION: XVT 0x451590
void Hud_AdvanceFlightMessagePaneTimers(void)
{
	if (g_readyMessagePaneQueue[0].stateOrMessageId != UINT16_MAX) {
		++g_readyMessagePaneQueue[0].ageSeconds;
	}
	if (g_systemMessagePane.stateOrMessageId != UINT16_MAX) {
		++g_systemMessagePane.ageSeconds;
	}
	if (g_flightGroupMessagePane.stateOrMessageId != UINT16_MAX) {
		++g_flightGroupMessagePane.ageSeconds;
	}
}

/* In the forward and HUD-only views, redraws the status line in layout 126 of
 * the current set: the local player's craft name, or with
 * g_flightConfTickCounterEnabled and all three samples nonzero, two numbers:
 * SIMULATION_TICKS_PER_SECOND divided by g_flightTickOverlayLastLoopTicks and
 * by the mean ticks per sample of the window. With more than one player it also
 * draws the packet drop and lag marks (g_strCmdThreatDisplayText 16 and 17)
 * beside the ready pane on g_flightOffscreenBuffer, colored by
 * g_packetDropIndicator and g_lagIndicator (0 draws them in the transparent
 * color). Leaves g_flightTextShadowEnabled at 0 and the shadow color 64 in that
 * case. */
// FUNCTION: XVT 0x4515D0
void Hud_DrawCraftNameFpsAndNetworkStatus(void)
{

	uint8_t hudStateLive;
	int elementIndex;
	int offscreenPitchBytes;

	hudStateLive = g_players[g_localPlayer].viewState.hudStateLive;
	if (hudStateLive != HUD_VIEW_FORWARD &&
	    hudStateLive != HUD_VIEW_HUD_ONLY) {
		return;
	}

	FlightSurface_Lock();
	FlightSw_SetRenderTarget(NULL, 320, 200, 0);
	FlightText_SetBackgroundColor(44);
	FlightText_SetFontTier(0);
	if (g_flightResolutionMode == FLIGHT_RESOLUTION_320X240) {
		Hud_AppendObjectDisplayName(
			g_players[g_localPlayer].objectIndex, 7);
	} else {
		Hud_AppendObjectDisplayName(
			g_players[g_localPlayer].objectIndex, 3);
	}
	FlightText_SetCursor(
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].x,
		g_hudElementLayouts[g_hudInstrumentSetBaseIndex + 126].y);
	elementIndex = g_hudInstrumentSetBaseIndex;
	FlightText_SetClipRect(
		g_hudElementLayouts[elementIndex + 126].x,
		g_hudElementLayouts[elementIndex + 126].y,
		g_hudElementLayouts[elementIndex + 126].x +
			g_hudElementLayouts[elementIndex + 126].clipWidth,
		g_hudElementLayouts[elementIndex + 126].y +
			g_hudElementLayouts[elementIndex + 126]
				.clipHeightOrForegroundColor);
	g_flightFillClipRectFn();
	if (g_flightConfTickCounterEnabled != 0 &&
	    g_flightTickOverlayLastLoopTicks != 0 &&
	    g_flightTickOverlayWindowTicks != 0 &&
	    g_flightTickOverlaySampleCount != 0) {
		FlightText_SetColor(78);
		sprintf(g_flightTextScratchBuffer, "%d %d",
			SIMULATION_TICKS_PER_SECOND /
				g_flightTickOverlayLastLoopTicks,
			SIMULATION_TICKS_PER_SECOND /
				(g_flightTickOverlayWindowTicks /
				 g_flightTickOverlaySampleCount));
	}
#ifdef XVT_MODERN
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS,
				   g_flightTextScratchBuffer,
				   XVT_COCKPIT_ALIGN_CENTER);
#endif
	FlightText_DrawStringCentered(g_flightTextScratchBuffer);
	FlightSurface_Unlock();

	if (g_flightPlayerCount > 1) {
		g_flightTextShadowEnabled = 0;
		FlightText_SetFontTier(0);
		offscreenPitchBytes = g_flightBytesPerPixel * g_screenWidth;
		FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth,
					 g_screenHeight, offscreenPitchBytes);
		FlightText_SetClipRect(g_readyMessagePaneLeft -
					       2 * g_flightFontDigitWidth,
				       g_readyMessagePaneTop,
				       2 * g_flightFontDigitWidth +
					       g_readyMessagePaneRight + 1,
				       g_readyMessagePaneBottom);
		FlightText_SetCursor(g_readyMessagePaneLeft -
					     2 * g_flightFontDigitWidth,
				     g_readyMessagePaneTop);
		switch (g_packetDropIndicator) {
		case 0:
			FlightText_SetBackgroundColor(
				g_flightTransparentColorIndex);
			FlightText_SetColor(g_flightTransparentColorIndex);
			FlightText_SetShadowColor(
				g_flightTransparentColorIndex);
			break;
		case 1:
			FlightText_SetBackgroundColor(65);
			FlightText_SetColor(78);
			break;
		case 2:
			FlightText_SetBackgroundColor(65);
			FlightText_SetColor(75);
			break;
		case 3:
			FlightText_SetBackgroundColor(65);
			FlightText_SetColor(74);
			break;
		}
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_NETWORK_PING,
					   g_strCmdThreatDisplayText[16],
					   XVT_COCKPIT_ALIGN_LEFT);
#endif
		FlightText_DrawString(g_strCmdThreatDisplayText[16]);
		switch (g_lagIndicator) {
		case 0:
			FlightText_SetBackgroundColor(
				g_flightTransparentColorIndex);
			FlightText_SetColor(g_flightTransparentColorIndex);
			FlightText_SetShadowColor(
				g_flightTransparentColorIndex);
			break;
		case 1:
			FlightText_SetBackgroundColor(65);
			FlightText_SetColor(78);
			break;
		case 2:
			FlightText_SetBackgroundColor(65);
			FlightText_SetColor(74);
			break;
		case 3:
			FlightText_SetBackgroundColor(65);
			FlightText_SetColor(75);
			break;
		}
#ifdef XVT_MODERN
		XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_NETWORK_LAG,
					   g_strCmdThreatDisplayText[17],
					   XVT_COCKPIT_ALIGN_RIGHT);
#endif
		FlightText_DrawStringRightAligned(
			g_strCmdThreatDisplayText[17]);
		FlightText_SetShadowColor(64);
		FlightSw_SetRenderTarget(NULL, 320, 200, 0);
	}
}

/* Returns g_systemMessagePane.stateOrMessageId: 0xFFFF while the pane is empty,
 * 1 once a message is shown in it. */
// FUNCTION: XVT 0x451930
uint16_t Hud_GetSystemMessagePaneState(void)
{
	return g_systemMessagePane.stateOrMessageId;
}

/* Copies the message panes from g_flightOffscreenBuffer onto the flight
 * surface, skipping the transparent color: the ready pane, widened by two digit
 * widths on each side while g_flightPlayerCount is 1 or more, to layout 117 of
 * the current set; the system and flight group panes, while they hold a
 * message, to layouts 118 and 119, or in the full-screen view to those layouts'
 * x at 11 and 22 rows above the viewport's bottom. The modern build latches the
 * panes for its renderer. */
// FUNCTION: XVT 0x452960
void Hud_BlitSoftwareHudTextPanes(void)
{
	uint16_t transparentColor;

#ifdef XVT_MODERN
	XvtCockpit_BeginMessagePlacement();
#endif
	transparentColor = g_flightTransparentColorIndex;
	if (g_flightPlayerCount >= 1) {
		FlightText_SetFontTier(0);
#ifdef XVT_MODERN
		XvtCockpit_LatchMessage(
			XVT_COCKPIT_MESSAGE_READY,
			g_readyMessagePaneLeft - 2 * g_flightFontDigitWidth,
			g_readyMessagePaneTop,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_readyMessagePaneRight + 4 * g_flightFontDigitWidth -
				g_readyMessagePaneLeft + 1,
			g_readyMessagePaneBottom - g_readyMessagePaneTop);
#endif
		FlightSw_BlitRectToFlightSurface(
			g_flightOffscreenBuffer, transparentColor,
			g_readyMessagePaneLeft - 2 * g_flightFontDigitWidth,
			g_readyMessagePaneTop,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_readyMessagePaneRight + 4 * g_flightFontDigitWidth -
				g_readyMessagePaneLeft + 1,
			g_readyMessagePaneBottom - g_readyMessagePaneTop,
			g_screenWidth * g_flightBytesPerPixel);
	} else {
#ifdef XVT_MODERN
		XvtCockpit_LatchMessage(
			XVT_COCKPIT_MESSAGE_READY, g_readyMessagePaneLeft,
			g_readyMessagePaneTop,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_readyMessagePaneRight - g_readyMessagePaneLeft,
			g_readyMessagePaneBottom - g_readyMessagePaneTop);
#endif
		FlightSw_BlitRectToFlightSurface(
			g_flightOffscreenBuffer, g_flightTransparentColorIndex,
			g_readyMessagePaneLeft, g_readyMessagePaneTop,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hudElementLayouts[g_hudInstrumentSetBaseIndex +
					    HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_readyMessagePaneRight - g_readyMessagePaneLeft,
			g_readyMessagePaneBottom - g_readyMessagePaneTop,
			g_screenWidth * g_flightBytesPerPixel);
	}

	if (g_players[g_localPlayer].viewState.hudStateLive !=
	    HUD_VIEW_FULL_SCREEN) {
		if (g_systemMessagePane.stateOrMessageId != UINT16_MAX) {
#ifdef XVT_MODERN
			XvtCockpit_LatchMessage(
				XVT_COCKPIT_MESSAGE_SYSTEM,
				g_systemMessagePaneLeft, g_systemMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 118]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 118]
						.y,
				g_systemMessagePaneRight -
					g_systemMessagePaneLeft,
				g_systemMessagePaneBottom -
					g_systemMessagePaneTop);
#endif
			FlightSw_BlitRectToFlightSurface(
				g_flightOffscreenBuffer, transparentColor,
				g_systemMessagePaneLeft, g_systemMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 118]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 118]
						.y,
				g_systemMessagePaneRight -
					g_systemMessagePaneLeft,
				g_systemMessagePaneBottom -
					g_systemMessagePaneTop,
				g_screenWidth * g_flightBytesPerPixel);
		}
		if (g_flightGroupMessagePane.stateOrMessageId != UINT16_MAX) {
#ifdef XVT_MODERN
			XvtCockpit_LatchMessage(
				XVT_COCKPIT_MESSAGE_FLIGHT_GROUP,
				g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 119]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 119]
						.y,
				g_flightGroupMessagePaneRight -
					g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneBottom -
					g_flightGroupMessagePaneTop);
#endif
			FlightSw_BlitRectToFlightSurface(
				g_flightOffscreenBuffer, transparentColor,
				g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 119]
						.x,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 119]
						.y,
				g_flightGroupMessagePaneRight -
					g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneBottom -
					g_flightGroupMessagePaneTop,
				g_screenWidth * g_flightBytesPerPixel);
		}
	} else {
		if (g_systemMessagePane.stateOrMessageId != UINT16_MAX) {
#ifdef XVT_MODERN
			XvtCockpit_LatchMessage(
				XVT_COCKPIT_MESSAGE_SYSTEM,
				g_systemMessagePaneLeft, g_systemMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 118]
						.x,
				g_flightVpHeight - 11,
				g_systemMessagePaneRight -
					g_systemMessagePaneLeft,
				g_systemMessagePaneBottom -
					g_systemMessagePaneTop);
#endif
			FlightSw_BlitRectToFlightSurface(
				g_flightOffscreenBuffer, transparentColor,
				g_systemMessagePaneLeft, g_systemMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 118]
						.x,
				g_flightVpHeight - 11,
				g_systemMessagePaneRight -
					g_systemMessagePaneLeft,
				g_systemMessagePaneBottom -
					g_systemMessagePaneTop,
				g_screenWidth * g_flightBytesPerPixel);
		}
		if (g_flightGroupMessagePane.stateOrMessageId != UINT16_MAX) {
#ifdef XVT_MODERN
			XvtCockpit_LatchMessage(
				XVT_COCKPIT_MESSAGE_FLIGHT_GROUP,
				g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 119]
						.x,
				g_flightVpHeight - 22,
				g_flightGroupMessagePaneRight -
					g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneBottom -
					g_flightGroupMessagePaneTop);
#endif
			FlightSw_BlitRectToFlightSurface(
				g_flightOffscreenBuffer, transparentColor,
				g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneTop,
				g_hudElementLayouts
					[g_hudInstrumentSetBaseIndex + 119]
						.x,
				g_flightVpHeight - 22,
				g_flightGroupMessagePaneRight -
					g_flightGroupMessagePaneLeft,
				g_flightGroupMessagePaneBottom -
					g_flightGroupMessagePaneTop,
				g_screenWidth * g_flightBytesPerPixel);
		}
	}
#ifdef XVT_MODERN
	XvtCockpit_LatchMessages();
#endif
}

/* Returns the width in pixels of pane paneType's text without its type byte and
 * without "[" and "]", up to 70 characters. The test for a 0xFE escape inside
 * the bracket check never holds; FlightText_MeasureStringWidth skips the escape
 * instead. A type 1 message's color digit is measured too. */
// FUNCTION: XVT 0x452C40
uint16_t Hud_MeasureFlightMessagePaneText(int16_t paneType)
{
	const char *text;
	char measuredText[80];
	char currentChar;
	unsigned int processedCount;
	uint16_t outputLength;

	if (paneType == 3 || paneType == 4 || paneType == 7) {
		text = g_systemMessagePane.text;
	} else if (paneType == 8) {
		text = g_flightGroupMessagePane.text;
	} else {
		text = g_readyMessagePaneQueue[0].text;
	}

	if ((uint8_t)text[0] < 9) {
		++text;
	}

	processedCount = 0;
	outputLength = 0;
	if (text[processedCount] != '\0') {
		do {
			if ((uint16_t)processedCount >= 70) {
				break;
			}
			currentChar = text[processedCount];
			if (currentChar == '[' || currentChar == ']') {
				if (currentChar == (char)0xFE) {
					text += 2;
				}
			} else {
				measuredText[outputLength++] = currentChar;
			}
			++processedCount;
		} while (text[processedCount] != '\0');
	}
	measuredText[outputLength] = '\0';

	return FlightText_MeasureStringWidth(measuredText);
}

/* Draws the corners of a box at x, y in the flight viewport, each stroke an
 * eighth of the box's size, at least 3 and at most the size, tested against the
 * scene's depth at depth (at least 1). Returns at once when the box is empty or
 * lies outside the viewport. With hardware 3D it hands the box to
 * Hud_DrawBoxOverlayHW; else it draws the strokes as spans through
 * sw3d_BlitOccludedSpan at depth g_projScaleInt / depth, from a run of colorIdx
 * in g_panelBoxSpanScratch, locking the surface unless
 * g_flightSurfaceAlreadyLocked is set. */
// FUNCTION: XVT 0x497E00
void Hud_DrawDepthTestedBoxCorners(int x, int y, int width, int height,
				   int colorIdx, int depth)
{

	int bottom;
	int cornerWidth;
	int left;
	int cornerHeight;
	uint8_t *span;
	float spanDepth;

	left = x;
	bottom = y + height;
	if (bottom <= 0) {
		return;
	}
	if (left + width <= 0 || left >= g_flightVpWidth ||
	    y >= g_flightVpHeight || height <= 0 || width <= 0) {
		return;
	}
	if (g_useHardware3D != 0) {
		Hud_DrawBoxOverlayHW(left, y, width, height, colorIdx, depth);
		return;
	}

	cornerWidth = width >> 3;
	cornerHeight = height >> 3;
	if (cornerWidth < 3) {
		cornerWidth = 3;
	}
	if (cornerHeight < 3) {
		cornerHeight = 3;
	}
	if (cornerWidth > width) {
		cornerWidth = width;
	}
	if (height < cornerHeight) {
		cornerHeight = height;
	}

	span = g_panelBoxSpanScratch;
	if (g_flightBytesPerPixel == 2) {
		uint16_t *span16;
		uint16_t color;
		int i;

		if (left > 0) {
			span = &g_panelBoxSpanScratch[2 * left];
		}
		span16 = (uint16_t *)span;
		color = g_flightPalette16Bpp[colorIdx];
		for (i = 0; i < cornerWidth; ++i) {
			span16[i] = color;
		}
	} else {
		int i;

		if (left > 0) {
			span = &g_panelBoxSpanScratch[left];
		}
		for (i = 0; i < cornerWidth; ++i) {
			span[i] = (uint8_t)colorIdx;
		}
	}

	if (depth < 1) {
		depth = 1;
	}
	spanDepth = (float)(uint32_t)g_projScaleInt / (float)depth;
	if (g_flightSurfaceAlreadyLocked == 0) {
		FlightSurface_Lock();
	}

	if (y >= 0) {
		int spanStart;
		int spanEnd;

		spanStart = left;
		spanEnd = left + cornerWidth;
		if (spanEnd > 0 && g_flightVpWidth > left) {
			if (spanStart < 0) {
				spanStart = 0;
			}
			if (spanEnd > g_flightVpWidth) {
				spanEnd = g_flightVpWidth;
			}
			sw3d_BlitOccludedSpan(span, spanStart, spanEnd, y,
					      spanDepth);
		}

		spanStart = width - cornerWidth + left;
		spanEnd = left + width;
		if (spanEnd > 0 && spanStart < g_flightVpWidth) {
			if (spanStart < 0) {
				spanStart = 0;
			}
			if (spanEnd > g_flightVpWidth) {
				spanEnd = g_flightVpWidth;
			}
			sw3d_BlitOccludedSpan(span, spanStart, spanEnd, y,
					      spanDepth);
		}
	}

	if (g_flightVpHeight >= bottom) {
		int spanStart;
		int spanEnd;

		spanStart = left;
		spanEnd = left + cornerWidth;
		if (spanEnd > 0 && g_flightVpWidth > left) {
			if (spanStart < 0) {
				spanStart = 0;
			}
			if (spanEnd > g_flightVpWidth) {
				spanEnd = g_flightVpWidth;
			}
			sw3d_BlitOccludedSpan(span, spanStart, spanEnd,
					      bottom - 1, spanDepth);
		}

		spanStart = width - cornerWidth + left;
		spanEnd = left + width;
		if (spanEnd > 0 && spanStart < g_flightVpWidth) {
			if (spanStart < 0) {
				spanStart = 0;
			}
			if (spanEnd > g_flightVpWidth) {
				spanEnd = g_flightVpWidth;
			}
			sw3d_BlitOccludedSpan(span, spanStart, spanEnd,
					      bottom - 1, spanDepth);
		}
	}

	{
		int rowOffset;

		for (rowOffset = 1; rowOffset < cornerHeight; ++rowOffset) {
			int scanY;

			scanY = y + rowOffset;
			if (scanY >= 0 && g_flightVpHeight > scanY) {
				if (left >= 0) {
					sw3d_BlitOccludedSpan(span, left,
							      left + 1, scanY,
							      spanDepth);
				}
				if (left + width <= g_flightVpWidth) {
					sw3d_BlitOccludedSpan(
						span, left + width - 1,
						left + width, scanY, spanDepth);
				}
			}
		}
	}

	{
		int rowOffset;
		int lastRow;

		lastRow = height - 1;
		for (rowOffset = height - cornerHeight; rowOffset < lastRow;
		     ++rowOffset) {
			if (rowOffset >= cornerHeight) {
				int scanY;

				scanY = y + rowOffset;
				if (scanY >= 0 && g_flightVpHeight > scanY) {
					if (left >= 0) {
						sw3d_BlitOccludedSpan(
							span, left, left + 1,
							scanY, spanDepth);
					}
					if (left + width <= g_flightVpWidth) {
						sw3d_BlitOccludedSpan(
							span, left + width - 1,
							left + width, scanY,
							spanDepth);
					}
				}
			}
		}
	}

	if (g_flightSurfaceAlreadyLocked == 0) {
		FlightSurface_Unlock();
	}
}

/* Reads panel sprites from fileName: records end at a 0xFF byte; the first
 * recordsToSkip are dropped and the next spriteCount are copied, each ended
 * with 0xFF, to g_hudPanelSpriteDataWriteCursor, which advances, with
 * g_hudPanelSpriteDataByIndex from firstSpriteIndex on pointing at each. A file
 * that ends early gives empty sprites. Returns what FeDiskIo_CloseGlobalStream
 * returns; in the modern build 1 when the file does not open, and a read error
 * is fatal. Does not check the 265 entries or the space at the cursor. */
// FUNCTION: XVT 0x49C250
int16_t Hud_LoadPanelSpriteRecords(const char *fileName,
				   uint16_t firstSpriteIndex,
				   int16_t spriteCount, uint16_t recordsToSkip)
{
	int16_t remainingSprites;
	int16_t recordIndex;
	int16_t byteValue;
	XvtFile *stream;

	FeDiskIo_OpenGlobalStream(fileName, "rb", 1, 0);
	remainingSprites = spriteCount;
	stream = g_stream;
#ifdef XVT_MODERN
	if (!stream) {
		return 1;
	}
#endif
	recordIndex = 0;
	while (remainingSprites != 0) {
		g_hudPanelSpriteDataByIndex[firstSpriteIndex] =
			g_hudPanelSpriteDataWriteCursor;
		if (recordIndex >= (int)recordsToSkip) {
			++firstSpriteIndex;
		}
#ifdef XVT_MODERN
		for (byteValue = (int16_t)File_Getc(stream);
		     !File_Eof(stream) && !File_HasError(stream);
		     byteValue = (int16_t)File_Getc(stream)) {
#else
		for (byteValue = (int16_t)File_Getc(stream);
		     (((Msvc42CrtFilePrefix *)stream)->flags & 0x10) == 0;
		     byteValue = (int16_t)File_Getc(stream)) {
#endif
			if (byteValue == 0xff) {
				break;
			}
			if (recordIndex >= (int)recordsToSkip) {
				*g_hudPanelSpriteDataWriteCursor =
					(uint8_t)byteValue;
				++g_hudPanelSpriteDataWriteCursor;
			}
		}
#ifdef XVT_MODERN
		if (File_HasError(stream)) {
			FeDiskIo_CloseGlobalStream(0);
			XvtStorage_Fatal("Cannot read panel sprites", 1);
			return 1;
		}
#endif
		if (recordIndex >= (int)recordsToSkip) {
			--remainingSprites;
			*g_hudPanelSpriteDataWriteCursor = 0xff;
			++g_hudPanelSpriteDataWriteCursor;
		}
		++recordIndex;
	}
#ifdef XVT_MODERN
	XvtRenderAssets_RegisterPanel(
		fileName, (uint16_t)(firstSpriteIndex - spriteCount),
		(uint16_t)spriteCount, recordsToSkip);
#endif
	return FeDiskIo_CloseGlobalStream(0);
}

/* Reads flight icon frames from fileName into dataBuffer, frames ending at a
 * 0xFF byte, each copied with its 0xFF and pointed at by framePointers in
 * order. Returns the frame count, or 0 when the file does not open; in the
 * modern build a read error is fatal. A file ending in 0xFF yields an empty
 * last frame. Does not check the buffer's size or the pointer count. */
// FUNCTION: XVT 0x49C330
int FlightIcon_LoadFrames(char *fileName, uint8_t *dataBuffer,
			  uint8_t **framePointers)
{
	int16_t frameCount;
	XvtFile *stream;
	int16_t value;
#ifndef XVT_MODERN
	int *streamFlags;
#endif

	if (FeDiskIo_OpenGlobalStream(fileName, "rb", 1, 0) == 0) {
		return 0;
	}
	frameCount = 0;
	stream = g_stream;
#ifdef XVT_MODERN
	while (1) {
		if (File_HasError(stream)) {
			FeDiskIo_CloseGlobalStream(0);
			XvtStorage_Fatal("Cannot read icon frames", 1);
			return 0;
		}
		if (File_Eof(stream)) {
			break;
		}
		framePointers[frameCount] = dataBuffer;
		while (1) {
			value = (int16_t)File_Getc(stream);
			if (File_HasError(stream)) {
				FeDiskIo_CloseGlobalStream(0);
				XvtStorage_Fatal("Cannot read icon frames", 1);
				return 0;
			}
			if (File_Eof(stream)) {
				break;
			}
			if (value == 0xff) {
				break;
			}
			*dataBuffer = (uint8_t)value;
			++dataBuffer;
		}
		++frameCount;
		*dataBuffer = 0xff;
		++dataBuffer;
	}
#else
	streamFlags = &((Msvc42CrtFilePrefix *)stream)->flags;
	for (; (*streamFlags & 0x10) == 0; ++dataBuffer) {
		framePointers[frameCount] = dataBuffer;
		for (value = (int16_t)File_Getc(stream);
		     (*streamFlags & 0x10) == 0;
		     value = (int16_t)File_Getc(stream)) {
			if (value == 0xff) {
				break;
			}
			*dataBuffer = (uint8_t)value;
			++dataBuffer;
		}
		++frameCount;
		*dataBuffer = 0xff;
	}
#endif
	FeDiskIo_CloseGlobalStream(0);
#ifdef XVT_MODERN
	XvtRenderAssets_RegisterIcons(fileName, framePointers,
				      (uint16_t)frameCount);
#endif
	return frameCount;
}
