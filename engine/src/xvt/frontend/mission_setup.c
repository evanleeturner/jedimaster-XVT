#include "xvt/frontend/mission_setup.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/campaign_task.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/frontend_cleanup.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/mission_dialogs.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/assets/model_preview.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XVT_MODERN
#include <strings.h>
#endif

/* Folder of each mission type, by MissionDirectoryId: training, melee,
 * tournament, combat engagement, battle, campaign. Mission lists and files are
 * read as "<folder>\<file>". Constant. */
// GLOBAL: XVT 0x52C1E8
const char *g_missionDirectoryNames[6] = {"train",  "melee",  "tourn",
					  "combat", "battle", "campaign"};
/* Offset from FRONTSTR_273_NONE of the name of each flight group warhead code,
 * 0 to 10: code 10 gives 2, and codes 2 to 9 give 3 to 10. Constant; only
 * MissionSetup_GetWarheadType reads it. */
// GLOBAL: XVT 0x52C6A8
const int g_warheadTypeMap[11] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 2};
/* Craft species of each preset craft choice: index 0 holds 0, preset option 0
 * being the flight group's own craft; 1 to 5 are the Rebel craft (Z-95, X-wing,
 * Y-wing, A-wing, B-wing) and 6 to 10 the Imperial ones (TIE fighter,
 * interceptor, bomber, advanced, assault gunboat). Flight groups of preset
 * category 3 index it at their option plus 5. Constant. */
// GLOBAL: XVT 0x52C564
const int g_presetCraftTypes[11] = {0, 14, 1, 2, 3, 4, 5, 6, 7, 8, 16};
/* For each craft species 0 to 19, the craft of the other side that
 * MissionSetup_InitCraftLoadout, its only reader, switches to when a melee or
 * tournament sequence past its first mission gives the pilot a craft of the
 * wrong side; 0 for none. Constant. */
// GLOBAL: XVT 0x52C550
const uint8_t g_craftIffCounterpart[20] = {0, 6, 16, 8, 16, 14, 1, 14, 3, 3,
					   3, 3, 3,  3, 5,  5,	2, 0,  0, 0};
/* World position (x, y, z) of the craft model in the briefing craft screen's
 * preview, by craft species 0 to 16; only MissionSetup_DrawCraftLoadout reads
 * it, without checking that the species is under 17. Constant. */
// GLOBAL: XVT 0x52C5D8
const ModelPreviewCraftPosition g_modelPreviewCraftPositions[17] = {
	{0, 0, 0},	 {25, -30, 20}, {0, -120, 20}, {10, -55, 20},
	{0, 0, 0},	 {0, 400, 30},	{40, 390, 45}, {-30, 240, 10},
	{-10, 200, 10},	 {0, 0, 0},	{0, 0, 0},     {0, 0, 0},
	{0, 0, 0},	 {0, 0, 0},	{40, 10, 40},  {0, 0, 0},
	{-10, -200, 10},
};
/* Flight group given to each team slot: entry team * 8 + slot, beside
 * g_missionSetupPlayerAssignments.teamPlayerIds[team][slot]; -1 for none. Many
 * functions write it, chiefly MissionSetup_FlightAssignmentUpdate, which sets
 * all 80 entries to -1 on its frame 0 unless g_frontendSkipScreenEntrySetup is
 * set, the other flight assignment functions, and
 * FrontendNet_ProcessNetworkPackets from the flight assignment packets. */
// GLOBAL: XVT 0xAA5AC0
int g_missionSetupPlayerFlightGroupIndices[80] = {0};
/* Five ramps of eight colors, dark to bright, from 0x48 to 0xFC in each lit
 * channel: green, red, yellow, blue and magenta. The briefing map's label and
 * craft icon highlights read it. Only MissionSetup_FlightAssignmentUpdate
 * writes it, on its frame 0; until then every entry is 0. */
// GLOBAL: XVT 0xAA5C10
int g_textShadeRamps[5][8] = {{0}};
/* The team assignment: teamPlayerIds holds the player id in each of the 8 slots
 * of the 10 teams, slot 0 being the team's captain, 0 for an empty slot, and
 * assignedPlayerIds the ids of the players who have a team slot. Many functions
 * write it, chiefly the team assignment screen and the functions it calls, the
 * prune functions, and FrontendNet_ProcessNetworkPackets from the team
 * assignment packets. */
// GLOBAL: XVT 0xAA5CB0
MissionSetupPlayerAssignments g_missionSetupPlayerAssignments = {{0}, {0}};
/* Player flight groups of each team in the loaded mission, which is the team's
 * number of player slots. Only MissionSetup_UpdateTeamCounts writes it. */
// GLOBAL: XVT 0xAA5E10
int g_teamPlayerFlightGroupCount[10] = {0};
/* Id of the player being dragged on the team or flight assignment screen; 0 for
 * none. 5 functions write it: MissionSetup_TeamAssignmentUpdate,
 * MissionSetup_DrawUnassignedPlayers, MissionSetup_DrawTeamAssignments,
 * MissionSetup_FlightAssignmentUpdate and
 * MissionSetup_DrawFlightAssignments. */
// GLOBAL: XVT 0xAA5E38
int g_missionSetupDraggedPlayerId = 0;
/* Ids of the players someone has started to drag on the team or flight
 * assignment screen, which no one else may drag; the first
 * g_missionSetupReservedPlayerCount entries count. Written by the team and
 * flight assignment screens and their draw functions when a drag starts or a
 * reservation packet arrives, and by MissionBriefing_CraftSelectionUpdate from
 * the reservation packets; cleared when those screens start. */
// GLOBAL: XVT 0xAA5E40
int g_missionSetupReservedPlayerIds[8] = {0};
/* Entries in use in g_missionSetupReservedPlayerIds, 0 to 8; written beside
 * it. */
// GLOBAL: XVT 0xAA5E60
int g_missionSetupReservedPlayerCount = 0;
/* 1 when the team assignment screen went on without showing its teams (one team
 * with one player, single-slot melee teams, a solo Quick Start, a single 8-slot
 * training team, a solo combat engagement or battle, or a debriefing return); 0
 * once it shows them. The flight assignment and briefing screens read it to
 * offer their way back as a return to mission selection. Only
 * MissionSetup_TeamAssignmentUpdate writes it. */
// GLOBAL: XVT 0xAA5E64
int g_missionSetupTeamAssignmentSkipped = 0;
/* Teams with at least one player flight group in the loaded mission, 0 to 10;
 * the team loops take teams 0 to g_teamCount - 1. Only
 * MissionSetup_UpdateTeamCounts writes it. */
// GLOBAL: XVT 0xAA5E68
int g_teamCount = 0;
/* Heap array of 100 ship list entries that ShipList_Load fills from
 * frontres\frntspec.lst for the tech library and the briefing's craft screen;
 * NULL until loaded. ShipList_Load allocates it;
 * MissionBriefing_CraftSelectionExit, TechLibrary_Update,
 * Frontend_HandleCommonScreenControls and, in the modern build,
 * XvtFrontendTask_Shutdown free it and set it to NULL. */
// GLOBAL: XVT 0xAA60F4
ShipListEntry *g_shipList = NULL;
/* Index in g_shipList of each craft species' model. It starts with species 2 to
 * 8 at 1 to 7, 14 at 8, 16 at 9 and the rest at 0; ShipList_Load, its only
 * writer, sets the entry of each species under 17 that the list names. */
// GLOBAL: XVT 0x52C590
int g_shipTypeToShipListIndex[18] = {0, 0, 1, 2, 3, 4, 5, 6, 7,
				     0, 0, 0, 0, 0, 8, 0, 9, 0};
/* Entries ShipList_Load kept in g_shipList; the tech library steps through that
 * many. Written by ShipList_Load when the list opens and, set to 0, by
 * XvtFrontendTask_Shutdown in the modern build. */
// GLOBAL: XVT 0xAA60FC
int g_shipCount = 0;
/* Index in g_frontendMission.flightGroups of the local player's flight group on
 * the briefing's craft screen. Only MissionSetup_InitCraftLoadout writes it. */
// GLOBAL: XVT 0xAA6104
int g_missionSetupSelectedFlightGroupIndex = 0;
/* The local player's craft choice among its flight group's own craft: 0 the
 * group's craft, n its optional craft n - 1. Written by
 * MissionSetup_InitCraftLoadout (0, then any stepping to the right side),
 * MissionSetup_UpdateCraftLoadout, and FrontendNet_ProcessNetworkPackets from
 * the host's CRAFT_LOADOUT when craft selection is host only. */
// GLOBAL: XVT 0xAA60F8
int g_missionSetupSelectedFlightGroupCraftOptionIndex = 0;
/* The local player's choice among the preset craft, an index into
 * g_presetCraftTypes (the option plus 5 for category 3); 0 for the flight
 * group's own craft. Written as
 * g_missionSetupSelectedFlightGroupCraftOptionIndex is. */
// GLOBAL: XVT 0xAA6100
int g_missionSetupSelectedPresetCraftOptionIndex = 0;
/* Preset craft choices the local player's flight group offers: 11 for preset
 * category 1, 6 for categories 2 and 3, 0 for categories 0 and 4. Only
 * MissionSetup_InitCraftLoadout writes it. */
// GLOBAL: XVT 0xAA6108
int g_missionSetupPresetCraftOptionCount = 0;
/* The local player's warhead choice: 0 the flight group's default, n its
 * optional warhead n - 1. Written by MissionSetup_InitCraftLoadout (0),
 * MissionSetup_UpdateCraftLoadout, and FrontendNet_ProcessNetworkPackets from
 * the host's CRAFT_LOADOUT when craft selection is host only. */
// GLOBAL: XVT 0xAA60D8
int g_missionSetupSelectedWarheadOptionIndex = 0;
/* The local player's beam weapon choice: 0 the flight group's default, n its
 * optional beam n - 1. Written as g_missionSetupSelectedWarheadOptionIndex
 * is. */
// GLOBAL: XVT 0xAA60E4
int g_missionSetupSelectedBeamOptionIndex = 0;
/* The local player's countermeasure choice: 0 the flight group's default, n its
 * optional countermeasure n - 1. Written as
 * g_missionSetupSelectedWarheadOptionIndex is. */
// GLOBAL: XVT 0xAA610C
int g_missionSetupSelectedCountermeasureOptionIndex = 0;
/* Waves of the local player's flight group, or of its chosen optional craft,
 * less one as the mission stores them; the craft screen shows it plus 1 when
 * craft waves are on their default. Written by MissionSetup_InitCraftLoadout,
 * MissionSetup_UpdateCraftLoadout, and FrontendNet_ProcessNetworkPackets from
 * the host's CRAFT_LOADOUT when craft selection is host only. */
// GLOBAL: XVT 0xAA60E8
int g_missionSetupSelectedWaveCountMinusOne = 0;
/* Craft in the local player's flight group, or of its chosen optional craft,
 * from the mission. Written as g_missionSetupSelectedWaveCountMinusOne is. */
// GLOBAL: XVT 0xAA60EC
int g_missionSetupSelectedCraftCount = 0;
/* Craft choices the local player's flight group offers from its own list: 1 for
 * preset category 0, 1 plus its optional craft for category 4, 0 for the preset
 * categories 1 to 3. Only MissionSetup_InitCraftLoadout writes it. */
// GLOBAL: XVT 0xAA60F0
int g_missionSetupFlightGroupCraftOptionCount = 0;
/* Warhead choices the local player's flight group offers: its nonzero optional
 * warheads, plus 1 when it has any or a default warhead. Only
 * MissionSetup_InitCraftLoadout writes it. */
// GLOBAL: XVT 0xAA60D4
int g_missionSetupWarheadOptionCount = 0;
/* Beam weapon choices the local player's flight group offers: its nonzero
 * optional beams, plus 1 when it has any or a default beam. Only
 * MissionSetup_InitCraftLoadout writes it. */
// GLOBAL: XVT 0xAA60DC
int g_missionSetupBeamOptionCount = 0;
/* Countermeasure choices the local player's flight group offers: its nonzero
 * optional countermeasures, plus 1 when it has any or a default one. Only
 * MissionSetup_InitCraftLoadout writes it. */
// GLOBAL: XVT 0xAA60E0
int g_missionSetupCountermeasureOptionCount = 0;
/* Heap array of the current mission type's list entries, g_missionCount of
 * them; NULL when none is loaded. MissionSetup_LoadMissionList frees and
 * reloads it, and many screens' exit functions free it and set it to NULL.
 * MissionSetup_BattleChoice_BuildList swaps it out for a moment, and the pilot
 * record takes a loaded list over as g_battleMissionList. */
// GLOBAL: XVT 0xAA6114
MissionListEntry *g_missionList = NULL;
/* Index in g_missionList of the selected mission, the entry whose missionIdx is
 * the current type's selected description id, or g_missionCount when none is.
 * Many functions write it, chiefly FrontendMission_LoadCurrent and the mission
 * pickers of the mission setup screens. */
// GLOBAL: XVT 0xAA6134
int g_selectedMissionListIndex = 0;
/* 1 while a game is under way that leaving would abort: the mission setup and
 * common screen controls then ask before leaving. MissionSetup_DrawPlayerRoster
 * sets it to 1 when more than one player is ready and to 0 otherwise, and
 * MissionSetup_FlightAssignmentUpdate to 1 in a solo game; screens such as the
 * concourse, debriefing, join and host screens set it to 0. */
// GLOBAL: XVT 0xAA613C
int g_frontendGameSessionInProgress = 0;
/* 1 on the host of a network game and in a solo game, 0 on a client. Many
 * functions write it, chiefly the host and join screens, the concourse, the
 * flight loading screen, and MissionSetup_EnterNextMission and
 * MissionSetup_EnterCurrentMission, which set 1 in a solo game. */
// GLOBAL: XVT 0xAA6144
int g_missionSetupIsHost = 0;
/* 1 once a mission is starting: while it is set the lobby packets send the
 * eight g_mpRoster entries as they are, after clearing departed players,
 * instead of the network roster's ready players. MissionSetup_Update sets it to
 * 1 on the host's Begin and when the host's mission start arrives, and
 * MissionSetup_EnterNextMission and MissionSetup_EnterCurrentMission outside a
 * solo game. MissionSetup_Update's frame 0 and many other screens set it to
 * 0. */
// GLOBAL: XVT 0xB6A2A8
int g_missionSetupRosterAuthoritative = 0;
/* Entries in g_missionList. Only MissionSetup_LoadMissionList, which sets it to
 * 0 before loading, and MissionSetup_BattleChoice_BuildList, which puts it back
 * after loading the battle list, write it. */
// GLOBAL: XVT 0xAA6138
unsigned int g_missionCount = 0;
/* The game's players as the mission setup screens show them: up to 8 entries,
 * each a name, an id (0 for an empty entry), a rating and the loadout choices.
 * Many functions write it, chiefly FrontendNet_ProcessNetworkPackets from the
 * host's lobby and loadout packets, the lobby senders, which clear departed
 * players, and in a solo game MissionSetup_Update and the Enter functions,
 * which put the pilot in entry 0. */
// GLOBAL: XVT 0xAA6150
MpRosterEntry g_mpRoster[8] = {{0}};
/* Entries in g_battleMissionList. Set by MissionSetup_BattleChoice_BuildList
 * and PilotRecord_DrawMissionAchievementsPage, and to 0 by
 * MissionSetup_BattleChoice_Exit. */
// GLOBAL: XVT 0xB6A244
int g_battleMissionListCount = 0;
/* Index in g_pilotData.networkPlayers of the local player's entry. Written by
 * FrontendMission_InitPlayerState and MissionSetup_PruneDisconnectedPlayers,
 * which leaves it as it was when no entry holds the local id. */
// GLOBAL: XVT 0xB6A24C
int g_localPilotNetworkPlayerIndex = 0;
/* Heap array of mission list entries: on the battle choice screen, the missions
 * of the current battle (MissionSetup_BattleChoice_BuildList); on the pilot
 * record's achievements page, the battle list. NULL when none.
 * MissionSetup_BattleChoice_Exit, Concourse_Exit and
 * PilotRecord_DrawMissionAchievementsPage free it and set it to NULL. */
// GLOBAL: XVT 0xB6A2B4
MissionListEntry *g_battleMissionList = NULL;
/* GetTickCount() at the battle choice countdown's latest frame, in ms. Only
 * MissionSetup_BattleChoice_Update writes it. */
// GLOBAL: XVT 0x66D880
int g_battleChoiceClockMs = 0;
/* First row the battle choice screen's mission list shows. Only
 * MissionSetup_BattleChoice_DrawList writes it: on its frame 0, and from its
 * scrollbar. */
// GLOBAL: XVT 0x66D884
int g_battleChoiceScrollOffset = 0;
/* Milliseconds left to choose the next battle mission: 120000 on the battle
 * choice screen's frame 0, lowered each frame by the time elapsed and to a
 * host's BRIEFING_COUNTDOWN value when that is lower, and held at 0 once under
 * 0. Only MissionSetup_BattleChoice_Update writes it. */
// GLOBAL: XVT 0x66D888
int g_battleChoiceRemainingMs = 0;
/* GetTickCount() at the battle choice countdown's previous frame, in ms; the
 * difference to g_battleChoiceClockMs is the time elapsed. Only
 * MissionSetup_BattleChoice_Update writes it. */
// GLOBAL: XVT 0x66D88C
int g_battleChoicePreviousClockMs = 0;
/* Rows of the battle choice screen's mission list: available entries plus
 * section headings. Only MissionSetup_BattleChoice_DrawList writes it, on its
 * frame 0. */
// GLOBAL: XVT 0x66D890
int g_battleChoiceRowCount = 0;
/* Whole seconds left when the host last sent BRIEFING_COUNTDOWN; 120 on the
 * battle choice screen's frame 0. Only MissionSetup_BattleChoice_Update writes
 * it. */
// GLOBAL: XVT 0x66D894
int g_battleChoiceLastSentSecond = 0;
/* 1 once the battle choice countdown has run out and been handled, so the
 * captain's choice is sent once; 0 on the screen's frame 0. Only
 * MissionSetup_BattleChoice_Update writes it. */
// GLOBAL: XVT 0x66D898
int g_battleChoiceTimeoutHandled = 0;
/* 1 while the mission setup screens use g_pilotData.factionStatistics[2], the
 * mission state of games outside solo play, rather than the current faction's
 * entry. MissionSetup_Update, its only writer, sets it on frame 0: 1 outside a
 * solo game, 0 in one. MissionSetup_Exit saves the state back to the entry it
 * names. */
// GLOBAL: XVT 0x665D24
int g_missionSetupUseCombatSimPilotState = 0;
/* Panel the network mission setup screen shows below the description: the
 * player roster or the game settings. MissionSetup_Update sets the roster on
 * frame 0, and MissionSetup_DrawMissionTypeControls's Players and Settings
 * buttons switch it. */
// GLOBAL: XVT 0x665D10
MissionSetupActivePanel g_missionSetupActivePanel = MISSION_SETUP_PANEL_PLAYERS;
/* GetTickCount() when the host last sent the ready roster and lobby selection,
 * in ms. MissionSetup_Update, its only writer, sets it on frame 0 and after
 * each send. */
// GLOBAL: XVT 0x665D14
int g_missionSetupLastHostBroadcastMs = 0;
/* Rows of the mission setup screen's mission list: available missions plus
 * section headings. Only MissionSetup_DrawMissionList writes it, on its frame
 * 0. */
// GLOBAL: XVT 0x665D1C
int g_missionSetupMissionListRowCount = 0;
/* First row the mission setup screen's mission list shows. Only
 * MissionSetup_DrawMissionList writes it: on its frame 0, and from its
 * scrollbar. */
// GLOBAL: XVT 0x665D20
int g_missionSetupMissionListScrollOffset = 0;
/* Index in g_mpRoster of the player the host selected in the roster, whom the
 * boot button removes; -1 for none. MissionSetup_Update sets -1 on frame 0 and
 * on a lobby state, MissionSetup_DrawMissionList on a lobby state, and
 * MissionSetup_DrawPlayerRoster sets or clears it on a click. */
// GLOBAL: XVT 0x665D18
int g_missionSetupSelectedPlayerRosterIndex = 0;
/* On a network client, whether the host's selected battle has an active saved
 * continuation, from the host's BATTLE_PROGRESS packet
 * (FrontendNet_ProcessNetworkPackets). Set to 0 by MissionSetup_Update on frame
 * 0, and when a sequence starts without continuing by
 * MissionSetup_TeamAssignmentUpdate in the original build and
 * XvtCampaignTask_EnterTeams in the modern one. */
// GLOBAL: XVT 0xAA6120
int g_remoteBattleContinuationActive = 0;
/* On a network client, the host's continue choice from BATTLE_PROGRESS
 * (SEQUENCE_RESTART or SEQUENCE_CONTINUE); written and cleared as
 * g_remoteBattleContinuationActive is. */
// GLOBAL: XVT 0xAA6124
int g_remoteBattleSequenceContinuationChoice = 0;
/* On a network client, the current mission index of the host's saved battle,
 * from BATTLE_PROGRESS; written and cleared as g_remoteBattleContinuationActive
 * is. Nothing reads it. */
// GLOBAL: XVT 0xAA6128
int g_remoteBattleLastCompletedMissionIndex = 0;
/* On a network client, the Rebel victories of the host's saved battle, from
 * BATTLE_PROGRESS; written and cleared as g_remoteBattleContinuationActive
 * is. */
// GLOBAL: XVT 0xAA612C
int g_remoteBattleRebelVictoryCount = 0;
/* On a network client, the Imperial victories of the host's saved battle, from
 * BATTLE_PROGRESS; written and cleared as g_remoteBattleContinuationActive
 * is. */
// GLOBAL: XVT 0xAA6130
int g_remoteBattleImperialVictoryCount = 0;
/* 1 when a screen hands back to an earlier one that should keep its state
 * rather than start over: the team assignment screen then keeps and prunes its
 * assignments, the flight assignment screen keeps its flight groups, and the
 * join screen keeps its game list or, for a transport other than IPX, returns
 * to the concourse. Many functions write it, chiefly the mission setup,
 * briefing and debriefing screens when they go back (1), the team assignment
 * and join screens once they have read it (0), and the flight assignment
 * screen, which sets 1 after its own setup. */
// GLOBAL: XVT 0x52C184
int g_frontendSkipScreenEntrySetup = 0;
/* Frames before the host's Begin button on the mission setup screen shows
 * again: 240 after a Begin press, 24 when FrontendNet_ProcessNetworkPackets
 * admits a joining player. MissionSetup_Update sets it to 0 on frame 0 and
 * lowers it by 1 each frame. */
// GLOBAL: XVT 0xAA6140
int g_missionSetupBeginButtonLockoutFrames = 0;
/* 1 while the team assignment screen shows the mission description in place of
 * the team slots, a choice offered in combat engagement sequences. Set by
 * MissionSetup_UpdateTeamControls's buttons, and to 0 on the screen's frame 0
 * by MissionSetup_TeamAssignmentUpdate in the original build and
 * XvtCampaignTask_EnterTeams in the modern one. */
// GLOBAL: XVT 0x6691D0
int g_missionSetupShowDescriptionPanel = 0;
/* GetTickCount() at the flight assignment countdown's latest frame, in ms. Only
 * MissionSetup_FlightAssignmentUpdate writes it. */
// GLOBAL: XVT 0x6691D8
int g_missionSetupCountdownClockMs = 0;
/* Milliseconds left before the flight assignments are final: 120000 on the
 * flight assignment screen's frame 0, lowered each frame by the time elapsed
 * and to a countdown packet's value when that is lower, and held at 0 once
 * under 0. Only MissionSetup_FlightAssignmentUpdate writes it. */
// GLOBAL: XVT 0x669250
int g_missionSetupLaunchCountdownMs = 0;
/* GetTickCount() at the flight assignment countdown's previous frame, in ms;
 * the difference to g_missionSetupCountdownClockMs is the time elapsed. Only
 * MissionSetup_FlightAssignmentUpdate writes it. */
// GLOBAL: XVT 0x669254
int g_missionSetupCountdownPreviousClockMs = 0;
/* 1 once this player, as its team's captain, sent FLIGHT_ASSIGNMENTS_READY when
 * the flight assignment countdown ran out; 0 on the screen's frame 0. Only
 * MissionSetup_FlightAssignmentUpdate writes it. */
// GLOBAL: XVT 0x669788
int g_missionSetupLaunchSignalSent = 0;
/* Whole seconds left when the host last sent the flight assignment countdown;
 * 120 on the screen's frame 0. Only MissionSetup_FlightAssignmentUpdate writes
 * it. */
// GLOBAL: XVT 0x66978C
int g_missionSetupLastBroadcastCountdownSecond = 0;
/* 1 while the flight assignment screen shows the mission description and the
 * full flight slot list in place of the briefing map, for a team with more than
 * 4 player flight groups. Written by MissionSetup_FlightAssignmentUpdate on
 * frame 0 and by MissionSetup_DrawAssignmentControls's buttons. */
// GLOBAL: XVT 0x669790
int g_missionSetupUseExpandedAssignmentLayout = 0;
/* How the debriefing hands back to the mission setup screens: replaying the
 * current mission or going on to the next. MissionSetup_TeamAssignmentUpdate,
 * finding it set, clears it and goes straight to flight assignment; while it is
 * ENTER_CURRENT_MISSION no saved campaign or battle is offered on entry.
 * Written by MissionDebrief_Update and MissionSetup_TeamAssignmentUpdate. */
// GLOBAL: XVT 0xA91B8C
MissionSetupDebriefTransition g_missionSetupDebriefTransition =
	MISSION_SETUP_DEBRIEF_TRANSITION_NONE;

/* The mission setup screen's exit callback. Frees g_missionList and
 * g_missionText and sets both to NULL, frees the "background" image, and saves
 * the pilot's team, mission type, selected mission per type and sequence fields
 * back into g_pilotData.factionStatistics: entry 2 while
 * g_missionSetupUseCombatSimPilotState is set, else the current faction's
 * entry. Then resets the scrollable controls and clears the mouse input gate.
 * Ignores frameCounter and returns 0. */
// FUNCTION: XVT 0x4E1470
int MissionSetup_Exit(int frameCounter)
{
	(void)frameCounter;

	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_missionText != NULL) {
		free(g_missionText);
		g_missionText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	if (g_missionSetupUseCombatSimPilotState) {
		g_pilotData.factionStatistics[2].team = g_pilotData.team;
		g_pilotData.factionStatistics[2].missionDirectoryId =
			g_pilotData.missionDirectoryId;
		memcpy(g_pilotData.factionStatistics[2].missionDescriptionIds,
		       g_pilotData.missionDescriptionIds,
		       sizeof(g_pilotData.factionStatistics[2]
				      .missionDescriptionIds));
		g_pilotData.factionStatistics[2].missionSequenceActive =
			g_pilotData.missionSequenceActive;
		g_pilotData.factionStatistics[2].savedMissionDescriptionId =
			g_pilotData.savedMissionDescriptionId;
	} else {
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.team = g_pilotData.team;
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.missionDirectoryId = g_pilotData.missionDirectoryId;
		memcpy(g_pilotData
			       .factionStatistics[g_pilotData.currentFactionId]
			       .missionDescriptionIds,
		       g_pilotData.missionDescriptionIds,
		       sizeof(g_pilotData
				      .factionStatistics
					      [g_pilotData.currentFactionId]
				      .missionDescriptionIds));
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.missionSequenceActive =
			g_pilotData.missionSequenceActive;
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.savedMissionDescriptionId =
			g_pilotData.savedMissionDescriptionId;
	}
	Frontend_ResetScrollableControls();
	FrontendMouse_ClearInputGate();
	return 0;
}

/* The mission setup screen, run once per frame: the player picks a mission type
 * and a mission, a network game's players gather, and Begin moves on to
 * MissionSetup_TeamAssignmentUpdate. Below, "outside a solo game" means
 * g_frontendMissionSessionMode is not FRONTEND_MISSION_SESSION_SINGLEPLAYER. On
 * frame 0 it resets the screen: it compacts g_mpRoster; clears
 * g_frontendBriefingEnteredCount, g_missionSetupBeginButtonLockoutFrames and
 * the five g_remoteBattle globals; loads the pilot's mission state from
 * g_pilotData.factionStatistics, entry 2 outside a solo game (setting
 * g_missionSetupUseCombatSimPilotState to 1), else the current faction's entry
 * (setting it to 0); allocates the 4096-byte g_missionText; and clears the
 * three sequence states. A sequence left active (missionSequenceActive 1) puts
 * savedMissionDescriptionId back as the current type's selected mission and
 * moves the mission type from melee to tournament, from combat engagement to
 * battle, and from any other type to campaign; missionSequenceActive is then
 * cleared. It loads the mission, its text and team counts, clamps the
 * g_gameConfig settings the mission type does not allow, takes a saved battle's
 * length and setup choice or a saved campaign's setup choice, and clears the
 * eight network player slots and g_mpRosterReadyFlags; a solo game fills roster
 * entry 0 from the pilot. Every frame outside a solo game, the host sends the
 * ready roster and lobby selection when more than 5000 ms have passed since
 * g_missionSetupLastHostBroadcastMs, and the packet type
 * FrontendNet_ProcessNetworkPackets returns is handled: a lobby state loads the
 * host's mission; a host cancel shuts the session down and leaves for the
 * concourse or the join screen; a mission start sets
 * g_missionSetupRosterAuthoritative and moves to team assignment; a kick sends
 * a leave packet, shuts the session down and leaves; a pilot rating updates its
 * sender's roster entry. Its branch for a PLAYER_UNAVAILABLE packet never runs:
 * FrontendNet_ProcessNetworkPackets returns NET_PACKET_NONE for one. Then it
 * draws the game name (outside a solo game), title, mission name, description,
 * chat panel, version and pilot, and the settings panel (always in a solo game)
 * or the player roster, and handles the Quick Start or Previous button, the
 * Begin button, the mission list button, the mission type controls and, for a
 * network host on the players panel, the button that boots the selected player.
 * In a solo game Begin and Quick Start move to team assignment, first picking a
 * tournament, battle or campaign's first mission and staying when
 * MissionSetup_SelectFirstSequenceMission returns 0; Begin sets
 * g_gameConfig.randomSeed before that pick, and Quick Start sets
 * g_frontendQuickStartLaunchFlag. A host's Begin is refused when the first
 * character of the mission's file name, read as a digit, is under the ready
 * player count; otherwise it picks a sequence's first mission likewise and
 * sends the lobby state and a mission start packet; either way it locks the
 * button for 240 frames. In the modern build each dialog returns 0 at once and
 * the action after it runs when the dialog closes. Returns 1 when
 * Frontend_HandleCommonScreenControls returns 1, the player having quit the
 * game; else 0. */
// FUNCTION: XVT 0x4E15D0
int MissionSetup_Update(int frameCounter)
{
	enum {
		NETWORK_PLAYER_COUNT = 8,
		COMBAT_SIM_FACTION = 2,
		BRIEFING_TEXT_SIZE = 4096,
		HOST_BROADCAST_INTERVAL_MS = 5000,
		BUTTON_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TEXT_COLOR_WHITE = 0xFFFF,
		HOVER_BEGIN = 7,
		HOVER_PREVIOUS = 8,
		HOVER_MISSION_LIST = 9,
		HOVER_BOOT_PLAYER = 10,
		BEGIN_BUTTON_LOCKOUT_FRAMES = 240,
		PACKET_SIZE_ONE_WORD = sizeof(int),
		PACKET_SIZE_MISSION_START = 5 * sizeof(int),
		RANDOM_SETUP_SEQUENTIAL = 0,
		RANDOM_SETUP_PLAYER_CHOICE = 2,
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
		MISSION_PLAYER_COUNT_CHARACTER_OFFSET = '0',
		ANIMATION_CYCLE_FRAMES = 32,
	};

	RECT rect;
	unsigned int nowMs;
	int packetType;
	int playerIndex;
	int missionTypeControlsChanged;
	int missionCount;
	int buttonPressed;

	if (frameCounter == 0) {
		FrontendCursor_SetPos(37, 445);
		MpRoster_CompactActiveEntries();
		g_remoteBattleContinuationActive = 0;
		g_frontendBriefingEnteredCount = 0;
		g_missionSetupBeginButtonLockoutFrames = 0;
		g_remoteBattleSequenceContinuationChoice = 0;
		g_remoteBattleLastCompletedMissionIndex = 0;
		g_remoteBattleRebelVictoryCount = 0;
		g_remoteBattleImperialVictoryCount = 0;

		if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_pilotData.team =
				g_pilotData
					.factionStatistics[COMBAT_SIM_FACTION]
					.team;
			g_pilotData.missionDirectoryId =
				g_pilotData
					.factionStatistics[COMBAT_SIM_FACTION]
					.missionDirectoryId;
			g_missionSetupUseCombatSimPilotState = 1;
			memcpy(g_pilotData.missionDescriptionIds,
			       g_pilotData.factionStatistics[COMBAT_SIM_FACTION]
				       .missionDescriptionIds,
			       sizeof(g_pilotData.missionDescriptionIds));
			g_pilotData.missionSequenceActive =
				g_pilotData
					.factionStatistics[COMBAT_SIM_FACTION]
					.missionSequenceActive;
			g_pilotData.factionStatistics[COMBAT_SIM_FACTION]
				.missionSequenceActive = 0;
			g_pilotData.savedMissionDescriptionId =
				g_pilotData
					.factionStatistics[COMBAT_SIM_FACTION]
					.savedMissionDescriptionId;
		} else {
			g_missionSetupUseCombatSimPilotState = 0;
			g_pilotData.team =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.team;
			g_pilotData.missionDirectoryId =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionDirectoryId;
			memcpy(g_pilotData.missionDescriptionIds,
			       g_pilotData
				       .factionStatistics
					       [g_pilotData.currentFactionId]
				       .missionDescriptionIds,
			       sizeof(g_pilotData.missionDescriptionIds));
			g_pilotData.missionSequenceActive =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionSequenceActive;
			g_pilotData
				.factionStatistics[g_pilotData.currentFactionId]
				.missionSequenceActive = 0;
			g_pilotData.savedMissionDescriptionId =
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.savedMissionDescriptionId;
		}

		g_configConnectionTypeEditable = 0;
		g_missionText = (char *)malloc(BRIEFING_TEXT_SIZE);
		g_frontendFirstVisibleLine = 0;
		g_missionSetupActivePanel = MISSION_SETUP_PANEL_PLAYERS;
		g_frontendQuickStartLaunchFlag = 0;
		g_missionSetupRosterAuthoritative = 0;
		g_missionSetupSelectedPlayerRosterIndex = -1;
		g_gameConfig.continueBattleOrCampaign = SEQUENCE_CONTINUE;
		g_missionSetupLastHostBroadcastMs = GetTickCount();
		g_selectedMissionListIndex = 0;
		if (g_missionList != NULL) {
			for (; (unsigned int)g_selectedMissionListIndex <
			       g_missionCount;
			     ++g_selectedMissionListIndex) {
				if (g_missionList[g_selectedMissionListIndex]
					    .missionIdx ==
				    g_pilotData.missionDescriptionIds
					    [g_pilotData.missionDirectoryId]) {
					break;
				}
			}
		}

		if (g_pilotData.missionSequenceActive == 1) {
			g_pilotData.missionDescriptionIds
				[g_pilotData.missionDirectoryId] =
				g_pilotData.savedMissionDescriptionId;
		}
		memset(&g_pilotData.meleeTournamentSequenceState, 0,
		       sizeof(g_pilotData.meleeTournamentSequenceState));
		memset(&g_pilotData.battleSequenceState, 0,
		       sizeof(g_pilotData.battleSequenceState));
		memset(&g_pilotData.campaignSequenceState, 0,
		       sizeof(g_pilotData.campaignSequenceState));
		if (g_pilotData.missionSequenceActive == 1) {
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_MELEES) {
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_TOURNAMENTS;
			} else if (g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_BATTLES;
			} else {
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_CAMPAIGNS;
			}
		}
		g_pilotData.missionSequenceActive = 0;
		FrontendMission_LoadCurrent();
		MissionSetup_LoadMissionDescText(g_missionText);
		MissionSetup_UpdateTeamCounts();

		if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    (g_pilotData.missionDirectoryId ==
			     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
		     g_pilotData.missionDirectoryId ==
			     MISSION_DIRECTORY_BATTLES) &&
		    g_gameConfig.craftWaves == CRAFT_WAVES_UNLIMITED) {
			g_gameConfig.craftWaves = CRAFT_WAVES_DEFAULT;
		}
		if (g_gameConfig.craftSelection == CRAFT_SELECTION_HOST_ONLY &&
		    (g_frontendMissionSessionMode ==
			     FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		     (g_pilotData.missionDirectoryId !=
			      MISSION_DIRECTORY_MELEES &&
		      g_pilotData.missionDirectoryId !=
			      MISSION_DIRECTORY_TOURNAMENTS))) {
			g_gameConfig.craftSelection = CRAFT_SELECTION_ON;
		}
		if (g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_CAMPAIGNS &&
		    g_gameConfig.difficulty == GAME_DIFFICULTY_EASY_CHEAT) {
			g_gameConfig.difficulty = GAME_DIFFICULTY_EASY;
		}
		if (g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_BATTLES &&
		    g_gameConfig.randomSetup == RANDOM_SETUP_PLAYER_CHOICE) {
			g_gameConfig.randomSetup = RANDOM_SETUP_SEQUENTIAL;
		}
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_BATTLES) {
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilotData
					    .spBattleContinuations
						    [g_missionList
							     [g_selectedMissionListIndex]
								     .missionIdx]
					    .isActive != 0) {
					g_gameConfig.battleLengthIndex =
						g_pilotData
							.spBattleContinuations
								[g_missionList[g_selectedMissionListIndex]
									 .missionIdx]
							.battleLengthIndex;
					g_gameConfig.randomSetup =
						g_pilotData
							.spBattleContinuations
								[g_missionList[g_selectedMissionListIndex]
									 .missionIdx]
							.randomSetup;
				}
			} else if (
				g_pilotData
					.mpBattleContinuations
						[g_missionList
							 [g_selectedMissionListIndex]
								 .missionIdx]
					.isActive != 0) {
				g_gameConfig.battleLengthIndex =
					g_pilotData
						.mpBattleContinuations
							[g_missionList
								 [g_selectedMissionListIndex]
									 .missionIdx]
						.battleLengthIndex;
				g_gameConfig.randomSetup =
					g_pilotData
						.mpBattleContinuations
							[g_missionList
								 [g_selectedMissionListIndex]
									 .missionIdx]
						.randomSetup;
			}
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_CAMPAIGNS) {
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilotData
					    .spCampaignContinuations
						    [g_missionList
							     [g_selectedMissionListIndex]
								     .missionIdx]
					    .isActive != 0) {
					g_gameConfig.randomSetup =
						g_pilotData
							.spCampaignContinuations
								[g_missionList[g_selectedMissionListIndex]
									 .missionIdx]
							.randomSetup;
				}
			} else if (Net_IsHost() != 0) {
				if (g_pilotData
					    .mpCampaignContinuations
						    [g_missionList
							     [g_selectedMissionListIndex]
								     .missionIdx]
					    .isActive != 0) {
					g_gameConfig.randomSetup =
						g_pilotData
							.mpCampaignContinuations
								[g_missionList[g_selectedMissionListIndex]
									 .missionIdx]
							.randomSetup;
				}
			} else if (
				g_pilotData
					.mpCampaignContinuations
						[g_missionList
							 [g_selectedMissionListIndex]
								 .missionIdx +
						 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
					.isActive != 0) {
				g_gameConfig.randomSetup =
					g_pilotData
						.mpCampaignContinuations
							[g_missionList[g_selectedMissionListIndex]
								 .missionIdx +
							 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
						.randomSetup;
			}
		}

		g_frontendSkipScreenEntrySetup = 0;
		for (playerIndex = 0; playerIndex < NETWORK_PLAYER_COUNT;
		     ++playerIndex) {
			memset(&g_pilotData.networkPlayers[playerIndex], 0,
			       sizeof(g_pilotData.networkPlayers[playerIndex]));
			g_pilotData.networkPlayers[playerIndex].craftId = 0;
			g_pilotData.networkPlayers[playerIndex].craftOption =
				-1;
			g_pilotData.networkPlayers[playerIndex].warheadOption =
				-1;
			g_pilotData.networkPlayers[playerIndex].beamOption = -1;
			g_pilotData.networkPlayers[playerIndex]
				.countermeasureOption = -1;
		}
		memset(g_mpRosterReadyFlags, 0, sizeof(g_mpRosterReadyFlags));
		if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			strcpy(g_mpRoster[0].name, g_pilotData.name);
			g_mpRoster[0].playerId = 1;
			g_mpRoster[0].pilotRating = g_pilotData.rating;
		}
		MissionSetup_DrawBackground();
		FrontendText_StartTextFadeIn(20);
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (Net_IsHost() != 0) {
			nowMs = GetTickCount();
			if (nowMs - (unsigned int)
					    g_missionSetupLastHostBroadcastMs >
			    HOST_BROADCAST_INTERVAL_MS) {
				MissionSetup_BroadcastReadyRoster(0);
				MissionSetup_BroadcastLobbySelection();
				g_missionSetupLastHostBroadcastMs = nowMs;
			}
		}

		packetType = FrontendNet_ProcessNetworkPackets();
		if (packetType == NET_PACKET_STATE) {
			if (g_pilotData.missionDirectoryId !=
				    g_frontendNetReceivedMissionDirectoryId ||
			    g_pilotData.missionDescriptionIds
					    [g_frontendNetReceivedMissionDirectoryId] !=
				    g_frontendNetReceivedMissionDescriptionId) {
				g_gameConfig.continueBattleOrCampaign =
					SEQUENCE_CONTINUE;
			}
			g_pilotData.missionDirectoryId =
				g_frontendNetReceivedMissionDirectoryId;
			g_pilotData.missionDescriptionIds
				[g_frontendNetReceivedMissionDirectoryId] =
				g_frontendNetReceivedMissionDescriptionId;
			FrontendMission_LoadCurrent();
			FrontImage_FreeResourceByName("background");
			MissionSetup_DrawBackground();
			MissionSetup_LoadMissionDescText(g_missionText);
			MissionSetup_UpdateTeamCounts();
			if (g_missionList != NULL) {
				for (g_selectedMissionListIndex = 0;
				     (unsigned int)g_selectedMissionListIndex <
				     g_missionCount;
				     ++g_selectedMissionListIndex) {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    g_pilotData.missionDescriptionIds
						    [g_pilotData
							     .missionDirectoryId]) {
						break;
					}
				}
			}
			g_missionSetupSelectedPlayerRosterIndex = -1;
			g_frontendFirstVisibleLine = 0;
			if (g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_BATTLES &&
			    g_gameConfig.randomSetup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
				g_gameConfig.randomSetup =
					RANDOM_SETUP_SEQUENTIAL;
			}
			if (g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_CAMPAIGNS &&
			    g_gameConfig.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
				g_gameConfig.difficulty = GAME_DIFFICULTY_EASY;
			}
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_CAMPAIGNS) {
				g_gameConfig.randomSetup =
					RANDOM_SETUP_SEQUENTIAL;
			}
			if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
			    (g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			     g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_BATTLES) &&
			    g_gameConfig.craftWaves == CRAFT_WAVES_UNLIMITED) {
				g_gameConfig.craftWaves = CRAFT_WAVES_DEFAULT;
			}
			if (g_gameConfig.craftSelection ==
			    CRAFT_SELECTION_HOST_ONLY) {
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_gameConfig.craftSelection =
						CRAFT_SELECTION_ON;
				} else if (
					g_pilotData.missionDirectoryId !=
						MISSION_DIRECTORY_MELEES &&
					g_pilotData.missionDirectoryId !=
						MISSION_DIRECTORY_TOURNAMENTS) {
					g_gameConfig.craftSelection =
						CRAFT_SELECTION_ON;
				}
			}
		} else if (packetType == NET_PACKET_HOST_CANCELLED) {
			Net_ShutdownDirectPlaySession();
			if (Net_IsHost() == 0) {
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					FrontendString_Get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					FrontendString_Get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(
					XvtMissionDialogs_Resume,
					XVT_MISSION_SETUP_CANCELLED);
#endif
			}
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			if (g_gameConfig.networkType != 0) {
				FrontendScreen_SetCallbacks(Concourse_Update,
							    Concourse_Exit);
			} else {
				FrontendScreen_SetCallbacks(
					FrontendNet_JoinGameScreen,
					(FrontendScreenExitFn)
						FrontendMissionList_FreeScreenResources);
			}
		} else if (packetType == NET_PACKET_PLAYER_UNAVAILABLE) {
			Net_ShutdownDirectPlaySession();
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_675_THERE_WAS_A_ERROR_WHILE_ATTEMPTING),
				FrontendString_Get(
					FRONTSTR_676_TO_CONNECT_PLEASE_TRY_AGAIN_OR),
				FrontendString_Get(
					FRONTSTR_677_SELECT_ANOTHER_GAME),
				NULL, NULL);
#ifdef XVT_MODERN
			return XvtDialog_ContinueWith(
				XvtMissionDialogs_Resume,
				XVT_MISSION_SETUP_CANCELLED);
#endif
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			if (g_gameConfig.networkType != 0) {
				FrontendScreen_SetCallbacks(Concourse_Update,
							    Concourse_Exit);
			} else {
				FrontendScreen_SetCallbacks(
					FrontendNet_JoinGameScreen,
					(FrontendScreenExitFn)
						FrontendMissionList_FreeScreenResources);
			}
		} else if (packetType == NET_PACKET_FRONTEND_MISSION_START) {
			if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
			    (g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			     g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_BATTLES) &&
			    g_gameConfig.craftWaves == CRAFT_WAVES_UNLIMITED) {
				g_gameConfig.craftWaves = CRAFT_WAVES_DEFAULT;
			}
			if (g_gameConfig.craftSelection ==
				    CRAFT_SELECTION_HOST_ONLY &&
			    g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_MELEES &&
			    g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_TOURNAMENTS) {
				g_gameConfig.craftSelection =
					CRAFT_SELECTION_ON;
			}
			if (g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
			    g_pilotData.missionSequenceActive == 1) {
				g_pilotData.battleSequenceState
					.missionListIndices[0] =
					g_selectedMissionListIndex;
				g_pilotData.battleSequenceState
					.missionOrdinals[0] =
					g_selectedMissionListIndex;
				if (g_missionList != NULL) {
					g_pilotData.battleSequenceState
						.currentMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
				}
			}
			g_missionSetupRosterAuthoritative = 1;
			FrontendScreen_SetCallbacks(
				MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
				XvtFrontendCleanup_MissionResources
#else
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
			);
			return 0;
		} else if (packetType == NET_PACKET_PLAYER_KICKED) {
			g_frontendSkipScreenEntrySetup = 1;
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_PLAYER_LEFT;
			Net_SendPacketAndFlush(Net_GetHostPlayerId(),
					       &g_frontendNetPacketScratch,
					       PACKET_SIZE_ONE_WORD);
			Net_ShutdownDirectPlaySession();
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_562_YOU_HAVE_BEEN_BOOTED_BY_THE_HOST),
				FrontendString_Get(
					FRONTSTR_563_PLEASE_CHOOSE_ANOTHER),
				FrontendString_Get(FRONTSTR_564_GAME_TO_JOIN),
				NULL, NULL);
#ifdef XVT_MODERN
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
						      XVT_MISSION_SETUP_BOOTED);
#endif
			if (g_gameConfig.networkType != 0) {
				FrontendScreen_SetCallbacks(Concourse_Update,
							    Concourse_Exit);
			} else {
				FrontendScreen_SetCallbacks(
					FrontendNet_JoinGameScreen,
					(FrontendScreenExitFn)
						FrontendMissionList_FreeScreenResources);
			}
			return 0;
		} else if (packetType == NET_PACKET_PILOT_RATING) {
			for (playerIndex = 0;
			     playerIndex < NETWORK_PLAYER_COUNT;
			     ++playerIndex) {
				if (g_mpRoster[playerIndex].playerId ==
				    g_frontendNetPacketSenderPlayerId) {
					g_mpRoster[playerIndex].pilotRating =
						g_frontendNetPacketArg0;
					break;
				}
			}
		}
	}
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
		FrontendText_DrawCentered(BUTTON_FONT_SIZE,
					  g_pilotData.multiplayerGameName,
					  &rect, TEXT_COLOR_WHITE);
	}

	sprintf(g_frontendScratchBuffer, "%s %c%s",
		FrontendString_Get(FRONTSTR_193_SELECT_MISSION), 4,
		FrontendString_Get(
			(FrontendStringId)(g_pilotData.missionDirectoryId +
					   FRONTSTR_194_TRAINING_EXERCISES)));
	FrontendDraw_RectAssign(&rect, 84, 90, 434, 108);
	FrontendText_DrawCentered(TITLE_FONT_SIZE, g_frontendScratchBuffer,
				  &rect, TEXT_COLOR_WHITE);
	if (Net_IsHost() != 0 ||
	    g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 84, 112, 416, 130);
	} else {
		FrontendDraw_RectAssign(&rect, 84, 112, 434, 130);
	}
	if ((unsigned int)g_selectedMissionListIndex < g_missionCount) {
		FrontendDraw_RectInsetXY(&rect, 4, 0);
		FrontendText_DrawAlignedInRect(
			TITLE_FONT_SIZE,
			g_missionList[g_selectedMissionListIndex].description,
			&rect, 0, 1, g_colorYellow);
	}
	MissionSetup_DrawMissionDescription();
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendNet_UpdateAndDrawChatPanel(frameCounter);
	}
	FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
	sprintf(g_frontendScratchBuffer, "v. %d.%d", 2, 0);
	FrontendText_DrawCentered(BUTTON_FONT_SIZE, g_frontendScratchBuffer,
				  &rect, TEXT_COLOR_WHITE);
	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
			g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(BUTTON_FONT_SIZE,
					  g_frontendScratchBuffer, &rect,
					  g_colorYellow);
		if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_MELEES &&
		    g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_TOURNAMENTS) {
			if (g_pilotData.currentFactionId == 0) {
				sprintf(g_frontendScratchBuffer, "rebtiny%d",
					(frameCounter %
					 ANIMATION_CYCLE_FRAMES) >>
						1);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      204, 453);
			} else {
				sprintf(g_frontendScratchBuffer, "imptiny%d",
					(frameCounter %
					 ANIMATION_CYCLE_FRAMES) >>
						1);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      204, 453);
			}
		} else {
			sprintf(g_frontendScratchBuffer, "rebtiny%d",
				(frameCounter % ANIMATION_CYCLE_FRAMES) >> 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 204,
					      453);
			sprintf(g_frontendScratchBuffer, "imptiny%d",
				(frameCounter % ANIMATION_CYCLE_FRAMES) >> 1);
		}
		FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
	}

	if (g_missionSetupActivePanel == MISSION_SETUP_PANEL_SETTINGS ||
	    g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		MissionSetup_DrawGameSettings();
	} else {
		MissionSetup_DrawPlayerRoster(frameCounter);
	}

	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_659_QUICK_START));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "quickflyup", "quickflydown",
			    FrontendString_Get(FRONTSTR_659_QUICK_START),
			    BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS,
			    "buttonsound") != 0) {
			if ((g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_TOURNAMENTS ||
			     g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_BATTLES ||
			     g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_CAMPAIGNS) &&
			    MissionSetup_SelectFirstSequenceMission() == 0) {
				FrontendButton_DisableOverlayText();
				return 0;
			}
			g_frontendQuickStartLaunchFlag = 1;
			FrontendScreen_SetCallbacks(
				MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
				XvtFrontendCleanup_MissionResources
#else
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
			);
			FrontendButton_DisableOverlayText();
			return 0;
		}
	} else if (Net_IsHost() != 0) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_569_PREVIOUS));
		buttonPressed = FrontendButton_HandleSpriteButton(
			&rect, "leaveup", "leavedown",
			FrontendString_Get(
				FRONTSTR_258_RETURN_TO_PILOT_RECORDS),
			BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS, "buttonsound");
		if (buttonPressed != 0) {
			if (g_frontendGameSessionInProgress != 0 &&
			    g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				buttonPressed = FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(
						FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
					FrontendString_Get(
						FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
					FrontendString_Get(
						FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
					FrontendString_Get(FRONTSTR_523_OKAY),
					FrontendString_Get(
						FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(
					XvtMissionDialogs_Resume,
					XVT_MISSION_SETUP_HOST_LEAVE);
#endif
			}
			if (buttonPressed != 0) {
				g_frontendSkipScreenEntrySetup = 1;
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_HOST_CANCELLED;
				Net_SendPacketAndFlush(
					0, &g_frontendNetPacketScratch,
					PACKET_SIZE_ONE_WORD);
				Net_ShutdownDirectPlaySession();
				g_frontendMissionSessionMode =
					FRONTEND_MISSION_SESSION_NONE;
				FrontendScreen_SetCallbacks(Concourse_Update,
							    Concourse_Exit);
			}
		}
	} else {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_569_PREVIOUS));

#ifdef XVT_MODERN
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_259_RETURN_TO_JOIN_GAME),
			    BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS,
			    "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				FrontendString_Get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				FrontendString_Get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				FrontendString_Get(FRONTSTR_523_OKAY),
				FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
						      XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_259_RETURN_TO_JOIN_GAME),
			    BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS,
			    "buttonsound") != 0 &&
		    FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    FrontendString_Get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    FrontendString_Get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendSkipScreenEntrySetup = 1;
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_PLAYER_LEFT;
			Net_SendPacketAndFlush(Net_GetHostPlayerId(),
					       &g_frontendNetPacketScratch,
					       PACKET_SIZE_ONE_WORD);
			Net_ShutdownDirectPlaySession();
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResources);
		}
#endif
	}

	if ((unsigned int)g_missionSetupBeginButtonLockoutFrames > 0) {
		--g_missionSetupBeginButtonLockoutFrames;
	}
	FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_705_BEGIN));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "nextup", "nextdown",
			    FrontendString_Get(FRONTSTR_705_BEGIN),
			    BUTTON_FONT_SIZE, 0, HOVER_BEGIN,
			    "flysound") != 0) {
			if (g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_TOURNAMENTS ||
			    g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_BATTLES ||
			    g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_CAMPAIGNS) {
				g_gameConfig.randomSeed = GetTickCount();
				if (MissionSetup_SelectFirstSequenceMission() ==
				    0) {
					FrontendButton_DisableOverlayText();
					return 0;
				}
			}
			FrontendScreen_SetCallbacks(
				MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
				XvtFrontendCleanup_MissionResources
#else
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
			);
			FrontendButton_DisableOverlayText();
			return 0;
		}
	} else if (Net_IsHost() != 0 &&
		   g_missionSetupBeginButtonLockoutFrames == 0) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_705_BEGIN));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "nextup", "nextdown",
			    FrontendString_Get(FRONTSTR_705_BEGIN),
			    BUTTON_FONT_SIZE, 0, HOVER_BEGIN,
			    "flysound") != 0) {
			g_missionSetupBeginButtonLockoutFrames =
				BEGIN_BUTTON_LOCKOUT_FRAMES;
			if ((int)(uint8_t)g_missionList
					    [g_selectedMissionListIndex]
						    .fileName[0] -
				    MISSION_PLAYER_COUNT_CHARACTER_OFFSET <
			    Net_CountReadyPlayers()) {
				if (g_missionList[g_selectedMissionListIndex]
					    .fileName[0] == '1') {
					FrontendDialog_ShowConfirmDialog(
						FrontendString_Get(
							FRONTSTR_637_YOU_HAVE_SELECTED_A_SINGLE_PLAYER_MISSION),
						FrontendString_Get(
							FRONTSTR_638_FOR_A_MULTIPLAYER_GAME),
						FrontendString_Get(
							FRONTSTR_639_PLEASE_SELECT_A_MULTIPLAYER_MISSION),
						NULL, NULL);
#ifdef XVT_MODERN
					return XvtDialog_ContinueWith(
						XvtMissionDialogs_Resume,
						XVT_MISSION_NOTICE);
#endif
				} else {
					sprintf(g_frontendScratchBuffer,
						FrontendString_Get(
							FRONTSTR_532_THIS_MISSION_ONLY_SUPPORTS_PERCENT_D_PLAYERS),
						(int)(uint8_t)g_missionList
								[g_selectedMissionListIndex]
									.fileName
										[0] -
							MISSION_PLAYER_COUNT_CHARACTER_OFFSET);
					FrontendDialog_ShowConfirmDialog(
						g_frontendScratchBuffer,
						FrontendString_Get(
							FRONTSTR_533_REMOVE_SOME_PLAYERS_BEFORE_CONTINUING),
						NULL, NULL, NULL);
#ifdef XVT_MODERN
					return XvtDialog_ContinueWith(
						XvtMissionDialogs_Resume,
						XVT_MISSION_NOTICE);
#endif
				}
			} else {
				if ((g_pilotData.missionDirectoryId ==
					     MISSION_DIRECTORY_TOURNAMENTS ||
				     g_pilotData.missionDirectoryId ==
					     MISSION_DIRECTORY_BATTLES ||
				     g_pilotData.missionDirectoryId ==
					     MISSION_DIRECTORY_CAMPAIGNS) &&
				    MissionSetup_SelectFirstSequenceMission() ==
					    0) {
					FrontendButton_DisableOverlayText();
					return 0;
				}
				MissionSetup_SendLobbyState(0);
				*(int *)&g_frontendNetPacketScratch.payload[4] =
					g_pilotData.missionDirectoryId;
				/* missionCount first carries the selected mission's description id (payload word 0); below
				 * it carries the sequence length, which for combat engagements is the number of victories
				 * needed. */
				missionCount =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				*(int *)&g_frontendNetPacketScratch.payload[0] =
					missionCount;
				*(int *)&g_frontendNetPacketScratch.payload[8] =
					g_pilotData.missionSequenceActive;
				g_missionSetupRosterAuthoritative = 1;
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_FRONTEND_MISSION_START;
				if (g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_MELEES) {
					missionCount =
						g_pilotData
							.meleeTournamentSequenceState
							.missionCount;
				} else if (
					g_pilotData.missionDirectoryId ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					missionCount =
						g_pilotData.battleSequenceState
							.victoriesNeeded;
				} else {
					missionCount =
						g_pilotData
							.campaignSequenceState
							.missionCount;
				}
				*(int *)&g_frontendNetPacketScratch
					 .payload[12] = missionCount;
				Net_SendPacketAndFlush(
					0, &g_frontendNetPacketScratch,
					PACKET_SIZE_MISSION_START);
			}
		}
	}

	FrontendButton_DisableOverlayText();
	if (Net_IsHost() != 0 ||
	    g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 417, 112, 434, 130);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "dropbtnup", "dropbtndown",
			    FrontendString_Get(FRONTSTR_684_MISSION_LIST),
			    TITLE_FONT_SIZE, TEXT_COLOR_WHITE,
			    HOVER_MISSION_LIST, "buttonsound") != 0) {
			FrontendDraw_RectAssign(&rect, 0, 0, 639, 479);
			FrontendScreen_QueuePush(MissionSetup_DrawMissionList,
						 &rect);
		}
	}

	missionTypeControlsChanged = MissionSetup_DrawMissionTypeControls();
	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    Net_IsHost() != 0 && missionTypeControlsChanged != 0) {
		MissionSetup_SendLobbyState(0);
	}
	if (Frontend_HandleCommonScreenControls(1) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (XvtDialog_IsActive()) {
		return 0;
	}
#endif
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NET_HOST &&
	    Net_IsHost() != 0 &&
	    g_missionSetupActivePanel == MISSION_SETUP_PANEL_PLAYERS) {
		FrontendDraw_RectAssign(&rect, 417, 309, 432, 336);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "bootu", "bootd",
			    FrontendString_Get(
				    FRONTSTR_531_REMOVE_PLAYER_FROM_GAME),
			    BUTTON_FONT_SIZE, 0, HOVER_BOOT_PLAYER,
			    "buttonsound") != 0 &&
		    g_missionSetupSelectedPlayerRosterIndex != -1) {
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_PLAYER_KICKED;
			Net_SendPacketAndFlush(
				g_mpRoster
					[g_missionSetupSelectedPlayerRosterIndex]
						.playerId,
				&g_frontendNetPacketScratch,
				PACKET_SIZE_ONE_WORD);
			Net_ClearPlayerReadyFlagWithLockGuard(
				g_mpRoster
					[g_missionSetupSelectedPlayerRosterIndex]
						.playerId);
			MissionSetup_SendLobbyState(0);
		}
	}
	return 0;
}

/* Draws the mission type buttons on the mission setup screen and handles clicks
 * on them; called each frame by MissionSetup_Update. The eight navigation
 * lights show the selected mission type and, in a network game, the Players or
 * Settings panel, or in a solo game the pilot's faction. In a solo game the
 * Rebel and Imperial pilot buttons save the current faction's mission state
 * into g_pilotData.factionStatistics and load the other faction's, keeping the
 * mission type and, when the new list holds it, the selected mission. In a
 * network game the Players and Settings buttons set g_missionSetupActivePanel.
 * For the host or a solo player, a click on an unselected mission type button
 * sets g_pilotData.missionDirectoryId, loads that type's mission and its text,
 * and selects the first available mission when the stored one is marked
 * unavailable (for campaigns, also when it is missing); a right click on the
 * selected button steps g_selectedMissionListIndex back to the previous
 * available mission and a left click forward to the next, wrapping, and a
 * network host then calls MissionSetup_SendLobbyState. A new type also sets
 * g_gameConfig.continueBattleOrCampaign to SEQUENCE_CONTINUE and clears the
 * settings it does not allow: easy cheat difficulty (all types but campaign),
 * player choice setup (all but battle; campaign sets sequential setup outright)
 * and, outside a solo game, unlimited waves (battle and combat engagement).
 * Outside a solo game it also sets g_gameConfig.missionTimeLimit to 255
 * (written as -1), which the settings show as Default, when it moves to
 * training, and when it moves between groups (melee and tournament; combat
 * engagement and battle; campaign), except from training to campaign. A battle
 * then takes its saved length and setup choice, a campaign its setup choice.
 * Returns 1 when the faction, the panel or the mission type changed, or when a
 * step through the training missions changed the IFF of the mission's first
 * flight group with a player; else 0. Does not check that any mission is
 * available: a step loops forever when every entry is unavailable. */
// FUNCTION: XVT 0x4E2970
int MissionSetup_DrawMissionTypeControls(void)
{
	enum {
		NAVIGATION_SLOT_COUNT = 8,
		PLAYERS_OR_REBEL_NAVIGATION_SLOT = 5,
		SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT = 6,
		CAMPAIGN_NAVIGATION_SLOT = 7,
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
		PILOT_FACTION_REBEL = 0,
		PILOT_FACTION_IMPERIAL = 1,
		RANDOM_SETUP_SEQUENTIAL = 0,
		RANDOM_SETUP_PLAYER_CHOICE = 2,
		HOVER_TRAINING = 11,
		HOVER_MELEE = 12,
		HOVER_TOURNAMENT = 13,
		HOVER_COMBAT_ENGAGEMENT = 14,
		HOVER_BATTLE = 15,
		HOVER_PLAYERS_OR_REBEL = 16,
		HOVER_SETTINGS_OR_IMPERIAL = 17,
		HOVER_CAMPAIGN = 18,
		BUTTON_LEFT = 22,
		BUTTON_RIGHT = 42,
		BUTTON_TOP = 334,
		BUTTON_BOTTOM = 358,
		CAMPAIGN_BUTTON_TOP = 254,
		CAMPAIGN_BUTTON_BOTTOM = 278,
		BATTLE_BUTTON_TOP = 226,
		BATTLE_BUTTON_BOTTOM = 250,
		BUTTON_SPACING = 28,
		BUTTON_FONT_SIZE = 12,
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
	};

	int changed = 0;
	int isHost;
	RECT rect;
	int previousMissionDirectoryId;
	int cursorY;
	int cursorX;
	int previousMissionDescriptionId;
	int navigationIndex;
	FrontendNavigationSlotState navigationSlotStates[NAVIGATION_SLOT_COUNT];
	int index;
	int selectedMissionId;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	navigationSlotStates[MISSION_DIRECTORY_TRAINING_EXERCISES] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigationSlotStates[MISSION_DIRECTORY_MELEES] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigationSlotStates[MISSION_DIRECTORY_TOURNAMENTS] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigationSlotStates[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigationSlotStates[MISSION_DIRECTORY_BATTLES] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	if (g_frontendMissionSessionMode == FRONTEND_MISSION_SESSION_NONE) {
		navigationSlotStates[PLAYERS_OR_REBEL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
		navigationSlotStates[SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
	} else {
		navigationSlotStates[PLAYERS_OR_REBEL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		navigationSlotStates[SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			navigationIndex = g_missionSetupActivePanel;
		} else {
			navigationIndex = g_pilotData.currentFactionId;
		}
		navigationSlotStates[PLAYERS_OR_REBEL_NAVIGATION_SLOT +
				     navigationIndex] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	navigationSlotStates[CAMPAIGN_NAVIGATION_SLOT] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS) {
		navigationSlotStates[CAMPAIGN_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else {
		navigationSlotStates[g_pilotData.missionDirectoryId] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	FrontendButton_DrawEightSlotNavigationState(navigationSlotStates);

	isHost = Net_IsHost();
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		isHost = 1;
	}
	FrontendDraw_RectAssign(&rect, BUTTON_LEFT, BUTTON_TOP, BUTTON_RIGHT,
				BUTTON_BOTTOM);
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_pilotData.currentFactionId != PILOT_FACTION_IMPERIAL) {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "reg7u", "reg7u",
				    FrontendString_Get(
					    FRONTSTR_643_IMPERIAL_PILOT),
				    BUTTON_FONT_SIZE, 0,
				    HOVER_SETTINGS_OR_IMPERIAL,
				    "jewelsound") != 0) {
				changed = 1;
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.team = g_pilotData.team;
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionDirectoryId =
					g_pilotData.missionDirectoryId;
				memcpy(g_pilotData
					       .factionStatistics
						       [g_pilotData
								.currentFactionId]
					       .missionDescriptionIds,
				       g_pilotData.missionDescriptionIds,
				       sizeof(g_pilotData
						      .factionStatistics
							      [g_pilotData
								       .currentFactionId]
						      .missionDescriptionIds));
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionSequenceActive =
					g_pilotData.missionSequenceActive;
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.savedMissionDescriptionId =
					g_pilotData.savedMissionDescriptionId;
				previousMissionDirectoryId =
					g_pilotData.missionDirectoryId;
				previousMissionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				g_pilotData.currentFactionId =
					PILOT_FACTION_IMPERIAL;
				g_pilotData.team =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.team;
				g_pilotData.missionDirectoryId =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.missionDirectoryId;
				memcpy(g_pilotData.missionDescriptionIds,
				       g_pilotData
					       .factionStatistics
						       [g_pilotData
								.currentFactionId]
					       .missionDescriptionIds,
				       sizeof(g_pilotData
						      .missionDescriptionIds));
				g_pilotData.missionSequenceActive =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.missionSequenceActive;
				g_pilotData.savedMissionDescriptionId =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.savedMissionDescriptionId;
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "reg7d",
				FrontendString_Get(FRONTSTR_643_IMPERIAL_PILOT),
				BUTTON_FONT_SIZE, 0);
		}
		FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
		if (g_pilotData.currentFactionId != PILOT_FACTION_REBEL) {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "reg6u", "reg6u",
				    FrontendString_Get(
					    FRONTSTR_642_REBEL_PILOT),
				    BUTTON_FONT_SIZE, 0, HOVER_PLAYERS_OR_REBEL,
				    "jewelsound") != 0) {
				changed = 1;
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.team = g_pilotData.team;
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionDirectoryId =
					g_pilotData.missionDirectoryId;
				memcpy(g_pilotData
					       .factionStatistics
						       [g_pilotData
								.currentFactionId]
					       .missionDescriptionIds,
				       g_pilotData.missionDescriptionIds,
				       sizeof(g_pilotData
						      .factionStatistics
							      [g_pilotData
								       .currentFactionId]
						      .missionDescriptionIds));
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.missionSequenceActive =
					g_pilotData.missionSequenceActive;
				g_pilotData
					.factionStatistics
						[g_pilotData.currentFactionId]
					.savedMissionDescriptionId =
					g_pilotData.savedMissionDescriptionId;
				previousMissionDirectoryId =
					g_pilotData.missionDirectoryId;
				previousMissionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				g_pilotData.currentFactionId =
					PILOT_FACTION_REBEL;
				g_pilotData.team =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.team;
				g_pilotData.missionDirectoryId =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.missionDirectoryId;
				memcpy(g_pilotData.missionDescriptionIds,
				       g_pilotData
					       .factionStatistics
						       [g_pilotData
								.currentFactionId]
					       .missionDescriptionIds,
				       sizeof(g_pilotData
						      .missionDescriptionIds));
				g_pilotData.missionSequenceActive =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.missionSequenceActive;
				g_pilotData.savedMissionDescriptionId =
					g_pilotData
						.factionStatistics
							[g_pilotData
								 .currentFactionId]
						.savedMissionDescriptionId;
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "reg6d",
				FrontendString_Get(FRONTSTR_642_REBEL_PILOT),
				BUTTON_FONT_SIZE, 0);
		}
		if (changed != 0) {
			if (g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_CAMPAIGNS &&
			    g_gameConfig.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
				g_gameConfig.difficulty = GAME_DIFFICULTY_EASY;
			}
			if (g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_BATTLES &&
			    g_gameConfig.randomSetup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
				g_gameConfig.randomSetup =
					RANDOM_SETUP_SEQUENTIAL;
			}
			g_pilotData.missionDirectoryId =
				previousMissionDirectoryId;
			MissionSetup_LoadMissionList(
				previousMissionDirectoryId);
			if (g_missionList != NULL) {
				for (g_selectedMissionListIndex = 0;
				     (unsigned int)g_selectedMissionListIndex <
				     g_missionCount;
				     ++g_selectedMissionListIndex) {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    previousMissionDescriptionId) {
						g_pilotData.missionDescriptionIds
							[g_pilotData
								 .missionDirectoryId] =
							previousMissionDescriptionId;
						break;
					}
				}
				if ((unsigned int)g_selectedMissionListIndex >=
				    g_missionCount) {
					for (g_selectedMissionListIndex = 0;
					     (unsigned int)
						     g_selectedMissionListIndex <
					     g_missionCount;
					     ++g_selectedMissionListIndex) {
						if (g_missionList
							    [g_selectedMissionListIndex]
								    .missionIdx ==
						    g_pilotData.missionDescriptionIds
							    [g_pilotData
								     .missionDirectoryId]) {
							break;
						}
					}
					if ((unsigned int)
						    g_selectedMissionListIndex >=
					    g_missionCount) {
						g_pilotData.missionDescriptionIds
							[g_pilotData
								 .missionDirectoryId] =
							g_missionList[0]
								.missionIdx;
					}
				}
			}
			FrontendMission_LoadCurrent();
			MissionSetup_UpdateTeamCounts();
			MissionSetup_LoadMissionDescText(g_missionText);
			g_frontendFirstVisibleLine = 0;
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_BATTLES) {
				if (g_pilotData
					    .spBattleContinuations
						    [g_missionList
							     [g_selectedMissionListIndex]
								     .missionIdx]
					    .isActive != 0) {
					g_gameConfig.battleLengthIndex =
						g_pilotData
							.spBattleContinuations
								[g_missionList[g_selectedMissionListIndex]
									 .missionIdx]
							.battleLengthIndex;
					g_gameConfig.randomSetup =
						g_pilotData
							.spBattleContinuations
								[g_missionList[g_selectedMissionListIndex]
									 .missionIdx]
							.randomSetup;
				}
			} else if (g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_CAMPAIGNS) {
				if (g_pilotData
					    .spCampaignContinuations
						    [g_missionList
							     [g_selectedMissionListIndex]
								     .missionIdx]
					    .isActive != 0) {
					g_gameConfig.randomSetup =
						g_pilotData
							.spCampaignContinuations
								[g_missionList[g_selectedMissionListIndex]
									 .missionIdx]
							.randomSetup;
				}
			}
		}
	} else {
		if (navigationSlotStates
			    [SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE) {
			if (g_missionSetupActivePanel !=
			    MISSION_SETUP_PANEL_SETTINGS) {
				if (FrontendButton_HandleSpriteButton(
					    &rect, "game7u", "game7u",
					    FrontendString_Get(
						    FRONTSTR_203_SETTINGS),
					    BUTTON_FONT_SIZE, 0,
					    HOVER_SETTINGS_OR_IMPERIAL,
					    "jewelsound") != 0) {
					changed = 1;
					g_missionSetupActivePanel =
						MISSION_SETUP_PANEL_SETTINGS;
				}
			} else {
				FrontendButton_DrawSpriteAndTooltip(
					&rect, "game7d",
					FrontendString_Get(
						FRONTSTR_203_SETTINGS),
					BUTTON_FONT_SIZE, 0);
			}
		}
		FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
		if (navigationSlotStates[PLAYERS_OR_REBEL_NAVIGATION_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE) {
			if (g_missionSetupActivePanel !=
			    MISSION_SETUP_PANEL_PLAYERS) {
				if (FrontendButton_HandleSpriteButton(
					    &rect, "game6u", "game6u",
					    FrontendString_Get(
						    FRONTSTR_202_PLAYERS),
					    BUTTON_FONT_SIZE, 0,
					    HOVER_PLAYERS_OR_REBEL,
					    "jewelsound") != 0) {
					g_missionSetupActivePanel =
						MISSION_SETUP_PANEL_PLAYERS;
					changed = 1;
				}
			} else {
				FrontendButton_DrawSpriteAndTooltip(
					&rect, "game6d",
					FrontendString_Get(
						FRONTSTR_202_PLAYERS),
					BUTTON_FONT_SIZE, 0);
			}
		}
	}

	FrontendDraw_RectAssign(&rect, BUTTON_LEFT, CAMPAIGN_BUTTON_TOP,
				BUTTON_RIGHT, CAMPAIGN_BUTTON_BOTTOM);
	if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_CAMPAIGNS) {
		if (isHost != 0) {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "game8u", "game8u",
				    FrontendString_Get(FRONTSTR_199_CAMPAIGNS),
				    BUTTON_FONT_SIZE, 0, HOVER_CAMPAIGN,
				    "jewelsound") != 0) {
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_TRAINING_EXERCISES &&
				    g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_CAMPAIGNS) {
					g_gameConfig.missionTimeLimit = -1;
				}
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_CAMPAIGNS;
				g_gameConfig.randomSetup =
					RANDOM_SETUP_SEQUENTIAL;
				changed = 1;
				g_gameConfig.continueBattleOrCampaign =
					SEQUENCE_CONTINUE;
				FrontendMission_LoadCurrent();
				MissionSetup_UpdateTeamCounts();
				MissionSetup_LoadMissionDescText(g_missionText);
				g_frontendFirstVisibleLine = 0;
				if (g_missionList != NULL) {
					g_selectedMissionListIndex = 0;
					while ((unsigned int)g_selectedMissionListIndex <
						       g_missionCount &&
					       g_missionList[g_selectedMissionListIndex]
							       .missionIdx !=
						       g_pilotData.missionDescriptionIds
							       [g_pilotData
									.missionDirectoryId]) {
						++g_selectedMissionListIndex;
					}
					if (g_selectedMissionListIndex ==
						    (int)g_missionCount ||
					    g_missionList[g_selectedMissionListIndex]
							    .isUnavailable !=
						    0) {
						for (index = 0;
						     index <
						     (int)g_missionCount;
						     ++index) {
							if (g_missionList[index]
								    .isUnavailable ==
							    0) {
								g_pilotData.missionDescriptionIds
									[g_pilotData
										 .missionDirectoryId] =
									g_missionList[index]
										.missionIdx;
								g_selectedMissionListIndex =
									index;
								break;
							}
						}
						FrontendMission_LoadCurrent();
						MissionSetup_UpdateTeamCounts();
						MissionSetup_LoadMissionDescText(
							g_missionText);
						g_frontendFirstVisibleLine = 0;
					}
				}
				if (g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_BATTLES &&
				    g_gameConfig.randomSetup ==
					    RANDOM_SETUP_PLAYER_CHOICE) {
					g_gameConfig.randomSetup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					int missionIndex =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;

					if (g_pilotData
						    .spCampaignContinuations
							    [missionIndex]
						    .isActive != 0) {
						g_gameConfig.randomSetup =
							g_pilotData
								.spCampaignContinuations
									[missionIndex]
								.randomSetup;
					}
				} else if (Net_IsHost() != 0) {
					int missionIndex =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;

					if (g_pilotData
						    .mpCampaignContinuations
							    [missionIndex]
						    .isActive != 0) {
						g_gameConfig.randomSetup =
							g_pilotData
								.mpCampaignContinuations
									[missionIndex]
								.randomSetup;
					}
				} else {
					int missionIndex =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;

					if (g_pilotData
						    .mpCampaignContinuations
							    [missionIndex +
							     CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
						    .isActive != 0) {
						g_gameConfig.randomSetup =
							g_pilotData
								.mpCampaignContinuations
									[missionIndex +
									 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
								.randomSetup;
					}
				}
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "game8u",
				FrontendString_Get(FRONTSTR_199_CAMPAIGNS),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		FrontendButton_DrawSpriteAndTooltip(
			&rect, "game8d",
			FrontendString_Get(FRONTSTR_199_CAMPAIGNS),
			BUTTON_FONT_SIZE, 0);
		if (isHost != 0 &&
		    FrontendDraw_PointInRect(&rect, cursorX, cursorY) != 0) {
			if (FrontendMouse_GetRightClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selectedMissionListIndex ==
						    0) {
							g_selectedMissionListIndex =
								g_missionCount -
								1;
						} else {
							--g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					if (g_missionList != NULL) {
						g_frontendFirstVisibleLine = 0;
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
					if (g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						int missionIndex =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;

						if (g_pilotData
							    .spCampaignContinuations
								    [missionIndex]
							    .isActive != 0) {
							g_gameConfig
								.randomSetup =
								g_pilotData
									.spCampaignContinuations
										[missionIndex]
									.randomSetup;
						}
					} else if (Net_IsHost() != 0) {
						int missionIndex =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;

						if (g_pilotData
							    .mpCampaignContinuations
								    [missionIndex]
							    .isActive != 0) {
							g_gameConfig
								.randomSetup =
								g_pilotData
									.mpCampaignContinuations
										[missionIndex]
									.randomSetup;
						}
					} else {
						int missionIndex =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;

						if (g_pilotData
							    .mpCampaignContinuations
								    [missionIndex +
								     CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
							    .isActive != 0) {
							g_gameConfig
								.randomSetup =
								g_pilotData
									.mpCampaignContinuations
										[missionIndex +
										 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
									.randomSetup;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			} else if (FrontendMouse_GetLeftClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_missionCount -
							    g_selectedMissionListIndex ==
						    1) {
							g_selectedMissionListIndex =
								0;
						} else {
							++g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					if (g_missionList != NULL) {
						g_frontendFirstVisibleLine = 0;
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
					if (g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						int missionIndex =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;

						if (g_pilotData
							    .spCampaignContinuations
								    [missionIndex]
							    .isActive != 0) {
							g_gameConfig
								.randomSetup =
								g_pilotData
									.spCampaignContinuations
										[missionIndex]
									.randomSetup;
						}
					} else if (Net_IsHost() != 0) {
						int missionIndex =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;

						if (g_pilotData
							    .mpCampaignContinuations
								    [missionIndex]
							    .isActive != 0) {
							g_gameConfig
								.randomSetup =
								g_pilotData
									.mpCampaignContinuations
										[missionIndex]
									.randomSetup;
						}
					} else {
						int missionIndex =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;

						if (g_pilotData
							    .mpCampaignContinuations
								    [missionIndex +
								     CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
							    .isActive != 0) {
							g_gameConfig
								.randomSetup =
								g_pilotData
									.mpCampaignContinuations
										[missionIndex +
										 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
									.randomSetup;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			}
		}
	}

	FrontendDraw_RectAssign(&rect, BUTTON_LEFT, BATTLE_BUTTON_TOP,
				BUTTON_RIGHT, BATTLE_BUTTON_BOTTOM);
	if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_BATTLES) {
		if (isHost != 0) {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "game5u", "game5u",
				    FrontendString_Get(FRONTSTR_198_BATTLES),
				    BUTTON_FONT_SIZE, 0, HOVER_BATTLE,
				    "jewelsound") != 0) {
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
					    g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_BATTLES) {
						g_gameConfig.missionTimeLimit =
							-1;
					}
					if (g_gameConfig.craftWaves ==
					    CRAFT_WAVES_UNLIMITED) {
						g_gameConfig.craftWaves =
							CRAFT_WAVES_DEFAULT;
					}
				}
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_BATTLES;
				changed = 1;
				g_gameConfig.continueBattleOrCampaign =
					SEQUENCE_CONTINUE;
				if (g_gameConfig.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_gameConfig.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				FrontendMission_LoadCurrent();
				MissionSetup_UpdateTeamCounts();
				MissionSetup_LoadMissionDescText(g_missionText);
				g_frontendFirstVisibleLine = 0;
				if (g_missionList != NULL) {
					for (g_selectedMissionListIndex = 0;
					     (unsigned int)
						     g_selectedMissionListIndex <
					     g_missionCount;
					     ++g_selectedMissionListIndex) {
						if (g_missionList
							    [g_selectedMissionListIndex]
								    .missionIdx ==
						    g_pilotData.missionDescriptionIds
							    [g_pilotData
								     .missionDirectoryId]) {
							if (g_missionList[g_selectedMissionListIndex]
								    .isUnavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_missionCount;
								     ++index) {
									if (g_missionList[index]
										    .isUnavailable ==
									    0) {
										g_pilotData
											.missionDescriptionIds
												[g_pilotData
													 .missionDirectoryId] =
											g_missionList[index]
												.missionIdx;
										g_selectedMissionListIndex =
											index;
										break;
									}
								}
								FrontendMission_LoadCurrent();
								MissionSetup_UpdateTeamCounts();
								MissionSetup_LoadMissionDescText(
									g_missionText);
							}
							break;
						}
					}
				}
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilotData
						    .spBattleContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
						    .isActive != 0) {
						g_gameConfig.battleLengthIndex =
							g_pilotData
								.spBattleContinuations
									[g_missionList[g_selectedMissionListIndex]
										 .missionIdx]
								.battleLengthIndex;
						g_gameConfig.randomSetup =
							g_pilotData
								.spBattleContinuations
									[g_missionList[g_selectedMissionListIndex]
										 .missionIdx]
								.randomSetup;
					}
				} else {
					if (g_pilotData
						    .mpBattleContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
						    .isActive != 0) {
						g_gameConfig.battleLengthIndex =
							g_pilotData
								.mpBattleContinuations
									[g_missionList[g_selectedMissionListIndex]
										 .missionIdx]
								.battleLengthIndex;
						g_gameConfig.randomSetup =
							g_pilotData
								.mpBattleContinuations
									[g_missionList[g_selectedMissionListIndex]
										 .missionIdx]
								.randomSetup;
					}
				}
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "game5u",
				FrontendString_Get(FRONTSTR_198_BATTLES),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		FrontendButton_DrawSpriteAndTooltip(
			&rect, "game5d",
			FrontendString_Get(FRONTSTR_198_BATTLES),
			BUTTON_FONT_SIZE, 0);
		if (isHost != 0 &&
		    FrontendDraw_PointInRect(&rect, cursorX, cursorY) != 0) {
			if (FrontendMouse_GetRightClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selectedMissionListIndex ==
						    0) {
							g_selectedMissionListIndex =
								g_missionCount -
								1;
						} else {
							--g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
					if (g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_pilotData
							    .spBattleContinuations
								    [g_missionList[g_selectedMissionListIndex]
									     .missionIdx]
							    .isActive != 0) {
							g_gameConfig
								.battleLengthIndex =
								g_pilotData
									.spBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.battleLengthIndex;
							g_gameConfig
								.randomSetup =
								g_pilotData
									.spBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.randomSetup;
						}
					} else {
						if (g_pilotData
							    .mpBattleContinuations
								    [g_missionList[g_selectedMissionListIndex]
									     .missionIdx]
							    .isActive != 0) {
							g_gameConfig
								.battleLengthIndex =
								g_pilotData
									.mpBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.battleLengthIndex;
							g_gameConfig
								.randomSetup =
								g_pilotData
									.mpBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.randomSetup;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			} else if (FrontendMouse_GetLeftClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_missionCount -
							    g_selectedMissionListIndex ==
						    1) {
							g_selectedMissionListIndex =
								0;
						} else {
							++g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
					if (g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_pilotData
							    .spBattleContinuations
								    [g_missionList[g_selectedMissionListIndex]
									     .missionIdx]
							    .isActive != 0) {
							g_gameConfig
								.battleLengthIndex =
								g_pilotData
									.spBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.battleLengthIndex;
							g_gameConfig
								.randomSetup =
								g_pilotData
									.spBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.randomSetup;
						}
					} else {
						if (g_pilotData
							    .mpBattleContinuations
								    [g_missionList[g_selectedMissionListIndex]
									     .missionIdx]
							    .isActive != 0) {
							g_gameConfig
								.battleLengthIndex =
								g_pilotData
									.mpBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.battleLengthIndex;
							g_gameConfig
								.randomSetup =
								g_pilotData
									.mpBattleContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx]
									.randomSetup;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (g_pilotData.missionDirectoryId !=
	    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		if (isHost != 0) {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "game4u", "game4u",
				    FrontendString_Get(
					    FRONTSTR_191_COMBAT_ENGAGEMENTS),
				    BUTTON_FONT_SIZE, 0,
				    HOVER_COMBAT_ENGAGEMENT,
				    "jewelsound") != 0) {
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
					    g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_BATTLES) {
						g_gameConfig.missionTimeLimit =
							-1;
					}
					if (g_gameConfig.craftWaves ==
					    CRAFT_WAVES_UNLIMITED) {
						g_gameConfig.craftWaves =
							CRAFT_WAVES_DEFAULT;
					}
				}
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
				changed = 1;
				g_gameConfig.continueBattleOrCampaign =
					SEQUENCE_CONTINUE;
				if (g_gameConfig.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_gameConfig.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				if (g_gameConfig.randomSetup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
					g_gameConfig.randomSetup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				FrontendMission_LoadCurrent();
				MissionSetup_UpdateTeamCounts();
				MissionSetup_LoadMissionDescText(g_missionText);
				g_frontendFirstVisibleLine = 0;
				g_selectedMissionListIndex = 0;
				if (g_missionList != NULL) {
					for (;
					     (unsigned int)
						     g_selectedMissionListIndex <
					     g_missionCount;
					     ++g_selectedMissionListIndex) {
						if (g_missionList
							    [g_selectedMissionListIndex]
								    .missionIdx ==
						    g_pilotData.missionDescriptionIds
							    [g_pilotData
								     .missionDirectoryId]) {
							if (g_missionList[g_selectedMissionListIndex]
								    .isUnavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_missionCount;
								     ++index) {
									if (g_missionList[index]
										    .isUnavailable ==
									    0) {
										g_pilotData
											.missionDescriptionIds
												[g_pilotData
													 .missionDirectoryId] =
											g_missionList[index]
												.missionIdx;
										g_selectedMissionListIndex =
											index;
										break;
									}
								}
								FrontendMission_LoadCurrent();
								MissionSetup_UpdateTeamCounts();
								MissionSetup_LoadMissionDescText(
									g_missionText);
							}
							break;
						}
					}
				}
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "game4u",
				FrontendString_Get(
					FRONTSTR_191_COMBAT_ENGAGEMENTS),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		FrontendButton_DrawSpriteAndTooltip(
			&rect, "game4d",
			FrontendString_Get(FRONTSTR_191_COMBAT_ENGAGEMENTS),
			BUTTON_FONT_SIZE, 0);
		if (isHost != 0 &&
		    FrontendDraw_PointInRect(&rect, cursorX, cursorY) != 0) {
			if (FrontendMouse_GetRightClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selectedMissionListIndex ==
						    0) {
							g_selectedMissionListIndex =
								g_missionCount -
								1;
						} else {
							--g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			} else if (FrontendMouse_GetLeftClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_missionCount -
							    g_selectedMissionListIndex ==
						    1) {
							g_selectedMissionListIndex =
								0;
						} else {
							++g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (navigationSlotStates[MISSION_DIRECTORY_TOURNAMENTS] !=
	    FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_pilotData.missionDirectoryId !=
		    MISSION_DIRECTORY_TOURNAMENTS) {
			if (isHost != 0) {
				if (FrontendButton_HandleSpriteButton(
					    &rect, "game3u", "game3u",
					    FrontendString_Get(
						    FRONTSTR_196_TOURNAMENTS),
					    BUTTON_FONT_SIZE, 0,
					    HOVER_TOURNAMENT,
					    "jewelsound") != 0) {
					if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
					    g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_MELEES &&
					    g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_TOURNAMENTS) {
						g_gameConfig.missionTimeLimit =
							-1;
					}
					g_pilotData.missionDirectoryId =
						MISSION_DIRECTORY_TOURNAMENTS;
					changed = 1;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					if (g_gameConfig.difficulty ==
					    GAME_DIFFICULTY_EASY_CHEAT) {
						g_gameConfig.difficulty =
							GAME_DIFFICULTY_EASY;
					}
					if (g_gameConfig.randomSetup ==
					    RANDOM_SETUP_PLAYER_CHOICE) {
						g_gameConfig.randomSetup =
							RANDOM_SETUP_SEQUENTIAL;
					}
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						for (g_selectedMissionListIndex =
							     0;
						     (unsigned int)
							     g_selectedMissionListIndex <
						     g_missionCount;
						     ++g_selectedMissionListIndex) {
							if (g_missionList[g_selectedMissionListIndex]
								    .missionIdx ==
							    g_pilotData.missionDescriptionIds
								    [g_pilotData
									     .missionDirectoryId]) {
								if (g_missionList[g_selectedMissionListIndex]
									    .isUnavailable !=
								    0) {
									for (index = 0;
									     index <
									     (int)g_missionCount;
									     ++index) {
										if (g_missionList[index]
											    .isUnavailable ==
										    0) {
											g_pilotData
												.missionDescriptionIds
													[g_pilotData
														 .missionDirectoryId] =
												g_missionList[index]
													.missionIdx;
											g_selectedMissionListIndex =
												index;
											break;
										}
									}
									FrontendMission_LoadCurrent();
									MissionSetup_UpdateTeamCounts();
									MissionSetup_LoadMissionDescText(
										g_missionText);
								}
								break;
							}
						}
					}
				}
			} else {
				FrontendButton_DrawSpriteAndTooltip(
					&rect, "game3u",
					FrontendString_Get(
						FRONTSTR_196_TOURNAMENTS),
					BUTTON_FONT_SIZE, 0);
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "game3d",
				FrontendString_Get(FRONTSTR_196_TOURNAMENTS),
				BUTTON_FONT_SIZE, 0);
			if (isHost != 0 &&
			    FrontendDraw_PointInRect(&rect, cursorX, cursorY) !=
				    0) {
				if (FrontendMouse_GetRightClick() != 0) {
					if (g_missionList != NULL) {
						if (g_gameConfig
							    .sfxDatapadEnabled !=
						    0) {
							FrontendSound_PlayUISound(
								"jewelsound", 1,
								0, 255,
								SOUND_VOLUME_SCALE *
									g_gameConfig
										.sfxDatapadVolume,
								SOUND_CENTER_PAN);
						}
						do {
							if (g_selectedMissionListIndex ==
							    0) {
								g_selectedMissionListIndex =
									g_missionCount -
									1;
							} else {
								--g_selectedMissionListIndex;
							}
						} while (
							g_missionList[g_selectedMissionListIndex]
								.isUnavailable !=
							0);
						selectedMissionId =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;
						g_gameConfig
							.continueBattleOrCampaign =
							SEQUENCE_CONTINUE;
						g_pilotData.missionDescriptionIds
							[g_pilotData
								 .missionDirectoryId] =
							selectedMissionId;
						FrontendMission_LoadCurrent();
						MissionSetup_UpdateTeamCounts();
						MissionSetup_LoadMissionDescText(
							g_missionText);
						g_frontendFirstVisibleLine = 0;
						if (g_missionList != NULL) {
							g_selectedMissionListIndex =
								0;
							while ((unsigned int)g_selectedMissionListIndex <
								       g_missionCount &&
							       g_missionList[g_selectedMissionListIndex]
									       .missionIdx !=
								       g_pilotData
									       .missionDescriptionIds
										       [g_pilotData
												.missionDirectoryId]) {
								++g_selectedMissionListIndex;
							}
						}
					}
					if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
					    Net_IsHost() != 0) {
						MissionSetup_SendLobbyState(0);
					}
				} else if (FrontendMouse_GetLeftClick() != 0) {
					if (g_missionList != NULL) {
						if (g_gameConfig
							    .sfxDatapadEnabled !=
						    0) {
							FrontendSound_PlayUISound(
								"jewelsound", 1,
								0, 255,
								SOUND_VOLUME_SCALE *
									g_gameConfig
										.sfxDatapadVolume,
								SOUND_CENTER_PAN);
						}
						do {
							if (g_missionCount -
								    g_selectedMissionListIndex ==
							    1) {
								g_selectedMissionListIndex =
									0;
							} else {
								++g_selectedMissionListIndex;
							}
						} while (
							g_missionList[g_selectedMissionListIndex]
								.isUnavailable !=
							0);
						selectedMissionId =
							g_missionList
								[g_selectedMissionListIndex]
									.missionIdx;
						g_gameConfig
							.continueBattleOrCampaign =
							SEQUENCE_CONTINUE;
						g_pilotData.missionDescriptionIds
							[g_pilotData
								 .missionDirectoryId] =
							selectedMissionId;
						FrontendMission_LoadCurrent();
						MissionSetup_UpdateTeamCounts();
						MissionSetup_LoadMissionDescText(
							g_missionText);
						g_frontendFirstVisibleLine = 0;
						if (g_missionList != NULL) {
							g_selectedMissionListIndex =
								0;
							while ((unsigned int)g_selectedMissionListIndex <
								       g_missionCount &&
							       g_missionList[g_selectedMissionListIndex]
									       .missionIdx !=
								       g_pilotData
									       .missionDescriptionIds
										       [g_pilotData
												.missionDirectoryId]) {
								++g_selectedMissionListIndex;
							}
						}
					}
					if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
					    Net_IsHost() != 0) {
						MissionSetup_SendLobbyState(0);
					}
				}
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_MELEES) {
		if (isHost != 0) {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "game2u", "game2u",
				    FrontendString_Get(FRONTSTR_190_MELEES),
				    BUTTON_FONT_SIZE, 0, HOVER_MELEE,
				    "jewelsound") != 0) {
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_MELEES &&
				    g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_TOURNAMENTS) {
					g_gameConfig.missionTimeLimit = -1;
				}
				changed = 1;
				g_gameConfig.continueBattleOrCampaign =
					SEQUENCE_CONTINUE;
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_MELEES;
				if (g_gameConfig.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_gameConfig.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				if (g_gameConfig.randomSetup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
					g_gameConfig.randomSetup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				FrontendMission_LoadCurrent();
				MissionSetup_UpdateTeamCounts();
				MissionSetup_LoadMissionDescText(g_missionText);
				g_frontendFirstVisibleLine = 0;
				if (g_missionList != NULL) {
					for (g_selectedMissionListIndex = 0;
					     (unsigned int)
						     g_selectedMissionListIndex <
					     g_missionCount;
					     ++g_selectedMissionListIndex) {
						if (g_missionList
							    [g_selectedMissionListIndex]
								    .missionIdx ==
						    g_pilotData.missionDescriptionIds
							    [g_pilotData
								     .missionDirectoryId]) {
							if (g_missionList[g_selectedMissionListIndex]
								    .isUnavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_missionCount;
								     ++index) {
									if (g_missionList[index]
										    .isUnavailable ==
									    0) {
										g_pilotData
											.missionDescriptionIds
												[g_pilotData
													 .missionDirectoryId] =
											g_missionList[index]
												.missionIdx;
										g_selectedMissionListIndex =
											index;
										break;
									}
								}
								FrontendMission_LoadCurrent();
								MissionSetup_UpdateTeamCounts();
								MissionSetup_LoadMissionDescText(
									g_missionText);
							}
							break;
						}
					}
				}
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "game2u",
				FrontendString_Get(FRONTSTR_190_MELEES),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		FrontendButton_DrawSpriteAndTooltip(
			&rect, "game2d",
			FrontendString_Get(FRONTSTR_190_MELEES),
			BUTTON_FONT_SIZE, 0);
		if (isHost != 0 &&
		    FrontendDraw_PointInRect(&rect, cursorX, cursorY) != 0) {
			if (FrontendMouse_GetRightClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selectedMissionListIndex ==
						    0) {
							g_selectedMissionListIndex =
								g_missionCount -
								1;
						} else {
							--g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			} else if (FrontendMouse_GetLeftClick() != 0) {
				if (g_missionList != NULL) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_missionCount -
							    g_selectedMissionListIndex ==
						    1) {
							g_selectedMissionListIndex =
								0;
						} else {
							++g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					selectedMissionId =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						selectedMissionId;
					FrontendMission_LoadCurrent();
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (g_pilotData.missionDirectoryId !=
	    MISSION_DIRECTORY_TRAINING_EXERCISES) {
		if (isHost != 0) {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "game1u", "game1u",
				    FrontendString_Get(FRONTSTR_189_EXERCISE),
				    BUTTON_FONT_SIZE, 0, HOVER_TRAINING,
				    "jewelsound") != 0) {
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_gameConfig.missionTimeLimit = -1;
				}
				changed = 1;
				g_gameConfig.continueBattleOrCampaign =
					SEQUENCE_CONTINUE;
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_TRAINING_EXERCISES;
				if (g_gameConfig.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_gameConfig.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				if (g_gameConfig.randomSetup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
					g_gameConfig.randomSetup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				FrontendMission_LoadCurrent();
				MissionSetup_UpdateTeamCounts();
				MissionSetup_LoadMissionDescText(g_missionText);
				g_frontendFirstVisibleLine = 0;
				if (g_missionList != NULL) {
					for (g_selectedMissionListIndex = 0;
					     (unsigned int)
						     g_selectedMissionListIndex <
					     g_missionCount;
					     ++g_selectedMissionListIndex) {
						if (g_missionList
							    [g_selectedMissionListIndex]
								    .missionIdx ==
						    g_pilotData.missionDescriptionIds
							    [g_pilotData
								     .missionDirectoryId]) {
							if (g_missionList[g_selectedMissionListIndex]
								    .isUnavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_missionCount;
								     ++index) {
									if (g_missionList[index]
										    .isUnavailable ==
									    0) {
										g_pilotData
											.missionDescriptionIds
												[g_pilotData
													 .missionDirectoryId] =
											g_missionList[index]
												.missionIdx;
										g_selectedMissionListIndex =
											index;
										break;
									}
								}
								FrontendMission_LoadCurrent();
								MissionSetup_UpdateTeamCounts();
								MissionSetup_LoadMissionDescText(
									g_missionText);
							}
							break;
						}
					}
				}
			}
		} else {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "game1u",
				FrontendString_Get(FRONTSTR_189_EXERCISE),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		FrontendButton_DrawSpriteAndTooltip(
			&rect, "game1d",
			FrontendString_Get(FRONTSTR_189_EXERCISE),
			BUTTON_FONT_SIZE, 0);
		if (isHost != 0 &&
		    FrontendDraw_PointInRect(&rect, cursorX, cursorY) != 0) {
			if (FrontendMouse_GetRightClick() != 0) {
				if (g_missionList != NULL) {
					int previousPlayerIff;
					int playerFlightGroupIndex;

					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selectedMissionListIndex ==
						    0) {
							g_selectedMissionListIndex =
								g_missionCount -
								1;
						} else {
							--g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					playerFlightGroupIndex = 0;
					while (playerFlightGroupIndex <
						       (int)g_frontendMission
							       .flightGroupCount &&
					       g_frontendMission
							       .flightGroups
								       [playerFlightGroupIndex]
							       .playerNumber ==
						       0) {
						++playerFlightGroupIndex;
					}
					previousPlayerIff =
						g_frontendMission
							.flightGroups
								[playerFlightGroupIndex]
							.iff;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					FrontendMission_LoadCurrent();
					playerFlightGroupIndex = 0;
					while (playerFlightGroupIndex <
						       (int)g_frontendMission
							       .flightGroupCount &&
					       g_frontendMission
							       .flightGroups
								       [playerFlightGroupIndex]
							       .playerNumber ==
						       0) {
						++playerFlightGroupIndex;
					}
					if (g_frontendMission
						    .flightGroups
							    [playerFlightGroupIndex]
						    .iff != previousPlayerIff) {
						changed = 1;
					}
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			} else if (FrontendMouse_GetLeftClick() != 0) {
				if (g_missionList != NULL) {
					int previousPlayerIff;
					int playerFlightGroupIndex;

					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_missionCount -
							    g_selectedMissionListIndex ==
						    1) {
							g_selectedMissionListIndex =
								0;
						} else {
							++g_selectedMissionListIndex;
						}
					} while (
						g_missionList
							[g_selectedMissionListIndex]
								.isUnavailable !=
						0);
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId] =
						g_missionList
							[g_selectedMissionListIndex]
								.missionIdx;
					playerFlightGroupIndex = 0;
					while (playerFlightGroupIndex <
						       (int)g_frontendMission
							       .flightGroupCount &&
					       g_frontendMission
							       .flightGroups
								       [playerFlightGroupIndex]
							       .playerNumber ==
						       0) {
						++playerFlightGroupIndex;
					}
					previousPlayerIff =
						g_frontendMission
							.flightGroups
								[playerFlightGroupIndex]
							.iff;
					g_gameConfig.continueBattleOrCampaign =
						SEQUENCE_CONTINUE;
					FrontendMission_LoadCurrent();
					playerFlightGroupIndex = 0;
					while (playerFlightGroupIndex <
						       (int)g_frontendMission
							       .flightGroupCount &&
					       g_frontendMission
							       .flightGroups
								       [playerFlightGroupIndex]
							       .playerNumber ==
						       0) {
						++playerFlightGroupIndex;
					}
					if (g_frontendMission
						    .flightGroups
							    [playerFlightGroupIndex]
						    .iff != previousPlayerIff) {
						changed = 1;
					}
					MissionSetup_UpdateTeamCounts();
					MissionSetup_LoadMissionDescText(
						g_missionText);
					g_frontendFirstVisibleLine = 0;
					if (g_missionList != NULL) {
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}
					}
				}
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_IsHost() != 0) {
					MissionSetup_SendLobbyState(0);
				}
			}
		}
	}

	if (changed != 0) {
		FrontImage_FreeResourceByName("background");
		MissionSetup_DrawBackground();
	}
	return changed;
}

/* Loads the mission list of a mission type into g_missionList and
 * g_missionCount, freeing the old list first. The file is "<type>\mission.lst"
 * in a network game; in a solo game or with no session, training, combat
 * engagements and campaigns read rebel.lst or imperial.lst by the pilot's
 * faction, and melees, tournaments and battles read mission.lst. In the modern
 * build a list that cannot be opened ends the program; the original build asks
 * for the game's CD until it opens, and Cancel returns with an empty list.
 * Lines starting with "//" are skipped and a "[name]" line names the section of
 * the entries after it. Each entry is three lines: the mission id, the file
 * name (lowercased) and the description. A file name starting with "&" marks
 * the entry unavailable; one starting with "*" carries two numbers, from its
 * third character, that are read and dropped, and marks the entry unavailable
 * while the campaign mission with the entry's id has never been flown: by the
 * current faction in a solo game, else by either faction. A list that ends
 * early keeps the entries completed. Does not check the type id: outside 0 to 5
 * a solo game opens whatever name g_frontendScratchBuffer held and a network
 * game reads past g_missionDirectoryNames. A failed allocation returns with
 * g_missionList NULL and g_missionCount still holding the count. */
// FUNCTION: XVT 0x4E5000
void MissionSetup_LoadMissionList(int missionDirectoryId)
{
	enum {
		MISSION_LINE_CAPACITY = 256,
		SECTION_NAME_CAPACITY = 128,
		MISSION_PREFIX_FIRST_NUMBER = 2,
	};

	XvtFile *stream;
	unsigned int entryIndex;
	unsigned int characterIndex;
	char *secondNumber;
	char currentSection[SECTION_NAME_CAPACITY];

	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	g_missionCount = 0;
	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_frontendMissionSessionMode != FRONTEND_MISSION_SESSION_NONE) {
		sprintf(g_frontendScratchBuffer, "%s\\mission.lst",
			g_missionDirectoryNames[missionDirectoryId]);
	} else {
		switch (missionDirectoryId) {
		case MISSION_DIRECTORY_TRAINING_EXERCISES:
		case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
		case MISSION_DIRECTORY_CAMPAIGNS:
			if (g_pilotData.currentFactionId == 0) {
				sprintf(g_frontendScratchBuffer,
					"%s\\rebel.lst",
					g_missionDirectoryNames
						[missionDirectoryId]);
			} else {
				sprintf(g_frontendScratchBuffer,
					"%s\\imperial.lst",
					g_missionDirectoryNames
						[missionDirectoryId]);
			}
			break;
		case MISSION_DIRECTORY_MELEES:
		case MISSION_DIRECTORY_TOURNAMENTS:
		case MISSION_DIRECTORY_BATTLES:
			sprintf(g_frontendScratchBuffer, "%s\\mission.lst",
				g_missionDirectoryNames[missionDirectoryId]);
			break;
		default:
			break;
		}
	}

#ifdef XVT_MODERN
	stream = File_Open(g_frontendScratchBuffer, "r");
	if (stream == NULL) {
		XvtStorage_Fatal("Cannot load mission list", 1);
		return;
	}
#else
	while ((stream = File_Open(g_frontendScratchBuffer, "r")) == NULL) {
		if (FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
			    FrontendString_Get(
				    FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
			    FrontendString_Get(
				    FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) == 0) {
			return;
		}
	}

#endif
	g_missionCount = MissionSetup_CountMissionListEntries(stream);
	File_Seek(stream, 0, 0);
	if (g_missionCount == 0) {
		File_Close(stream);
		return;
	}
	g_missionList = (MissionListEntry *)malloc(sizeof(*g_missionList) *
						   g_missionCount);
	if (g_missionList == NULL) {
		File_Close(stream);
		return;
	}

	memset(currentSection, 0, sizeof(currentSection));
	for (entryIndex = 0; entryIndex < g_missionCount; ++entryIndex) {
		for (;;) {
			do {
				if (File_Gets(g_frontendScratchBuffer,
					      MISSION_LINE_CAPACITY,
					      stream) == NULL) {
					g_missionCount = entryIndex;
					File_Close(stream);
					return;
				}
			} while (g_frontendScratchBuffer[0] == '/' &&
				 g_frontendScratchBuffer[1] == '/');
			if (g_frontendScratchBuffer
				    [strlen(g_frontendScratchBuffer) - 1] ==
			    '\n') {
				g_frontendScratchBuffer
					[strlen(g_frontendScratchBuffer) - 1] =
						'\0';
			}
			if (g_frontendScratchBuffer[0] != '[') {
				break;
			}
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
			strcpy(currentSection, &g_frontendScratchBuffer[1]);
		}

		strcpy(g_missionList[entryIndex].sectionName, currentSection);
		g_missionList[entryIndex].isUnavailable = 0;
		g_missionList[entryIndex].missionIdx =
			atoi(g_frontendScratchBuffer);
		if (File_Gets(g_frontendScratchBuffer, MISSION_LINE_CAPACITY,
			      stream) == NULL) {
			g_missionCount = entryIndex;
			File_Close(stream);
			return;
		}
		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		for (characterIndex = 0;
		     characterIndex < strlen(g_frontendScratchBuffer);
		     ++characterIndex) {
			g_frontendScratchBuffer[characterIndex] = (char)tolower(
				(unsigned char)g_frontendScratchBuffer
					[characterIndex]);
		}

		if (g_frontendScratchBuffer[0] == '*') {
			for (characterIndex = MISSION_PREFIX_FIRST_NUMBER;
			     g_frontendScratchBuffer[characterIndex] != ' ';
			     ++characterIndex) {
			}
			g_frontendScratchBuffer[characterIndex] = '\0';
			(void)atoi(&g_frontendScratchBuffer
					   [MISSION_PREFIX_FIRST_NUMBER]);
			++characterIndex;
			secondNumber = &g_frontendScratchBuffer[characterIndex];
			while (g_frontendScratchBuffer[characterIndex] != ' ') {
				++characterIndex;
			}
			g_frontendScratchBuffer[characterIndex] = '\0';
			(void)atoi(secondNumber);
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilotData
					    .factionStatistics
						    [g_pilotData
							     .currentFactionId]
					    .spCampaignMissions
						    [g_missionList[entryIndex]
							     .missionIdx -
						     1]
					    .numberTimesFlown == 0) {
					g_missionList[entryIndex]
						.isUnavailable = 1;
				}
			} else if (
				g_pilotData.factionStatistics[0]
						.mpCampaignMissions
							[g_missionList[entryIndex]
								 .missionIdx -
							 1]
						.numberTimesFlown == 0 &&
				g_pilotData.factionStatistics[1]
						.mpCampaignMissions
							[g_missionList[entryIndex]
								 .missionIdx -
							 1]
						.numberTimesFlown == 0) {
				g_missionList[entryIndex].isUnavailable = 1;
			}
			strcpy(g_missionList[entryIndex].fileName,
			       &g_frontendScratchBuffer[characterIndex + 1]);
		} else if (g_frontendScratchBuffer[0] == '&') {
			g_missionList[entryIndex].isUnavailable = 1;
			strcpy(g_missionList[entryIndex].fileName,
			       &g_frontendScratchBuffer[2]);
		} else {
			strcpy(g_missionList[entryIndex].fileName,
			       g_frontendScratchBuffer);
		}

		if (File_Gets(g_frontendScratchBuffer, MISSION_LINE_CAPACITY,
			      stream) == NULL) {
			g_missionCount = entryIndex;
			File_Close(stream);
			return;
		}
		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		strcpy(g_missionList[entryIndex].description,
		       g_frontendScratchBuffer);
	}
	File_Close(stream);
}

/* Fills the 4096-byte outText4096 with the description of the selected mission
 * (the entry of g_missionList matching g_pilotData's description id for the
 * current mission type), zeroing it first. For a tournament, battle or campaign
 * it reads the sequence file as text: the first line gives a count of lines to
 * skip, and the printable characters and newlines of the rest are copied, at
 * most 4095 of them. Any other mission file is read by its format word: 14 or
 * 13 takes the file's last 4096 bytes, 12 its last 1024, each ended with a NUL
 * in its last byte; another version leaves the text empty. Returns at once, the
 * text empty, when outText4096 is NULL, no entry matches or the file does not
 * open. */
// FUNCTION: XVT 0x4E5590
void MissionSetup_LoadMissionDescText(char *outText4096)
{
	unsigned int missionListIndex;
	int skippedLineCount;
	int outputLength;
	char *outputCursor;
	unsigned int lineLength;
	unsigned int characterIndex;
	int character;
	uint16_t missionVersion;
	XvtFile *stream;

	if (outText4096 == NULL) {
		return;
	}

	memset(outText4096, 0, 4096);
	missionListIndex = 0;
	while (missionListIndex < g_missionCount) {
		if (g_missionList[missionListIndex].missionIdx ==
		    g_pilotData.missionDescriptionIds
			    [g_pilotData.missionDirectoryId]) {
			break;
		}
		++missionListIndex;
	}
	if (missionListIndex >= g_missionCount) {
		return;
	}

	sprintf(g_frontendScratchBuffer, "%s\\%s",
		g_missionDirectoryNames[g_pilotData.missionDirectoryId],
		g_missionList[missionListIndex].fileName);
	stream = File_Open(g_frontendScratchBuffer, "rb");
	if (stream == NULL) {
		return;
	}

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS ||
	    g_pilotData.missionDirectoryId == MISSION_DIRECTORY_BATTLES ||
	    g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS) {
		File_Gets(g_frontendScratchBuffer, 256, stream);
		skippedLineCount = atoi(g_frontendScratchBuffer);
		while (skippedLineCount != 0) {
			File_Gets(g_frontendScratchBuffer, 256, stream);
			--skippedLineCount;
		}

		outputLength = 0;
		outputCursor = outText4096;
		while (File_Gets(g_frontendScratchBuffer, 256, stream) !=
		       NULL) {
			lineLength = strlen(g_frontendScratchBuffer);
			characterIndex = 0;
			if (lineLength != 0) {
				do {
					character = (unsigned char)
						g_frontendScratchBuffer
							[characterIndex];
					if (isprint(character) ||
					    character == '\n') {
						*outputCursor = character;
						++outputCursor;
						++outputLength;
						if ((unsigned int)
							    outputLength >=
						    4095) {
							break;
						}
					}
					++characterIndex;
				} while (lineLength > characterIndex);
			}
			if ((unsigned int)outputLength >= 4095) {
				break;
			}
		}
		outText4096[outputLength] = '\0';
	} else {
		File_ReadWord(stream, &missionVersion);
		if (missionVersion == 14 || missionVersion == 13) {
			File_Seek(stream, -4096, SEEK_END);
			File_ReadBytes(stream, outText4096, 4096);
			outText4096[4095] = '\0';
		} else if (missionVersion == 12) {
			File_Seek(stream, -1024, SEEK_END);
			File_ReadBytes(stream, outText4096, 1024);
			outText4096[1023] = '\0';
		}
	}
	File_Close(stream);
}

/* Draws the mission description box of the mission setup screen: a heading for
 * a tournament, battle, campaign or other mission, then g_missionText wrapped,
 * with a scrollbar that sets g_frontendFirstVisibleLine when the text's wrapped
 * line count (from a draw starting at line 4096) plus one is over 9. For a
 * battle whose saved continuation is in use the heading is followed by the
 * Imperial and Rebel victory counts: in a solo game or for a network host,
 * counted over the saved results up to the current mission index, at most 10;
 * for a network client, g_remoteBattleImperialVictoryCount and
 * g_remoteBattleRebelVictoryCount. In use means the continuation is active and
 * g_gameConfig.continueBattleOrCampaign is not SEQUENCE_RESTART, or for a
 * client, g_remoteBattleContinuationActive and
 * g_remoteBattleSequenceContinuationChoice both nonzero. Returns 1. Does not
 * check g_missionList for a battle. */
// FUNCTION: XVT 0x4E5810
int MissionSetup_DrawMissionDescription(void)
{
	RECT rect;
	int continuationActive;
	int imperialVictories;
	int rebelVictories;
	int resultIndex;
	int maxResultIndex;
	int missionIndex;
	int lineCount;

	FrontendDraw_RectAssign(&rect, 88, 138, 430, 153);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_BATTLES) {
		continuationActive = 0;
		if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx;
			if (g_pilotData.spBattleContinuations[missionIndex]
					    .isActive != 0 &&
			    g_gameConfig.continueBattleOrCampaign !=
				    SEQUENCE_RESTART) {
				continuationActive = 1;
			}
		} else if (g_frontendMissionSessionMode ==
			   FRONTEND_MISSION_SESSION_NET_HOST) {
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx;
			if (g_pilotData.mpBattleContinuations[missionIndex]
					    .isActive != 0 &&
			    g_gameConfig.continueBattleOrCampaign !=
				    SEQUENCE_RESTART) {
				continuationActive = 1;
			}
		} else if (g_remoteBattleContinuationActive != 0 &&
			   g_remoteBattleSequenceContinuationChoice != 0) {
			continuationActive = 1;
		}

		if (continuationActive) {
			FrontendText_DrawAlignedInRect(
				15,
				FrontendString_Get(
					FRONTSTR_472_BATTLE_DESCRIPTION),
				&rect, 0, 1, 0xFFFF);
			rect.left += FrontendText_MeasureWidth(
				FrontendString_Get(
					FRONTSTR_472_BATTLE_DESCRIPTION),
				15);
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				imperialVictories = 0;
				rebelVictories = 0;
				resultIndex = 0;
				missionIndex =
					g_missionList
						[g_selectedMissionListIndex]
							.missionIdx;
				maxResultIndex = g_pilotData
							 .spBattleContinuations
								 [missionIndex]
							 .sequenceState
							 .currentMissionIndex;
				while (resultIndex <= maxResultIndex &&
				       resultIndex < 10) {
					switch (g_pilotData
							.spBattleContinuations
								[missionIndex]
							.sequenceState
							.missionResults
								[resultIndex]) {
					case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
						++imperialVictories;
						break;
					case BATTLE_MISSION_RESULT_REBEL_VICTORY:
						++rebelVictories;
						break;
					default:
						break;
					}
					++resultIndex;
				}
			} else if (g_frontendMissionSessionMode ==
				   FRONTEND_MISSION_SESSION_NET_HOST) {
				imperialVictories = 0;
				rebelVictories = 0;
				resultIndex = 0;
				missionIndex =
					g_missionList
						[g_selectedMissionListIndex]
							.missionIdx;
				maxResultIndex = g_pilotData
							 .mpBattleContinuations
								 [missionIndex]
							 .sequenceState
							 .currentMissionIndex;
				while (resultIndex <= maxResultIndex &&
				       resultIndex < 10) {
					switch (g_pilotData
							.mpBattleContinuations
								[missionIndex]
							.sequenceState
							.missionResults
								[resultIndex]) {
					case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
						++imperialVictories;
						break;
					case BATTLE_MISSION_RESULT_REBEL_VICTORY:
						++rebelVictories;
						break;
					default:
						break;
					}
					++resultIndex;
				}
			} else {
				rebelVictories =
					g_remoteBattleRebelVictoryCount;
				imperialVictories =
					g_remoteBattleImperialVictoryCount;
			}
			sprintf(g_frontendScratchBuffer,
				"%c%s %c%d   %c%s %c%d", 5,
				FrontendString_Get(
					FRONTSTR_342_IMPERIAL_VICTORIES),
				1, imperialVictories, 3,
				FrontendString_Get(
					FRONTSTR_343_REBEL_VICTORIES),
				1, rebelVictories);
			FrontendText_DrawCentered(12, g_frontendScratchBuffer,
						  &rect, 0xFFFF);
		} else {
			FrontendText_DrawAlignedInRect(
				15,
				FrontendString_Get(
					FRONTSTR_472_BATTLE_DESCRIPTION),
				&rect, 0, 1, 0xFFFF);
		}
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}

	FrontendDraw_RectAssign(&rect, 88, 157, 420, 301);
	lineCount = FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF,
					     4, 4096) +
		    1;
	if (lineCount > 9) {
		FrontendDraw_RectAssign(&rect, 421, 157, 430, 301);
		g_frontendFirstVisibleLine = FrontendScrollbar_Draw(
			&rect, g_frontendFirstVisibleLine, lineCount, 0, 5,
			(unsigned int)g_colorNavy, 9);
		FrontendDraw_RectAssign(&rect, 88, 157, 420, 301);
	} else {
		FrontendDraw_RectAssign(&rect, 88, 157, 430, 301);
	}
	FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF, 4,
				 g_frontendFirstVisibleLine);
	return 1;
}

/* Draws the Players in Game panel of the mission setup screen: each g_mpRoster
 * entry with a nonzero playerId, four to a column, as its rating and name, or
 * the join in progress text when it has neither; the local player's entry
 * pulses, as does every entry in a solo game. Sets
 * g_frontendGameSessionInProgress to 1 when Net_CountReadyPlayers returns more
 * than 1, else 0. Outside a solo game, over TCP/IP with internet play or over a
 * modem, each player but the host gets a latency light, light1 to light6 for
 * the average latency (capped at 749 ms) divided by 125, plus 1, and a
 * dropped-packet light, drop1 to drop6; a second pass gives each light a
 * tooltip with the latency, the dropped-packet percent and a quality rating,
 * 100 - latencyPenalty - 10 * dropPenalty / 100, where latencyPenalty is the
 * latency less 100, divided by 25, and dropPenalty is the drop rate in
 * hundredths of a percent less 100, each penalty kept from going under 0 and
 * the rating too. A network host that clicks another player's entry, left or
 * right, stores its index in g_missionSetupSelectedPlayerRosterIndex, or -1
 * when it was already selected; the selected entry is drawn on navy. Returns
 * 1. */
// FUNCTION: XVT 0x4E5B90
int MissionSetup_DrawPlayerRoster(int frameCounter)
{
	RECT rect;
	RECT previousClipRect;
	RECT lightRect;
	RECT qualityRect;
	int mouseX;
	int mouseY;
	int displayedCount;
	int rosterIndex;
	int averageLatency;
	int dropRate;
	int connectionQuality;
	int *playerId;
	int latencyPenalty;
	int dropPenalty;

	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&rect, 88, 311, 430, 326);
	FrontendText_DrawAlignedInRect(
		15, FrontendString_Get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0,
		1, 0xFFFF);
	if (Net_CountReadyPlayers() > 1) {
		g_frontendGameSessionInProgress = 1;
	} else {
		g_frontendGameSessionInProgress = 0;
	}

	displayedCount = 0;
	FrontendDraw_RectAssign(&rect, 88, 331, 258, 345);
	FrontImage_GetResourceRect("light1", &lightRect);

	for (rosterIndex = 0; rosterIndex < 8; ++rosterIndex) {
		MpRosterEntry *rosterEntry = &g_mpRoster[rosterIndex];
		playerId = &rosterEntry->playerId;
		if (*playerId == 0) {
			continue;
		}

		if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    Net_IsHost() != 0 && Net_GetLocalPlayerId() != *playerId &&
		    rosterIndex == g_missionSetupSelectedPlayerRosterIndex) {
			FrontendDraw_Rect(&rect, 0, 0, g_colorNavy, 1);
		}

		FrontendDisplay_GetScreenClipRect(&previousClipRect);
		FrontendDisplay_SetScreenClipRect640x480(&rect);
		if (rosterEntry->pilotRating != 0 ||
		    rosterEntry->name[0] != '\0') {
			sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
				FrontendString_Get(FRONTSTR_154_DRONE +
						   rosterEntry->pilotRating),
				1, rosterEntry->name);
		} else {
			sprintf(g_frontendScratchBuffer, "%s",
				FrontendString_Get(
					FRONTSTR_720_JOIN_IN_PROGRESS));
		}
		if (Net_GetLocalPlayerId() == *playerId ||
		    g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			FrontendText_DrawAlignedInRect(
				12, g_frontendScratchBuffer, &rect, 0, 1,
				g_pulseColorRamp[((frameCounter % 24) & ~1) >>
						 1]);
		} else {
			FrontendText_DrawAlignedInRect(
				12, g_frontendScratchBuffer, &rect, 0, 1,
				g_colorYellow);
		}
		FrontendDisplay_SetScreenClipRect640x480(&previousClipRect);

		if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    ((g_gameConfig.networkType == NET_TRANSPORT_TCPIP &&
		      g_gameConfig.internetPlay != 0) ||
		     g_gameConfig.networkType == NET_TRANSPORT_MODEM) &&
		    Net_GetHostPlayerId() != *playerId) {
			averageLatency = Net_GetAverageLatencyMs(*playerId);
			if (averageLatency > 749) {
				averageLatency = 749;
			}
			sprintf(g_frontendScratchBuffer, "light%d",
				averageLatency / 125 + 1);
			FrontImage_DrawSprite(g_frontendScratchBuffer,
					      rect.right - 33, rect.top + 2);
			/* dropRate here is the number of the drop%d sprite: the dropped-packet percent plus one, at
			 * most 6. */
			dropRate = Net_GetPacketDropRateBasisPoints(*playerId) /
					   100 +
				   1;
			if (dropRate > 6) {
				dropRate = 6;
			}
			sprintf(g_frontendScratchBuffer, "drop%d", dropRate);
			FrontImage_DrawSprite(g_frontendScratchBuffer,
					      rect.right - 33, rect.top + 9);
		}

		++displayedCount;
		if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    Net_IsHost() != 0 && Net_GetLocalPlayerId() != *playerId &&
		    FrontendDraw_PointInRect(&rect, mouseX, mouseY)) {
			FrontendDraw_RectOutline(&rect, 0, 0, g_colorGreen);
			if (FrontendMouse_GetLeftClick() != 0 ||
			    FrontendMouse_GetRightClick() != 0) {
				if (g_gameConfig.sfxDatapadEnabled != 0) {
					FrontendSound_PlayUISound(
						"jewelsound", 1, 0, 255,
						12 * g_gameConfig
								.sfxDatapadVolume,
						63);
				}
				if (rosterIndex ==
				    g_missionSetupSelectedPlayerRosterIndex) {
					g_missionSetupSelectedPlayerRosterIndex =
						-1;
				} else {
					g_missionSetupSelectedPlayerRosterIndex =
						rosterIndex;
				}
			}
		}
		if (displayedCount == 4) {
			FrontendDraw_RectAssign(&rect, 260, 331, 430, 345);
		} else {
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
		}
	}

	displayedCount = 0;
	FrontendDraw_RectAssign(&rect, 88, 331, 258, 345);
	FrontImage_GetResourceRect("light1", &lightRect);
	for (rosterIndex = 0; rosterIndex < 8; ++rosterIndex) {
		MpRosterEntry *rosterEntry = &g_mpRoster[rosterIndex];
		playerId = &rosterEntry->playerId;
		if (*playerId == 0) {
			continue;
		}
		if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    ((g_gameConfig.networkType == NET_TRANSPORT_TCPIP &&
		      g_gameConfig.internetPlay != 0) ||
		     g_gameConfig.networkType == NET_TRANSPORT_MODEM) &&
		    Net_GetHostPlayerId() != *playerId) {
			int lightWidth = lightRect.right - lightRect.left + 1;
			averageLatency = Net_GetAverageLatencyMs(*playerId);
			if (averageLatency > 749) {
				averageLatency = 749;
			}
			/* dropRate here is the drop rate in hundredths of a percent. */
			dropRate = Net_GetPacketDropRateBasisPoints(*playerId);
			latencyPenalty = averageLatency - 100;
			if (latencyPenalty < 0) {
				latencyPenalty = 0;
			}
			latencyPenalty /= 25;
			dropPenalty = dropRate - 100;
			if (dropPenalty < 0) {
				dropPenalty = 0;
			}
			connectionQuality =
				100 - latencyPenalty - 10 * dropPenalty / 100;
			if (connectionQuality < 0) {
				connectionQuality = 0;
			}
			sprintf(g_frontendScratchBuffer,
				"%s: %d %s  |  %s: %d%%  |  %s: %d%%",
				FrontendString_Get(
					FRONTSTR_755_AVERAGE_LATENCY),
				averageLatency,
				FrontendString_Get(FRONTSTR_756_MS),
				FrontendString_Get(
					FRONTSTR_758_PACKETS_DROPPED),
				dropRate / 100,
				FrontendString_Get(
					FRONTSTR_757_CONNECTION_QUALITY_RATING),
				connectionQuality);
			FrontendDraw_RectAssign(
				&qualityRect, rect.right - 33, rect.top + 2,
				rect.right - 33 + lightWidth,
				rect.top + 2 + lightRect.bottom -
					lightRect.top);
			FrontendButton_DrawSpriteAndTooltip(
				&qualityRect, NULL, g_frontendScratchBuffer, 12,
				0xFFFF);
		}
		++displayedCount;
		if (displayedCount == 4) {
			FrontendDraw_RectAssign(&rect, 260, 331, 430, 345);
		} else {
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
		}
	}
	return 1;
}

/* Sends a lobby selection packet to every player; MissionSetup_Update calls it
 * on the host when more than 5000 ms have passed since the last. Counting the
 * packet type as word 0, words 1 to 8 carry the game name, 10 and 11 the
 * mission type and its selected description id, 12 the value 8, and from word
 * 14 six words per player: id, pilot rating, average latency in ms, and its
 * packet, dropped packet and retry counts. While
 * g_missionSetupRosterAuthoritative is set it first clears the playerId of each
 * g_mpRoster entry with no ready network player of that id, then sends all
 * eight entries and 8 in word 13; otherwise it sends each ready network player,
 * its rating being the first byte of its player info minus 1, and the ready
 * count in word 13. Word 9 is not set. Returns 1. */
// FUNCTION: XVT 0x4E6120
int MissionSetup_BroadcastLobbySelection(void)
{
	const int rosterCapacity = 8;
	const int headerWordCount = 14;
	int *packetWords = &g_frontendNetPacketScratch.packetType;
	int packetWordCount;
	int rosterCount;
	int missionDescriptionId;
	int missionDirectoryId;
	int playerRosterIndex;
	int rosterIndex;
	NetPlayerInfo *playerRoster;

	g_frontendNetPacketScratch.packetType = NET_PACKET_LOBBY_SELECTION;
	memcpy(g_frontendNetPacketScratch.payload,
	       g_pilotData.multiplayerGameName,
	       sizeof(g_pilotData.multiplayerGameName));
	missionDirectoryId = g_pilotData.missionDirectoryId;
	packetWords[10] = missionDirectoryId;
	missionDescriptionId =
		g_pilotData.missionDescriptionIds[missionDirectoryId];
	packetWords[11] = missionDescriptionId;
	if (g_missionSetupRosterAuthoritative != 0) {
		packetWordCount = headerWordCount;
		packetWords[12] = rosterCapacity;
		packetWords[13] = rosterCapacity;
		playerRoster = Net_GetPlayerRoster(&rosterCount);
		/* Before sending, clear each roster entry with no ready player in the network roster. */
		for (rosterIndex = 0; rosterIndex < rosterCapacity;
		     ++rosterIndex) {
			MpRosterEntry *rosterEntry = &g_mpRoster[rosterIndex];

			playerRosterIndex = 0;
			if (rosterCount > 0) {
				NetPlayerInfo *player = playerRoster;

				do {
					if (player->playerId != 0 &&
					    player->readyFlag != 0 &&
					    rosterEntry->playerId ==
						    (int)player->playerId) {
						break;
					}
					++player;
					++playerRosterIndex;
				} while (playerRosterIndex < rosterCount);
			}
			if (playerRosterIndex == rosterCount) {
				rosterEntry->playerId = 0;
			}
		}

		for (rosterIndex = 0; rosterIndex < rosterCapacity;
		     ++rosterIndex) {
			const MpRosterEntry *rosterEntry =
				&g_mpRoster[rosterIndex];
			int playerId = rosterEntry->playerId;

			packetWords[packetWordCount++] = playerId;
			packetWords[packetWordCount++] =
				rosterEntry->pilotRating;
			packetWords[packetWordCount++] =
				Net_GetAverageLatencyMs(playerId);
			packetWords[packetWordCount++] =
				Net_GetPlayerPacketCount(playerId);
			packetWords[packetWordCount++] =
				Net_GetPlayerPacketDropCount(playerId);
			packetWords[packetWordCount++] =
				Net_GetPlayerPacketRetryCount(playerId);
		}
	} else {
		packetWords[12] = rosterCapacity;
		packetWordCount = headerWordCount;
		packetWords[13] = Net_CountReadyPlayers();
		playerRoster = Net_GetPlayerRoster(&rosterCount);
		rosterIndex = 0;
		if (rosterCount > 0) {
			NetPlayerInfo *player = playerRoster;

			do {
				if (player->readyFlag == 1) {
					packetWords[packetWordCount++] =
						player->playerId;
					packetWords[packetWordCount++] =
						(int)(uint8_t)
							player->longName[0] -
						1;
					packetWords[packetWordCount++] =
						Net_GetAverageLatencyMs(
							player->playerId);
					packetWords[packetWordCount++] =
						Net_GetPlayerPacketCount(
							player->playerId);
					packetWords[packetWordCount++] =
						Net_GetPlayerPacketDropCount(
							player->playerId);
					packetWords[packetWordCount++] =
						Net_GetPlayerPacketRetryCount(
							player->playerId);
				}
				++player;
				++rosterIndex;
			} while (rosterIndex < rosterCount);
		}
	}
	Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
			       (unsigned int)(packetWordCount * sizeof(int)));
	return 1;
}

/* Sends the host's lobby state to toPlayerId, 0 meaning every player. First a
 * STATE packet: counting the type as word 0, words 1 to 8 carry the game name,
 * 10 and 11 the mission type and its selected description id, 12 the value 8,
 * and from word 14 three words per player: id, pilot rating and average latency
 * in ms. While g_missionSetupRosterAuthoritative is set it first refreshes each
 * g_mpRoster entry's rating from its ready network player, or clears its
 * playerId when there is none, sends all eight entries with 8 in word 13, and
 * then a LOADOUT_ROSTER packet with each entry's five loadout fields. Otherwise
 * it sends each ready network player, its rating being the first byte of its
 * player info minus 1, with Net_CountReadyPlayers() in word 13, and for a
 * battle a BATTLE_PROGRESS packet: whether the selected mission's saved
 * continuation is active (the solo one in a solo game, else the network one),
 * the continue choice, its current mission index, and the Rebel and Imperial
 * victories over its results up to that index, at most 10. Last a GAME_OPTIONS
 * packet of 19 words: the type, g_gameConfig settings, and in word 13 a rand()
 * value. Word 9 of the STATE packet is not set. Returns what
 * Net_SendPacketAndFlush returns for the options packet. */
// FUNCTION: XVT 0x4E6310
int MissionSetup_SendLobbyState(int toPlayerId)
{
	enum {
		PACKET_HEADER_WORD_COUNT = 14,
		ROSTER_RECORD_WORD_COUNT = 3,
		LOADOUT_RECORD_WORD_COUNT = 5,
	};

	int *packetWords;
	int packetWordCount;
	int readyPacketWordCount;
	int rosterCapacity;
	int rosterCount;
	NetPlayerInfo *playerRoster;
	int playerRosterIndex;
	int rosterIndex;
	int missionDirectoryId;

	packetWords = &g_frontendNetPacketScratch.packetType;
	rosterCapacity = (int)(sizeof(g_mpRoster) / sizeof(g_mpRoster[0]));
	packetWords[0] = NET_PACKET_STATE;
	memcpy(g_frontendNetPacketScratch.payload,
	       g_pilotData.multiplayerGameName,
	       sizeof(g_pilotData.multiplayerGameName));
	missionDirectoryId = g_pilotData.missionDirectoryId;
	packetWords[10] = missionDirectoryId;
	packetWords[11] = g_pilotData.missionDescriptionIds[missionDirectoryId];
	if (g_missionSetupRosterAuthoritative != 0) {
		packetWordCount = PACKET_HEADER_WORD_COUNT;
		packetWords[12] = rosterCapacity;
		packetWords[13] = rosterCapacity;
		playerRoster = Net_GetPlayerRoster(&rosterCount);
		/* Before sending, refresh each roster entry's pilot rating from the network roster, and clear
		 * entries with no ready player there. */
		for (rosterIndex = 0; rosterIndex < rosterCapacity;
		     ++rosterIndex) {
			MpRosterEntry *rosterEntry = &g_mpRoster[rosterIndex];

			for (playerRosterIndex = 0;
			     playerRosterIndex < rosterCount;
			     ++playerRosterIndex) {
				if (playerRoster[playerRosterIndex].playerId !=
					    0 &&
				    playerRoster[playerRosterIndex].readyFlag !=
					    0 &&
				    playerRoster[playerRosterIndex].playerId ==
					    (DPID)rosterEntry->playerId) {
					rosterEntry->pilotRating =
						(PilotRating)(uint8_t)playerRoster
							[playerRosterIndex]
								.longName[0] -
						1;
					break;
				}
			}
			if (playerRosterIndex == rosterCount) {
				rosterEntry->playerId = 0;
			}
		}
		for (rosterIndex = 0; rosterIndex < rosterCapacity;
		     ++rosterIndex) {
			const MpRosterEntry *rosterEntry =
				&g_mpRoster[rosterIndex];
			int playerId = rosterEntry->playerId;

			packetWords[packetWordCount++] = playerId;
			packetWords[packetWordCount++] =
				rosterEntry->pilotRating;
			packetWords[packetWordCount++] =
				Net_GetAverageLatencyMs(playerId);
		}
		Net_SendPacketAndFlush(toPlayerId, &g_frontendNetPacketScratch,
				       (unsigned int)(packetWordCount *
						      sizeof(packetWords[0])));

		packetWordCount = 1;
		packetWords[0] = NET_PACKET_LOADOUT_ROSTER;
		for (rosterIndex = 0; rosterIndex < rosterCapacity;
		     ++rosterIndex) {
			const MpRosterEntry *rosterEntry =
				&g_mpRoster[rosterIndex];

			packetWords[packetWordCount++] =
				rosterEntry->craftTypeOverride;
			packetWords[packetWordCount++] =
				rosterEntry->craftOptionIndex;
			packetWords[packetWordCount++] =
				rosterEntry->warheadOptionIndex;
			packetWords[packetWordCount++] =
				rosterEntry->beamOptionIndex;
			packetWords[packetWordCount++] =
				rosterEntry->countermeasureOptionIndex;
		}
		Net_SendPacketAndFlush(toPlayerId, &g_frontendNetPacketScratch,
				       (unsigned int)(packetWordCount *
						      sizeof(packetWords[0])));
	} else {
		readyPacketWordCount = PACKET_HEADER_WORD_COUNT;
		packetWords[12] = rosterCapacity;
		packetWords[13] = Net_CountReadyPlayers();
		playerRoster = Net_GetPlayerRoster(&rosterCount);
		for (rosterIndex = 0; rosterIndex < rosterCount;
		     ++rosterIndex) {
			const NetPlayerInfo *player =
				&playerRoster[rosterIndex];

			if (player->readyFlag != 1) {
				continue;
			}
			packetWords[readyPacketWordCount++] = player->playerId;
			packetWords[readyPacketWordCount++] =
				(int)(uint8_t)player->longName[0] - 1;
			packetWords[readyPacketWordCount++] =
				Net_GetAverageLatencyMs(player->playerId);
		}
		Net_SendPacketAndFlush(toPlayerId, &g_frontendNetPacketScratch,
				       (unsigned int)(readyPacketWordCount *
						      sizeof(packetWords[0])));

		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_BATTLES) {
			const int *missionIndex;
			int imperialVictories = 0;
			int rebelVictories = 0;
			int resultIndex;
			int currentMissionIndex;

			packetWords[0] = NET_PACKET_BATTLE_PROGRESS;
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				missionIndex =
					&g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx;
				packetWords[1] = g_pilotData
							 .spBattleContinuations
								 [*missionIndex]
							 .isActive;
				packetWords[2] =
					(uint8_t)g_gameConfig
						.continueBattleOrCampaign;
				packetWords[3] = g_pilotData
							 .spBattleContinuations
								 [*missionIndex]
							 .sequenceState
							 .currentMissionIndex;
				currentMissionIndex =
					g_pilotData
						.spBattleContinuations
							[*missionIndex]
						.sequenceState
						.currentMissionIndex;
				resultIndex = 0;
				if (currentMissionIndex >= 0) {
					do {
						if (resultIndex >=
						    (int)(sizeof(g_pilotData
									 .spBattleContinuations
										 [*missionIndex]
									 .sequenceState
									 .missionResults) /
							  sizeof(g_pilotData
									 .spBattleContinuations
										 [*missionIndex]
									 .sequenceState
									 .missionResults
										 [0]))) {
							break;
						}
						switch (g_pilotData
								.spBattleContinuations
									[*missionIndex]
								.sequenceState
								.missionResults
									[resultIndex]) {
						case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
							++imperialVictories;
							break;
						case BATTLE_MISSION_RESULT_REBEL_VICTORY:
							++rebelVictories;
							break;
						default:
							break;
						}
						++resultIndex;
					} while (resultIndex <=
						 currentMissionIndex);
				}
			} else {
				missionIndex =
					&g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx;
				packetWords[1] = g_pilotData
							 .mpBattleContinuations
								 [*missionIndex]
							 .isActive;
				packetWords[2] =
					(uint8_t)g_gameConfig
						.continueBattleOrCampaign;
				packetWords[3] = g_pilotData
							 .mpBattleContinuations
								 [*missionIndex]
							 .sequenceState
							 .currentMissionIndex;
				currentMissionIndex =
					g_pilotData
						.mpBattleContinuations
							[*missionIndex]
						.sequenceState
						.currentMissionIndex;
				resultIndex = 0;
				if (currentMissionIndex >= 0) {
					do {
						if (resultIndex >=
						    (int)(sizeof(g_pilotData
									 .mpBattleContinuations
										 [*missionIndex]
									 .sequenceState
									 .missionResults) /
							  sizeof(g_pilotData
									 .mpBattleContinuations
										 [*missionIndex]
									 .sequenceState
									 .missionResults
										 [0]))) {
							break;
						}
						switch (g_pilotData
								.mpBattleContinuations
									[*missionIndex]
								.sequenceState
								.missionResults
									[resultIndex]) {
						case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
							++imperialVictories;
							break;
						case BATTLE_MISSION_RESULT_REBEL_VICTORY:
							++rebelVictories;
							break;
						default:
							break;
						}
						++resultIndex;
					} while (resultIndex <=
						 currentMissionIndex);
				}
			}
			packetWords[4] = rebelVictories;
			packetWords[5] = imperialVictories;
			Net_SendPacketAndFlush(toPlayerId,
					       &g_frontendNetPacketScratch,
					       6 * sizeof(packetWords[0]));
		}
	}

	packetWords[0] = NET_PACKET_GAME_OPTIONS;
	packetWords[1] = (uint8_t)g_gameConfig.difficulty;
	packetWords[2] = g_gameConfig.collisions;
	packetWords[3] = g_gameConfig.craftJumping;
	packetWords[4] = g_gameConfig.randomSetup;
	packetWords[5] = (uint8_t)g_gameConfig.battleLengthIndex;
	packetWords[6] = g_gameConfig.requirePassword;
	packetWords[7] = g_gameConfig.inProgressJoin;
	packetWords[8] = (uint8_t)g_gameConfig.craftSelection;
	packetWords[9] = g_gameConfig.locatePlayers;
	packetWords[10] = (uint8_t)g_gameConfig.craftWaves;
	packetWords[11] = g_gameConfig.missionTimeLimit;
	packetWords[12] = g_gameConfig.lastTeamTimeLimitMinutes;
	packetWords[13] = rand();
	packetWords[14] = g_gameConfig.internetPlay;
	packetWords[15] = g_gameConfig.aiOpponents;
	packetWords[16] = g_gameConfig.serverUpdateRate;
	packetWords[17] = (uint8_t)g_gameConfig.combatBalance;
	packetWords[18] = (uint8_t)g_gameConfig.continueBattleOrCampaign;
	return Net_SendPacketAndFlush(toPlayerId, &g_frontendNetPacketScratch,
				      19 * sizeof(packetWords[0]));
}

/* Sends a READY_ROSTER packet to toPlayerId, 0 meaning every player (its one
 * caller, MissionSetup_Update, passes 0): 8, the ready player count, then for
 * each ready network player its id, its rating (the first byte of its player
 * info minus 1) and its average latency in ms. Returns what
 * Net_SendPacketAndFlush returns. */
// FUNCTION: XVT 0x4E6750
int MissionSetup_BroadcastReadyRoster(int toPlayerId)
{
	enum { ROSTER_CAPACITY = 8, PACKET_HEADER_WORD_COUNT = 3 };

	NetPlayerInfo *roster;
	int rosterCount;
	int rosterIndex;
	int packetWordCount;
	int *packetWords;

	packetWords = (int *)&g_frontendNetPacketScratch;
	packetWords[0] = NET_PACKET_READY_ROSTER;
	packetWords[1] = ROSTER_CAPACITY;
	packetWordCount = PACKET_HEADER_WORD_COUNT;
	packetWords[2] = Net_CountReadyPlayers();
	roster = Net_GetPlayerRoster(&rosterCount);
	for (rosterIndex = 0; rosterIndex < rosterCount; ++rosterIndex) {
		if (roster[rosterIndex].readyFlag == 1) {
			packetWords[packetWordCount++] =
				roster[rosterIndex].playerId;
			packetWords[packetWordCount++] =
				(int)(uint8_t)roster[rosterIndex].longName[0] -
				1;
			packetWords[packetWordCount++] =
				Net_GetAverageLatencyMs(
					roster[rosterIndex].playerId);
		}
	}
	return Net_SendPacketAndFlush(
		toPlayerId, &g_frontendNetPacketScratch,
		(unsigned int)(packetWordCount * sizeof(int)));
}

/* The mission list that drops down on the mission setup screen, a pushed screen
 * run each frame. On frame 0 it counts the rows in
 * g_missionSetupMissionListRowCount, one per available mission and one per
 * section heading, and sets g_missionSetupMissionListScrollOffset to the
 * selected mission's index in g_missionList, or 0 when the count is under 20.
 * Outside a solo game a lobby state from the host loads the host's mission, as
 * MissionSetup_Update does. It draws up to 19 rows, section headings red and
 * missions white, the selected one yellow and, outside a solo game, a
 * single-player mission (file name starting with "1") gray, with a scrollbar
 * when there are more rows; then each mission's award beside it: a medal
 * (medlvl) or, for training and combat engagements, a citation (citlvl for the
 * Imperial faction, rcitlvl for the Rebel), with a tooltip naming it. A solo
 * game shows the current faction's award; otherwise the lower nonzero award
 * level of the two factions, the tooltip naming each faction's. A click on a
 * mission selects it (outside a solo game only when its file name does not
 * start with "1"), takes a saved battle's length and setup or a saved
 * campaign's setup, loads the mission and its text and, outside a solo game,
 * sends the lobby state; a click on a mission row, or a click outside the list,
 * closes the list. Returns 0. When g_missionList is NULL it pops the screen and
 * goes on drawing. In a solo game a campaign picked here takes its setup from
 * spCampaignContinuations at the mission id plus 12 unless Net_IsHost returns
 * nonzero. */
// FUNCTION: XVT 0x4E67F0
int MissionSetup_DrawMissionList(int frameCounter)
{
	enum {
		VISIBLE_ROW_COUNT = 19,
		ROW_HEIGHT = 15,
		LIST_LEFT = 84,
		LIST_TOP = 131,
		LIST_RIGHT = 434,
		LIST_SCROLLBAR_RIGHT = 423,
		LIST_BOTTOM_PADDING = 139,
		MISSION_TEXT_INDENT = 37,
		TEXT_FONT_SIZE = 12,
		SCROLLBAR_CONTROL_ID = 8,
		SINGLE_PLAYER_FILE_PREFIX = '1',
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
		AWARD_TOOLTIP_MEDAL_BASE = FRONTSTR_381_BATTLE_MEDALLION,
		AWARD_TOOLTIP_CITATION_BASE = FRONTSTR_659_QUICK_START,
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
	};

	RECT rect;
	int cursorX;
	int cursorY;
	RECT awardRect;
	int missionListIndex;
	int displayRow;
	unsigned int awardId;
	int awardFactionId;
	unsigned int imperialAwardId;
	unsigned int rebelAwardId;
	unsigned short textColor;
	char lastSectionName[sizeof(g_missionList[0].sectionName)];

	if (g_missionList == NULL) {
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
	}

	if (frameCounter == 0) {
		g_missionSetupMissionListRowCount = g_missionCount;
		memset(lastSectionName, 0, sizeof(lastSectionName));
		for (missionListIndex = 0;
		     (unsigned int)missionListIndex < g_missionCount;
		     ++missionListIndex) {
			if (g_missionList[missionListIndex].isUnavailable !=
			    0) {
				--g_missionSetupMissionListRowCount;
			} else {
				if (g_pilotData.missionDescriptionIds
					    [g_pilotData.missionDirectoryId] ==
				    g_missionList[missionListIndex]
					    .missionIdx) {
					g_missionSetupMissionListScrollOffset =
						missionListIndex;
				}
				if (strcmp(g_missionList[missionListIndex]
						   .sectionName,
					   lastSectionName) != 0) {
					++g_missionSetupMissionListRowCount;
					strcpy(lastSectionName,
					       g_missionList[missionListIndex]
						       .sectionName);
				}
			}
		}
		if (g_missionSetupMissionListRowCount < VISIBLE_ROW_COUNT + 1) {
			g_missionSetupMissionListScrollOffset = 0;
		}
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    FrontendNet_ProcessNetworkPackets() == NET_PACKET_STATE) {
		if (g_pilotData.missionDirectoryId !=
			    g_frontendNetReceivedMissionDirectoryId ||
		    g_pilotData.missionDescriptionIds
				    [g_frontendNetReceivedMissionDirectoryId] !=
			    g_frontendNetReceivedMissionDescriptionId) {
			g_gameConfig.continueBattleOrCampaign =
				SEQUENCE_CONTINUE;
		}
		g_pilotData.missionDirectoryId =
			g_frontendNetReceivedMissionDirectoryId;
		g_pilotData.missionDescriptionIds
			[g_frontendNetReceivedMissionDirectoryId] =
			g_frontendNetReceivedMissionDescriptionId;
		FrontendMission_LoadCurrent();
		FrontImage_FreeResourceByName("background");
		MissionSetup_DrawBackground();
		MissionSetup_LoadMissionDescText(g_missionText);
		MissionSetup_UpdateTeamCounts();
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			while ((unsigned int)g_selectedMissionListIndex <
				       g_missionCount &&
			       g_missionList[g_selectedMissionListIndex]
					       .missionIdx !=
				       g_pilotData.missionDescriptionIds
					       [g_pilotData
							.missionDirectoryId]) {
				++g_selectedMissionListIndex;
			}
		}
		g_frontendFirstVisibleLine = 0;
		g_missionSetupSelectedPlayerRosterIndex = -1;
		if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    (g_pilotData.missionDirectoryId ==
			     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
		     g_pilotData.missionDirectoryId ==
			     MISSION_DIRECTORY_BATTLES) &&
		    g_gameConfig.craftWaves == CRAFT_WAVES_UNLIMITED) {
			g_gameConfig.craftWaves = CRAFT_WAVES_DEFAULT;
		}
		if (g_gameConfig.craftSelection == CRAFT_SELECTION_HOST_ONLY &&
		    (g_frontendMissionSessionMode ==
			     FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		     (g_pilotData.missionDirectoryId !=
			      MISSION_DIRECTORY_MELEES &&
		      g_pilotData.missionDirectoryId !=
			      MISSION_DIRECTORY_TOURNAMENTS))) {
			g_gameConfig.craftSelection = CRAFT_SELECTION_ON;
		}
		if (g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_BATTLES &&
		    g_gameConfig.randomSetup == 2) {
			g_gameConfig.randomSetup = 0;
		}
		if (g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_CAMPAIGNS &&
		    g_gameConfig.difficulty == GAME_DIFFICULTY_EASY_CHEAT) {
			g_gameConfig.difficulty = GAME_DIFFICULTY_EASY;
		}
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_CAMPAIGNS) {
			g_gameConfig.randomSetup = 0;
		}
	}

	memset(lastSectionName, 0, sizeof(lastSectionName));
	if (g_missionSetupMissionListRowCount > VISIBLE_ROW_COUNT) {
		FrontendDraw_RectAssign(&rect, 424, LIST_TOP, LIST_RIGHT, 424);
		g_missionSetupMissionListScrollOffset = FrontendScrollbar_Draw(
			&rect, g_missionSetupMissionListScrollOffset,
			g_missionSetupMissionListRowCount, 0, 5,
			(unsigned int)g_colorNavy, SCROLLBAR_CONTROL_ID);
	}

	FrontendCursor_GetPos(&cursorX, &cursorY);
	/* displayRow first holds the number of rows the list box shows, to size the box; from the list loop on
	 * it is the running row index, headers included. */
	displayRow = VISIBLE_ROW_COUNT;
	if (g_missionSetupMissionListRowCount <= VISIBLE_ROW_COUNT) {
		displayRow = g_missionSetupMissionListRowCount;
	}
	FrontendDraw_RectAssign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				ROW_HEIGHT * displayRow + LIST_BOTTOM_PADDING);
	if (!FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
	    (FrontendMouse_GetLeftClick() != 0 ||
	     FrontendMouse_GetRightClick() != 0)) {
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
		Frontend_UnregisterScrollableControl(SCROLLBAR_CONTROL_ID);
		FrontImage_FreeResourceByName("background");
		MissionSetup_DrawBackground();
	}

	FrontendDraw_RectAssign(&rect, LIST_LEFT, LIST_TOP,
				g_missionSetupMissionListRowCount <=
						VISIBLE_ROW_COUNT
					? LIST_RIGHT
					: LIST_SCROLLBAR_RIGHT,
				ROW_HEIGHT * displayRow + LIST_BOTTOM_PADDING);
	FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 64), 1);
	FrontendDraw_Rect(&rect, 0, 0, 0xFFFF, 0);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	FrontendDraw_Rect(&rect, 0, 0, 0, 0);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	rect.left += MISSION_TEXT_INDENT;
	rect.bottom = rect.top + 14;

	displayRow = 0;
	for (missionListIndex = 0;
	     (unsigned int)missionListIndex < g_missionCount;
	     ++missionListIndex) {
		if (g_missionList[missionListIndex].isUnavailable != 0) {
			continue;
		}
		if (strcmp(g_missionList[missionListIndex].sectionName,
			   lastSectionName) != 0) {
			if (displayRow >=
				    g_missionSetupMissionListScrollOffset &&
			    displayRow - g_missionSetupMissionListScrollOffset <
				    VISIBLE_ROW_COUNT) {
				rect.left -= MISSION_TEXT_INDENT;
				FrontendText_DrawAlignedInRect(
					TEXT_FONT_SIZE,
					g_missionList[missionListIndex]
						.sectionName,
					&rect, 0, 1, g_colorRed);
				rect.left += MISSION_TEXT_INDENT;
				FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
			}
			strcpy(lastSectionName,
			       g_missionList[missionListIndex].sectionName);
			++displayRow;
		}
		if (displayRow - g_missionSetupMissionListScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (displayRow < g_missionSetupMissionListScrollOffset ||
		    displayRow - g_missionSetupMissionListScrollOffset >=
			    VISIBLE_ROW_COUNT) {
			++displayRow;
			if (displayRow -
				    g_missionSetupMissionListScrollOffset >=
			    VISIBLE_ROW_COUNT) {
				break;
			}
			continue;
		} else {
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY)) {
				FrontendDraw_Rect(&rect, 0, 0, g_colorGreen, 0);
				if (FrontendMouse_GetLeftClick() != 0 ||
				    FrontendMouse_GetRightClick() != 0) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}

					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_missionList
							    [missionListIndex]
								    .fileName
									    [0] !=
						    SINGLE_PLAYER_FILE_PREFIX) {
							g_pilotData.missionDescriptionIds
								[g_pilotData
									 .missionDirectoryId] =
								g_missionList[missionListIndex]
									.missionIdx;
							g_selectedMissionListIndex =
								0;
							while ((unsigned int)g_selectedMissionListIndex <
								       g_missionCount &&
							       g_missionList[g_selectedMissionListIndex]
									       .missionIdx !=
								       g_pilotData
									       .missionDescriptionIds
										       [g_pilotData
												.missionDirectoryId]) {
								++g_selectedMissionListIndex;
							}

							if (g_pilotData
								    .missionDirectoryId ==
							    MISSION_DIRECTORY_BATTLES) {
								if (g_frontendMissionSessionMode ==
								    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
									if (g_pilotData
										    .spBattleContinuations
											    [g_missionList[g_selectedMissionListIndex]
												     .missionIdx]
										    .isActive !=
									    0) {
										g_gameConfig
											.battleLengthIndex =
											g_pilotData
												.spBattleContinuations
													[g_missionList[g_selectedMissionListIndex]
														 .missionIdx]
												.battleLengthIndex;
										g_gameConfig
											.randomSetup =
											g_pilotData
												.spBattleContinuations
													[g_missionList[g_selectedMissionListIndex]
														 .missionIdx]
												.randomSetup;
									}
								} else if (
									g_pilotData
										.mpBattleContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.isActive !=
									0) {
									g_gameConfig
										.battleLengthIndex =
										g_pilotData
											.mpBattleContinuations
												[g_missionList[g_selectedMissionListIndex]
													 .missionIdx]
											.battleLengthIndex;
									g_gameConfig
										.randomSetup =
										g_pilotData
											.mpBattleContinuations
												[g_missionList[g_selectedMissionListIndex]
													 .missionIdx]
											.randomSetup;
								}
							} else if (
								g_pilotData
									.missionDirectoryId ==
								MISSION_DIRECTORY_CAMPAIGNS) {
								if (g_frontendMissionSessionMode ==
								    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
									if (g_pilotData
										    .spCampaignContinuations
											    [g_missionList[g_selectedMissionListIndex]
												     .missionIdx]
										    .isActive !=
									    0) {
										g_gameConfig
											.randomSetup =
											g_pilotData
												.spCampaignContinuations
													[g_missionList[g_selectedMissionListIndex]
														 .missionIdx]
												.randomSetup;
									}
								} else {
									if (Net_IsHost() !=
									    0) {
										if (g_pilotData
											    .mpCampaignContinuations
												    [g_missionList[g_selectedMissionListIndex]
													     .missionIdx]
											    .isActive !=
										    0) {
											g_gameConfig
												.randomSetup =
												g_pilotData
													.mpCampaignContinuations
														[g_missionList[g_selectedMissionListIndex]
															 .missionIdx]
													.randomSetup;
										}
									} else if (
										g_pilotData
											.mpCampaignContinuations
												[g_missionList[g_selectedMissionListIndex]
													 .missionIdx +
												 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
											.isActive !=
										0) {
										g_gameConfig
											.randomSetup =
											g_pilotData
												.mpCampaignContinuations
													[g_missionList[g_selectedMissionListIndex]
														 .missionIdx +
													 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
												.randomSetup;
									}
								}
							}
							FrontendMission_LoadCurrent();
							FrontImage_FreeResourceByName(
								"background");
							MissionSetup_DrawBackground();
							MissionSetup_UpdateTeamCounts();
							MissionSetup_LoadMissionDescText(
								g_missionText);
							g_frontendFirstVisibleLine =
								0;
							if (g_frontendMissionSessionMode !=
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								MissionSetup_SendLobbyState(
									0);
							}
						}
					} else {
						g_pilotData.missionDescriptionIds
							[g_pilotData
								 .missionDirectoryId] =
							g_missionList
								[missionListIndex]
									.missionIdx;
						g_selectedMissionListIndex = 0;
						while ((unsigned int)g_selectedMissionListIndex <
							       g_missionCount &&
						       g_missionList[g_selectedMissionListIndex]
								       .missionIdx !=
							       g_pilotData.missionDescriptionIds
								       [g_pilotData
										.missionDirectoryId]) {
							++g_selectedMissionListIndex;
						}

						if (g_pilotData
							    .missionDirectoryId ==
						    MISSION_DIRECTORY_BATTLES) {
							if (g_pilotData
								    .spBattleContinuations
									    [g_missionList[g_selectedMissionListIndex]
										     .missionIdx]
								    .isActive !=
							    0) {
								g_gameConfig
									.battleLengthIndex =
									g_pilotData
										.spBattleContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.battleLengthIndex;
								g_gameConfig
									.randomSetup =
									g_pilotData
										.spBattleContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.randomSetup;
							}
						} else if (
							g_pilotData
								.missionDirectoryId ==
							MISSION_DIRECTORY_CAMPAIGNS) {
							if (Net_IsHost() != 0) {
								if (g_pilotData
									    .spCampaignContinuations
										    [g_missionList[g_selectedMissionListIndex]
											     .missionIdx]
									    .isActive !=
								    0) {
									g_gameConfig
										.randomSetup =
										g_pilotData
											.spCampaignContinuations
												[g_missionList[g_selectedMissionListIndex]
													 .missionIdx]
											.randomSetup;
								}
							} else if (
								g_pilotData
									.spCampaignContinuations
										[g_missionList[g_selectedMissionListIndex]
											 .missionIdx +
										 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
									.isActive !=
								0) {
								g_gameConfig
									.randomSetup =
									g_pilotData
										.spCampaignContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx +
											 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
										.randomSetup;
							}
						}
						FrontendMission_LoadCurrent();
						FrontImage_FreeResourceByName(
							"background");
						MissionSetup_DrawBackground();
						MissionSetup_UpdateTeamCounts();
						MissionSetup_LoadMissionDescText(
							g_missionText);
						g_frontendFirstVisibleLine = 0;
					}

					Keyboard_FlushCharBuffer();
					FrontendScreen_PopState();
					Frontend_UnregisterScrollableControl(
						SCROLLBAR_CONTROL_ID);
					FrontImage_FreeResourceByName(
						"background");
					MissionSetup_DrawBackground();
				}
			}

			textColor = 0xFFFF;
			if (g_pilotData.missionDescriptionIds
				    [g_pilotData.missionDirectoryId] ==
			    g_missionList[missionListIndex].missionIdx) {
				textColor = g_colorYellow;
			}
			if (g_missionList[missionListIndex].fileName[0] ==
				    SINGLE_PLAYER_FILE_PREFIX &&
			    g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				textColor = g_colorGray;
			}
			FrontendText_DrawAlignedInRect(
				TEXT_FONT_SIZE,
				g_missionList[missionListIndex].description,
				&rect, 0, 1, textColor);
			FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
		}
		++displayRow;
		if (displayRow - g_missionSetupMissionListScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}

	memset(lastSectionName, 0, sizeof(lastSectionName));
	FrontendDraw_RectAssign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				ROW_HEIGHT * missionListIndex +
					LIST_BOTTOM_PADDING);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	displayRow = 0;
	rect.left += MISSION_TEXT_INDENT;
	rect.bottom = rect.top + 14;

	for (missionListIndex = 0;
	     (unsigned int)missionListIndex < g_missionCount;
	     ++missionListIndex) {
		if (g_missionList[missionListIndex].isUnavailable != 0) {
			continue;
		}
		if (strcmp(g_missionList[missionListIndex].sectionName,
			   lastSectionName) != 0) {
			if (displayRow >=
				    g_missionSetupMissionListScrollOffset &&
			    displayRow - g_missionSetupMissionListScrollOffset <
				    VISIBLE_ROW_COUNT) {
				FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
			}
			strcpy(lastSectionName,
			       g_missionList[missionListIndex].sectionName);
			++displayRow;
		}
		if (displayRow - g_missionSetupMissionListScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (displayRow < g_missionSetupMissionListScrollOffset ||
		    displayRow - g_missionSetupMissionListScrollOffset >=
			    VISIBLE_ROW_COUNT) {
			++displayRow;
			if (displayRow -
				    g_missionSetupMissionListScrollOffset >=
			    VISIBLE_ROW_COUNT) {
				break;
			}
			continue;
		} else {
			awardId = 0;
			rebelAwardId = 0;
			imperialAwardId = 0;
			awardFactionId = 0;
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				awardFactionId = g_pilotData.currentFactionId;
				switch (g_pilotData.missionDirectoryId) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spTrainingMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_MELEES:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spMeleeMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spTournaments
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spCombatMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_BATTLES:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spBattles
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					break;
				default:
					break;
				}
			} else {
				switch (g_pilotData.missionDirectoryId) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					rebelAwardId =
						g_pilotData.factionStatistics[0]
							.mpTrainingMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpTrainingMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					awardId = rebelAwardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_MELEES:
					rebelAwardId =
						g_pilotData.factionStatistics[0]
							.mpMeleeMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpMeleeMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					awardId = rebelAwardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					rebelAwardId =
						g_pilotData.factionStatistics[0]
							.mpTournaments
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpTournaments
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					awardId = rebelAwardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					rebelAwardId =
						g_pilotData.factionStatistics[0]
							.mpCombatMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpCombatMissions
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					awardId = rebelAwardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_BATTLES:
					rebelAwardId =
						g_pilotData.factionStatistics[0]
							.mpBattles
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpBattles
								[g_missionList[missionListIndex]
									 .missionIdx]
							.awardLevel;
					awardId = rebelAwardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				default:
					break;
				}
			}

			if (awardId != 0) {
				FrontendDraw_RectCopy(&awardRect, &rect);
				awardRect.left -= MISSION_TEXT_INDENT;
				awardRect.right =
					awardRect.left + MISSION_TEXT_INDENT;
				if (g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				    g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_TRAINING_EXERCISES) {
					sprintf(g_frontendScratchBuffer,
						"medlvl%d", awardId);
					FrontImage_DrawSprite(
						g_frontendScratchBuffer,
						awardRect.left, awardRect.top);
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (rebelAwardId == 0) {
							if (imperialAwardId !=
							    0) {
								sprintf(g_frontendScratchBuffer,
									"%s: %s",
									FrontendString_Get(
										FRONTSTR_759_IMPERIAL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
												   imperialAwardId)));
							} else {
								sprintf(g_frontendScratchBuffer,
									"%s: %s, %s: %s",
									FrontendString_Get(
										FRONTSTR_760_REBEL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
												   rebelAwardId)),
									FrontendString_Get(
										FRONTSTR_759_IMPERIAL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
												   imperialAwardId)));
							}
						} else if (imperialAwardId !=
							   0) {
							sprintf(g_frontendScratchBuffer,
								"%s: %s, %s: %s",
								FrontendString_Get(
									FRONTSTR_760_REBEL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											  rebelAwardId)),
								FrontendString_Get(
									FRONTSTR_759_IMPERIAL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											  imperialAwardId)));
						} else {
							sprintf(g_frontendScratchBuffer,
								"%s: %s",
								FrontendString_Get(
									FRONTSTR_760_REBEL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											  rebelAwardId)));
						}
					} else {
						strcpy(g_frontendScratchBuffer,
						       FrontendString_Get((
							       FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
										 awardId)));
					}
				} else {
					sprintf(g_frontendScratchBuffer,
						awardFactionId != 0
							? "citlvl%d"
							: "rcitlvl%d",
						awardId);
					FrontImage_DrawSprite(
						g_frontendScratchBuffer,
						awardRect.left, awardRect.top);
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (rebelAwardId != 0) {
							if (imperialAwardId ==
							    0) {
								sprintf(g_frontendScratchBuffer,
									"%s: %s",
									FrontendString_Get(
										FRONTSTR_760_REBEL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
												   rebelAwardId)));
							} else {
								sprintf(g_frontendScratchBuffer,
									"%s: %s, %s: %s",
									FrontendString_Get(
										FRONTSTR_760_REBEL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
												   rebelAwardId)),
									FrontendString_Get(
										FRONTSTR_759_IMPERIAL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
												   imperialAwardId)));
							}
						} else if (imperialAwardId !=
							   0) {
							sprintf(g_frontendScratchBuffer,
								"%s: %s",
								FrontendString_Get(
									FRONTSTR_759_IMPERIAL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
											  imperialAwardId)));
						} else {
							sprintf(g_frontendScratchBuffer,
								"%s: %s, %s: %s",
								FrontendString_Get(
									FRONTSTR_760_REBEL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
											  rebelAwardId)),
								FrontendString_Get(
									FRONTSTR_759_IMPERIAL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
											  imperialAwardId)));
						}
					} else {
						strcpy(g_frontendScratchBuffer,
						       FrontendString_Get((
							       FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
										 awardId)));
					}
				}
				FrontendButton_DrawSpriteAndTooltip(
					&awardRect, NULL,
					g_frontendScratchBuffer, TEXT_FONT_SIZE,
					0xFFFF);
			}
			FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
		}
		++displayRow;
		if (displayRow - g_missionSetupMissionListScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}
	return 0;
}

/* Starts the tournament, battle or campaign selected on the mission setup
 * screen at its first mission. Opens the sequence file of the g_missionList
 * entry matching the selected description id; its first line is the mission
 * count, stored in meleeTournamentSequenceState.missionCount for a tournament
 * and campaignSequenceState.missionCount for a campaign. For a battle it sets
 * battleSequenceState.victoriesNeeded to g_gameConfig.battleLengthIndex + 2,
 * seeds rand with GetTickCount, and stores the first mission's ordinal in
 * missionOrdinals[0]: rand() % count when g_gameConfig.randomSetup is nonzero,
 * else 0. The mission file is named on line ordinal + 1 after the count line.
 * It then sets missionSequenceActive to 1, moves the mission type to the one
 * the sequence plays (tournament to melee, battle to combat engagement,
 * campaign to training), saves that type's selected mission in
 * savedMissionDescriptionId, loads its list, selects the mission whose file
 * name matches the line lowercased (for a combat engagement also storing its id
 * in battleSequenceState.currentMissionId), and stores its list index in
 * battleSequenceState.missionListIndices[0] whatever the type. Returns 1.
 * Returns 0 when the file does not open or is empty, or the mission's line is
 * empty; by then the count, and for a battle the victories and first ordinal,
 * may already be stored. Does not check a count of 0, which divides by zero
 * with random setup, or a selected mission missing from the list. */
// FUNCTION: XVT 0x4E79A0
int MissionSetup_SelectFirstSequenceMission(void)
{
	enum {
		SEQUENCE_DESCRIPTOR_PATH_CAPACITY = 128,
		SEQUENCE_DESCRIPTOR_LINE_CAPACITY = 255,
		FIRST_SEQUENCE_MISSION_INDEX = 0,
	};

	unsigned int descriptorMissionIndex;
	unsigned int missionListIndex;
	unsigned int characterIndex;
	int missionCount;
	int firstMissionOrdinal;
	int linesToRead;
	char descriptorPath[SEQUENCE_DESCRIPTOR_PATH_CAPACITY];
	XvtFile *stream;

	descriptorMissionIndex = 0;
	if (g_missionCount > descriptorMissionIndex) {
		while (1) {
			if (g_missionList[descriptorMissionIndex].missionIdx ==
			    g_pilotData.missionDescriptionIds
				    [g_pilotData.missionDirectoryId]) {
				break;
			}
			++descriptorMissionIndex;
			if (g_missionCount <= descriptorMissionIndex) {
				break;
			}
		}
	}
	sprintf(descriptorPath, "%s\\%s",
		g_missionDirectoryNames[g_pilotData.missionDirectoryId],
		g_missionList[descriptorMissionIndex].fileName);
	stream = File_Open(descriptorPath, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(g_frontendScratchBuffer,
		      SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream) == NULL) {
		File_Close(stream);
		return 0;
	}

	missionCount = atoi(g_frontendScratchBuffer);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		firstMissionOrdinal = 0;
		g_pilotData.meleeTournamentSequenceState.missionCount =
			missionCount;
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		firstMissionOrdinal = 0;
		g_pilotData.campaignSequenceState.missionCount = missionCount;
	} else {
		srand(GetTickCount());
		g_pilotData.battleSequenceState.victoriesNeeded =
			g_gameConfig.battleLengthIndex + 2;
		firstMissionOrdinal = 0;
		if (0 != g_gameConfig.randomSetup) {
			firstMissionOrdinal = rand() % missionCount;
		}
		g_pilotData.battleSequenceState
			.missionOrdinals[FIRST_SEQUENCE_MISSION_INDEX] =
			firstMissionOrdinal;
	}

	linesToRead = firstMissionOrdinal + 1;
	do {
		File_Gets(g_frontendScratchBuffer,
			  SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream);
		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		--linesToRead;
	} while (linesToRead != 0);
	File_Close(stream);
	if (g_frontendScratchBuffer[0] == '\0') {
		return 0;
	}

	g_pilotData.missionSequenceActive = 1;
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS) {
		g_pilotData.missionDirectoryId =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
	} else {
		--g_pilotData.missionDirectoryId;
	}
	g_pilotData.savedMissionDescriptionId =
		g_pilotData
			.missionDescriptionIds[g_pilotData.missionDirectoryId];
	/* descriptorPath is reused here for the first mission's file name, lowercased to match the mission
	 * list. */
	strcpy(descriptorPath, g_frontendScratchBuffer);
	for (characterIndex = 0; characterIndex < strlen(descriptorPath);
	     ++characterIndex) {
		descriptorPath[characterIndex] = (char)tolower(
			(unsigned char)descriptorPath[characterIndex]);
	}

	MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
	missionListIndex = 0;
	if (g_missionCount > missionListIndex) {
		while (1) {
			if (strcmp(descriptorPath,
				   g_missionList[missionListIndex].fileName) ==
			    0) {
				g_pilotData.missionDescriptionIds
					[g_pilotData.missionDirectoryId] =
					g_missionList[missionListIndex]
						.missionIdx;
				if (g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					g_pilotData.battleSequenceState
						.currentMissionId =
						g_missionList[missionListIndex]
							.missionIdx;
				}
				break;
			}
			++missionListIndex;
			if (g_missionCount <= missionListIndex) {
				break;
			}
		}
	}

	if (g_missionList != NULL) {
		g_selectedMissionListIndex = 0;
		while ((unsigned int)g_selectedMissionListIndex <
		       g_missionCount) {
			if (g_missionList[g_selectedMissionListIndex]
				    .missionIdx ==
			    g_pilotData.missionDescriptionIds
				    [g_pilotData.missionDirectoryId]) {
				g_pilotData.battleSequenceState
					.missionListIndices
						[FIRST_SEQUENCE_MISSION_INDEX] =
					g_selectedMissionListIndex;
				break;
			}
			++g_selectedMissionListIndex;
		}
	}
	return 1;
}

/* Draws the Mission Settings panel of the mission setup screen in two columns
 * of 6 rows. The host or a solo player clicks a value to change it, a right
 * click stepping the time limits back; other players see each value in yellow.
 * The rows: for a battle or campaign with a saved continuation, its status,
 * Restart or Continue, which toggles g_gameConfig.continueBattleOrCampaign and
 * on Continue takes back the saved length and setup choice (a client sees
 * g_remoteBattleSequenceContinuationChoice while
 * g_remoteBattleContinuationActive is set); for the host outside a solo game,
 * whether joining needs a password; outside a solo game, combat balance for a
 * combat engagement or battle, else difficulty (Easy to Hard, and for a
 * campaign also the easy cheat level); outside a solo game the mission time
 * limit (none, default, 1 to 20 minutes) and the last team's time limit (none,
 * 1 to 10 minutes); for a battle its length (2 to 4 wins); but for a campaign
 * the random setup (off or on, and for a battle also the player's choice);
 * outside a solo game AI opponents for a melee or tournament, craft selection
 * but for a campaign (off, on, host only; host only becomes off outside a melee
 * or tournament, for every player, each frame), and locate players; and for
 * everyone craft waves (none, default, unlimited; unlimited is skipped outside
 * a solo game for a combat engagement or battle) and starfighter collisions. A
 * battle's length and random setup stay fixed while its saved continuation is
 * active and Continue is chosen; the random setup row also tests a campaign's
 * continuation, but campaigns never show that row. When a value changed outside
 * a solo game it sends the 19-word GAME_OPTIONS packet to every player, laid
 * out as MissionSetup_SendLobbyState lays it out. Returns 1. */
// FUNCTION: XVT 0x4E7CE0
int MissionSetup_DrawGameSettings(void)
{
	enum {
		SETTINGS_LEFT_X = 88,
		SETTINGS_RIGHT_X = 262,
		SETTINGS_TOP_Y = 330,
		SETTINGS_TITLE_Y = 311,
		SETTINGS_COLUMN_WIDTH = 165,
		SETTINGS_ROW_HEIGHT = 14,
		SETTINGS_VALUE_GAP = 2,
		SETTINGS_ROWS_PER_COLUMN = 6,
		SETTINGS_FONT_SIZE = 10,
		SETTINGS_TITLE_FONT_SIZE = 15,
		SETTINGS_TEXT_COLOR = 0xFFFF,
		HOVER_DIFFICULTY_OR_BALANCE = 20,
		HOVER_JOINING_GAME = 21,
		HOVER_MISSION_TIME_LIMIT = 22,
		HOVER_LAST_TEAM_TIME_LIMIT = 23,
		HOVER_RANDOM_SETUP = 24,
		HOVER_AI_OPPONENTS = 25,
		HOVER_BATTLE_LENGTH = 26,
		HOVER_CRAFT_SELECTION = 27,
		HOVER_LOCATE_PLAYERS = 28,
		HOVER_CRAFT_WAVES = 29,
		HOVER_COLLISIONS = 31,
		HOVER_SEQUENCE_STATUS = 33,
		MAX_MISSION_TIME_MINUTES = 20,
		MAX_LAST_TEAM_TIME_MINUTES = 10,
		GAME_OPTIONS_PACKET_WORD_COUNT = 19,
	};

	int x;
	int y;
	int row;
	int settingsChanged;
	int canEdit;
	int continuationActive;
	int optionEnabled;
	int buttonResult;
	int textWidth;
	const char *label;
	RECT rect;
	int *packetWords;

	FrontendText_Draw(SETTINGS_TITLE_FONT_SIZE,
			  FrontendString_Get(FRONTSTR_658_MISSION_SETTINGS),
			  SETTINGS_LEFT_X, SETTINGS_TITLE_Y,
			  SETTINGS_TEXT_COLOR);
	x = SETTINGS_LEFT_X;
	y = SETTINGS_TOP_Y;
	row = 0;
	settingsChanged = 0;
	canEdit = Net_IsHost() != 0 ||
		  g_frontendMissionSessionMode ==
			  FRONTEND_MISSION_SESSION_SINGLEPLAYER;

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_BATTLES ||
	    g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS) {
		if (canEdit) {
			continuationActive = 0;
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_BATTLES) {
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilotData
						    .spBattleContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
						    .isActive == 1) {
						continuationActive = 1;
					}
				} else {
					if (g_pilotData
						    .mpBattleContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
						    .isActive == 1) {
						continuationActive = 1;
					}
				}
				label = FrontendString_Get(
					FRONTSTR_773_BATTLE_STATUS);
			} else {
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilotData
						    .spCampaignContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
						    .isActive == 1) {
						continuationActive = 1;
					}
				} else if (Net_IsHost() != 0) {
					if (g_pilotData
						    .mpCampaignContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
						    .isActive == 1) {
						continuationActive = 1;
					}
				} else {
					if (g_pilotData
						    .mpCampaignContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx +
							     12]
						    .isActive == 1) {
						continuationActive = 1;
					}
				}
				label = FrontendString_Get(
					FRONTSTR_778_CAMPAIGN_STATUS);
			}
			strcpy(g_frontendScratchBuffer, label);
			if (continuationActive) {
				FrontendText_Draw(SETTINGS_FONT_SIZE,
						  g_frontendScratchBuffer, x, y,
						  SETTINGS_TEXT_COLOR);
				textWidth = FrontendText_MeasureWidth(
					g_frontendScratchBuffer,
					SETTINGS_FONT_SIZE);
				FrontendDraw_RectAssign(
					&rect,
					x + textWidth + SETTINGS_VALUE_GAP, y,
					x + SETTINGS_COLUMN_WIDTH,
					y + SETTINGS_ROW_HEIGHT - 1);
				if (FrontendButton_HandleTextButton(
					    &rect,
					    FrontendString_Get((
						    FrontendStringId)((uint8_t)g_gameConfig
									      .continueBattleOrCampaign +
								      FRONTSTR_774_RESTART)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_SEQUENCE_STATUS,
					    "settingsound") != 0) {
					settingsChanged = 1;
					g_gameConfig.continueBattleOrCampaign ^=
						1;
					if (g_gameConfig
						    .continueBattleOrCampaign !=
					    SEQUENCE_RESTART) {
						if (g_pilotData
							    .missionDirectoryId ==
						    MISSION_DIRECTORY_BATTLES) {
							if (g_frontendMissionSessionMode ==
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								g_gameConfig
									.battleLengthIndex =
									(BattleLength)g_pilotData
										.spBattleContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.battleLengthIndex;
								g_gameConfig
									.randomSetup =
									(uint8_t)g_pilotData
										.spBattleContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.randomSetup;
							} else {
								g_gameConfig
									.battleLengthIndex =
									(BattleLength)g_pilotData
										.mpBattleContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.battleLengthIndex;
								g_gameConfig
									.randomSetup =
									(uint8_t)g_pilotData
										.mpBattleContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.randomSetup;
							}
						} else if (
							g_pilotData
								.missionDirectoryId ==
							MISSION_DIRECTORY_CAMPAIGNS) {
							if (g_frontendMissionSessionMode ==
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								g_gameConfig
									.randomSetup =
									(uint8_t)g_pilotData
										.spCampaignContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.randomSetup;
							} else if (
								Net_IsHost() !=
								0) {
								g_gameConfig
									.randomSetup =
									(uint8_t)g_pilotData
										.mpCampaignContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx]
										.randomSetup;
							} else {
								g_gameConfig
									.randomSetup =
									(uint8_t)g_pilotData
										.mpCampaignContinuations
											[g_missionList[g_selectedMissionListIndex]
												 .missionIdx +
											 12]
										.randomSetup;
							}
						}
					}
				}
				++row;
				y += SETTINGS_ROW_HEIGHT;
			}
		} else if (g_remoteBattleContinuationActive != 0) {
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_BATTLES) {
				strcpy(g_frontendScratchBuffer,
				       FrontendString_Get(
					       FRONTSTR_773_BATTLE_STATUS));
			} else if (g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_CAMPAIGNS) {
				strcpy(g_frontendScratchBuffer,
				       FrontendString_Get(
					       FRONTSTR_778_CAMPAIGN_STATUS));
			}
			FrontendText_Draw(SETTINGS_FONT_SIZE,
					  g_frontendScratchBuffer, x, y,
					  SETTINGS_TEXT_COLOR);
			textWidth = FrontendText_MeasureWidth(
				g_frontendScratchBuffer, SETTINGS_FONT_SIZE);
			FrontendDraw_RectAssign(
				&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
				x + SETTINGS_COLUMN_WIDTH,
				y + SETTINGS_ROW_HEIGHT - 1);
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(g_remoteBattleSequenceContinuationChoice +
							  FRONTSTR_774_RESTART)),
				&rect, 0, 1, g_colorYellow);
			++row;
			y += SETTINGS_ROW_HEIGHT;
		}
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    canEdit) {
		FrontendText_Draw(SETTINGS_FONT_SIZE,
				  FrontendString_Get(FRONTSTR_426_JOINING_GAME),
				  x, y, SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_426_JOINING_GAME),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (FrontendButton_HandleTextButton(
			    &rect,
			    FrontendString_Get((
				    FrontendStringId)(g_gameConfig
							      .requirePassword +
						      FRONTSTR_439_OPEN)),
			    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
			    HOVER_JOINING_GAME, "settingsound") != 0) {
			settingsChanged = 1;
			g_gameConfig.requirePassword ^= 1;
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    (g_pilotData.missionDirectoryId ==
		     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
	     g_pilotData.missionDirectoryId == MISSION_DIRECTORY_BATTLES)) {
		FrontendText_Draw(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(FRONTSTR_768_COMBAT_BALANCE), x, y,
			SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_768_COMBAT_BALANCE),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (canEdit) {
			if (FrontendButton_HandleTextButton(
				    &rect,
				    FrontendString_Get((
					    FrontendStringId)((uint8_t)g_gameConfig
								      .combatBalance +
							      FRONTSTR_769_AUTOBALANCE)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_DIFFICULTY_OR_BALANCE,
				    "settingsound") != 0) {
				settingsChanged = 1;
				if (++g_gameConfig.combatBalance >
				    COMBAT_BALANCE_FAVOR_REBEL) {
					g_gameConfig.combatBalance =
						COMBAT_BALANCE_AUTOBALANCE;
				}
			}
		} else {
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)((uint8_t)g_gameConfig
								  .combatBalance +
							  FRONTSTR_769_AUTOBALANCE)),
				&rect, 0, 1, g_colorYellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	} else {
		FrontendText_Draw(SETTINGS_FONT_SIZE,
				  FrontendString_Get(FRONTSTR_421_DIFFICULTY),
				  x, y, SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_421_DIFFICULTY),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_CAMPAIGNS) {
			if (canEdit) {
				if (FrontendButton_HandleTextButton(
					    &rect,
					    FrontendString_Get((
						    FrontendStringId)((uint8_t)g_gameConfig
									      .difficulty +
								      FRONTSTR_808_EASY)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_DIFFICULTY_OR_BALANCE,
					    "settingsound") != 0) {
					settingsChanged = 1;
					if (++g_gameConfig.difficulty >
					    GAME_DIFFICULTY_EASY_CHEAT) {
						g_gameConfig.difficulty =
							GAME_DIFFICULTY_EASY;
					}
				}
			} else {
				FrontendText_DrawAlignedInRect(
					SETTINGS_FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)((uint8_t)g_gameConfig
									  .difficulty +
								  FRONTSTR_808_EASY)),
					&rect, 0, 1, g_colorYellow);
			}
		} else {
			if (canEdit) {
				if (FrontendButton_HandleTextButton(
					    &rect,
					    FrontendString_Get((
						    FrontendStringId)((uint8_t)g_gameConfig
									      .difficulty +
								      FRONTSTR_433_EASY)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_DIFFICULTY_OR_BALANCE,
					    "settingsound") != 0) {
					settingsChanged = 1;
					if (++g_gameConfig.difficulty >
					    GAME_DIFFICULTY_HARD) {
						g_gameConfig.difficulty =
							GAME_DIFFICULTY_EASY;
					}
				}
			} else {
				FrontendText_DrawAlignedInRect(
					SETTINGS_FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)((uint8_t)g_gameConfig
									  .difficulty +
								  FRONTSTR_433_EASY)),
					&rect, 0, 1, g_colorYellow);
			}
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendText_Draw(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(FRONTSTR_431_MISSION_TIME_LIMIT), x,
			y, SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_431_MISSION_TIME_LIMIT),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (g_gameConfig.missionTimeLimit == 0) {
			strcpy(g_frontendScratchBuffer,
			       FrontendString_Get(FRONTSTR_445_NONE));
		} else if (g_gameConfig.missionTimeLimit == UINT8_MAX) {
			strcpy(g_frontendScratchBuffer,
			       FrontendString_Get(FRONTSTR_446_DEFAULT));
		} else {
			sprintf(g_frontendScratchBuffer, "%d %s",
				g_gameConfig.missionTimeLimit,
				FrontendString_Get(FRONTSTR_448_MIN));
		}
		if (canEdit) {
			buttonResult = FrontendButton_HandleTextButton(
				&rect, g_frontendScratchBuffer,
				SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				HOVER_MISSION_TIME_LIMIT, "settingsound");
			if (buttonResult == 1) {
				settingsChanged = 1;
				if (g_gameConfig.missionTimeLimit == 0) {
					g_gameConfig.missionTimeLimit =
						UINT8_MAX;
				} else if (g_gameConfig.missionTimeLimit ==
					   UINT8_MAX) {
					g_gameConfig.missionTimeLimit = 1;
				} else if (++g_gameConfig.missionTimeLimit >
					   MAX_MISSION_TIME_MINUTES) {
					g_gameConfig.missionTimeLimit = 0;
				}
			}
			if (buttonResult == 2) {
				settingsChanged = 1;
				if (g_gameConfig.missionTimeLimit ==
				    UINT8_MAX) {
					g_gameConfig.missionTimeLimit = 0;
				} else if (g_gameConfig.missionTimeLimit == 0) {
					g_gameConfig.missionTimeLimit =
						MAX_MISSION_TIME_MINUTES;
				} else if (--g_gameConfig.missionTimeLimit ==
					   0) {
					g_gameConfig.missionTimeLimit =
						UINT8_MAX;
				}
			}
		} else {
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE, g_frontendScratchBuffer,
				&rect, 0, 1, g_colorYellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}

		FrontendText_Draw(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(FRONTSTR_432_LAST_TEAM_TIME_LIMIT),
			x, y, SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_432_LAST_TEAM_TIME_LIMIT),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (g_gameConfig.lastTeamTimeLimitMinutes == 0) {
			strcpy(g_frontendScratchBuffer,
			       FrontendString_Get(FRONTSTR_445_NONE));
		} else {
			sprintf(g_frontendScratchBuffer, "%d %s",
				g_gameConfig.lastTeamTimeLimitMinutes,
				FrontendString_Get(FRONTSTR_448_MIN));
		}
		if (canEdit) {
			buttonResult = FrontendButton_HandleTextButton(
				&rect, g_frontendScratchBuffer,
				SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				HOVER_LAST_TEAM_TIME_LIMIT, "settingsound");
			if (buttonResult == 1) {
				settingsChanged = 1;
				if (++g_gameConfig.lastTeamTimeLimitMinutes >
				    MAX_LAST_TEAM_TIME_MINUTES) {
					g_gameConfig.lastTeamTimeLimitMinutes =
						0;
				}
			}
			if (buttonResult == 2) {
				settingsChanged = 1;
				if (g_gameConfig.lastTeamTimeLimitMinutes ==
				    0) {
					g_gameConfig.lastTeamTimeLimitMinutes =
						MAX_LAST_TEAM_TIME_MINUTES;
				} else {
					--g_gameConfig.lastTeamTimeLimitMinutes;
				}
			}
		} else {
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE, g_frontendScratchBuffer,
				&rect, 0, 1, g_colorYellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_BATTLES) {
		FrontendText_Draw(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(FRONTSTR_425_BATTLE_LENGTH), x, y,
			SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_425_BATTLE_LENGTH),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		optionEnabled = 0;
		if (canEdit) {
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_gameConfig.continueBattleOrCampaign !=
					    SEQUENCE_CONTINUE ||
				    g_pilotData.spBattleContinuations
						    [g_missionList
							     [g_selectedMissionListIndex]
								     .missionIdx]
							    .isActive != 1) {
					optionEnabled = 1;
				}
			} else if (
				g_gameConfig.continueBattleOrCampaign !=
					SEQUENCE_CONTINUE ||
				g_pilotData.mpBattleContinuations
						[g_missionList
							 [g_selectedMissionListIndex]
								 .missionIdx]
							.isActive != 1) {
				optionEnabled = 1;
			}
		}
		if (optionEnabled) {
			if (FrontendButton_HandleTextButton(
				    &rect,
				    FrontendString_Get((
					    FrontendStringId)((uint8_t)g_gameConfig
								      .battleLengthIndex +
							      FRONTSTR_436_2_WINS)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_BATTLE_LENGTH, "settingsound") != 0) {
				settingsChanged = 1;
				if (++g_gameConfig.battleLengthIndex >
				    BATTLE_LENGTH_FOUR_WINS) {
					g_gameConfig.battleLengthIndex =
						BATTLE_LENGTH_TWO_WINS;
				}
			}
		} else {
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)((uint8_t)g_gameConfig
								  .battleLengthIndex +
							  FRONTSTR_436_2_WINS)),
				&rect, 0, 1, g_colorYellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_CAMPAIGNS) {
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_BATTLES) {
			FrontendText_Draw(
				SETTINGS_FONT_SIZE,
				FrontendString_Get(FRONTSTR_736_RANDOM_MISSION),
				x, y, SETTINGS_TEXT_COLOR);
			textWidth = FrontendText_MeasureWidth(
				FrontendString_Get(FRONTSTR_736_RANDOM_MISSION),
				SETTINGS_FONT_SIZE);
		} else {
			FrontendText_Draw(
				SETTINGS_FONT_SIZE,
				FrontendString_Get(FRONTSTR_424_RANDOMIZE), x,
				y, SETTINGS_TEXT_COLOR);
			textWidth = FrontendText_MeasureWidth(
				FrontendString_Get(FRONTSTR_424_RANDOMIZE),
				SETTINGS_FONT_SIZE);
		}
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		optionEnabled = 0;
		if (canEdit) {
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_BATTLES) {
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_gameConfig.continueBattleOrCampaign !=
						    SEQUENCE_CONTINUE ||
					    g_pilotData.spBattleContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
								    .isActive !=
						    1) {
						optionEnabled = 1;
					}
				} else if (
					g_gameConfig.continueBattleOrCampaign !=
						SEQUENCE_CONTINUE ||
					g_pilotData.mpBattleContinuations
							[g_missionList
								 [g_selectedMissionListIndex]
									 .missionIdx]
								.isActive !=
						1) {
					optionEnabled = 1;
				}
			} else if (g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_CAMPAIGNS) {
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_gameConfig.continueBattleOrCampaign !=
						    SEQUENCE_CONTINUE ||
					    g_pilotData.spCampaignContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
								    .isActive !=
						    1) {
						optionEnabled = 1;
					}
				} else if (Net_IsHost() != 0) {
					if (g_gameConfig.continueBattleOrCampaign !=
						    SEQUENCE_CONTINUE ||
					    g_pilotData.mpCampaignContinuations
							    [g_missionList[g_selectedMissionListIndex]
								     .missionIdx]
								    .isActive !=
						    1) {
						optionEnabled = 1;
					}
				} else if (
					g_gameConfig.continueBattleOrCampaign !=
						SEQUENCE_CONTINUE ||
					g_pilotData.mpCampaignContinuations
							[g_missionList[g_selectedMissionListIndex]
								 .missionIdx +
							 12]
								.isActive !=
						1) {
					optionEnabled = 1;
				}
			} else {
				optionEnabled = 1;
			}
		}
		if (optionEnabled) {
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_BATTLES) {
				if (FrontendButton_HandleTextButton(
					    &rect,
					    FrontendString_Get((
						    FrontendStringId)(g_gameConfig
									      .randomSetup +
								      FRONTSTR_819_OFF)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_RANDOM_SETUP,
					    "settingsound") != 0) {
					settingsChanged = 1;
					if (++g_gameConfig.randomSetup > 2) {
						g_gameConfig.randomSetup = 0;
					}
				}
			} else if (
				FrontendButton_HandleTextButton(
					&rect,
					FrontendString_Get((
						FrontendStringId)(g_gameConfig
									  .randomSetup +
								  FRONTSTR_236_OFF)),
					SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
					HOVER_RANDOM_SETUP,
					"settingsound") != 0) {
				settingsChanged = 1;
				g_gameConfig.randomSetup ^= 1;
			}
		} else {
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_BATTLES) {
				FrontendText_DrawAlignedInRect(
					SETTINGS_FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)(g_gameConfig
									  .randomSetup +
								  FRONTSTR_819_OFF)),
					&rect, 0, 1, g_colorYellow);
			} else {
				FrontendText_DrawAlignedInRect(
					SETTINGS_FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)(g_gameConfig
									  .randomSetup +
								  FRONTSTR_236_OFF)),
					&rect, 0, 1, g_colorYellow);
			}
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
	     g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS)) {
		FrontendText_Draw(SETTINGS_FONT_SIZE,
				  FrontendString_Get(FRONTSTR_572_AI_OPPONENTS),
				  x, y, SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_572_AI_OPPONENTS),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (canEdit) {
			if (FrontendButton_HandleTextButton(
				    &rect,
				    FrontendString_Get((
					    FrontendStringId)(g_gameConfig
								      .aiOpponents +
							      FRONTSTR_236_OFF)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_AI_OPPONENTS, "settingsound") != 0) {
				settingsChanged = 1;
				g_gameConfig.aiOpponents ^= 1;
			}
		} else {
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(g_gameConfig
								  .aiOpponents +
							  FRONTSTR_236_OFF)),
				&rect, 0, 1, g_colorYellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_pilotData.missionDirectoryId != MISSION_DIRECTORY_CAMPAIGNS) {
		FrontendText_Draw(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(FRONTSTR_428_CRAFT_SELECTION), x, y,
			SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_428_CRAFT_SELECTION),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (canEdit) {
			if (FrontendButton_HandleTextButton(
				    &rect,
				    FrontendString_Get((
					    FrontendStringId)((uint8_t)g_gameConfig
								      .craftSelection +
							      FRONTSTR_536_OFF)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_CRAFT_SELECTION,
				    "settingsound") != 0) {
				settingsChanged = 1;
				if (++g_gameConfig.craftSelection >
				    CRAFT_SELECTION_HOST_ONLY) {
					g_gameConfig.craftSelection =
						CRAFT_SELECTION_OFF;
				}
			}
		} else {
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)((uint8_t)g_gameConfig
								  .craftSelection +
							  FRONTSTR_536_OFF)),
				&rect, 0, 1, g_colorYellow);
		}
		if (g_gameConfig.craftSelection == CRAFT_SELECTION_HOST_ONLY &&
		    g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_MELEES &&
		    g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_TOURNAMENTS) {
			g_gameConfig.craftSelection = CRAFT_SELECTION_OFF;
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendText_Draw(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(FRONTSTR_429_LOCATE_PLAYERS), x, y,
			SETTINGS_TEXT_COLOR);
		textWidth = FrontendText_MeasureWidth(
			FrontendString_Get(FRONTSTR_429_LOCATE_PLAYERS),
			SETTINGS_FONT_SIZE);
		FrontendDraw_RectAssign(
			&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (canEdit) {
			if (FrontendButton_HandleTextButton(
				    &rect,
				    FrontendString_Get((
					    FrontendStringId)(g_gameConfig
								      .locatePlayers +
							      FRONTSTR_443_OFF)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_LOCATE_PLAYERS,
				    "settingsound") != 0) {
				settingsChanged = 1;
				g_gameConfig.locatePlayers ^= 1;
			}
		} else {
			FrontendText_DrawAlignedInRect(
				SETTINGS_FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(g_gameConfig
								  .locatePlayers +
							  FRONTSTR_443_OFF)),
				&rect, 0, 1, g_colorYellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	FrontendText_Draw(SETTINGS_FONT_SIZE,
			  FrontendString_Get(FRONTSTR_430_CRAFT_WAVES), x, y,
			  SETTINGS_TEXT_COLOR);
	textWidth = FrontendText_MeasureWidth(
		FrontendString_Get(FRONTSTR_430_CRAFT_WAVES),
		SETTINGS_FONT_SIZE);
	FrontendDraw_RectAssign(&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
				x + SETTINGS_COLUMN_WIDTH,
				y + SETTINGS_ROW_HEIGHT - 1);
	if (canEdit) {
		if (FrontendButton_HandleTextButton(
			    &rect,
			    FrontendString_Get(
				    (FrontendStringId)((uint8_t)g_gameConfig
							       .craftWaves +
						       FRONTSTR_445_NONE)),
			    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
			    HOVER_CRAFT_WAVES, "settingsound") != 0) {
			settingsChanged = 1;
			if (++g_gameConfig.craftWaves > CRAFT_WAVES_UNLIMITED) {
				g_gameConfig.craftWaves = CRAFT_WAVES_NONE;
			}
			if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
			    (g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			     g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_BATTLES) &&
			    g_gameConfig.craftWaves == CRAFT_WAVES_UNLIMITED) {
				g_gameConfig.craftWaves = CRAFT_WAVES_NONE;
			}
		}
	} else {
		FrontendText_DrawAlignedInRect(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(
				(FrontendStringId)((uint8_t)g_gameConfig
							   .craftWaves +
						   FRONTSTR_445_NONE)),
			&rect, 0, 1, g_colorYellow);
	}
	++row;
	y += SETTINGS_ROW_HEIGHT;
	if (row == SETTINGS_ROWS_PER_COLUMN) {
		x = SETTINGS_RIGHT_X;
		y = SETTINGS_TOP_Y;
	}

	FrontendText_Draw(
		SETTINGS_FONT_SIZE,
		FrontendString_Get(FRONTSTR_422_STARFIGHTER_COLLISIONS), x, y,
		SETTINGS_TEXT_COLOR);
	textWidth = FrontendText_MeasureWidth(
		FrontendString_Get(FRONTSTR_422_STARFIGHTER_COLLISIONS),
		SETTINGS_FONT_SIZE);
	FrontendDraw_RectAssign(&rect, x + textWidth + SETTINGS_VALUE_GAP, y,
				x + SETTINGS_COLUMN_WIDTH,
				y + SETTINGS_ROW_HEIGHT - 1);
	if (canEdit) {
		if (FrontendButton_HandleTextButton(
			    &rect,
			    FrontendString_Get(
				    (FrontendStringId)(g_gameConfig.collisions +
						       FRONTSTR_236_OFF)),
			    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
			    HOVER_COLLISIONS, "settingsound") != 0) {
			settingsChanged = 1;
			g_gameConfig.collisions ^= 1;
		}
	} else {
		FrontendText_DrawAlignedInRect(
			SETTINGS_FONT_SIZE,
			FrontendString_Get(
				(FrontendStringId)(g_gameConfig.collisions +
						   FRONTSTR_236_OFF)),
			&rect, 0, 1, g_colorYellow);
	}

	++row;
	y += SETTINGS_ROW_HEIGHT;
	if (row == SETTINGS_ROWS_PER_COLUMN) {
		x = SETTINGS_RIGHT_X;
		y = SETTINGS_TOP_Y;
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    settingsChanged == 1) {
		packetWords = &g_frontendNetPacketScratch.packetType;
		packetWords[0] = NET_PACKET_GAME_OPTIONS;
		packetWords[1] = (uint8_t)g_gameConfig.difficulty;
		packetWords[2] = g_gameConfig.collisions;
		packetWords[3] = g_gameConfig.craftJumping;
		packetWords[4] = g_gameConfig.randomSetup;
		packetWords[5] = (uint8_t)g_gameConfig.battleLengthIndex;
		packetWords[6] = g_gameConfig.requirePassword;
		packetWords[7] = g_gameConfig.inProgressJoin;
		packetWords[8] = (uint8_t)g_gameConfig.craftSelection;
		packetWords[9] = g_gameConfig.locatePlayers;
		packetWords[10] = (uint8_t)g_gameConfig.craftWaves;
		packetWords[11] = g_gameConfig.missionTimeLimit;
		packetWords[12] = g_gameConfig.lastTeamTimeLimitMinutes;
		packetWords[13] = rand();
		packetWords[14] = g_gameConfig.internetPlay;
		packetWords[15] = g_gameConfig.aiOpponents;
		packetWords[16] = g_gameConfig.serverUpdateRate;
		packetWords[17] = (uint8_t)g_gameConfig.combatBalance;
		packetWords[18] =
			(uint8_t)g_gameConfig.continueBattleOrCampaign;
		Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
				       GAME_OPTIONS_PACKET_WORD_COUNT *
					       sizeof(packetWords[0]));
	}
	return 1;
}

/* Counts the entries of a mission list file from the stream's position, as
 * MissionSetup_LoadMissionList reads them: lines starting with "//" and
 * "[section]" lines are skipped, and each entry is three lines; an entry the
 * file ends inside is not counted. Returns the count and leaves the stream at
 * its end. The section name it copies into a local is never used. */
// FUNCTION: XVT 0x4E9230
int MissionSetup_CountMissionListEntries(XvtFile *stream)
{
	int entryCount;
	char sectionName[128];

	entryCount = 0;
	while (1) {
		do {
			if (File_Gets(g_frontendScratchBuffer,
				      sizeof(g_frontendScratchBuffer),
				      stream) == NULL) {
				return entryCount;
			}
		} while (g_frontendScratchBuffer[0] == '/' &&
			 g_frontendScratchBuffer[1] == '/');
		if (g_frontendScratchBuffer[0] == '[') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
#ifdef XVT_MODERN
			strncpy(sectionName, &g_frontendScratchBuffer[1],
				sizeof(sectionName) - 1);
			sectionName[sizeof(sectionName) - 1] = '\0';
#else
			strcpy(sectionName, &g_frontendScratchBuffer[1]);
#endif
			continue;
		}
		if (File_Gets(g_frontendScratchBuffer,
			      sizeof(g_frontendScratchBuffer),
			      stream) == NULL) {
			return entryCount;
		}
		if (File_Gets(g_frontendScratchBuffer,
			      sizeof(g_frontendScratchBuffer),
			      stream) == NULL) {
			return entryCount;
		}
		++entryCount;
	}
}

/* Loads the background for the current mission type into the "background" image
 * and draws the mission setup screen's base into the offscreen surface:
 * background, frame, "allactive" while g_hostCdAvailable is set, else
 * "clientactive", the chat box outside a solo game, and the overlay. In a solo
 * game the background is chosen by mission type and, for training, combat
 * engagements, battles and campaigns, by the pilot's faction (0 for Rebel);
 * outside a solo game a training mission's comes from the IFF of its first
 * flight group with a player (0 for Rebel), a campaign's from
 * MissionSetup_UseRebelBackground, and the rest by type alone. Tournaments, and
 * any type id not listed, take gametrn. Returns 1. Does not check that a
 * network training mission has a flight group with a player. */
// FUNCTION: XVT 0x4E9330
int MissionSetup_DrawBackground(void)
{
	int flightGroupIndex;
	uint8_t *playerNumberPtr;

	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			if (g_pilotData.currentFactionId == 0) {
				FrontImage_RegisterResourceDefault(
					"frontres\\gametr.bmp", "background");
			} else {
				FrontImage_RegisterResourceDefault(
					"frontres\\gameti.bmp", "background");
			}
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			if (g_pilotData.currentFactionId == 0) {
				FrontImage_RegisterResourceDefault(
					"frontres\\gamecr.bmp", "background");
			} else {
				FrontImage_RegisterResourceDefault(
					"frontres\\gameci.bmp", "background");
			}
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_BATTLES) {
			if (g_pilotData.currentFactionId == 0) {
				FrontImage_RegisterResourceDefault(
					"frontres\\gamebr.bmp", "background");
			} else {
				FrontImage_RegisterResourceDefault(
					"frontres\\gamebi.bmp", "background");
			}
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_MELEES) {
			FrontImage_RegisterResourceDefault(
				"frontres\\gamemn.bmp", "background");
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_CAMPAIGNS) {
			if (g_pilotData.currentFactionId == 0) {
				FrontImage_RegisterResourceDefault(
					"frontres\\gamecar.bmp", "background");
			} else {
				FrontImage_RegisterResourceDefault(
					"frontres\\gamecai.bmp", "background");
			}
		} else {
			FrontImage_RegisterResourceDefault(
				"frontres\\gametrn.bmp", "background");
		}
	} else {
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			flightGroupIndex = 0;
			if (*(int16_t *)&g_frontendMission.flightGroupCount >
			    flightGroupIndex) {
				playerNumberPtr =
					&g_frontendMission.flightGroups[0]
						 .playerNumber;
				do {
					if (*playerNumberPtr != 0) {
						break;
					}
					playerNumberPtr +=
						sizeof(XvtFlightGroup);
					++flightGroupIndex;
				} while (*(int16_t *)&g_frontendMission
						  .flightGroupCount >
					 flightGroupIndex);
			}
			if (g_frontendMission.flightGroups[flightGroupIndex]
				    .iff == 0) {
				FrontImage_RegisterResourceDefault(
					"frontres\\gametr.bmp", "background");
			} else {
				FrontImage_RegisterResourceDefault(
					"frontres\\gameti.bmp", "background");
			}
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			FrontImage_RegisterResourceDefault(
				"frontres\\gamecn.bmp", "background");
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_BATTLES) {
			FrontImage_RegisterResourceDefault(
				"frontres\\gamebn.bmp", "background");
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_MELEES) {
			FrontImage_RegisterResourceDefault(
				"frontres\\gamemn.bmp", "background");
		} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_CAMPAIGNS) {
			if (MissionSetup_UseRebelBackground() != 0) {
				FrontImage_RegisterResourceDefault(
					"frontres\\gamecar.bmp", "background");
			} else {
				FrontImage_RegisterResourceDefault(
					"frontres\\gamecai.bmp", "background");
			}
		} else {
			FrontImage_RegisterResourceDefault(
				"frontres\\gametrn.bmp", "background");
		}
	}

	FrontendDisplay_LockOffscreenSurface();
	FrontImage_DrawSpriteOpaque("background", 0, 0);
	FrontImage_DrawSprite("frame", 0, 0);
	if (g_hostCdAvailable != 0) {
		FrontImage_DrawSprite("allactive", 0, 0);
	} else {
		FrontImage_DrawSprite("clientactive", 0, 0);
	}
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
	}
	FrontImage_DrawSpriteTranslucent("gameoverlay", 0, 0);
	FrontendDisplay_UnlockOffscreenSurface(1);
	return 1;
}

/* Tells which background a network campaign gets: opens the selected campaign's
 * sequence file, takes its second line as the first mission's file name in the
 * training directory, loads that mission into a local copy, and returns 1 when
 * the IFF of its first flight group with a player is 0 (Rebel), else 0. Returns
 * 1 when the campaign file does not open or has no second line. Does not check
 * that the mission file opened: when it did not, the result comes from an unset
 * local copy. */
// FUNCTION: XVT 0x4E9560
int MissionSetup_UseRebelBackground(void)
{
	XvtFile *stream;
	char *readResult;
	char filePath[256];
	FrontendMission mission;

	sprintf(filePath, "%s\\%s",
		g_missionDirectoryNames[MISSION_DIRECTORY_CAMPAIGNS],
		g_missionList[g_selectedMissionListIndex].fileName);
	stream = File_Open(filePath, "r");
	if (stream != NULL) {
		File_Gets(g_frontendScratchBuffer,
			  sizeof(g_frontendScratchBuffer), stream);
		readResult = File_Gets(g_frontendScratchBuffer,
				       sizeof(g_frontendScratchBuffer), stream);
		File_Close(stream);
		if (readResult != NULL) {
			if (g_frontendScratchBuffer
				    [strlen(g_frontendScratchBuffer) - 1] ==
			    '\n') {
				g_frontendScratchBuffer
					[strlen(g_frontendScratchBuffer) - 1] =
						'\0';
			}
			sprintf(filePath, "%s\\%s",
				g_missionDirectoryNames
					[MISSION_DIRECTORY_TRAINING_EXERCISES],
				g_frontendScratchBuffer);
			FrontendMission_LoadFile(filePath, &mission);
			{
				int flightGroupIndex;
				uint8_t *playerNumberPtr;

				flightGroupIndex = 0;
				if ((int16_t)mission.flightGroupCount > 0) {
					playerNumberPtr =
						&mission.flightGroups[0]
							 .playerNumber;
					do {
						if (*playerNumberPtr != 0) {
							break;
						}
						playerNumberPtr +=
							sizeof(XvtFlightGroup);
						++flightGroupIndex;
					} while ((int16_t)mission
							 .flightGroupCount >
						 flightGroupIndex);
				}

				return mission.flightGroups[flightGroupIndex]
					       .iff == 0;
			}
		}
	}

	return 1;
}

/* Draws the local player's craft on the mission briefing's craft screen; called
 * by mission_briefing.c. First the model preview of the chosen craft, in a
 * larger box in a solo game, turned to fixed angles and placed at
 * g_modelPreviewCraftPositions for its type; in a melee or tournament it shows
 * the flight group's markings, plus 1 (past 3 back to 0) for craft other than
 * types 1 to 4, 14 and 16. Then the craft type, craft count and wave count on
 * the left (a wave count of none shows 1, unlimited shows its name, otherwise
 * g_missionSetupSelectedWaveCountMinusOne + 1) and the warhead, beam weapon and
 * countermeasure on the right. Once the local player is marked ready in
 * g_mpRosterReadyFlags outside a solo game, everything is gray. Otherwise the
 * craft values are drawn in color code 1 when there is more than one craft
 * choice and craft selection is allowed (in a training sequence only at the
 * easy cheat difficulty, outside a solo game as g_gameConfig.craftSelection
 * says), else in code 4; the warhead and countermeasure values in code 1 when
 * there is more than one choice and the loadout is not locked, which happens
 * only in a training sequence below the easy cheat difficulty, else in code 4;
 * and the beam value in yellow when there is one choice or the loadout is
 * locked. The beam line is left out for craft types 1 to 5 and 14. Does not
 * check that the local player has a g_mpRoster entry: without one it reads
 * g_mpRosterReadyFlags[8], past the array's end. */
// FUNCTION: XVT 0x4EC0B0
void MissionSetup_DrawCraftLoadout(void)
{
	enum {
		FONT_SIZE = 12,
		ROW_HEIGHT = 15,
		LABEL_LEFT = 88,
		LABEL_TOP = 111,
		LABEL_RIGHT = 234,
		LABEL_BOTTOM = 125,
		VALUE_LEFT = 275,
		VALUE_RIGHT = 431,
		COLOR_ESCAPE_SELECTABLE = 1,
		COLOR_ESCAPE_FIXED = 4,
		CRAFT_STRING_BASE = 21,
		WARHEAD_STRING_BASE = 273,
		BEAM_STRING_BASE = 283,
		COUNTERMEASURE_STRING_BASE = 288,
	};

	RECT rect;
	int craftType;
	int rosterIndex;
	int craftSelectionAllowed;
	int loadoutSelectionLocked;
	char valueEscape;
	int drawBeam;
	const char *valueText;
	FrontendStringId valueStringId;

	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 84, 107, 430, 407);
	} else {
		FrontendDraw_RectAssign(&rect, 144, 107, 370, 333);
	}
	ModelPreview_SetObjectEulerDegrees(110.0f, -135.0f, 20.0f);
	craftType = MissionSetup_GetCraftType(-1);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
	    g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		int markings;

		switch (craftType) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 14:
		case 16:
			markings =
				g_frontendMission
					.flightGroups
						[g_missionSetupSelectedFlightGroupIndex]
					.markings;
			break;
		default:
			markings =
				g_frontendMission
					.flightGroups
						[g_missionSetupSelectedFlightGroupIndex]
					.markings +
				1;
			if (markings > 3) {
				markings = 0;
			}
			break;
		}
		ModelPreview_SetNodeSwitchIndex(markings);
	}
	ModelPreview_SetObjectWorldPosition(
		g_modelPreviewCraftPositions[craftType].x,
		g_modelPreviewCraftPositions[craftType].y,
		g_modelPreviewCraftPositions[craftType].z);
	ModelPreview_RenderViewport(rect.left, rect.top,
				    rect.right - rect.left + 1,
				    rect.bottom - rect.top + 1, 0);

	for (rosterIndex = 0;
	     rosterIndex < (int)(sizeof(g_mpRoster) / sizeof(g_mpRoster[0]));
	     ++rosterIndex) {
		if (Net_GetLocalPlayerId() ==
		    g_mpRoster[rosterIndex].playerId) {
			break;
		}
	}

	if (g_mpRosterReadyFlags[rosterIndex] != 0 &&
	    g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, LABEL_LEFT, LABEL_TOP,
					LABEL_RIGHT, LABEL_BOTTOM);
		craftType = MissionSetup_GetCraftType(-1);
		sprintf(g_frontendScratchBuffer, "%s: %s",
			FrontendString_Get(FRONTSTR_605_CRAFT_TYPE),
			FrontendString_Get(
				(FrontendStringId)(craftType +
						   CRAFT_STRING_BASE)));
		FrontendText_DrawAlignedInRect(FONT_SIZE,
					       g_frontendScratchBuffer, &rect,
					       0, 1, g_colorGray);
		FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);

		sprintf(g_frontendScratchBuffer, "%s %u",
			FrontendString_Get(FRONTSTR_573_OF_CRAFT),
			g_missionSetupSelectedCraftCount);
		FrontendText_DrawAlignedInRect(FONT_SIZE,
					       g_frontendScratchBuffer, &rect,
					       0, 1, g_colorGray);
		FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);

		if (g_gameConfig.craftWaves == CRAFT_WAVES_UNLIMITED) {
			sprintf(g_frontendScratchBuffer, "%s %s",
				FrontendString_Get(FRONTSTR_267_OF_WAVES),
				FrontendString_Get(FRONTSTR_447_UNLIMITED));
		} else if (g_gameConfig.craftWaves == CRAFT_WAVES_NONE) {
			sprintf(g_frontendScratchBuffer, "%s 1",
				FrontendString_Get(FRONTSTR_267_OF_WAVES));
		} else {
			sprintf(g_frontendScratchBuffer, "%s %u",
				FrontendString_Get(FRONTSTR_267_OF_WAVES),
				g_missionSetupSelectedWaveCountMinusOne + 1);
		}
		FrontendText_DrawAlignedInRect(FONT_SIZE,
					       g_frontendScratchBuffer, &rect,
					       0, 1, g_colorGray);

		FrontendDraw_RectAssign(&rect, 270, LABEL_TOP, 426,
					LABEL_BOTTOM);
		FrontendDraw_RectAssign(&rect, VALUE_LEFT, LABEL_TOP,
					VALUE_RIGHT, LABEL_BOTTOM);
		if (g_missionSetupWarheadOptionCount != 0) {
			valueStringId =
				(FrontendStringId)(MissionSetup_GetWarheadType(
							   -1) +
						   WARHEAD_STRING_BASE);
		} else {
			valueStringId = FRONTSTR_273_NONE;
		}
		valueText = FrontendString_Get(valueStringId);
		sprintf(g_frontendScratchBuffer, "%s %s",
			FrontendString_Get(FRONTSTR_264_WARHEADS), valueText);
		rect.left = rect.right -
			    FrontendText_MeasureWidth(g_frontendScratchBuffer,
						      FONT_SIZE);
		FrontendText_DrawAlignedInRect(FONT_SIZE,
					       g_frontendScratchBuffer, &rect,
					       0, 1, g_colorGray);
		FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);

		drawBeam = 1;
		if (g_missionSetupBeamOptionCount == 0) {
			sprintf(g_frontendScratchBuffer, "%s %s",
				FrontendString_Get(FRONTSTR_266_BEAM_WEAPON),
				FrontendString_Get(FRONTSTR_273_NONE));
			craftType = MissionSetup_GetCraftType(-1);
			if (craftType >= 1 &&
			    (craftType <= 5 || craftType == 14)) {
				drawBeam = 0;
			}
		} else {
			craftType = MissionSetup_GetCraftType(-1);
			if (craftType < 1 ||
			    (craftType > 5 && craftType != 14)) {
				valueText = FrontendString_Get((
					FrontendStringId)(MissionSetup_GetBeamType(
								  -1) +
							  BEAM_STRING_BASE));
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get(
						FRONTSTR_266_BEAM_WEAPON),
					valueText);
			} else {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get(
						FRONTSTR_266_BEAM_WEAPON),
					FrontendString_Get(FRONTSTR_273_NONE));
				drawBeam = 0;
			}
		}
		if (drawBeam != 0) {
			rect.left = rect.right -
				    FrontendText_MeasureWidth(
					    g_frontendScratchBuffer, FONT_SIZE);
			FrontendText_DrawAlignedInRect(
				FONT_SIZE, g_frontendScratchBuffer, &rect, 0, 1,
				g_colorGray);
			FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
		}

		if (g_missionSetupCountermeasureOptionCount != 0) {
			valueStringId =
				(FrontendStringId)(MissionSetup_GetCountermeasureType(
							   -1) +
						   COUNTERMEASURE_STRING_BASE);
		} else {
			valueStringId = FRONTSTR_273_NONE;
		}
		valueText = FrontendString_Get(valueStringId);
		sprintf(g_frontendScratchBuffer, "%s %s",
			FrontendString_Get(FRONTSTR_265_COUNTERMEASURES),
			valueText);
		rect.left = rect.right -
			    FrontendText_MeasureWidth(g_frontendScratchBuffer,
						      FONT_SIZE);
		FrontendText_DrawAlignedInRect(FONT_SIZE,
					       g_frontendScratchBuffer, &rect,
					       0, 1, g_colorGray);
		return;
	}

	FrontendDraw_RectAssign(&rect, LABEL_LEFT, LABEL_TOP, LABEL_RIGHT,
				LABEL_BOTTOM);
	craftType = MissionSetup_GetCraftType(-1);
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilotData.missionSequenceActive == 1) {
			if (g_gameConfig.difficulty ==
			    GAME_DIFFICULTY_EASY_CHEAT) {
				loadoutSelectionLocked = 0;
				craftSelectionAllowed = 1;
			} else {
				loadoutSelectionLocked = 1;
				craftSelectionAllowed = 0;
			}
		} else {
			loadoutSelectionLocked = 0;
			craftSelectionAllowed = 1;
		}
	} else if (g_pilotData.missionDirectoryId ==
			   MISSION_DIRECTORY_TRAINING_EXERCISES &&
		   g_pilotData.missionSequenceActive == 1) {
		if (g_gameConfig.difficulty == GAME_DIFFICULTY_EASY_CHEAT) {
			craftSelectionAllowed = 1;
			loadoutSelectionLocked = 0;
		} else {
			craftSelectionAllowed = 0;
			loadoutSelectionLocked = 1;
		}
	} else {
		craftSelectionAllowed = 0;
		loadoutSelectionLocked = 0;
		switch (g_gameConfig.craftSelection) {
		case CRAFT_SELECTION_OFF:
			craftSelectionAllowed = 0;
			break;
		case CRAFT_SELECTION_ON:
			craftSelectionAllowed = 1;
			break;
		case CRAFT_SELECTION_HOST_ONLY:
			if (Net_IsHost() != 0 ||
			    g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				craftSelectionAllowed = 1;
			}
			break;
		default:
			break;
		}
	}

	if ((g_missionSetupFlightGroupCraftOptionCount > 1 ||
	     g_missionSetupPresetCraftOptionCount > 1) &&
	    craftSelectionAllowed != 0) {
		valueEscape = COLOR_ESCAPE_SELECTABLE;
	} else {
		valueEscape = COLOR_ESCAPE_FIXED;
	}
	sprintf(g_frontendScratchBuffer, "%c%s: %c%s", COLOR_ESCAPE_FIXED,
		FrontendString_Get(FRONTSTR_605_CRAFT_TYPE), valueEscape,
		FrontendString_Get(
			(FrontendStringId)(craftType + CRAFT_STRING_BASE)));
	FrontendText_DrawAlignedInRect(FONT_SIZE, g_frontendScratchBuffer,
				       &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);

	sprintf(g_frontendScratchBuffer, "%c%s %c%u", COLOR_ESCAPE_FIXED,
		FrontendString_Get(FRONTSTR_573_OF_CRAFT), valueEscape,
		g_missionSetupSelectedCraftCount);
	FrontendText_DrawAlignedInRect(FONT_SIZE, g_frontendScratchBuffer,
				       &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);

	if (g_gameConfig.craftWaves == CRAFT_WAVES_UNLIMITED) {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_267_OF_WAVES),
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_447_UNLIMITED));
	} else if (g_gameConfig.craftWaves == CRAFT_WAVES_NONE) {
		sprintf(g_frontendScratchBuffer, "%c%s %c1", COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_267_OF_WAVES),
			COLOR_ESCAPE_FIXED);
	} else {
		sprintf(g_frontendScratchBuffer, "%c%s %c%u",
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_267_OF_WAVES), valueEscape,
			g_missionSetupSelectedWaveCountMinusOne + 1);
	}
	FrontendText_DrawAlignedInRect(FONT_SIZE, g_frontendScratchBuffer,
				       &rect, 0, 1, 0xFFFF);

	FrontendDraw_RectAssign(&rect, VALUE_LEFT, LABEL_TOP, VALUE_RIGHT,
				LABEL_BOTTOM);
	if (g_missionSetupWarheadOptionCount == 0) {
		sprintf(g_frontendScratchBuffer, "%c%s %s", COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_264_WARHEADS),
			FrontendString_Get(FRONTSTR_273_NONE));
	} else if (g_missionSetupWarheadOptionCount != 1 &&
		   loadoutSelectionLocked == 0) {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_264_WARHEADS),
			COLOR_ESCAPE_SELECTABLE,
			FrontendString_Get(
				(FrontendStringId)(MissionSetup_GetWarheadType(
							   -1) +
						   WARHEAD_STRING_BASE)));
	} else {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_264_WARHEADS),
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(
				(FrontendStringId)(MissionSetup_GetWarheadType(
							   -1) +
						   WARHEAD_STRING_BASE)));
	}
	rect.left = rect.right - FrontendText_MeasureWidth(
					 g_frontendScratchBuffer, FONT_SIZE);
	FrontendText_DrawAlignedInRect(FONT_SIZE, g_frontendScratchBuffer,
				       &rect, 0, 1, 0xFFFF);
	FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);

	drawBeam = 1;
	if (g_missionSetupBeamOptionCount <= 0) {
		sprintf(g_frontendScratchBuffer, "%c%s %s", COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_266_BEAM_WEAPON),
			FrontendString_Get(FRONTSTR_273_NONE));
		craftType = MissionSetup_GetCraftType(-1);
		if (craftType >= 1 && (craftType <= 5 || craftType == 14)) {
			drawBeam = 0;
		}
	} else {
		craftType = MissionSetup_GetCraftType(-1);
		if (craftType < 1 || (craftType > 5 && craftType != 14)) {
			sprintf(g_frontendScratchBuffer, "%c%s %c%s",
				COLOR_ESCAPE_FIXED,
				FrontendString_Get(FRONTSTR_266_BEAM_WEAPON),
				COLOR_ESCAPE_SELECTABLE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetBeamType(
								  -1) +
							  BEAM_STRING_BASE)));
		} else {
			sprintf(g_frontendScratchBuffer, "%c%s %s",
				COLOR_ESCAPE_FIXED,
				FrontendString_Get(FRONTSTR_266_BEAM_WEAPON),
				FrontendString_Get(FRONTSTR_273_NONE));
			drawBeam = 0;
		}
	}
	if (drawBeam != 0) {
		rect.left = rect.right -
			    FrontendText_MeasureWidth(g_frontendScratchBuffer,
						      FONT_SIZE);
		if (g_missionSetupBeamOptionCount == 1 ||
		    loadoutSelectionLocked != 0) {
			FrontendText_DrawAlignedInRect(
				FONT_SIZE, g_frontendScratchBuffer, &rect, 0, 1,
				g_colorYellow);
		} else {
			FrontendText_DrawAlignedInRect(FONT_SIZE,
						       g_frontendScratchBuffer,
						       &rect, 0, 1, 0xFFFF);
		}
		FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
	}

	if (g_missionSetupCountermeasureOptionCount == 0) {
		sprintf(g_frontendScratchBuffer, "%c%s %s", COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_265_COUNTERMEASURES),
			FrontendString_Get(FRONTSTR_273_NONE));
	} else if (g_missionSetupCountermeasureOptionCount != 1 &&
		   loadoutSelectionLocked == 0) {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_265_COUNTERMEASURES),
			COLOR_ESCAPE_SELECTABLE,
			FrontendString_Get((
				FrontendStringId)(MissionSetup_GetCountermeasureType(
							  -1) +
						  COUNTERMEASURE_STRING_BASE)));
	} else {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			FrontendString_Get(FRONTSTR_265_COUNTERMEASURES),
			COLOR_ESCAPE_FIXED,
			FrontendString_Get((
				FrontendStringId)(MissionSetup_GetCountermeasureType(
							  -1) +
						  COUNTERMEASURE_STRING_BASE)));
	}
	rect.left = rect.right - FrontendText_MeasureWidth(
					 g_frontendScratchBuffer, FONT_SIZE);
	FrontendText_DrawAlignedInRect(FONT_SIZE, g_frontendScratchBuffer,
				       &rect, 0, 1, 0xFFFF);
}

/* Draws the table of the players' craft and loadouts on the mission briefing's
 * craft screen; called by mission_briefing.c. Its headings sit at y
 * 341 - (15 * readyCount >> 1), readyCount being Net_CountReadyPlayers(). The
 * local player's row comes first, then the others: rating and name, craft,
 * warhead, beam and countermeasure, gray once the player is marked ready in
 * g_mpRosterReadyFlags, else yellow, the local name (every name in a solo game)
 * pulsing until then. For training, combat engagements and battles the other
 * players on the pilot's team (g_pilotData.team) follow, then the rest with the
 * not on your team text in place of a loadout. For any other mission type every
 * other player follows, with "----" in place of the loadout when the flight
 * group assigned to the player's slot, found among the first g_teamCount teams,
 * is -1. */
// FUNCTION: XVT 0x4ECB90
void MissionSetup_DrawPlayerLoadouts(int frameCounter)
{
	enum {
		PLAYER_SLOTS_PER_TEAM = 8,
		ROSTER_CAPACITY = sizeof(g_mpRoster) / sizeof(g_mpRoster[0]),
		PLAYER_X = 88,
		CRAFT_X = 228,
		WARHEAD_X = 338,
		BEAM_X = 378,
		COUNTERMEASURE_X = 403,
		ROW_HEIGHT = 15,
		FONT_SIZE = 12,
		PULSE_PERIOD = 24,
		PULSE_PAIR_MASK = ~1
	};

	RECT rowRect;
	RECT oldClipRect;
	RECT otherTeamRect;
	int rowY;
	int rosterIndex;
	int teamIndex;
	int teamPlayerIndex;
	int playerId;
	int assignmentMissing;
	uint16_t color;

	rowY = 341 - (ROW_HEIGHT * Net_CountReadyPlayers() >> 1);
	FrontendText_Draw(FONT_SIZE, FrontendString_Get(FRONTSTR_186_PLAYERS),
			  PLAYER_X, rowY, g_colorYellow);
	FrontendText_Draw(FONT_SIZE, FrontendString_Get(FRONTSTR_576_CRAFT),
			  CRAFT_X, rowY, g_colorYellow);
	FrontendText_Draw(FONT_SIZE, FrontendString_Get(FRONTSTR_577_WHD),
			  WARHEAD_X, rowY, g_colorYellow);
	FrontendText_Draw(FONT_SIZE, FrontendString_Get(FRONTSTR_578_BM),
			  BEAM_X, rowY, g_colorYellow);
	FrontendText_Draw(FONT_SIZE, FrontendString_Get(FRONTSTR_579_CM),
			  COUNTERMEASURE_X, rowY, g_colorYellow);
	rowY += ROW_HEIGHT;
	(void)Net_CountReadyPlayers();

	if (g_pilotData.missionDirectoryId !=
		    MISSION_DIRECTORY_TRAINING_EXERCISES &&
	    g_pilotData.missionDirectoryId !=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilotData.missionDirectoryId != MISSION_DIRECTORY_BATTLES) {
		for (rosterIndex = 0; rosterIndex < ROSTER_CAPACITY;
		     ++rosterIndex) {
			playerId = g_mpRoster[rosterIndex].playerId;
			if (playerId == 0 ||
			    Net_GetLocalPlayerId() != playerId) {
				continue;
			}

			color = g_mpRosterReadyFlags[rosterIndex] != 0
					? g_colorGray
					: g_colorYellow;
			FrontendDraw_RectAssign(&rowRect, PLAYER_X, rowY,
						CRAFT_X - 2, rowY + ROW_HEIGHT);
			FrontendDisplay_GetScreenClipRect(&oldClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rowRect);
			if (g_mpRosterReadyFlags[rosterIndex] != 0) {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					g_mpRoster[rosterIndex].name);
				FrontendText_Draw(FONT_SIZE,
						  g_frontendScratchBuffer,
						  PLAYER_X, rowY, color);
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				FrontendText_Draw(
					FONT_SIZE, g_frontendScratchBuffer,
					PLAYER_X, rowY,
					Net_GetLocalPlayerId() ==
								g_mpRoster[rosterIndex]
									.playerId ||
							g_frontendMissionSessionMode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulseColorRamp
							  [((frameCounter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			FrontendDisplay_SetScreenClipRect640x480(&oldClipRect);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetCraftType(
								  rosterIndex) +
							  21)),
				CRAFT_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetWarheadType(
								  rosterIndex) +
							  580)),
				WARHEAD_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetBeamType(
								  rosterIndex) +
							  590)),
				BEAM_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetCountermeasureType(
								  rosterIndex) +
							  595)),
				COUNTERMEASURE_X, rowY, color);
			rowY += ROW_HEIGHT;
		}

		for (rosterIndex = 0; rosterIndex < ROSTER_CAPACITY;
		     ++rosterIndex) {
			playerId = g_mpRoster[rosterIndex].playerId;
			if (playerId == 0 ||
			    Net_GetLocalPlayerId() == playerId) {
				continue;
			}

			color = g_mpRosterReadyFlags[rosterIndex] != 0
					? g_colorGray
					: g_colorYellow;
			assignmentMissing = 0;
			for (teamIndex = 0; teamIndex < g_teamCount;
			     ++teamIndex) {
				for (teamPlayerIndex = 0;
				     teamPlayerIndex <
				     g_teamPlayerFlightGroupCount[teamIndex];
				     ++teamPlayerIndex) {
					if (g_missionSetupPlayerAssignments
						    .teamPlayerIds
							    [teamIndex]
							    [teamPlayerIndex] ==
					    playerId) {
						break;
					}
				}
				if (teamPlayerIndex <
				    g_teamPlayerFlightGroupCount[teamIndex]) {
					if (g_missionSetupPlayerFlightGroupIndices
						    [teamIndex *
							     PLAYER_SLOTS_PER_TEAM +
						     teamPlayerIndex] == -1) {
						assignmentMissing = 1;
					}
					break;
				}
			}

			FrontendDraw_RectAssign(&rowRect, PLAYER_X, rowY,
						CRAFT_X - 2, rowY + ROW_HEIGHT);
			FrontendDisplay_GetScreenClipRect(&oldClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rowRect);
			if (g_mpRosterReadyFlags[rosterIndex] != 0) {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					g_mpRoster[rosterIndex].name);
				FrontendText_Draw(FONT_SIZE,
						  g_frontendScratchBuffer,
						  PLAYER_X, rowY, color);
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				FrontendText_Draw(
					FONT_SIZE, g_frontendScratchBuffer,
					PLAYER_X, rowY,
					Net_GetLocalPlayerId() == playerId ||
							g_frontendMissionSessionMode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulseColorRamp
							  [((frameCounter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			FrontendDisplay_SetScreenClipRect640x480(&oldClipRect);
			if (assignmentMissing) {
				FrontendText_Draw(FONT_SIZE, "----", CRAFT_X,
						  rowY, color);
				FrontendText_Draw(FONT_SIZE, "----", WARHEAD_X,
						  rowY, color);
				FrontendText_Draw(FONT_SIZE, "----", BEAM_X,
						  rowY, color);
				FrontendText_Draw(FONT_SIZE, "----",
						  COUNTERMEASURE_X, rowY,
						  color);
			} else {
				FrontendText_Draw(
					FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)(MissionSetup_GetCraftType(
									  rosterIndex) +
								  21)),
					CRAFT_X, rowY, color);
				FrontendText_Draw(
					FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)(MissionSetup_GetWarheadType(
									  rosterIndex) +
								  580)),
					WARHEAD_X, rowY, color);
				FrontendText_Draw(
					FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)(MissionSetup_GetBeamType(
									  rosterIndex) +
								  590)),
					BEAM_X, rowY, color);
				FrontendText_Draw(
					FONT_SIZE,
					FrontendString_Get((
						FrontendStringId)(MissionSetup_GetCountermeasureType(
									  rosterIndex) +
								  595)),
					COUNTERMEASURE_X, rowY, color);
			}
			rowY += ROW_HEIGHT;
		}
	} else {
		for (rosterIndex = 0; rosterIndex < ROSTER_CAPACITY;
		     ++rosterIndex) {
			playerId = g_mpRoster[rosterIndex].playerId;
			if (playerId == 0 ||
			    Net_GetLocalPlayerId() != playerId) {
				continue;
			}

			color = g_mpRosterReadyFlags[rosterIndex] != 0
					? g_colorGray
					: g_colorYellow;
			FrontendDraw_RectAssign(&rowRect, PLAYER_X, rowY,
						CRAFT_X - 2, rowY + ROW_HEIGHT);
			FrontendDisplay_GetScreenClipRect(&oldClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rowRect);
			if (g_mpRosterReadyFlags[rosterIndex] != 0) {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					g_mpRoster[rosterIndex].name);
				FrontendText_Draw(FONT_SIZE,
						  g_frontendScratchBuffer,
						  PLAYER_X, rowY, color);
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				FrontendText_Draw(
					FONT_SIZE, g_frontendScratchBuffer,
					PLAYER_X, rowY,
					Net_GetLocalPlayerId() ==
								g_mpRoster[rosterIndex]
									.playerId ||
							g_frontendMissionSessionMode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulseColorRamp
							  [((frameCounter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			FrontendDisplay_SetScreenClipRect640x480(&oldClipRect);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetCraftType(
								  rosterIndex) +
							  21)),
				CRAFT_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetWarheadType(
								  rosterIndex) +
							  580)),
				WARHEAD_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetBeamType(
								  rosterIndex) +
							  590)),
				BEAM_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetCountermeasureType(
								  rosterIndex) +
							  595)),
				COUNTERMEASURE_X, rowY, color);
			rowY += ROW_HEIGHT;
		}

		for (rosterIndex = 0; rosterIndex < ROSTER_CAPACITY;
		     ++rosterIndex) {
			playerId = g_mpRoster[rosterIndex].playerId;
			if (playerId == 0 ||
			    Net_GetLocalPlayerId() == playerId) {
				continue;
			}

			for (teamPlayerIndex = 0;
			     teamPlayerIndex <
			     g_teamPlayerFlightGroupCount[g_pilotData.team];
			     ++teamPlayerIndex) {
				if (g_missionSetupPlayerAssignments
					    .teamPlayerIds[g_pilotData.team]
							  [teamPlayerIndex] ==
				    playerId) {
					break;
				}
			}
			if (teamPlayerIndex ==
			    g_teamPlayerFlightGroupCount[g_pilotData.team]) {
				continue;
			}

			color = g_mpRosterReadyFlags[rosterIndex] != 0
					? g_colorGray
					: g_colorYellow;
			FrontendDraw_RectAssign(&rowRect, PLAYER_X, rowY,
						CRAFT_X - 2, rowY + ROW_HEIGHT);
			FrontendDisplay_GetScreenClipRect(&oldClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rowRect);
			if (g_mpRosterReadyFlags[rosterIndex] != 0) {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					g_mpRoster[rosterIndex].name);
				FrontendText_Draw(FONT_SIZE,
						  g_frontendScratchBuffer,
						  PLAYER_X, rowY, color);
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				FrontendText_Draw(
					FONT_SIZE, g_frontendScratchBuffer,
					PLAYER_X, rowY,
					Net_GetLocalPlayerId() == playerId ||
							g_frontendMissionSessionMode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulseColorRamp
							  [((frameCounter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			FrontendDisplay_SetScreenClipRect640x480(&oldClipRect);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetCraftType(
								  rosterIndex) +
							  21)),
				CRAFT_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetWarheadType(
								  rosterIndex) +
							  580)),
				WARHEAD_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetBeamType(
								  rosterIndex) +
							  590)),
				BEAM_X, rowY, color);
			FrontendText_Draw(
				FONT_SIZE,
				FrontendString_Get((
					FrontendStringId)(MissionSetup_GetCountermeasureType(
								  rosterIndex) +
							  595)),
				COUNTERMEASURE_X, rowY, color);
			rowY += ROW_HEIGHT;
		}

		for (rosterIndex = 0; rosterIndex < ROSTER_CAPACITY;
		     ++rosterIndex) {
			playerId = g_mpRoster[rosterIndex].playerId;
			if (playerId == 0 ||
			    Net_GetLocalPlayerId() == playerId) {
				continue;
			}

			for (teamPlayerIndex = 0;
			     teamPlayerIndex <
			     g_teamPlayerFlightGroupCount[g_pilotData.team];
			     ++teamPlayerIndex) {
				if (g_missionSetupPlayerAssignments
					    .teamPlayerIds[g_pilotData.team]
							  [teamPlayerIndex] ==
				    playerId) {
					break;
				}
			}
			if (teamPlayerIndex !=
			    g_teamPlayerFlightGroupCount[g_pilotData.team]) {
				continue;
			}

			color = g_mpRosterReadyFlags[rosterIndex] != 0
					? g_colorGray
					: g_colorYellow;
			FrontendDraw_RectAssign(&rowRect, PLAYER_X, rowY,
						CRAFT_X - 2, rowY + ROW_HEIGHT);
			FrontendDisplay_GetScreenClipRect(&oldClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rowRect);
			if (g_mpRosterReadyFlags[rosterIndex] != 0) {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					g_mpRoster[rosterIndex].name);
				FrontendText_Draw(FONT_SIZE,
						  g_frontendScratchBuffer,
						  PLAYER_X, rowY, color);
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				FrontendText_Draw(
					FONT_SIZE, g_frontendScratchBuffer,
					PLAYER_X, rowY,
					Net_GetLocalPlayerId() == playerId ||
							g_frontendMissionSessionMode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulseColorRamp
							  [((frameCounter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			FrontendDisplay_SetScreenClipRect640x480(&oldClipRect);
			FrontendDraw_RectAssign(&otherTeamRect, CRAFT_X, rowY,
						COUNTERMEASURE_X,
						rowY + ROW_HEIGHT - 1);
			FrontendText_DrawAlignedInRect(
				FONT_SIZE,
				FrontendString_Get(
					FRONTSTR_599_NOT_ON_YOUR_TEAM),
				&otherTeamRect, 1, 0, color);
			rowY += ROW_HEIGHT;
		}
	}
}

/* Handles the loadout buttons of the mission briefing's craft screen; called by
 * mission_briefing.c each frame. Next and previous craft, warhead, beam weapon
 * and countermeasure are each lit when there is more than one choice and the
 * player may change the loadout: a solo player may; in a training sequence
 * (training type with missionSequenceActive 1), in either kind of game, only at
 * the easy cheat difficulty; outside a solo game otherwise as
 * g_gameConfig.craftSelection says: on for everyone, host only for the host,
 * off for no one. The beam button is also unlit for craft types 1 to 5 and 14.
 * The warhead, beam and countermeasure buttons step their
 * g_missionSetupSelected option forward on a left click and back otherwise,
 * wrapping. The craft buttons step
 * g_missionSetupSelectedPresetCraftOptionIndex, skipping the preset that is the
 * flight group's own craft, or else
 * g_missionSetupSelectedFlightGroupCraftOptionIndex, which also sets
 * g_missionSetupSelectedCraftCount and g_missionSetupSelectedWaveCountMinusOne
 * from the option; in a melee or tournament sequence past its first mission
 * they also step past craft of the other faction (types 1 to 4 and 14 are
 * Rebel). A new craft loads its model into the preview and, in a melee or
 * tournament, swaps the background between the Rebel and Imperial craft screens
 * by the craft's faction, setting g_missionBriefingCraftScreenFaction and
 * redrawing the screen's base. When a choice changed outside a solo game it
 * sends a 9-word CRAFT_LOADOUT packet to every player: counting the type as
 * word 0, the flight group's optional craft category, the preset and flight
 * group craft options, the warhead, beam and countermeasure options, the wave
 * count minus one and the craft count. Returns 0. Does not check that some
 * craft option fits the faction: the stepping then never ends. */
// FUNCTION: XVT 0x4ED700
int MissionSetup_UpdateCraftLoadout(void)
{
	enum {
		NAVIGATION_SLOT_COUNT = 8,
		ACTIVE_LOADOUT_SLOT_COUNT = 5,
		NEXT_CRAFT_SLOT = 0,
		PREVIOUS_CRAFT_SLOT = 1,
		WARHEAD_SLOT = 2,
		BEAM_SLOT = 3,
		COUNTERMEASURE_SLOT = 4,
		BUTTON_SPACING = 28,
		BUTTON_FONT_SIZE = 12,
		NEXT_CRAFT_HELD_SLOT = 11,
		PREVIOUS_CRAFT_HELD_SLOT = 12,
		WARHEAD_HELD_SLOT = 13,
		BEAM_HELD_SLOT = 14,
		COUNTERMEASURE_HELD_SLOT = 15,
		PILOT_FACTION_REBEL = 0,
		PILOT_FACTION_IMPERIAL = 1,
		PACKET_PRESET_CRAFT_OFFSET = 4,
		PACKET_FLIGHT_GROUP_CRAFT_OFFSET = 8,
		PACKET_WARHEAD_OFFSET = 12,
		PACKET_BEAM_OFFSET = 16,
		PACKET_COUNTERMEASURE_OFFSET = 20,
		PACKET_WAVE_COUNT_OFFSET = 24,
		PACKET_CRAFT_COUNT_OFFSET = 28,
		CRAFT_LOADOUT_PACKET_SIZE = 9 * sizeof(int),
	};

	int isHost;
	int canChangeLoadout;
	int loadoutChanged;
	int craftButtonsEnabled;
	FrontendNavigationSlotState slotStates[NAVIGATION_SLOT_COUNT];
	int slotIndex;
	int mouseY;
	int mouseX;
	RECT rect;
	int selectedPresetCraftOptionIndex;
	int selectedFlightGroupCraftOptionIndex;
	int selectedFlightGroupIndex;
	int rejectCraftChoice;
	int craftType;
	int selectedOptionIndex;
	int leftClick;

	isHost = 0;
	if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
	    Net_IsHost() != 0) {
		isHost = 1;
	}
	loadoutChanged = 0;
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilotData.missionSequenceActive == 1) {
			canChangeLoadout = g_gameConfig.difficulty ==
					   GAME_DIFFICULTY_EASY_CHEAT;
		} else {
			canChangeLoadout = 1;
		}
	} else {
		canChangeLoadout = 0;
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilotData.missionSequenceActive == 1) {
			canChangeLoadout = g_gameConfig.difficulty ==
					   GAME_DIFFICULTY_EASY_CHEAT;
		} else {
			/* selectedOptionIndex holds the craft selection setting here; from the loadout buttons on it
			 * holds the countermeasure, beam or warhead option being cycled. */
			selectedOptionIndex = g_gameConfig.craftSelection;
			if (selectedOptionIndex != CRAFT_SELECTION_OFF) {
				if (selectedOptionIndex == CRAFT_SELECTION_ON) {
					canChangeLoadout = 1;
				} else if (selectedOptionIndex ==
						   CRAFT_SELECTION_HOST_ONLY &&
					   isHost) {
					canChangeLoadout = 1;
				}
			}
		}
	}

	craftButtonsEnabled = 0;
	if ((g_missionSetupFlightGroupCraftOptionCount > 1 ||
	     g_missionSetupPresetCraftOptionCount > 1) &&
	    canChangeLoadout) {
		craftButtonsEnabled = 1;
	}
	slotStates[NEXT_CRAFT_SLOT] = craftButtonsEnabled;
	slotStates[PREVIOUS_CRAFT_SLOT] = craftButtonsEnabled;
	slotStates[WARHEAD_SLOT] =
		g_missionSetupWarheadOptionCount > 1 && canChangeLoadout;
	if (g_missionSetupBeamOptionCount <= 1 || !canChangeLoadout) {
		slotStates[BEAM_SLOT] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	} else {
		slotStates[BEAM_SLOT] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		craftType = MissionSetup_GetCraftType(-1);
		if (craftType >= CRAFT_SPECIES_X_WING &&
		    (craftType <= CRAFT_SPECIES_TIE_FIGHTER ||
		     craftType == CRAFT_SPECIES_Z_95_HEADHUNTER)) {
			slotStates[BEAM_SLOT] =
				FRONTEND_NAVIGATION_SLOT_INACTIVE;
		}
	}
	slotStates[COUNTERMEASURE_SLOT] =
		g_missionSetupCountermeasureOptionCount > 1 && canChangeLoadout;
	slotStates[ACTIVE_LOADOUT_SLOT_COUNT] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[ACTIVE_LOADOUT_SLOT_COUNT + 1] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[ACTIVE_LOADOUT_SLOT_COUNT + 2] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;

	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&rect, 22, 114, 42, 138);
	for (slotIndex = 0; slotIndex < ACTIVE_LOADOUT_SLOT_COUNT;
	     ++slotIndex) {
		if (slotStates[slotIndex] !=
			    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
		    FrontendDraw_PointInRect(&rect, mouseX, mouseY) != 0 &&
		    (FrontendMouse_GetLeftDown() != 0 ||
		     FrontendMouse_GetRightDown() != 0 ||
		     FrontendMouse_GetLeftClick() != 0 ||
		     FrontendMouse_GetRightClick() != 0)) {
			slotStates[slotIndex] =
				FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		FrontendDraw_RectOffsetXY(&rect, 0, BUTTON_SPACING);
	}
	FrontendButton_DrawEightSlotNavigationState(slotStates);

	FrontendDraw_RectAssign(&rect, 22, 226, 42, 250);
	if (slotStates[COUNTERMEASURE_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    FrontendButton_HandleSpriteButton(
		    &rect, "craft5u", "craft5d",
		    FrontendString_Get(FRONTSTR_271_NEXT_COUNTERMEASURE_CHOICE),
		    BUTTON_FONT_SIZE, 0, COUNTERMEASURE_HELD_SLOT,
		    "jewelsound") != 0) {
		leftClick = FrontendMouse_GetLeftClick();
		selectedOptionIndex =
			g_missionSetupSelectedCountermeasureOptionIndex;
		if (leftClick != 0) {
			++selectedOptionIndex;
			g_missionSetupSelectedCountermeasureOptionIndex =
				selectedOptionIndex;
			if (g_missionSetupCountermeasureOptionCount <=
			    selectedOptionIndex) {
				g_missionSetupSelectedCountermeasureOptionIndex =
					0;
			}
		} else {
			if (selectedOptionIndex != 0) {
				--selectedOptionIndex;
			} else {
				selectedOptionIndex =
					g_missionSetupCountermeasureOptionCount -
					1;
			}
			g_missionSetupSelectedCountermeasureOptionIndex =
				selectedOptionIndex;
		}
		loadoutChanged = 1;
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (slotStates[BEAM_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    FrontendButton_HandleSpriteButton(
		    &rect, "craft4u", "craft4d",
		    FrontendString_Get(FRONTSTR_272_NEXT_BEAM_WEAPON_CHOICE),
		    BUTTON_FONT_SIZE, 0, BEAM_HELD_SLOT, "jewelsound") != 0) {
		leftClick = FrontendMouse_GetLeftClick();
		selectedOptionIndex = g_missionSetupSelectedBeamOptionIndex;
		if (leftClick != 0) {
			++selectedOptionIndex;
			g_missionSetupSelectedBeamOptionIndex =
				selectedOptionIndex;
			if (g_missionSetupBeamOptionCount <=
			    selectedOptionIndex) {
				g_missionSetupSelectedBeamOptionIndex = 0;
			}
		} else {
			if (selectedOptionIndex != 0) {
				--selectedOptionIndex;
			} else {
				selectedOptionIndex =
					g_missionSetupBeamOptionCount - 1;
			}
			g_missionSetupSelectedBeamOptionIndex =
				selectedOptionIndex;
		}
		loadoutChanged = 1;
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (slotStates[WARHEAD_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    FrontendButton_HandleSpriteButton(
		    &rect, "craft3u", "craft3d",
		    FrontendString_Get(FRONTSTR_270_NEXT_WARHEAD_CHOICE),
		    BUTTON_FONT_SIZE, 0, WARHEAD_HELD_SLOT,
		    "jewelsound") != 0) {
		leftClick = FrontendMouse_GetLeftClick();
		selectedOptionIndex = g_missionSetupSelectedWarheadOptionIndex;
		if (leftClick != 0) {
			++selectedOptionIndex;
			g_missionSetupSelectedWarheadOptionIndex =
				selectedOptionIndex;
			if (g_missionSetupWarheadOptionCount <=
			    selectedOptionIndex) {
				g_missionSetupSelectedWarheadOptionIndex = 0;
			}
		} else {
			if (selectedOptionIndex != 0) {
				--selectedOptionIndex;
			} else {
				selectedOptionIndex =
					g_missionSetupWarheadOptionCount - 1;
			}
			g_missionSetupSelectedWarheadOptionIndex =
				selectedOptionIndex;
		}
		loadoutChanged = 1;
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (slotStates[PREVIOUS_CRAFT_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    FrontendButton_HandleSpriteButton(
		    &rect, "craft2u", "craft2d",
		    FrontendString_Get(FRONTSTR_269_PREVIOUS_CRAFT_CHOICE),
		    BUTTON_FONT_SIZE, 0, PREVIOUS_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		loadoutChanged = 1;
		selectedPresetCraftOptionIndex =
			g_missionSetupSelectedPresetCraftOptionIndex;
		selectedFlightGroupCraftOptionIndex =
			g_missionSetupSelectedFlightGroupCraftOptionIndex;
		selectedFlightGroupIndex =
			g_missionSetupSelectedFlightGroupIndex;
		for (;;) {
			rejectCraftChoice = 0;
			if (g_missionSetupPresetCraftOptionCount != 0) {
				--g_missionSetupSelectedPresetCraftOptionIndex;
				selectedPresetCraftOptionIndex =
					g_missionSetupSelectedPresetCraftOptionIndex;
				if (selectedPresetCraftOptionIndex < 0) {
					selectedPresetCraftOptionIndex =
						g_missionSetupPresetCraftOptionCount -
						1;
				}
				/* craftType holds the flight group's preset craft category here (1 to 3), not a craft
				 * species. */
				craftType =
					g_frontendMission
						.flightGroups
							[selectedFlightGroupIndex]
						.optionalCraftCategory;
				if (selectedPresetCraftOptionIndex != 0 &&
				    craftType >= 1) {
					if (craftType <= 2) {
						if ((uint8_t)g_frontendMission
							    .flightGroups
								    [selectedFlightGroupIndex]
							    .craftType ==
						    g_presetCraftTypes
							    [selectedPresetCraftOptionIndex]) {
							--selectedPresetCraftOptionIndex;
						}
					} else if (
						craftType == 3 &&
						(uint8_t)g_frontendMission
								.flightGroups
									[selectedFlightGroupIndex]
								.craftType ==
							g_presetCraftTypes
								[selectedPresetCraftOptionIndex +
								 5]) {
						--selectedPresetCraftOptionIndex;
					}
				}
			} else {
				--g_missionSetupSelectedFlightGroupCraftOptionIndex;
				selectedFlightGroupCraftOptionIndex =
					g_missionSetupSelectedFlightGroupCraftOptionIndex;
				if (selectedFlightGroupCraftOptionIndex < 0) {
					selectedFlightGroupCraftOptionIndex =
						g_missionSetupFlightGroupCraftOptionCount -
						1;
				}
				if (selectedFlightGroupCraftOptionIndex != 0) {
					g_missionSetupSelectedCraftCount =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfOptionalCraft
								[selectedFlightGroupCraftOptionIndex -
								 1];
					g_missionSetupSelectedWaveCountMinusOne =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfOptionalCraftWaves
								[selectedFlightGroupCraftOptionIndex -
								 1];
				} else {
					g_missionSetupSelectedCraftCount =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfCraft;
					g_missionSetupSelectedWaveCountMinusOne =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfWaves;
				}
			}

			if (g_pilotData.missionSequenceActive == 1 &&
			    (g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_MELEES ||
			     g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_TOURNAMENTS)) {
				g_missionSetupSelectedPresetCraftOptionIndex =
					selectedPresetCraftOptionIndex;
				g_missionSetupSelectedFlightGroupCraftOptionIndex =
					selectedFlightGroupCraftOptionIndex;
				if ((unsigned int)g_pilotData
					    .meleeTournamentSequenceState
					    .currentMissionIndex > 0) {
					craftType =
						MissionSetup_GetCraftType(-1);
					selectedPresetCraftOptionIndex =
						g_missionSetupSelectedPresetCraftOptionIndex;
					selectedFlightGroupCraftOptionIndex =
						g_missionSetupSelectedFlightGroupCraftOptionIndex;
					selectedFlightGroupIndex =
						g_missionSetupSelectedFlightGroupIndex;
					if (craftType >= CRAFT_SPECIES_X_WING &&
					    (craftType <=
						     CRAFT_SPECIES_B_WING ||
					     craftType ==
						     CRAFT_SPECIES_Z_95_HEADHUNTER)) {
						if (g_pilotData
							    .currentFactionId ==
						    PILOT_FACTION_IMPERIAL) {
							rejectCraftChoice = 1;
						}
					} else if (g_pilotData
							   .currentFactionId ==
						   PILOT_FACTION_REBEL) {
						rejectCraftChoice = 1;
					}
				}
			}
			g_missionSetupSelectedPresetCraftOptionIndex =
				selectedPresetCraftOptionIndex;
			g_missionSetupSelectedFlightGroupCraftOptionIndex =
				selectedFlightGroupCraftOptionIndex;
			if (rejectCraftChoice == 0) {
				craftType = MissionSetup_GetCraftType(-1);
				ModelPreview_LoadModel(
					g_shipList[g_shipTypeToShipListIndex
							   [craftType]]
						.modelFileName);
				ModelPreview_SetLightDirection(-1, 0, 1);
				if (g_pilotData.missionDirectoryId ==
					    MISSION_DIRECTORY_MELEES ||
				    g_pilotData.missionDirectoryId ==
					    MISSION_DIRECTORY_TOURNAMENTS) {
					craftType =
						MissionSetup_GetCraftType(-1);
					if (craftType >= CRAFT_SPECIES_X_WING &&
					    (craftType <=
						     CRAFT_SPECIES_B_WING ||
					     craftType ==
						     CRAFT_SPECIES_Z_95_HEADHUNTER)) {
						if (g_missionBriefingCraftScreenFaction ==
						    MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL) {
							FrontImage_FreeResourceByName(
								"background");
							g_missionBriefingCraftScreenFaction =
								MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
							if (g_frontendMissionSessionMode ==
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								FrontImage_RegisterResourceDefault(
									"frontres\\craftsr.bmp",
									"background");
							} else {
								FrontImage_RegisterResourceDefault(
									"frontres\\craftmr.bmp",
									"background");
							}
						}
					} else if (
						g_missionBriefingCraftScreenFaction ==
						MISSION_BRIEFING_CRAFT_SCREEN_REBEL) {
						FrontImage_FreeResourceByName(
							"background");
						g_missionBriefingCraftScreenFaction =
							MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
						if (g_frontendMissionSessionMode ==
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							FrontImage_RegisterResourceDefault(
								"frontres\\craftsi.bmp",
								"background");
						} else {
							FrontImage_RegisterResourceDefault(
								"frontres\\craftmi.bmp",
								"background");
						}
					}
					FrontendDisplay_LockOffscreenSurface();
					FrontImage_DrawSpriteOpaque(
						"background", 0, 0);
					FrontImage_DrawSprite("frame", 0, 0);
					FrontImage_DrawSprite("allactive", 0,
							      0);
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						FrontImage_DrawSpriteTranslucent(
							"chatbox", 0, 0);
					}
					FrontImage_DrawSpriteTranslucent(
						"regoverlay", 0, 0);
					FrontendDisplay_UnlockOffscreenSurface(
						1);
				}
				break;
			}
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_SPACING);
	if (slotStates[NEXT_CRAFT_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    FrontendButton_HandleSpriteButton(
		    &rect, "craft1u", "craft1d",
		    FrontendString_Get(FRONTSTR_268_NEXT_CRAFT_CHOICE),
		    BUTTON_FONT_SIZE, 0, NEXT_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		loadoutChanged = 1;
		selectedPresetCraftOptionIndex =
			g_missionSetupSelectedPresetCraftOptionIndex;
		selectedFlightGroupCraftOptionIndex =
			g_missionSetupSelectedFlightGroupCraftOptionIndex;
		selectedFlightGroupIndex =
			g_missionSetupSelectedFlightGroupIndex;
		do {
			rejectCraftChoice = 0;
			if (g_missionSetupPresetCraftOptionCount != 0) {
				++g_missionSetupSelectedPresetCraftOptionIndex;
				selectedPresetCraftOptionIndex =
					g_missionSetupSelectedPresetCraftOptionIndex;
				if (g_missionSetupPresetCraftOptionCount <=
				    selectedPresetCraftOptionIndex) {
					selectedPresetCraftOptionIndex = 0;
				}
				/* craftType holds the flight group's preset craft category here (1 to 3), not a craft
				 * species. */
				craftType =
					g_frontendMission
						.flightGroups
							[selectedFlightGroupIndex]
						.optionalCraftCategory;
				if (selectedPresetCraftOptionIndex != 0 &&
				    craftType >= 1) {
					if (craftType <= 2) {
						if ((uint8_t)g_frontendMission
							    .flightGroups
								    [selectedFlightGroupIndex]
							    .craftType ==
						    g_presetCraftTypes
							    [selectedPresetCraftOptionIndex]) {
							++selectedPresetCraftOptionIndex;
						}
					} else if (
						craftType == 3 &&
						(uint8_t)g_frontendMission
								.flightGroups
									[selectedFlightGroupIndex]
								.craftType ==
							g_presetCraftTypes
								[selectedPresetCraftOptionIndex +
								 5]) {
						++selectedPresetCraftOptionIndex;
					}
				}
				g_missionSetupSelectedPresetCraftOptionIndex =
					selectedPresetCraftOptionIndex;
				if (g_missionSetupPresetCraftOptionCount <=
				    selectedPresetCraftOptionIndex) {
					selectedPresetCraftOptionIndex = 0;
				}
			} else {
				++g_missionSetupSelectedFlightGroupCraftOptionIndex;
				selectedFlightGroupCraftOptionIndex =
					g_missionSetupSelectedFlightGroupCraftOptionIndex;
				if (g_missionSetupFlightGroupCraftOptionCount <=
				    selectedFlightGroupCraftOptionIndex) {
					selectedFlightGroupCraftOptionIndex = 0;
				}
				if (selectedFlightGroupCraftOptionIndex != 0) {
					g_missionSetupSelectedCraftCount =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfOptionalCraft
								[selectedFlightGroupCraftOptionIndex -
								 1];
					g_missionSetupSelectedWaveCountMinusOne =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfOptionalCraftWaves
								[selectedFlightGroupCraftOptionIndex -
								 1];
				} else {
					g_missionSetupSelectedCraftCount =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfCraft;
					g_missionSetupSelectedWaveCountMinusOne =
						g_frontendMission
							.flightGroups
								[selectedFlightGroupIndex]
							.numberOfWaves;
				}
			}

			if (g_pilotData.missionSequenceActive == 1 &&
			    (g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_MELEES ||
			     g_pilotData.missionDirectoryId ==
				     MISSION_DIRECTORY_TOURNAMENTS)) {
				g_missionSetupSelectedPresetCraftOptionIndex =
					selectedPresetCraftOptionIndex;
				g_missionSetupSelectedFlightGroupCraftOptionIndex =
					selectedFlightGroupCraftOptionIndex;
				if ((unsigned int)g_pilotData
					    .meleeTournamentSequenceState
					    .currentMissionIndex > 0) {
					craftType =
						MissionSetup_GetCraftType(-1);
					selectedPresetCraftOptionIndex =
						g_missionSetupSelectedPresetCraftOptionIndex;
					selectedFlightGroupCraftOptionIndex =
						g_missionSetupSelectedFlightGroupCraftOptionIndex;
					selectedFlightGroupIndex =
						g_missionSetupSelectedFlightGroupIndex;
					if (craftType >= CRAFT_SPECIES_X_WING &&
					    (craftType <=
						     CRAFT_SPECIES_B_WING ||
					     craftType ==
						     CRAFT_SPECIES_Z_95_HEADHUNTER)) {
						if (g_pilotData
							    .currentFactionId ==
						    PILOT_FACTION_IMPERIAL) {
							rejectCraftChoice = 1;
						}
					} else if (g_pilotData
							   .currentFactionId ==
						   PILOT_FACTION_REBEL) {
						rejectCraftChoice = 1;
					}
				}
			}
			g_missionSetupSelectedPresetCraftOptionIndex =
				selectedPresetCraftOptionIndex;
			g_missionSetupSelectedFlightGroupCraftOptionIndex =
				selectedFlightGroupCraftOptionIndex;
		} while (rejectCraftChoice != 0);

		craftType = MissionSetup_GetCraftType(-1);
		ModelPreview_LoadModel(
			g_shipList[g_shipTypeToShipListIndex[craftType]]
				.modelFileName);
		ModelPreview_SetLightDirection(-1, 0, 1);
		selectedPresetCraftOptionIndex =
			g_missionSetupSelectedPresetCraftOptionIndex;
		selectedFlightGroupCraftOptionIndex =
			g_missionSetupSelectedFlightGroupCraftOptionIndex;
		selectedFlightGroupIndex =
			g_missionSetupSelectedFlightGroupIndex;
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_MELEES ||
		    g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TOURNAMENTS) {
			craftType = MissionSetup_GetCraftType(-1);
			if (craftType >= CRAFT_SPECIES_X_WING &&
			    (craftType <= CRAFT_SPECIES_B_WING ||
			     craftType == CRAFT_SPECIES_Z_95_HEADHUNTER)) {
				if (g_missionBriefingCraftScreenFaction ==
				    MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL) {
					FrontImage_FreeResourceByName(
						"background");
					g_missionBriefingCraftScreenFaction =
						MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
					if (g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						FrontImage_RegisterResourceDefault(
							"frontres\\craftsr.bmp",
							"background");
					} else {
						FrontImage_RegisterResourceDefault(
							"frontres\\craftmr.bmp",
							"background");
					}
				}
			} else if (g_missionBriefingCraftScreenFaction ==
				   MISSION_BRIEFING_CRAFT_SCREEN_REBEL) {
				FrontImage_FreeResourceByName("background");
				g_missionBriefingCraftScreenFaction =
					MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				if (g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontImage_RegisterResourceDefault(
						"frontres\\craftsi.bmp",
						"background");
				} else {
					FrontImage_RegisterResourceDefault(
						"frontres\\craftmi.bmp",
						"background");
				}
			}
			FrontendDisplay_LockOffscreenSurface();
			FrontImage_DrawSpriteOpaque("background", 0, 0);
			FrontImage_DrawSprite("frame", 0, 0);
			FrontImage_DrawSprite("allactive", 0, 0);
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontImage_DrawSpriteTranslucent("chatbox", 0,
								 0);
			}
			FrontImage_DrawSpriteTranslucent("regoverlay", 0, 0);
			FrontendDisplay_UnlockOffscreenSurface(1);
		}
	}

	selectedPresetCraftOptionIndex =
		g_missionSetupSelectedPresetCraftOptionIndex;
	selectedFlightGroupCraftOptionIndex =
		g_missionSetupSelectedFlightGroupCraftOptionIndex;
	selectedFlightGroupIndex = g_missionSetupSelectedFlightGroupIndex;
	if (loadoutChanged != 0 &&
	    g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		*(int *)&g_frontendNetPacketScratch
			 .payload[PACKET_PRESET_CRAFT_OFFSET] =
			selectedPresetCraftOptionIndex;
		*(int *)&g_frontendNetPacketScratch
			 .payload[PACKET_FLIGHT_GROUP_CRAFT_OFFSET] =
			selectedFlightGroupCraftOptionIndex;
		g_frontendNetPacketScratch.packetType =
			NET_PACKET_CRAFT_LOADOUT;
		memcpy(&g_frontendNetPacketScratch.payload[PACKET_BEAM_OFFSET],
		       &g_missionSetupSelectedBeamOptionIndex,
		       sizeof(g_missionSetupSelectedBeamOptionIndex));
		*(int *)&g_frontendNetPacketScratch.payload[0] =
			g_frontendMission.flightGroups[selectedFlightGroupIndex]
				.optionalCraftCategory;
		memcpy(&g_frontendNetPacketScratch
				.payload[PACKET_WARHEAD_OFFSET],
		       &g_missionSetupSelectedWarheadOptionIndex,
		       sizeof(g_missionSetupSelectedWarheadOptionIndex));
		memcpy(&g_frontendNetPacketScratch
				.payload[PACKET_COUNTERMEASURE_OFFSET],
		       &g_missionSetupSelectedCountermeasureOptionIndex,
		       sizeof(g_missionSetupSelectedCountermeasureOptionIndex));
		memcpy(&g_frontendNetPacketScratch
				.payload[PACKET_WAVE_COUNT_OFFSET],
		       &g_missionSetupSelectedWaveCountMinusOne,
		       sizeof(g_missionSetupSelectedWaveCountMinusOne));
		memcpy(&g_frontendNetPacketScratch
				.payload[PACKET_CRAFT_COUNT_OFFSET],
		       &g_missionSetupSelectedCraftCount,
		       sizeof(g_missionSetupSelectedCraftCount));
		Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
				       CRAFT_LOADOUT_PACKET_SIZE);
	}
	return 0;
}

/* Sets up the local player's craft and loadout choices for the mission
 * briefing's craft screen; called by mission_briefing.c. Picks the flight
 * group: in a solo game
 * g_missionSetupPlayerFlightGroupIndices[g_pilotData.team * 8]; otherwise the
 * one assigned to the slot of g_pilotData.team holding the local player's id,
 * or g_missionSetupSelectedFlightGroupIndex as it was when no slot does. Stores
 * it in g_missionSetupSelectedFlightGroupIndex, sets the five option indices to
 * 0 and the wave and craft counts from the flight group, and sets the option
 * counts: by the group's optional craft category, 0 gives one flight group
 * choice, 1 the 11 presets, 2 and 3 six presets, and 4 the group's own optional
 * craft plus 1 (unchanged for another category); warheads, beams and
 * countermeasures each count the group's nonzero optional entries, plus 1 when
 * there are any or the group has a default. In a melee or tournament sequence
 * past its first mission, when the craft does not suit the pilot's faction (a
 * Rebel craft, types 1 to 4 and 14, for an Imperial pilot, or any other craft
 * for a Rebel pilot), it steps through the craft options until it reaches
 * g_craftIffCounterpart of that craft; nothing changes when the counterpart is
 * 0. Does not check that an option gives the counterpart: the stepping then
 * never ends. */
// FUNCTION: XVT 0x4EE1A0
void MissionSetup_InitCraftLoadout(void)
{
	int selectedFlightGroupIndex;
	int playerIndex;
	int teamPlayerId;
	int localPlayerId;
	int optionIndex;
	int optionCount;
	int craftType;
	int counterpartCraftType;
	int craftTypeMismatch;
	int presetCraftOptionIndex;
	int flightGroupCraftOptionIndex;

	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		selectedFlightGroupIndex =
			g_missionSetupPlayerFlightGroupIndices
				[g_pilotData.team * 8];
	} else {
		for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
			teamPlayerId = g_missionSetupPlayerAssignments
					       .teamPlayerIds[g_pilotData.team]
							     [playerIndex];
			localPlayerId = Net_GetLocalPlayerId();
			selectedFlightGroupIndex =
				g_missionSetupSelectedFlightGroupIndex;
			if (teamPlayerId == localPlayerId) {
				selectedFlightGroupIndex =
					g_missionSetupPlayerFlightGroupIndices
						[g_pilotData.team * 8 +
						 playerIndex];
			}
			g_missionSetupSelectedFlightGroupIndex =
				selectedFlightGroupIndex;
		}
	}

	g_missionSetupSelectedFlightGroupIndex = selectedFlightGroupIndex;
	g_missionSetupSelectedFlightGroupCraftOptionIndex = 0;
	g_missionSetupSelectedPresetCraftOptionIndex = 0;
	g_missionSetupSelectedWarheadOptionIndex = 0;
	g_missionSetupSelectedBeamOptionIndex = 0;
	g_missionSetupSelectedCountermeasureOptionIndex = 0;
	g_missionSetupSelectedWaveCountMinusOne =
		g_frontendMission.flightGroups[selectedFlightGroupIndex]
			.numberOfWaves;
	g_missionSetupSelectedCraftCount =
		g_frontendMission.flightGroups[selectedFlightGroupIndex]
			.numberOfCraft;

	switch (g_frontendMission.flightGroups[selectedFlightGroupIndex]
			.optionalCraftCategory) {
	case 0:
		g_missionSetupPresetCraftOptionCount = 0;
		g_missionSetupFlightGroupCraftOptionCount = 1;
		break;
	case 1:
		g_missionSetupPresetCraftOptionCount = 11;
		g_missionSetupFlightGroupCraftOptionCount = 0;
		break;
	case 2:
		g_missionSetupPresetCraftOptionCount = 6;
		g_missionSetupFlightGroupCraftOptionCount = 0;
		break;
	case 3:
		g_missionSetupPresetCraftOptionCount = 6;
		g_missionSetupFlightGroupCraftOptionCount = 0;
		break;
	case 4:
		optionCount = 1;
		g_missionSetupPresetCraftOptionCount = 0;
		for (optionIndex = 0; optionIndex < 10; ++optionIndex) {
			if (g_frontendMission
				    .flightGroups[selectedFlightGroupIndex]
				    .optionalCraft[optionIndex] !=
			    CRAFT_SPECIES_UNKNOWN) {
				++optionCount;
			}
			g_missionSetupFlightGroupCraftOptionCount = optionCount;
		}
		break;
	default:
		break;
	}

	optionCount = 0;
	for (optionIndex = 0; optionIndex < 8; ++optionIndex) {
		if (g_frontendMission.flightGroups[selectedFlightGroupIndex]
			    .optionalWarheads[optionIndex] != 0) {
			++optionCount;
		}
	}
	if (g_frontendMission.flightGroups[selectedFlightGroupIndex].warhead !=
	    0) {
		g_missionSetupWarheadOptionCount = optionCount + 1;
	} else {
		g_missionSetupWarheadOptionCount = optionCount;
		if (optionCount != 0) {
			g_missionSetupWarheadOptionCount = optionCount + 1;
		}
	}

	optionCount = 0;
	for (optionIndex = 0; optionIndex < 6; ++optionIndex) {
		if (g_frontendMission.flightGroups[selectedFlightGroupIndex]
			    .optionalBeams[optionIndex] != 0) {
			++optionCount;
		}
	}
	if (g_frontendMission.flightGroups[selectedFlightGroupIndex].beam !=
	    0) {
		g_missionSetupBeamOptionCount = optionCount + 1;
	} else {
		g_missionSetupBeamOptionCount = optionCount;
		if (optionCount != 0) {
			g_missionSetupBeamOptionCount = optionCount + 1;
		}
	}

	optionCount = 0;
	for (optionIndex = 0; optionIndex < 4; ++optionIndex) {
		if (g_frontendMission.flightGroups[selectedFlightGroupIndex]
			    .optionalCountermeasures[optionIndex] != 0) {
			++optionCount;
		}
	}
	if (g_frontendMission.flightGroups[selectedFlightGroupIndex]
		    .countermeasures != 0) {
		g_missionSetupCountermeasureOptionCount = optionCount + 1;
	} else {
		g_missionSetupCountermeasureOptionCount = optionCount;
		if (optionCount != 0) {
			g_missionSetupCountermeasureOptionCount =
				optionCount + 1;
		}
	}

	if (g_pilotData.missionSequenceActive == 1 &&
	    (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
	     g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) &&
	    (unsigned int)g_pilotData.meleeTournamentSequenceState
			    .currentMissionIndex > 0) {
		counterpartCraftType = 0;
		craftType = MissionSetup_GetCraftType(-1);
		if (((craftType >= 1 && craftType <= 4) || craftType == 14)) {
			if (g_pilotData.currentFactionId == 1) {
				counterpartCraftType =
					g_craftIffCounterpart[craftType];
			}
		} else if (g_pilotData.currentFactionId == 0) {
			counterpartCraftType = g_craftIffCounterpart[craftType];
		}
		if (counterpartCraftType == 0) {
			return;
		}

		do {
			craftTypeMismatch = 0;
			if (counterpartCraftType !=
			    MissionSetup_GetCraftType(-1)) {
				craftTypeMismatch = 1;
			}
			if (craftTypeMismatch) {
				if (g_missionSetupPresetCraftOptionCount != 0) {
					++g_missionSetupSelectedPresetCraftOptionIndex;
					presetCraftOptionIndex =
						g_missionSetupSelectedPresetCraftOptionIndex;
					if (presetCraftOptionIndex >=
					    g_missionSetupPresetCraftOptionCount) {
						presetCraftOptionIndex = 0;
					}
					if (presetCraftOptionIndex != 0) {
						switch (g_frontendMission
								.flightGroups
									[g_missionSetupSelectedFlightGroupIndex]
								.optionalCraftCategory) {
						case 1:
						case 2:
							if (g_frontendMission
								    .flightGroups
									    [g_missionSetupSelectedFlightGroupIndex]
								    .craftType ==
							    g_presetCraftTypes
								    [presetCraftOptionIndex]) {
								++presetCraftOptionIndex;
							}
							break;
						case 3:
							if (g_frontendMission
								    .flightGroups
									    [g_missionSetupSelectedFlightGroupIndex]
								    .craftType ==
							    g_presetCraftTypes
								    [presetCraftOptionIndex +
								     5]) {
								++presetCraftOptionIndex;
							}
							break;
						default:
							break;
						}
					}
					g_missionSetupSelectedPresetCraftOptionIndex =
						presetCraftOptionIndex;
					if (presetCraftOptionIndex >=
					    g_missionSetupPresetCraftOptionCount) {
						g_missionSetupSelectedPresetCraftOptionIndex =
							0;
					}
				} else {
					++g_missionSetupSelectedFlightGroupCraftOptionIndex;
					flightGroupCraftOptionIndex =
						g_missionSetupSelectedFlightGroupCraftOptionIndex;
					if (flightGroupCraftOptionIndex >=
					    g_missionSetupFlightGroupCraftOptionCount) {
						flightGroupCraftOptionIndex = 0;
					}
					g_missionSetupSelectedFlightGroupCraftOptionIndex =
						flightGroupCraftOptionIndex;
					if (flightGroupCraftOptionIndex != 0) {
						g_missionSetupSelectedCraftCount =
							g_frontendMission
								.flightGroups
									[g_missionSetupSelectedFlightGroupIndex]
								.numberOfOptionalCraft
									[flightGroupCraftOptionIndex -
									 1];
						g_missionSetupSelectedWaveCountMinusOne =
							g_frontendMission
								.flightGroups
									[g_missionSetupSelectedFlightGroupIndex]
								.numberOfOptionalCraftWaves
									[flightGroupCraftOptionIndex -
									 1];
					} else {
						g_missionSetupSelectedCraftCount =
							g_frontendMission
								.flightGroups
									[g_missionSetupSelectedFlightGroupIndex]
								.numberOfCraft;
						g_missionSetupSelectedWaveCountMinusOne =
							g_frontendMission
								.flightGroups
									[g_missionSetupSelectedFlightGroupIndex]
								.numberOfWaves;
					}
				}
			}
		} while (craftTypeMismatch);
	}
}

/* Returns a player's warhead as the offset of its name from FRONTSTR_273_NONE
 * (0 for none), through g_warheadTypeMap. With playerRosterIndex -1 it is the
 * local choice: the selected flight group's default warhead when
 * g_missionSetupSelectedWarheadOptionIndex is 0 or below, else its optional
 * warhead at that index minus 1. For a g_mpRoster index it is that player's
 * assigned flight group, searched over every team's first
 * g_teamPlayerFlightGroupCount slots (a later match replacing an earlier one),
 * and the entry's warheadOptionIndex the same way. Does not check that the
 * player has a slot: the flight group index is then unset. */
// FUNCTION: XVT 0x4EE510
int MissionSetup_GetWarheadType(int playerRosterIndex)
{
	int teamIndex;
	int teamFlightGroupOffset;
	int teamPlayerIndex;
	int flightGroupIndex;
	int warheadOptionIndex;
	uint8_t warheadType;

	if (playerRosterIndex == -1) {
		warheadOptionIndex =
			g_missionSetupSelectedWarheadOptionIndex - 1;
		flightGroupIndex = g_missionSetupSelectedFlightGroupIndex;
		if (warheadOptionIndex < 0) {
			warheadType =
				g_frontendMission.flightGroups[flightGroupIndex]
					.warhead;
			if (warheadType == 0) {
				return 0;
			}
			return g_warheadTypeMap[warheadType];
		}
		warheadType = g_frontendMission.flightGroups[flightGroupIndex]
				      .optionalWarheads[warheadOptionIndex];
		return g_warheadTypeMap[warheadType];
	}

	teamFlightGroupOffset = 0;
	for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
		teamPlayerIndex = 0;
		while (teamPlayerIndex <
		       g_teamPlayerFlightGroupCount[teamIndex]) {
			if (g_missionSetupPlayerAssignments
				    .teamPlayerIds[teamIndex]
						  [teamPlayerIndex] ==
			    g_mpRoster[playerRosterIndex].playerId) {
				flightGroupIndex =
					g_missionSetupPlayerFlightGroupIndices
						[teamFlightGroupOffset +
						 teamPlayerIndex];
				break;
			}
			++teamPlayerIndex;
		}
		teamFlightGroupOffset += 8;
	}

	warheadOptionIndex = g_mpRoster[playerRosterIndex].warheadOptionIndex;
	if (warheadOptionIndex == 0) {
		warheadType = g_frontendMission.flightGroups[flightGroupIndex]
				      .warhead;
		if (warheadType == 0) {
			return 0;
		}
		return g_warheadTypeMap[warheadType];
	}
	warheadType = g_frontendMission.flightGroups[flightGroupIndex]
			      .optionalWarheads[warheadOptionIndex - 1];
	return g_warheadTypeMap[warheadType];
}

/* Returns a player's beam weapon as the mission's beam code, 0 for none: always
 * 0 for craft types 1 to 4 and 14, else the flight group's default beam when
 * the option index is 0, or its optional beam at the index minus 1. With
 * playerRosterIndex -1 it uses the local choice
 * (g_missionSetupSelectedBeamOptionIndex and the selected flight group); for a
 * g_mpRoster index, that entry's beamOptionIndex and the player's assigned
 * flight group, found as MissionSetup_GetWarheadType finds it, unset when the
 * player has no slot. */
// FUNCTION: XVT 0x4EE650
int MissionSetup_GetBeamType(int playerRosterIndex)
{
	int teamIndex;
	int teamFlightGroupOffset;
	int teamPlayerIndex;
	int flightGroupIndex;
	int beamOptionIndex;
	int craftType;
	uint8_t beamType;

	if (playerRosterIndex == -1) {
		craftType = MissionSetup_GetCraftType(-1);
		if ((craftType >= 1 && craftType <= 4) || craftType == 14) {
			return 0;
		} else {
			unsigned int selectedOptionIndex =
				g_missionSetupSelectedBeamOptionIndex;
			int selectedFlightGroupIndex =
				g_missionSetupSelectedFlightGroupIndex;
			if (selectedOptionIndex == 0) {
				beamType =
					g_frontendMission
						.flightGroups
							[selectedFlightGroupIndex]
						.beam;
				if (beamType == 0) {
					return 0;
				}
				return beamType;
			} else {
				return g_frontendMission
					.flightGroups[selectedFlightGroupIndex]
					.optionalBeams[selectedOptionIndex - 1];
			}
		}
	}

	teamFlightGroupOffset = 0;
	for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
		for (teamPlayerIndex = 0;
		     teamPlayerIndex < g_teamPlayerFlightGroupCount[teamIndex];
		     ++teamPlayerIndex) {
			if (g_missionSetupPlayerAssignments
				    .teamPlayerIds[teamIndex]
						  [teamPlayerIndex] ==
			    g_mpRoster[playerRosterIndex].playerId) {
				flightGroupIndex =
					g_missionSetupPlayerFlightGroupIndices
						[teamFlightGroupOffset +
						 teamPlayerIndex];
				break;
			}
		}
		teamFlightGroupOffset += 8;
	}

	craftType = MissionSetup_GetCraftType(playerRosterIndex);
	if ((craftType >= 1 && craftType <= 4) || craftType == 14) {
		return 0;
	} else {
		beamOptionIndex = g_mpRoster[playerRosterIndex].beamOptionIndex;
		if (beamOptionIndex == 0) {
			beamType =
				g_frontendMission.flightGroups[flightGroupIndex]
					.beam;
			if (beamType == 0) {
				return 0;
			}
			return beamType;
		} else {
			return g_frontendMission.flightGroups[flightGroupIndex]
				.optionalBeams[beamOptionIndex - 1];
		}
	}
}

/* Returns a player's countermeasure as the mission's countermeasure code, 0 for
 * none: the flight group's default when the option index is 0, else its
 * optional countermeasure at the index minus 1. With playerRosterIndex -1 it
 * uses the local choice (g_missionSetupSelectedCountermeasureOptionIndex and
 * the selected flight group); for a g_mpRoster index, that entry's
 * countermeasureOptionIndex and the player's assigned flight group, found as
 * MissionSetup_GetWarheadType finds it, unset when the player has no slot. */
// FUNCTION: XVT 0x4EE7C0
int MissionSetup_GetCountermeasureType(int playerRosterIndex)
{
	int selectedFlightGroupIndex;
	unsigned int selectedOptionIndex;
	uint8_t selectedType;
	int teamIndex;
	int teamFlightGroupOffset;
	int teamPlayerIndex;
	int flightGroupIndex;
	int countermeasureOptionIndex;
	uint8_t countermeasureType;

	if (playerRosterIndex == -1) {
		selectedOptionIndex =
			g_missionSetupSelectedCountermeasureOptionIndex;
		selectedFlightGroupIndex =
			g_missionSetupSelectedFlightGroupIndex;
		if (selectedOptionIndex == 0) {
			selectedType =
				g_frontendMission
					.flightGroups[selectedFlightGroupIndex]
					.countermeasures;
			if (selectedType == 0) {
				return 0;
			}
			return selectedType;
		}
		return g_frontendMission.flightGroups[selectedFlightGroupIndex]
			.optionalCountermeasures[selectedOptionIndex - 1];
	}

	teamFlightGroupOffset = 0;
	for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
		teamPlayerIndex = 0;
		while (teamPlayerIndex <
		       g_teamPlayerFlightGroupCount[teamIndex]) {
			if (g_missionSetupPlayerAssignments
				    .teamPlayerIds[teamIndex]
						  [teamPlayerIndex] ==
			    g_mpRoster[playerRosterIndex].playerId) {
				flightGroupIndex =
					g_missionSetupPlayerFlightGroupIndices
						[teamFlightGroupOffset +
						 teamPlayerIndex];
				break;
			}
			++teamPlayerIndex;
		}
		teamFlightGroupOffset += 8;
	}

	countermeasureOptionIndex =
		g_mpRoster[playerRosterIndex].countermeasureOptionIndex;
	if (countermeasureOptionIndex == 0) {
		countermeasureType =
			g_frontendMission.flightGroups[flightGroupIndex]
				.countermeasures;
		if (countermeasureType == 0) {
			return 0;
		}
		return countermeasureType;
	}
	return g_frontendMission.flightGroups[flightGroupIndex]
		.optionalCountermeasures[countermeasureOptionIndex - 1];
}

/* Returns the craft species a player flies. With playerRosterIndex -1, the
 * local choice: when the selected flight group offers presets, its own craft
 * for option 0, else g_presetCraftTypes at the option for categories 1 and 2
 * and at the option plus 5 for category 3, and the option index itself for any
 * other category; otherwise its own craft for flight group option 0, else its
 * optional craft at the option minus 1. For a g_mpRoster index: the entry's
 * craftTypeOverride when nonzero, else the assigned flight group's optional
 * craft at craftOptionIndex itself when that, read as unsigned, is under 10 (so
 * not for -1), else the group's own craft. The flight group is found as
 * MissionSetup_GetWarheadType finds it, unset when the player has no slot. */
// FUNCTION: XVT 0x4EE8E0
int MissionSetup_GetCraftType(int playerRosterIndex)
{
	int flightGroupIndex;
	int result;
	int optionalCraftCategory;
	int teamIndex;
	int teamFlightGroupOffset;
	int teamPlayerIndex;
	unsigned int craftOptionIndex;

	if (playerRosterIndex == -1) {
		if (g_missionSetupPresetCraftOptionCount != 0) {
			/* result holds the selected preset craft option here, an index into g_presetCraftTypes. */
			result = g_missionSetupSelectedPresetCraftOptionIndex;
			if (result == 0) {
				return g_frontendMission
					.flightGroups
						[g_missionSetupSelectedFlightGroupIndex]
					.craftType;
			}
			optionalCraftCategory =
				g_frontendMission
					.flightGroups
						[g_missionSetupSelectedFlightGroupIndex]
					.optionalCraftCategory;
			switch (optionalCraftCategory) {
			case 1:
			case 2:
				return g_presetCraftTypes[result];
			case 3:
				return g_presetCraftTypes[result + 5];
			}
			return result;
		}
		if (g_missionSetupSelectedFlightGroupCraftOptionIndex == 0) {
			return g_frontendMission
				.flightGroups
					[g_missionSetupSelectedFlightGroupIndex]
				.craftType;
		}
		return g_frontendMission
			.flightGroups[g_missionSetupSelectedFlightGroupIndex]
			.optionalCraft
				[g_missionSetupSelectedFlightGroupCraftOptionIndex -
				 1];
	}

	teamFlightGroupOffset = 0;
	for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
		teamPlayerIndex = 0;
		while (teamPlayerIndex <
		       g_teamPlayerFlightGroupCount[teamIndex]) {
			if (g_missionSetupPlayerAssignments
				    .teamPlayerIds[teamIndex]
						  [teamPlayerIndex] ==
			    g_mpRoster[playerRosterIndex].playerId) {
				flightGroupIndex =
					g_missionSetupPlayerFlightGroupIndices
						[teamFlightGroupOffset +
						 teamPlayerIndex];
				break;
			}
			++teamPlayerIndex;
		}
		teamFlightGroupOffset += 8;
	}

	/* result now holds the roster entry's craft type override, a craft species. */
	result = g_mpRoster[playerRosterIndex].craftTypeOverride;
	if (result != 0) {
		return result;
	}

	craftOptionIndex = g_mpRoster[playerRosterIndex].craftOptionIndex;
	if (craftOptionIndex != UINT32_MAX && craftOptionIndex < 10) {
		return g_frontendMission.flightGroups[flightGroupIndex]
			.optionalCraft[craftOptionIndex];
	}
	return g_frontendMission.flightGroups[flightGroupIndex].craftType;
}

/* Loads the ship list that the tech library and the briefing's craft screen
 * draw models from, once: it does nothing while g_shipList is set. Allocates
 * 100 entries in g_shipList, then reads frontres\frntspec.lst, a file name and
 * a craft type per line, keeping only names whose last three characters,
 * lowercased, are "opt"; sets g_shipTypeToShipListIndex[type] to the entry's
 * index for types under 17 and g_shipCount to the count kept. When the file
 * does not open it returns with g_shipList allocated and g_shipCount unchanged.
 * Does not check for more than 100 entries, or for names under 3 characters. */
// FUNCTION: XVT 0x4EEA80
void ShipList_Load(void)
{
	int shipCount;
	int shipListIndex;
	int craftType;
	unsigned int extensionIndex;
	XvtFile *stream;

	if (g_shipList != NULL) {
		return;
	}
	g_shipList = (ShipListEntry *)malloc(sizeof(*g_shipList) * 100);
	if (g_shipList == NULL) {
		return;
	}

	/* shipListIndex and shipCount always hold the same value: both start at 0 here and step together for
	 * each .opt entry kept from the list. */
	shipListIndex = 0;
	shipCount = 0;
	stream = File_Open("frontres\\frntspec.lst", "r");
	if (stream == NULL) {
		return;
	}

	for (;;) {
		if (File_Scanf(stream, "%s %d\n", g_frontendScratchBuffer,
			       &craftType) != 2) {
			break;
		}
		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		for (extensionIndex = strlen(g_frontendScratchBuffer) - 3;
		     extensionIndex < strlen(g_frontendScratchBuffer);
		     ++extensionIndex) {
			g_frontendScratchBuffer[extensionIndex] = (char)tolower(
				(unsigned char)g_frontendScratchBuffer
					[extensionIndex]);
		}
		if (strcmp(&g_frontendScratchBuffer
				   [strlen(g_frontendScratchBuffer) - 3],
			   "opt") != 0) {
			continue;
		}
		strncpy(g_shipList[shipListIndex].modelFileName,
			g_frontendScratchBuffer,
			sizeof(g_shipList[shipListIndex].modelFileName) - 1);
		g_shipList[shipListIndex].modelFileName
			[sizeof(g_shipList[shipListIndex].modelFileName) - 1] =
			'\0';
		g_shipList[shipListIndex].craftType = craftType;
		if (craftType < 17) {
			g_shipTypeToShipListIndex[craftType] = shipCount;
		}
		++shipListIndex;
		++shipCount;
	}
	File_Close(stream);
	g_shipCount = shipCount;
}

/* Exit callback of the MissionSetup_EnterNextMission screen: frees
 * g_missionList and g_missionText, setting both to NULL, and the "background"
 * image. Returns 0. */
// FUNCTION: XVT 0x4F1190
int MissionSetup_ExitNextMission(void)
{
	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_missionText != NULL) {
		free(g_missionText);
		g_missionText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	return 0;
}

/* A one-frame screen the mission debriefing sets to go on to a tournament,
 * battle or campaign's next mission. Clears g_frontendSkipScreenEntrySetup,
 * puts savedMissionDescriptionId back as the played mission type's selected
 * mission, moves the mission type back to the sequence's own (training to
 * campaign, else up by one), loads its list and selects its entry, resets the
 * eight g_pilotData.networkPlayers results and choices, and calls
 * MissionSetup_SelectNextSequenceMission, ignoring its result. Then sends a
 * PILOT_RATING packet with the pilot's rating to every player and moves on. In
 * a solo game it sets g_missionSetupIsHost and refills g_mpRoster with the
 * pilot alone; outside one it sets g_missionSetupRosterAuthoritative and prunes
 * the departed players and the team assignments. A training sequence (a
 * campaign) goes to team assignment. A combat engagement sequence with the
 * player's choice setup (g_gameConfig.randomSetup 2) past its first mission
 * goes to MissionSetup_BattleChoice_Update when the previous mission's result
 * is not equal to g_pilotData.team, or, outside a solo game, when the ready
 * player count is not 1. Anything else goes to flight assignment. Ignores
 * frameCounter and returns 0. */
// FUNCTION: XVT 0x4F11E0
int MissionSetup_EnterNextMission(int frameCounter)
{
	enum { NETWORK_PLAYER_COUNT = 8 };

	int playerIndex;
	int *craftIdPtr;

	(void)frameCounter;
	g_frontendSkipScreenEntrySetup = 0;
	g_pilotData.missionDescriptionIds[g_pilotData.missionDirectoryId] =
		g_pilotData.savedMissionDescriptionId;
	if (g_pilotData.missionDirectoryId ==
	    MISSION_DIRECTORY_TRAINING_EXERCISES) {
		g_pilotData.missionDirectoryId = MISSION_DIRECTORY_CAMPAIGNS;
	} else {
		++g_pilotData.missionDirectoryId;
	}

	MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
	if (g_missionList != NULL) {
		g_selectedMissionListIndex = 0;
		while ((unsigned int)g_selectedMissionListIndex <
			       g_missionCount &&
		       g_missionList[g_selectedMissionListIndex].missionIdx !=
			       g_pilotData.missionDescriptionIds
				       [g_pilotData.missionDirectoryId]) {
			++g_selectedMissionListIndex;
		}
	}

	for (playerIndex = 0; playerIndex < NETWORK_PLAYER_COUNT;
	     ++playerIndex) {
		craftIdPtr = &g_pilotData.networkPlayers[playerIndex].craftId;
		*craftIdPtr = 0;
		g_pilotData.networkPlayers[playerIndex].craftOption = -1;
		g_pilotData.networkPlayers[playerIndex].warheadOption = -1;
		g_pilotData.networkPlayers[playerIndex].beamOption = -1;
		g_pilotData.networkPlayers[playerIndex].countermeasureOption =
			-1;
		g_pilotData.networkPlayers[playerIndex].totalScore = 0;
		g_pilotData.networkPlayers[playerIndex].kills = 0;
		g_pilotData.networkPlayers[playerIndex].killsShared = 0;
		g_pilotData.networkPlayers[playerIndex].craftInspected = 0;
		g_pilotData.networkPlayers[playerIndex].killsAssist = 0;
		g_pilotData.networkPlayers[playerIndex].totalLosses = 0;
		g_pilotData.networkPlayers[playerIndex].hasLeft = 0;
	}

	MissionSetup_SelectNextSequenceMission();
	*(int *)g_frontendNetPacketScratch.payload = g_pilotData.rating;
	g_frontendNetPacketScratch.packetType = NET_PACKET_PILOT_RATING;
	Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 2 * sizeof(int));
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_missionSetupIsHost = 1;
		memset(g_mpRoster, 0, sizeof(g_mpRoster));
		strcpy(g_mpRoster[0].name, g_pilotData.name);
		g_mpRoster[0].playerId = 1;
		g_mpRoster[0].pilotRating = g_pilotData.rating;
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilotData.missionSequenceActive == 1) {
			FrontendScreen_SetCallbacks(
				MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
				XvtFrontendCleanup_MissionResources
#else
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
			);
			return 0;
		}
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
		    g_pilotData.missionSequenceActive == 1 &&
		    g_gameConfig.randomSetup == 2 &&
		    g_pilotData.battleSequenceState.currentMissionIndex > 0 &&
		    (int)g_pilotData.battleSequenceState.missionResults
				    [g_pilotData.battleSequenceState
					     .currentMissionIndex -
				     1] != g_pilotData.team) {
			FrontendScreen_SetCallbacks(
				MissionSetup_BattleChoice_Update,

#ifdef XVT_MODERN
				XvtFrontendCleanup_BattleChoice
#else
				(FrontendScreenExitFn)
					MissionSetup_BattleChoice_Exit
#endif
			);
			return 0;
		}
		FrontendScreen_SetCallbacks(MissionSetup_FlightAssignmentUpdate,
					    MissionSetup_FreeScreenResources);
		return 0;
	}

	g_missionSetupRosterAuthoritative = 1;
	MissionSetup_PruneDisconnectedPlayers();
	MissionSetup_PruneTeamAssignments();
	if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES &&
	    g_pilotData.missionSequenceActive == 1) {
		FrontendScreen_SetCallbacks(MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
					    XvtFrontendCleanup_MissionResources
#else
					    (FrontendScreenExitFn)
						    FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
		);
		return 0;
	}
	if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilotData.missionSequenceActive == 1 &&
	    g_gameConfig.randomSetup == 2 &&
	    g_pilotData.battleSequenceState.currentMissionIndex > 0) {
		if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    Net_CountReadyPlayers() != 1) {
			FrontendScreen_SetCallbacks(
				MissionSetup_BattleChoice_Update,

#ifdef XVT_MODERN
				XvtFrontendCleanup_BattleChoice
#else
				(FrontendScreenExitFn)
					MissionSetup_BattleChoice_Exit
#endif
			);
			return 0;
		}
		if ((int)g_pilotData.battleSequenceState
			    .missionResults[g_pilotData.battleSequenceState
						    .currentMissionIndex -
					    1] != g_pilotData.team) {
			FrontendScreen_SetCallbacks(
				MissionSetup_BattleChoice_Update,

#ifdef XVT_MODERN
				XvtFrontendCleanup_BattleChoice
#else
				(FrontendScreenExitFn)
					MissionSetup_BattleChoice_Exit
#endif
			);
			return 0;
		}
	}
	FrontendScreen_SetCallbacks(MissionSetup_FlightAssignmentUpdate,
				    MissionSetup_FreeScreenResources);
	return 0;
}

/* Drops the players who left the lobby from the mission's assignments, using
 * the lobby roster from Net_GetPlayerRoster. Clears each
 * g_missionSetupPlayerAssignments.teamPlayerIds slot and each
 * g_pilotData.networkPlayers directPlayId whose player is not a ready roster
 * member; sets g_localPilotNetworkPlayerIndex to the networkPlayers entry
 * holding the local player's id, when one does; and for each assignedPlayerIds
 * slot holding 0, shifts the players of each of the first g_teamCount teams,
 * with their g_missionSetupPlayerFlightGroupIndices, down over the team's slots
 * holding 0, setting slot 7 to 0 and its flight group to -1. The shifting uses
 * a player count of 1 in a solo game, else Net_CountReadyPlayers(), and does
 * nothing when that count is 0. It clears an assignedPlayerIds slot that has no
 * ready roster member, with the g_mpRoster entry of the same index, only when
 * its search stops at index 8; with fewer than 8 players in the lobby roster no
 * slot is cleared. Returns 1. */
// FUNCTION: XVT 0x4F14A0
int MissionSetup_PruneDisconnectedPlayers(void)
{
	int playerCount;
	NetPlayerInfo *playerRoster;
	int teamIndex;
	int teamPlayerIndex;
	int rosterIndex;
	int activePlayerIndex;
	int pilotPlayerIndex;
	int emptyPlayerId;
	int shiftIndex;

	playerRoster = Net_GetPlayerRoster(&playerCount);
	for (teamIndex = 0; teamIndex < 10; teamIndex++) {
		for (teamPlayerIndex = 0; teamPlayerIndex < 8;
		     teamPlayerIndex++) {
			rosterIndex = 0;
			if (playerCount > 0) {
				do {
					if (playerRoster[rosterIndex]
							    .readyFlag != 0 &&
					    playerRoster[rosterIndex]
							    .playerId ==
						    (DPID)g_missionSetupPlayerAssignments
							    .teamPlayerIds
								    [teamIndex]
								    [teamPlayerIndex]) {
						break;
					}
					rosterIndex++;
				} while (playerCount > rosterIndex);
			}
			if (playerCount == rosterIndex) {
				g_missionSetupPlayerAssignments
					.teamPlayerIds[teamIndex]
						      [teamPlayerIndex] = 0;
			}
		}
	}

	for (activePlayerIndex = 0; activePlayerIndex < 8;
	     activePlayerIndex++) {
		rosterIndex = 0;
		if (playerCount > 0) {
			do {
				if (playerRoster[rosterIndex].readyFlag != 0 &&
				    playerRoster[rosterIndex].playerId ==
					    (DPID)g_missionSetupPlayerAssignments
						    .assignedPlayerIds
							    [activePlayerIndex]) {
					break;
				}
				rosterIndex++;
			} while (playerCount > rosterIndex);
		}
		if (rosterIndex == 8) {
			g_missionSetupPlayerAssignments
				.assignedPlayerIds[activePlayerIndex] = 0;
			g_mpRoster[activePlayerIndex].playerId = 0;
		}
	}

	for (pilotPlayerIndex = 0; pilotPlayerIndex < 8; pilotPlayerIndex++) {
		rosterIndex = 0;
		if (playerCount > 0) {
			do {
				if (playerRoster[rosterIndex].readyFlag != 0 &&
				    playerRoster[rosterIndex].playerId ==
					    (DPID)g_pilotData
						    .networkPlayers
							    [pilotPlayerIndex]
						    .directPlayId) {
					break;
				}
				rosterIndex++;
			} while (playerCount > rosterIndex);
		}
		if (playerCount == rosterIndex) {
			g_pilotData.networkPlayers[pilotPlayerIndex]
				.directPlayId = 0;
		}
	}

	for (pilotPlayerIndex = 0; pilotPlayerIndex < 8; pilotPlayerIndex++) {
		if (g_pilotData.networkPlayers[pilotPlayerIndex].directPlayId ==
		    Net_GetLocalPlayerId()) {
			g_localPilotNetworkPlayerIndex = pilotPlayerIndex;
			break;
		}
	}

	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		playerCount = 1;
	} else {
		playerCount = Net_CountReadyPlayers();
	}
	for (activePlayerIndex = 0; activePlayerIndex < 8;
	     activePlayerIndex++) {
		rosterIndex = 0;
		if (playerCount > 0) {
			do {
				if (g_missionSetupPlayerAssignments
					    .assignedPlayerIds
						    [activePlayerIndex] == 0) {
					break;
				}
				rosterIndex++;
			} while (playerCount > rosterIndex);
		}
		if (playerCount != rosterIndex && g_teamCount > 0) {
			/* The loop above stops early only when this assigned slot is empty, so the id taken here is
			 * always 0, and the pass below shifts each team's players down over its empty slots. */
			emptyPlayerId =
				g_missionSetupPlayerAssignments
					.assignedPlayerIds[activePlayerIndex];
			for (teamIndex = 0; teamIndex < g_teamCount;
			     teamIndex++) {
				for (teamPlayerIndex = 0; teamPlayerIndex < 8;
				     teamPlayerIndex++) {
					if (g_missionSetupPlayerAssignments
						    .teamPlayerIds
							    [teamIndex]
							    [teamPlayerIndex] ==
					    emptyPlayerId) {
						if (teamPlayerIndex < 7) {
							for (shiftIndex =
								     teamPlayerIndex;
							     shiftIndex < 7;
							     shiftIndex++) {
								g_missionSetupPlayerAssignments
									.teamPlayerIds
										[teamIndex]
										[shiftIndex] =
									g_missionSetupPlayerAssignments
										.teamPlayerIds
											[teamIndex]
											[shiftIndex +
											 1];
								g_missionSetupPlayerFlightGroupIndices
									[teamIndex *
										 8 +
									 shiftIndex] = g_missionSetupPlayerFlightGroupIndices
										[teamIndex *
											 8 +
										 shiftIndex +
										 1];
							}
						}
						g_missionSetupPlayerAssignments
							.teamPlayerIds
								[teamIndex][7] =
							0;
						g_missionSetupPlayerFlightGroupIndices
							[teamIndex * 8 + 7] =
								-1;
					}
				}
			}
		}
	}
	return 1;
}

/* Moves the g_mpRoster entries with a nonzero playerId to the front, keeping
 * their order, and zeroes the entries they leave. Returns 1. */
// FUNCTION: XVT 0x4F1680
int MpRoster_CompactActiveEntries(void)
{
	unsigned int emptyIndex;
	unsigned int activeIndex;

	for (emptyIndex = 0; emptyIndex < 8; ++emptyIndex) {
		if (g_mpRoster[emptyIndex].playerId == 0) {
			for (activeIndex = emptyIndex + 1; activeIndex < 8;
			     ++activeIndex) {
				if (g_mpRoster[activeIndex].playerId != 0) {
					g_mpRoster[emptyIndex] =
						g_mpRoster[activeIndex];
					memset(&g_mpRoster[activeIndex], 0,
					       sizeof(g_mpRoster[activeIndex]));
					break;
				}
			}
		}
	}

	return 1;
}

/* Moves an active tournament, battle or campaign on to its next mission; the
 * mission type must be the sequence's own, as MissionSetup_EnterNextMission
 * leaves it. Opens the sequence file of the g_missionList entry matching the
 * selected description id and picks the next mission's ordinal: a tournament's
 * meleeTournamentSequenceState.currentMissionIndex; a campaign's
 * campaignSequenceState.currentMissionIndex, first lowered by 1 when
 * lastMissionCompleted is 0, so the mission is flown again; for a battle, when
 * the previous mission was a draw, battleSequenceState.currentMissionIndex is
 * lowered by 1 and its stored ordinal reused; otherwise, with
 * g_gameConfig.randomSetup nonzero, a rand() % count ordinal not yet used in
 * this battle, rand being seeded with g_gameConfig.randomSeed; otherwise the
 * current index. A battle stores the ordinal in
 * missionOrdinals[currentMissionIndex]. The mission file is named on line
 * ordinal + 1 after the count line; an ordinal below 0 reads no line, so the
 * count line itself is taken as the name. Then, as
 * MissionSetup_SelectFirstSequenceMission does, it sets missionSequenceActive
 * to 1, moves to the played mission type, saves that type's selected mission in
 * savedMissionDescriptionId, loads its list, selects the named mission (for
 * a combat engagement also storing its id in
 * battleSequenceState.currentMissionId) and stores its list index in
 * battleSequenceState.missionListIndices[currentMissionIndex] whatever the
 * type; last it loads the mission and its team counts. Returns 1, or 0 when the
 * file does not open or is empty, or the mission's line is empty. Does not
 * check that a random draw can find an unused ordinal (it then never ends), or
 * that a battle's currentMissionIndex is above 0 before it reads the previous
 * result. */
// FUNCTION: XVT 0x4F1700
int MissionSetup_SelectNextSequenceMission(void)
{
	enum {
		SEQUENCE_DESCRIPTOR_PATH_CAPACITY = 128,
		SEQUENCE_DESCRIPTOR_LINE_CAPACITY = 255,
	};

	unsigned int descriptorMissionIndex;
	unsigned int randomSeed;
	unsigned int missionCount;
	int currentMissionOrdinal;
	int duplicateMission;
	int previousMissionIndex;
	int linesToRead;
	int characterIndex;
	int missionListIndex;
	char descriptorPath[SEQUENCE_DESCRIPTOR_PATH_CAPACITY];
	XvtFile *stream;

	descriptorMissionIndex = 0;
	randomSeed = g_gameConfig.randomSeed;
	while (descriptorMissionIndex < g_missionCount &&
	       g_missionList[descriptorMissionIndex].missionIdx !=
		       g_pilotData.missionDescriptionIds
			       [g_pilotData.missionDirectoryId]) {
		++descriptorMissionIndex;
	}
	sprintf(descriptorPath, "%s\\%s",
		g_missionDirectoryNames[g_pilotData.missionDirectoryId],
		g_missionList[descriptorMissionIndex].fileName);
	stream = File_Open(descriptorPath, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(g_frontendScratchBuffer,
		      SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream) == NULL) {
		File_Close(stream);
		return 0;
	}

	missionCount = atoi(g_frontendScratchBuffer);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		currentMissionOrdinal = g_pilotData.meleeTournamentSequenceState
						.currentMissionIndex;
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		if (g_pilotData.campaignSequenceState.lastMissionCompleted ==
		    0) {
			--g_pilotData.campaignSequenceState.currentMissionIndex;
		}
		currentMissionOrdinal =
			g_pilotData.campaignSequenceState.currentMissionIndex;
	} else {
		if (g_pilotData.battleSequenceState
			    .missionResults[g_pilotData.battleSequenceState
						    .currentMissionIndex -
					    1] == BATTLE_MISSION_RESULT_DRAW) {
			--g_pilotData.battleSequenceState.currentMissionIndex;
			currentMissionOrdinal =
				g_pilotData.battleSequenceState.missionOrdinals
					[g_pilotData.battleSequenceState
						 .currentMissionIndex];
		} else if (g_gameConfig.randomSetup != 0) {
			srand(randomSeed);
			do {
				duplicateMission = 0;
				currentMissionOrdinal = rand() % missionCount;
				for (previousMissionIndex =
					     (int)g_pilotData
						     .battleSequenceState
						     .currentMissionIndex -
					     1;
				     previousMissionIndex >= 0;
				     --previousMissionIndex) {
					if (g_pilotData.battleSequenceState
						    .missionOrdinals
							    [previousMissionIndex] ==
					    currentMissionOrdinal) {
						duplicateMission = 1;
						break;
					}
				}
			} while (duplicateMission != 0);
		} else {
			currentMissionOrdinal =
				(int)g_pilotData.battleSequenceState
					.currentMissionIndex;
		}
		g_pilotData.battleSequenceState.missionOrdinals
			[g_pilotData.battleSequenceState.currentMissionIndex] =
			currentMissionOrdinal;
	}

	if (currentMissionOrdinal >= 0) {
		linesToRead = currentMissionOrdinal + 1;
		do {
			File_Gets(g_frontendScratchBuffer,
				  SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream);
			if (g_frontendScratchBuffer
				    [strlen(g_frontendScratchBuffer) - 1] ==
			    '\n') {
				g_frontendScratchBuffer
					[strlen(g_frontendScratchBuffer) - 1] =
						'\0';
			}
			--linesToRead;
		} while (linesToRead != 0);
	}
	File_Close(stream);
	if (g_frontendScratchBuffer[0] == '\0') {
		return 0;
	}

	g_pilotData.missionSequenceActive = 1;
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS) {
		g_pilotData.missionDirectoryId =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
	} else {
		--g_pilotData.missionDirectoryId;
	}
	g_pilotData.savedMissionDescriptionId =
		g_pilotData
			.missionDescriptionIds[g_pilotData.missionDirectoryId];
	/* descriptorPath is reused here for the chosen mission's file name, lowercased to match the mission
	 * list. */
	strcpy(descriptorPath, g_frontendScratchBuffer);
	for (characterIndex = 0; characterIndex < (int)strlen(descriptorPath);
	     ++characterIndex) {
		descriptorPath[characterIndex] = (char)tolower(
			(unsigned char)descriptorPath[characterIndex]);
	}

	MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
	missionListIndex = 0;
	while (missionListIndex < (int)g_missionCount) {
		if (strcmp(descriptorPath,
			   g_missionList[missionListIndex].fileName) == 0) {
			g_pilotData.missionDescriptionIds
				[g_pilotData.missionDirectoryId] =
				g_missionList[missionListIndex].missionIdx;
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				g_pilotData.battleSequenceState
					.currentMissionId =
					g_missionList[missionListIndex]
						.missionIdx;
			}
			break;
		}
		++missionListIndex;
	}

	if (g_missionList != NULL) {
		g_selectedMissionListIndex = 0;
		while ((unsigned int)g_selectedMissionListIndex <
		       g_missionCount) {
			if (g_missionList[g_selectedMissionListIndex]
				    .missionIdx ==
			    g_pilotData.missionDescriptionIds
				    [g_pilotData.missionDirectoryId]) {
				g_pilotData.battleSequenceState
					.missionListIndices
						[g_pilotData.battleSequenceState
							 .currentMissionIndex] =
					g_selectedMissionListIndex;
				break;
			}
			++g_selectedMissionListIndex;
		}
	}
	FrontendMission_LoadCurrent();
	MissionSetup_UpdateTeamCounts();
	return 1;
}

/* Exit callback of the MissionSetup_EnterCurrentMission screen: frees
 * g_missionList and g_missionText, setting both to NULL, and the "background"
 * image. Returns 0. */
// FUNCTION: XVT 0x4F1AB0
int MissionSetup_ExitCurrentMission(void)
{
	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_missionText != NULL) {
		free(g_missionText);
		g_missionText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	return 0;
}

/* A one-frame screen the mission debriefing sets to fly the current mission
 * again. Clears g_frontendSkipScreenEntrySetup, resets the eight
 * g_pilotData.networkPlayers results and choices (craftId and the counts 0, the
 * four options -1, hasLeft 0), and sends a PILOT_RATING packet with the pilot's
 * rating to every player. In a solo game it sets g_missionSetupIsHost and
 * refills g_mpRoster with the pilot alone; outside one it sets
 * g_missionSetupRosterAuthoritative and prunes the departed players and the
 * team assignments. Then a training sequence (a campaign) goes to team
 * assignment and anything else to flight assignment. Ignores frameCounter and
 * returns 0. */
// FUNCTION: XVT 0x4F1B00
int MissionSetup_EnterCurrentMission(int frameCounter)
{
	PilotNetworkPlayer *player;
	int *craftIdPtr;

	(void)frameCounter;
	player = &g_pilotData.networkPlayers[0];
	craftIdPtr = &player->craftId;
	g_frontendSkipScreenEntrySetup = 0;
	do {
		*craftIdPtr = 0;
		craftIdPtr = (int *)((char *)craftIdPtr +
				     sizeof(PilotNetworkPlayer));
		player->craftOption = -1;
		player->warheadOption = -1;
		player->beamOption = -1;
		player->countermeasureOption = -1;
		player->totalScore = 0;
		player->kills = 0;
		player->killsShared = 0;
		player->craftInspected = 0;
		player->killsAssist = 0;
		player->totalLosses = 0;
		player->hasLeft = 0;
		++player;
	} while (craftIdPtr < &g_pilotData.teams[2].unknown08);

	*(int *)g_frontendNetPacketScratch.payload = g_pilotData.rating;
	g_frontendNetPacketScratch.packetType = NET_PACKET_PILOT_RATING;
	Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch, 2 * sizeof(int));
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_missionSetupIsHost = 1;
		memset(g_mpRoster, 0, sizeof(g_mpRoster));
		strcpy(g_mpRoster[0].name, g_pilotData.name);
		g_mpRoster[0].playerId = 1;
		g_mpRoster[0].pilotRating = g_pilotData.rating;
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilotData.missionSequenceActive == 1) {
			FrontendScreen_SetCallbacks(
				MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
				XvtFrontendCleanup_MissionResources
#else
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
			);
			return 0;
		}
		FrontendScreen_SetCallbacks(MissionSetup_FlightAssignmentUpdate,
					    MissionSetup_FreeScreenResources);
		return 0;
	} else {
		g_missionSetupRosterAuthoritative = 1;
		MissionSetup_PruneDisconnectedPlayers();
		MissionSetup_PruneTeamAssignments();
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilotData.missionSequenceActive == 1) {
			FrontendScreen_SetCallbacks(
				MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
				XvtFrontendCleanup_MissionResources
#else
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
			);
			return 0;
		}
		FrontendScreen_SetCallbacks(MissionSetup_FlightAssignmentUpdate,
					    MissionSetup_FreeScreenResources);
		return 0;
	}
}

/* The team assignment screen, run once per frame: players are dragged into the
 * mission's team slots, and Next moves on to flight assignment. On frame 0 the
 * original build clears g_frontendChatTeamOnly,
 * g_missionSetupShowDescriptionPanel and g_frontendFirstVisibleLine; asks for
 * the game CD until it is found, Cancel returning 1 after, outside a solo game,
 * telling the host or the players and shutting the session down; and leaves for
 * the concourse when a client CD tries to host or fly solo. Then, entering a
 * campaign or combat engagement sequence (g_frontendSkipScreenEntrySetup 0, not
 * replaying the current mission), it continues a saved campaign or battle when
 * there is one (MissionSetup_TryContinueCampaign or
 * MissionSetup_TryContinueBattle), clearing the five g_remoteBattle globals
 * when it does not, and plays a campaign's cutscenes through
 * Cutscene_PlayForCurrentMissionPhase(0); a network player whose cutscene
 * result is 0 leaves for the join screen. The modern build does all this in
 * XvtCampaignTask_EnterTeams, which ends the program in place of the CD
 * dialogs, and returns 0 until that returns 1. A pending debriefing transition
 * then is cleared and goes straight to flight assignment. Otherwise it clears
 * the drag and the reservations, loads the mission, and either clears the
 * assignments and recounts the teams or, with g_frontendSkipScreenEntrySetup
 * set, keeps them and prunes them. One team with one ready player puts roster
 * entry 0 alone on team 0 and goes to flight assignment (back to
 * MissionSetup_Update instead when skipping the entry setup). A solo combat
 * engagement or battle puts the pilot on team currentFactionId ^ 1 and goes to
 * the battle choice or flight assignment; a solo game with more teams,
 * g_frontendSkipScreenEntrySetup and g_missionSetupTeamAssignmentSkipped set
 * goes back to MissionSetup_Update. In other cases the ready players are dealt
 * into the teams' free slots, one per team per round, in g_mpRoster order. A
 * melee whose teams have one slot each, a solo Quick Start, and a training
 * mission with one team of 8 slots also go straight to flight assignment. Every
 * one of these skips past the teams, the debriefing one included, sets
 * g_missionSetupTeamAssignmentSkipped. Else it draws the screen's base with a
 * captain and slot overlay per team slot and allocates and loads g_missionText.
 * Every frame it draws the mission name, title, help text, unassigned players,
 * the team slots or the mission description, the chat panel and the pilot
 * banner. Outside a solo game it handles one packet: a host cancel leaves for
 * the join screen; a lobby state compacts g_mpRoster and prunes the teams; the
 * final team assignments set g_pilotData.team and go to the battle choice or
 * flight assignment; a return to setup goes back to MissionSetup_Update; the
 * reservation packets update g_missionSetupReservedPlayerIds; and new or
 * cleared assignments clear the reservations and end any drag. Previous, for
 * the host or a solo player, returns a solo game to MissionSetup_Update and has
 * a host send every player RETURN_TO_SETUP; for a campaign it first shows a
 * confirm dialog whose answer neither build reads. A client's Leave asks, then
 * tells the host and returns to the join screen. When the teams are valid, Next
 * sets the solo pilot's team and goes to the battle choice or flight
 * assignment, or for a host sends the final team assignments. Last it handles
 * the team controls, draws a dragged name at the cursor and, when a click ends
 * the drag, outside a solo game sends a RELEASE_TEAM_RESERVATION. Returns 1
 * when Frontend_HandleCommonScreenControls returns 1 or the original build's CD
 * dialog is cancelled; else 0. Does not check that the teams have a free slot
 * for each ready player: the dealing then never ends. */
// FUNCTION: XVT 0x4F1CC0
int MissionSetup_TeamAssignmentUpdate(int frameCounter)
{
	enum {
		MAX_PLAYERS = 8,
		MAX_TEAMS = 10,
		BRIEFING_TEXT_CAPACITY = 4096,
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
		DRAG_INPUT_GATE = 2,
	};

	int readyPlayerCount;
	int assignedPlayerCount;
	int teamIndex;
	int slotIndex;
	int rosterIndex;
	int flightGroupIndex;
	int packetType;
#ifndef XVT_MODERN
	int cutsceneResult;
#endif
	int useBattleChoice;
	int textIndex;
	int animationFrame;
	int localPlayerId;
	int cursorX;
	int cursorY;
	RECT rect;
	RECT savedClipRect;

	if (frameCounter == 0) {
#ifdef XVT_MODERN
		if (XvtCampaignTask_EnterTeams() != 1) {
			return 0;
		}
#else
		g_frontendChatTeamOnly = 0;
		g_missionSetupShowDescriptionPanel = 0;
		g_frontendFirstVisibleLine = 0;
		while (File_CheckGameCdPresent(g_skipMovieChecks) == 0) {
			CDAudio_Initialize();
			if (FrontendDialog_ShowConfirmDialog(
				    FrontendString_Get(
					    FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
				    FrontendString_Get(
					    FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
				    FrontendString_Get(
					    FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
				    FrontendString_Get(FRONTSTR_523_OKAY),
				    FrontendString_Get(FRONTSTR_019_CANCEL)) ==
			    0) {
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (Net_IsHost() != 0) {
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_HOST_CANCELLED;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							sizeof(g_frontendNetPacketScratch
								       .packetType));
					} else {
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_PLAYER_LEFT;
						Net_SendPacketAndFlush(
							Net_GetHostPlayerId(),
							&g_frontendNetPacketScratch,
							sizeof(g_frontendNetPacketScratch
								       .packetType));
					}
					Net_ShutdownDirectPlaySessionForQuit();
				}
				return 1;
			}
		}
		Frontend_CheckHostCdPresent();
		if (g_hostCdAvailable == 0 &&
		    g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_NET_CLIENT) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_749_YOU_SWITCHED_TO_A_CLIENT_CD),
				FrontendString_Get(
					FRONTSTR_750_YOU_CANNOT_HOST_A_NETWORK_GAME),
				FrontendString_Get(
					FRONTSTR_751_OR_FLY_SOLO_WITH_THE_CLIENT_CD),
				NULL, NULL);
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_HOST_CANCELLED;
				Net_SendPacketAndFlush(
					0, &g_frontendNetPacketScratch,
					sizeof(g_frontendNetPacketScratch
						       .packetType));
				Net_ShutdownDirectPlaySession();
			}
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NONE;
			FrontendScreen_SetCallbacks(Concourse_Update,
						    Concourse_Exit);
			return 0;
		}

		if (g_frontendSkipScreenEntrySetup == 0 &&
		    g_missionSetupDebriefTransition !=
			    MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION &&
		    g_pilotData.missionSequenceActive == 1) {
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				if (MissionSetup_TryContinueCampaign() == 0) {
					g_remoteBattleContinuationActive = 0;
					g_remoteBattleSequenceContinuationChoice =
						0;
					g_remoteBattleLastCompletedMissionIndex =
						0;
					g_remoteBattleRebelVictoryCount = 0;
					g_remoteBattleImperialVictoryCount = 0;
				}
				cutsceneResult =
					Cutscene_PlayForCurrentMissionPhase(0);
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    cutsceneResult == 0) {
					g_frontendNetPacketScratch.packetType =
						NET_PACKET_PLAYER_LEFT;
					Net_SendPacketAndFlush(
						Net_GetHostPlayerId(),
						&g_frontendNetPacketScratch,
						sizeof(g_frontendNetPacketScratch
							       .packetType));
					Net_ShutdownDirectPlaySession();
					FrontendScreen_SetCallbacks(
						FrontendNet_JoinGameScreen,
						(FrontendScreenExitFn)
							FrontendMissionList_FreeScreenResources);
					return 0;
				}
			} else if (
				g_pilotData.missionDirectoryId ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				MissionSetup_TryContinueBattle() == 0) {
				g_remoteBattleContinuationActive = 0;
				g_remoteBattleSequenceContinuationChoice = 0;
				g_remoteBattleLastCompletedMissionIndex = 0;
				g_remoteBattleRebelVictoryCount = 0;
				g_remoteBattleImperialVictoryCount = 0;
			}
		}
#endif
		if (g_missionSetupDebriefTransition !=
		    MISSION_SETUP_DEBRIEF_TRANSITION_NONE) {
			g_missionSetupDebriefTransition =
				MISSION_SETUP_DEBRIEF_TRANSITION_NONE;
			g_missionSetupTeamAssignmentSkipped = 1;
			FrontendScreen_SetCallbacks(
				MissionSetup_FlightAssignmentUpdate,
				MissionSetup_FreeScreenResources);
			return 0;
		}

		FrontendCursor_SetPos(37, 445);
		g_missionSetupDraggedPlayerId = 0;
		g_missionSetupDebriefTransition =
			MISSION_SETUP_DEBRIEF_TRANSITION_NONE;
		g_missionSetupReservedPlayerCount = 0;
		memset(g_missionSetupReservedPlayerIds, 0,
		       sizeof(g_missionSetupReservedPlayerIds));
		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			for (g_selectedMissionListIndex = 0;
			     (unsigned int)g_selectedMissionListIndex <
			     g_missionCount;
			     ++g_selectedMissionListIndex) {
				if (g_missionList[g_selectedMissionListIndex]
					    .missionIdx ==
				    g_pilotData.missionDescriptionIds
					    [g_pilotData.missionDirectoryId]) {
					break;
				}
			}
		}
		FrontendMission_LoadCurrent();
		if (g_frontendSkipScreenEntrySetup == 0) {
			memset(g_missionSetupPlayerAssignments.teamPlayerIds, 0,
			       sizeof(g_missionSetupPlayerAssignments
					      .teamPlayerIds));
			memset(g_missionSetupPlayerAssignments
				       .assignedPlayerIds,
			       0,
			       sizeof(g_missionSetupPlayerAssignments
					      .assignedPlayerIds));
			MissionSetup_UpdateTeamCounts();
		} else {
			MissionSetup_PruneTeamAssignments();
		}

		if (g_teamCount == 1) {
			readyPlayerCount = 1;
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				readyPlayerCount = Net_CountReadyPlayers();
			}
			if (readyPlayerCount == 1) {
				if (g_frontendSkipScreenEntrySetup == 1) {
					FrontendScreen_SetCallbacks(
						MissionSetup_Update,
						(FrontendScreenExitFn)
							MissionSetup_Exit);
					g_frontendSkipScreenEntrySetup = 0;
					return 0;
				}
				g_missionSetupPlayerAssignments
					.teamPlayerIds[0][0] =
					g_mpRoster[0].playerId;
				g_missionSetupPlayerAssignments
					.assignedPlayerIds[0] =
					g_mpRoster[0].playerId;
				g_pilotData.team = 0;
				FrontendScreen_SetCallbacks(
					MissionSetup_FlightAssignmentUpdate,
					MissionSetup_FreeScreenResources);
				g_missionSetupTeamAssignmentSkipped = 1;
				g_frontendSkipScreenEntrySetup = 0;
				return 0;
			}
			if (g_frontendSkipScreenEntrySetup == 0 &&
			    g_teamCount != 0) {
				teamIndex = 0;
				assignedPlayerCount = 0;
				do {
					for (slotIndex = 0;
					     slotIndex <
					     g_teamPlayerFlightGroupCount
						     [teamIndex];
					     ++slotIndex) {
						if (g_missionSetupPlayerAssignments
							    .teamPlayerIds
								    [teamIndex]
								    [slotIndex] ==
						    0) {
							g_missionSetupPlayerAssignments
								.assignedPlayerIds
									[assignedPlayerCount] =
								g_mpRoster[assignedPlayerCount]
									.playerId;
							g_missionSetupPlayerAssignments
								.teamPlayerIds
									[teamIndex]
									[slotIndex] =
								g_mpRoster[assignedPlayerCount]
									.playerId;
							++assignedPlayerCount;
							break;
						}
					}
					if (++teamIndex >= g_teamCount) {
						teamIndex = 0;
					}
				} while (readyPlayerCount >
					 assignedPlayerCount);
			}
		} else {
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_frontendSkipScreenEntrySetup == 0) {
					if (g_pilotData.missionDirectoryId ==
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
					    g_pilotData.missionDirectoryId ==
						    MISSION_DIRECTORY_BATTLES) {
						teamIndex =
							g_pilotData
								.currentFactionId ^
							1;
						g_missionSetupPlayerAssignments
							.assignedPlayerIds[0] =
							g_mpRoster[0].playerId;
						g_pilotData.team = teamIndex;
						g_missionSetupPlayerAssignments
							.teamPlayerIds
								[teamIndex][0] =
							g_mpRoster[0].playerId;
						useBattleChoice = 0;
						if (g_pilotData.missionDirectoryId ==
							    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
						    g_pilotData.missionSequenceActive ==
							    1 &&
						    g_gameConfig.randomSetup ==
							    2 &&
						    g_pilotData.battleSequenceState
								    .currentMissionIndex >
							    0 &&
						    g_pilotData.battleSequenceState
								    .missionResults
									    [g_pilotData
										     .battleSequenceState
										     .currentMissionIndex -
									     1] !=
							    (BattleMissionResult)
								    teamIndex) {
							useBattleChoice = 1;
						}
						if (useBattleChoice != 0) {
							FrontendScreen_SetCallbacks(
								MissionSetup_BattleChoice_Update,

#ifdef XVT_MODERN
								XvtFrontendCleanup_BattleChoice
#else
								(FrontendScreenExitFn)
									MissionSetup_BattleChoice_Exit
#endif
							);
						} else {
							FrontendScreen_SetCallbacks(
								MissionSetup_FlightAssignmentUpdate,
								MissionSetup_FreeScreenResources);
						}
						g_missionSetupTeamAssignmentSkipped =
							1;
						g_frontendSkipScreenEntrySetup =
							0;
						return 0;
					}
				} else if (
					g_missionSetupTeamAssignmentSkipped !=
					0) {
					FrontendScreen_SetCallbacks(
						MissionSetup_Update,
						(FrontendScreenExitFn)
							MissionSetup_Exit);
					g_frontendSkipScreenEntrySetup = 0;
					return 0;
				}
			}
			if (g_frontendSkipScreenEntrySetup == 0) {
				readyPlayerCount = 1;
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					readyPlayerCount =
						Net_CountReadyPlayers();
				}
				if (g_teamCount != 0) {
					teamIndex = 0;
					assignedPlayerCount = 0;
					do {
						for (slotIndex = 0;
						     slotIndex <
						     g_teamPlayerFlightGroupCount
							     [teamIndex];
						     ++slotIndex) {
							if (g_missionSetupPlayerAssignments
								    .teamPlayerIds
									    [teamIndex]
									    [slotIndex] ==
							    0) {
								g_missionSetupPlayerAssignments
									.assignedPlayerIds
										[assignedPlayerCount] =
									g_mpRoster[assignedPlayerCount]
										.playerId;
								g_missionSetupPlayerAssignments
									.teamPlayerIds
										[teamIndex]
										[slotIndex] =
									g_mpRoster[assignedPlayerCount]
										.playerId;
								++assignedPlayerCount;
								break;
							}
						}
						if (++teamIndex >=
						    g_teamCount) {
							teamIndex = 0;
						}
					} while (readyPlayerCount >
						 assignedPlayerCount);
				}
			}
		}

		g_frontendSkipScreenEntrySetup = 0;
		g_missionSetupTeamAssignmentSkipped = 0;
		if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (teamIndex = 0; teamIndex < g_teamCount;
			     ++teamIndex) {
				if (g_missionSetupPlayerAssignments
					    .teamPlayerIds[teamIndex][0] == 1) {
					g_pilotData.team = teamIndex;
					break;
				}
			}
		}
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_MELEES) {
			for (teamIndex = 0; teamIndex < g_teamCount;
			     ++teamIndex) {
				if (g_teamPlayerFlightGroupCount[teamIndex] >
				    1) {
					break;
				}
			}
			if (teamIndex == g_teamCount) {
				g_pilotData.team = 0;
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					for (teamIndex = 0;
					     teamIndex < g_teamCount;
					     ++teamIndex) {
						for (slotIndex = 0;
						     slotIndex <
						     g_teamPlayerFlightGroupCount
							     [teamIndex];
						     ++slotIndex) {
							if (Net_GetLocalPlayerId() ==
							    g_missionSetupPlayerAssignments
								    .teamPlayerIds
									    [teamIndex]
									    [slotIndex]) {
								g_pilotData
									.team =
									teamIndex;
							}
						}
					}
				}
				g_frontendSkipScreenEntrySetup = 0;
				FrontendScreen_SetCallbacks(
					MissionSetup_FlightAssignmentUpdate,
					MissionSetup_FreeScreenResources);
				g_missionSetupTeamAssignmentSkipped = 1;
				return 0;
			}
		}
		if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    g_frontendQuickStartLaunchFlag == 1) {
			FrontendScreen_SetCallbacks(
				MissionSetup_FlightAssignmentUpdate,
				MissionSetup_FreeScreenResources);
			g_missionSetupTeamAssignmentSkipped = 1;
			g_frontendSkipScreenEntrySetup = 0;
			return 0;
		}
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_teamCount == 1 &&
		    g_teamPlayerFlightGroupCount[0] == MAX_PLAYERS) {
			for (teamIndex = 0; teamIndex < g_teamCount;
			     ++teamIndex) {
				for (slotIndex = 0; slotIndex < MAX_PLAYERS;
				     ++slotIndex) {
					if (Net_GetLocalPlayerId() ==
					    g_missionSetupPlayerAssignments
						    .teamPlayerIds[teamIndex]
								  [slotIndex]) {
						g_pilotData.team = teamIndex;
						break;
					}
				}
			}
			FrontendScreen_SetCallbacks(
				MissionSetup_FlightAssignmentUpdate,
				MissionSetup_FreeScreenResources);
			g_missionSetupTeamAssignmentSkipped = 1;
			g_frontendSkipScreenEntrySetup = 0;
			return 0;
		}

		FrontImage_RegisterResourceDefault("frontres\\soloteam.bmp",
						   "background");
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		if (g_hostCdAvailable != 0) {
			FrontImage_DrawSprite("allactive", 0, 0);
		} else {
			FrontImage_DrawSprite("clientactive", 0, 0);
		}
		if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
		}
		FrontImage_DrawSpriteTranslucent("teamoverlay", 0, 0);
		FrontendDraw_RectAssign(&rect, 88, 207, 256, 221);
		for (teamIndex = 0; teamIndex < g_teamCount; ++teamIndex) {
			/* MAX_PLAYERS stands for team counts here: with 7 or 8 teams, teams 4 and up move to a second
			 * column. */
			if ((g_teamCount == MAX_PLAYERS ||
			     g_teamCount == MAX_PLAYERS - 1) &&
			    teamIndex == 4) {
				FrontendDraw_RectAssign(&rect, 260, 207, 428,
							221);
			}
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
			for (slotIndex = 0;
			     slotIndex <
			     g_teamPlayerFlightGroupCount[teamIndex];
			     ++slotIndex) {
				if (slotIndex == 0) {
					FrontImage_DrawSpriteTranslucent(
						"captoverlay", rect.left,
						rect.top);
				} else {
					FrontImage_DrawSpriteTranslucent(
						"slotoverlay", rect.left,
						rect.top);
				}
				FrontendDraw_RectOffsetXY(&rect, 0, 15);
			}
		}
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_StartTextFadeIn(20);
		g_missionText = malloc(BRIEFING_TEXT_CAPACITY);
		MissionSetup_LoadMissionDescText(g_missionText);
	}

	FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
	FrontendDisplay_GetScreenClipRect(&savedClipRect);
	FrontendDisplay_SetScreenClipRect640x480(&rect);
	sprintf(g_frontendScratchBuffer, "%c%s", 4,
		g_missionList[g_selectedMissionListIndex].description);
	for (textIndex = (int)strlen(g_frontendScratchBuffer) - 1;
	     textIndex > 0; --textIndex) {
		if (g_frontendScratchBuffer[textIndex] == '(') {
			g_frontendScratchBuffer[textIndex] = '\0';
			break;
		}
	}
	FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
	FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);
	FrontendDraw_RectAssign(&rect, 84, 90, 434, 108);
	if (g_pilotData.missionDirectoryId ==
	    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		FrontendText_DrawCentered(
			15, FrontendString_Get(FRONTSTR_466_CHOOSE_SIDES),
			&rect, 0xFFFF);
	} else {
		FrontendText_DrawCentered(
			15, FrontendString_Get(FRONTSTR_465_CHOOSE_TEAMS),
			&rect, 0xFFFF);
	}
	if (g_missionSetupShowDescriptionPanel == 0) {
		FrontendDraw_RectAssign(&rect, 84, 400, 434, 414);
		if (Net_IsHost() == 0) {
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				for (teamIndex = 0; teamIndex < g_teamCount;
				     ++teamIndex) {
					if (Net_GetLocalPlayerId() ==
					    g_missionSetupPlayerAssignments
						    .teamPlayerIds[teamIndex]
								  [0]) {
						FrontendText_DrawCentered(
							12,
							FrontendString_Get(
								FRONTSTR_741_YOU_ARE_A_TEAM_CAPTAIN),
							&rect, g_colorRed);
					}
				}
				FrontendDraw_RectOffsetXY(&rect, 0, 15);
				FrontendText_DrawCentered(
					12,
					FrontendString_Get(
						FRONTSTR_469_PLEASE_WAIT_WHILE_THE_HOST_PICKS_TEAMS),
					&rect, g_colorRed);
			} else {
				FrontendDraw_RectOffsetXY(&rect, 0, 15);
				FrontendText_DrawCentered(
					12,
					FrontendString_Get(
						FRONTSTR_467_ASSIGN_TEAMS_BY_DRAGGING_PLAYERS_NAMES_INTO_TEAM_SLOTS),
					&rect, g_colorGreen);
			}
		} else if (g_frontendMissionSessionMode !=
			   FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			FrontendText_DrawCentered(
				12,
				FrontendString_Get(
					FRONTSTR_468_YOU_ARE_THE_HOST),
				&rect, g_colorGreen);
		} else {
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
			FrontendText_DrawCentered(
				12,
				FrontendString_Get(
					FRONTSTR_467_ASSIGN_TEAMS_BY_DRAGGING_PLAYERS_NAMES_INTO_TEAM_SLOTS),
				&rect, g_colorGreen);
		}
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		packetType = FrontendNet_ProcessNetworkPackets();
		if (packetType == NET_PACKET_HOST_CANCELLED) {
			if (Net_IsHost() == 0) {
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					FrontendString_Get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					FrontendString_Get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(
					XvtMissionDialogs_Resume,
					XVT_MISSION_TEAM_CANCELLED);
#endif
			}
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			g_frontendSkipScreenEntrySetup = 1;
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResources);
			Net_ShutdownDirectPlaySession();
		} else if (packetType == NET_PACKET_STATE) {
			MpRoster_CompactActiveEntries();
			MissionSetup_PruneTeamAssignments();
		} else if (packetType == NET_PACKET_TEAM_ASSIGNMENTS_READY) {
			for (teamIndex = 0; teamIndex < g_teamCount;
			     ++teamIndex) {
				for (slotIndex = 0; slotIndex < MAX_PLAYERS;
				     ++slotIndex) {
					if (Net_GetLocalPlayerId() ==
					    g_missionSetupPlayerAssignments
						    .teamPlayerIds[teamIndex]
								  [slotIndex]) {
						g_pilotData.team = teamIndex;
						break;
					}
				}
			}
			if (g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
			    g_pilotData.missionSequenceActive == 1 &&
			    g_gameConfig.randomSetup == 2 &&
			    g_pilotData.battleSequenceState
					    .currentMissionIndex > 0) {
				if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    Net_CountReadyPlayers() != 1) {
					FrontendScreen_SetCallbacks(
						MissionSetup_BattleChoice_Update,

#ifdef XVT_MODERN
						XvtFrontendCleanup_BattleChoice
#else
						(FrontendScreenExitFn)
							MissionSetup_BattleChoice_Exit
#endif
					);
					return 0;
				}
				if (g_pilotData.battleSequenceState.missionResults
					    [g_pilotData.battleSequenceState
						     .currentMissionIndex -
					     1] !=
				    (BattleMissionResult)g_pilotData.team) {
					FrontendScreen_SetCallbacks(
						MissionSetup_BattleChoice_Update,

#ifdef XVT_MODERN
						XvtFrontendCleanup_BattleChoice
#else
						(FrontendScreenExitFn)
							MissionSetup_BattleChoice_Exit
#endif
					);
					return 0;
				}
			}
			FrontendScreen_SetCallbacks(
				MissionSetup_FlightAssignmentUpdate,
				MissionSetup_FreeScreenResources);
			return 0;
		} else if (packetType == NET_PACKET_RETURN_TO_SETUP) {
			g_frontendSkipScreenEntrySetup = 1;
			FrontendScreen_SetCallbacks(
				MissionSetup_Update,
				(FrontendScreenExitFn)MissionSetup_Exit);
			return 0;
		} else if (packetType == NET_PACKET_RELEASE_TEAM_RESERVATION) {
			for (slotIndex = 0;
			     slotIndex < g_missionSetupReservedPlayerCount;
			     ++slotIndex) {
				if (g_missionSetupReservedPlayerIds
					    [slotIndex] ==
				    g_frontendNetPacketArg0) {
					--g_missionSetupReservedPlayerCount;
					break;
				}
			}
			for (; slotIndex < MAX_PLAYERS - 1; ++slotIndex) {
				g_missionSetupReservedPlayerIds[slotIndex] =
					g_missionSetupReservedPlayerIds
						[slotIndex + 1];
			}
			g_missionSetupReservedPlayerIds[MAX_PLAYERS - 1] = 0;
		} else if (packetType == NET_PACKET_TEAM_RESERVATION) {
			for (slotIndex = 0;
			     slotIndex < g_missionSetupReservedPlayerCount;
			     ++slotIndex) {
				if (g_missionSetupReservedPlayerIds
					    [slotIndex] ==
				    g_frontendNetPacketArg0) {
					break;
				}
			}
			if (slotIndex == g_missionSetupReservedPlayerCount) {
				g_missionSetupReservedPlayerIds
					[g_missionSetupReservedPlayerCount++] =
						g_frontendNetPacketArg0;
			}
		} else if (packetType == NET_PACKET_TEAM_ASSIGNMENTS ||
			   packetType == NET_PACKET_CLEAR_TEAM_ASSIGNMENTS) {
			g_missionSetupReservedPlayerCount = 0;
			memset(g_missionSetupReservedPlayerIds, 0,
			       sizeof(g_missionSetupReservedPlayerIds));
			g_missionSetupDraggedPlayerId = 0;
			if (FrontendMouse_IsGateOwner(DRAG_INPUT_GATE)) {
				FrontendMouse_ClearInputGate();
			}
		}
	}

	MissionSetup_DrawUnassignedPlayers(frameCounter);
	if (g_missionSetupShowDescriptionPanel == 0) {
		MissionSetup_DrawTeamAssignments(frameCounter);
	} else {
		MissionSetup_DrawTeamMissionDescription();
	}
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendNet_UpdateAndDrawChatPanel(frameCounter);
	}
	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
			g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect,
					  g_colorYellow);
		if (g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_MELEES &&
		    g_pilotData.missionDirectoryId !=
			    MISSION_DIRECTORY_TOURNAMENTS) {
			if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				for (flightGroupIndex = 0;
				     flightGroupIndex <
				     (int16_t)
					     g_frontendMission.flightGroupCount;
				     ++flightGroupIndex) {
					if (g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .playerNumber != 0) {
						if (g_frontendMission
							    .flightGroups
								    [flightGroupIndex]
							    .iff == 0) {
							animationFrame =
								(frameCounter %
								 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
								1;
							sprintf(g_frontendScratchBuffer,
								"rebtiny%d",
								animationFrame);
							FrontImage_DrawSprite(
								g_frontendScratchBuffer,
								204, 453);
							FrontImage_DrawSprite(
								g_frontendScratchBuffer,
								420, 453);
						} else if (
							g_frontendMission
								.flightGroups
									[flightGroupIndex]
								.iff == 1) {
							animationFrame =
								(frameCounter %
								 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
								1;
							sprintf(g_frontendScratchBuffer,
								"imptiny%d",
								animationFrame);
							FrontImage_DrawSprite(
								g_frontendScratchBuffer,
								204, 453);
							FrontImage_DrawSprite(
								g_frontendScratchBuffer,
								420, 453);
						} else {
							animationFrame =
								(frameCounter %
								 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
								1;
							sprintf(g_frontendScratchBuffer,
								"rebtiny%d",
								animationFrame);
							FrontImage_DrawSprite(
								g_frontendScratchBuffer,
								204, 453);
							sprintf(g_frontendScratchBuffer,
								"imptiny%d",
								animationFrame);
							FrontImage_DrawSprite(
								g_frontendScratchBuffer,
								420, 453);
						}
						break;
					}
				}
			} else {
				localPlayerId = Net_GetLocalPlayerId();
				for (teamIndex = 0; teamIndex < g_teamCount;
				     ++teamIndex) {
					for (slotIndex = 0;
					     slotIndex <
					     g_teamPlayerFlightGroupCount
						     [teamIndex];
					     ++slotIndex) {
						if (g_missionSetupPlayerAssignments
							    .teamPlayerIds
								    [teamIndex]
								    [slotIndex] ==
						    localPlayerId) {
							break;
						}
					}
					if (slotIndex <
					    g_teamPlayerFlightGroupCount
						    [teamIndex]) {
						break;
					}
				}
				if (teamIndex != g_teamCount) {
					if (teamIndex == 0) {
						animationFrame =
							(frameCounter %
							 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1;
						sprintf(g_frontendScratchBuffer,
							"imptiny%d",
							animationFrame);
					} else {
						animationFrame =
							(frameCounter %
							 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1;
						sprintf(g_frontendScratchBuffer,
							"rebtiny%d",
							animationFrame);
					}
					FrontImage_DrawSprite(
						g_frontendScratchBuffer, 204,
						453);
					FrontImage_DrawSprite(
						g_frontendScratchBuffer, 420,
						453);
				}
			}
		} else {
			animationFrame =
				(frameCounter %
				 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
				1;
			sprintf(g_frontendScratchBuffer, "rebtiny%d",
				animationFrame);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 204,
					      453);
			sprintf(g_frontendScratchBuffer, "imptiny%d",
				animationFrame);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 420,
					      453);
		}
	}

	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}
	if (Net_IsHost() != 0 ||
	    g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_569_PREVIOUS));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			if (g_pilotData.missionSequenceActive == 1 &&
			    g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(
						FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
					FrontendString_Get(
						FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
					FrontendString_Get(
						FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
					FrontendString_Get(FRONTSTR_523_OKAY),
					FrontendString_Get(
						FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(
					XvtMissionDialogs_Resume,
					XVT_MISSION_TEAM_PREVIOUS);
#endif
			}
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_RETURN_TO_SETUP;
				Net_SendPacketAndFlush(
					0, &g_frontendNetPacketScratch,
					sizeof(g_frontendNetPacketScratch
						       .packetType));
			}
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontendScreen_SetCallbacks(
					MissionSetup_Update,
					(FrontendScreenExitFn)
						MissionSetup_Exit);
			}
		}
	} else {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_569_PREVIOUS));

#ifdef XVT_MODERN
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				FrontendString_Get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				FrontendString_Get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				FrontendString_Get(FRONTSTR_523_OKAY),
				FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(
				XvtMissionDialogs_Resume,
				XVT_MISSION_TEAM_CLIENT_LEAVE);
		}
#else
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0 &&
		    FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    FrontendString_Get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    FrontendString_Get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_PLAYER_LEFT;
			Net_SendPacketAndFlush(
				Net_GetHostPlayerId(),
				&g_frontendNetPacketScratch,
				sizeof(g_frontendNetPacketScratch.packetType));
			Net_ShutdownDirectPlaySession();
			g_frontendSkipScreenEntrySetup = 1;
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResources);
		}
#endif
	}

	FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (MissionSetup_IsTeamAssignmentValid()) {
			FrontendButton_SetOverlayText(
				FrontendString_Get(FRONTSTR_212_NEXT));
			if (FrontendButton_HandleSpriteButton(
				    &rect, "nextup", "nextdown",
				    FrontendString_Get(
					    FRONTSTR_475_GO_TO_BRIEFING),
				    12, 0, 7, "flysound") != 0) {
				for (teamIndex = 0; teamIndex < g_teamCount;
				     ++teamIndex) {
					for (slotIndex = 0;
					     slotIndex < MAX_PLAYERS;
					     ++slotIndex) {
						if (g_missionSetupPlayerAssignments
							    .teamPlayerIds
								    [teamIndex]
								    [slotIndex] ==
						    1) {
							g_pilotData.team =
								teamIndex;
							break;
						}
					}
				}
				if (g_pilotData.missionDirectoryId ==
					    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				    g_pilotData.missionSequenceActive == 1 &&
				    g_gameConfig.randomSetup == 2 &&
				    g_pilotData.battleSequenceState
						    .currentMissionIndex > 0 &&
				    g_pilotData.battleSequenceState.missionResults
						    [g_pilotData
							     .battleSequenceState
							     .currentMissionIndex -
						     1] !=
					    (BattleMissionResult)
						    g_pilotData.team) {
					FrontendScreen_SetCallbacks(
						MissionSetup_BattleChoice_Update,

#ifdef XVT_MODERN
						XvtFrontendCleanup_BattleChoice
#else
						(FrontendScreenExitFn)
							MissionSetup_BattleChoice_Exit
#endif
					);
					return 0;
				}
				FrontendScreen_SetCallbacks(
					MissionSetup_FlightAssignmentUpdate,
					MissionSetup_FreeScreenResources);
				FrontendButton_DisableOverlayText();
				return 0;
			}
		}
	} else if (Net_IsHost() != 0 && MissionSetup_IsTeamAssignmentValid()) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_212_NEXT));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "nextup", "nextdown",
			    FrontendString_Get(FRONTSTR_475_GO_TO_BRIEFING), 12,
			    0, 7, "flysound") != 0) {
			MissionSetup_BroadcastTeamAssignments();
		}
	}
	FrontendButton_DisableOverlayText();
	MissionSetup_UpdateTeamControls();
	if (Frontend_HandleCommonScreenControls(1) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (XvtDialog_IsActive()) {
		return 0;
	}
#endif

	if (FrontendMouse_IsGateOwner(DRAG_INPUT_GATE)) {
		if (FrontendMouse_GetLeftClickFor(DRAG_INPUT_GATE) == 0 &&
		    FrontendMouse_GetRightClickFor(DRAG_INPUT_GATE) == 0) {
			/* readyPlayerCount holds the number of roster slots to search here (1, or all eight), not a
			 * count of ready players. */
			readyPlayerCount =
				g_frontendMissionSessionMode ==
						FRONTEND_MISSION_SESSION_SINGLEPLAYER
					? 1
					: MAX_PLAYERS;
			for (rosterIndex = 0; rosterIndex < readyPlayerCount;
			     ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId !=
				    g_missionSetupDraggedPlayerId) {
					continue;
				}
				FrontendCursor_GetPos(&cursorX, &cursorY);
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				if (Net_GetLocalPlayerId() ==
					    g_mpRoster[rosterIndex].playerId ||
				    g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontendText_Draw(
						12, g_frontendScratchBuffer,
						cursorX - 7, cursorY - 7,
						g_pulseColorRamp
							[((frameCounter % 24) &
							  ~1) >>
							 1]);
				} else {
					FrontendText_Draw(
						12, g_frontendScratchBuffer,
						cursorX - 7, cursorY - 7,
						g_colorYellow);
				}
				return 0;
			}
			return 0;
		}
		if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			*(int *)g_frontendNetPacketScratch.payload =
				g_missionSetupDraggedPlayerId;
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_RELEASE_TEAM_RESERVATION;
			Net_SendPacketAndFlush(
				0, &g_frontendNetPacketScratch,
				2 * sizeof(g_frontendNetPacketScratch
						   .packetType));
		}
		FrontendMouse_ClearInputGate();
		g_missionSetupDraggedPlayerId = 0;
	}
	return 0;
}

/* Draws the Unassigned Players list of the team assignment screen: each of the
 * first readyCount g_mpRoster entries (1 in a solo game, else
 * Net_CountReadyPlayers()) whose player is in no assignedPlayerIds slot and is
 * not being dragged, four to a column. A player in
 * g_missionSetupReservedPlayerIds is gray, the local player (every player in a
 * solo game) pulses, others are yellow. The host or a solo player starts a drag
 * by pressing a mouse button on a name while the drag gate (2) is free: a solo
 * game sets g_missionSetupDraggedPlayerId and takes the gate; a host does so
 * only for a player not reserved, also adding it to
 * g_missionSetupReservedPlayerIds, sending every player a TEAM_RESERVATION
 * (player and host ids) and a TEAM_ASSIGNMENT to team 10, and taking the player
 * out of assignedPlayerIds and out of every team, shifting the later slots
 * down. Returns 1. */
// FUNCTION: XVT 0x4F32B0
int MissionSetup_DrawUnassignedPlayers(int frameCounter)
{
	RECT rect;
	RECT previousClipRect;
	int readyPlayerCount;
	int displayedPlayerCount;
	int rosterIndex;
	int playerId;
	int reservedPlayerCount;
	int index;
	int teamSlotCount;
	int localPlayerId;
	int textColor;
	int sessionMode;
	int team;
	int slot;
	int shift;
	int cursorX;
	int cursorY;

	FrontendDraw_RectAssign(&rect, 88, 114, 430, 128);
	FrontendText_DrawAlignedInRect(
		15, FrontendString_Get(FRONTSTR_474_UNASSIGNED_PLAYERS), &rect,
		0, 1, 0xFFFF);
	readyPlayerCount = 1;
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		readyPlayerCount = Net_CountReadyPlayers();
	}
	displayedPlayerCount = 0;
	FrontendDraw_RectAssign(&rect, 88, 134, 258, 148);
	if (readyPlayerCount > 0) {
		reservedPlayerCount = g_missionSetupReservedPlayerCount;
		for (rosterIndex = 0; rosterIndex < readyPlayerCount;
		     ++rosterIndex) {
			playerId = g_mpRoster[rosterIndex].playerId;
			for (index = 0;
			     index <
			     (int)(sizeof(g_missionSetupPlayerAssignments
						  .assignedPlayerIds) /
				   sizeof(g_missionSetupPlayerAssignments
						  .assignedPlayerIds[0]));
			     ++index) {
				if (g_missionSetupPlayerAssignments
					    .assignedPlayerIds[index] ==
				    playerId) {
					break;
				}
			}
			if (index !=
				    (int)(sizeof(g_missionSetupPlayerAssignments
							 .assignedPlayerIds) /
					  sizeof(g_missionSetupPlayerAssignments
							 .assignedPlayerIds
								 [0])) ||
			    g_missionSetupDraggedPlayerId == playerId) {
				continue;
			}
			for (index = 0; index < reservedPlayerCount; ++index) {
				if (g_missionSetupReservedPlayerIds[index] ==
				    playerId) {
					break;
				}
			}
			FrontendDisplay_GetScreenClipRect(&previousClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rect);
			if (index == g_missionSetupReservedPlayerCount) {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				localPlayerId = Net_GetLocalPlayerId();
				textColor = g_colorYellow;
				sessionMode = g_frontendMissionSessionMode;
				if (playerId == localPlayerId ||
				    sessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontendText_DrawAlignedInRect(
						12, g_frontendScratchBuffer,
						&rect, 0, 1,
						g_pulseColorRamp
							[((frameCounter % 24) &
							  ~1) >>
							 1]);
				} else {
					FrontendText_DrawAlignedInRect(
						12, g_frontendScratchBuffer,
						&rect, 0, 1, textColor);
				}
			} else {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					g_mpRoster[rosterIndex].name);
				FrontendText_DrawAlignedInRect(
					12, g_frontendScratchBuffer, &rect, 0,
					1, g_colorGray);
			}
			++displayedPlayerCount;
			FrontendDisplay_SetScreenClipRect640x480(
				&previousClipRect);
			if ((Net_IsHost() != 0 ||
			     g_frontendMissionSessionMode ==
				     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
			    !FrontendMouse_IsGateOwner(2)) {
				FrontendCursor_GetPos(&cursorX, &cursorY);
				if ((FrontendMouse_GetLeftDown() != 0 ||
				     FrontendMouse_GetRightDown() != 0) &&
				    FrontendDraw_PointInRect(&rect, cursorX,
							     cursorY)) {
					if (g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_missionSetupDraggedPlayerId =
							playerId;
						FrontendMouse_SetInputGate(2);
					} else {
						for (index = 0;
						     index <
						     g_missionSetupReservedPlayerCount;
						     ++index) {
							if (g_missionSetupReservedPlayerIds
								    [index] ==
							    playerId) {
								break;
							}
						}
						if (index ==
						    g_missionSetupReservedPlayerCount) {
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_TEAM_RESERVATION;
							++g_missionSetupReservedPlayerCount;
							g_missionSetupReservedPlayerIds
								[index] =
									playerId;
							*(int *)g_frontendNetPacketScratch
								 .payload =
								playerId;
							*(int *)(g_frontendNetPacketScratch
									 .payload +
								 sizeof(int)) =
								Net_GetHostPlayerId();
							Net_SendPacketAndFlush(
								0,
								&g_frontendNetPacketScratch,
								3 * sizeof(int));
							g_missionSetupDraggedPlayerId =
								playerId;
							FrontendMouse_SetInputGate(
								2);
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_TEAM_ASSIGNMENT;
							*(int *)g_frontendNetPacketScratch
								 .payload =
								g_missionSetupDraggedPlayerId;
							*(int *)(g_frontendNetPacketScratch
									 .payload +
								 sizeof(int)) =
								10;
							Net_SendPacketAndFlush(
								0,
								&g_frontendNetPacketScratch,
								3 * sizeof(int));
							for (index = 0;
							     index <
							     (int)(sizeof(g_missionSetupPlayerAssignments
										  .assignedPlayerIds) /
								   sizeof(g_missionSetupPlayerAssignments
										  .assignedPlayerIds
											  [0]));
							     ++index) {
								if (g_missionSetupPlayerAssignments
									    .assignedPlayerIds
										    [index] ==
								    g_missionSetupDraggedPlayerId) {
									g_missionSetupPlayerAssignments
										.assignedPlayerIds
											[index] =
										0;
								}
							}
							for (team = 0;
							     team < g_teamCount;
							     ++team) {
								teamSlotCount = g_teamPlayerFlightGroupCount
									[team];
								for (slot = 0;
								     slot <
								     teamSlotCount;
								     ++slot) {
									if (g_missionSetupPlayerAssignments
										    .teamPlayerIds
											    [team]
											    [slot] ==
									    g_missionSetupDraggedPlayerId) {
										for (shift = slot +
											     1;
										     shift <
										     (int)(sizeof(g_missionSetupPlayerAssignments
													  .teamPlayerIds
														  [team]) /
											   sizeof(g_missionSetupPlayerAssignments
													  .teamPlayerIds
														  [team]
														  [0]));
										     ++shift) {
											g_missionSetupPlayerAssignments
												.teamPlayerIds
													[team]
													[shift -
													 1] =
												g_missionSetupPlayerAssignments
													.teamPlayerIds
														[team]
														[shift];
										}
										g_missionSetupPlayerAssignments
											.teamPlayerIds
												[team]
												[sizeof(g_missionSetupPlayerAssignments
														.teamPlayerIds
															[team]) /
													 sizeof(g_missionSetupPlayerAssignments
															.teamPlayerIds
																[team]
																[0]) -
												 1] =
											0;
									}
								}
							}
						}
					}
				}
			}
			if (displayedPlayerCount == 4) {
				FrontendDraw_RectAssign(&rect, 259, 134, 430,
							148);
			} else {
				FrontendDraw_RectOffsetXY(&rect, 0, 15);
			}
			reservedPlayerCount = g_missionSetupReservedPlayerCount;
		}
	}
	return 1;
}

/* Counts the player flight groups of the loaded mission (g_frontendMission) by
 * team into g_teamPlayerFlightGroupCount, a flight group counting when its
 * playerNumber is nonzero, and sets g_teamCount to the number of teams with at
 * least one. Does not check that a flight group's team is under 10. */
// FUNCTION: XVT 0x4F36D0
void MissionSetup_UpdateTeamCounts(void)
{
	int flightGroupIndex;
	int activeTeamCount;

	memset(g_teamPlayerFlightGroupCount, 0,
	       sizeof(g_teamPlayerFlightGroupCount));
	activeTeamCount = 0;
	for (flightGroupIndex = 0;
	     *(int16_t *)&g_frontendMission.flightGroupCount > flightGroupIndex;
	     ++flightGroupIndex) {
		if (g_frontendMission.flightGroups[flightGroupIndex]
			    .playerNumber != 0) {
			++g_teamPlayerFlightGroupCount
				[g_frontendMission
					 .flightGroups[flightGroupIndex]
					 .team];
		}
	}

	/* flightGroupIndex walks the ten teams here. */
	for (flightGroupIndex = 0; flightGroupIndex < 10; ++flightGroupIndex) {
		if (g_teamPlayerFlightGroupCount[flightGroupIndex] != 0) {
			++activeTeamCount;
		}
		g_teamCount = activeTeamCount;
	}
}

/* Draws the team slots of the team assignment screen and handles drops and
 * drags on them, in two columns when there are 7 or 8 teams. Each heading is
 * "Team n:" and the team's name, or for a combat engagement the name alone,
 * with the side's wins while a continued battle sequence is shown
 * (g_remoteBattleContinuationActive, g_remoteBattleSequenceContinuationChoice
 * and missionSequenceActive 1): g_remoteBattleImperialVictoryCount for team 0,
 * g_remoteBattleRebelVictoryCount for the others. A click on a slot while
 * dragging (gate 2) drops g_missionSetupDraggedPlayerId: a player already in
 * that slot loses its assignedPlayerIds slot (outside a solo game a
 * TEAM_ASSIGNMENT to team 10 is sent for it) and is replaced; on an empty slot
 * the player goes to the team's first empty slot up to it. The dropped player
 * gets an assignedPlayerIds slot when it has none; outside a solo game a
 * TEAM_ASSIGNMENT (player, team, slot) and a RELEASE_TEAM_RESERVATION go to
 * every player; the drag ends. Each filled slot whose player is among the first
 * readyCount g_mpRoster entries shows its rating and name, pulsing for the
 * local player and in a solo game, else yellow. The host or a solo player
 * pressing a mouse button on a filled slot starts a drag of its player: a host
 * only for a player not reserved, reserving it, sending a TEAM_RESERVATION and
 * a TEAM_ASSIGNMENT to team 10, and taking it out of assignedPlayerIds and its
 * team, shifting the later slots down; a solo game takes it out of its team
 * only when it holds an assignedPlayerIds slot. */
// FUNCTION: XVT 0x4F3740
void MissionSetup_DrawTeamAssignments(int frameCounter)
{
	RECT rect;
	RECT savedClip;
	int teamIndex;
	int slotIndex;
	int cursorX;
	int cursorY;
	int activeIndex;
	int shift;

	FrontendDraw_RectAssign(&rect, 90, 207, 256, 221);
	for (teamIndex = 0; teamIndex < g_teamCount; ++teamIndex) {
		if ((g_teamCount == 8 || g_teamCount == 7) && teamIndex == 4) {
			FrontendDraw_RectAssign(&rect, 262, 207, 428, 221);
		}
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			if (g_remoteBattleContinuationActive != 0 &&
			    g_remoteBattleSequenceContinuationChoice != 0 &&
			    g_pilotData.missionSequenceActive == 1) {
				if (teamIndex == 0) {
					sprintf(g_frontendScratchBuffer,
						"%s - %c%s: %d",
						g_frontendMission
							.teams[teamIndex]
							.name,
						4,
						FrontendString_Get(
							FRONTSTR_776_WINS),
						g_remoteBattleImperialVictoryCount);
				} else {
					sprintf(g_frontendScratchBuffer,
						"%s - %c%s: %d",
						g_frontendMission
							.teams[teamIndex]
							.name,
						4,
						FrontendString_Get(
							FRONTSTR_776_WINS),
						g_remoteBattleRebelVictoryCount);
				}
			} else {
				sprintf(g_frontendScratchBuffer, "%s",
					g_frontendMission.teams[teamIndex]
						.name);
			}
		} else {
			sprintf(g_frontendScratchBuffer, "%s %u: %s",
				FrontendString_Get(FRONTSTR_217_TEAM),
				teamIndex + 1,
				g_frontendMission.teams[teamIndex].name);
		}
		FrontendText_DrawAlignedInRect(12, g_frontendScratchBuffer,
					       &rect, 0, 1, 0xFFFF);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		for (slotIndex = 0;
		     slotIndex < g_teamPlayerFlightGroupCount[teamIndex];
		     ++slotIndex) {
			if (FrontendMouse_IsGateOwner(2) &&
			    (FrontendMouse_GetLeftClickFor(2) != 0 ||
			     FrontendMouse_GetRightClickFor(2) != 0)) {
				FrontendCursor_GetPos(&cursorX, &cursorY);
				if (FrontendDraw_PointInRect(&rect, cursorX,
							     cursorY)) {
					int replacedPlayerId =
						g_missionSetupPlayerAssignments
							.teamPlayerIds
								[teamIndex]
								[slotIndex];
					int targetSlot;

					if (replacedPlayerId != 0) {
						for (activeIndex = 0;
						     activeIndex < 8;
						     ++activeIndex) {
							if (g_missionSetupPlayerAssignments
								    .assignedPlayerIds
									    [activeIndex] ==
							    replacedPlayerId) {
								g_missionSetupPlayerAssignments
									.assignedPlayerIds
										[activeIndex] =
									0;
							}
						}
						if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_TEAM_ASSIGNMENT;
							*(int *)&g_frontendNetPacketScratch
								 .payload[0] =
								replacedPlayerId;
							*(int *)&g_frontendNetPacketScratch
								 .payload[4] =
								10;
							Net_SendPacketAndFlush(
								0,
								&g_frontendNetPacketScratch,
								12);
						}
					}
					if (replacedPlayerId == 0) {
						for (targetSlot = 0;
						     targetSlot < slotIndex;
						     ++targetSlot) {
							if (g_missionSetupPlayerAssignments
								    .teamPlayerIds
									    [teamIndex]
									    [targetSlot] ==
							    0) {
								break;
							}
						}
					} else {
						targetSlot = slotIndex;
					}
					g_missionSetupPlayerAssignments
						.teamPlayerIds[teamIndex]
							      [targetSlot] =
						g_missionSetupDraggedPlayerId;
					for (activeIndex = 0; activeIndex < 8;
					     ++activeIndex) {
						if (g_missionSetupPlayerAssignments
							    .assignedPlayerIds
								    [activeIndex] ==
						    g_missionSetupDraggedPlayerId) {
							break;
						}
					}
					if (activeIndex == 8) {
						for (activeIndex = 0;
						     activeIndex < 8;
						     ++activeIndex) {
							if (g_missionSetupPlayerAssignments
								    .assignedPlayerIds
									    [activeIndex] ==
							    0) {
								g_missionSetupPlayerAssignments
									.assignedPlayerIds
										[activeIndex] =
									g_missionSetupDraggedPlayerId;
								break;
							}
						}
					}
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"slotsound", 1, 0, 255,
							12 * g_gameConfig
									.sfxDatapadVolume,
							63);
					}
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_TEAM_ASSIGNMENT;
						*(int *)&g_frontendNetPacketScratch
							 .payload[0] =
							g_missionSetupDraggedPlayerId;
						*(int *)&g_frontendNetPacketScratch
							 .payload[4] =
							teamIndex;
						*(int *)&g_frontendNetPacketScratch
							 .payload[8] =
							targetSlot;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							16);
					}
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_RELEASE_TEAM_RESERVATION;
						*(int *)&g_frontendNetPacketScratch
							 .payload[0] =
							g_missionSetupDraggedPlayerId;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							8);
					}
					g_missionSetupDraggedPlayerId = 0;
					FrontendMouse_ClearInputGate();
				}
			}
			if (g_missionSetupPlayerAssignments
				    .teamPlayerIds[teamIndex][slotIndex] != 0) {
				int readyCount =
					g_frontendMissionSessionMode ==
							FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? 1
						: Net_CountReadyPlayers();
				int rosterIndex;
				for (rosterIndex = 0; rosterIndex < readyCount;
				     ++rosterIndex) {
					if (g_mpRoster[rosterIndex].playerId ==
					    g_missionSetupPlayerAssignments
						    .teamPlayerIds[teamIndex]
								  [slotIndex]) {
						break;
					}
				}
				if (rosterIndex < readyCount) {
					sprintf(g_frontendScratchBuffer,
						"%c%s %c%s", 6,
						FrontendString_Get(
							FRONTSTR_154_DRONE +
							g_mpRoster[rosterIndex]
								.pilotRating),
						1,
						g_mpRoster[rosterIndex].name);
					FrontendDisplay_GetScreenClipRect(
						&savedClip);
					FrontendDisplay_SetScreenClipRect640x480(
						&rect);
					if (Net_GetLocalPlayerId() ==
						    g_mpRoster[rosterIndex]
							    .playerId ||
					    g_frontendMissionSessionMode ==
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						FrontendText_DrawAlignedInRect(
							12,
							g_frontendScratchBuffer,
							&rect, 0, 1,
							g_pulseColorRamp
								[((frameCounter %
								   24) &
								  ~1) >>
								 1]);
					} else {
						FrontendText_DrawAlignedInRect(
							12,
							g_frontendScratchBuffer,
							&rect, 0, 1,
							g_colorYellow);
					}
					FrontendDisplay_SetScreenClipRect640x480(
						&savedClip);
					if (!FrontendMouse_IsGateOwner(2) &&
					    (Net_IsHost() != 0 ||
					     g_frontendMissionSessionMode ==
						     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
					    (FrontendMouse_GetLeftDown() != 0 ||
					     FrontendMouse_GetRightDown() !=
						     0)) {
						FrontendCursor_GetPos(&cursorX,
								      &cursorY);
						if (FrontendDraw_PointInRect(
							    &rect, cursorX,
							    cursorY)) {
							if (g_frontendMissionSessionMode !=
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								int reservedIndex;
								int removeTeam;
								int removeSlot;

								for (reservedIndex =
									     0;
								     reservedIndex <
								     g_missionSetupReservedPlayerCount;
								     ++reservedIndex) {
									if (g_missionSetupReservedPlayerIds
										    [reservedIndex] ==
									    g_mpRoster[rosterIndex]
										    .playerId) {
										break;
									}
								}
								if (reservedIndex ==
								    g_missionSetupReservedPlayerCount) {
									g_missionSetupReservedPlayerIds
										[reservedIndex] =
											g_mpRoster[rosterIndex]
												.playerId;
									++g_missionSetupReservedPlayerCount;
									g_frontendNetPacketScratch
										.packetType =
										NET_PACKET_TEAM_RESERVATION;
									*(int *)&g_frontendNetPacketScratch
										 .payload[0] =
										g_mpRoster[rosterIndex]
											.playerId;
									*(int *)&g_frontendNetPacketScratch
										 .payload[4] =
										Net_GetHostPlayerId();
									Net_SendPacketAndFlush(
										0,
										&g_frontendNetPacketScratch,
										12);
									g_missionSetupDraggedPlayerId =
										g_mpRoster[rosterIndex]
											.playerId;
									FrontendMouse_SetInputGate(
										2);
									g_frontendNetPacketScratch
										.packetType =
										NET_PACKET_TEAM_ASSIGNMENT;
									*(int *)&g_frontendNetPacketScratch
										 .payload[0] =
										g_missionSetupDraggedPlayerId;
									*(int *)&g_frontendNetPacketScratch
										 .payload[4] =
										10;
									Net_SendPacketAndFlush(
										0,
										&g_frontendNetPacketScratch,
										12);
									for (activeIndex =
										     0;
									     activeIndex <
									     8;
									     ++activeIndex) {
										if (g_missionSetupPlayerAssignments
											    .assignedPlayerIds
												    [activeIndex] ==
										    g_missionSetupDraggedPlayerId) {
											g_missionSetupPlayerAssignments
												.assignedPlayerIds
													[activeIndex] =
												0;
											break;
										}
									}
									for (removeTeam =
										     0;
									     removeTeam <
									     g_teamCount;
									     ++removeTeam) {
										for (removeSlot =
											     0;
										     removeSlot <
										     g_teamPlayerFlightGroupCount
											     [removeTeam];
										     ++removeSlot) {
											if (g_missionSetupPlayerAssignments
												    .teamPlayerIds
													    [removeTeam]
													    [removeSlot] ==
											    g_missionSetupDraggedPlayerId) {
												for (shift = removeSlot +
													     1;
												     shift <
												     8;
												     ++shift) {
													g_missionSetupPlayerAssignments
														.teamPlayerIds
															[removeTeam]
															[shift -
															 1] =
														g_missionSetupPlayerAssignments
															.teamPlayerIds
																[removeTeam]
																[shift];
												}
												g_missionSetupPlayerAssignments
													.teamPlayerIds
														[removeTeam]
														[7] =
													0;
												break;
											}
										}
									}
								}
							} else {
								g_missionSetupDraggedPlayerId =
									g_mpRoster[rosterIndex]
										.playerId;
								FrontendMouse_SetInputGate(
									2);
								for (activeIndex =
									     0;
								     activeIndex <
								     8;
								     ++activeIndex) {
									if (g_missionSetupPlayerAssignments
										    .assignedPlayerIds
											    [activeIndex] ==
									    g_missionSetupDraggedPlayerId) {
										g_missionSetupPlayerAssignments
											.assignedPlayerIds
												[activeIndex] =
											0;
										for (shift = slotIndex +
											     1;
										     shift <
										     8;
										     ++shift) {
											g_missionSetupPlayerAssignments
												.teamPlayerIds
													[teamIndex]
													[shift -
													 1] =
												g_missionSetupPlayerAssignments
													.teamPlayerIds
														[teamIndex]
														[shift];
										}
										g_missionSetupPlayerAssignments
											.teamPlayerIds
												[teamIndex]
												[7] =
											0;
										break;
									}
								}
							}
						}
					}
				}
			}
			FrontendDraw_RectOffsetXY(&rect, 0, 15);
		}
	}
}

/* Takes out of the team assignments every assignedPlayerIds entry that is not
 * the nonzero playerId of a g_mpRoster entry, a 0 entry included: the id is
 * removed from each slot of the first g_teamCount teams that holds it, the
 * later slots shifting down and slot 7 becoming 0, and the entry is set to 0.
 * g_missionSetupPlayerFlightGroupIndices is not shifted with them. Outside a
 * solo game it also calls Net_CountReadyPlayers and drops the result. */
// FUNCTION: XVT 0x4F3EA0
void MissionSetup_PruneTeamAssignments(void)
{
	int rosterPlayerValue;
	unsigned int rosterIndex;
	int *rosterPlayerIdPtr;
	unsigned int teamOffset;
	int *teamLastPlayerIdPtr;
	int removedPlayerId;
	int teamPlayerIndex;
	int *shiftDestination;
	int shiftCount;
	int shiftedPlayerId;
	int *activePlayerIdPtr;
	int activePlayerIndex;
	int teamsRemaining;

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		Net_CountReadyPlayers();
	}

	activePlayerIndex = 0;
	do {
		activePlayerIdPtr =
			&g_missionSetupPlayerAssignments
				 .assignedPlayerIds[activePlayerIndex];
		rosterIndex = 0;
		rosterPlayerIdPtr = &g_mpRoster[0].playerId;
		do {
			rosterPlayerValue = *rosterPlayerIdPtr;
			if (rosterPlayerValue != 0 &&
			    *activePlayerIdPtr == rosterPlayerValue) {
				break;
			}
			rosterPlayerIdPtr = (int *)((char *)rosterPlayerIdPtr +
						    sizeof(MpRosterEntry));
		} while (++rosterIndex < 8);

		if (rosterIndex == 8) {
			if (g_teamCount > 0) {
				removedPlayerId = *activePlayerIdPtr;
				teamLastPlayerIdPtr =
					&g_missionSetupPlayerAssignments
						 .teamPlayerIds[0][7];
				teamOffset = 0;
				teamsRemaining = g_teamCount;
				do {
					for (teamPlayerIndex = 0;
					     teamPlayerIndex < 8;
					     ++teamPlayerIndex) {
						if (removedPlayerId ==
						    ((int *)g_missionSetupPlayerAssignments
							     .teamPlayerIds)
							    [teamOffset +
							     teamPlayerIndex]) {
							if (teamPlayerIndex <
							    7) {
								shiftCount =
									7 -
									teamPlayerIndex;
								shiftDestination =
									(int *)((char *)&g_missionSetupPlayerAssignments +
										4 * teamOffset +
										4 * teamPlayerIndex);
								do {
									shiftedPlayerId = shiftDestination
										[1];
									*shiftDestination =
										shiftedPlayerId;
									++shiftDestination;
									--shiftCount;
								} while (
									shiftCount !=
									0);
							}
							*teamLastPlayerIdPtr =
								0;
						}
					}
					teamLastPlayerIdPtr += 8;
					teamOffset += 8;
					--teamsRemaining;
				} while (teamsRemaining != 0);
			}
			*activePlayerIdPtr = 0;
		}
		++activePlayerIndex;
	} while (activePlayerIndex < 8);
}

/* Tells whether the teams are ready for flight assignment. Returns 0 when one
 * of the first readyCount g_mpRoster entries (1 in a solo game, else
 * Net_CountReadyPlayers()) has a playerId of 0 or one in no assignedPlayerIds
 * slot, or when one of the first g_teamCount teams has a player in slots 1 to 7
 * but none in slot 0, its captain's slot; else 1. */
// FUNCTION: XVT 0x4F3F70
int MissionSetup_IsTeamAssignmentValid(void)
{
	int readyPlayerCount;
	int readyPlayerIndex;
	int assignmentIndex;
	int playerId;
	int teamIndex;
	int teamPlayerIndex;

	readyPlayerCount = 1;
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		readyPlayerCount = Net_CountReadyPlayers();
	}

	readyPlayerIndex = 0;
	if (readyPlayerCount > 0) {
		do {
			assignmentIndex = 0;
			playerId = g_mpRoster[readyPlayerIndex].playerId;
			do {
				if (playerId != 0 &&
				    g_missionSetupPlayerAssignments
						    .assignedPlayerIds
							    [assignmentIndex] ==
					    playerId) {
					break;
				}
				++assignmentIndex;
			} while (assignmentIndex < 8);
			if (assignmentIndex == 8) {
				return 0;
			}
			++readyPlayerIndex;
		} while (readyPlayerIndex < readyPlayerCount);
	}

	teamIndex = 0;
	if (g_teamCount > 0) {
		do {
			teamPlayerIndex = 1;
			do {
				if (g_missionSetupPlayerAssignments
						    .teamPlayerIds
							    [teamIndex]
							    [teamPlayerIndex] !=
					    0 &&
				    g_missionSetupPlayerAssignments
						    .teamPlayerIds[teamIndex]
								  [0] == 0) {
					return 0;
				}
				++teamPlayerIndex;
			} while (teamPlayerIndex < 8);
			++teamIndex;
		} while (teamIndex < g_teamCount);
	}

	return 1;
}

/* Draws the side buttons of the team assignment screen and handles them. For a
 * combat engagement sequence (missionSequenceActive 1) the Assign Teams and
 * Mission Description buttons set g_missionSetupShowDescriptionPanel to 0 or 1
 * and redraw the screen's base, with the team slot overlays when the teams come
 * back; Mission Description also resets g_frontendFirstVisibleLine. For the
 * host or a solo player while the teams show, a left or right click inside
 * Clear List calls MissionSetup_ClearTeamAssignments and inside Auto Assign
 * MissionSetup_RandomizeTeamAssignments. Returns 1. */
// FUNCTION: XVT 0x4F4010
int MissionSetup_UpdateTeamControls(void)
{
	FrontendNavigationSlotState slotStates[8];
	int hostControls = Net_IsHost() != 0 ||
			   g_frontendMissionSessionMode ==
				   FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	int cursorX;
	int cursorY;
	int slot;
	RECT rect;

	if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilotData.missionSequenceActive == 1) {
		slotStates[0] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[1] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[g_missionSetupShowDescriptionPanel] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else {
		slotStates[0] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slotStates[1] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	}
	slotStates[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[3] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[4] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	if (hostControls && g_missionSetupShowDescriptionPanel == 0) {
		slotStates[5] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[6] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	} else {
		slotStates[5] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slotStates[6] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	}
	slotStates[7] = FRONTEND_NAVIGATION_SLOT_INACTIVE;

	FrontendCursor_GetPos(&cursorX, &cursorY);
	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (hostControls) {
		FrontendDraw_RectAssign(&rect, 22, 306, 42, 330);
		for (slot = 5; slot < 7; ++slot) {
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
			    (FrontendMouse_GetLeftDown() != 0 ||
			     FrontendMouse_GetRightDown() != 0 ||
			     FrontendMouse_GetLeftClick() != 0 ||
			     FrontendMouse_GetRightClick() != 0)) {
				slotStates[slot] =
					FRONTEND_NAVIGATION_SLOT_SELECTED;
			}
			FrontendDraw_RectOffsetXY(&rect, 0, 28);
		}
	}
	FrontendButton_DrawEightSlotNavigationState(slotStates);

	if (g_missionSetupShowDescriptionPanel == 0 && hostControls) {
		FrontendDraw_RectAssign(&rect, 22, 334, 42, 358);
		FrontendButton_HandleSpriteButton(
			&rect, "clearu", "cleard",
			FrontendString_Get(FRONTSTR_215_CLEAR_LIST), 12, 0, 16,
			"jewelsound");
		if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
		    (FrontendMouse_GetLeftClick() != 0 ||
		     FrontendMouse_GetRightClick() != 0)) {
			MissionSetup_ClearTeamAssignments();
		}
		FrontendDraw_RectOffsetXY(&rect, 0, -28);
		FrontendButton_HandleSpriteButton(
			&rect, "assignu", "assignd",
			FrontendString_Get(FRONTSTR_214_AUTO_ASSIGN), 12, 0, 17,
			"jewelsound");
		if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
		    (FrontendMouse_GetLeftClick() != 0 ||
		     FrontendMouse_GetRightClick() != 0)) {
			MissionSetup_RandomizeTeamAssignments();
		}
	}

	FrontendDraw_RectAssign(&rect, 22, 142, 42, 166);
	if (slotStates[1] != FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_missionSetupShowDescriptionPanel == 1) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "team2d",
				FrontendString_Get(
					FRONTSTR_798_MISSION_DESCRIPTION),
				12, 0);
		} else if (FrontendButton_HandleSpriteButton(
				   &rect, "team2u", "team2u",
				   FrontendString_Get(
					   FRONTSTR_798_MISSION_DESCRIPTION),
				   12, 0, 12, "jewelsound") != 0) {
			g_missionSetupShowDescriptionPanel = 1;
			g_frontendFirstVisibleLine = 0;
			FrontImage_RegisterResourceDefault(
				"frontres\\soloteam.bmp", "background");
			FrontendDisplay_LockOffscreenSurface();
			FrontImage_DrawSpriteOpaque("background", 0, 0);
			FrontImage_DrawSprite("frame", 0, 0);
			if (g_hostCdAvailable != 0) {
				FrontImage_DrawSprite("allactive", 0, 0);
			} else {
				FrontImage_DrawSprite("clientactive", 0, 0);
			}
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontImage_DrawSpriteTranslucent("chatbox", 0,
								 0);
			}
			FrontImage_DrawSpriteTranslucent("teamoverlay", 0, 0);
			FrontendDisplay_UnlockOffscreenSurface(1);
		}
	}

	FrontendDraw_RectOffsetXY(&rect, 0, -28);
	if (slotStates[0] != FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_missionSetupShowDescriptionPanel == 0) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "team1d",
				FrontendString_Get(FRONTSTR_797_ASSIGN_TEAMS),
				12, 0);
		} else {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "team1u", "team1u",
				    FrontendString_Get(
					    FRONTSTR_797_ASSIGN_TEAMS),
				    12, 0, 11, "jewelsound") != 0) {
				g_missionSetupShowDescriptionPanel = 0;
				FrontImage_RegisterResourceDefault(
					"frontres\\soloteam.bmp", "background");
				FrontendDisplay_LockOffscreenSurface();
				FrontImage_DrawSpriteOpaque("background", 0, 0);
				FrontImage_DrawSprite("frame", 0, 0);
				if (g_hostCdAvailable != 0) {
					FrontImage_DrawSprite("allactive", 0,
							      0);
				} else {
					FrontImage_DrawSprite("clientactive", 0,
							      0);
				}
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontImage_DrawSpriteTranslucent(
						"chatbox", 0, 0);
				}
				FrontImage_DrawSpriteTranslucent("teamoverlay",
								 0, 0);
				{
					int overlayTeam = 0;
					FrontendDraw_RectAssign(&rect, 88, 207,
								256, 221);
					for (overlayTeam = 0;
					     overlayTeam < g_teamCount;
					     ++overlayTeam) {
						if ((g_teamCount == 8 ||
						     g_teamCount == 7) &&
						    overlayTeam == 4) {
							FrontendDraw_RectAssign(
								&rect, 260, 207,
								428, 221);
						}
						FrontendDraw_RectOffsetXY(
							&rect, 0, 15);
						for (slot = 0;
						     slot <
						     g_teamPlayerFlightGroupCount
							     [overlayTeam];
						     ++slot) {
							if (slot == 0) {
								FrontImage_DrawSpriteTranslucent(
									"captoverlay",
									rect.left,
									rect.top);
							} else {
								FrontImage_DrawSpriteTranslucent(
									"slotoverlay",
									rect.left,
									rect.top);
							}
							FrontendDraw_RectOffsetXY(
								&rect, 0, 15);
						}
					}
				}
				FrontendDisplay_UnlockOffscreenSurface(1);
			}
		}
	}
	return 1;
}

/* Clears the team assignments and places each of the readyCount players (1 in a
 * solo game, else Net_CountReadyPlayers()) at random: a g_mpRoster entry drawn
 * by rand() % 8 among those with a nonzero playerId not yet placed, on a team
 * drawn by rand() % g_teamCount among those with fewer placed players than
 * their g_teamPlayerFlightGroupCount, in that team's first empty slot; the
 * player's id also goes in assignedPlayerIds at the roster entry's index.
 * Outside a solo game it then sends every player a TEAM_ASSIGNMENTS packet with
 * teamPlayerIds and assignedPlayerIds. Returns 1. Does not check that there are
 * enough roster entries and team slots for the players, or that g_teamCount is
 * nonzero: the draws then never end, or divide by zero. */
// FUNCTION: XVT 0x4F4580
int MissionSetup_RandomizeTeamAssignments(void)
{
	int rosterIndex;
	int readyPlayerCount;
	int team;
	int assignmentSlot;
	int *teamAssignment;
	int playerId;
	int usedRoster[8];
	int assignedTeamCounts[10];

	memset(g_missionSetupPlayerAssignments.teamPlayerIds, 0,
	       sizeof(g_missionSetupPlayerAssignments.teamPlayerIds));
	memset(g_missionSetupPlayerAssignments.assignedPlayerIds, 0,
	       sizeof(g_missionSetupPlayerAssignments.assignedPlayerIds));
	memset(assignedTeamCounts, 0, sizeof(assignedTeamCounts));
	memset(usedRoster, 0, sizeof(usedRoster));
	readyPlayerCount = 1;
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		readyPlayerCount = Net_CountReadyPlayers();
	}
	while (readyPlayerCount > 0) {
		do {
			do {
				rosterIndex = rand() % 8;
			} while (g_mpRoster[rosterIndex].playerId == 0);
		} while (usedRoster[rosterIndex] != 0);

		do {
			team = rand() % g_teamCount;
		} while (assignedTeamCounts[team] >=
			 g_teamPlayerFlightGroupCount[team]);

		assignmentSlot = 0;
		++assignedTeamCounts[team];
		teamAssignment =
			g_missionSetupPlayerAssignments.teamPlayerIds[team];
		for (;;) {
			if (*teamAssignment == 0) {
				--readyPlayerCount;
				usedRoster[rosterIndex] = 1;
				playerId = g_mpRoster[rosterIndex].playerId;
				g_missionSetupPlayerAssignments
					.teamPlayerIds[team][assignmentSlot] =
					playerId;
				g_missionSetupPlayerAssignments
					.assignedPlayerIds[rosterIndex] =
					playerId;
				break;
			}
			++teamAssignment;
			++assignmentSlot;
			if (assignmentSlot >= 8) {
				break;
			}
		}
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_frontendNetPacketScratch.packetType =
			NET_PACKET_TEAM_ASSIGNMENTS;
		memcpy(g_frontendNetPacketScratch.payload,
		       g_missionSetupPlayerAssignments.teamPlayerIds,
		       sizeof(g_missionSetupPlayerAssignments.teamPlayerIds));
		memcpy(g_frontendNetPacketScratch.payload +
			       sizeof(g_missionSetupPlayerAssignments
					      .teamPlayerIds),
		       g_missionSetupPlayerAssignments.assignedPlayerIds,
		       sizeof(g_missionSetupPlayerAssignments
				      .assignedPlayerIds));
		Net_SendPacketAndFlush(
			0, &g_frontendNetPacketScratch,
			sizeof(g_missionSetupPlayerAssignments) +
				sizeof(g_frontendNetPacketScratch.packetType));
	}
	return 1;
}

/* Clears teamPlayerIds and assignedPlayerIds and, outside a solo game, sends
 * every player a CLEAR_TEAM_ASSIGNMENTS packet. Returns 1. */
// FUNCTION: XVT 0x4F46C0
int MissionSetup_ClearTeamAssignments(void)
{
	memset(g_missionSetupPlayerAssignments.teamPlayerIds, 0,
	       sizeof(g_missionSetupPlayerAssignments.teamPlayerIds));
	memset(g_missionSetupPlayerAssignments.assignedPlayerIds, 0,
	       sizeof(g_missionSetupPlayerAssignments.assignedPlayerIds));
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_frontendNetPacketScratch.packetType =
			NET_PACKET_CLEAR_TEAM_ASSIGNMENTS;
		Net_SendPacketAndFlush(
			0, &g_frontendNetPacketScratch,
			sizeof(g_frontendNetPacketScratch.packetType));
	}
	return 1;
}

/* Sends every player the host's final team assignments, a
 * TEAM_ASSIGNMENTS_READY packet carrying teamPlayerIds (not assignedPlayerIds).
 * Returns 1. */
// FUNCTION: XVT 0x4F4710
int MissionSetup_BroadcastTeamAssignments(void)
{
	g_frontendNetPacketScratch.packetType =
		NET_PACKET_TEAM_ASSIGNMENTS_READY;
	memcpy(g_frontendNetPacketScratch.payload,
	       g_missionSetupPlayerAssignments.teamPlayerIds,
	       sizeof(g_missionSetupPlayerAssignments.teamPlayerIds));
	Net_SendPacketAndFlush(
		0, &g_frontendNetPacketScratch,
		sizeof(g_missionSetupPlayerAssignments.teamPlayerIds) +
			sizeof(g_frontendNetPacketScratch.packetType));
	return 1;
}

/* Continues a saved battle when a combat engagement sequence starts, the
 * mission type being combat engagement; MissionSetup_TeamAssignmentUpdate calls
 * it on frame 0, the modern build through its campaign task. It moves the
 * mission type up to battle, loads the list and selects the stored battle. A
 * solo game continues when g_gameConfig.continueBattleOrCampaign is not
 * SEQUENCE_RESTART and the battle's spBattleContinuations entry is active,
 * copying the saved sequence state into g_pilotData.battleSequenceState. A
 * network host does the same with mpBattleContinuations, first sending every
 * player a BATTLE_CONTINUATION packet: 1, the saved randomSeed and the saved
 * state, or 0 alone when it does not continue. A client waits for that packet
 * until more than 30000 ms have passed; the original build drops any other
 * packet it reads meanwhile. On 1 it takes the host's state, keeping its own
 * saved cumulativeScore when the host's seed equals its own saved seed and
 * setting it to 0 when not. Continuing sets launchSessionMarker to 1, raises
 * currentMissionIndex by 1, calls MissionSetup_SelectNextSequenceMission and
 * returns 1. Otherwise it lowers the mission type by 1, clears the entry's
 * isActive and returns 0. On a client's timeout it returns 0 having lowered the
 * mission type by 1 without raising it first, which leaves it at tournament. In
 * the modern build a client returns XVT_CAMPAIGN_PENDING while it waits. */
// FUNCTION: XVT 0x4F4750
int MissionSetup_TryContinueBattle(void)
{
	enum { BATTLE_CONTINUATION_WAIT_TIMEOUT_MS = 30000 };

	int missionIndex;
	int missionDescriptionId;
	uint32_t continuationSeed;
#ifndef XVT_MODERN
	uint32_t waitStartMs;
	DPID senderId;
	uint32_t packetSize;
#endif
	int *receivedPacket;
	BattleSequenceState localSequenceState;

	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		++g_pilotData.missionDirectoryId;
		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			if ((unsigned int)g_missionCount > 0) {
				missionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				do {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    missionDescriptionId) {
						break;
					}
					++g_selectedMissionListIndex;
				} while ((unsigned int)g_missionCount >
					 (unsigned int)
						 g_selectedMissionListIndex);
			}
		}
		if (g_gameConfig.continueBattleOrCampaign != SEQUENCE_RESTART &&
		    g_pilotData.spBattleContinuations
				    [g_missionList[g_selectedMissionListIndex]
					     .missionIdx]
					    .isActive != 0) {
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx;
			memcpy(&g_pilotData.battleSequenceState,
			       &g_pilotData.spBattleContinuations[missionIndex]
					.sequenceState,
			       sizeof(g_pilotData.battleSequenceState));
			g_pilotData.launchSessionMarker = 1;
			++g_pilotData.battleSequenceState.currentMissionIndex;
			MissionSetup_SelectNextSequenceMission();
			return 1;
		}

		--g_pilotData.missionDirectoryId;
		missionIndex =
			g_missionList[g_selectedMissionListIndex].missionIdx;
		g_pilotData.spBattleContinuations[missionIndex].isActive = 0;
		return 0;
	}

	if (Net_IsHost() != 0) {
		++g_pilotData.missionDirectoryId;
		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			if ((unsigned int)g_missionCount > 0) {
				missionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				do {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    missionDescriptionId) {
						break;
					}
					++g_selectedMissionListIndex;
				} while ((unsigned int)g_missionCount >
					 (unsigned int)
						 g_selectedMissionListIndex);
			}
		}
		if (g_gameConfig.continueBattleOrCampaign == SEQUENCE_RESTART ||
		    g_pilotData.mpBattleContinuations
				    [g_missionList[g_selectedMissionListIndex]
					     .missionIdx]
					    .isActive == 0) {
			--g_pilotData.missionDirectoryId;
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx;
			*(int *)g_frontendNetPacketScratch.payload = 0;
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_BATTLE_CONTINUATION;
			g_pilotData.mpBattleContinuations[missionIndex]
				.isActive = 0;
			Net_SendPacketAndFlush(
				0, &g_frontendNetPacketScratch,
				sizeof(g_frontendNetPacketScratch.packetType) +
					sizeof(int));
			return 0;
		}

		g_frontendNetPacketScratch.packetType =
			NET_PACKET_BATTLE_CONTINUATION;
		*(int *)g_frontendNetPacketScratch.payload = 1;
		memcpy(g_frontendNetPacketScratch.payload + sizeof(int),
		       &g_pilotData
				.mpBattleContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.randomSeed,
		       sizeof(g_pilotData.mpBattleContinuations[0].randomSeed));
		memcpy(g_frontendNetPacketScratch.payload + sizeof(int) +
			       sizeof(continuationSeed),
		       &g_pilotData
				.mpBattleContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.sequenceState,
		       sizeof(g_pilotData.mpBattleContinuations[0]
				      .sequenceState));
		Net_SendPacketAndFlush(
			0, &g_frontendNetPacketScratch,
			sizeof(g_frontendNetPacketScratch.packetType) +
				sizeof(int) + sizeof(continuationSeed) +
				sizeof(BattleSequenceState));
		memcpy(&g_pilotData.battleSequenceState,
		       &g_pilotData
				.mpBattleContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.sequenceState,
		       sizeof(g_pilotData.battleSequenceState));
	} else {
#ifdef XVT_MODERN
		int waitResult = XvtCampaignTask_WaitPacket(
			NET_PACKET_BATTLE_CONTINUATION, &receivedPacket);
		if (waitResult == XVT_CAMPAIGN_PENDING) {
			return XVT_CAMPAIGN_PENDING;
		}
		if (waitResult == 0) {
			--g_pilotData.missionDirectoryId;
			return 0;
		}
#else
		waitStartMs = GetTickCount();
		do {
			receivedPacket =
				Net_GetNextAppPacket(&senderId, &packetSize);
			if (receivedPacket != NULL &&
			    receivedPacket[0] ==
				    NET_PACKET_BATTLE_CONTINUATION) {
				break;
			}
			if (GetTickCount() - waitStartMs >
			    BATTLE_CONTINUATION_WAIT_TIMEOUT_MS) {
				--g_pilotData.missionDirectoryId;
				return 0;
			}
		} while (1);
#endif

		++g_pilotData.missionDirectoryId;
		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			if ((unsigned int)g_missionCount > 0) {
				missionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				do {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    missionDescriptionId) {
						break;
					}
					++g_selectedMissionListIndex;
				} while ((unsigned int)g_missionCount >
					 (unsigned int)
						 g_selectedMissionListIndex);
			}
		}
		if (receivedPacket[1] == 0) {
			--g_pilotData.missionDirectoryId;
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx;
			g_pilotData.mpBattleContinuations[missionIndex]
				.isActive = 0;
			return 0;
		}

		memcpy(&localSequenceState,
		       &g_pilotData
				.mpBattleContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.sequenceState,
		       sizeof(localSequenceState));
		continuationSeed =
			g_pilotData
				.mpBattleContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.randomSeed;
		memcpy(&g_pilotData.battleSequenceState, &receivedPacket[3],
		       sizeof(g_pilotData.battleSequenceState));
		if ((uint32_t)receivedPacket[2] == continuationSeed) {
			g_pilotData.battleSequenceState.cumulativeScore =
				localSequenceState.cumulativeScore;
		} else {
			g_pilotData.battleSequenceState.cumulativeScore = 0;
		}
	}

	g_pilotData.launchSessionMarker = 1;
	++g_pilotData.battleSequenceState.currentMissionIndex;
	MissionSetup_SelectNextSequenceMission();
	return 1;
}

/* Continues a saved campaign when a campaign sequence starts, the mission type
 * being training; MissionSetup_TeamAssignmentUpdate calls it on frame 0, the
 * modern build through its campaign task. It sets the mission type to campaign,
 * loads the list and selects the stored campaign. A solo game continues when
 * g_gameConfig.continueBattleOrCampaign is not SEQUENCE_RESTART and the
 * campaign's spCampaignContinuations entry is active, copying the saved
 * sequence state into g_pilotData.campaignSequenceState. A network host does
 * the same with mpCampaignContinuations, first sending every player a
 * CAMPAIGN_CONTINUATION packet: 1, the saved randomSeed and the saved state, or
 * 0 alone when it does not continue. A client waits for that packet until more
 * than 30000 ms have passed; the original build drops any other packet it reads
 * meanwhile. On 1 it takes the host's state, keeping the cumulativeScore of its
 * own saved entry (mpCampaignContinuations at the campaign id plus 12) when the
 * host's seed equals that entry's seed and setting it to 0 when not. Continuing
 * sets launchSessionMarker to 1, raises currentMissionIndex by 1, calls
 * MissionSetup_SelectNextSequenceMission and returns 1. Otherwise it sets the
 * mission type back to training, clears the entry's isActive (a client's at the
 * id plus 12) unless the wait timed out, and returns 0. In the modern build a
 * client returns XVT_CAMPAIGN_PENDING while it waits. */
// FUNCTION: XVT 0x4F4B80
int MissionSetup_TryContinueCampaign(void)
{
	enum {
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
		CAMPAIGN_CONTINUATION_WAIT_TIMEOUT_MS = 30000
	};

	int missionIndex;
	int missionDescriptionId;
	uint32_t continuationSeed;
#ifndef XVT_MODERN
	uint32_t waitStartMs;
	DPID senderId;
	uint32_t packetSize;
#endif
	int *receivedPacket;
	CampaignSequenceState localSequenceState;

	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_pilotData.missionDirectoryId = MISSION_DIRECTORY_CAMPAIGNS;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_CAMPAIGNS);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			if ((unsigned int)g_missionCount > 0) {
				missionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				do {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    missionDescriptionId) {
						break;
					}
					++g_selectedMissionListIndex;
				} while ((unsigned int)g_missionCount >
					 (unsigned int)
						 g_selectedMissionListIndex);
			}
		}
		if (g_gameConfig.continueBattleOrCampaign != SEQUENCE_RESTART &&
		    g_pilotData.spCampaignContinuations
				    [g_missionList[g_selectedMissionListIndex]
					     .missionIdx]
					    .isActive != 0) {
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx;
			memcpy(&g_pilotData.campaignSequenceState,
			       &g_pilotData
					.spCampaignContinuations[missionIndex]
					.sequenceState,
			       sizeof(g_pilotData.campaignSequenceState));
			g_pilotData.launchSessionMarker = 1;
			++g_pilotData.campaignSequenceState.currentMissionIndex;
			MissionSetup_SelectNextSequenceMission();
			return 1;
		}

		g_pilotData.missionDirectoryId =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
		missionIndex =
			g_missionList[g_selectedMissionListIndex].missionIdx;
		g_pilotData.spCampaignContinuations[missionIndex].isActive = 0;
		return 0;
	}

	if (Net_IsHost() != 0) {
		g_pilotData.missionDirectoryId = MISSION_DIRECTORY_CAMPAIGNS;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_CAMPAIGNS);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			if ((unsigned int)g_missionCount > 0) {
				missionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				do {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    missionDescriptionId) {
						break;
					}
					++g_selectedMissionListIndex;
				} while ((unsigned int)g_missionCount >
					 (unsigned int)
						 g_selectedMissionListIndex);
			}
		}
		if (g_gameConfig.continueBattleOrCampaign == SEQUENCE_RESTART ||
		    g_pilotData.mpCampaignContinuations
				    [g_missionList[g_selectedMissionListIndex]
					     .missionIdx]
					    .isActive == 0) {
			g_pilotData.missionDirectoryId =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx;
			*(int *)g_frontendNetPacketScratch.payload = 0;
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_CAMPAIGN_CONTINUATION;
			g_pilotData.mpCampaignContinuations[missionIndex]
				.isActive = 0;
			Net_SendPacketAndFlush(
				0, &g_frontendNetPacketScratch,
				sizeof(g_frontendNetPacketScratch.packetType) +
					sizeof(int));
			return 0;
		}

		g_frontendNetPacketScratch.packetType =
			NET_PACKET_CAMPAIGN_CONTINUATION;
		*(int *)g_frontendNetPacketScratch.payload = 1;
		memcpy(g_frontendNetPacketScratch.payload + sizeof(int),
		       &g_pilotData
				.mpCampaignContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.randomSeed,
		       sizeof(g_pilotData.mpCampaignContinuations[0]
				      .randomSeed));
		memcpy(g_frontendNetPacketScratch.payload + sizeof(int) +
			       sizeof(continuationSeed),
		       &g_pilotData
				.mpCampaignContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.sequenceState,
		       sizeof(g_pilotData.mpCampaignContinuations[0]
				      .sequenceState));
		Net_SendPacketAndFlush(
			0, &g_frontendNetPacketScratch,
			sizeof(g_frontendNetPacketScratch.packetType) +
				sizeof(int) + sizeof(continuationSeed) +
				sizeof(CampaignSequenceState));
		memcpy(&g_pilotData.campaignSequenceState,
		       &g_pilotData
				.mpCampaignContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx]
				.sequenceState,
		       sizeof(g_pilotData.campaignSequenceState));
	} else {
#ifdef XVT_MODERN
		int waitResult = XvtCampaignTask_WaitPacket(
			NET_PACKET_CAMPAIGN_CONTINUATION, &receivedPacket);
		if (waitResult == XVT_CAMPAIGN_PENDING) {
			return XVT_CAMPAIGN_PENDING;
		}
		if (waitResult == 0) {
			g_pilotData.missionDirectoryId =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			return 0;
		}
#else
		waitStartMs = GetTickCount();
		do {
			receivedPacket =
				Net_GetNextAppPacket(&senderId, &packetSize);
			if (receivedPacket != NULL &&
			    receivedPacket[0] ==
				    NET_PACKET_CAMPAIGN_CONTINUATION) {
				break;
			}
			if (GetTickCount() - waitStartMs >
			    CAMPAIGN_CONTINUATION_WAIT_TIMEOUT_MS) {
				g_pilotData.missionDirectoryId =
					MISSION_DIRECTORY_TRAINING_EXERCISES;
				return 0;
			}
		} while (1);
#endif

		g_pilotData.missionDirectoryId = MISSION_DIRECTORY_CAMPAIGNS;
		MissionSetup_LoadMissionList(MISSION_DIRECTORY_CAMPAIGNS);
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			if ((unsigned int)g_missionCount > 0) {
				missionDescriptionId =
					g_pilotData.missionDescriptionIds
						[g_pilotData
							 .missionDirectoryId];
				do {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    missionDescriptionId) {
						break;
					}
					++g_selectedMissionListIndex;
				} while ((unsigned int)g_missionCount >
					 (unsigned int)
						 g_selectedMissionListIndex);
			}
		}
		if (receivedPacket[1] == 0) {
			g_pilotData.missionDirectoryId =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			missionIndex = g_missionList[g_selectedMissionListIndex]
					       .missionIdx +
				       CAMPAIGN_CLIENT_CONTINUATION_OFFSET;
			g_pilotData.mpCampaignContinuations[missionIndex]
				.isActive = 0;
			return 0;
		}

		memcpy(&localSequenceState,
		       &g_pilotData
				.mpCampaignContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx +
					 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
				.sequenceState,
		       sizeof(localSequenceState));
		continuationSeed =
			g_pilotData
				.mpCampaignContinuations
					[g_missionList
						 [g_selectedMissionListIndex]
							 .missionIdx +
					 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
				.randomSeed;
		memcpy(&g_pilotData.campaignSequenceState, &receivedPacket[3],
		       sizeof(g_pilotData.campaignSequenceState));
		if ((uint32_t)receivedPacket[2] == continuationSeed) {
			g_pilotData.campaignSequenceState.cumulativeScore =
				localSequenceState.cumulativeScore;
		} else {
			g_pilotData.campaignSequenceState.cumulativeScore = 0;
		}
	}

	g_pilotData.launchSessionMarker = 1;
	++g_pilotData.campaignSequenceState.currentMissionIndex;
	MissionSetup_SelectNextSequenceMission();
	return 1;
}

/* Draws the mission description on the team assignment screen in place of the
 * team slots: a heading for a tournament, battle, campaign or other mission,
 * then g_missionText wrapped, with a scrollbar that sets
 * g_frontendFirstVisibleLine when the text's wrapped line count (from a draw
 * starting at line 4096) plus one is over 13. Returns 1. */
// FUNCTION: XVT 0x4F4F80
int MissionSetup_DrawTeamMissionDescription(void)
{
	RECT rect;
	int lineCount;

	FrontendDraw_RectAssign(&rect, 88, 207, 430, 224);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_BATTLES) {
		FrontendText_DrawAlignedInRect(
			15, FrontendString_Get(FRONTSTR_472_BATTLE_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}
	FrontendDraw_RectAssign(&rect, 88, 225, 420, 433);
	lineCount = FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF,
					     4, 4096) +
		    1;
	if (lineCount > 13) {
		FrontendDraw_RectAssign(&rect, 421, 225, 430, 433);
		g_frontendFirstVisibleLine = FrontendScrollbar_Draw(
			&rect, g_frontendFirstVisibleLine, lineCount, 0, 5,
			(unsigned int)g_colorNavy, 9);
		FrontendDraw_RectAssign(&rect, 88, 225, 420, 433);
	} else {
		FrontendDraw_RectAssign(&rect, 88, 225, 430, 433);
	}
	FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF, 4,
				 g_frontendFirstVisibleLine);
	return 1;
}

/* Exit callback of the flight assignment screen: frees the briefing text
 * buffers, unloads the frontres\mapicons.lst images, frees g_missionList and
 * g_missionText, setting both to NULL, frees the "background" image, resets the
 * scrollable controls and clears the mouse input gate. Ignores frameCounter and
 * returns 0. */
// FUNCTION: XVT 0x4F5100
int MissionSetup_FreeScreenResources(int frameCounter)
{
	(void)frameCounter;

	BriefingText_FreeAllocatedBuffersExit();
	FrontImage_UnloadResourceList("frontres\\mapicons.lst");
	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_missionText != NULL) {
		free(g_missionText);
		g_missionText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	Frontend_ResetScrollableControls();
	FrontendMouse_ClearInputGate();
	return 0;
}

/* The flight assignment screen, run once per frame: each team's players are
 * placed in the team's player flight groups beside the briefing map, and Next
 * moves on to MissionBriefing_CraftSelectionUpdate. On frame 0 it clears
 * g_missionSetupLaunchSignalSent, the reservations,
 * g_frontendBriefingEnteredCount and g_missionSetupUseExpandedAssignmentLayout;
 * sets g_frontendChatTeamOnly when the pilot's team has more than one player
 * flight group; sets g_frontendGameSessionInProgress in a solo game; loads the
 * mission for the briefing and the map icons; fills g_textShadeRamps with five
 * ramps of eight shades from 0x48 to 0xFC (green, red, yellow, blue, magenta);
 * sets every g_missionSetupPlayerFlightGroupIndices entry to -1 unless
 * g_frontendSkipScreenEntrySetup is set; prunes the assignments; for each team
 * gives its player flight groups, in mission order, each to the next slot that
 * holds a player and has no flight group; and sets
 * g_frontendSkipScreenEntrySetup. A melee whose teams have one slot each, or a
 * Quick Start, goes straight to the briefing. Otherwise it picks the background
 * by mission type and side (the pilot's team, or for other types the IFF of the
 * first player flight group), draws the screen's base with a slot overlay per
 * player flight group of the pilot's team (the expanded layout, setting
 * g_missionSetupUseExpandedAssignmentLayout, when there are more than 4, else
 * at most 4 under the map), starts the 120000 ms launch countdown, and
 * allocates and loads g_missionText. Every frame it draws the mission name and,
 * outside a solo game, handles one packet: a host cancel leaves for the join
 * screen; a lobby state prunes the assignments; FLIGHT_ASSIGNMENTS_READY fills
 * the slots still empty and goes to the briefing; a return to setup goes back
 * to MissionSetup_Update; a countdown packet (type 'e') lowers
 * g_missionSetupLaunchCountdownMs to its value; reservations update
 * g_missionSetupReservedPlayerIds; a pilot rating updates its sender's roster
 * entry. With more than 4 player flight groups it shows, by
 * g_missionSetupUseExpandedAssignmentLayout, the mission description and the
 * flight slots (1) or the briefing map and the assigned players (0); with 4 or
 * fewer, the briefing map and the flight slots. Then the pilot banner and,
 * outside a solo game with more than one team, the countdown: the ms elapsed
 * since the last frame come off g_missionSetupLaunchCountdownMs, and the host
 * sends every player a countdown packet whenever the whole seconds change; once
 * it is under 0 it stays at 0, the captain of the pilot's team (slot 0) sends
 * FLIGHT_ASSIGNMENTS_READY to each player of the team once, and the function
 * returns 0 each frame before its buttons. Previous returns a solo game to team
 * assignment, asking first in a tournament or battle sequence (mission type
 * melee or combat engagement); a host's Restart asks, then sends every player
 * RETURN_TO_SETUP; a client's Leave asks, then leaves for the join screen.
 * Next, once MissionSetup_AreFlightAssignmentsComplete returns nonzero, goes to
 * the briefing in a solo game, or for the team's captain sends
 * FLIGHT_ASSIGNMENTS_READY to each player of the team. Last it handles the
 * assignment controls and the drag: a left click ends it, outside a solo game
 * sending RELEASE_FLIGHT_RESERVATION (the code tests the left click twice and
 * never the right); else the dragged name is drawn at the cursor. Returns 1
 * when Frontend_HandleCommonScreenControls returns 1, else 0. */
// FUNCTION: XVT 0x4F5170
int MissionSetup_FlightAssignmentUpdate(int frameCounter)
{
	enum {
		MAX_PLAYERS = 8,
		MAX_TEAMS = 10,
		MAX_COMPACT_FLIGHT_GROUPS = 4,
		BRIEFING_TEXT_CAPACITY = 4096,
		LAUNCH_COUNTDOWN_MS = 120000,
		DRAG_INPUT_GATE = 3,
		PACKET_COUNTDOWN = 'e',
	};

	int teamIndex;
	int slotIndex;
	int flightGroupIndex;
	int rosterIndex;
	int packetType;
	int flightGroupSlot;
	int drawnSlotCount;
	int textIndex;
	int animationFrame;
	int cursorX;
	int cursorY;
	int leaveConfirmed;
	RECT rect;
	RECT mapRect;
	RECT savedClipRect;

	if (frameCounter == 0) {
		g_missionSetupLaunchSignalSent = 0;
		if (g_teamPlayerFlightGroupCount[g_pilotData.team] > 1) {
			g_frontendChatTeamOnly = 1;
		}
		if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_frontendGameSessionInProgress = 1;
		}
		FrontendCursor_SetPos(37, 445);
		g_frontendFirstVisibleLine = 0;
		g_missionSetupReservedPlayerCount = 0;
		memset(g_missionSetupReservedPlayerIds, 0,
		       sizeof(g_missionSetupReservedPlayerIds));
		g_frontendBriefingEnteredCount = 0;
		g_missionSetupUseExpandedAssignmentLayout = 0;
		FrontendMission_LoadForBriefing();
		FrontImage_LoadResourceList("frontres\\mapicons.lst");

		g_textShadeRamps[0][0] = FrontendDisplay_PackRGB(0, 0x48, 0);
		g_textShadeRamps[0][1] = FrontendDisplay_PackRGB(0, 0x60, 0);
		g_textShadeRamps[0][2] = FrontendDisplay_PackRGB(0, 0x78, 0);
		g_textShadeRamps[0][3] = FrontendDisplay_PackRGB(0, 0x94, 0);
		g_textShadeRamps[0][4] = FrontendDisplay_PackRGB(0, 0xAC, 0);
		g_textShadeRamps[0][5] = FrontendDisplay_PackRGB(0, 0xC8, 0);
		g_textShadeRamps[0][6] = FrontendDisplay_PackRGB(0, 0xE0, 0);
		g_textShadeRamps[0][7] = FrontendDisplay_PackRGB(0, 0xFC, 0);
		g_textShadeRamps[1][0] = FrontendDisplay_PackRGB(0x48, 0, 0);
		g_textShadeRamps[1][1] = FrontendDisplay_PackRGB(0x60, 0, 0);
		g_textShadeRamps[1][2] = FrontendDisplay_PackRGB(0x78, 0, 0);
		g_textShadeRamps[1][3] = FrontendDisplay_PackRGB(0x94, 0, 0);
		g_textShadeRamps[1][4] = FrontendDisplay_PackRGB(0xAC, 0, 0);
		g_textShadeRamps[1][5] = FrontendDisplay_PackRGB(0xC8, 0, 0);
		g_textShadeRamps[1][6] = FrontendDisplay_PackRGB(0xE0, 0, 0);
		g_textShadeRamps[1][7] = FrontendDisplay_PackRGB(0xFC, 0, 0);
		g_textShadeRamps[2][0] = FrontendDisplay_PackRGB(0x48, 0x48, 0);
		g_textShadeRamps[2][1] = FrontendDisplay_PackRGB(0x60, 0x60, 0);
		g_textShadeRamps[2][2] = FrontendDisplay_PackRGB(0x78, 0x78, 0);
		g_textShadeRamps[2][3] = FrontendDisplay_PackRGB(0x94, 0x94, 0);
		g_textShadeRamps[2][4] = FrontendDisplay_PackRGB(0xAC, 0xAC, 0);
		g_textShadeRamps[2][5] = FrontendDisplay_PackRGB(0xC8, 0xC8, 0);
		g_textShadeRamps[2][6] = FrontendDisplay_PackRGB(0xE0, 0xE0, 0);
		g_textShadeRamps[2][7] = FrontendDisplay_PackRGB(0xFC, 0xFC, 0);
		g_textShadeRamps[3][0] = FrontendDisplay_PackRGB(0, 0, 0x48);
		g_textShadeRamps[3][1] = FrontendDisplay_PackRGB(0, 0, 0x60);
		g_textShadeRamps[3][2] = FrontendDisplay_PackRGB(0, 0, 0x78);
		g_textShadeRamps[3][3] = FrontendDisplay_PackRGB(0, 0, 0x94);
		g_textShadeRamps[3][4] = FrontendDisplay_PackRGB(0, 0, 0xAC);
		g_textShadeRamps[3][5] = FrontendDisplay_PackRGB(0, 0, 0xC8);
		g_textShadeRamps[3][6] = FrontendDisplay_PackRGB(0, 0, 0xE0);
		g_textShadeRamps[3][7] = FrontendDisplay_PackRGB(0, 0, 0xFC);
		g_textShadeRamps[4][0] = FrontendDisplay_PackRGB(0x48, 0, 0x48);
		g_textShadeRamps[4][1] = FrontendDisplay_PackRGB(0x60, 0, 0x60);
		g_textShadeRamps[4][2] = FrontendDisplay_PackRGB(0x78, 0, 0x78);
		g_textShadeRamps[4][3] = FrontendDisplay_PackRGB(0x94, 0, 0x94);
		g_textShadeRamps[4][4] = FrontendDisplay_PackRGB(0xAC, 0, 0xAC);
		g_textShadeRamps[4][5] = FrontendDisplay_PackRGB(0xC8, 0, 0xC8);
		g_textShadeRamps[4][6] = FrontendDisplay_PackRGB(0xE0, 0, 0xE0);
		g_textShadeRamps[4][7] = FrontendDisplay_PackRGB(0xFC, 0, 0xFC);

		if (g_frontendSkipScreenEntrySetup == 0) {
			for (teamIndex = 0; teamIndex < MAX_TEAMS;
			     ++teamIndex) {
				memset(&g_missionSetupPlayerFlightGroupIndices
					       [teamIndex * MAX_PLAYERS],
				       0xFF,
				       MAX_PLAYERS *
					       sizeof(g_missionSetupPlayerFlightGroupIndices
							      [0]));
			}
		}
		MissionSetup_PruneFlightAssignments();
		g_missionSetupDraggedPlayerId = 0;
		for (teamIndex = 0; teamIndex < g_teamCount; ++teamIndex) {
			flightGroupSlot = 0;
			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int16_t)g_frontendMission.flightGroupCount;
			     ++flightGroupIndex) {
				if (g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .playerNumber != 0 &&
				    g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .team == teamIndex &&
				    flightGroupSlot < MAX_PLAYERS) {
					for (; flightGroupSlot < MAX_PLAYERS;
					     ++flightGroupSlot) {
						if (g_missionSetupPlayerAssignments
								    .teamPlayerIds
									    [teamIndex]
									    [flightGroupSlot] !=
							    0 &&
						    g_missionSetupPlayerFlightGroupIndices
								    [teamIndex *
									     MAX_PLAYERS +
								     flightGroupSlot] ==
							    -1) {
							g_missionSetupPlayerFlightGroupIndices
								[teamIndex *
									 MAX_PLAYERS +
								 flightGroupSlot] =
									flightGroupIndex;
							break;
						}
					}
				}
			}
		}
		g_frontendSkipScreenEntrySetup = 1;

		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_MELEES) {
			for (teamIndex = 0; teamIndex < g_teamCount;
			     ++teamIndex) {
				if (g_teamPlayerFlightGroupCount[teamIndex] >
				    1) {
					break;
				}
			}
			if (teamIndex == g_teamCount) {
				FrontendScreen_SetCallbacks(
					MissionBriefing_CraftSelectionUpdate,
					MissionBriefing_CraftSelectionExit);
				return 0;
			}
		}
		if (g_frontendQuickStartLaunchFlag == 1) {
			FrontendScreen_SetCallbacks(
				MissionBriefing_CraftSelectionUpdate,
				MissionBriefing_CraftSelectionExit);
			return 0;
		}

		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_MELEES ||
		    g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TOURNAMENTS) {
			FrontImage_RegisterResourceDefault(
				"frontres\\player.bmp", "background");
		} else if (g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			   g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_BATTLES) {
			if (g_pilotData.team == 0) {
				FrontImage_RegisterResourceDefault(
					"frontres\\playeri.bmp", "background");
			} else {
				FrontImage_RegisterResourceDefault(
					"frontres\\playerr.bmp", "background");
			}
		} else {
			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int16_t)g_frontendMission.flightGroupCount;
			     ++flightGroupIndex) {
				if (g_frontendMission
					    .flightGroups[flightGroupIndex]
					    .playerNumber != 0) {
					if (g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .iff == 0) {
						FrontImage_RegisterResourceDefault(
							"frontres\\playerr.bmp",
							"background");
					} else if (
						g_frontendMission
							.flightGroups
								[flightGroupIndex]
							.iff == 1) {
						FrontImage_RegisterResourceDefault(
							"frontres\\playeri.bmp",
							"background");
					} else {
						FrontImage_RegisterResourceDefault(
							"frontres\\player.bmp",
							"background");
					}
					break;
				}
			}
		}

		if (g_teamPlayerFlightGroupCount[g_pilotData.team] >
		    MAX_COMPACT_FLIGHT_GROUPS) {
			g_missionSetupUseExpandedAssignmentLayout = 1;
			FrontendCursor_SetPos(33, 319);
			FrontendDisplay_LockOffscreenSurface();
			FrontImage_DrawSpriteOpaque("background", 0, 0);
			FrontImage_DrawSprite("frame", 0, 0);
			if (g_hostCdAvailable != 0) {
				FrontImage_DrawSprite("allactive", 0, 0);
			} else {
				FrontImage_DrawSprite("clientactive", 0, 0);
			}
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontImage_DrawSpriteTranslucent("chatbox", 0,
								 0);
			}
			FrontImage_DrawSpriteTranslucent("mapassignoverlay", 0,
							 0);
			/* cursorX and cursorY hold each flight group slot overlay's screen position here, not the
			 * mouse. */
			cursorX = 230;
			cursorY = 284;
			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int16_t)g_frontendMission.flightGroupCount;
			     ++flightGroupIndex) {
				if (g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .playerNumber != 0 &&
				    g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .team == g_pilotData.team) {
					FrontImage_DrawSpriteTranslucent(
						"fgslotoverlay", cursorX,
						cursorY);
					cursorY += 17;
				}
			}
		} else {
			FrontendDisplay_LockOffscreenSurface();
			FrontImage_DrawSpriteOpaque("background", 0, 0);
			FrontImage_DrawSprite("frame", 0, 0);
			if (g_hostCdAvailable != 0) {
				FrontImage_DrawSprite("allactive", 0, 0);
			} else {
				FrontImage_DrawSprite("clientactive", 0, 0);
			}
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontImage_DrawSpriteTranslucent("chatbox", 0,
								 0);
			}
			FrontImage_DrawSpriteTranslucent("mapoverlay", 0, 0);
			FrontendDraw_RectCopy(&rect, &g_briefingMapPanelRect);
			FrontendDraw_RectOffsetXY(&rect, 84, 96);
			rect.top = rect.bottom - 27;
			FrontendDraw_FillRectTranslucent(&rect, 0, 0,
							 g_colorBlue);
			/* cursorX and cursorY hold each flight group slot overlay's screen position here, not the
			 * mouse. */
			cursorX = 230;
			cursorY = 352;
			drawnSlotCount = 0;
			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int16_t)g_frontendMission.flightGroupCount;
			     ++flightGroupIndex) {
				if (g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .playerNumber != 0 &&
				    g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .team == g_pilotData.team) {
					++drawnSlotCount;
					FrontImage_DrawSpriteTranslucent(
						"fgslotoverlay", cursorX,
						cursorY);
					if (drawnSlotCount >=
					    MAX_COMPACT_FLIGHT_GROUPS) {
						break;
					}
					cursorY += 17;
				}
			}
		}
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_StartTextFadeIn(20);
		g_missionSetupLaunchCountdownMs = LAUNCH_COUNTDOWN_MS;
		g_missionSetupCountdownClockMs = GetTickCount();
		g_missionSetupLastBroadcastCountdownSecond =
			LAUNCH_COUNTDOWN_MS / 1000;
		g_missionSetupCountdownPreviousClockMs =
			g_missionSetupCountdownClockMs;
		g_missionText = malloc(BRIEFING_TEXT_CAPACITY);
		MissionSetup_LoadMissionDescText(g_missionText);
	}

	FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
	FrontendDisplay_GetScreenClipRect(&savedClipRect);
	FrontendDisplay_SetScreenClipRect640x480(&rect);
	sprintf(g_frontendScratchBuffer, "%c%s", 4,
		g_missionList[g_selectedMissionListIndex].description);
	for (textIndex = (int)strlen(g_frontendScratchBuffer) - 1;
	     textIndex > 0; --textIndex) {
		if (g_frontendScratchBuffer[textIndex] == '(') {
			g_frontendScratchBuffer[textIndex] = '\0';
			break;
		}
	}
	FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect, 0xFFFF);
	FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		packetType = FrontendNet_ProcessNetworkPackets();
		if (packetType == NET_PACKET_HOST_CANCELLED) {
			Net_ShutdownDirectPlaySession();
			if (Net_IsHost() == 0) {
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					FrontendString_Get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					FrontendString_Get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(
					XvtMissionDialogs_Resume,
					XVT_MISSION_ASSIGNMENT_CANCELLED);
#endif
			}
			g_frontendSkipScreenEntrySetup = 1;
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResources);
		} else if (packetType == NET_PACKET_STATE) {
			MissionSetup_PruneFlightAssignments();
		} else if (packetType == NET_PACKET_FLIGHT_ASSIGNMENTS_READY) {
			MissionSetup_FillFlightAssignments();
			FrontendScreen_SetCallbacks(
				MissionBriefing_CraftSelectionUpdate,
				MissionBriefing_CraftSelectionExit);
			return 0;
		} else if (packetType == NET_PACKET_RETURN_TO_SETUP) {
			g_frontendSkipScreenEntrySetup = 1;
			FrontendScreen_SetCallbacks(
				MissionSetup_Update,
				(FrontendScreenExitFn)MissionSetup_Exit);
			return 0;
		} else if (packetType == PACKET_COUNTDOWN) {
			if (g_missionSetupLaunchCountdownMs >
			    g_frontendNetPacketArg0) {
				g_missionSetupLaunchCountdownMs =
					g_frontendNetPacketArg0;
			}
		} else if (packetType ==
			   NET_PACKET_RELEASE_FLIGHT_RESERVATION) {
			for (slotIndex = 0;
			     slotIndex < g_missionSetupReservedPlayerCount;
			     ++slotIndex) {
				if (g_missionSetupReservedPlayerIds
					    [slotIndex] ==
				    g_frontendNetPacketArg0) {
					--g_missionSetupReservedPlayerCount;
					break;
				}
			}
			for (; slotIndex < MAX_PLAYERS - 1; ++slotIndex) {
				g_missionSetupReservedPlayerIds[slotIndex] =
					g_missionSetupReservedPlayerIds
						[slotIndex + 1];
			}
			g_missionSetupReservedPlayerIds[MAX_PLAYERS - 1] = 0;
		} else if (packetType == NET_PACKET_FLIGHT_RESERVATION) {
			for (slotIndex = 0;
			     slotIndex < g_missionSetupReservedPlayerCount;
			     ++slotIndex) {
				if (g_missionSetupReservedPlayerIds
					    [slotIndex] ==
				    g_frontendNetPacketArg0) {
					break;
				}
			}
			if (slotIndex == g_missionSetupReservedPlayerCount) {
				g_missionSetupReservedPlayerIds
					[g_missionSetupReservedPlayerCount++] =
						g_frontendNetPacketArg0;
			}
		} else if (packetType == NET_PACKET_PILOT_RATING) {
			for (rosterIndex = 0; rosterIndex < MAX_PLAYERS;
			     ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId ==
				    g_frontendNetPacketSenderPlayerId) {
					g_mpRoster[rosterIndex].pilotRating =
						g_frontendNetPacketArg0;
					break;
				}
			}
		}
	}

	if (g_teamPlayerFlightGroupCount[g_pilotData.team] >
	    MAX_COMPACT_FLIGHT_GROUPS) {
		if (g_missionSetupUseExpandedAssignmentLayout == 0) {
			FrontendDraw_RectAssign(&rect, 84, 96, 443, 335);
			FrontendDraw_RectAssign(&mapRect, 84, 96, 443, 335);
			FrontendDisplay_GetScreenClipRect(&savedClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&mapRect);
			FrontendCursor_GetPos(&cursorX, &cursorY);
			MissionBriefing_HandleMapMouseInput(
				&rect, &mapRect, 0, FrontendMouse_GetLeftDown(),
				FrontendMouse_GetRightDown(), (int16_t)cursorX,
				(int16_t)cursorY);
			BriefingScript_AdvanceOrResetAtEnd(frameCounter);
			MissionBriefing_DrawMapViewport(&rect, &mapRect, 1);
			FrontendDisplay_SetScreenClipRect640x480(
				&savedClipRect);
			sprintf(g_frontendScratchBuffer, "%s %c%s",
				FrontendString_Get(FRONTSTR_207_BRIEFING), 4,
				g_frontendMission.teams[g_pilotData.team].name);
			FrontendText_Draw(12, g_frontendScratchBuffer, 86, 92,
					  0xFFFF);
			MissionSetup_DrawAssignedPlayers(frameCounter);
		} else {
			MissionSetup_DrawAssignmentMissionDescription();
			MissionSetup_DrawFlightAssignments(frameCounter);
		}
	} else {
		FrontendDraw_RectAssign(&rect, 84, 96, 443, 335);
		FrontendDraw_RectAssign(&mapRect, 84, 96, 443, 335);
		FrontendDisplay_GetScreenClipRect(&savedClipRect);
		FrontendDisplay_SetScreenClipRect640x480(&mapRect);
		FrontendCursor_GetPos(&cursorX, &cursorY);
		MissionBriefing_HandleMapMouseInput(
			&rect, &mapRect, 0, FrontendMouse_GetLeftDown(),
			FrontendMouse_GetRightDown(), (int16_t)cursorX,
			(int16_t)cursorY);
		BriefingScript_AdvanceOrResetAtEnd(frameCounter);
		MissionBriefing_DrawMapViewport(&rect, &mapRect, 1);
		FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);
		sprintf(g_frontendScratchBuffer, "%s %c%s",
			FrontendString_Get(FRONTSTR_207_BRIEFING), 4,
			g_frontendMission.teams[g_pilotData.team].name);
		FrontendText_Draw(12, g_frontendScratchBuffer, 86, 92, 0xFFFF);
		MissionSetup_DrawFlightAssignments(frameCounter);
	}

	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
			g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect,
					  g_colorYellow);
		if (g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_MELEES ||
		    g_pilotData.missionDirectoryId ==
			    MISSION_DIRECTORY_TOURNAMENTS) {
			animationFrame = (frameCounter % 32) >> 1;
			sprintf(g_frontendScratchBuffer, "rebtiny%d",
				animationFrame);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 204,
					      453);
			sprintf(g_frontendScratchBuffer, "imptiny%d",
				animationFrame);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 420,
					      453);
		} else if (g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			   g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_BATTLES) {
			if (g_pilotData.team == 0) {
				animationFrame = (frameCounter % 32) >> 1;
				sprintf(g_frontendScratchBuffer, "imptiny%d",
					animationFrame);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      204, 453);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      420, 453);
			} else {
				animationFrame = (frameCounter % 32) >> 1;
				sprintf(g_frontendScratchBuffer, "rebtiny%d",
					animationFrame);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      204, 453);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      420, 453);
			}
		} else {
			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int16_t)g_frontendMission.flightGroupCount;
			     ++flightGroupIndex) {
				if (g_frontendMission
					    .flightGroups[flightGroupIndex]
					    .playerNumber != 0) {
					if (g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .iff == 0) {
						animationFrame =
							(frameCounter % 32) >>
							1;
						sprintf(g_frontendScratchBuffer,
							"rebtiny%d",
							animationFrame);
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							204, 453);
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							420, 453);
					} else if (
						g_frontendMission
							.flightGroups
								[flightGroupIndex]
							.iff == 1) {
						animationFrame =
							(frameCounter % 32) >>
							1;
						sprintf(g_frontendScratchBuffer,
							"imptiny%d",
							animationFrame);
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							204, 453);
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							420, 453);
					} else {
						animationFrame =
							(frameCounter % 32) >>
							1;
						sprintf(g_frontendScratchBuffer,
							"rebtiny%d",
							animationFrame);
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							204, 453);
						sprintf(g_frontendScratchBuffer,
							"imptiny%d",
							animationFrame);
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							420, 453);
					}
					break;
				}
			}
		}
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_teamCount > 1) {
		FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
		Frontend_FormatSecondsToClockString(
			g_missionSetupLaunchCountdownMs / 1000);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect,
					  0xFFFF);
	}
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendNet_UpdateAndDrawChatPanel(frameCounter);
	}
	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_teamCount > 1) {
		g_missionSetupCountdownClockMs = GetTickCount();
		g_missionSetupLaunchCountdownMs +=
			g_missionSetupCountdownPreviousClockMs -
			g_missionSetupCountdownClockMs;
		if (Net_IsHost() != 0 &&
		    g_missionSetupLastBroadcastCountdownSecond !=
			    g_missionSetupLaunchCountdownMs / 1000) {
			g_missionSetupLastBroadcastCountdownSecond =
				g_missionSetupLaunchCountdownMs / 1000;
			*(int *)g_frontendNetPacketScratch.payload =
				g_missionSetupLaunchCountdownMs;
			g_frontendNetPacketScratch.packetType =
				PACKET_COUNTDOWN;
			Net_SendPacketAndFlush(
				0, &g_frontendNetPacketScratch,
				2 * sizeof(g_frontendNetPacketScratch
						   .packetType));
		}
		if (g_missionSetupLaunchCountdownMs < 0) {
			g_missionSetupLaunchCountdownMs = 0;
			if (g_missionSetupLaunchSignalSent == 0 &&
			    Net_GetLocalPlayerId() ==
				    g_missionSetupPlayerAssignments
					    .teamPlayerIds[g_pilotData.team]
							  [0]) {
				g_missionSetupLaunchSignalSent = 1;
				for (slotIndex = 0; slotIndex < MAX_PLAYERS;
				     ++slotIndex) {
					if (g_missionSetupPlayerAssignments
						    .teamPlayerIds
							    [g_pilotData.team]
							    [slotIndex] != 0) {
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_FLIGHT_ASSIGNMENTS_READY;
						Net_SendPacketAndFlush(
							g_missionSetupPlayerAssignments
								.teamPlayerIds
									[g_pilotData
										 .team]
									[slotIndex],
							&g_frontendNetPacketScratch,
							sizeof(g_frontendNetPacketScratch
								       .packetType));
					}
				}
			}
			return 0;
		}
		g_missionSetupCountdownPreviousClockMs =
			g_missionSetupCountdownClockMs;
	}

	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_569_PREVIOUS));
		if (g_missionSetupTeamAssignmentSkipped != 0) {
			leaveConfirmed = FrontendButton_HandleSpriteButton(
				&rect, "leaveup", "leavedown",
				FrontendString_Get(
					FRONTSTR_260_RETURN_TO_SELECT_MISSION),
				12, 0, 8, "buttonsound");
		} else {
			leaveConfirmed = FrontendButton_HandleSpriteButton(
				&rect, "leaveup", "leavedown",
				FrontendString_Get(
					FRONTSTR_261_RETURN_TO_SELECT_TEAMS),
				12, 0, 8, "buttonsound");
		}
		if (leaveConfirmed != 0) {
			if (g_pilotData.missionSequenceActive == 1) {
				if (g_pilotData.missionDirectoryId ==
				    MISSION_DIRECTORY_MELEES) {
					leaveConfirmed = FrontendDialog_ShowConfirmDialog(
						FrontendString_Get(
							FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
						FrontendString_Get(
							FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
						FrontendString_Get(
							FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
						FrontendString_Get(
							FRONTSTR_523_OKAY),
						FrontendString_Get(
							FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
					return XvtDialog_ContinueWith(
						XvtMissionDialogs_Resume,
						XVT_MISSION_SOLO_BACK_TO_TEAMS);
#endif
				} else if (
					g_pilotData.missionDirectoryId ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					leaveConfirmed = FrontendDialog_ShowConfirmDialog(
						FrontendString_Get(
							FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
						FrontendString_Get(
							FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
						FrontendString_Get(
							FRONTSTR_683_TERMINATE_THIS_BATTLE),
						FrontendString_Get(
							FRONTSTR_523_OKAY),
						FrontendString_Get(
							FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
					return XvtDialog_ContinueWith(
						XvtMissionDialogs_Resume,
						XVT_MISSION_SOLO_BACK_TO_TEAMS);
#endif
				}
			}
			if (leaveConfirmed != 0) {
				g_frontendSkipScreenEntrySetup = 1;
				FrontendScreen_SetCallbacks(
					MissionSetup_TeamAssignmentUpdate,

#ifdef XVT_MODERN
					XvtFrontendCleanup_MissionResources
#else
					(FrontendScreenExitFn)
						FrontendMissionList_FreeScreenResourcesAndClearInputGate
#endif
				);
			}
		}
	} else if (Net_IsHost() != 0) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_668_RESTART));

#ifdef XVT_MODERN
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
				FrontendString_Get(
					FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
				FrontendString_Get(
					FRONTSTR_754_TO_SELECT_MISSION),
				FrontendString_Get(FRONTSTR_523_OKAY),
				FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
						      XVT_MISSION_HOST_RESTART);
		}
#else
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0 &&
		    FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
			    FrontendString_Get(
				    FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
			    FrontendString_Get(FRONTSTR_754_TO_SELECT_MISSION),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_RETURN_TO_SETUP;
			Net_SendPacketAndFlush(
				0, &g_frontendNetPacketScratch,
				sizeof(g_frontendNetPacketScratch.packetType));
		}
#endif

	} else {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_204_LEAVE));

#ifdef XVT_MODERN
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				FrontendString_Get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				FrontendString_Get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				FrontendString_Get(FRONTSTR_523_OKAY),
				FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
						      XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0 &&
		    FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    FrontendString_Get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    FrontendString_Get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendSkipScreenEntrySetup = 1;
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_PLAYER_LEFT;
			Net_SendPacketAndFlush(
				Net_GetHostPlayerId(),
				&g_frontendNetPacketScratch,
				sizeof(g_frontendNetPacketScratch.packetType));
			Net_ShutdownDirectPlaySession();
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResources);
		}
#endif
	}

	FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (MissionSetup_AreFlightAssignmentsComplete() != 0) {
			FrontendButton_SetOverlayText(
				FrontendString_Get(FRONTSTR_212_NEXT));
			if (FrontendButton_HandleSpriteButton(
				    &rect, "nextup", "nextdown",
				    FrontendString_Get(
					    FRONTSTR_667_GO_TO_CRAFT_SELECTION),
				    12, 0, 7, "flysound") != 0) {
				FrontendScreen_SetCallbacks(
					MissionBriefing_CraftSelectionUpdate,
					MissionBriefing_CraftSelectionExit);
				FrontendButton_DisableOverlayText();
				return 0;
			}
		}
	} else if (Net_GetLocalPlayerId() ==
			   g_missionSetupPlayerAssignments
				   .teamPlayerIds[g_pilotData.team][0] &&
		   MissionSetup_AreFlightAssignmentsComplete() != 0) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_212_NEXT));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "nextup", "nextdown",
			    FrontendString_Get(
				    FRONTSTR_667_GO_TO_CRAFT_SELECTION),
			    12, 0, 7, "flysound") != 0) {
			for (slotIndex = 0; slotIndex < MAX_PLAYERS;
			     ++slotIndex) {
				if (g_missionSetupPlayerAssignments
					    .teamPlayerIds[g_pilotData.team]
							  [slotIndex] != 0) {
					g_frontendNetPacketScratch.packetType =
						NET_PACKET_FLIGHT_ASSIGNMENTS_READY;
					Net_SendPacketAndFlush(
						g_missionSetupPlayerAssignments
							.teamPlayerIds
								[g_pilotData
									 .team]
								[slotIndex],
						&g_frontendNetPacketScratch,
						sizeof(g_frontendNetPacketScratch
							       .packetType));
				}
			}
		}
	}
	FrontendButton_DisableOverlayText();
	MissionSetup_DrawAssignmentControls();
	if (Frontend_HandleCommonScreenControls(1) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (XvtDialog_IsActive()) {
		return 0;
	}
#endif

	if (FrontendMouse_IsGateOwner(DRAG_INPUT_GATE)) {
		if (FrontendMouse_GetLeftClickFor(DRAG_INPUT_GATE) != 0 ||
		    FrontendMouse_GetLeftClickFor(DRAG_INPUT_GATE) != 0) {
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				*(int *)g_frontendNetPacketScratch.payload =
					g_missionSetupDraggedPlayerId;
				g_frontendNetPacketScratch.packetType =
					NET_PACKET_RELEASE_FLIGHT_RESERVATION;
				Net_SendPacketAndFlush(
					0, &g_frontendNetPacketScratch,
					2 * sizeof(g_frontendNetPacketScratch
							   .packetType));
			}
			g_missionSetupDraggedPlayerId = 0;
			FrontendMouse_ClearInputGate();
			return 0;
		}
		for (rosterIndex = 0; rosterIndex < MAX_PLAYERS;
		     ++rosterIndex) {
			if (g_mpRoster[rosterIndex].playerId != 0 &&
			    g_mpRoster[rosterIndex].playerId ==
				    g_missionSetupDraggedPlayerId) {
				FrontendCursor_GetPos(&cursorX, &cursorY);
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				if (Net_GetLocalPlayerId() ==
					    g_mpRoster[rosterIndex].playerId ||
				    g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontendText_Draw(
						12, g_frontendScratchBuffer,
						cursorX - 7, cursorY - 7,
						g_pulseColorRamp
							[((frameCounter % 24) &
							  ~1) >>
							 1]);
				} else {
					FrontendText_Draw(
						12, g_frontendScratchBuffer,
						cursorX - 7, cursorY - 7,
						g_colorYellow);
				}
				return 0;
			}
		}
	}
	return 0;
}

/* Draws the side buttons of the flight assignment screen and handles them. The
 * captain's buttons belong to the local player when it holds slot 0 of
 * g_pilotData.team, and to every solo player. While the briefing map shows
 * (g_missionSetupUseExpandedAssignmentLayout 0) there are the map's buttons:
 * Play sets g_briefingPlaybackActive and Stop clears it; a click on Forward
 * while playing advances the briefing a line and restarts it when
 * g_briefingTextSlotBlockIdx[1] then equals g_briefingLastNarratedTextBlockIdx;
 * Rewind restarts it. When the pilot's team has more than 4 player flight
 * groups, Assign Players and View Briefing Map set
 * g_missionSetupUseExpandedAssignmentLayout to 1 or 0 and redraw the screen's
 * base (Assign Players also resets g_frontendFirstVisibleLine, View Briefing
 * Map puts the cursor at 37, 445), and the captain gets Clear List and Auto
 * Assign in the expanded layout. With 2 to 4 player flight groups the captain
 * gets Clear List and Auto Assign under the map. Clear List calls
 * MissionSetup_ClearFlightAssignments, Auto Assign
 * MissionSetup_RandomizeFlightAssignments. Returns 0. */
// FUNCTION: XVT 0x4F8E40
int MissionSetup_DrawAssignmentControls(void)
{
	enum {
		NAV_SLOT_PLAY = 0,
		NAV_SLOT_STOP = 1,
		NAV_SLOT_REWIND = 3,
		NAV_SLOT_EXPANDED_AUTO_ASSIGN = 4,
		NAV_SLOT_FIRST_ASSIGNMENT = 5,
		NAV_SLOT_SECOND_ASSIGNMENT = 6,
		NAV_SLOT_EXPANDED_CLEAR_LIST = 7,
		NAV_SLOT_COUNT = 8,
		COMPACT_FLIGHT_GROUP_LIMIT = 4,
		SINGLE_FLIGHT_GROUP_COUNT = 1,
		BUTTON_LEFT = 22,
		BUTTON_RIGHT = 42,
		MAP_BUTTON_TOP = 198,
		MAP_BUTTON_BOTTOM = 222,
		EXPANDED_TOP_BUTTON_TOP = 226,
		EXPANDED_TOP_BUTTON_BOTTOM = 250,
		EXPANDED_SECOND_BUTTON_TOP = 254,
		EXPANDED_SECOND_BUTTON_BOTTOM = 278,
		COMPACT_ASSIGN_BUTTON_TOP = 306,
		COMPACT_ASSIGN_BUTTON_BOTTOM = 330,
		BOTTOM_BUTTON_TOP = 334,
		BOTTOM_BUTTON_BOTTOM = 358,
		BUTTON_ROW_OFFSET = 28,
		REWIND_TO_STOP_OFFSET = -56,
		BUTTON_FONT_SIZE = 12,
		PLAY_OR_AUTO_ASSIGN_HELD_SLOT = 11,
		STOP_OR_CLEAR_LIST_HELD_SLOT = 12,
		REWIND_HELD_SLOT = 14,
		BRIEFING_MAP_OR_AUTO_ASSIGN_HELD_SLOT = 16,
		ASSIGN_PLAYERS_OR_CLEAR_LIST_HELD_SLOT = 17,
		ASSIGNMENT_OVERLAY_X = 230,
		ASSIGNMENT_OVERLAY_Y = 284,
		ASSIGNMENT_OVERLAY_ROW_HEIGHT = 17,
		BRIEFING_MAP_OFFSET_X = 84,
		BRIEFING_MAP_OFFSET_Y = 96,
		BRIEFING_NARRATION_HEIGHT = 27,
		BRIEFING_CURSOR_X = 37,
		BRIEFING_CURSOR_Y = 445,
		UI_SOUND_PRIORITY = 255,
		UI_SOUND_VOLUME_SCALE = 12,
		UI_SOUND_PAN_CENTER = 63
	};

	RECT rect;
	int cursorX;
	int cursorY;
	int captainControls;
	int expandedAtStart;
	int teamFlightGroups;
	FrontendNavigationSlotState slotStates[NAV_SLOT_COUNT];
	int slotIndex;
	int flightGroupIndex;

	captainControls = Net_GetLocalPlayerId() ==
			  g_missionSetupPlayerAssignments
				  .teamPlayerIds[g_pilotData.team][0];
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		captainControls = 1;
	}
	expandedAtStart = g_missionSetupUseExpandedAssignmentLayout;
	if (expandedAtStart == 0) {
		slotStates[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slotStates[NAV_SLOT_PLAY] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[NAV_SLOT_STOP] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[NAV_SLOT_REWIND] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[NAV_SLOT_EXPANDED_AUTO_ASSIGN] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slotStates[NAV_SLOT_EXPANDED_CLEAR_LIST] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
	} else {
		slotStates[NAV_SLOT_PLAY] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slotStates[NAV_SLOT_STOP] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slotStates[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slotStates[NAV_SLOT_REWIND] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		if (captainControls != 0) {
			slotStates[NAV_SLOT_EXPANDED_AUTO_ASSIGN] =
				FRONTEND_NAVIGATION_SLOT_ACTIVE;
			slotStates[NAV_SLOT_EXPANDED_CLEAR_LIST] =
				FRONTEND_NAVIGATION_SLOT_ACTIVE;
		} else {
			slotStates[NAV_SLOT_EXPANDED_AUTO_ASSIGN] =
				FRONTEND_NAVIGATION_SLOT_INACTIVE;
			slotStates[NAV_SLOT_EXPANDED_CLEAR_LIST] =
				FRONTEND_NAVIGATION_SLOT_INACTIVE;
		}
	}

	slotStates[NAV_SLOT_FIRST_ASSIGNMENT] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slotStates[NAV_SLOT_SECOND_ASSIGNMENT] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	teamFlightGroups = g_teamPlayerFlightGroupCount[g_pilotData.team];
	if (teamFlightGroups > COMPACT_FLIGHT_GROUP_LIMIT) {
		slotStates[NAV_SLOT_FIRST_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[NAV_SLOT_SECOND_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[NAV_SLOT_FIRST_ASSIGNMENT +
			   g_missionSetupUseExpandedAssignmentLayout] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else if (captainControls != 0 &&
		   teamFlightGroups > SINGLE_FLIGHT_GROUP_COUNT) {
		slotStates[NAV_SLOT_FIRST_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slotStates[NAV_SLOT_SECOND_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
	}
	if (expandedAtStart == 0) {
		slotStates[g_briefingPlaybackActive ^ 1] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}

	FrontendDraw_RectAssign(&rect, BUTTON_LEFT, MAP_BUTTON_TOP,
				BUTTON_RIGHT, MAP_BUTTON_BOTTOM);
	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (!g_missionSetupUseExpandedAssignmentLayout &&
	    FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
	    (FrontendMouse_GetLeftDown() != 0 ||
	     FrontendMouse_GetRightDown() != 0 ||
	     FrontendMouse_GetLeftClick() != 0 ||
	     FrontendMouse_GetRightClick() != 0)) {
		slotStates[NAV_SLOT_REWIND] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	}

	if (g_teamPlayerFlightGroupCount[g_pilotData.team] >
	    COMPACT_FLIGHT_GROUP_LIMIT) {
		if (g_missionSetupUseExpandedAssignmentLayout == 1 &&
		    captainControls != 0) {
			FrontendDraw_RectAssign(
				&rect, BUTTON_LEFT, EXPANDED_TOP_BUTTON_TOP,
				BUTTON_RIGHT, EXPANDED_TOP_BUTTON_BOTTOM);
			for (slotIndex = 0; slotIndex < 2; ++slotIndex) {
				if (FrontendDraw_PointInRect(&rect, cursorX,
							     cursorY) &&
				    (FrontendMouse_GetLeftDown() != 0 ||
				     FrontendMouse_GetRightDown() != 0 ||
				     FrontendMouse_GetLeftClick() != 0 ||
				     FrontendMouse_GetRightClick() != 0)) {
					if (slotIndex == 0) {
						slotStates[NAV_SLOT_FIRST_ASSIGNMENT] =
							FRONTEND_NAVIGATION_SLOT_SELECTED;
					} else {
						slotStates[NAV_SLOT_SECOND_ASSIGNMENT] =
							FRONTEND_NAVIGATION_SLOT_SELECTED;
					}
				}
				FrontendDraw_RectOffsetXY(&rect, 0,
							  BUTTON_ROW_OFFSET);
			}
		}
	} else if (captainControls != 0 &&
		   slotStates[NAV_SLOT_FIRST_ASSIGNMENT] !=
			   FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		FrontendDraw_RectAssign(&rect, BUTTON_LEFT,
					COMPACT_ASSIGN_BUTTON_TOP, BUTTON_RIGHT,
					COMPACT_ASSIGN_BUTTON_BOTTOM);
		for (slotIndex = NAV_SLOT_FIRST_ASSIGNMENT;
		     slotIndex < NAV_SLOT_EXPANDED_CLEAR_LIST; ++slotIndex) {
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
			    (FrontendMouse_GetLeftDown() != 0 ||
			     FrontendMouse_GetRightDown() != 0 ||
			     FrontendMouse_GetLeftClick() != 0 ||
			     FrontendMouse_GetRightClick() != 0)) {
				slotStates[slotIndex] =
					FRONTEND_NAVIGATION_SLOT_SELECTED;
			}
			FrontendDraw_RectOffsetXY(&rect, 0, BUTTON_ROW_OFFSET);
		}
	}
	FrontendButton_DrawEightSlotNavigationState(slotStates);

	if (g_teamPlayerFlightGroupCount[g_pilotData.team] >
	    COMPACT_FLIGHT_GROUP_LIMIT) {
		FrontendDraw_RectAssign(&rect, BUTTON_LEFT, BOTTOM_BUTTON_TOP,
					BUTTON_RIGHT, BOTTOM_BUTTON_BOTTOM);
		if (g_missionSetupUseExpandedAssignmentLayout == 1) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "play2d",
				FrontendString_Get(FRONTSTR_799_ASSIGN_PLAYERS),
				BUTTON_FONT_SIZE, 0);
		} else if (FrontendButton_HandleSpriteButton(
				   &rect, "play2u", "play2u",
				   FrontendString_Get(
					   FRONTSTR_799_ASSIGN_PLAYERS),
				   BUTTON_FONT_SIZE, 0,
				   ASSIGN_PLAYERS_OR_CLEAR_LIST_HELD_SLOT,
				   "jewelsound") != 0) {
			g_missionSetupUseExpandedAssignmentLayout = 1;
			g_frontendFirstVisibleLine = 0;
			FrontendDisplay_LockOffscreenSurface();
			FrontImage_DrawSpriteOpaque("background", 0, 0);
			FrontImage_DrawSprite("frame", 0, 0);
			if (g_hostCdAvailable != 0) {
				FrontImage_DrawSprite("allactive", 0, 0);
			} else {
				FrontImage_DrawSprite("clientactive", 0, 0);
			}
			if (g_frontendMissionSessionMode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontImage_DrawSpriteTranslucent("chatbox", 0,
								 0);
			}
			FrontImage_DrawSpriteTranslucent("mapassignoverlay", 0,
							 0);
			cursorX = ASSIGNMENT_OVERLAY_X;
			cursorY = ASSIGNMENT_OVERLAY_Y;
			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int16_t)g_frontendMission.flightGroupCount;
			     ++flightGroupIndex) {
				const XvtFlightGroup *flightGroup;

				flightGroup = &g_frontendMission.flightGroups
						       [flightGroupIndex];
				if (flightGroup->playerNumber != 0 &&
				    flightGroup->team == g_pilotData.team) {
					FrontImage_DrawSpriteTranslucent(
						"fgslotoverlay", cursorX,
						cursorY);
					cursorY +=
						ASSIGNMENT_OVERLAY_ROW_HEIGHT;
				}
			}
			FrontendDisplay_UnlockOffscreenSurface(1);
		}

		FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_ROW_OFFSET);
		if (g_missionSetupUseExpandedAssignmentLayout == 0) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "play1d",
				FrontendString_Get(
					FRONTSTR_800_VIEW_BRIEFING_MAP),
				BUTTON_FONT_SIZE, 0);
		} else {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "play1u", "play1u",
				    FrontendString_Get(
					    FRONTSTR_800_VIEW_BRIEFING_MAP),
				    BUTTON_FONT_SIZE, 0,
				    BRIEFING_MAP_OR_AUTO_ASSIGN_HELD_SLOT,
				    "jewelsound") != 0) {
				g_missionSetupUseExpandedAssignmentLayout = 0;
				FrontendDisplay_LockOffscreenSurface();
				FrontImage_DrawSpriteOpaque("background", 0, 0);
				FrontImage_DrawSprite("frame", 0, 0);
				if (g_hostCdAvailable != 0) {
					FrontImage_DrawSprite("allactive", 0,
							      0);
				} else {
					FrontImage_DrawSprite("clientactive", 0,
							      0);
				}
				if (g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontImage_DrawSpriteTranslucent(
						"chatbox", 0, 0);
				}
				FrontImage_DrawSpriteTranslucent("mapoverlay",
								 0, 0);
				FrontendDraw_RectCopy(&rect,
						      &g_briefingMapPanelRect);
				FrontendDraw_RectOffsetXY(
					&rect, BRIEFING_MAP_OFFSET_X,
					BRIEFING_MAP_OFFSET_Y);
				rect.top =
					rect.bottom - BRIEFING_NARRATION_HEIGHT;
				FrontendDraw_FillRectTranslucent(
					&rect, 0, 0, (unsigned int)g_colorBlue);
				FrontendDisplay_UnlockOffscreenSurface(1);
				FrontendCursor_SetPos(BRIEFING_CURSOR_X,
						      BRIEFING_CURSOR_Y);
			}
		}
	} else if (captainControls != 0 &&
		   slotStates[NAV_SLOT_FIRST_ASSIGNMENT] !=
			   FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		FrontendDraw_RectAssign(&rect, BUTTON_LEFT, BOTTOM_BUTTON_TOP,
					BUTTON_RIGHT, BOTTOM_BUTTON_BOTTOM);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "clearu", "cleard",
			    FrontendString_Get(FRONTSTR_215_CLEAR_LIST),
			    BUTTON_FONT_SIZE, 0,
			    ASSIGN_PLAYERS_OR_CLEAR_LIST_HELD_SLOT,
			    "jewelsound") != 0) {
			MissionSetup_ClearFlightAssignments();
		}
		FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_ROW_OFFSET);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "assignu", "assignd",
			    FrontendString_Get(FRONTSTR_214_AUTO_ASSIGN),
			    BUTTON_FONT_SIZE, 0,
			    BRIEFING_MAP_OR_AUTO_ASSIGN_HELD_SLOT,
			    "jewelsound") != 0) {
			MissionSetup_RandomizeFlightAssignments();
		}
	}

	if (g_missionSetupUseExpandedAssignmentLayout == 0) {
		FrontendDraw_RectAssign(&rect, BUTTON_LEFT, MAP_BUTTON_TOP,
					BUTTON_RIGHT, MAP_BUTTON_BOTTOM);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "map4u", "map4d",
			    FrontendString_Get(FRONTSTR_210_REWIND),
			    BUTTON_FONT_SIZE, 0, REWIND_HELD_SLOT,
			    "jewelsound") != 0) {
			g_briefingLastNarratedTextBlockIdx = 0;
			g_briefingTextPageNumber = 0;
			BriefingScript_ResetState();
		}
		FrontendDraw_RectOffsetXY(&rect, 0, REWIND_TO_STOP_OFFSET);
		if (g_briefingPlaybackActive == 0) {
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "map2d",
				FrontendString_Get(FRONTSTR_208_STOP),
				BUTTON_FONT_SIZE, 0);
		} else {
			if (FrontendButton_HandleSpriteButton(
				    &rect, "map2u", "map2u",
				    FrontendString_Get(FRONTSTR_208_STOP),
				    BUTTON_FONT_SIZE, 0,
				    STOP_OR_CLEAR_LIST_HELD_SLOT,
				    "jewelsound") != 0) {
				g_briefingPlaybackActive = 0;
			}
		}
		FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_ROW_OFFSET);
		if (g_briefingPlaybackActive != 0) {
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
			    (FrontendMouse_GetLeftClick() != 0 ||
			     FrontendMouse_GetRightClick() != 0)) {
				if (g_gameConfig.sfxDatapadEnabled != 0) {
					FrontendSound_PlayUISound(
						"jewelsound", 1, 0,
						UI_SOUND_PRIORITY,
						UI_SOUND_VOLUME_SCALE *
							g_gameConfig
								.sfxDatapadVolume,
						UI_SOUND_PAN_CENTER);
				}
				BriefingScript_AdvanceToNextVisibleLine();
				if (g_briefingTextSlotBlockIdx[1] ==
				    g_briefingLastNarratedTextBlockIdx) {
					g_briefingLastNarratedTextBlockIdx = 0;
					g_briefingTextPageNumber = 0;
					BriefingScript_ResetState();
				}
			}
			FrontendButton_DrawSpriteAndTooltip(
				&rect, "map1d",
				FrontendString_Get(FRONTSTR_209_FORWARD),
				BUTTON_FONT_SIZE, 0);
		} else if (FrontendButton_HandleSpriteButton(
				   &rect, "map1u", "map1u",
				   FrontendString_Get(FRONTSTR_561_PLAY),
				   BUTTON_FONT_SIZE, 0,
				   PLAY_OR_AUTO_ASSIGN_HELD_SLOT,
				   "jewelsound") != 0) {
			g_briefingPlaybackActive = 1;
		}
	} else if (captainControls != 0 &&
		   slotStates[NAV_SLOT_EXPANDED_AUTO_ASSIGN] !=
			   FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		FrontendDraw_RectAssign(
			&rect, BUTTON_LEFT, EXPANDED_SECOND_BUTTON_TOP,
			BUTTON_RIGHT, EXPANDED_SECOND_BUTTON_BOTTOM);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "clear2u", "clear2d",
			    FrontendString_Get(FRONTSTR_215_CLEAR_LIST),
			    BUTTON_FONT_SIZE, 0, STOP_OR_CLEAR_LIST_HELD_SLOT,
			    "jewelsound") != 0) {
			MissionSetup_ClearFlightAssignments();
		}
		FrontendDraw_RectOffsetXY(&rect, 0, -BUTTON_ROW_OFFSET);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "assign2u", "assign2d",
			    FrontendString_Get(FRONTSTR_214_AUTO_ASSIGN),
			    BUTTON_FONT_SIZE, 0, PLAY_OR_AUTO_ASSIGN_HELD_SLOT,
			    "jewelsound") != 0) {
			MissionSetup_RandomizeFlightAssignments();
		}
	}
	return 0;
}

/* Draws the flight assignment table of the pilot's team and handles drags and
 * drops on it: the duty roster of the team's player flight groups (number,
 * craft and name in a color by IFF, and the first order's designation or the
 * General text), each with its assigned pilot, and up to 4 of the team's
 * players who have no flight group, gray when reserved. With more than 4 player
 * flight groups the table sits 68 pixels higher. With more than one, a help
 * line tells the team's captain (the local player in slot 0 of
 * g_pilotData.team) or a solo player to drag names into pilot slots, and others
 * to wait. The captain or a solo player pressing a mouse button on an
 * unassigned player starts a drag (input gate 3); with more than one player
 * flight group, so does a press on a flight group's pilot, and a solo game then
 * takes that pilot's flight group away (-1). A captain drags only a player not
 * reserved, reserving it and sending every player a FLIGHT_RESERVATION (the
 * player and the local id) and a FLIGHT_ASSIGNMENT_NOTIFY clearing each of the
 * team's slots that holds the player with a flight group. With more than one
 * player flight group, a click while dragging on a flight group's pilot cell
 * drops the player there: a solo game sets the dragged player's
 * g_missionSetupPlayerFlightGroupIndices entry to that flight group, clearing
 * the pilot it replaces; a captain instead sends FLIGHT_ASSIGNMENT_NOTIFY
 * packets, clearing the replaced pilot and then assigning the dragged player's
 * slot, and a RELEASE_FLIGHT_RESERVATION. Either way the drag ends. Returns
 * 1. */
// FUNCTION: XVT 0x4F9750
int MissionSetup_DrawFlightAssignments(int frameCounter)
{
	enum {
		PLAYER_SLOTS_PER_TEAM = 8,
		MAX_UNASSIGNED_ROWS = 4,
		INPUT_GATE = 3,
		HEADER_Y = 335,
		LIST_Y = 352,
		COLUMN_DUTY = 88,
		COLUMN_ASSIGNED = 232,
		COLUMN_UNASSIGNED = 339,
		ROW_HEIGHT = 17,
		FONT_SIZE = 12,
		UI_SOUND_PRIORITY = 255,
		UI_SOUND_VOLUME_SCALE = 12,
		UI_SOUND_PAN_CENTER = 63,
	};

	RECT rect;
	RECT playerRect;
	RECT savedClipRect;
	int yOffset;
	int isTeamCaptain;
	int teamPlayerIndex;
	int cursorX;
	int cursorY;
	int flightGroupIndex;

	yOffset = g_teamPlayerFlightGroupCount[g_pilotData.team] > 4 ? -68 : 0;
	FrontendDraw_RectAssign(&rect, COLUMN_DUTY, HEADER_Y + yOffset, 229,
				HEADER_Y + yOffset + 14);
	FrontendText_DrawAlignedInRect(
		FONT_SIZE, FrontendString_Get(FRONTSTR_627_DUTY_ROSTER), &rect,
		0, 1, 0xFFFF);
	FrontendDraw_RectAssign(&rect, COLUMN_ASSIGNED, HEADER_Y + yOffset, 336,
				HEADER_Y + yOffset + 14);
	FrontendText_DrawAlignedInRect(
		FONT_SIZE, FrontendString_Get(FRONTSTR_628_ASSIGNED_PILOTS),
		&rect, 0, 1, 0xFFFF);
	FrontendDraw_RectAssign(&rect, COLUMN_UNASSIGNED, HEADER_Y + yOffset,
				443, HEADER_Y + yOffset + 14);
	FrontendText_DrawAlignedInRect(
		FONT_SIZE, FrontendString_Get(FRONTSTR_685_UNASSIGNED_PLAYERS),
		&rect, 0, 1, 0xFFFF);
	isTeamCaptain = Net_GetLocalPlayerId() ==
			g_missionSetupPlayerAssignments
				.teamPlayerIds[g_pilotData.team][0];
	if (g_teamPlayerFlightGroupCount[g_pilotData.team] > 1) {
		const char *instructionText;
		int instructionColor;

		FrontendDraw_RectAssign(&rect, COLUMN_DUTY, 422, 443, 436);
		if (isTeamCaptain ||
		    g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			instructionColor = g_colorGreen;
			instructionText = FrontendString_Get(
				FRONTSTR_629_ASSIGN_BY_DRAGGING_PLAYERS_NAMES_INTO_PILOT_SLOTS);
		} else {
			instructionColor = g_colorRed;
			instructionText = FrontendString_Get(
				FRONTSTR_630_PLEASE_WAIT_WHILE_THE_TEAM_CAPTAIN_ASSIGNS_FLIGHT_GROUPS);
		}
		FrontendText_DrawCentered(FONT_SIZE, instructionText, &rect,
					  instructionColor);
	}

	{
		int displayedPlayerCount;

		displayedPlayerCount = 0;
		FrontendDraw_RectAssign(&rect, COLUMN_UNASSIGNED,
					LIST_Y + yOffset, 443,
					LIST_Y + yOffset + 16);
		for (teamPlayerIndex = 0;
		     teamPlayerIndex <
		     g_teamPlayerFlightGroupCount[g_pilotData.team];
		     ++teamPlayerIndex) {
			int assignmentIndex;
			int playerId;
			int rosterIndex;
			int reservedIndex;

			assignmentIndex =
				g_pilotData.team * PLAYER_SLOTS_PER_TEAM +
				teamPlayerIndex;
			playerId = g_missionSetupPlayerAssignments
					   .teamPlayerIds[g_pilotData.team]
							 [teamPlayerIndex];
			for (rosterIndex = 0;
			     rosterIndex < PLAYER_SLOTS_PER_TEAM;
			     ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId ==
				    playerId) {
					break;
				}
			}
			if (rosterIndex == PLAYER_SLOTS_PER_TEAM ||
			    g_missionSetupPlayerFlightGroupIndices
					    [assignmentIndex] != -1 ||
			    g_missionSetupDraggedPlayerId == playerId ||
			    playerId == 0) {
				continue;
			}

			for (reservedIndex = 0;
			     reservedIndex < g_missionSetupReservedPlayerCount;
			     ++reservedIndex) {
				if (g_missionSetupReservedPlayerIds
					    [reservedIndex] == playerId) {
					break;
				}
			}
			FrontendDisplay_GetScreenClipRect(&savedClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rect);
			if (reservedIndex ==
			    g_missionSetupReservedPlayerCount) {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				if (Net_GetLocalPlayerId() == playerId ||
				    g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontendText_DrawAlignedInRect(
						FONT_SIZE,
						g_frontendScratchBuffer, &rect,
						0, 1,
						g_pulseColorRamp
							[((frameCounter % 24) &
							  ~1) >>
							 1]);
				} else {
					FrontendText_DrawAlignedInRect(
						FONT_SIZE,
						g_frontendScratchBuffer, &rect,
						0, 1, g_colorYellow);
				}
			} else {
				sprintf(g_frontendScratchBuffer, "%s %s",
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					g_mpRoster[rosterIndex].name);
				FrontendText_DrawAlignedInRect(
					FONT_SIZE, g_frontendScratchBuffer,
					&rect, 0, 1, g_colorGray);
			}
			FrontendDisplay_SetScreenClipRect640x480(
				&savedClipRect);
			++displayedPlayerCount;

			if ((Net_GetLocalPlayerId() ==
				     g_missionSetupPlayerAssignments
					     .teamPlayerIds[g_pilotData.team]
							   [0] ||
			     g_frontendMissionSessionMode ==
				     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
			    !FrontendMouse_IsGateOwner(INPUT_GATE)) {
				FrontendCursor_GetPos(&cursorX, &cursorY);
				if ((FrontendMouse_GetLeftDown() != 0 ||
				     FrontendMouse_GetRightDown() != 0) &&
				    FrontendDraw_PointInRect(&rect, cursorX,
							     cursorY)) {
					if (g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_missionSetupDraggedPlayerId =
							playerId;
						FrontendMouse_SetInputGate(
							INPUT_GATE);
					} else {
						for (reservedIndex = 0;
						     reservedIndex <
						     g_missionSetupReservedPlayerCount;
						     ++reservedIndex) {
							if (g_missionSetupReservedPlayerIds
								    [reservedIndex] ==
							    playerId) {
								break;
							}
						}
						if (reservedIndex ==
						    g_missionSetupReservedPlayerCount) {
							g_missionSetupReservedPlayerIds
								[reservedIndex] =
									playerId;
							++g_missionSetupReservedPlayerCount;
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_FLIGHT_RESERVATION;
							*(int *)g_frontendNetPacketScratch
								 .payload =
								playerId;
							/* playerId now holds the local player's id, sent as the player making the
							 * reservation; the dragged player's id is read back from the roster below. */
							playerId =
								Net_GetLocalPlayerId();
							*(int *)(g_frontendNetPacketScratch
									 .payload +
								 sizeof(int)) =
								playerId;
							Net_SendPacketAndFlush(
								0,
								&g_frontendNetPacketScratch,
								3 * sizeof(int));
							g_missionSetupDraggedPlayerId =
								g_mpRoster[rosterIndex]
									.playerId;
							FrontendMouse_SetInputGate(
								INPUT_GATE);
							/* reservedIndex walks this team's player slots here, not the reserved list. */
							for (reservedIndex = 0;
							     reservedIndex <
							     g_teamPlayerFlightGroupCount
								     [g_pilotData
									      .team];
							     ++reservedIndex) {
								assignmentIndex =
									g_pilotData.team *
										PLAYER_SLOTS_PER_TEAM +
									reservedIndex;
								if (g_missionSetupPlayerAssignments
										    .teamPlayerIds
											    [g_pilotData
												     .team]
											    [reservedIndex] ==
									    g_missionSetupDraggedPlayerId &&
								    g_missionSetupPlayerFlightGroupIndices
										    [assignmentIndex] !=
									    -1) {
									g_frontendNetPacketScratch
										.packetType =
										NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
									*(int *)g_frontendNetPacketScratch
										 .payload =
										g_pilotData
											.team;
									*(int *)(g_frontendNetPacketScratch
											 .payload +
										 sizeof(int)) =
										reservedIndex;
									*(int *)(g_frontendNetPacketScratch
											 .payload +
										 2 * sizeof(int)) =
										-1;
									Net_SendPacketAndFlush(
										0,
										&g_frontendNetPacketScratch,
										5 * sizeof(int));
								}
							}
						}
					}
				}
			}
			FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
			if (displayedPlayerCount >= MAX_UNASSIGNED_ROWS) {
				break;
			}
		}
	}

	{
		int displayedFlightGroupCount;

		displayedFlightGroupCount = 0;
		FrontendCursor_GetPos(&cursorX, &cursorY);
		FrontendDraw_RectAssign(&rect, COLUMN_DUTY, LIST_Y + yOffset,
					229, LIST_Y + yOffset + 16);
		FrontendDraw_RectAssign(&playerRect, COLUMN_ASSIGNED,
					LIST_Y + yOffset, 332,
					LIST_Y + yOffset + 16);
		for (flightGroupIndex = 0;
		     flightGroupIndex <
		     (int)(int16_t)g_frontendMission.flightGroupCount;
		     ++flightGroupIndex) {
			XvtFlightGroup *flightGroup;
			uint8_t iffColorCode;
			const char *designation;
			int assignedTeamPlayerIndex;
			int rosterIndex;

			flightGroup = &g_frontendMission
					       .flightGroups[flightGroupIndex];
			if (flightGroup->playerNumber == 0 ||
			    flightGroup->team != g_pilotData.team) {
				continue;
			}
			switch (flightGroup->iff) {
			case 0:
				iffColorCode = 2;
				break;
			case 1:
			case 4:
				iffColorCode = 3;
				break;
			case 2:
				iffColorCode = 5;
				break;
			case 3:
				iffColorCode = 4;
				break;
			case 5:
				iffColorCode = 6;
				break;
			default:
				iffColorCode = 1;
				break;
			}
			sprintf(g_frontendScratchBuffer, "%u. %c%s %s: %c",
				displayedFlightGroupCount + 1, iffColorCode,
				FrontendString_Get(
					(FrontendStringId)(flightGroup
								   ->craftType +
							   609)),
				flightGroup->name, 1);
			designation =
				flightGroup->orders[0].designation[0] != '\0'
					? flightGroup->orders[0].designation
					: FrontendString_Get(
						  FRONTSTR_626_GENERAL);
			strcat(g_frontendScratchBuffer, designation);
			FrontendText_DrawAlignedInRect(FONT_SIZE,
						       g_frontendScratchBuffer,
						       &rect, 0, 1, 0xFFFF);

			assignedTeamPlayerIndex = -1;
			for (teamPlayerIndex = 0;
			     teamPlayerIndex <
			     g_teamPlayerFlightGroupCount[g_pilotData.team];
			     ++teamPlayerIndex) {
				int assignmentIndex;

				assignmentIndex =
					g_pilotData.team *
						PLAYER_SLOTS_PER_TEAM +
					teamPlayerIndex;
				if (g_missionSetupPlayerFlightGroupIndices
					    [assignmentIndex] !=
				    flightGroupIndex) {
					continue;
				}
				assignedTeamPlayerIndex = teamPlayerIndex;
				for (rosterIndex = 0;
				     rosterIndex < PLAYER_SLOTS_PER_TEAM;
				     ++rosterIndex) {
					if (g_mpRoster[rosterIndex].playerId ==
					    g_missionSetupPlayerAssignments
						    .teamPlayerIds
							    [g_pilotData.team]
							    [teamPlayerIndex]) {
						break;
					}
				}
				if (rosterIndex == PLAYER_SLOTS_PER_TEAM) {
					continue;
				}

				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get((
						FrontendStringId)(g_mpRoster[rosterIndex]
									  .pilotRating +
								  154)),
					1, g_mpRoster[rosterIndex].name);
				FrontendDisplay_GetScreenClipRect(
					&savedClipRect);
				FrontendDisplay_SetScreenClipRect640x480(
					&playerRect);
				if (Net_GetLocalPlayerId() ==
					    g_mpRoster[rosterIndex].playerId ||
				    g_frontendMissionSessionMode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					FrontendText_DrawAlignedInRect(
						FONT_SIZE,
						g_frontendScratchBuffer,
						&playerRect, 0, 1,
						g_pulseColorRamp
							[((frameCounter % 24) &
							  ~1) >>
							 1]);
				} else {
					FrontendText_DrawAlignedInRect(
						FONT_SIZE,
						g_frontendScratchBuffer,
						&playerRect, 0, 1,
						g_colorYellow);
				}
				FrontendDisplay_SetScreenClipRect640x480(
					&savedClipRect);
			}

			if (g_teamPlayerFlightGroupCount[g_pilotData.team] >
			    1) {
				if (assignedTeamPlayerIndex == -1) {
					if (FrontendMouse_IsGateOwner(
						    INPUT_GATE) &&
					    (FrontendMouse_GetLeftClickFor(
						     INPUT_GATE) != 0 ||
					     FrontendMouse_GetRightClickFor(
						     INPUT_GATE) != 0) &&
					    FrontendDraw_PointInRect(
						    &playerRect, cursorX,
						    cursorY)) {
						if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							int draggedTeamPlayerIndex;

							for (draggedTeamPlayerIndex =
								     0;
							     draggedTeamPlayerIndex <
							     g_teamPlayerFlightGroupCount
								     [g_pilotData
									      .team];
							     ++draggedTeamPlayerIndex) {
								if (g_missionSetupPlayerAssignments
									    .teamPlayerIds
										    [g_pilotData
											     .team]
										    [draggedTeamPlayerIndex] ==
								    g_missionSetupDraggedPlayerId) {
									break;
								}
							}
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
							*(int *)g_frontendNetPacketScratch
								 .payload =
								g_pilotData
									.team;
							*(int *)(g_frontendNetPacketScratch
									 .payload +
								 sizeof(int)) =
								draggedTeamPlayerIndex;
							*(int *)(g_frontendNetPacketScratch
									 .payload +
								 2 * sizeof(int)) =
								flightGroupIndex;
							Net_SendPacketAndFlush(
								0,
								&g_frontendNetPacketScratch,
								4 * sizeof(int));
							g_frontendNetPacketScratch
								.packetType =
								NET_PACKET_RELEASE_FLIGHT_RESERVATION;
							*(int *)g_frontendNetPacketScratch
								 .payload =
								g_missionSetupDraggedPlayerId;
							Net_SendPacketAndFlush(
								0,
								&g_frontendNetPacketScratch,
								2 * sizeof(int));
						} else {
							int draggedTeamPlayerIndex;

							if (g_gameConfig
								    .sfxDatapadEnabled !=
							    0) {
								FrontendSound_PlayUISound(
									"slotsound",
									1, 0,
									UI_SOUND_PRIORITY,
									UI_SOUND_VOLUME_SCALE *
										g_gameConfig
											.sfxDatapadVolume,
									UI_SOUND_PAN_CENTER);
							}
							for (draggedTeamPlayerIndex =
								     0;
							     draggedTeamPlayerIndex <
							     g_teamPlayerFlightGroupCount
								     [g_pilotData
									      .team];
							     ++draggedTeamPlayerIndex) {
								if (g_missionSetupPlayerAssignments
									    .teamPlayerIds
										    [g_pilotData
											     .team]
										    [draggedTeamPlayerIndex] ==
								    g_missionSetupDraggedPlayerId) {
									break;
								}
							}
							if (draggedTeamPlayerIndex <
							    g_teamPlayerFlightGroupCount
								    [g_pilotData
									     .team]) {
								g_missionSetupPlayerFlightGroupIndices
									[g_pilotData.team *
										 PLAYER_SLOTS_PER_TEAM +
									 draggedTeamPlayerIndex] =
										flightGroupIndex;
							}
						}
						g_missionSetupDraggedPlayerId =
							0;
						FrontendMouse_ClearInputGate();
					}
				} else if (!FrontendMouse_IsGateOwner(
						   INPUT_GATE)) {
					if ((isTeamCaptain ||
					     g_frontendMissionSessionMode ==
						     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
					    (FrontendMouse_GetLeftDown() != 0 ||
					     FrontendMouse_GetRightDown() !=
						     0) &&
					    FrontendDraw_PointInRect(
						    &playerRect, cursorX,
						    cursorY)) {
						int playerId;
						int reservedIndex;

						playerId =
							g_missionSetupPlayerAssignments
								.teamPlayerIds
									[g_pilotData
										 .team]
									[assignedTeamPlayerIndex];
						if (g_frontendMissionSessionMode ==
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							g_missionSetupDraggedPlayerId =
								playerId;
							FrontendMouse_SetInputGate(
								INPUT_GATE);
							g_missionSetupPlayerFlightGroupIndices
								[g_pilotData.team *
									 PLAYER_SLOTS_PER_TEAM +
								 assignedTeamPlayerIndex] =
									-1;
						} else {
							for (reservedIndex = 0;
							     reservedIndex <
							     g_missionSetupReservedPlayerCount;
							     ++reservedIndex) {
								if (g_missionSetupReservedPlayerIds
									    [reservedIndex] ==
								    playerId) {
									break;
								}
							}
							if (reservedIndex ==
							    g_missionSetupReservedPlayerCount) {
								g_missionSetupReservedPlayerIds
									[reservedIndex] =
										playerId;
								++g_missionSetupReservedPlayerCount;
								g_frontendNetPacketScratch
									.packetType =
									NET_PACKET_FLIGHT_RESERVATION;
								*(int *)g_frontendNetPacketScratch
									 .payload =
									playerId;
								/* rosterIndex is reused here for the local player's id, sent as the player
								 * making the reservation. */
								rosterIndex =
									Net_GetLocalPlayerId();
								*(int *)(g_frontendNetPacketScratch
										 .payload +
									 sizeof(int)) =
									rosterIndex;
								Net_SendPacketAndFlush(
									0,
									&g_frontendNetPacketScratch,
									3 * sizeof(int));
								g_missionSetupDraggedPlayerId =
									playerId;
								FrontendMouse_SetInputGate(
									INPUT_GATE);
								/* reservedIndex walks this team's player slots here, not the reserved
								 * list. */
								for (reservedIndex =
									     0;
								     reservedIndex <
								     g_teamPlayerFlightGroupCount
									     [g_pilotData
										      .team];
								     ++reservedIndex) {
									int assignmentIndex;

									assignmentIndex =
										g_pilotData.team *
											PLAYER_SLOTS_PER_TEAM +
										reservedIndex;
									if (g_missionSetupPlayerAssignments
											    .teamPlayerIds
												    [g_pilotData
													     .team]
												    [reservedIndex] ==
										    g_missionSetupDraggedPlayerId &&
									    g_missionSetupPlayerFlightGroupIndices
											    [assignmentIndex] !=
										    -1) {
										g_frontendNetPacketScratch
											.packetType =
											NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
										*(int *)g_frontendNetPacketScratch
											 .payload =
											g_pilotData
												.team;
										*(int *)(g_frontendNetPacketScratch
												 .payload +
											 sizeof(int)) =
											reservedIndex;
										*(int *)(g_frontendNetPacketScratch
												 .payload +
											 2 * sizeof(int)) =
											-1;
										Net_SendPacketAndFlush(
											0,
											&g_frontendNetPacketScratch,
											5 * sizeof(int));
									}
								}
							}
						}
					}
				} else if ((FrontendMouse_GetLeftClickFor(
						    INPUT_GATE) != 0 ||
					    FrontendMouse_GetRightClickFor(
						    INPUT_GATE) != 0) &&
					   FrontendDraw_PointInRect(&playerRect,
								    cursorX,
								    cursorY)) {
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						int draggedTeamPlayerIndex;

						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
						*(int *)g_frontendNetPacketScratch
							 .payload =
							g_pilotData.team;
						*(int *)(g_frontendNetPacketScratch
								 .payload +
							 sizeof(int)) =
							assignedTeamPlayerIndex;
						*(int *)(g_frontendNetPacketScratch
								 .payload +
							 2 * sizeof(int)) = -1;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							5 * sizeof(int));
						for (draggedTeamPlayerIndex = 0;
						     draggedTeamPlayerIndex <
						     g_teamPlayerFlightGroupCount
							     [g_pilotData.team];
						     ++draggedTeamPlayerIndex) {
							if (g_missionSetupPlayerAssignments
								    .teamPlayerIds
									    [g_pilotData
										     .team]
									    [draggedTeamPlayerIndex] ==
							    g_missionSetupDraggedPlayerId) {
								break;
							}
						}
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
						*(int *)g_frontendNetPacketScratch
							 .payload =
							g_pilotData.team;
						*(int *)(g_frontendNetPacketScratch
								 .payload +
							 sizeof(int)) =
							draggedTeamPlayerIndex;
						*(int *)(g_frontendNetPacketScratch
								 .payload +
							 2 * sizeof(int)) =
							flightGroupIndex;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							4 * sizeof(int));
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_RELEASE_FLIGHT_RESERVATION;
						*(int *)g_frontendNetPacketScratch
							 .payload =
							g_missionSetupDraggedPlayerId;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							2 * sizeof(int));
					} else {
						int draggedPlayerId;
						int draggedTeamPlayerIndex;

						if (g_gameConfig
							    .sfxDatapadEnabled !=
						    0) {
							FrontendSound_PlayUISound(
								"slotsound", 1,
								0,
								UI_SOUND_PRIORITY,
								UI_SOUND_VOLUME_SCALE *
									g_gameConfig
										.sfxDatapadVolume,
								UI_SOUND_PAN_CENTER);
						}
						draggedPlayerId =
							g_missionSetupDraggedPlayerId;
						g_missionSetupPlayerFlightGroupIndices
							[g_pilotData.team *
								 PLAYER_SLOTS_PER_TEAM +
							 assignedTeamPlayerIndex] =
								-1;
						for (draggedTeamPlayerIndex = 0;
						     draggedTeamPlayerIndex <
						     g_teamPlayerFlightGroupCount
							     [g_pilotData.team];
						     ++draggedTeamPlayerIndex) {
							if (g_missionSetupPlayerAssignments
								    .teamPlayerIds
									    [g_pilotData
										     .team]
									    [draggedTeamPlayerIndex] ==
							    draggedPlayerId) {
								break;
							}
						}
						if (draggedTeamPlayerIndex <
						    g_teamPlayerFlightGroupCount
							    [g_pilotData
								     .team]) {
							g_missionSetupPlayerFlightGroupIndices
								[g_pilotData.team *
									 PLAYER_SLOTS_PER_TEAM +
								 draggedTeamPlayerIndex] =
									flightGroupIndex;
						}
					}
					g_missionSetupDraggedPlayerId = 0;
					FrontendMouse_ClearInputGate();
				}
			}

			FrontendDraw_RectOffsetXY(&playerRect, 0, ROW_HEIGHT);
			FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
			++displayedFlightGroupCount;
		}
	}
	return 1;
}

/* Takes out of the team and flight assignments every assignedPlayerIds entry
 * that is not the playerId of a g_mpRoster entry (a 0 entry stays while some
 * roster entry is empty): the id is removed from each slot of the first
 * g_teamCount teams that holds it, the later slots and their
 * g_missionSetupPlayerFlightGroupIndices entries shifting down and slot 7
 * becoming 0 with flight group -1, and the entry is set to 0. Outside a solo
 * game it also calls Net_CountReadyPlayers and drops the result. */
// FUNCTION: XVT 0x4FA420
void MissionSetup_PruneFlightAssignments(void)
{
	int *rosterPlayerIdPtr;
	int *activePlayerIdPtr;
	unsigned int rosterIndex;
	int removedPlayerId;
	unsigned int teamByteOffset;
	int teamsRemaining;
	int playerByteOffset;
	int shiftByteOffset;
	int shiftRemaining;
	int shiftedPlayerId;
	int activePlayerIndex;

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		Net_CountReadyPlayers();
	}

	activePlayerIndex = 0;
	do {
		activePlayerIdPtr =
			&g_missionSetupPlayerAssignments
				 .assignedPlayerIds[activePlayerIndex];
		rosterIndex = 0;
		rosterPlayerIdPtr = &g_mpRoster[0].playerId;
		removedPlayerId = *activePlayerIdPtr;
		do {
			if (*rosterPlayerIdPtr == removedPlayerId) {
				break;
			}
			rosterPlayerIdPtr = (int *)((char *)rosterPlayerIdPtr +
						    sizeof(MpRosterEntry));
			++rosterIndex;
		} while (rosterIndex < 8);

		if (rosterIndex == 8) {
			if (g_teamCount > 0) {
				teamByteOffset = 0;
				teamsRemaining = g_teamCount;
				do {
					/* rosterIndex counts the player slots within a team here, alongside playerByteOffset,
					 * not roster entries. */
					rosterIndex = 0;
					playerByteOffset = teamByteOffset;
					do {
						if (*(int *)((uint8_t *)g_missionSetupPlayerAssignments
								     .teamPlayerIds +
							     playerByteOffset) ==
						    removedPlayerId) {
							if ((int)rosterIndex <
							    7) {
								shiftByteOffset =
									playerByteOffset;
								shiftRemaining =
									7 -
									(int)rosterIndex;
								do {
									shiftedPlayerId = *(
										int *)((uint8_t *)g_missionSetupPlayerAssignments
											       .teamPlayerIds +
										       shiftByteOffset +
										       sizeof(int));
									*(int *)((uint8_t *)g_missionSetupPlayerAssignments
											 .teamPlayerIds +
										 shiftByteOffset) =
										shiftedPlayerId;
									*(int *)((uint8_t *)
											 g_missionSetupPlayerFlightGroupIndices +
										 shiftByteOffset) =
										*(int *)((uint8_t *)
												 g_missionSetupPlayerFlightGroupIndices +
											 shiftByteOffset +
											 sizeof(int));
									shiftByteOffset +=
										sizeof(int);
									--shiftRemaining;
								} while (
									shiftRemaining !=
									0);
							}
							*(int *)((uint8_t *)g_missionSetupPlayerAssignments
									 .teamPlayerIds +
								 teamByteOffset +
								 7 * sizeof(int)) =
								0;
							*(int *)((uint8_t *)
									 g_missionSetupPlayerFlightGroupIndices +
								 teamByteOffset +
								 7 * sizeof(int)) =
								-1;
						}
						playerByteOffset += sizeof(int);
						++rosterIndex;
					} while ((int)rosterIndex < 8);
					teamByteOffset += 8 * sizeof(int);
					--teamsRemaining;
				} while (teamsRemaining != 0);
			}
			*activePlayerIdPtr = 0;
		}
		++activePlayerIndex;
	} while (activePlayerIndex < 8);
}

/* Tells whether every player of the pilot's team has a flight group: returns 0
 * when one of the team's first g_teamPlayerFlightGroupCount slots holds a
 * player whose g_missionSetupPlayerFlightGroupIndices entry is -1, else 1. It
 * runs the same test of the pilot's team once per team, g_teamCount times, and
 * checks no other team. */
// FUNCTION: XVT 0x4FA500
int MissionSetup_AreFlightAssignmentsComplete(void)
{
	int teamIndex;
	int playerIndex;
	int playerCount;
	int assignmentIndex;

	teamIndex = 0;
	if (g_teamCount > 0) {
		playerCount = g_teamPlayerFlightGroupCount[g_pilotData.team];
		do {
			playerIndex = 0;
			if (playerCount > 0) {
				do {
					assignmentIndex = 8 * g_pilotData.team +
							  playerIndex;
					if (g_missionSetupPlayerAssignments.teamPlayerIds
							    [g_pilotData.team]
							    [playerIndex] !=
						    0 &&
					    g_missionSetupPlayerFlightGroupIndices
							    [assignmentIndex] ==
						    -1) {
						return 0;
					}
				} while (++playerIndex < playerCount);
			}
		} while (++teamIndex < g_teamCount);
	}
	return 1;
}

/* Gives each player of the pilot's team a random player flight group of the
 * team, when slot 0 holds a player. For each slot in turn, until one holds 0,
 * it draws rand() % 9 + 1 and steps that many times through the team's player
 * flight groups not given to an earlier slot, in mission order and cycling, and
 * gives the slot the one it stops on. Outside a solo game it then sends every
 * player a FLIGHT_ASSIGNMENTS packet with the team and its eight
 * g_missionSetupPlayerFlightGroupIndices entries. Returns 1. Does not check
 * that the team has a free flight group for each player (the stepping then
 * never ends), or stop after slot 7 when all 8 slots hold players: it then goes
 * on into the next team's row. */
// FUNCTION: XVT 0x4FA570
int MissionSetup_RandomizeFlightAssignments(void)
{
	enum { PLAYER_SLOTS_PER_TEAM = 8, FLIGHT_GROUP_SELECTION_COUNT = 9 };

	int assignedPlayers;
	int team;

	assignedPlayers = 0;
	team = g_pilotData.team;
	if (g_missionSetupPlayerAssignments.teamPlayerIds[team][0] != 0) {
		do {
			int selection;

			selection = rand() % FLIGHT_GROUP_SELECTION_COUNT + 1;
			g_missionSetupPlayerFlightGroupIndices
				[team * PLAYER_SLOTS_PER_TEAM +
				 assignedPlayers] = -1;
			do {
				int flightGroupIndex;

				flightGroupIndex = 0;
				if ((int16_t)g_frontendMission
					    .flightGroupCount > 0) {
					do {
						int priorAssignment;

						if (g_frontendMission
								    .flightGroups
									    [flightGroupIndex]
								    .playerNumber !=
							    0 &&
						    g_frontendMission
								    .flightGroups
									    [flightGroupIndex]
								    .team ==
							    team) {
							priorAssignment = 0;
							if (assignedPlayers >
							    0) {
								do {
									if (g_missionSetupPlayerFlightGroupIndices
										    [team * PLAYER_SLOTS_PER_TEAM +
										     priorAssignment] ==
									    flightGroupIndex) {
										break;
									}
									++priorAssignment;
								} while (
									priorAssignment <
									assignedPlayers);
							}
							if (priorAssignment ==
								    assignedPlayers &&
							    --selection == 0) {
								g_missionSetupPlayerFlightGroupIndices
									[team * PLAYER_SLOTS_PER_TEAM +
									 assignedPlayers] =
										flightGroupIndex;
								break;
							}
						}
						++flightGroupIndex;
					} while (flightGroupIndex <
						 (int)(int16_t)g_frontendMission
							 .flightGroupCount);
				}
			} while (g_missionSetupPlayerFlightGroupIndices
					 [team * PLAYER_SLOTS_PER_TEAM +
					  assignedPlayers] == -1);
			++assignedPlayers;
		} while (g_missionSetupPlayerAssignments
				 .teamPlayerIds[team][assignedPlayers] != 0);
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		memcpy(g_frontendNetPacketScratch.payload, &g_pilotData.team,
		       sizeof(g_pilotData.team));
		g_frontendNetPacketScratch.packetType =
			NET_PACKET_FLIGHT_ASSIGNMENTS;
		memcpy(g_frontendNetPacketScratch.payload +
			       sizeof(g_pilotData.team),
		       &g_missionSetupPlayerFlightGroupIndices
			       [g_pilotData.team * PLAYER_SLOTS_PER_TEAM],
		       PLAYER_SLOTS_PER_TEAM *
			       sizeof(g_missionSetupPlayerFlightGroupIndices
					      [0]));
		Net_SendPacketAndFlush(
			0, &g_frontendNetPacketScratch,
			sizeof(g_frontendNetPacketScratch.packetType) +
				sizeof(g_pilotData.team) +
				PLAYER_SLOTS_PER_TEAM *
					sizeof(g_missionSetupPlayerFlightGroupIndices
						       [0]));
	}
	return 1;
}

/* Sets the pilot's team's eight g_missionSetupPlayerFlightGroupIndices entries
 * to -1 and, outside a solo game, sends every player a CLEAR_FLIGHT_ASSIGNMENTS
 * packet with the team. Returns 1. */
// FUNCTION: XVT 0x4FA690
int MissionSetup_ClearFlightAssignments(void)
{
	int firstAssignment;
	int assignmentIndex;

	firstAssignment = 8 * g_pilotData.team;
	assignmentIndex = 0;
	do {
		int currentAssignment = firstAssignment + assignmentIndex;

		++assignmentIndex;
		g_missionSetupPlayerFlightGroupIndices[currentAssignment] = -1;
	} while (assignmentIndex < 8);
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		memcpy(g_frontendNetPacketScratch.payload, &g_pilotData.team,
		       sizeof(g_pilotData.team));
		g_frontendNetPacketScratch.packetType =
			NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS;
		Net_SendPacketAndFlush(
			0, &g_frontendNetPacketScratch,
			sizeof(g_frontendNetPacketScratch.packetType) +
				sizeof(g_pilotData.team));
	}
	return 1;
}

/* Gives each player of the pilot's team who has no flight group the first of
 * the team's player flight groups no slot holds, and sends every player a
 * FLIGHT_ASSIGNMENT packet (team, slot, flight group) for it; when every one is
 * taken the slot keeps the team's last player flight group and nothing is sent.
 * Only MissionSetup_FlightAssignmentUpdate calls it, when the captain's
 * FLIGHT_ASSIGNMENTS_READY arrives. Returns 1. */
// FUNCTION: XVT 0x4FA6F0
int MissionSetup_FillFlightAssignments(void)
{
	enum { PLAYER_SLOTS_PER_TEAM = 8, PACKET_VALUE_COUNT = 3 };

	int playerIndex;

	for (playerIndex = 0;
	     playerIndex < g_teamPlayerFlightGroupCount[g_pilotData.team];
	     ++playerIndex) {
		int teamAssignmentOffset =
			PLAYER_SLOTS_PER_TEAM * g_pilotData.team;

		if (g_missionSetupPlayerFlightGroupIndices
				    [teamAssignmentOffset + playerIndex] ==
			    -1 &&
		    g_missionSetupPlayerAssignments
				    .teamPlayerIds[g_pilotData.team]
						  [playerIndex] != 0) {
			int flightGroupIndex;

			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int)(int16_t)g_frontendMission.flightGroupCount;
			     ++flightGroupIndex) {
				if (g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .playerNumber != 0 &&
				    g_frontendMission
						    .flightGroups
							    [flightGroupIndex]
						    .team == g_pilotData.team) {
					int priorAssignment;

					for (priorAssignment = 0;
					     priorAssignment <
					     g_teamPlayerFlightGroupCount
						     [g_pilotData.team];
					     ++priorAssignment) {
						if (g_missionSetupPlayerFlightGroupIndices
							    [teamAssignmentOffset +
							     priorAssignment] ==
						    flightGroupIndex) {
							break;
						}
					}
					g_missionSetupPlayerFlightGroupIndices
						[teamAssignmentOffset +
						 playerIndex] =
							flightGroupIndex;
					if (priorAssignment ==
					    g_teamPlayerFlightGroupCount
						    [g_pilotData.team]) {
						*(int *)&g_frontendNetPacketScratch
							 .payload[0] =
							g_pilotData.team;
						*(int *)&g_frontendNetPacketScratch
							 .payload[4] =
							playerIndex;
						*(int *)&g_frontendNetPacketScratch
							 .payload[8] =
							flightGroupIndex;
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_FLIGHT_ASSIGNMENT;
						Net_SendPacketAndFlush(
							0,
							&g_frontendNetPacketScratch,
							sizeof(g_frontendNetPacketScratch
								       .packetType) +
								PACKET_VALUE_COUNT *
									sizeof(int));
						break;
					}
				}
			}
		}
	}
	return 1;
}

/* Draws the duty roster under the briefing map on the flight assignment screen
 * when the pilot's team has more than 4 player flight groups and the map shows:
 * the team's player flight groups, numbered, four to a column, each with its
 * pilot's rating and name (pulsing for the local player and in a solo game,
 * else yellow) or the unassigned text. The team's captain, or a solo player,
 * also sees the team captain line. Returns 1. */
// FUNCTION: XVT 0x4FA810
int MissionSetup_DrawAssignedPlayers(int frameCounter)
{
	RECT rect;
	RECT playerTextRect;
	RECT previousClipRect;
	int isTeamCaptain;
	int flightGroupIndex;
	int displayedFlightGroupCount;
	int assignmentSlot;
	int rosterIndex;

	FrontendDraw_RectAssign(&rect, 88, 335, 443, 349);
	FrontendText_DrawCentered(12,
				  FrontendString_Get(FRONTSTR_627_DUTY_ROSTER),
				  &rect, 0xFFFF);
	isTeamCaptain = Net_GetLocalPlayerId() ==
			g_missionSetupPlayerAssignments
				.teamPlayerIds[g_pilotData.team][0];
	if (isTeamCaptain || g_frontendMissionSessionMode ==
				     FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 88, 422, 443, 436);
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(FRONTSTR_741_YOU_ARE_A_TEAM_CAPTAIN),
			&rect, g_colorGreen);
	}

	displayedFlightGroupCount = 0;
	FrontendDraw_RectAssign(&rect, 88, 352, 264, 368);
	for (flightGroupIndex = 0;
	     *(int16_t *)&g_frontendMission.flightGroupCount > flightGroupIndex;
	     ++flightGroupIndex) {
		if (g_frontendMission.flightGroups[flightGroupIndex]
				    .playerNumber != 0 &&
		    g_frontendMission.flightGroups[flightGroupIndex].team ==
			    g_pilotData.team) {
			for (assignmentSlot = 0;
			     assignmentSlot <
			     g_teamPlayerFlightGroupCount[g_pilotData.team];
			     ++assignmentSlot) {
				if (g_missionSetupPlayerFlightGroupIndices
					    [g_pilotData.team * 8 +
					     assignmentSlot] ==
				    flightGroupIndex) {
					for (rosterIndex = 0; rosterIndex < 8;
					     ++rosterIndex) {
						if (g_mpRoster[rosterIndex]
							    .playerId ==
						    g_missionSetupPlayerAssignments
							    .teamPlayerIds
								    [g_pilotData
									     .team]
								    [assignmentSlot]) {
							sprintf(g_frontendScratchBuffer,
								"%u.",
								displayedFlightGroupCount +
									1);
							FrontendDraw_RectCopy(
								&playerTextRect,
								&rect);
							playerTextRect.right =
								playerTextRect
									.left +
								15;
							FrontendText_DrawAlignedInRect(
								12,
								g_frontendScratchBuffer,
								&playerTextRect,
								0, 1, 0xFFFF);
							playerTextRect.left =
								rect.left + 15;
							playerTextRect.right =
								rect.right;
							sprintf(g_frontendScratchBuffer,
								"%c%s %c%s", 6,
								FrontendString_Get(
									FRONTSTR_154_DRONE +
									g_mpRoster[rosterIndex]
										.pilotRating),
								1,
								g_mpRoster[rosterIndex]
									.name);
							FrontendDisplay_GetScreenClipRect(
								&previousClipRect);
							FrontendDisplay_SetScreenClipRect640x480(
								&rect);
							if (Net_GetLocalPlayerId() ==
								    g_mpRoster[rosterIndex]
									    .playerId ||
							    g_frontendMissionSessionMode ==
								    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								FrontendText_DrawAlignedInRect(
									12,
									g_frontendScratchBuffer,
									&playerTextRect,
									0, 1,
									g_pulseColorRamp
										[((frameCounter %
										   24) &
										  ~1) >>
										 1]);
							} else {
								FrontendText_DrawAlignedInRect(
									12,
									g_frontendScratchBuffer,
									&playerTextRect,
									0, 1,
									g_colorYellow);
							}
							FrontendDisplay_SetScreenClipRect640x480(
								&previousClipRect);
							break;
						}
					}
					break;
				}
			}

			if (g_teamPlayerFlightGroupCount[g_pilotData.team] ==
			    assignmentSlot) {
				sprintf(g_frontendScratchBuffer, "%u. %s",
					displayedFlightGroupCount + 1,
					FrontendString_Get(
						FRONTSTR_801_UNASSIGNED));
				FrontendText_DrawAlignedInRect(
					12, g_frontendScratchBuffer, &rect, 0,
					1, 0xFFFF);
			}
			++displayedFlightGroupCount;
			if (displayedFlightGroupCount == 4) {
				FrontendDraw_RectAssign(&rect, 267, 352, 443,
							368);
			} else {
				FrontendDraw_RectOffsetXY(&rect, 0, 17);
			}
		}
		if (g_teamPlayerFlightGroupCount[g_pilotData.team] <=
		    displayedFlightGroupCount) {
			break;
		}
	}
	return 1;
}

/* Draws the mission description on the flight assignment screen's expanded
 * layout: a heading for a tournament, battle, campaign or other mission, then
 * g_missionText wrapped, with a scrollbar that sets g_frontendFirstVisibleLine
 * when the text's wrapped line count (from a draw starting at line 4096) plus
 * one is over 9. Returns 1. */
// FUNCTION: XVT 0x4FAB50
int MissionSetup_DrawAssignmentMissionDescription(void)
{
	RECT rect;
	int lineCount;

	FrontendDraw_RectAssign(&rect, 88, 90, 430, 107);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_BATTLES) {
		FrontendText_DrawAlignedInRect(
			15, FrontendString_Get(FRONTSTR_472_BATTLE_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}
	FrontendDraw_RectAssign(&rect, 88, 108, 420, 252);
	lineCount = FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF,
					     4, 4096) +
		    1;
	if (lineCount > 9) {
		FrontendDraw_RectAssign(&rect, 421, 108, 430, 252);
		g_frontendFirstVisibleLine = FrontendScrollbar_Draw(
			&rect, g_frontendFirstVisibleLine, lineCount, 0, 5,
			(unsigned int)g_colorNavy, 9);
		FrontendDraw_RectAssign(&rect, 88, 108, 420, 252);
	} else {
		FrontendDraw_RectAssign(&rect, 88, 108, 430, 252);
	}
	FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF, 4,
				 g_frontendFirstVisibleLine);
	return 1;
}

/* Exit callback of the battle choice screen: frees g_missionList,
 * g_battleMissionList (setting g_battleMissionListCount to 0) and
 * g_missionText, setting each to NULL, frees the "background" image, resets the
 * scrollable controls and clears the mouse input gate. Returns 0. */
// FUNCTION: XVT 0x4FBE10
int MissionSetup_BattleChoice_Exit(void)
{
	if (g_missionList != NULL) {
		free(g_missionList);
		g_missionList = NULL;
	}
	if (g_battleMissionList != NULL) {
		free(g_battleMissionList);
		g_battleMissionList = NULL;
		g_battleMissionListCount = 0;
	}
	if (g_missionText != NULL) {
		free(g_missionText);
		g_missionText = NULL;
	}
	FrontImage_FreeResourceByName("background");
	Frontend_ResetScrollableControls();
	FrontendMouse_ClearInputGate();
	return 0;
}

/* The battle choice screen, run once per frame: in a combat engagement sequence
 * with the players' choice setup, the team whose index is not the previous
 * mission's result, which the screen's text calls the losing team, picks the
 * next mission from the battle's list. On frame 0 it clears
 * g_battleChoiceTimeoutHandled, draws the screen's base, reloads the list and
 * selects the stored mission, starts the 120000 ms countdown in
 * g_battleChoiceRemainingMs, allocates and loads g_missionText and builds the
 * choice list. Outside a solo game it handles one packet: a host cancel leaves
 * for the join screen; a lobby state prunes the flight assignments;
 * MISSION_CHOICE stores the chosen mission id as
 * battleSequenceState.currentMissionId and in missionOrdinals at
 * currentMissionIndex, stores its list index in missionListIndices, loads the
 * mission and moves to flight assignment; a return to setup goes back to
 * MissionSetup_Update; a BRIEFING_COUNTDOWN lowers g_battleChoiceRemainingMs to
 * its value; a pilot rating updates its sender's roster entry. It draws the
 * battle's name, the title, the selected mission, the description, the chat
 * panel, the pilot banner and, outside a solo game, the countdown: the ms
 * elapsed since the last frame come off g_battleChoiceRemainingMs and the host
 * sends every player a BRIEFING_COUNTDOWN whenever the whole seconds change;
 * once it is under 0 it stays at 0, the losing team's captain (slot 0) sends
 * the host SUBMIT_MISSION_CHOICE once, and the function returns 0 each frame
 * before the rest. Then the help text, the roster, and Previous (solo: ask,
 * then mission setup), Restart (host: ask, then RETURN_TO_SETUP to every
 * player) or Leave (client: ask, then the join screen), and Next: a solo player
 * moves to flight assignment, the losing team's captain sends the host
 * SUBMIT_MISSION_CHOICE. A solo player or that captain also gets the mission
 * list button, which pushes MissionSetup_BattleChoice_DrawList. Both
 * SUBMIT_MISSION_CHOICE sends give a size of one word, so the description id
 * written into the payload is not sent and the host relays a word that was not
 * received. Returns 1 when Frontend_HandleCommonScreenControls returns 1, else
 * 0. Does not check that currentMissionIndex is above 0 before reading the
 * previous result. */
// FUNCTION: XVT 0x4FBE90
int MissionSetup_BattleChoice_Update(int frameCounter)
{
	enum {
		PLAYER_COUNT = 8,
		BATTLE_CHOICE_DURATION_MS = 120000,
		MILLISECONDS_PER_SECOND = 1000,
		BATTLE_CHOICE_DURATION_SECONDS =
			BATTLE_CHOICE_DURATION_MS / MILLISECONDS_PER_SECOND,
		BRIEFING_TEXT_CAPACITY = 4096,
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
		BASIC_PACKET_SIZE = sizeof(int),
		TIMER_PACKET_SIZE = 2 * sizeof(int),
	};

	int packetType;
	int rosterIndex;
	int animationFrame;
	int remainingSeconds;
	int canChooseMission;
	RECT rect;
	RECT savedClipRect;

	if (frameCounter == 0) {
		g_battleChoiceTimeoutHandled = 0;
		FrontendCursor_SetPos(37, 445);
		g_frontendFirstVisibleLine = 0;
		FrontImage_RegisterResourceDefault("frontres\\soloteam.bmp",
						   "background");
		MissionSetup_LoadMissionList(g_pilotData.missionDirectoryId);
		if (g_missionList != NULL) {
			for (g_selectedMissionListIndex = 0;
			     (unsigned int)g_selectedMissionListIndex <
			     g_missionCount;
			     ++g_selectedMissionListIndex) {
				if (g_missionList[g_selectedMissionListIndex]
					    .missionIdx ==
				    g_pilotData.missionDescriptionIds
					    [g_pilotData.missionDirectoryId]) {
					break;
				}
			}
		}
		FrontendDisplay_LockOffscreenSurface();
		FrontImage_DrawSpriteOpaque("background", 0, 0);
		FrontImage_DrawSprite("frame", 0, 0);
		if (g_hostCdAvailable != 0) {
			FrontImage_DrawSprite("allactive", 0, 0);
		} else {
			FrontImage_DrawSprite("clientactive", 0, 0);
		}
		if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			FrontImage_DrawSpriteTranslucent("chatbox", 0, 0);
		}
		FrontImage_DrawSpriteTranslucent("gameoverlay", 0, 0);
		FrontendDisplay_UnlockOffscreenSurface(1);
		FrontendText_StartTextFadeIn(20);
		g_battleChoiceRemainingMs = BATTLE_CHOICE_DURATION_MS;
		g_battleChoiceClockMs = GetTickCount();
		g_battleChoiceLastSentSecond = BATTLE_CHOICE_DURATION_SECONDS;
		g_battleChoicePreviousClockMs = g_battleChoiceClockMs;
		g_missionText = malloc(BRIEFING_TEXT_CAPACITY);
		MissionSetup_LoadMissionDescText(g_missionText);
		MissionSetup_BattleChoice_BuildList();
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		packetType = FrontendNet_ProcessNetworkPackets();
		if (packetType == NET_PACKET_HOST_CANCELLED) {
			Net_ShutdownDirectPlaySession();
			if (Net_IsHost() == 0) {
				FrontendDialog_ShowConfirmDialog(
					FrontendString_Get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					FrontendString_Get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					FrontendString_Get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return XvtDialog_ContinueWith(
					XvtMissionDialogs_Resume,
					XVT_MISSION_ASSIGNMENT_CANCELLED);
#endif
			}
			g_frontendSkipScreenEntrySetup = 1;
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResources);
		} else if (packetType == NET_PACKET_STATE) {
			MissionSetup_PruneFlightAssignments();
		} else if (packetType == NET_PACKET_MISSION_CHOICE) {
			g_pilotData.battleSequenceState.currentMissionId =
				g_frontendNetPacketArg0;
			g_pilotData.battleSequenceState
				.missionOrdinals[g_pilotData.battleSequenceState
							 .currentMissionIndex] =
				g_frontendNetPacketArg0;
			if (g_missionList != NULL) {
				for (g_selectedMissionListIndex = 0;
				     (unsigned int)g_selectedMissionListIndex <
				     g_missionCount;
				     ++g_selectedMissionListIndex) {
					if (g_missionList
						    [g_selectedMissionListIndex]
							    .missionIdx ==
					    g_frontendNetPacketArg0) {
						g_pilotData.battleSequenceState.missionListIndices
							[g_pilotData
								 .battleSequenceState
								 .currentMissionIndex] =
							g_selectedMissionListIndex;
						break;
					}
				}
			}
			FrontendMission_LoadCurrent();
			FrontendScreen_SetCallbacks(
				MissionSetup_FlightAssignmentUpdate,
				MissionSetup_FreeScreenResources);
			return 0;
		} else if (packetType == NET_PACKET_RETURN_TO_SETUP) {
			g_frontendSkipScreenEntrySetup = 1;
			FrontendScreen_SetCallbacks(
				MissionSetup_Update,
				(FrontendScreenExitFn)MissionSetup_Exit);
			return 0;
		} else if (packetType == NET_PACKET_BRIEFING_COUNTDOWN) {
			if (g_frontendNetPacketArg0 <
			    g_battleChoiceRemainingMs) {
				g_battleChoiceRemainingMs =
					g_frontendNetPacketArg0;
			}
		} else if (packetType == NET_PACKET_PILOT_RATING) {
			for (rosterIndex = 0; rosterIndex < PLAYER_COUNT;
			     ++rosterIndex) {
				if (g_mpRoster[rosterIndex].playerId ==
				    g_frontendNetPacketSenderPlayerId) {
					g_mpRoster[rosterIndex].pilotRating =
						g_frontendNetPacketArg0;
					break;
				}
			}
		}
	}

	FrontendDraw_RectAssign(&rect, 158, 52, 491, 68);
	FrontendDisplay_GetScreenClipRect(&savedClipRect);
	FrontendDisplay_SetScreenClipRect640x480(&rect);
	FrontendText_DrawCentered(12, g_missionSequenceDescription, &rect,
				  0xFFFF);
	FrontendDisplay_SetScreenClipRect640x480(&savedClipRect);
	FrontendDraw_RectAssign(&rect, 84, 90, 434, 108);
	FrontendText_DrawCentered(
		15, FrontendString_Get(FRONTSTR_812_SELECT_NEXT_BATTLE_MISSION),
		&rect, 0xFFFF);
	if (Net_IsHost() != 0 ||
	    g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 84, 112, 416, 130);
	} else {
		FrontendDraw_RectAssign(&rect, 84, 112, 434, 130);
	}
	if ((unsigned int)g_selectedMissionListIndex < g_missionCount) {
		FrontendDraw_RectInsetXY(&rect, 4, 0);
		FrontendText_DrawAlignedInRect(
			15,
			g_missionList[g_selectedMissionListIndex].description,
			&rect, 0, 1, g_colorYellow);
	}
	MissionSetup_BattleChoice_DrawDescription();
	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendNet_UpdateAndDrawChatPanel(frameCounter);
	}

	FrontendDraw_RectAssign(&rect, 200, 452, 436, 464);
	if (g_pilotData.name[0] != '\0') {
		sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
			g_pilotData.ratingName, 1, g_pilotData.name);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect,
					  g_colorYellow);
		if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if (g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_MELEES &&
			    g_pilotData.missionDirectoryId !=
				    MISSION_DIRECTORY_TOURNAMENTS) {
				if (g_pilotData.currentFactionId == 0) {
					sprintf(g_frontendScratchBuffer,
						"rebtiny%d",
						(frameCounter %
						 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1);
					FrontImage_DrawSprite(
						g_frontendScratchBuffer, 204,
						453);
				} else {
					sprintf(g_frontendScratchBuffer,
						"imptiny%d",
						(frameCounter %
						 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1);
					FrontImage_DrawSprite(
						g_frontendScratchBuffer, 204,
						453);
				}
			} else {
				animationFrame =
					(frameCounter %
					 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
					1;
				sprintf(g_frontendScratchBuffer, "rebtiny%d",
					animationFrame);
				FrontImage_DrawSprite(g_frontendScratchBuffer,
						      204, 453);
				sprintf(g_frontendScratchBuffer, "imptiny%d",
					animationFrame);
			}
		} else {
			animationFrame =
				(frameCounter %
				 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
				1;
			sprintf(g_frontendScratchBuffer, "rebtiny%d",
				animationFrame);
			FrontImage_DrawSprite(g_frontendScratchBuffer, 204,
					      453);
			sprintf(g_frontendScratchBuffer, "imptiny%d",
				animationFrame);
		}
		FrontImage_DrawSprite(g_frontendScratchBuffer, 420, 453);
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendDraw_RectAssign(&rect, 507, 452, 562, 464);
		Frontend_FormatSecondsToClockString(g_battleChoiceRemainingMs /
						    MILLISECONDS_PER_SECOND);
		FrontendText_DrawCentered(12, g_frontendScratchBuffer, &rect,
					  0xFFFF);
		if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_battleChoiceClockMs = GetTickCount();
			g_battleChoiceRemainingMs +=
				g_battleChoicePreviousClockMs -
				g_battleChoiceClockMs;
			if (Net_IsHost() != 0) {
				remainingSeconds = g_battleChoiceRemainingMs /
						   MILLISECONDS_PER_SECOND;
				if (g_battleChoiceLastSentSecond !=
				    remainingSeconds) {
					g_battleChoiceLastSentSecond =
						remainingSeconds;
					*(int *)g_frontendNetPacketScratch
						 .payload =
						g_battleChoiceRemainingMs;
					g_frontendNetPacketScratch.packetType =
						NET_PACKET_BRIEFING_COUNTDOWN;
					Net_SendPacketAndFlush(
						0, &g_frontendNetPacketScratch,
						TIMER_PACKET_SIZE);
				}
			}
			if (g_battleChoiceRemainingMs < 0) {
				g_battleChoiceRemainingMs = 0;
				if (g_battleChoiceTimeoutHandled == 0) {
					g_battleChoiceTimeoutHandled = 1;
					if (g_pilotData.battleSequenceState.missionResults
							    [g_pilotData
								     .battleSequenceState
								     .currentMissionIndex -
							     1] !=
						    (BattleMissionResult)
							    g_pilotData.team &&
					    Net_GetLocalPlayerId() ==
						    g_missionSetupPlayerAssignments
							    .teamPlayerIds
								    [g_pilotData
									     .team]
								    [0]) {
						*(int *)g_frontendNetPacketScratch
							 .payload =
							g_pilotData.missionDescriptionIds
								[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS];
						g_frontendNetPacketScratch
							.packetType =
							NET_PACKET_SUBMIT_MISSION_CHOICE;
						Net_SendPacketAndFlush(
							Net_GetHostPlayerId(),
							&g_frontendNetPacketScratch,
							BASIC_PACKET_SIZE);
					}
				}
				return 0;
			}
			g_battleChoicePreviousClockMs = g_battleChoiceClockMs;
		}
	}

	FrontendDraw_RectAssign(&rect, 84, 400, 434, 414);
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_813_YOU_LOST_THE_LAST_BATTLE_MISSION),
			&rect, g_colorGreen);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_814_SELECT_THE_NEXT_BATTLE_MISSION),
			&rect, g_colorGreen);
	} else if (g_pilotData.battleSequenceState
			   .missionResults[g_pilotData.battleSequenceState
						   .currentMissionIndex -
					   1] ==
		   (BattleMissionResult)g_pilotData.team) {
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_815_YOUR_TEAM_WON_THE_LAST_BATTLE_MISSION),
			&rect, g_colorRed);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_816_THE_LOSING_TEAM_WILL_SELECT_THE_NEXT_BATTLE_MISSION),
			&rect, g_colorRed);
	} else if (Net_GetLocalPlayerId() ==
		   g_missionSetupPlayerAssignments
			   .teamPlayerIds[g_pilotData.team][0]) {
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_817_YOUR_TEAM_LOST_THE_LAST_BATTLE_MISSION),
			&rect, g_colorGreen);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_814_SELECT_THE_NEXT_BATTLE_MISSION),
			&rect, g_colorGreen);
	} else {
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_817_YOUR_TEAM_LOST_THE_LAST_BATTLE_MISSION),
			&rect, g_colorRed);
		FrontendDraw_RectOffsetXY(&rect, 0, 15);
		FrontendText_DrawCentered(
			12,
			FrontendString_Get(
				FRONTSTR_818_YOUR_TEAM_CAPTAIN_WILL_CHOOSE_THE_NEXT_BATTLE_MISSION),
			&rect, g_colorRed);
	}
	MissionSetup_BattleChoice_DrawRoster(frameCounter);

	FrontendDraw_RectAssign(&rect, 85, 447, 176, 471);
	if (g_gameConfig.helpOn != 0) {
		FrontendButton_EnableOverlayText();
	}
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_569_PREVIOUS));

#ifdef XVT_MODERN
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
				FrontendString_Get(
					FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
				FrontendString_Get(
					FRONTSTR_683_TERMINATE_THIS_BATTLE),
				FrontendString_Get(FRONTSTR_523_OKAY),
				FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(
				XvtMissionDialogs_Resume,
				XVT_MISSION_SOLO_BACK_TO_SETUP);
		}
#else
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0 &&
		    FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
			    FrontendString_Get(
				    FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
			    FrontendString_Get(
				    FRONTSTR_683_TERMINATE_THIS_BATTLE),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendSkipScreenEntrySetup = 1;
			FrontendScreen_SetCallbacks(
				MissionSetup_Update,
				(FrontendScreenExitFn)MissionSetup_Exit);
		}
#endif

	} else if (Net_IsHost() != 0) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_668_RESTART));

#ifdef XVT_MODERN
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
				FrontendString_Get(
					FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
				FrontendString_Get(
					FRONTSTR_754_TO_SELECT_MISSION),
				FrontendString_Get(FRONTSTR_523_OKAY),
				FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
						      XVT_MISSION_HOST_RESTART);
		}
#else
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0 &&
		    FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
			    FrontendString_Get(
				    FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
			    FrontendString_Get(FRONTSTR_754_TO_SELECT_MISSION),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_RETURN_TO_SETUP;
			Net_SendPacketAndFlush(0, &g_frontendNetPacketScratch,
					       BASIC_PACKET_SIZE);
		}
#endif

	} else {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_204_LEAVE));

#ifdef XVT_MODERN
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0) {
			FrontendDialog_ShowConfirmDialog(
				FrontendString_Get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				FrontendString_Get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				FrontendString_Get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				FrontendString_Get(FRONTSTR_523_OKAY),
				FrontendString_Get(FRONTSTR_019_CANCEL));
			return XvtDialog_ContinueWith(XvtMissionDialogs_Resume,
						      XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (FrontendButton_HandleSpriteButton(
			    &rect, "leaveup", "leavedown",
			    FrontendString_Get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0 &&
		    FrontendDialog_ShowConfirmDialog(
			    FrontendString_Get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    FrontendString_Get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    FrontendString_Get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    FrontendString_Get(FRONTSTR_523_OKAY),
			    FrontendString_Get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontendSkipScreenEntrySetup = 1;
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_PLAYER_LEFT;
			Net_SendPacketAndFlush(Net_GetHostPlayerId(),
					       &g_frontendNetPacketScratch,
					       BASIC_PACKET_SIZE);
			Net_ShutdownDirectPlaySession();
			FrontendScreen_SetCallbacks(
				FrontendNet_JoinGameScreen,
				(FrontendScreenExitFn)
					FrontendMissionList_FreeScreenResources);
		}
#endif
	}

	FrontendDraw_RectAssign(&rect, 8, 405, 71, 470);
	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_212_NEXT));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "nextup", "nextdown",
			    FrontendString_Get(FRONTSTR_475_GO_TO_BRIEFING), 12,
			    0, 7, "flysound") != 0) {
			FrontendScreen_SetCallbacks(
				MissionSetup_FlightAssignmentUpdate,
				MissionSetup_FreeScreenResources);
			FrontendButton_DisableOverlayText();
			return 0;
		}
	} else if (g_pilotData.battleSequenceState.missionResults
				   [g_pilotData.battleSequenceState
					    .currentMissionIndex -
				    1] !=
			   (BattleMissionResult)g_pilotData.team &&
		   Net_GetLocalPlayerId() ==
			   g_missionSetupPlayerAssignments
				   .teamPlayerIds[g_pilotData.team][0]) {
		FrontendButton_SetOverlayText(
			FrontendString_Get(FRONTSTR_212_NEXT));
		if (FrontendButton_HandleSpriteButton(
			    &rect, "nextup", "nextdown",
			    FrontendString_Get(FRONTSTR_475_GO_TO_BRIEFING), 12,
			    0, 7, "flysound") != 0) {
			*(int *)g_frontendNetPacketScratch.payload =
				g_pilotData.missionDescriptionIds
					[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS];
			g_frontendNetPacketScratch.packetType =
				NET_PACKET_SUBMIT_MISSION_CHOICE;
			Net_SendPacketAndFlush(Net_GetHostPlayerId(),
					       &g_frontendNetPacketScratch,
					       BASIC_PACKET_SIZE);
		}
	}

	canChooseMission = 0;
	FrontendButton_DisableOverlayText();
	if (g_frontendMissionSessionMode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
	    (g_pilotData.battleSequenceState
			     .missionResults[g_pilotData.battleSequenceState
						     .currentMissionIndex -
					     1] !=
		     (BattleMissionResult)g_pilotData.team &&
	     Net_GetLocalPlayerId() ==
		     g_missionSetupPlayerAssignments
			     .teamPlayerIds[g_pilotData.team][0])) {
		canChooseMission = 1;
	}
	if (canChooseMission == 1) {
		FrontendDraw_RectAssign(&rect, 417, 112, 434, 130);
		if (FrontendButton_HandleSpriteButton(
			    &rect, "dropbtnup", "dropbtndown",
			    FrontendString_Get(FRONTSTR_684_MISSION_LIST), 15,
			    0xFFFF, 9, "buttonsound") != 0) {
			FrontendDraw_RectAssign(&rect, 0, 0, 639, 479);
			FrontendScreen_QueuePush(
				MissionSetup_BattleChoice_DrawList, &rect);
		}
	}
	return Frontend_HandleCommonScreenControls(1) == 1;
}

/* Draws the description box of the battle choice screen: a heading for a
 * tournament, battle, campaign or other mission, then g_missionText wrapped,
 * with a scrollbar that sets g_frontendFirstVisibleLine when the text's wrapped
 * line count (from a draw starting at line 4096) plus one is over 9. Returns
 * 1. */
// FUNCTION: XVT 0x4FCBF0
int MissionSetup_BattleChoice_DrawDescription(void)
{
	RECT rect;
	int lineCount;

	FrontendDraw_RectAssign(&rect, 88, 138, 430, 153);
	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_BATTLES) {
		FrontendText_DrawAlignedInRect(
			15, FrontendString_Get(FRONTSTR_472_BATTLE_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilotData.missionDirectoryId ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		FrontendText_DrawAlignedInRect(
			15,
			FrontendString_Get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}
	FrontendDraw_RectAssign(&rect, 88, 157, 420, 301);
	lineCount = FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF,
					     4, 4096) +
		    1;
	if (lineCount > 9) {
		FrontendDraw_RectAssign(&rect, 421, 157, 430, 301);
		g_frontendFirstVisibleLine = FrontendScrollbar_Draw(
			&rect, g_frontendFirstVisibleLine, lineCount, 0, 5,
			(unsigned int)g_colorNavy, 9);
		FrontendDraw_RectAssign(&rect, 88, 157, 420, 301);
	} else {
		FrontendDraw_RectAssign(&rect, 88, 157, 430, 301);
	}
	FrontendText_DrawWrapped(12, g_missionText, &rect, 0xFFFF, 4,
				 g_frontendFirstVisibleLine);
	return 1;
}

/* Draws the Players in Game panel of the battle choice screen: each g_mpRoster
 * entry with a nonzero playerId, four to a column, as its rating and name, or
 * the join in progress text when it has neither; the local player's entry
 * pulses, as does every entry in a solo game, and the others are yellow.
 * Returns 1. */
// FUNCTION: XVT 0x4FCD70
int MissionSetup_BattleChoice_DrawRoster(int frameCounter)
{
	RECT rect;
	RECT previousClipRect;
	int mouseX;
	int mouseY;
	int displayedCount;
	int rosterIndex;

	FrontendCursor_GetPos(&mouseX, &mouseY);
	FrontendDraw_RectAssign(&rect, 88, 311, 430, 326);
	FrontendText_DrawAlignedInRect(
		15, FrontendString_Get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0,
		1, 0xFFFF);
	displayedCount = 0;
	FrontendDraw_RectAssign(&rect, 88, 331, 258, 345);
	for (rosterIndex = 0; rosterIndex < 8; ++rosterIndex) {
		if (g_mpRoster[rosterIndex].playerId != 0) {
			FrontendDisplay_GetScreenClipRect(&previousClipRect);
			FrontendDisplay_SetScreenClipRect640x480(&rect);
			if (g_mpRoster[rosterIndex].pilotRating == 0 &&
			    g_mpRoster[rosterIndex].name[0] == '\0') {
				sprintf(g_frontendScratchBuffer, "%s",
					FrontendString_Get(
						FRONTSTR_720_JOIN_IN_PROGRESS));
			} else {
				sprintf(g_frontendScratchBuffer, "%c%s %c%s", 6,
					FrontendString_Get(
						FRONTSTR_154_DRONE +
						g_mpRoster[rosterIndex]
							.pilotRating),
					1, g_mpRoster[rosterIndex].name);
			}
			if (Net_GetLocalPlayerId() ==
				    g_mpRoster[rosterIndex].playerId ||
			    g_frontendMissionSessionMode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				FrontendText_DrawAlignedInRect(
					12, g_frontendScratchBuffer, &rect, 0,
					1,
					g_pulseColorRamp[((frameCounter % 24) &
							  ~1) >>
							 1]);
			} else {
				FrontendText_DrawAlignedInRect(
					12, g_frontendScratchBuffer, &rect, 0,
					1, g_colorYellow);
			}
			++displayedCount;
			if (displayedCount == 4) {
				FrontendDraw_RectAssign(&rect, 260, 331, 430,
							345);
			} else {
				FrontendDraw_RectOffsetXY(&rect, 0, 15);
			}
			FrontendDisplay_SetScreenClipRect640x480(
				&previousClipRect);
		}
	}

	return 1;
}

/* Builds g_battleMissionList, the battle's missions the choosing team may pick,
 * freeing the old one first. It loads the battle list for a moment to find the
 * selected battle's sequence file and copy the battle's description into
 * g_missionSequenceDescription, then puts g_missionList and g_missionCount
 * back. The file's first line gives the count, stored in
 * g_battleMissionListCount; each line after it names a mission, matched without
 * regard to case against g_missionList's file names. A match is copied in with
 * its section name cleared, and marked unavailable when its line's index is
 * among the missionOrdinals before currentMissionIndex. Returns 1, or 0 when
 * the file does not open or is empty, or ends early, the count then cut to the
 * lines read. Does not check the allocation, and leaves an entry unset when its
 * file name is not in the list. */
// FUNCTION: XVT 0x4FCF30
int MissionSetup_BattleChoice_BuildList(void)
{
	enum {
		BATTLE_DESCRIPTOR_PATH_CAPACITY = 128,
		BATTLE_DESCRIPTOR_LINE_CAPACITY = 255
	};

	MissionListEntry *savedMissionList;
	unsigned int savedMissionCount;
	unsigned int selectedMissionIndex;
	unsigned int sourceMissionIndex;
	int parsedMissionCount;
	int battleMissionIndex;
	int previousMissionIndex;
	int missionWasUsed;
	char battleDescriptorPath[BATTLE_DESCRIPTOR_PATH_CAPACITY];
	XvtFile *stream;

	if (g_battleMissionList != NULL) {
		free(g_battleMissionList);
		g_battleMissionList = NULL;
	}
	savedMissionList = g_missionList;
	savedMissionCount = g_missionCount;
	g_missionList = NULL;
	MissionSetup_LoadMissionList(MISSION_DIRECTORY_BATTLES);
	selectedMissionIndex = 0;
	while (selectedMissionIndex < g_missionCount &&
	       g_missionList[selectedMissionIndex].missionIdx !=
		       g_pilotData.missionDescriptionIds
			       [MISSION_DIRECTORY_BATTLES]) {
		++selectedMissionIndex;
	}
	sprintf(battleDescriptorPath, "%s\\%s",
		g_missionDirectoryNames[MISSION_DIRECTORY_BATTLES],
		g_missionList[selectedMissionIndex].fileName);
	strcpy(g_missionSequenceDescription,
	       g_missionList[selectedMissionIndex].description);
	free(g_missionList);
	g_missionList = savedMissionList;
	g_missionCount = savedMissionCount;

	stream = File_Open(battleDescriptorPath, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(g_frontendScratchBuffer, BATTLE_DESCRIPTOR_LINE_CAPACITY,
		      stream) == NULL) {
		File_Close(stream);
		return 0;
	}

	parsedMissionCount = atoi(g_frontendScratchBuffer);
	g_battleMissionListCount = parsedMissionCount;
	g_battleMissionList = (MissionListEntry *)malloc(
		sizeof(*g_battleMissionList) * parsedMissionCount);
	battleMissionIndex = 0;
	while (battleMissionIndex < g_battleMissionListCount) {
		if (File_Gets(g_frontendScratchBuffer,
			      BATTLE_DESCRIPTOR_LINE_CAPACITY,
			      stream) == NULL) {
			g_battleMissionListCount = battleMissionIndex;
			File_Close(stream);
			return 0;
		}
		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}

		sourceMissionIndex = 0;
		while (sourceMissionIndex < g_missionCount) {
#ifdef XVT_MODERN
			if (strcasecmp(g_frontendScratchBuffer,
				       g_missionList[sourceMissionIndex]
					       .fileName) == 0)
#else
			if (_strcmpi(g_frontendScratchBuffer,
				     g_missionList[sourceMissionIndex]
					     .fileName) == 0)
#endif
			{
				missionWasUsed = 0;
				for (previousMissionIndex =
					     (int)g_pilotData
						     .battleSequenceState
						     .currentMissionIndex -
					     1;
				     previousMissionIndex >= 0;
				     --previousMissionIndex) {
					if (g_pilotData.battleSequenceState
						    .missionOrdinals
							    [previousMissionIndex] ==
					    battleMissionIndex) {
						missionWasUsed = 1;
						break;
					}
				}
				memcpy(&g_battleMissionList[battleMissionIndex],
				       &g_missionList[sourceMissionIndex],
				       sizeof(g_battleMissionList
						      [battleMissionIndex]));
				memset(g_battleMissionList[battleMissionIndex]
					       .sectionName,
				       0,
				       sizeof(g_battleMissionList
						      [battleMissionIndex]
							      .sectionName));
				if (missionWasUsed != 0) {
					g_battleMissionList[battleMissionIndex]
						.isUnavailable = 1;
				}
				break;
			}
			++sourceMissionIndex;
		}
		++battleMissionIndex;
	}
	return 1;
}

/* The battle choice screen's drop-down list of the battle's missions, a pushed
 * screen run each frame. On frame 0 it counts the rows in
 * g_battleChoiceRowCount, one per available entry of g_battleMissionList and
 * one per change of section among them, and sets g_battleChoiceScrollOffset to
 * the index of the entry matching the selected mission, or to 0 when the count
 * is under 20. Outside a solo game a lobby state from the host loads the host's
 * mission. It draws up to 19 rows, skipping unavailable entries, with a
 * scrollbar when there are more, then each entry's award beside it, chosen as
 * MissionSetup_DrawMissionList chooses it. A click on a row takes that entry as
 * the selected mission, unless outside a solo game its file name starts with
 * "1": it stores the entry's ordinal and list index at battleSequenceState's
 * currentMissionIndex (the ordinal being the entry's index in
 * g_battleMissionList in a solo game and its mission id outside one), loads the
 * mission and its text, and outside a solo game sends the lobby state. A click
 * on a row, or outside the list, closes the list. Each row's text and color
 * come from g_missionList at the entry's index, not from the entry. Returns 0.
 * When g_battleMissionList is NULL it pops the screen and goes on drawing. */
// FUNCTION: XVT 0x4FD1F0
int MissionSetup_BattleChoice_DrawList(int frameCounter)
{
	enum {
		VISIBLE_ROW_COUNT = 19,
		ROW_HEIGHT = 15,
		LIST_LEFT = 84,
		LIST_TOP = 131,
		LIST_RIGHT = 434,
		LIST_SCROLLBAR_RIGHT = 423,
		LIST_BOTTOM_PADDING = 139,
		MISSION_TEXT_INDENT = 37,
		TEXT_FONT_SIZE = 12,
		SCROLLBAR_CONTROL_ID = 8,
		SINGLE_PLAYER_FILE_PREFIX = '1',
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
		AWARD_TOOLTIP_MEDAL_BASE = FRONTSTR_381_BATTLE_MEDALLION,
		AWARD_TOOLTIP_CITATION_BASE = FRONTSTR_659_QUICK_START,
	};

	RECT rect;
	RECT awardRect;
	int cursorX;
	int cursorY;
	int battleMissionIndex;
	unsigned int awardId;
	unsigned int rebelAwardId;
	unsigned int imperialAwardId;
	int displayRow;
	int visibleRowCount;
	int awardFactionId;
	unsigned short textColor;
	char lastSectionName[sizeof(g_battleMissionList[0].sectionName)];

	if (g_battleMissionList == NULL) {
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
	}

	if (frameCounter == 0) {
		g_battleChoiceRowCount = g_battleMissionListCount;
		memset(lastSectionName, 0, sizeof(lastSectionName));
		for (battleMissionIndex = 0;
		     (unsigned int)g_battleMissionListCount >
		     (unsigned int)battleMissionIndex;
		     ++battleMissionIndex) {
			if (g_pilotData.missionDescriptionIds
				    [g_pilotData.missionDirectoryId] ==
			    g_battleMissionList[battleMissionIndex]
				    .missionIdx) {
				g_battleChoiceScrollOffset = battleMissionIndex;
			}
			if (g_battleMissionList[battleMissionIndex]
				    .isUnavailable != 0) {
				--g_battleChoiceRowCount;
			} else if (strcmp(g_battleMissionList
						  [battleMissionIndex]
							  .sectionName,
					  lastSectionName) != 0) {
				++g_battleChoiceRowCount;
				strcpy(lastSectionName,
				       g_battleMissionList[battleMissionIndex]
					       .sectionName);
			}
		}
		if (g_battleChoiceRowCount < VISIBLE_ROW_COUNT + 1) {
			g_battleChoiceScrollOffset = 0;
		}
	}

	if (g_frontendMissionSessionMode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    FrontendNet_ProcessNetworkPackets() == NET_PACKET_STATE) {
		g_pilotData.missionDirectoryId =
			g_frontendNetReceivedMissionDirectoryId;
		g_pilotData.missionDescriptionIds
			[g_frontendNetReceivedMissionDirectoryId] =
			g_frontendNetReceivedMissionDescriptionId;
		FrontendMission_LoadCurrent();
		MissionSetup_LoadMissionDescText(g_missionText);
		MissionSetup_UpdateTeamCounts();
		if (g_missionList != NULL) {
			g_selectedMissionListIndex = 0;
			while (g_missionCount >
				       (unsigned int)
					       g_selectedMissionListIndex &&
			       g_missionList[g_selectedMissionListIndex]
					       .missionIdx !=
				       g_pilotData.missionDescriptionIds
					       [g_pilotData
							.missionDirectoryId]) {
				++g_selectedMissionListIndex;
			}
		}
		g_frontendFirstVisibleLine = 0;
	}

	memset(lastSectionName, 0, sizeof(lastSectionName));
	if (g_battleChoiceRowCount > VISIBLE_ROW_COUNT) {
		FrontendDraw_RectAssign(&rect, 424, LIST_TOP, LIST_RIGHT, 424);
		g_battleChoiceScrollOffset = FrontendScrollbar_Draw(
			&rect, g_battleChoiceScrollOffset,
			g_battleChoiceRowCount, 0, 5, (unsigned int)g_colorNavy,
			SCROLLBAR_CONTROL_ID);
	}

	FrontendCursor_GetPos(&cursorX, &cursorY);
	if (g_battleChoiceRowCount > VISIBLE_ROW_COUNT) {
		visibleRowCount = VISIBLE_ROW_COUNT;
	} else {
		visibleRowCount = g_battleChoiceRowCount;
	}
	FrontendDraw_RectAssign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				ROW_HEIGHT * visibleRowCount +
					LIST_BOTTOM_PADDING);
	if (!FrontendDraw_PointInRect(&rect, cursorX, cursorY) &&
	    (FrontendMouse_GetLeftClick() != 0 ||
	     FrontendMouse_GetRightClick() != 0)) {
		Keyboard_FlushCharBuffer();
		FrontendScreen_PopState();
		Frontend_UnregisterScrollableControl(SCROLLBAR_CONTROL_ID);
	}

	if (g_battleChoiceRowCount > VISIBLE_ROW_COUNT) {
		FrontendDraw_RectAssign(
			&rect, LIST_LEFT, LIST_TOP, LIST_SCROLLBAR_RIGHT,
			ROW_HEIGHT * visibleRowCount + LIST_BOTTOM_PADDING);
	} else {
		FrontendDraw_RectAssign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
					ROW_HEIGHT * visibleRowCount +
						LIST_BOTTOM_PADDING);
	}
	battleMissionIndex = 0;
	FrontendDraw_Rect(&rect, 0, 0, FrontendDisplay_PackRGB(0, 0, 64), 1);
	FrontendDraw_Rect(&rect, 0, 0, 0xFFFF, 0);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	FrontendDraw_Rect(&rect, 0, 0, 0, 0);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	rect.bottom = rect.top + 14;
	rect.left += MISSION_TEXT_INDENT;

	displayRow = 0;
	for (; (unsigned int)g_battleMissionListCount >
	       (unsigned int)battleMissionIndex;
	     ++battleMissionIndex) {
		if (g_battleMissionList[battleMissionIndex].isUnavailable !=
		    0) {
			continue;
		}
		if (strcmp(g_battleMissionList[battleMissionIndex].sectionName,
			   lastSectionName) != 0) {
			if (displayRow >= g_battleChoiceScrollOffset &&
			    displayRow - g_battleChoiceScrollOffset <
				    VISIBLE_ROW_COUNT) {
				rect.left -= MISSION_TEXT_INDENT;
				FrontendText_DrawAlignedInRect(
					TEXT_FONT_SIZE,
					g_battleMissionList[battleMissionIndex]
						.sectionName,
					&rect, 0, 1, g_colorRed);
				rect.left += MISSION_TEXT_INDENT;
				FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
			}
			strcpy(lastSectionName,
			       g_battleMissionList[battleMissionIndex]
				       .sectionName);
			++displayRow;
		}
		if (displayRow - g_battleChoiceScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (displayRow >= g_battleChoiceScrollOffset &&
		    displayRow - g_battleChoiceScrollOffset <
			    VISIBLE_ROW_COUNT) {
			if (FrontendDraw_PointInRect(&rect, cursorX, cursorY)) {
				FrontendDraw_Rect(&rect, 0, 0, g_colorGreen, 0);
				if (FrontendMouse_GetLeftClick() != 0 ||
				    FrontendMouse_GetRightClick() != 0) {
					if (g_gameConfig.sfxDatapadEnabled !=
					    0) {
						FrontendSound_PlayUISound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_gameConfig
									.sfxDatapadVolume,
							SOUND_CENTER_PAN);
					}
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_battleMissionList
							    [battleMissionIndex]
								    .fileName
									    [0] !=
						    SINGLE_PLAYER_FILE_PREFIX) {
							g_pilotData.missionDescriptionIds
								[g_pilotData
									 .missionDirectoryId] =
								g_battleMissionList
									[battleMissionIndex]
										.missionIdx;
							for (g_selectedMissionListIndex =
								     0;
							     g_missionCount >
							     (unsigned int)
								     g_selectedMissionListIndex;
							     ++g_selectedMissionListIndex) {
								if (g_missionList[g_selectedMissionListIndex]
									    .missionIdx ==
								    g_pilotData.missionDescriptionIds
									    [g_pilotData
										     .missionDirectoryId]) {
									g_pilotData
										.battleSequenceState
										.missionOrdinals
											[g_pilotData
												 .battleSequenceState
												 .currentMissionIndex] =
										g_missionList[g_selectedMissionListIndex]
											.missionIdx;
									g_pilotData
										.battleSequenceState
										.missionListIndices
											[g_pilotData
												 .battleSequenceState
												 .currentMissionIndex] =
										g_selectedMissionListIndex;
									break;
								}
							}
							FrontendMission_LoadCurrent();
							MissionSetup_UpdateTeamCounts();
							MissionSetup_LoadMissionDescText(
								g_missionText);
							g_frontendFirstVisibleLine =
								0;
							if (g_frontendMissionSessionMode !=
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								MissionSetup_SendLobbyState(
									0);
							}
						}
					} else {
						g_pilotData.missionDescriptionIds
							[g_pilotData
								 .missionDirectoryId] =
							g_battleMissionList
								[battleMissionIndex]
									.missionIdx;
						for (g_selectedMissionListIndex =
							     0;
						     g_missionCount >
						     (unsigned int)
							     g_selectedMissionListIndex;
						     ++g_selectedMissionListIndex) {
							if (g_missionList[g_selectedMissionListIndex]
								    .missionIdx ==
							    g_pilotData.missionDescriptionIds
								    [g_pilotData
									     .missionDirectoryId]) {
								g_pilotData
									.battleSequenceState
									.missionOrdinals
										[g_pilotData
											 .battleSequenceState
											 .currentMissionIndex] =
									battleMissionIndex;
								g_pilotData
									.battleSequenceState
									.missionListIndices
										[g_pilotData
											 .battleSequenceState
											 .currentMissionIndex] =
									g_selectedMissionListIndex;
								break;
							}
						}
						FrontendMission_LoadCurrent();
						MissionSetup_UpdateTeamCounts();
						MissionSetup_LoadMissionDescText(
							g_missionText);
						g_frontendFirstVisibleLine = 0;
					}
					Keyboard_FlushCharBuffer();
					FrontendScreen_PopState();
					Frontend_UnregisterScrollableControl(
						SCROLLBAR_CONTROL_ID);
				}
			}

			textColor = 0xFFFF;
			if (g_pilotData.missionDescriptionIds
				    [g_pilotData.missionDirectoryId] ==
			    g_missionList[battleMissionIndex].missionIdx) {
				textColor = g_colorYellow;
			}
			if (g_missionList[battleMissionIndex].fileName[0] ==
				    SINGLE_PLAYER_FILE_PREFIX &&
			    g_frontendMissionSessionMode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				textColor = g_colorGray;
			}
			FrontendText_DrawAlignedInRect(
				TEXT_FONT_SIZE,
				g_missionList[battleMissionIndex].description,
				&rect, 0, 1, textColor);
			FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
		}
		++displayRow;
		if (displayRow - g_battleChoiceScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}

	memset(lastSectionName, 0, sizeof(lastSectionName));
	FrontendDraw_RectAssign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				ROW_HEIGHT * battleMissionIndex +
					LIST_BOTTOM_PADDING);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	FrontendDraw_RectInsetXY(&rect, 2, 2);
	rect.left += MISSION_TEXT_INDENT;
	rect.bottom = rect.top + 14;
	displayRow = 0;

	for (battleMissionIndex = 0; (unsigned int)g_battleMissionListCount >
				     (unsigned int)battleMissionIndex;
	     ++battleMissionIndex) {
		if (g_battleMissionList[battleMissionIndex].isUnavailable !=
		    0) {
			continue;
		}
		if (strcmp(g_battleMissionList[battleMissionIndex].sectionName,
			   lastSectionName) != 0) {
			if (displayRow >= g_battleChoiceScrollOffset &&
			    displayRow - g_battleChoiceScrollOffset <
				    VISIBLE_ROW_COUNT) {
				FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
			}
			strcpy(lastSectionName,
			       g_battleMissionList[battleMissionIndex]
				       .sectionName);
			++displayRow;
		}
		if (displayRow - g_battleChoiceScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (displayRow >= g_battleChoiceScrollOffset &&
		    displayRow - g_battleChoiceScrollOffset <
			    VISIBLE_ROW_COUNT) {
			awardId = 0;
			awardFactionId = 0;
			if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				awardFactionId = g_pilotData.currentFactionId;
				switch (g_pilotData.missionDirectoryId) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spTrainingMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_MELEES:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spMeleeMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spTournaments
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spCombatMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					break;
				case MISSION_DIRECTORY_BATTLES:
					awardId =
						g_pilotData
							.factionStatistics
								[awardFactionId]
							.spBattles
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					break;
				default:
					break;
				}
			} else {
				switch (g_pilotData.missionDirectoryId) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					awardId =
						g_pilotData.factionStatistics[0]
							.mpTrainingMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpTrainingMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					rebelAwardId = awardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_MELEES:
					awardId =
						g_pilotData.factionStatistics[0]
							.mpMeleeMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpMeleeMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					rebelAwardId = awardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					awardId =
						g_pilotData.factionStatistics[0]
							.mpTournaments
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpTournaments
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					rebelAwardId = awardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					awardId =
						g_pilotData.factionStatistics[0]
							.mpCombatMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpCombatMissions
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					rebelAwardId = awardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				case MISSION_DIRECTORY_BATTLES:
					awardId =
						g_pilotData.factionStatistics[0]
							.mpBattles
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					imperialAwardId =
						g_pilotData.factionStatistics[1]
							.mpBattles
								[g_battleMissionList
									 [battleMissionIndex]
										 .missionIdx]
							.awardLevel;
					rebelAwardId = awardId;
					if (imperialAwardId != 0 &&
					    (imperialAwardId < awardId ||
					     awardId == 0)) {
						awardId = imperialAwardId;
						awardFactionId = 1;
					}
					break;
				default:
					break;
				}
			}

			if (awardId != 0) {
				FrontendDraw_RectCopy(&awardRect, &rect);
				awardRect.left -= MISSION_TEXT_INDENT;
				awardRect.right =
					awardRect.left + MISSION_TEXT_INDENT;
				if (g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				    g_pilotData.missionDirectoryId !=
					    MISSION_DIRECTORY_TRAINING_EXERCISES) {
					sprintf(g_frontendScratchBuffer,
						"medlvl%d", awardId);
					FrontImage_DrawSprite(
						g_frontendScratchBuffer,
						awardRect.left, awardRect.top);
					if (g_frontendMissionSessionMode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (rebelAwardId == 0 &&
						    imperialAwardId != 0) {
							sprintf(g_frontendScratchBuffer,
								"%s: %s",
								FrontendString_Get(
									FRONTSTR_759_IMPERIAL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											  imperialAwardId)));
						} else if (rebelAwardId != 0 &&
							   imperialAwardId ==
								   0) {
							sprintf(g_frontendScratchBuffer,
								"%s: %s",
								FrontendString_Get(
									FRONTSTR_760_REBEL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											  rebelAwardId)));
						} else {
							sprintf(g_frontendScratchBuffer,
								"%s: %s, %s: %s",
								FrontendString_Get(
									FRONTSTR_760_REBEL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											  rebelAwardId)),
								FrontendString_Get(
									FRONTSTR_759_IMPERIAL),
								FrontendString_Get((
									FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											  imperialAwardId)));
						}
					} else {
						strcpy(g_frontendScratchBuffer,
						       FrontendString_Get((
							       FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
										 awardId)));
					}
				} else {
					if (awardFactionId == 0) {
						sprintf(g_frontendScratchBuffer,
							"rcitlvl%d", awardId);
					} else {
						sprintf(g_frontendScratchBuffer,
							"citlvl%d", awardId);
					}
					if (g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
					    g_pilotData.missionDirectoryId !=
						    MISSION_DIRECTORY_TRAINING_EXERCISES) {
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							awardRect.left,
							awardRect.top);
						if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (rebelAwardId == 0 &&
							    imperialAwardId !=
								    0) {
								sprintf(g_frontendScratchBuffer,
									"%s: %s",
									FrontendString_Get(
										FRONTSTR_759_IMPERIAL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
												   imperialAwardId)));
							} else if (
								rebelAwardId !=
									0 &&
								imperialAwardId ==
									0) {
								sprintf(g_frontendScratchBuffer,
									"%s: %s",
									FrontendString_Get(
										FRONTSTR_760_REBEL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
												   rebelAwardId)));
							} else {
								sprintf(g_frontendScratchBuffer,
									"%s: %s, %s: %s",
									FrontendString_Get(
										FRONTSTR_760_REBEL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
												   rebelAwardId)),
									FrontendString_Get(
										FRONTSTR_759_IMPERIAL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
												   imperialAwardId)));
							}
						} else {
							strcpy(g_frontendScratchBuffer,
							       FrontendString_Get((
								       FrontendStringId)(AWARD_TOOLTIP_MEDAL_BASE +
											 awardId)));
						}
					} else {
						FrontImage_DrawSprite(
							g_frontendScratchBuffer,
							awardRect.left,
							awardRect.top);
						if (g_frontendMissionSessionMode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (rebelAwardId == 0 &&
							    imperialAwardId !=
								    0) {
								sprintf(g_frontendScratchBuffer,
									"%s: %s",
									FrontendString_Get(
										FRONTSTR_759_IMPERIAL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
												   imperialAwardId)));
							} else if (
								rebelAwardId !=
									0 &&
								imperialAwardId ==
									0) {
								sprintf(g_frontendScratchBuffer,
									"%s: %s",
									FrontendString_Get(
										FRONTSTR_760_REBEL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
												   rebelAwardId)));
							} else {
								sprintf(g_frontendScratchBuffer,
									"%s: %s, %s: %s",
									FrontendString_Get(
										FRONTSTR_760_REBEL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
												   rebelAwardId)),
									FrontendString_Get(
										FRONTSTR_759_IMPERIAL),
									FrontendString_Get(
										(FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
												   imperialAwardId)));
							}
						} else {
							strcpy(g_frontendScratchBuffer,
							       FrontendString_Get((
								       FrontendStringId)(AWARD_TOOLTIP_CITATION_BASE +
											 awardId)));
						}
					}
				}
				FrontendButton_DrawSpriteAndTooltip(
					&awardRect, NULL,
					g_frontendScratchBuffer, TEXT_FONT_SIZE,
					0xFFFF);
			}
			FrontendDraw_RectOffsetXY(&rect, 0, ROW_HEIGHT);
		}
		++displayRow;
		if (displayRow - g_battleChoiceScrollOffset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}
	return 0;
}
