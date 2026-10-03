#ifndef XVT_FRONTEND_PILOT_RECORD_H
#define XVT_FRONTEND_PILOT_RECORD_H

#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_pilotRecordPage;
extern POINT g_pilotRatingIconPos[25];
extern int g_campaignSingleplayerAwardCount;
extern int g_campaignMedalScrollOffset;
extern int g_campaignSingleplayerAwardFlags[];
extern int g_campaignMultiplayerAwardCount;
extern int g_campaignMultiplayerAwardFlags[];
extern int g_campaignMedalEntryCount;
extern int g_cutsceneViewerScrollRow;
extern int g_cutsceneViewerTotalRows;
extern char g_pilotRecordNameInput[14];
extern int g_pilotListScrollOffset;

#pragma pack(push, 1)

struct PilotNetworkPlayer {
	/* Nothing writes it by name, so it holds what the pilot file held, or 0
	 * once MissionSetup_Update clears the entry.
	 * FrontendFlight_LaunchSession and the modern XvtLaunchTask_Queue copy
	 * the local player's entry's into the flight command line. */
	char formalName[14];
	/* The player's name from g_mpRoster, cut to 12 characters, set at
	 * launch by FrontendMission_InitPlayerState; the debriefing shows
	 * it. */
	char friendlyName[14];
	/* Flight group the player flies, from
	 * g_missionSetupPlayerFlightGroupIndices at launch. */
	int flightGroupId;
	/* The player's DirectPlay id, 0 for an empty entry; set at launch, set
	 * to 0 by MissionSetup_PruneDisconnectedPlayers. */
	int directPlayId;
	int rating; /* The player's pilot rating at launch, from g_mpRoster. */
	/* The player's mission score plus its team's bonus score, set by
	 * FeDiskIo_CommitFlightResults; 0 at launch and when the next mission
	 * or a replay starts. */
	int totalScore;
	/* The player's full kills of the mission's flight groups, summed by
	 * FeDiskIo_CommitFlightResults; 0 at launch, next mission and
	 * replay. */
	int kills;
	int killsShared; /* The player's shared kills, summed like kills. */
	/* Only ever set to 0, at launch, next mission and replay; only
	 * MissionDebrief_DrawTeamStatisticsPage, which nothing calls, reads
	 * it. */
	int craftInspected;
	int killsAssist; /* The player's kill assists, summed like kills. */
	/* Craft the player lost in the mission
	 * (perMissionKills.totalCraftLosses), set by
	 * FeDiskIo_CommitFlightResults. */
	int totalLosses;
	/* Craft type the player chose, from g_mpRoster craftTypeOverride; 0
	 * keeps the flight group's own craft. Mission_Init applies it. */
	int craftId;
	/* Index into the flight group's optionalCraft chosen, or -1 for none;
	 * FrontendMission_InitPlayerState sets -1 when that entry is
	 * CRAFT_SPECIES_UNKNOWN. Mission_Init applies it. */
	int craftOption;
	/* Index into the flight group's optionalWarheads: the roster's
	 * warheadOptionIndex minus 1; -1 keeps the flight group's own. */
	int warheadOption;
	/* Optional beam index: the roster's beamOptionIndex minus 1; -1 keeps
	 * the flight group's own. */
	int beamOption;
	/* Optional countermeasure index: the roster's countermeasureOptionIndex
	 * minus 1; -1 keeps the flight group's own. */
	int countermeasureOption;
	/* 1 once the player left the flight, set by
	 * FlightNet_MarkPilotNetworkPlayerLeft; 0 at launch, next mission and
	 * replay. The debriefing grays the player's name. */
	int hasLeft;
};

#pragma pack(pop)
typedef char xvt_size_PilotNetworkPlayer[(sizeof(PilotNetworkPlayer) == 88)
						 ? 1
						 : -1];

#pragma pack(push, 1)

struct PilotTeam {
	/* The team's score in the last mission: its bonus score plus, in a
	 * melee, its mission score, else its network players' mission scores.
	 * Set by FeDiskIo_CommitFlightResults for teams 0 to 7. */
	int missionScore;
	/* 1 when the team's primary goal status is 1 and its prevent goal
	 * status is not 1, else 0. */
	int isMissionCompleted;
	/* No code reads or writes it by name; MissionSetup_EnterCurrentMission
	 * uses the address of teams[2]'s as the end of a walk over
	 * networkPlayers. */
	int unknown08;
	/* The team's teamMissionCompletionTimeSeconds from the flight, in
	 * seconds. */
	int missionTime;
	int kills; /* The team's full kills in the mission (teamKillStats). */
	/* The team's shared kills in the mission (teamKillStats). */
	int killsShared;
	/* The team's craft losses in the mission (teamKillStats). */
	int losses;
};

#pragma pack(pop)
typedef char xvt_size_PilotTeam[(sizeof(PilotTeam) == 28) ? 1 : -1];

#pragma pack(push, 1)

struct PilotMission {
	/* Times the mission was flown, counted up by
	 * FeDiskIo_CommitFlightResults; the mission achievements page lists the
	 * mission once it is not 0. */
	int numberTimesFlown;
	/* Flights whose primary goal status was 1, counted up by
	 * FeDiskIo_CommitFlightResults; nothing reads it by name. */
	int completedCount;
	/* Flights whose primary goal status was 2 or prevent goal status 1,
	 * counted up by FeDiskIo_CommitFlightResults; nothing reads it by
	 * name. */
	int failedCount;
	/* Highest score of a flight; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int bestScore;
	/* Shortest nonzero completion time of the player's team, in seconds, 0
	 * until one is recorded; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int bestTime;
	/* Best finish of a melee, 1 for first; 0 until one is recorded and for
	 * other mission types. */
	int bestPlacement;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement
	 * FeDiskIo_CommitFlightResults moves one of the faction's
	 * missionEvaluations or meleePlaques counts from the old level to the
	 * new. A flight with no award clears a 6 and takes it off that
	 * count. */
	int awardLevel;
	/* Biggest lead over the closest other team when a melee finished first;
	 * only FeDiskIo_CommitFlightResults reads it, to compare. */
	unsigned int bestMargin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in PilotFaction). */
	int field20;
};

#pragma pack(pop)
typedef char xvt_size_PilotMission[(sizeof(PilotMission) == 36) ? 1 : -1];

#pragma pack(push, 1)

struct PilotMultiplayerMission {
	/* Times the mission was flown in multiplayer, counted up by
	 * FeDiskIo_CommitFlightResults; the mission achievements page lists the
	 * mission once it is not 0. */
	int numberTimesFlown;
	/* Melee flights finished first, counted up by
	 * FeDiskIo_CommitFlightResults; nothing reads it by name. */
	int firstPlaceCount;
	/* Melee flights finished second, counted up by
	 * FeDiskIo_CommitFlightResults; nothing reads it by name. */
	int secondPlaceCount;
	/* Melee flights finished third, counted up by
	 * FeDiskIo_CommitFlightResults; nothing reads it by name. */
	int thirdPlaceCount;
	/* Flights whose primary goal status was 1, counted up by
	 * FeDiskIo_CommitFlightResults; nothing reads it by name. */
	int completedCount;
	/* Flights whose primary goal status was 2 or prevent goal status 1,
	 * counted up by FeDiskIo_CommitFlightResults; nothing reads it by
	 * name. */
	int failedCount;
	/* Highest score of a flight; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int bestScore;
	/* Shortest nonzero completion time of the player's team, in seconds, 0
	 * until one is recorded; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int bestTime;
	/* Best finish of a melee, 1 for first; 0 until one is recorded and for
	 * other mission types. */
	int bestPlacement;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. Every award other than
	 * 6 a flight wins adds one to the faction's missionEvaluations or
	 * meleePlaques count, replaced or not, and a replaced 6 comes off it. A
	 * flight with no award clears a 6 and takes it off that count. */
	int awardLevel;
	/* Biggest lead over the closest other team when a melee finished first;
	 * only FeDiskIo_CommitFlightResults reads it, to compare. */
	unsigned int bestMargin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in PilotFaction). */
	int field2C;
};

#pragma pack(pop)
typedef char xvt_size_PilotMultiplayerMission
	[(sizeof(PilotMultiplayerMission) == 48) ? 1 : -1];

#pragma pack(push, 1)

struct PilotTournament {
	/* Tournaments started: FeDiskIo_CommitFlightResults counts it up when
	 * it commits a tournament's first mission (currentMissionIndex 0).
	 * Single-player tables take a tournament with one human player. */
	int attemptCount;
	/* Tournaments finished: counted up when FeDiskIo_CommitFlightResults
	 * commits the last mission of one (missionCount - currentMissionIndex
	 * is 1) other than its first; nothing reads it by name. */
	int completedCount;
	/* Tournaments finished first overall, counted with completedCount;
	 * nothing reads it by name. */
	int firstPlaceCount;
	/* Tournaments finished second overall, counted with completedCount;
	 * nothing reads it by name. */
	int secondPlaceCount;
	/* Tournaments finished third overall, counted with completedCount;
	 * nothing reads it by name. */
	int thirdPlaceCount;
	/* Highest total score of the local team at a tournament's end. */
	int bestScore;
	/* Best overall finish at a tournament's end, 1 for first; 0 until one
	 * is recorded. */
	int bestPlacement;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement
	 * FeDiskIo_CommitFlightResults moves one of the faction's
	 * tournamentTrophies counts from the old level to the new. A flight
	 * with no award clears a 6 and takes it off that count. */
	int awardLevel;
	/* Biggest overall lead when a tournament finished first; only
	 * FeDiskIo_CommitFlightResults reads it, to compare. */
	unsigned int bestMargin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in PilotFaction). */
	int field24;
};

#pragma pack(pop)
typedef char xvt_size_PilotTournament[(sizeof(PilotTournament) == 40) ? 1 : -1];

#pragma pack(push, 1)

struct PilotMultiplayerTournament {
	/* Tournaments started: FeDiskIo_CommitFlightResults counts it up when
	 * it commits a tournament's first mission (currentMissionIndex 0).
	 * Multiplayer tables take a tournament with any other number of human
	 * players. */
	int attemptCount;
	/* Tournaments finished: counted up when FeDiskIo_CommitFlightResults
	 * commits the last mission of one (missionCount - currentMissionIndex
	 * is 1) other than its first; nothing reads it by name. */
	int completedCount;
	/* Tournaments finished first overall, counted with completedCount;
	 * nothing reads it by name. */
	int firstPlaceCount;
	/* Tournaments finished second overall, counted with completedCount;
	 * nothing reads it by name. */
	int secondPlaceCount;
	/* Tournaments finished third overall, counted with completedCount;
	 * nothing reads it by name. */
	int thirdPlaceCount;
	/* Highest total score of the local team at a tournament's end. */
	int bestScore;
	/* Best overall finish at a tournament's end, 1 for first; 0 until one
	 * is recorded. */
	int bestPlacement;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field1C;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. Every award a flight
	 * wins adds one to the faction's tournamentTrophies count, replaced or
	 * not, and a replaced 6 comes off it. A flight with no award clears a 6
	 * and takes it off that count. */
	int awardLevel;
	/* Biggest overall lead when a tournament finished first; only
	 * FeDiskIo_CommitFlightResults reads it, to compare. */
	unsigned int bestMargin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in PilotFaction). */
	int field28;
};

#pragma pack(pop)
typedef char xvt_size_PilotMultiplayerTournament
	[(sizeof(PilotMultiplayerTournament) == 44) ? 1 : -1];

#pragma pack(push, 1)

struct PilotBattle {
	/* Battles started: FeDiskIo_CommitFlightResults counts it up when it
	 * commits a battle's first mission (currentMissionIndex 0).
	 * Single-player tables take a battle with fewer than 2 human
	 * players. */
	int attemptCount;
	/* Flights committed after which the local player's side (team 0
	 * Imperial, 1 Rebel) had victoriesNeeded wins;
	 * FeDiskIo_CommitFlightResults counts it up; the mission achievements
	 * page tests it. */
	int victoryCount;
	/* Flights committed after which the other side had victoriesNeeded
	 * wins; FeDiskIo_CommitFlightResults counts it up; nothing reads it by
	 * name. */
	int defeatCount;
	/* FeDiskIo_CommitFlightResults counts it up when it commits a flight
	 * whose currentMissionIndex is 10, also when it counted a victory or
	 * defeat for that flight; nothing reads it by name. */
	int drawCount; ///< Battles reaching the sequence mission limit without either side winning.
	/* Highest cumulative battle score (battleSequenceState.cumulativeScore)
	 * after a flight. */
	int bestScore;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field14;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement
	 * FeDiskIo_CommitFlightResults moves one of the faction's
	 * battleMedallions counts from the old level to the new. A flight with
	 * no award clears a 6 and takes it off that count. */
	int awardLevel;
	/* Biggest margin of a won battle: the winner's victories less the
	 * loser's, doubled for one human player at hard difficulty. */
	unsigned int bestVictoryMargin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in PilotFaction). */
	int field20;
};

#pragma pack(pop)
typedef char xvt_size_PilotBattle[(sizeof(PilotBattle) == 36) ? 1 : -1];

#pragma pack(push, 1)

struct PilotMultiplayerBattle {
	/* Battles started: FeDiskIo_CommitFlightResults counts it up when it
	 * commits a battle's first mission (currentMissionIndex 0). Multiplayer
	 * tables take a battle with 2 or more human players. */
	int attemptCount;
	/* Flights committed after which the local player's side (team 0
	 * Imperial, 1 Rebel) had victoriesNeeded wins;
	 * FeDiskIo_CommitFlightResults counts it up; the mission achievements
	 * page tests it. */
	int victoryCount;
	/* Flights committed after which the other side had victoriesNeeded
	 * wins; FeDiskIo_CommitFlightResults counts it up; nothing reads it by
	 * name. */
	int defeatCount;
	/* FeDiskIo_CommitFlightResults counts it up when it commits a flight
	 * whose currentMissionIndex is 10, also when it counted a victory or
	 * defeat for that flight; nothing reads it by name. */
	int drawCount; ///< Battles reaching the sequence mission limit without either side winning.
	/* Highest cumulative battle score (battleSequenceState.cumulativeScore)
	 * after a flight. */
	int bestScore;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field14;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field18;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. Every award a flight
	 * wins adds one to the faction's battleMedallions count, replaced or
	 * not, and a replaced 6 comes off it. A flight with no award clears a 6
	 * and takes it off that count. */
	int awardLevel;
	/* Biggest margin of a won battle: the winner's victories less the
	 * loser's, doubled for one human player at hard difficulty. */
	unsigned int bestVictoryMargin;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field24;
};

#pragma pack(pop)
typedef char xvt_size_PilotMultiplayerBattle
	[(sizeof(PilotMultiplayerBattle) == 40) ? 1 : -1];

#pragma pack(push, 1)

struct PilotCampaign {
	/* Campaigns started: FeDiskIo_CommitFlightResults counts it up when it
	 * commits a campaign's first mission (currentMissionIndex 0), in
	 * spCampaigns with fewer than 2 human players, else in mpCampaigns. */
	int attemptCount;
	/* One past the furthest mission completed in the campaign: raised to
	 * currentMissionIndex + 1 when a mission is completed; the achievements
	 * page shows it plus 1. */
	int nextMissionIndex;
	/* 1 once FeDiskIo_CommitFlightResults commits a completed mission that
	 * leaves currentMissionIndex equal to missionCount;
	 * MissionDebrief_Update also sets the single-player entry after a won
	 * campaign's last mission. */
	int isFinished;
	/* Highest campaign score: cumulativeScore after a completed mission, or
	 * cumulativeScore plus the mission's score after a failed one. */
	int bestScore;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field10;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field14;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field18;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field1C;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field20;
};

#pragma pack(pop)
typedef char xvt_size_PilotCampaign[(sizeof(PilotCampaign) == 36) ? 1 : -1];

#pragma pack(push, 1)

struct PilotCampaignMission {
	/* Campaign the mission was last flown in, by its id in the campaign
	 * list (missionDescriptionIds[MISSION_DIRECTORY_CAMPAIGNS]); the medals
	 * and achievements pages match it. */
	int campaignId;
	/* Times the mission was flown in a campaign, counted up by
	 * FeDiskIo_CommitFlightResults. MissionSetup_LoadMissionList marks a
	 * campaign mission entry unavailable while the single-player count is
	 * 0, or in network play while factions 0 and 1 both have a multiplayer
	 * count of 0. */
	int numberTimesFlown;
	/* 1 once the mission was completed with g_gameConfig.difficulty no
	 * higher than GAME_DIFFICULTY_HARD and waves not CRAFT_WAVES_UNLIMITED;
	 * the campaign medals page draws its award. */
	int awardEligible;
	/* Highest score of a flight with waves not CRAFT_WAVES_UNLIMITED; only
	 * FeDiskIo_CommitFlightResults reads it, to compare. */
	unsigned int bestScore;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement in
	 * spCampaignMissions FeDiskIo_CommitFlightResults moves one of the
	 * faction's missionEvaluations counts from the old level to the new; in
	 * mpCampaignMissions every award other than 6 a flight wins adds one to
	 * that count, replaced or not, and a replaced 6 comes off it. A flight
	 * with no award clears a 6 and takes it off that count. */
	int awardLevel;
	/* Shortest nonzero completion time of the player's team, in seconds;
	 * only FeDiskIo_CommitFlightResults reads it, to compare. */
	int bestTime;
	/* 1 once a flight of the mission was completed; unlocks the cutscenes
	 * shown after it. */
	int isCompleted;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int field1C;
};

#pragma pack(pop)
typedef char xvt_size_PilotCampaignMission[(sizeof(PilotCampaignMission) == 32)
						   ? 1
						   : -1];

#pragma pack(push, 1)

/* Accumulated combat statistics indexed by the game's three mission types. */
struct PilotStats {
	int totalScorePerMT[3]; ///< Accumulated score by mission type (3).
	int standaloneMissionsPlayedPerMT
		[3]; ///< Standalone missions played by mission type (3).
	int sequenceMissionsPlayedPerMT
		[3]; ///< Sequence missions played by mission type (3).
	int totalKillsPerMT[3]; ///< Full kills by mission type (3).
	int totalFriendliesKilledPerMT
		[3]; ///< Friendly kills by mission type (3).
	int killsPerCraftPerMT
		[3][100]; ///< Full kills by mission type and craft type (100).
	int killsSharedPerCraftPerMT
		[3]
		[100]; ///< Shared kills by mission type and craft type (100).
	int killsAssistsPerCraftPerMT
		[3]
		[100]; ///< Kill assists by mission type and craft type (100).
	int killsFullOnPlayerRatingPerMT
		[3]
		[25]; ///< Full kills by mission type and victim player rating (25).
	int killsSharedOnPlayerRatingPerMT
		[3]
		[25]; ///< Shared kills by mission type and victim player rating (25).
	int killsAssistOnPlayerRatingPerMT
		[3]
		[25]; ///< Kill assists by mission type and victim player rating (25).
	int killsFullOnAIRatingPerMT
		[3]
		[6]; ///< Full kills by mission type and victim AI rating (6).
	int killsSharedOnAIRatingPerMT
		[3]
		[6]; ///< Shared kills by mission type and victim AI rating (6).
	int killsAssistOnAIRatingPerMT
		[3]
		[6]; ///< Kill assists by mission type and victim AI rating (6).
	int numSpecialInspectedPerMT
		[3]; ///< Special-object inspections by mission type (3).
	int energyHitsPerMT
		[3]; ///< Combined laser and ion hits by mission type (3).
	int energyFiredPerMT
		[3]; ///< Combined laser and ion shots fired by mission type (3).
	int warheadsHitsPerMT[3];  ///< Warhead hits by mission type (3).
	int warheadsFiredPerMT[3]; ///< Warheads fired by mission type (3).
	int totalCraftLossesPerMT
		[3]; ///< Total craft losses by mission type (3).
	int lossesByCollisionsPerMT
		[3]; ///< Collision losses by mission type (3).
	int lossesByStarshipsPerMT
		[3];		   ///< Losses to starships by mission type (3).
	int lossesByMinesPerMT[3]; ///< Losses to mines by mission type (3).
	int killedByPlayerRatingPerMT
		[3]
		[25]; ///< Deaths by mission type and opposing player rating (25).
	int killedByAIRatingPerMT
		[3][6]; ///< Deaths by mission type and opposing AI rating (6).
};

#pragma pack(pop)
typedef char xvt_size_PilotStats[(sizeof(PilotStats) == 5256) ? 1 : -1];

#pragma pack(push, 1)

struct PilotFaction {
	int totalMissionsPlayedCount; ///< Total missions played for this faction.
	/* Team last chosen for this faction's missions, saved from g_pilotData
	 * when a mission launches (FrontendMission_InitPlayerState, entry 2 in
	 * network play) and by the mission setup screens, and copied back into
	 * g_pilotData when this faction or the pilot is selected. */
	int team;
	/* Mission directory last chosen for this faction, saved and restored
	 * like team. */
	MissionDirectoryId missionDirectoryId;
	/* Mission or sequence last chosen in each directory, saved and restored
	 * like team; a single-player launch saves only the entries of the
	 * current directory's kind. */
	int missionDescriptionIds[6];
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	uint8_t field24[32];
	int missionSequenceActive; ///< Persisted active-sequence flag mirrored to
				   ///< PilotData.missionSequenceActive.
	int missionSequenceDescriptionId; ///< Persisted sequence descriptor ID mirrored to
	///< PilotData.missionSequenceDescriptionId.
	int meleePlaques[6]; ///< Melee plaque counts by award level 1-6.
	int tournamentTrophies
		[6]; ///< Tournament trophy counts by award level 1-6.
	int missionEvaluations
		[6]; ///< Mission evaluation counts by award level 1-6.
	int battleMedallions
		[6]; ///< Battle medallion counts by award level 1-6.
	int missionAwards
		[4]; ///< Current mission award levels: melee, tournament, evaluation, battle.
	/* Pilot_LoadXvtRecord and Pilot_WriteXvtRecord copy it from and to the
	 * base-game record; nothing else reads it. */
	uint8_t fieldBC[16];
	int totalScore;	  ///< Overall score for this faction.
	PilotStats stats; ///< Accumulated combat statistics for this faction.
	/* Pilot_LoadXvtRecord and Pilot_WriteXvtRecord copy the base-game record's history blocks from and to
	 * 4 bytes before each array below: the first block from here, each later one from the last field of the
	 * array before it. So in that record each history entry starts with the 4 bytes this layout gives to
	 * the end of the entry before it, and mpBattles[24].field24 is not copied. No code reads these words
	 * by name. */
	uint8_t field1558[4];
	PilotMission spTrainingMissions
		[100]; ///< Single-player training history (100 records).
	PilotMission spMeleeMissions
		[250]; ///< Single-player melee history (250 records).
	PilotMission spCombatMissions
		[250]; ///< Single-player combat history (250 records).
	PilotMultiplayerMission mpTrainingMissions
		[100]; ///< Multiplayer training history (100 records).
	PilotMultiplayerMission mpMeleeMissions
		[250]; ///< Multiplayer melee history (250 records).
	PilotMultiplayerMission mpCombatMissions
		[250]; ///< Multiplayer combat history (250 records).
	PilotTournament spTournaments
		[25]; ///< Single-player tournament history (25 records).
	PilotMultiplayerTournament mpTournaments
		[25]; ///< Multiplayer tournament history (25 records).
	PilotBattle
		spBattles[25]; ///< Single-player battle history (25 records).
	PilotMultiplayerBattle
		mpBattles[25]; ///< Multiplayer battle history (25 records).
	PilotCampaign spCampaigns
		[25]; ///< Single-player campaign history (25 records).
	PilotCampaign
		mpCampaigns[25]; ///< Multiplayer campaign history (25 records).
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	uint8_t fieldF0E4
		[24]; ///< Unresolved bytes between multiplayer campaign history and the CD movie-check
	///< counter.
	/* Only entry 0's is used, by Concourse_Update in the original build
	 * when movie checks are not skipped and the game is neither host nor
	 * client: at 0 it asks for the game CD until the movie files are found
	 * and sets 1; otherwise it counts up and wraps to 0 at
	 * CONCOURSE_CD_MOVIE_CHECK_LIMIT (5). */
	uint32_t
		cdMovieCheckCounter; ///< Concourse CD movie-check retry counter.
	PilotCampaignMission spCampaignMissions
		[99]; ///< Single-player campaign mission history indexed by
		      ///< one-based mission ID 1-99.
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	uint8_t fieldFD60
		[32]; ///< Unresolved bytes preceding multiplayer campaign mission history.
	PilotCampaignMission mpCampaignMissions
		[99]; ///< Multiplayer campaign mission history indexed by
		      ///< one-based mission ID 1-99.
};

#pragma pack(pop)
typedef char xvt_size_PilotFaction[(sizeof(PilotFaction) == 68064) ? 1 : -1];

#pragma pack(push, 1)

struct PilotData {
	/* The pilot's name; empty when no pilot is loaded. Pilot_CreateNew sets
	 * it; loading a pilot file fills it. */
	char name[14];
	/* The pilot's score over every mission, raised by each mission's score
	 * in FeDiskIo_CommitFlightResults; the pilot statistics page shows
	 * it. */
	int totalScore;
	/* The local player's DirectPlay id, recorded by MissionDebrief_Update
	 * when a next or replayed mission starts; only Pilot_WriteXvtRecord
	 * reads it, into the base-game record. */
	int localPlayerId;
	/* Set to 1 by MissionDebrief_Update when a next or replayed mission
	 * starts, and by MissionSetup_TryContinueBattle and
	 * MissionSetup_TryContinueCampaign; only Pilot_WriteXvtRecord reads it,
	 * into the base-game record. */
	int launchSessionMarker;
	/* Net_IsHost(), or 1 in single player, recorded by
	 * MissionDebrief_Update when a next or replayed mission starts; only
	 * Pilot_WriteXvtRecord reads it, into the base-game record. */
	int isHost;
	/* Human players in the mission being launched:
	 * FlightLoading_UpdateReadyScreen sets the ready player count before
	 * each flight, and MissionDebrief_Update sets 1 in single player or
	 * Net_CountReadyPlayers() when a next or replayed mission starts. The
	 * flight code reads it for that flight's rules, such as the time limit,
	 * AI balance and dynamic music. */
	unsigned int numHumanPlayersLastMission;
	/* g_frontendMissionSessionMode, recorded by MissionDebrief_Update when
	 * a next or replayed mission starts; only Pilot_WriteXvtRecord reads
	 * it, into the base-game record. */
	int sessionMode;
	uint8_t xvtRecordPayload
		[672]; ///< Opaque 672-byte payload round-tripped by the XvT-compatible
		       ///< pilot-record reader and writer.
	/* Team the pilot flies for in the mission being set up. Many functions
	 * write it, chiefly MissionSetup_TeamAssignmentUpdate and the other
	 * mission setup screens; selecting a pilot or a faction restores it
	 * from factionStatistics. */
	int team;
	/* Mission directory being played (a MISSION_DIRECTORY_ value). While a
	 * tournament, battle or campaign is played it holds the directory its
	 * missions come from: melees, combat engagements or training exercises.
	 * Many functions write it, chiefly the MissionSetup screens and
	 * sequence functions. */
	MissionDirectoryId missionDirectoryId;
	int32_t missionDescriptionIds
		[6]; ///< Selected mission or sequence descriptor ID for each of the six
		     ///< MissionDirectoryId values.
	/* Name of the network game. The host screen edits it (an empty one
	 * becomes the pilot's name plus FRONTSTR_470_S_GAME);
	 * FrontendNet_JoinGameScreen copies the joined session's and
	 * FrontendNet_ProcessNetworkPackets the host's lobby state's.
	 * Pilot_CreateNew sets that default. */
	char multiplayerGameName[32];
	/* Game name the pilot last hosted under: FrontendNet_HostGameScreen
	 * copies it into multiplayerGameName when the host screen opens and
	 * saves the name back when it hosts. Starts as the default game
	 * name. */
	char multiplayerHostName[32];
	/* 1 while a tournament, battle or campaign is played, else 0. Many
	 * functions write it, chiefly the MissionSetup screens and sequence
	 * functions; selecting a pilot or a faction restores it from
	 * factionStatistics. */
	int missionSequenceActive;
	/* Despite the name, not the sequence's id: when a tournament, battle or
	 * campaign's first or next mission is selected, the entry of
	 * missionDescriptionIds for the directory its missions come from
	 * (melees, combat engagements or training exercises), saved before
	 * that mission's id replaces it. MissionSetup_Update, while a sequence
	 * is active, and MissionSetup_EnterNextMission copy it back into that
	 * entry. Saved and restored with factionStatistics like
	 * missionSequenceActive. */
	int missionSequenceDescriptionId;
	/* Promotion points earned at the current rating. Below the cap, Officer
	 * 1st Class for training missions and Jedi Master for SIMULATOR_2
	 * training, melee and combat, FeDiskIo_CommitFlightResults adds each
	 * flight's ratingPromoPoints and, while currentRatingWorsePromoPoints
	 * is under half the rating's threshold, its worseRatingPromoPoints; at
	 * the cap only negative points count. Reaching
	 * g_pilotRatingPromotionPointThresholds[rating] promotes the pilot
	 * (training then sets 0, melee and combat subtract the threshold).
	 * Below -2000 (PROMOTION_LOSS_LIMIT) a pilot above target drone is
	 * demoted and it is set to 0. */
	int currentRatingPromoPoints;
	/* Worse-rating promotion points counted at the current rating, also
	 * added to currentRatingPromoPoints; FeDiskIo_CommitFlightResults adds
	 * a flight's only while this is under half the rating's threshold, and
	 * sets 0 on a promotion or demotion. */
	int currentRatingWorsePromoPoints;
	PilotPromotionDelta
		promotionDelta; ///< Persisted signed rank change: -1 demotion, 0 unchanged, +1 promotion.
	/* Progress to the next rating after the last mission: 100 *
	 * currentRatingPromoPoints / the rating's threshold, at most 100, or
	 * for negative points 100 * points / 2000, at least -100; 0 after a
	 * demotion, and for a target drone with negative points. The pilot
	 * statistics page shows it below Jedi Master. */
	int nextPromotionPercent;
	PilotStats mainStats; ///< Pilot combat statistics.
	/* Progress and team standings of the tournament being played. Many
	 * functions write it, chiefly the MissionSetup sequence functions,
	 * FeDiskIo_CommitFlightResults and MissionDebrief_Update. */
	MeleeTournamentSequenceState meleeTournamentSequenceState;
	/* Progress and mission results of the battle being played. Many
	 * functions write it, chiefly the MissionSetup sequence functions,
	 * FeDiskIo_CommitFlightResults and MissionDebrief_Update. */
	BattleSequenceState battleSequenceState;
	/* The pilot's rating, PILOT_RATING_TARGET_DRONE (0) to
	 * PILOT_RATING_JEDI_MASTER (24); a new pilot starts as a trainee.
	 * FeDiskIo_CommitFlightResults raises and lowers it. */
	PilotRating rating;
	/* Missions the pilot has flown, counted up by
	 * FeDiskIo_CommitFlightResults; recorded in ratingAchievedOnMission. */
	int totalMissionsPlayedCount;
	/* Per rating, totalMissionsPlayedCount when
	 * FeDiskIo_CommitFlightResults promoted the pilot to it, or demoted the
	 * pilot to target drone or ground crew; the pilot rating page shows
	 * it. */
	int ratingAchievedOnMission[25];
	/* Name of the pilot's rating: the trainee string for a new pilot; after
	 * each flight FrontendFlight_LaunchSession (XvtLaunchTask_Complete in
	 * the modern build) sets it from rating before saving the pilot. Many
	 * screens show it. */
	char ratingName[32];
	/* The local player's score in the last mission, its mission score plus
	 * its team's bonus score, set by FeDiskIo_CommitFlightResults; 0 at
	 * launch. */
	int missionScore;
	/* Per network player slot, the local player's full kills of that player
	 * in the last mission, copied by FeDiskIo_CommitFlightResults; cleared
	 * at launch and when a mission is replayed or flown again. */
	int killsFullOnPlayer[8];
	/* Per network player slot, the local player's shared kills of that
	 * player in the last mission; cleared at launch and when a mission is
	 * replayed or flown again. */
	int killsSharedOnPlayer[8];
	/* Per flight group, the local player's full kills of it in the last
	 * mission, copied by FeDiskIo_CommitFlightResults; cleared at launch
	 * and when a mission is replayed or flown again. */
	int killsFullOnFlightGroup[48];
	/* Per flight group, the local player's shared kills of it in the last
	 * mission; cleared at launch and when a mission is replayed or flown
	 * again. */
	int killsSharedOnFlightGroup[48];
	/* Per network player slot, that player's full kills of the local player
	 * in the last mission; cleared at launch and when a mission is replayed
	 * or flown again. */
	int killsFullFromPlayer[8];
	/* Per network player slot, that player's shared kills of the local
	 * player in the last mission; cleared at launch and when a mission is
	 * replayed or flown again. */
	int killsSharedFromPlayer[8];
	/* Per flight group, its full kills of the local player in the last
	 * mission; cleared at launch and when a mission is replayed or flown
	 * again. */
	int killsFullFromFlightGroup[48];
	/* Per flight group, its shared kills of the local player in the last
	 * mission; cleared at launch and when a mission is replayed or flown
	 * again. */
	int killsSharedFromFlightGroup[48];
	/* Per flight group, the rating its AI pilots are shown with in a melee:
	 * g_flightGroupRatingBaseByAiLevel of its AI level plus, for a nonzero
	 * level, the flight group index & 3. FeDiskIo_CommitFlightResults sets
	 * it for a melee outside a sequence or on a tournament's first
	 * mission. */
	int flightGroupRating[48];
	PilotStats
		lastMissionStats; ///< Statistics of the most recent mission, shown on the debriefing.
	PilotNetworkPlayer
		networkPlayers[8]; ///< Persisted network-player results (8).
	PilotTeam teams[10];	   ///< Persisted team results (10).
	/* 0 Rebel, 1 Imperial, as the pilot record's faction buttons set it. At
	 * launch FrontendMission_InitPlayerState sets it: in a melee or
	 * tournament 0 when the local player's craft is type 1 to 4 or 14, else
	 * 1; otherwise the IFF of the local player's flight group. Entry 2 of
	 * factionStatistics keeps network play's selections. */
	int currentFactionId;	   ///< Selected faction-statistics record.
	PilotFaction factionStatistics
		[4]; ///< Per-faction pilot records (4 records, 0x109E0 bytes each).
	CampaignSequenceState
		campaignSequenceState; ///< Runtime state for an active campaign sequence.
	BattleContinuation spBattleContinuations
		[25]; ///< Saved single-player battle continuation slots indexed by battle ID.
	BattleContinuation mpBattleContinuations
		[25]; ///< Saved multiplayer battle continuation slots indexed by battle ID.
	CampaignContinuation spCampaignContinuations
		[25]; ///< Saved single-player campaign continuation slots
		      ///< indexed by campaign ID.
	CampaignContinuation mpCampaignContinuations
		[25]; ///< Saved multiplayer campaign continuation slots;
		      ///< client state uses the ID+12 partition.
};

#pragma pack(pop)
typedef char xvt_size_PilotData[(sizeof(PilotData) == 296238) ? 1 : -1];

extern PilotData g_pilotData;
extern unsigned int g_campaignAwardSpriteCount;
extern CampaignAwardSpriteEntry *g_campaignAwardSprites;
extern int g_pilotStatsAssists[3];
extern int g_pilotStatsPlayerKills[3];
extern int g_pilotStatsLossesToNonPlayers[3];
extern int g_pilotAchievementsScrollOffset;
extern int g_pilotSpTrainingHistoryCount;
extern int g_pilotSpMeleeHistoryCount;
extern int g_pilotSpCombatHistoryCount;
extern int g_pilotSpTournamentHistoryCount;
extern int g_pilotSpBattleHistoryCount;
extern int g_pilotSpCampaignHistoryRowCount;
extern int g_pilotMpTrainingHistoryCount;
extern int g_pilotMpMeleeHistoryCount;
extern int g_pilotMpCombatHistoryCount;
extern int g_pilotMpTournamentHistoryCount;
extern int g_pilotMpBattleHistoryCount;
extern int g_pilotStatsHasCraftKillsByType;
extern int g_pilotStatsRowHasData;
extern int g_pilotStatsHasLossesToPlayersByRank;
extern int g_pilotStatisticsScrollOffset;
extern int g_pilotStatsNonPlayerKillsShared[3];
extern int g_pilotStatsTotalKillsShared[3];
extern int g_pilotStatsNonPlayerKills[3];
extern int g_pilotStatsHasPlayerKillsByRating;
extern int g_pilotStatsPlayerKillsShared[3];
extern int g_pilotRecordPageRowCount;
extern int g_pilotStatsLossesToPlayers[3];
extern int g_pilotMpCampaignHistoryRowCount;

int PilotRecord_UpdatePilotSelectionPanel(int frameCounter);
int PilotRecord_DrawPilotList(const RECT *bounds, int firstVisibleIndex);
int PilotRecord_RebuildPilotList(int *selectedIndex);
int PilotRecord_DrawPilotStatisticsPage(void);
int PilotRecord_DrawMissionAchievementsPage(void);
int PilotRecord_DrawCutsceneViewerPage(void);
int PilotRecord_DrawCampaignMedalsPage(void);
int PilotRecord_DrawPilotAwardsPage(void);
int PilotRecord_DrawPilotRatingPage(void);
int PilotRecord_UpdateNavigationControls(void);
int PilotRecord_RedrawBackground(void);
int PilotRecord_LoadCampaignAwardSpriteTable(const char *fileName);

#ifdef __cplusplus
}
#endif

#endif
