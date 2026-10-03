#include "xvt/frontend/frontend_mission.h"

#include "xvt/assets/file.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net.h"
#include <stdlib.h>
#include <string.h>

/* The mission the frontend's setup, briefing and debriefing screens show, as
 * FrontendMission_LoadCurrent and FrontendMission_LoadCurrentWithBriefing
 * load it from its file. FrontendMission_InitForBriefing empties it to one
 * flight group with winType 1. */
// GLOBAL: XVT 0xA91CC0
FrontendMission g_frontendMission = {0};

/* Sets up the briefing map for the pilot's current mission:
 * FrontendMission_InitForBriefing, FrontendMission_LoadCurrentWithBriefing,
 * then BriefingScript_ResetState. Returns 1 on every path. */
// FUNCTION: XVT 0x4F6860
int FrontendMission_LoadForBriefing(void)
{
	FrontendMission_InitForBriefing();
	FrontendMission_LoadCurrentWithBriefing();
	BriefingScript_ResetState();
	return 1;
}

/* Sets the briefing up empty: g_briefingPlaybackActive 1; the selected
 * flight group, map center and target center, g_activeBriefingIndex,
 * g_briefingLastNarratedTextBlockIdx and g_briefingTextPageNumber 0; scales
 * 32. Empties g_frontendMission to one flight group with winType 1 and puts in
 * the default script. Allocates the 32 label buffers of 40 bytes, 32 text
 * blocks of 320 bytes and 20 unused buffers of 1024 bytes, without freeing
 * earlier ones or checking the allocations. Turns off both text slots and
 * all markers and labels, and sets g_briefingMapPanelRect to (0, 0) to (360,
 * 236). */
// FUNCTION: XVT 0x4F69B0
void FrontendMission_InitForBriefing(void)
{
	int16_t index;

	g_briefingPlaybackActive = 1;
	g_briefingSelectedMissionPoint14FlightGroupIdx = 0;
	g_briefingMapCenter.x = 0;
	g_briefingMapCenter.y = 0;
	g_briefingMapTargetCenter.x = 0;
	g_briefingMapTargetCenter.y = 0;
	g_activeBriefingIndex = 0;
	g_briefingMapScale.x = 32;
	g_briefingLastNarratedTextBlockIdx = 0;
	g_briefingMapScale.y = 32;
	g_briefingMapTargetScale.x = 32;
	g_briefingMapTargetScale.y = 32;
	g_briefingTextPageNumber = 0;
	memset(&g_frontendMission, 0, sizeof(g_frontendMission));
	g_frontendMission.flightGroupCount = 1;
	g_frontendMission.header.allWaypointsShown = 0;
	g_frontendMission.header.winType = 1;
	BriefingScript_InitDefaultScript();
	BriefingScript_ResetState();

	for (index = 0; index < 32; ++index) {
		g_briefingMapLabelTexts[index] = malloc(40);
	}
	for (index = 0; index < 32; ++index) {
		g_briefingTextBlocks[index] = malloc(320);
	}
	for (index = 0; index < 20; ++index) {
		g_briefingUnusedBuffers[index] = malloc(1024);
	}
	for (index = 0; index < 2; ++index) {
		g_briefingTextSlotActive[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefingMapFgMarkerActive[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefingMapLabelActive[index] = 0;
	}
	FrontendDraw_RectAssign(&g_briefingMapPanelRect, 0, 0, 360, 236);
}

/* Loads the pilot's current mission into g_frontendMission, with the
 * briefing for the pilot's team. Reloads the mission list of
 * g_pilotData.missionDirectoryId, sets g_selectedMissionListIndex to the entry
 * whose missionIdx is the pilot's mission in that directory, and opens that
 * file in the directory's folder. Returns without loading when the file does
 * not open, and with only formatVersion set when that is not 12, 13 or 14.
 * Reads the counts, header and flight groups, each message into the slot the
 * file gives before it, each team's goals and each team record the file
 * flags present. Each of the 8 briefings that follow is the script, 10 team
 * flags, then 32 labels and 32 text blocks, each with a 16-bit length. Every
 * briefing flagged for g_pilotData.team is kept, so a later one replaces an
 * earlier one: its script goes to g_briefingScript, its index to
 * g_activeBriefingIndex and its strings into g_briefingMapLabelTexts and
 * g_briefingTextBlocks; the strings of the others are skipped. Checks no
 * count, slot or string length against the space it fills, and does not
 * check that the list loaded or held the pilot's mission: it then reads past
 * the list's end, or through a NULL list. */
// FUNCTION: XVT 0x4F6B80
void FrontendMission_LoadCurrentWithBriefing(void)
{
	enum {
		MISSION_FILE_PATH_CAPACITY = 256,
		BRIEFING_COUNT = 8,
		TEAM_COUNT = 10,
		BRIEFING_LABEL_COUNT = 32,
		BRIEFING_LABEL_CAPACITY = 40,
		BRIEFING_TEXT_COUNT = 32,
		BRIEFING_TEXT_CAPACITY = 320,
	};

	uint8_t teamUsesBriefing;
	uint16_t indexedRecord;
	uint16_t textLength;
	int loadBriefingText;
	int briefingIndex;
	char fileName[MISSION_FILE_PATH_CAPACITY];
	FrontendBriefingScript briefingScript;
	XvtFile *stream;
	int flightGroupIndex;
	int messageIndex;
	unsigned int teamIndex;
	int globalGoalIndex;
	int labelIndex;
	int textIndex;
	char *briefingText;
	GlobalGoal *teamGoals;

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

	sprintf(fileName, "%s\\%s",
		g_missionDirectoryNames[g_pilotData.missionDirectoryId],
		g_missionList[g_selectedMissionListIndex].fileName);
	stream = File_Open(fileName, g_fileModeReadBinary);
	if (stream == NULL) {
		return;
	}

	memset(&g_frontendMission, 0, sizeof(g_frontendMission));
	File_ReadWord(stream, &g_frontendMission.formatVersion);
	if (g_frontendMission.formatVersion != 14 &&
	    g_frontendMission.formatVersion != 13 &&
	    g_frontendMission.formatVersion != 12) {
		File_Close(stream);
		return;
	}

	File_ReadWord(stream, &g_frontendMission.flightGroupCount);
	File_ReadWord(stream, &g_frontendMission.messageCount);
	File_ReadBytes(stream, &g_frontendMission.header,
		       sizeof(g_frontendMission.header));
	for (flightGroupIndex = 0;
	     flightGroupIndex < (int16_t)g_frontendMission.flightGroupCount;
	     ++flightGroupIndex) {
		File_ReadBytes(
			stream,
			&g_frontendMission.flightGroups[flightGroupIndex],
			sizeof(g_frontendMission
				       .flightGroups[flightGroupIndex]));
	}

	/* indexedRecord holds the 16-bit word read ahead of each record: the message's slot here, then each
	 * team's goal count, then whether a team record follows. */
	for (messageIndex = 0;
	     messageIndex < (int16_t)g_frontendMission.messageCount;
	     ++messageIndex) {
		File_ReadWord(stream, &indexedRecord);
		File_ReadBytes(
			stream,
			&g_frontendMission.messages[(int16_t)indexedRecord],
			sizeof(g_frontendMission.messages[0]));
	}

	for (teamIndex = 0; teamIndex < TEAM_COUNT; ++teamIndex) {
		teamGoals = g_frontendMission.globalGoals[teamIndex];
		File_ReadWord(stream, &indexedRecord);
		for (globalGoalIndex = 0;
		     globalGoalIndex < (int16_t)indexedRecord;
		     ++globalGoalIndex) {
			File_ReadBytes(stream, &teamGoals[globalGoalIndex],
				       sizeof(teamGoals[globalGoalIndex]));
		}
	}

	for (teamIndex = 0; teamIndex < TEAM_COUNT; ++teamIndex) {
		File_ReadWord(stream, &indexedRecord);
		if (indexedRecord != 0) {
			File_ReadBytes(
				stream, &g_frontendMission.teams[teamIndex],
				sizeof(g_frontendMission.teams[teamIndex]));
		}
	}

	for (briefingIndex = 0; briefingIndex < BRIEFING_COUNT;
	     ++briefingIndex) {
		loadBriefingText = 0;
		File_ReadBytes(stream, &briefingScript, sizeof(briefingScript));
		for (teamIndex = 0; teamIndex < TEAM_COUNT; ++teamIndex) {
			File_ReadByte(stream, &teamUsesBriefing);
			if (g_pilotData.team == (int)teamIndex &&
			    teamUsesBriefing != 0) {
				g_briefingScript = briefingScript;
				loadBriefingText = 1;
				g_activeBriefingIndex = briefingIndex;
			}
		}

		for (labelIndex = 0; labelIndex < BRIEFING_LABEL_COUNT;
		     ++labelIndex) {
			briefingText = g_briefingMapLabelTexts[labelIndex];
			if (loadBriefingText != 0) {
				memset(briefingText, 0,
				       BRIEFING_LABEL_CAPACITY);
			}
			File_ReadWord(stream, &textLength);
			if (textLength != 0) {
				if (loadBriefingText != 0) {
					File_ReadBytes(stream, briefingText,
						       (int16_t)textLength);
				} else {
					File_Seek(stream, (int16_t)textLength,
						  SEEK_CUR);
				}
			}
			if (loadBriefingText != 0) {
				briefingText[(int16_t)textLength] = '\0';
			}
		}

		for (textIndex = 0; textIndex < BRIEFING_TEXT_COUNT;
		     ++textIndex) {
			briefingText = g_briefingTextBlocks[textIndex];
			if (loadBriefingText != 0) {
				memset(briefingText, 0, BRIEFING_TEXT_CAPACITY);
			}
			File_ReadWord(stream, &textLength);
			if (textLength != 0) {
				if (loadBriefingText != 0) {
					File_ReadBytes(stream, briefingText,
						       (int16_t)textLength);
				} else {
					File_Seek(stream, (int16_t)textLength,
						  SEEK_CUR);
				}
			}
			if (loadBriefingText != 0) {
				briefingText[(int16_t)textLength] = '\0';
			}
		}
	}
	File_Close(stream);
}

/* Loads a mission file into *outMission the way
 * FrontendMission_LoadCurrentWithBriefing does, without the briefings. Leaves
 * *outMission untouched when the file does not open, and holding only
 * formatVersion when that is not 12, 13 or 14. Checks no count or slot
 * against the arrays. */
// FUNCTION: XVT 0x4F6F30
void FrontendMission_LoadFile(const char *fileName, FrontendMission *outMission)
{
	XvtFile *stream;
	int flightGroupIndex;
	XvtFlightGroup *flightGroup;
	int messageIndex;
	int globalGoalIndex;
	GlobalGoal *teamGoal;
	GlobalGoal *globalGoal;
	int teamIndex;
	uint16_t indexedRecord;

	stream = File_Open(fileName, "rb");
	if (stream != NULL) {
		memset(outMission, 0, sizeof(*outMission));
		File_ReadWord(stream, &outMission->formatVersion);
		if (outMission->formatVersion != 14 &&
		    outMission->formatVersion != 13 &&
		    outMission->formatVersion != 12) {
			File_Close(stream);
			return;
		}

		File_ReadWord(stream, &outMission->flightGroupCount);
		File_ReadWord(stream, &outMission->messageCount);
		flightGroupIndex = 0;
		File_ReadBytes(stream, &outMission->header,
			       sizeof(outMission->header));
		if ((int16_t)outMission->flightGroupCount > 0) {
			flightGroup = outMission->flightGroups;
			do {
				++flightGroupIndex;
				File_ReadBytes(stream, flightGroup,
					       sizeof(*flightGroup));
				++flightGroup;
			} while ((int16_t)outMission->flightGroupCount >
				 flightGroupIndex);
		}

		/* indexedRecord holds the 16-bit word read ahead of each record: the message's slot here, then
		 * each team's goal count, then whether a team record follows. */
		for (messageIndex = 0;
		     messageIndex < (int16_t)outMission->messageCount;
		     ++messageIndex) {
			File_ReadWord(stream, &indexedRecord);
			File_ReadBytes(
				stream,
				&outMission->messages[(int16_t)indexedRecord],
				sizeof(outMission->messages[0]));
		}

		teamIndex = 10;
		globalGoal = &outMission->globalGoals[0][0];
		do {
			globalGoalIndex = 0;
			File_ReadWord(stream, &indexedRecord);
			if ((int16_t)indexedRecord > 0) {
				teamGoal = globalGoal;
				do {
					++globalGoalIndex;
					File_ReadBytes(stream, teamGoal,
						       sizeof(*teamGoal));
					++teamGoal;
				} while ((int16_t)indexedRecord >
					 globalGoalIndex);
			}
			globalGoal += 7;
			--teamIndex;
		} while (teamIndex != 0);

		for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
			File_ReadWord(stream, &indexedRecord);
			if (indexedRecord != 0) {
				File_ReadBytes(
					stream, &outMission->teams[teamIndex],
					sizeof(outMission->teams[teamIndex]));
			}
		}

		File_Close(stream);
	}
}

/* FrontendMission_LoadCurrentWithBriefing without the briefings: reloads the
 * directory's mission list, sets g_selectedMissionListIndex and loads the
 * pilot's mission into g_frontendMission. The modern build returns without
 * loading when the list did not load or lacks the pilot's mission; the
 * original build then reads past the list's end, or through a NULL list. */
// FUNCTION: XVT 0x4F70F0
void FrontendMission_LoadCurrent(void)
{
	char fileName[256];
	XvtFile *stream;
	int flightGroupIndex;
	int messageIndex;
	int teamIndex;
	int globalGoalIndex;
	uint16_t indexedRecord;

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
#ifdef XVT_MODERN
	/* The caller selects an available entry when the saved selection is absent. */
	if (g_missionList == NULL ||
	    (unsigned int)g_selectedMissionListIndex >= g_missionCount) {
		return;
	}
#endif

	sprintf(fileName, "%s\\%s",
		g_missionDirectoryNames[g_pilotData.missionDirectoryId],
		g_missionList[g_selectedMissionListIndex].fileName);
	stream = File_Open(fileName, g_fileModeReadBinary);
	if (stream == NULL) {
		return;
	}

	memset(&g_frontendMission, 0, sizeof(g_frontendMission));
	File_ReadWord(stream, &g_frontendMission.formatVersion);
	if (g_frontendMission.formatVersion != 14 &&
	    g_frontendMission.formatVersion != 13 &&
	    g_frontendMission.formatVersion != 12) {
		File_Close(stream);
		return;
	}

	File_ReadWord(stream, &g_frontendMission.flightGroupCount);
	File_ReadWord(stream, &g_frontendMission.messageCount);
	File_ReadBytes(stream, &g_frontendMission.header,
		       sizeof(g_frontendMission.header));
	for (flightGroupIndex = 0;
	     flightGroupIndex < (int16_t)g_frontendMission.flightGroupCount;
	     ++flightGroupIndex) {
		File_ReadBytes(
			stream,
			&g_frontendMission.flightGroups[flightGroupIndex],
			sizeof(g_frontendMission
				       .flightGroups[flightGroupIndex]));
	}

	/* indexedRecord holds the 16-bit word read ahead of each record: the message's slot here, then each
	 * team's goal count, then whether a team record follows. */
	for (messageIndex = 0;
	     messageIndex < (int16_t)g_frontendMission.messageCount;
	     ++messageIndex) {
		File_ReadWord(stream, &indexedRecord);
		File_ReadBytes(
			stream,
			&g_frontendMission.messages[(int16_t)indexedRecord],
			sizeof(g_frontendMission.messages[0]));
	}

	for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
		File_ReadWord(stream, &indexedRecord);
		for (globalGoalIndex = 0;
		     globalGoalIndex < (int16_t)indexedRecord;
		     ++globalGoalIndex) {
			File_ReadBytes(
				stream,
				&g_frontendMission.globalGoals[teamIndex]
							      [globalGoalIndex],
				sizeof(g_frontendMission.globalGoals[0][0]));
		}
	}

	for (teamIndex = 0; teamIndex < 10; ++teamIndex) {
		File_ReadWord(stream, &indexedRecord);
		if (indexedRecord != 0) {
			File_ReadBytes(
				stream, &g_frontendMission.teams[teamIndex],
				sizeof(g_frontendMission.teams[teamIndex]));
		}
	}
	File_Close(stream);
}

/* Sets up the pilot record's per-mission player state before a mission
 * flies. Clears g_mpRoster entries 1 to 7 in single player, or in a network
 * game the entries of players DirectPlay no longer lists. Zeroes the pilot's
 * mission score, kill tables, lastMissionStats and team results, and
 * g_localPilotNetworkPlayerIndex. Copies every roster player into
 * g_pilotData.networkPlayers at the same index: name, craft, rating, and the
 * craft, warhead, beam and countermeasure choices (the last three less 1),
 * with zeroed results and the flight group of the player's slot in
 * g_missionSetupPlayerAssignments; a craft choice whose optional craft is
 * CRAFT_SPECIES_UNKNOWN becomes -1. The local player's index goes to
 * g_localPilotNetworkPlayerIndex. With a sequence active, the player count
 * goes to battleSequenceState for combat engagements and to
 * campaignSequenceState for training exercises. At the first mission of a
 * melee sequence it also marks which teams take part (0 in
 * aiOpponentSourceTeamAndTypeFlag, the rest -1) and stores the player and
 * team counts. g_pilotData.currentFactionId becomes, in melees and
 * tournaments, 0 when the local player's craft is an X-wing, Y-wing, A-wing,
 * B-wing or Z-95 and else 1, and elsewhere the IFF of the local player's
 * flight group; in melees and tournaments the other side's factionStatistics
 * entry gets missionSequenceActive 0. The mission choice then goes into
 * factionStatistics entry 2 in a network game, which also sets entries 0 and
 * 1's missionSequenceActive to 0, else into the current faction's entry. Ends
 * with Net_CompactReliablePeerSlotsForRoster. Does not check that the IFF is
 * under 4 before it picks a factionStatistics entry with it. */
// FUNCTION: XVT 0x4FAD00
void FrontendMission_InitPlayerState(void)
{
	int rosterIndex;
	int playerCount;

	if (g_frontendMissionSessionMode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		for (rosterIndex = 1; rosterIndex < 8; rosterIndex++) {
			memset(&g_mpRoster[rosterIndex], 0,
			       sizeof(g_mpRoster[rosterIndex]));
		}
	} else {
		for (rosterIndex = 0; rosterIndex < 8; rosterIndex++) {
			if (g_mpRoster[rosterIndex].playerId != 0 &&
			    Net_FindPlayer(g_mpRoster[rosterIndex].playerId) ==
				    NULL) {
				memset(&g_mpRoster[rosterIndex], 0,
				       sizeof(g_mpRoster[rosterIndex]));
			}
		}
	}

	g_pilotData.missionScore = 0;
	g_localPilotNetworkPlayerIndex = 0;
	memset(g_pilotData.killsFullOnPlayer, 0,
	       sizeof(g_pilotData.killsFullOnPlayer));
	memset(g_pilotData.killsSharedOnPlayer, 0,
	       sizeof(g_pilotData.killsSharedOnPlayer));
	memset(g_pilotData.killsFullOnFlightGroup, 0,
	       sizeof(g_pilotData.killsFullOnFlightGroup));
	memset(g_pilotData.killsSharedOnFlightGroup, 0,
	       sizeof(g_pilotData.killsSharedOnFlightGroup));
	memset(g_pilotData.killsFullFromPlayer, 0,
	       sizeof(g_pilotData.killsFullFromPlayer));
	memset(g_pilotData.killsSharedFromPlayer, 0,
	       sizeof(g_pilotData.killsSharedFromPlayer));
	memset(g_pilotData.killsFullFromFlightGroup, 0,
	       sizeof(g_pilotData.killsFullFromFlightGroup));
	memset(g_pilotData.killsSharedFromFlightGroup, 0,
	       sizeof(g_pilotData.killsSharedFromFlightGroup));
	memset(&g_pilotData.lastMissionStats, 0,
	       sizeof(g_pilotData.lastMissionStats));
	memset(g_pilotData.teams, 0, sizeof(g_pilotData.teams));
	playerCount = 0;

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES &&
	    g_pilotData.missionSequenceActive == 1 &&
	    g_pilotData.meleeTournamentSequenceState.currentMissionIndex == 0) {
		/* rosterIndex counts the ten teams here, not roster entries. */
		for (rosterIndex = 0; rosterIndex < 10; rosterIndex++) {
			g_pilotData.meleeTournamentSequenceState
				.teamStandings[rosterIndex]
				.aiOpponentSourceTeamAndTypeFlag = -1;
		}
	}

	for (rosterIndex = 0; rosterIndex < 8; rosterIndex++) {
		if (g_mpRoster[rosterIndex].playerId != 0) {
			int playerId;
			int teamIndex;
			int assignmentSlot;
			int flightGroupIndex;

			playerCount++;
			memcpy(g_pilotData.networkPlayers[rosterIndex]
				       .friendlyName,
			       g_mpRoster[rosterIndex].name, 13);
			g_pilotData.networkPlayers[rosterIndex]
				.friendlyName[12] = '\0';
			g_pilotData.networkPlayers[rosterIndex].craftId =
				g_mpRoster[rosterIndex].craftTypeOverride;
			g_pilotData.networkPlayers[rosterIndex].craftOption =
				g_mpRoster[rosterIndex].craftOptionIndex;
			g_pilotData.networkPlayers[rosterIndex].warheadOption =
				g_mpRoster[rosterIndex].warheadOptionIndex - 1;
			g_pilotData.networkPlayers[rosterIndex].beamOption =
				g_mpRoster[rosterIndex].beamOptionIndex - 1;
			g_pilotData.networkPlayers[rosterIndex]
				.countermeasureOption =
				g_mpRoster[rosterIndex]
					.countermeasureOptionIndex -
				1;
			playerId = g_mpRoster[rosterIndex].playerId;
			g_pilotData.networkPlayers[rosterIndex].directPlayId =
				playerId;
			g_pilotData.networkPlayers[rosterIndex].rating =
				g_mpRoster[rosterIndex].pilotRating;
			g_pilotData.networkPlayers[rosterIndex].totalScore = 0;
			g_pilotData.networkPlayers[rosterIndex].kills = 0;
			g_pilotData.networkPlayers[rosterIndex].killsShared = 0;
			g_pilotData.networkPlayers[rosterIndex].craftInspected =
				0;
			g_pilotData.networkPlayers[rosterIndex].killsAssist = 0;
			g_pilotData.networkPlayers[rosterIndex].totalLosses = 0;
			g_pilotData.networkPlayers[rosterIndex].hasLeft = 0;

			for (teamIndex = 0; teamIndex < 10; teamIndex++) {
				for (assignmentSlot = 0; assignmentSlot < 8;
				     assignmentSlot++) {
					if (g_missionSetupPlayerAssignments
						    .teamPlayerIds
							    [teamIndex]
							    [assignmentSlot] ==
					    playerId) {
						flightGroupIndex =
							g_missionSetupPlayerFlightGroupIndices
								[teamIndex * 8 +
								 assignmentSlot];
						g_pilotData
							.networkPlayers
								[rosterIndex]
							.flightGroupId =
							flightGroupIndex;
						if (g_pilotData.networkPlayers
								    [rosterIndex]
									    .craftId ==
							    0 &&
						    g_pilotData.networkPlayers
								    [rosterIndex]
									    .craftOption !=
							    -1 &&
						    g_frontendMission
								    .flightGroups
									    [flightGroupIndex]
								    .optionalCraft
									    [g_pilotData
										     .networkPlayers
											     [rosterIndex]
										     .craftOption] ==
							    CRAFT_SPECIES_UNKNOWN) {
							g_pilotData
								.networkPlayers
									[rosterIndex]
								.craftOption =
								-1;
						}
					}
				}
			}
			if (Net_GetLocalPlayerId() == playerId) {
				g_localPilotNetworkPlayerIndex = rosterIndex;
			}
		}
	}

	if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilotData.missionSequenceActive == 1) {
		g_pilotData.battleSequenceState.humanPlayerCount = playerCount;
	}
	if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES &&
	    g_pilotData.missionSequenceActive == 1) {
		g_pilotData.campaignSequenceState.humanPlayerCount =
			playerCount;
	}

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES &&
	    g_pilotData.missionSequenceActive == 1 &&
	    g_pilotData.meleeTournamentSequenceState.currentMissionIndex == 0) {
		int assignmentSlot;
		int teamIndex;

		for (teamIndex = 0; teamIndex < g_teamCount; teamIndex++) {
			for (assignmentSlot = 0;
			     assignmentSlot <
			     g_teamPlayerFlightGroupCount[teamIndex];
			     assignmentSlot++) {
				if (g_missionSetupPlayerAssignments
					    .teamPlayerIds[teamIndex]
							  [assignmentSlot] !=
				    0) {
					break;
				}
			}
			if (assignmentSlot <
			    g_teamPlayerFlightGroupCount[teamIndex]) {
				g_pilotData.meleeTournamentSequenceState
					.teamStandings[teamIndex]
					.aiOpponentSourceTeamAndTypeFlag = 0;
			}
		}
		if ((g_gameConfig.aiOpponents == 1 ||
		     g_frontendMissionSessionMode ==
			     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
		    (int16_t)g_frontendMission.flightGroupCount > 0) {
			int flightGroupIndex;
			for (flightGroupIndex = 0;
			     flightGroupIndex <
			     (int16_t)g_frontendMission.flightGroupCount;
			     flightGroupIndex++) {
				if (g_frontendMission
					    .flightGroups[flightGroupIndex]
					    .playerNumber != 0) {
					int team =
						g_frontendMission
							.flightGroups
								[flightGroupIndex]
							.team;
					g_pilotData.meleeTournamentSequenceState
						.teamStandings[team]
						.aiOpponentSourceTeamAndTypeFlag =
						0;
				}
			}
		}
		g_pilotData.meleeTournamentSequenceState.humanPlayerCount =
			playerCount;
		if (g_frontendMissionSessionMode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		    g_gameConfig.aiOpponents != 0) {
			g_pilotData.meleeTournamentSequenceState
				.participatingTeamCount = g_teamCount;
		} else {
			int participatingTeams = 0;
			for (teamIndex = 0; teamIndex < g_teamCount;
			     teamIndex++) {
				for (assignmentSlot = 0;
				     assignmentSlot <
				     g_teamPlayerFlightGroupCount[teamIndex];
				     assignmentSlot++) {
					if (g_missionSetupPlayerAssignments
						    .teamPlayerIds
							    [teamIndex]
							    [assignmentSlot] !=
					    0) {
						break;
					}
				}
				if (assignmentSlot <
				    g_teamPlayerFlightGroupCount[teamIndex]) {
					participatingTeams++;
					g_pilotData.meleeTournamentSequenceState
						.teamStandings[teamIndex]
						.aiOpponentSourceTeamAndTypeFlag =
						0;
				}
			}
			g_pilotData.meleeTournamentSequenceState
				.participatingTeamCount = participatingTeams;
		}
	}

	if (g_pilotData.missionDirectoryId == MISSION_DIRECTORY_MELEES ||
	    g_pilotData.missionDirectoryId == MISSION_DIRECTORY_TOURNAMENTS) {
		int flightGroupId =
			g_pilotData
				.networkPlayers[g_localPilotNetworkPlayerIndex]
				.flightGroupId;
		int craftId =
			g_pilotData
				.networkPlayers[g_localPilotNetworkPlayerIndex]
				.craftId;
		if (craftId == 0) {
			int craftOption =
				g_pilotData
					.networkPlayers
						[g_localPilotNetworkPlayerIndex]
					.craftOption;
			if (craftOption == -1 || craftOption >= 10) {
				craftId = g_frontendMission
						  .flightGroups[flightGroupId]
						  .craftType;
			} else {
				craftId = g_frontendMission
						  .flightGroups[flightGroupId]
						  .optionalCraft[craftOption];
			}
		}
		if (craftId >= 1 && (craftId <= 4 || craftId == 14)) {
			g_pilotData.currentFactionId = 0;
			g_pilotData.factionStatistics[1].missionSequenceActive =
				0;
		} else {
			g_pilotData.currentFactionId = 1;
			g_pilotData.factionStatistics[0].missionSequenceActive =
				0;
		}
	} else {
		int flightGroupId =
			g_pilotData
				.networkPlayers[g_localPilotNetworkPlayerIndex]
				.flightGroupId;
		g_pilotData.currentFactionId =
			g_frontendMission.flightGroups[flightGroupId].iff;
	}

	if (g_frontendMissionSessionMode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
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
		g_pilotData.factionStatistics[0].missionSequenceActive = 0;
		g_pilotData.factionStatistics[1].missionSequenceActive = 0;
	} else {
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.team = g_pilotData.team;
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.missionDirectoryId = g_pilotData.missionDirectoryId;
		if (g_pilotData.missionDirectoryId ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			g_pilotData
				.factionStatistics[g_pilotData.currentFactionId]
				.missionDescriptionIds[0] =
				g_pilotData.missionDescriptionIds[0];
		} else if (g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_MELEES ||
			   g_pilotData.missionDirectoryId ==
				   MISSION_DIRECTORY_TOURNAMENTS) {
			g_pilotData
				.factionStatistics[g_pilotData.currentFactionId]
				.missionDescriptionIds[1] =
				g_pilotData.missionDescriptionIds[1];
			g_pilotData
				.factionStatistics[g_pilotData.currentFactionId]
				.missionDescriptionIds[2] =
				g_pilotData.missionDescriptionIds[2];
		} else {
			g_pilotData
				.factionStatistics[g_pilotData.currentFactionId]
				.missionDescriptionIds[3] =
				g_pilotData.missionDescriptionIds[3];
			g_pilotData
				.factionStatistics[g_pilotData.currentFactionId]
				.missionDescriptionIds[4] =
				g_pilotData.missionDescriptionIds[4];
		}
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.missionSequenceActive =
			g_pilotData.missionSequenceActive;
		g_pilotData.factionStatistics[2].missionSequenceActive = 0;
		g_pilotData.factionStatistics[g_pilotData.currentFactionId]
			.savedMissionDescriptionId =
			g_pilotData.savedMissionDescriptionId;
	}
	Net_CompactReliablePeerSlotsForRoster();
}
