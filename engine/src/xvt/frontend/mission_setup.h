#ifndef XVT_FRONTEND_MISSION_SETUP_H
#define XVT_FRONTEND_MISSION_SETUP_H

#include "xvt/assets/file.h"
#include "xvt/assets/object_type.h"
#include "xvt/frontend/pilot.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as uint8_t in the binary (IDB enum BattleLength). */
typedef uint8_t BattleLength;

enum {
	BATTLE_LENGTH_TWO_WINS = 0x0,
	BATTLE_LENGTH_THREE_WINS = 0x1,
	BATTLE_LENGTH_FOUR_WINS = 0x2,
};

/* Stored as uint8_t in the binary (IDB enum CraftSelectionMode). */
typedef uint8_t CraftSelectionMode;

enum {
	CRAFT_SELECTION_OFF = 0x0,
	CRAFT_SELECTION_ON = 0x1,
	CRAFT_SELECTION_HOST_ONLY = 0x2,
};

/* Stored as uint8_t in the binary (IDB enum CraftWaveMode). */
typedef uint8_t CraftWaveMode;

enum {
	CRAFT_WAVES_NONE = 0x0,
	CRAFT_WAVES_DEFAULT = 0x1,
	CRAFT_WAVES_UNLIMITED = 0x2,
};

/* Stored as uint8_t in the binary (IDB enum CombatBalanceMode). */
typedef uint8_t CombatBalanceMode;

enum {
	COMBAT_BALANCE_AUTOBALANCE = 0x0,
	COMBAT_BALANCE_FAVOR_IMPERIAL = 0x1,
	COMBAT_BALANCE_NEUTRAL = 0x2,
	COMBAT_BALANCE_FAVOR_REBEL = 0x3,
};

/* Stored as int8_t in the binary (IDB enum SequenceContinuationChoice). */
typedef int8_t SequenceContinuationChoice;

enum {
	SEQUENCE_RESTART = 0x0,
	SEQUENCE_CONTINUE = 0x1,
};

#pragma pack(push, 1)

struct MpRosterEntry {
	/* Player name. The local player's entry gets the pilot's name from the
	 * solo, concourse and host screens, or the session's player name from
	 * the modern build's network session; other entries get at most 13
	 * characters copied from the network player list, or "No name" when it
	 * lacks the id. */
	char name[14];
	/* DirectPlay id of the player; 0 for an empty entry, 1 for the pilot in
	 * a solo game. */
	int playerId;
	/* Pilot rating; its name is string FRONTSTR_154_DRONE plus the
	 * rating. */
	PilotRating pilotRating;
	/* 0, or the chosen preset's craft: set by
	 * FrontendNet_ProcessNetworkPackets from a player's CRAFT_LOADOUT or
	 * copied from the host's roster packets, and in a solo game from the
	 * local choice by the briefing. */
	int craftTypeOverride; ///< Nonzero exact craft type; zero selects the assigned flight group's base or
	///< optional craft.
	/* The chosen flight group craft option minus 1, so -1 for the group's
	 * own craft; set as craftTypeOverride is. */
	int craftOptionIndex; ///< Optional craft index; values above 9 fall back to the assigned flight group's
			      ///< base craft.
	/* A value n above 0 is the flight group's optional warhead n - 1; set
	 * as craftTypeOverride is. */
	int warheadOptionIndex; ///< Warhead loadout option index; zero uses the mission default.
	/* A value n above 0 is the flight group's optional beam n - 1; set as
	 * craftTypeOverride is. */
	int beamOptionIndex; ///< Beam loadout option index; zero uses the mission default.
	/* A value n above 0 is the flight group's optional countermeasure
	 * n - 1; set as craftTypeOverride is. */
	int countermeasureOptionIndex; ///< Countermeasure loadout option index; zero uses the mission default.
};

#pragma pack(pop)
typedef char
	xvt_size_MpRosterEntry[(sizeof(struct MpRosterEntry) == 42) ? 1 : -1];

struct MissionSetupPlayerAssignments {
	/* Player id in each of the 8 slots of each team; slot 0 is the team's
	 * captain, 0 an empty slot. */
	int teamPlayerIds[10][8];
	/* Ids of the players who hold a team slot, 0 for an unused entry. Most
	 * writers put a player at its g_mpRoster index; a drop on the team
	 * screen takes the first empty entry. */
	int assignedPlayerIds[8];
};

struct ShipListEntry {
	/* Model file named by the list, its last three characters lowercased to
	 * "opt"; at most 63 characters kept. */
	char modelFileName[64];
	/* Set by ShipList_Load; the tech library and the briefing's craft
	 * screen read it. */
	int craftType; ///< CraftSpecies value read from FRONTRES/frntspec.lst; stored as int by fscanf.
};

typedef enum MissionSetupActivePanel {
	MISSION_SETUP_PANEL_PLAYERS = 0x0,
	MISSION_SETUP_PANEL_SETTINGS = 0x1,
} MissionSetupActivePanel;

typedef enum MissionSetupDebriefTransition {
	MISSION_SETUP_DEBRIEF_TRANSITION_NONE = 0x0,
	MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION = 0x1,
	MISSION_SETUP_DEBRIEF_TRANSITION_ADVANCE_MISSION_DIRECTORY = 0x2,
} MissionSetupDebriefTransition;

struct MeleeTournamentTeamStandings {
	/* -1 for a team outside the tournament's standings.
	 * FrontendMission_InitPlayerState sets -1 for every team at a melee
	 * sequence's first mission, then 0 for teams with a human player or,
	 * with AI opponents or in a solo game, with player flight groups. For a
	 * team the AI flies, Mission_Init stores the team whose player flight
	 * group it copies, with the top bit (INT32_MIN) set when that group's
	 * craft is a fighter object type or the Headhunter, and reads it back
	 * past a sequence's first mission. */
	int aiOpponentSourceTeamAndTypeFlag;
	/* Team's points over the tournament: after each melee, fediskio.c adds
	 * its bonus and mission team scores. A team outside the standings is
	 * given the score last computed for a team before it instead (when
	 * there is none, an unset value in the original build and 0 in the
	 * modern one). */
	int totalScore;
	/* Melees in which no other team in the standings scored more than the
	 * team. */
	int firstPlaceCount;
	/* Melees in which exactly one other team in the standings scored
	 * more. */
	int secondPlaceCount;
	/* Melees in which exactly two other teams in the standings scored
	 * more. */
	int thirdPlaceCount;
};

struct MeleeTournamentSequenceState {
	/* Nothing reads or writes it by name; it is cleared with the rest of
	 * the state. */
	int reserved[9];
	/* Step of the tournament being flown, from 0; the debriefing raises it
	 * by 1 for the next melee. */
	int currentMissionIndex;
	/* Melees in the tournament: the first line of its sequence file, or the
	 * host's mission start packet on a client. */
	int missionCount;
	/* Each team's standing, by team index. */
	struct MeleeTournamentTeamStandings teamStandings[10];
	/* Human players at the tournament's first melee, counted then by
	 * FrontendMission_InitPlayerState; the tournament award reads it. */
	unsigned int humanPlayerCount;
	/* Teams in the standings: g_teamCount with AI opponents or in a solo
	 * game, else the teams with a human player. Set by
	 * FrontendMission_InitPlayerState at the tournament's first melee. */
	int participatingTeamCount;
	/* In a melee, Mission_Init raises by one the AI skill (groupAI, while
	 * under its maximum) of the first player flight group no human flies on
	 * this team and on each following team, one team per boost: 3 on easy,
	 * 2 on medium, else 1. It picks a random team with player flight groups
	 * but no human, or takes team 0's player flight group count when there
	 * is none, and in a sequence keeps it here for the later melees. */
	int aiBoostFirstTeam;
};

typedef enum BattleMissionResult {
	BATTLE_MISSION_RESULT_IMPERIAL_VICTORY = 0x0,
	BATTLE_MISSION_RESULT_REBEL_VICTORY = 0x1,
	BATTLE_MISSION_RESULT_DRAW = 0x2,
} BattleMissionResult;

struct BattleSequenceState {
	/* Step of the battle being flown, from 0: raised by 1 when the battle
	 * goes on to its next mission, and lowered by 1 after a draw so that
	 * step is flown again. */
	uint32_t currentMissionIndex;
	/* Mission id of the combat engagement chosen for the current step.
	 * Nothing reads it. */
	int currentMissionId;
	/* Wins that end the battle: g_gameConfig.battleLengthIndex + 2, or the
	 * host's mission start packet on a client. */
	int victoriesNeeded;
	/* Result of each step, written by fediskio.c after the mission:
	 * IMPERIAL_VICTORY when team 0 is the first team with its primary goals
	 * complete and its prevent goals not, REBEL_VICTORY when team 1 is,
	 * else DRAW. */
	BattleMissionResult missionResults[10];
	/* The mission flown at each step of the sequence: its position in the sequence descriptor's mission
	 * list, which the repeat checks compare against. The multiplayer battle choice stores the mission id
	 * instead, and starting a combat engagement sequence stores a mission list index in the first slot. */
	/* Written by MissionSetup_Update's mission start,
	 * MissionSetup_SelectFirstSequenceMission,
	 * MissionSetup_SelectNextSequenceMission, and the battle choice screen
	 * and its list. MissionSetup_SelectNextSequenceMission and
	 * MissionSetup_BattleChoice_BuildList read the entries before
	 * currentMissionIndex to skip missions already flown, and after a draw
	 * the former reuses the drawn step's ordinal. */
	int missionOrdinals[10];
	/* Index in g_missionList of the mission flown at each step. */
	int missionListIndices[10];
	/* Human players in the latest mission, counted by
	 * FrontendMission_InitPlayerState. */
	unsigned int humanPlayerCount;
	/* Score over the battle: fediskio.c sets it to the mission score after
	 * the first step and adds it after each later one. */
	int cumulativeScore;
};

struct CampaignSequenceState {
	/* Nothing reads or writes it by name. */
	int32_t unused00; ///< Unresolved campaign-sequence field; cleared and persisted with the full state.
	/* Raised by 1 by the debriefing and when a saved campaign continues;
	 * MissionSetup_SelectNextSequenceMission lowers it by 1 when
	 * lastMissionCompleted is 0, so that mission is flown again. */
	int32_t currentMissionIndex; ///< Zero-based position of the current campaign mission.
	/* From MissionSetup_SelectFirstSequenceMission, or the host's mission
	 * start packet on a client. */
	int32_t missionCount; ///< Mission count read from the selected campaign descriptor.
	/* Copied by fediskio.c from the pilot's team's isMissionCompleted after
	 * each campaign mission. */
	int32_t lastMissionCompleted; ///< Whether the just-finished campaign mission completed successfully.
	/* Counted by FrontendMission_InitPlayerState for each mission. */
	int32_t humanPlayerCount; ///< Human players participating in the campaign mission.
	/* fediskio.c sets it to the mission score after a completed first
	 * mission and adds the score of each later completed one. */
	int32_t cumulativeScore; ///< Campaign score accumulated across completed missions.
};

struct BattleContinuation {
	/* Nothing reads or writes it by name. */
	int32_t unused00; ///< Unresolved persisted battle-continuation field.
	/* g_gameConfig.randomSeed when the debriefing saved the battle.
	 * Continuing does not put it back into g_gameConfig: a host sends it,
	 * and a client compares it with its own saved seed to keep or zero its
	 * cumulative score. */
	uint32_t
		randomSeed; ///< Random seed used to reproduce mission selection.
	/* Set by the debriefing while the battle is unfinished (in a network
	 * game only on the host) and cleared when it ends or is not
	 * continued. */
	int32_t isActive; ///< Continuation slot contains an unfinished battle.
	/* g_gameConfig.battleLengthIndex when saved; put back into g_gameConfig
	 * when the battle is selected. */
	int32_t battleLengthIndex; ///< Configured battle-length selector.
	/* g_gameConfig.randomSetup when saved; put back into g_gameConfig when
	 * the battle is selected. */
	int32_t randomSetup; ///< Configured sequential, random, or player-choice selection mode.
	/* Copy of g_pilotData.battleSequenceState when saved, copied back when
	 * the battle continues. */
	struct BattleSequenceState
		sequenceState; ///< Saved battle sequence state.
};

struct CampaignContinuation {
	/* Nothing reads or writes it by name. */
	int32_t unused00; ///< Unresolved persisted campaign-continuation field.
	/* g_gameConfig.randomSeed when the debriefing saved the campaign.
	 * Continuing does not put it back into g_gameConfig: a host sends it,
	 * and a client compares it with the seed of its own entry (the campaign
	 * id plus 12) to keep or zero its cumulative score. */
	uint32_t
		randomSeed; ///< Random seed used to reproduce mission selection.
	/* Set by the debriefing while the campaign is unfinished, cleared when
	 * it ends or is not continued; a client's own entry (the campaign id
	 * plus 12) is always saved with 0. */
	int32_t isActive; ///< Continuation slot contains an unfinished campaign.
	/* g_gameConfig.randomSetup when saved; put back into g_gameConfig when
	 * the campaign is selected. */
	int32_t randomSetup; ///< Configured sequential, random, or player-choice selection mode.
	/* Copy of g_pilotData.campaignSequenceState when saved, copied back
	 * when the campaign continues. */
	struct CampaignSequenceState
		sequenceState; ///< Saved campaign sequence state.
};

extern int g_teamPlayerFlightGroupCount[10];
extern int g_missionSetupDraggedPlayerId;
extern int g_missionSetupReservedPlayerIds[8];
extern int g_missionSetupReservedPlayerCount;
extern int g_missionSetupTeamAssignmentSkipped;
extern int g_missionSetupCountdownClockMs;
extern int g_missionSetupLaunchCountdownMs;
extern int g_missionSetupCountdownPreviousClockMs;
extern int g_missionSetupLaunchSignalSent;
extern int g_teamCount;
extern int g_missionSetupLastBroadcastCountdownSecond;
extern int g_missionSetupPlayerFlightGroupIndices[80];
extern int g_textShadeRamps[5][8];
extern struct MissionSetupPlayerAssignments g_missionSetupPlayerAssignments;
extern struct ShipListEntry *g_shipList;
extern int g_shipTypeToShipListIndex[18];
extern int g_shipCount;
extern int g_missionSetupSelectedFlightGroupIndex;
extern int g_missionSetupSelectedFlightGroupCraftOptionIndex;
extern int g_missionSetupSelectedPresetCraftOptionIndex;
extern int g_missionSetupPresetCraftOptionCount;
extern int g_missionSetupSelectedWarheadOptionIndex;
extern int g_missionSetupSelectedBeamOptionIndex;
extern int g_missionSetupSelectedCountermeasureOptionIndex;
extern int g_missionSetupSelectedWaveCountMinusOne;
extern int g_missionSetupSelectedCraftCount;
extern int g_missionSetupFlightGroupCraftOptionCount;
extern int g_missionSetupWarheadOptionCount;
extern int g_missionSetupBeamOptionCount;
extern int g_missionSetupCountermeasureOptionCount;
extern const int g_warheadTypeMap[11];
extern const int g_presetCraftTypes[11];
extern const uint8_t g_craftIffCounterpart[20];
extern unsigned int g_missionCount;
extern int g_selectedMissionListIndex;
extern struct MissionListEntry *g_missionList;
extern int g_frontendGameSessionInProgress;
extern int g_missionSetupIsHost;
extern int g_missionSetupRosterAuthoritative;
extern int g_missionSetupBeginButtonLockoutFrames;
extern int g_frontendSkipScreenEntrySetup;
extern const char *g_missionDirectoryNames[6];
extern struct MpRosterEntry g_mpRoster[8];
extern int g_mpRosterReadyFlags[8];
extern int g_battleMissionListCount;
extern struct MissionListEntry *g_battleMissionList;
extern int g_battleChoiceClockMs;
extern int g_battleChoiceScrollOffset;
extern int g_battleChoiceRemainingMs;
extern int g_battleChoicePreviousClockMs;
extern int g_battleChoiceRowCount;
extern int g_battleChoiceLastSentSecond;
extern int g_battleChoiceTimeoutHandled;
extern int g_missionSetupUseCombatSimPilotState;
extern int g_missionSetupMissionListRowCount;
extern int g_missionSetupMissionListScrollOffset;
extern int g_remoteBattleContinuationActive;
extern int g_remoteBattleSequenceContinuationChoice;
extern int g_remoteBattleLastCompletedMissionIndex;
extern int g_remoteBattleRebelVictoryCount;
extern int g_remoteBattleImperialVictoryCount;
extern int g_missionSetupSelectedPlayerRosterIndex;
extern MissionSetupActivePanel g_missionSetupActivePanel;
extern int g_missionSetupLastHostBroadcastMs;
extern int g_localPilotNetworkPlayerIndex;
extern int g_missionSetupShowDescriptionPanel;
extern MissionSetupDebriefTransition g_missionSetupDebriefTransition;

int MissionSetup_Exit(int frameCounter);
int MissionSetup_Update(int frameCounter);
int MissionSetup_DrawMissionTypeControls(void);
void MissionSetup_LoadMissionList(int missionDirectoryId);
void MissionSetup_LoadMissionDescText(char *outText4096);
int MissionSetup_DrawMissionDescription(void);
int MissionSetup_DrawPlayerRoster(int frameCounter);
int MissionSetup_BroadcastLobbySelection(void);
int MissionSetup_SendLobbyState(int toPlayerId);
int MissionSetup_BroadcastReadyRoster(int toPlayerId);
int MissionSetup_DrawMissionList(int frameCounter);
int MissionSetup_SelectFirstSequenceMission(void);
int MissionSetup_DrawGameSettings(void);
int MissionSetup_CountMissionListEntries(XvtFile *stream);
int MissionSetup_DrawBackground(void);
int MissionSetup_UseRebelBackground(void);
void MissionSetup_DrawCraftLoadout(void);
void MissionSetup_DrawPlayerLoadouts(int frameCounter);
int MissionSetup_UpdateCraftLoadout(void);
void MissionSetup_InitCraftLoadout(void);
int MissionSetup_GetWarheadType(int playerRosterIndex);
int MissionSetup_GetBeamType(int playerRosterIndex);
int MissionSetup_GetCountermeasureType(int playerRosterIndex);
int MissionSetup_GetCraftType(int playerRosterIndex);
void ShipList_Load(void);
int MissionSetup_ExitNextMission(void);
int MissionSetup_EnterNextMission(int frameCounter);
int MissionSetup_PruneDisconnectedPlayers(void);
int MpRoster_CompactActiveEntries(void);
int MissionSetup_SelectNextSequenceMission(void);
int MissionSetup_ExitCurrentMission(void);
int MissionSetup_EnterCurrentMission(int frameCounter);
int MissionSetup_TeamAssignmentUpdate(int frameCounter);
int MissionSetup_DrawUnassignedPlayers(int frameCounter);
void MissionSetup_UpdateTeamCounts(void);
void MissionSetup_DrawTeamAssignments(int frameCounter);
void MissionSetup_PruneTeamAssignments(void);
int MissionSetup_IsTeamAssignmentValid(void);
int MissionSetup_UpdateTeamControls(void);
int MissionSetup_RandomizeTeamAssignments(void);
int MissionSetup_ClearTeamAssignments(void);
int MissionSetup_BroadcastTeamAssignments(void);
int MissionSetup_TryContinueBattle(void);
int MissionSetup_TryContinueCampaign(void);
int MissionSetup_DrawTeamMissionDescription(void);
int MissionSetup_FreeScreenResources(int frameCounter);
int MissionSetup_FlightAssignmentUpdate(int frameCounter);
int MissionSetup_DrawAssignmentControls(void);
int MissionSetup_DrawFlightAssignments(int frameCounter);
void MissionSetup_PruneFlightAssignments(void);
int MissionSetup_AreFlightAssignmentsComplete(void);
int MissionSetup_RandomizeFlightAssignments(void);
int MissionSetup_ClearFlightAssignments(void);
int MissionSetup_FillFlightAssignments(void);
int MissionSetup_DrawAssignedPlayers(int frameCounter);
int MissionSetup_DrawAssignmentMissionDescription(void);
int MissionSetup_BattleChoice_Exit(void);
int MissionSetup_BattleChoice_Update(int frameCounter);
int MissionSetup_BattleChoice_DrawDescription(void);
int MissionSetup_BattleChoice_DrawRoster(int frameCounter);
int MissionSetup_BattleChoice_BuildList(void);
int MissionSetup_BattleChoice_DrawList(int frameCounter);

#ifdef __cplusplus
}
#endif

#endif
