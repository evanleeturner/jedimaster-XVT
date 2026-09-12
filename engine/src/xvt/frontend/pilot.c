#include "xvt/frontend/pilot.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

#ifdef XVT_MODERN
#include "xvt_runtime/compat/pilot_port.h"
#include <strings.h>
#endif

#include <stdlib.h>
#include <string.h>

// GLOBAL: XVT 0x52AF78
const char* const g_randomPilotNames[40] = {
	"Luke",        "Han Solo",  "Darth Vader",  "Leia",       "Lando",      "Boba Fett",   "Chewbacca",
	"R2-D2",       "C-3PO",     "Jabba",        "Greedo",     "Thrawn",     "Ackbar",      "Wedge",
	"Bollux",      "Blue Max",  "Bossk",        "C'Baoth",    "Palpatine",  "Biggs",       "Dodonna",
	"Bib Fortuna", "Mara Jade", "Talon Karrde", "Obi-Wan",    "Lobot",      "Crix Madine", "Mon Mothma",
	"Nien Nunb",   "Pellaeon",  "Anakin Solo",  "Jacen Solo", "Jaina Solo", "Tarkin",      "Yoda",
	"Zaarin",      "Harkov",    "Tarrak",       "Xizor",      "Guri",
};

// FUNCTION: XVT 0x4BF260
int Pilot_DeleteCurrent(void) {
	uint8_t xvtPilotRecord[0x3DF3A];
	FrontendFileListNode* node;
	int selectedIndex;

	if (g_pilotListDisplayNames != NULL && g_pilotData.name[0] != '\0' && g_pilotFileList != NULL) {
		node = g_pilotFileList->head;
		selectedIndex = 0;
		if (g_pilotListDisplayNames != NULL) {
			while (g_pilotFileList->count > selectedIndex) {
#ifdef XVT_MODERN
				if (strcasecmp(g_pilotListDisplayNames[selectedIndex], g_pilotData.name) == 0) {
#else
				if (_strcmpi(g_pilotListDisplayNames[selectedIndex], g_pilotData.name) == 0) {
#endif
					File_ChangeToBaseGameInstallPath();
#ifdef XVT_MODERN
					if (!Pilot_RemoveFileModern(node->path))
						return 0;
#else
					File_Remove(node->path);
#endif
					File_ChangeToInstallPath();
					strcpy(g_frontendScratchBuffer, node->path);
					g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '2';
#ifdef XVT_MODERN
					if (!Pilot_RemoveFileModern(g_frontendScratchBuffer))
						return 0;
#else
					File_Remove(g_frontendScratchBuffer);
#endif
					break;
				}
				node = node->next;
				++selectedIndex;
				if (g_pilotListDisplayNames[selectedIndex] == NULL) {
					break;
				}
			}
		}
	}

	memset(&g_pilotData, 0, sizeof(g_pilotData));
	memset(xvtPilotRecord, 0, sizeof(xvtPilotRecord));
	selectedIndex = 0;
	PilotRecord_RebuildPilotList(&selectedIndex);
	return 1;
}

// FUNCTION: XVT 0x4BF3A0
int Pilot_CreateNew(const char* pilotName) {
	char pilotPath[32];
	XvtFile* probeStream;
#ifndef XVT_MODERN
	XvtFile* stream;
#endif
	int fileIndex;

	fileIndex = 0;
	for (;;) {
		sprintf(pilotPath, "%s%d.pl2", pilotName, fileIndex);
#ifdef XVT_MODERN
		probeStream = File_Open(pilotPath, g_fileModeReadBinary);
#else
		probeStream = File_RawOpen(pilotPath, g_fileModeReadBinary);
#endif
		if (probeStream == NULL)
			break;
#ifdef XVT_MODERN
		File_Close(probeStream);
#else
		File_RawClose(probeStream);
#endif
		++fileIndex;
	}

#ifndef XVT_MODERN
	stream = File_Open(pilotPath, "wb");
	if (stream == NULL)
		return 0;
#endif

	memset(&g_pilotData, 0, sizeof(g_pilotData));
	strcpy(g_pilotData.name, pilotName);
	g_pilotData.rating = PILOT_RATING_TRAINEE;
	strcpy(g_pilotData.ratingName, FrontendString_Get(FRONTSTR_124_TRAINEE));
	sprintf(g_pilotData.multiplayerGameName, "%s%s", pilotName, FrontendString_Get(FRONTSTR_470_S_GAME));
	strcpy(g_pilotData.multiplayerHostName, g_pilotData.multiplayerGameName);

	MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
	if (g_missionList != NULL) {
		g_pilotData.missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] = g_missionList[0].missionIdx;
		g_pilotData.factionStatistics[0].missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}

	g_pilotData.currentFactionId = 1;
	MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
	g_pilotData.currentFactionId = 0;
	if (g_missionList != NULL) {
		g_pilotData.factionStatistics[1].missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}

	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
	MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
	if (g_missionList != NULL) {
		g_pilotData.factionStatistics[2].missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
#ifdef XVT_MODERN
	if (!XvtStorage_WriteAtomic(pilotPath, &g_pilotData, sizeof(g_pilotData)))
		return 0;
#else
	File_WriteCount(stream, &g_pilotData, sizeof(g_pilotData));
#endif

	MissionSetup_LoadMissionList(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	if (g_missionList != NULL) {
		g_pilotData.missionDescriptionIds[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] = g_missionList[2].missionIdx;
		g_pilotData.factionStatistics[0].missionDescriptionIds[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}

	g_pilotData.currentFactionId = 1;
	MissionSetup_LoadMissionList(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	g_pilotData.currentFactionId = 0;
	if (g_missionList != NULL) {
		g_pilotData.factionStatistics[1].missionDescriptionIds[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}

#ifndef XVT_MODERN
	File_Close(stream);
#endif
#ifdef XVT_MODERN
	return Pilot_Save(0);
#else
	Pilot_Save(0);
	return 1;
#endif
}

// FUNCTION: XVT 0x4BF650
int Pilot_Save(int useTemporaryFile) {
	char pilotPath[30];
	FrontendFileList* fileList;
	FrontendFileListNode* node;
	XvtFile* stream;
	int fileIndex;

	if (g_pilotData.name[0] == '\0')
		return 0;

	memset(pilotPath, 0, sizeof(pilotPath));
	if (useTemporaryFile == 0) {
		fileList = FrontendFileList_BuildSorted("*.pl2");
		if (fileList != NULL) {
			node = fileList->head;
			fileIndex = 0;
			while (fileList->count > fileIndex) {
				stream = File_Open(node->path, g_fileModeReadBinary);
				if (stream != NULL) {
					File_ReadCount(stream, g_frontendScratchBuffer, sizeof(g_pilotData.name));
					File_Close(stream);
#ifdef XVT_MODERN
					if (strcasecmp(g_frontendScratchBuffer, g_pilotData.name) == 0) {
						snprintf(pilotPath, sizeof(pilotPath), "%s", node->path);
#else
					if (_strcmpi(g_frontendScratchBuffer, g_pilotData.name) == 0) {
						strcpy(pilotPath, node->path);
#endif
						break;
					}
				}
				node = node->next;
				++fileIndex;
			}
			FrontendFileList_Free(fileList);
		}
	} else {
		strcpy(pilotPath, "__temp__.tmp");
	}

	if (pilotPath[0] == '\0') {
		File_ChangeToBaseGameInstallPath();
		fileList = FrontendFileList_BuildSorted("*.plt");
		File_ChangeToInstallPath();
		if (fileList != NULL) {
			node = fileList->head;
			fileIndex = 0;
			while (fileList->count > fileIndex) {
				stream = File_Open(node->path, g_fileModeReadBinary);
				if (stream != NULL) {
					File_ReadCount(stream, g_frontendScratchBuffer, sizeof(g_pilotData.name));
					File_Close(stream);
#ifdef XVT_MODERN
					if (strcasecmp(g_frontendScratchBuffer, g_pilotData.name) == 0) {
						snprintf(pilotPath, sizeof(pilotPath), "%s", node->path);
#else
					if (_strcmpi(g_frontendScratchBuffer, g_pilotData.name) == 0) {
						strcpy(pilotPath, node->path);
#endif
						break;
					}
				}
				node = node->next;
				++fileIndex;
			}
			FrontendFileList_Free(fileList);
		}

		if (pilotPath[0] == '\0') {
#ifdef XVT_MODERN
			snprintf(pilotPath, sizeof(pilotPath), "%s0.pl2", g_pilotData.name);
#else
			sprintf(pilotPath, "%s0.pl2", g_pilotData.name);
#endif
		} else {
			pilotPath[strlen(pilotPath) - 1] = '2';
		}
	}

#ifndef XVT_MODERN
	stream = File_Open(pilotPath, "wb");
	if (stream == NULL)
		return 0;
#endif
#ifdef XVT_MODERN
	if (!XvtStorage_WriteAtomic(pilotPath, &g_pilotData, sizeof(g_pilotData)))
		return 0;
#else
	File_WriteCount(stream, &g_pilotData, sizeof(g_pilotData));
#endif
#ifndef XVT_MODERN
	File_Close(stream);
#endif
	if (pilotPath[0] == '\0') {
#ifdef XVT_MODERN
		snprintf(pilotPath, sizeof(pilotPath), "%s0.plt", g_pilotData.name);
#else
		sprintf(pilotPath, "%s0.plt", g_pilotData.name);
#endif
	} else {
		pilotPath[strlen(pilotPath) - 1] = 't';
	}
#ifdef XVT_MODERN
	return Pilot_WriteXvtRecord(pilotPath, NULL);
#else
	Pilot_WriteXvtRecord(pilotPath, stream);
#endif
	return 1;
}

// FUNCTION: XVT 0x4BF8C0
int Pilot_FindAndLoadByName(const char* pilotName) {
	FrontendFileList* fileList;
	FrontendFileListNode* node;
	XvtFile* stream;
	int fileIndex;
	int wasLoaded;

	File_ChangeToBaseGameInstallPath();
	fileList = FrontendFileList_BuildSorted("*.plt");
	File_ChangeToInstallPath();
	if (fileList == NULL) {
		return 0;
	}
	if (pilotName == NULL) {
		FrontendFileList_Free(fileList);
		return 0;
	}
	if (pilotName[0] == '\0') {
		FrontendFileList_Free(fileList);
		return 0;
	}

	node = fileList->head;
	fileIndex = 0;
	wasLoaded = 0;
	while (fileIndex < fileList->count) {
		stream = File_Open(node->path, g_fileModeReadBinary);
		if (stream != NULL) {
			File_ReadCount(stream, g_frontendScratchBuffer, sizeof(g_pilotData.name));
			File_Close(stream);
#ifdef XVT_MODERN
			if (strcasecmp(g_frontendScratchBuffer, pilotName) == 0) {
#else
			if (_strcmpi(g_frontendScratchBuffer, pilotName) == 0) {
#endif
				if (Pilot_LoadFromPath(node->path) != 0) {
					wasLoaded = 1;
				}
				break;
			}
		}
		node = node->next;
		++fileIndex;
	}

	FrontendFileList_Free(fileList);
	return wasLoaded;
}

// FUNCTION: XVT 0x4C9C50
int Pilot_ParseCommandLine(const char* cmdLine) {
	int commandIndex;
	int commandLength;
	int hasNetworkAddress;

	struct ParsedCommandLine {
		int hasPilotName;
		char pilotName[16];
		char parameter[256];
	} parsed;

	enum {
		RANDOM_PILOT_NAME_COUNT = 40,
	};

	commandIndex = 0;
	commandLength = strlen(cmdLine);
	hasNetworkAddress = 0;
	parsed.hasPilotName = 0;
	if (commandLength > 0) {
		do {
			if (cmdLine[commandIndex] == '"') {
				int parameterIndex;

				++commandIndex;
				parameterIndex = 0;
				while (cmdLine[commandIndex] != '"') {
					if (commandIndex >= commandLength) {
						break;
					}
					if (parameterIndex >= (int)sizeof(parsed.parameter) - 1) {
						break;
					}
					parsed.parameter[parameterIndex] = cmdLine[commandIndex];
					++parameterIndex;
					++commandIndex;
				}

				parsed.parameter[parameterIndex] = '\0';
				if (parsed.parameter[0] == 'a' && parsed.parameter[1] == '=') {
					strncpy(g_gameConfig.ipAddress, &parsed.parameter[2], sizeof(g_gameConfig.ipAddress) - 1);
					g_gameConfig.ipAddress[sizeof(g_gameConfig.ipAddress) - 1] = '\0';
					hasNetworkAddress = 1;
				} else {
					/* The original parser accepts every non-address option using the x= form. */
					parsed.parameter[0] = parsed.parameter[1] == '=';
					if (parsed.parameter[0] != 0) {
						strncpy(parsed.pilotName, &parsed.parameter[2],
								sizeof(g_gameConfig.lastPilotName) - 1);
						parsed.pilotName[sizeof(g_gameConfig.lastPilotName) - 1] = '\0';
						parsed.hasPilotName = 1;
					}
				}
			}

			++commandIndex;
		} while (commandIndex < commandLength);
	}

	if (parsed.hasPilotName != 0) {
		if (g_gameConfig.lastPilotName[0] == '\0') {
			if (strcmp(parsed.pilotName, "joiner") == 0 || strcmp(parsed.pilotName, "host") == 0) {
				srand(GetTickCount());
				strcpy(g_gameConfig.lastPilotName, g_randomPilotNames[rand() % RANDOM_PILOT_NAME_COUNT]);
			} else {
				strcpy(g_gameConfig.lastPilotName, parsed.pilotName);
			}
			Pilot_CreateNew(g_gameConfig.lastPilotName);
		} else if (Pilot_FindAndLoadByName(g_gameConfig.lastPilotName) == 0) {
			if (strcmp(parsed.pilotName, "joiner") == 0 || strcmp(parsed.pilotName, "host") == 0) {
				srand(GetTickCount());
				strcpy(g_gameConfig.lastPilotName, g_randomPilotNames[rand() % RANDOM_PILOT_NAME_COUNT]);
			} else {
				strcpy(g_gameConfig.lastPilotName, parsed.pilotName);
			}
			Pilot_CreateNew(g_gameConfig.lastPilotName);
		}
	}

	if (parsed.hasPilotName != 0 && hasNetworkAddress != 0) {
		g_gameConfig.networkType = NET_TRANSPORT_TCPIP;
		g_gameConfig.asyncFlag = 1;
		return 1;
	}

	g_optIsHost = 0;
	g_optIsClient = 0;
	return 1;
}

// FUNCTION: XVT 0x4CB0C0
int Pilot_LoadFromPath(const char* basePilotPath) {
	char expansionPilotPath[128];
	XvtFile* expansionStream;
	XvtFile* xvtStream;
	int hasExpansionRecord;

	hasExpansionRecord = 0;
	memset(&g_pilotData, 0, sizeof(g_pilotData));
	strcpy(expansionPilotPath, basePilotPath);
	expansionPilotPath[strlen(expansionPilotPath) - 1] = '2';
	expansionStream = File_Open(expansionPilotPath, g_fileModeReadBinary);
	if (expansionStream != NULL) {
		hasExpansionRecord = 1;
#ifdef XVT_MODERN
		if (!File_ReadCount(expansionStream, &g_pilotData, sizeof(g_pilotData))) {
			File_Close(expansionStream);
			memset(&g_pilotData, 0, sizeof(g_pilotData));
			return 0;
		}
#else
		File_ReadCount(expansionStream, &g_pilotData, sizeof(g_pilotData));
#endif
		File_Close(expansionStream);
	}

	File_ChangeToBaseGameInstallPath();
	xvtStream = File_Open(basePilotPath, g_fileModeReadBinary);
	File_ChangeToInstallPath();
	if (xvtStream != NULL) {
		if (hasExpansionRecord == 0) {
			g_pilotData.rating = PILOT_RATING_TRAINEE;
			strcpy(g_pilotData.ratingName, FrontendString_Get(FRONTSTR_124_TRAINEE));
			MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
			if (g_missionList != NULL) {
				g_pilotData.missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				g_pilotData.factionStatistics[0].missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				free(g_missionList);
				g_missionList = NULL;
			}

			g_pilotData.currentFactionId = 1;
			MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
			g_pilotData.currentFactionId = 0;
			if (g_missionList != NULL) {
				g_pilotData.factionStatistics[1].missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				free(g_missionList);
				g_missionList = NULL;
			}

			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
			MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
			if (g_missionList != NULL) {
				g_pilotData.factionStatistics[2].missionDescriptionIds[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				free(g_missionList);
				g_missionList = NULL;
			}
			g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
		}

#ifdef XVT_MODERN
		if (!Pilot_LoadXvtRecord(xvtStream)) {
			File_Close(xvtStream);
			return 0;
		}
#else
		Pilot_LoadXvtRecord(xvtStream);
#endif
		File_Close(xvtStream);
		if (hasExpansionRecord == 0) {
			sprintf(g_pilotData.multiplayerGameName, "%s%s", g_pilotData.name,
					FrontendString_Get(FRONTSTR_470_S_GAME));
			strcpy(g_pilotData.multiplayerHostName, g_pilotData.multiplayerGameName);
		}
	} else if (hasExpansionRecord == 0) {
		return 0;
	}

	return 1;
}

#pragma pack(push, 1)

typedef struct PilotXvtStats {
	int totalScorePerMT[3];
	int standaloneMissionsPlayedPerMT[3];
	int sequenceMissionsPlayedPerMT[3];
	int totalKillsPerMT[3];
	int totalFriendliesKilledPerMT[3];
	int killsPerCraftPerMT[3][88];
	int killsSharedPerCraftPerMT[3][88];
	int killsAssistsPerCraftPerMT[3][88];
	int killsFullOnPlayerRatingPerMT[3][25];
	int killsSharedOnPlayerRatingPerMT[3][25];
	int killsAssistOnPlayerRatingPerMT[3][25];
	int killsFullOnAIRatingPerMT[3][6];
	int killsSharedOnAIRatingPerMT[3][6];
	int killsAssistOnAIRatingPerMT[3][6];
	int numSpecialInspectedPerMT[3];
	int energyHitsPerMT[3];
	int energyFiredPerMT[3];
	int warheadsHitsPerMT[3];
	int warheadsFiredPerMT[3];
	int totalCraftLossesPerMT[3];
	int lossesByCollisionsPerMT[3];
	int lossesByStarshipsPerMT[3];
	int lossesByMinesPerMT[3];
	int killedByPlayerRatingPerMT[3][25];
	int killedByAIRatingPerMT[3][6];
} PilotXvtStats;

typedef struct PilotXvtFaction {
	int totalMissionsPlayedCount;
	uint8_t selectionState[68];
	int meleePlaques[6];
	int tournamentTrophies[6];
	int missionEvaluations[6];
	int battleMedallions[6];
	int missionAwards[4];
	uint8_t fieldBC[16];
	int totalScore;
	PilotXvtStats stats;
	uint8_t spTrainingData[3600];
	uint8_t spMeleeData[9000];
	uint8_t spCombatData[9000];
	uint8_t mpTrainingData[4800];
	uint8_t mpMeleeData[12000];
	uint8_t mpCombatData[12000];
	uint8_t spTournamentData[1000];
	uint8_t mpTournamentData[1100];
	uint8_t spBattleData[900];
	uint8_t mpBattleData[1000];
} PilotXvtFaction;

typedef struct PilotXvtRecord {
	char name[14];
	int totalScore;
	int localPlayerId;
	int launchSessionMarker;
	int isHost;
	unsigned int numHumanPlayersLastMission;
	int gameMode;
	uint8_t xvtRecordCombatPayload[320];
	uint8_t xvtRecordIdentityPayload[32];
	uint8_t xvtRecordObjectPayload[320];
	uint8_t legacyRatingState[100];
	int currentRatingPromoPoints;
	int currentRatingWorsePromoPoints;
	PilotPromotionDelta promotionDelta;
	int nextPromotionPercent;
	PilotXvtStats mainStats;
	uint8_t missionSequenceState[3348];
	PilotRating rating;
	int totalMissionsPlayedCount;
	int totalMissionsPlayedCountPerRating[25];
	char ratingName[32];
	int missionScore;
	int killsFullOnPlayer[8];
	int killsSharedOnPlayer[8];
	int killsFullOnFlightGroup[48];
	int killsSharedOnFlightGroup[48];
	int killsFullFromPlayer[8];
	int killsSharedFromPlayer[8];
	int killsFullFromFlightGroup[48];
	int killsSharedFromFlightGroup[48];
	int flightGroupRating[48];
	PilotXvtStats objectStats;
	PilotNetworkPlayer networkPlayers[8];
	PilotTeam teams[10];
	int currentFactionId;
	PilotXvtFaction factionStatistics[4];
} PilotXvtRecord;

#pragma pack(pop)

typedef char xvt_size_PilotXvtStats[(sizeof(PilotXvtStats) == 4824) ? 1 : -1];
typedef char xvt_size_PilotXvtFaction[(sizeof(PilotXvtFaction) == 59428) ? 1 : -1];
typedef char xvt_size_PilotXvtRecord[(sizeof(PilotXvtRecord) == 0x3DF3A) ? 1 : -1];

// FUNCTION: XVT 0x4C9F80
int Pilot_LoadXvtRecord(XvtFile* stream) {
	PilotXvtFaction* sourceFaction;
	PilotFaction* destinationFaction;
	int mainMissionType;
	int missionType;
	int factionId;

	struct PreservedCraftStats {
		int bWing;
		int superStarDestroyer;
		int modifiedFrigate;
		int carrackCruiser;
		int modifiedCorvette;
		int dreadnaught;
		int gunEmplacement;
	} preservedCraftStats;

	PilotXvtRecord record;

	enum {
		MISSION_TYPE_COUNT = 3,
		FACTION_COUNT = 4,
		B_WING_CRAFT_INDEX = 4,
		DREADNAUGHT_CRAFT_INDEX = 36,
		MODIFIED_CORVETTE_CRAFT_INDEX = 41,
		MODIFIED_FRIGATE_CRAFT_INDEX = 43,
		CARRACK_CRUISER_CRAFT_INDEX = 45,
		SUPER_STAR_DESTROYER_CRAFT_INDEX = 54,
		GUN_EMPLACEMENT_CRAFT_INDEX = 78
	};

#ifdef XVT_MODERN
	if (!File_ReadCount(stream, &record, sizeof(record)))
		return 0;
#else
	File_ReadCount(stream, &record, sizeof(record));
#endif
	memcpy(g_pilotData.name, record.name, sizeof(record.name));
	g_pilotData.totalScore = record.totalScore;
	g_pilotData.localPlayerId = record.localPlayerId;
	g_pilotData.launchSessionMarker = record.launchSessionMarker;
	g_pilotData.isHost = record.isHost;
	g_pilotData.numHumanPlayersLastMission = record.numHumanPlayersLastMission;
	g_pilotData.gameMode = record.gameMode;
	memcpy(g_pilotData.xvtRecordPayload, record.xvtRecordCombatPayload,
		   sizeof(record.xvtRecordCombatPayload));
	memcpy(&g_pilotData.xvtRecordPayload[sizeof(record.xvtRecordCombatPayload)],
		   record.xvtRecordIdentityPayload, sizeof(record.xvtRecordIdentityPayload));
	memcpy(&g_pilotData.xvtRecordPayload[sizeof(record.xvtRecordCombatPayload) +
										 sizeof(record.xvtRecordIdentityPayload)],
		   record.xvtRecordObjectPayload, sizeof(record.xvtRecordObjectPayload));
	g_pilotData.currentRatingPromoPoints = record.currentRatingPromoPoints;
	g_pilotData.currentRatingWorsePromoPoints = record.currentRatingWorsePromoPoints;
	g_pilotData.promotionDelta = record.promotionDelta;
	g_pilotData.nextPromotionPercent = record.nextPromotionPercent;

	memcpy(g_pilotData.mainStats.totalScorePerMT, record.mainStats.totalScorePerMT,
		   sizeof(record.mainStats.totalScorePerMT));
	memcpy(g_pilotData.mainStats.standaloneMissionsPlayedPerMT,
		   record.mainStats.standaloneMissionsPlayedPerMT,
		   sizeof(record.mainStats.standaloneMissionsPlayedPerMT));
	memcpy(g_pilotData.mainStats.sequenceMissionsPlayedPerMT, record.mainStats.sequenceMissionsPlayedPerMT,
		   sizeof(record.mainStats.sequenceMissionsPlayedPerMT));
	memcpy(g_pilotData.mainStats.totalKillsPerMT, record.mainStats.totalKillsPerMT,
		   sizeof(record.mainStats.totalKillsPerMT));
	memcpy(g_pilotData.mainStats.totalFriendliesKilledPerMT, record.mainStats.totalFriendliesKilledPerMT,
		   sizeof(record.mainStats.totalFriendliesKilledPerMT));
	for (mainMissionType = 0; mainMissionType < MISSION_TYPE_COUNT; ++mainMissionType) {
		preservedCraftStats.bWing =
			g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType],
			   record.mainStats.killsPerCraftPerMT[mainMissionType],
			   sizeof(record.mainStats.killsPerCraftPerMT[mainMissionType]));
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType],
			   record.mainStats.killsSharedPerCraftPerMT[mainMissionType],
			   sizeof(record.mainStats.killsSharedPerCraftPerMT[mainMissionType]));
		g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.mainStats.killsSharedPerCraftPerMT[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.mainStats
				.killsAssistsPerCraftPerMT[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType],
			   record.mainStats.killsAssistsPerCraftPerMT[mainMissionType],
			   sizeof(record.mainStats.killsAssistsPerCraftPerMT[mainMissionType]));
		g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;
	}

	memcpy(g_pilotData.mainStats.killsFullOnPlayerRatingPerMT, record.mainStats.killsFullOnPlayerRatingPerMT,
		   sizeof(record.mainStats.killsFullOnPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killsSharedOnPlayerRatingPerMT,
		   record.mainStats.killsSharedOnPlayerRatingPerMT,
		   sizeof(record.mainStats.killsSharedOnPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killsAssistOnPlayerRatingPerMT,
		   record.mainStats.killsAssistOnPlayerRatingPerMT,
		   sizeof(record.mainStats.killsAssistOnPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killsFullOnAIRatingPerMT, record.mainStats.killsFullOnAIRatingPerMT,
		   sizeof(record.mainStats.killsFullOnAIRatingPerMT));
	memcpy(g_pilotData.mainStats.killsSharedOnAIRatingPerMT, record.mainStats.killsSharedOnAIRatingPerMT,
		   sizeof(record.mainStats.killsSharedOnAIRatingPerMT));
	memcpy(g_pilotData.mainStats.killsAssistOnAIRatingPerMT, record.mainStats.killsAssistOnAIRatingPerMT,
		   sizeof(record.mainStats.killsAssistOnAIRatingPerMT));
	memcpy(g_pilotData.mainStats.numSpecialInspectedPerMT, record.mainStats.numSpecialInspectedPerMT,
		   sizeof(record.mainStats.numSpecialInspectedPerMT));
	memcpy(g_pilotData.mainStats.energyHitsPerMT, record.mainStats.energyHitsPerMT,
		   sizeof(record.mainStats.energyHitsPerMT));
	memcpy(g_pilotData.mainStats.energyFiredPerMT, record.mainStats.energyFiredPerMT,
		   sizeof(record.mainStats.energyFiredPerMT));
	memcpy(g_pilotData.mainStats.warheadsHitsPerMT, record.mainStats.warheadsHitsPerMT,
		   sizeof(record.mainStats.warheadsHitsPerMT));
	memcpy(g_pilotData.mainStats.warheadsFiredPerMT, record.mainStats.warheadsFiredPerMT,
		   sizeof(record.mainStats.warheadsFiredPerMT));
	memcpy(g_pilotData.mainStats.totalCraftLossesPerMT, record.mainStats.totalCraftLossesPerMT,
		   sizeof(record.mainStats.totalCraftLossesPerMT));
	memcpy(g_pilotData.mainStats.lossesByCollisionsPerMT, record.mainStats.lossesByCollisionsPerMT,
		   sizeof(record.mainStats.lossesByCollisionsPerMT));
	memcpy(g_pilotData.mainStats.lossesByStarshipsPerMT, record.mainStats.lossesByStarshipsPerMT,
		   sizeof(record.mainStats.lossesByStarshipsPerMT));
	memcpy(g_pilotData.mainStats.lossesByMinesPerMT, record.mainStats.lossesByMinesPerMT,
		   sizeof(record.mainStats.lossesByMinesPerMT));
	memcpy(g_pilotData.mainStats.killedByPlayerRatingPerMT, record.mainStats.killedByPlayerRatingPerMT,
		   sizeof(record.mainStats.killedByPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killedByAIRatingPerMT, record.mainStats.killedByAIRatingPerMT,
		   sizeof(record.mainStats.killedByAIRatingPerMT));

	g_pilotData.rating = record.rating;
	g_pilotData.totalMissionsPlayedCount = record.totalMissionsPlayedCount;
	memcpy(g_pilotData.totalMissionsPlayedCountPerRating, record.totalMissionsPlayedCountPerRating,
		   sizeof(record.totalMissionsPlayedCountPerRating));
	memcpy(g_pilotData.ratingName, record.ratingName, sizeof(record.ratingName));
	g_pilotData.missionScore = record.missionScore;
	memcpy(g_pilotData.killsFullOnPlayer, record.killsFullOnPlayer, sizeof(record.killsFullOnPlayer));
	memcpy(g_pilotData.killsSharedOnPlayer, record.killsSharedOnPlayer, sizeof(record.killsSharedOnPlayer));
	memcpy(g_pilotData.killsFullOnFlightGroup, record.killsFullOnFlightGroup,
		   sizeof(record.killsFullOnFlightGroup));
	memcpy(g_pilotData.killsSharedOnFlightGroup, record.killsSharedOnFlightGroup,
		   sizeof(record.killsSharedOnFlightGroup));
	memcpy(g_pilotData.killsFullFromPlayer, record.killsFullFromPlayer, sizeof(record.killsFullFromPlayer));
	memcpy(g_pilotData.killsSharedFromPlayer, record.killsSharedFromPlayer,
		   sizeof(record.killsSharedFromPlayer));
	memcpy(g_pilotData.killsFullFromFlightGroup, record.killsFullFromFlightGroup,
		   sizeof(record.killsFullFromFlightGroup));
	memcpy(g_pilotData.killsSharedFromFlightGroup, record.killsSharedFromFlightGroup,
		   sizeof(record.killsSharedFromFlightGroup));
	memcpy(g_pilotData.flightGroupRating, record.flightGroupRating, sizeof(record.flightGroupRating));

	memcpy(g_pilotData.objectStats.totalScorePerMT, record.objectStats.totalScorePerMT,
		   sizeof(record.objectStats.totalScorePerMT));
	memcpy(g_pilotData.objectStats.standaloneMissionsPlayedPerMT,
		   record.objectStats.standaloneMissionsPlayedPerMT,
		   sizeof(record.objectStats.standaloneMissionsPlayedPerMT));
	memcpy(g_pilotData.objectStats.sequenceMissionsPlayedPerMT,
		   record.objectStats.sequenceMissionsPlayedPerMT,
		   sizeof(record.objectStats.sequenceMissionsPlayedPerMT));
	memcpy(g_pilotData.objectStats.totalKillsPerMT, record.objectStats.totalKillsPerMT,
		   sizeof(record.objectStats.totalKillsPerMT));
	memcpy(g_pilotData.objectStats.totalFriendliesKilledPerMT, record.objectStats.totalFriendliesKilledPerMT,
		   sizeof(record.objectStats.totalFriendliesKilledPerMT));
	for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
		preservedCraftStats.bWing =
			g_pilotData.objectStats.killsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.objectStats.killsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.objectStats.killsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.objectStats.killsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.objectStats.killsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.objectStats.killsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.objectStats.killsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.objectStats.killsPerCraftPerMT[missionType],
			   record.objectStats.killsPerCraftPerMT[missionType],
			   sizeof(record.objectStats.killsPerCraftPerMT[missionType]));
		g_pilotData.objectStats.killsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.objectStats.killsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.objectStats.killsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.objectStats.killsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.objectStats.killsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.objectStats.killsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.objectStats.killsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType],
			   record.objectStats.killsSharedPerCraftPerMT[missionType],
			   sizeof(record.objectStats.killsSharedPerCraftPerMT[missionType]));
		g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType],
			   record.objectStats.killsAssistsPerCraftPerMT[missionType],
			   sizeof(record.objectStats.killsAssistsPerCraftPerMT[missionType]));
		g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;
	}

	memcpy(g_pilotData.objectStats.killsFullOnPlayerRatingPerMT,
		   record.objectStats.killsFullOnPlayerRatingPerMT,
		   sizeof(record.objectStats.killsFullOnPlayerRatingPerMT));
	memcpy(g_pilotData.objectStats.killsSharedOnPlayerRatingPerMT,
		   record.objectStats.killsSharedOnPlayerRatingPerMT,
		   sizeof(record.objectStats.killsSharedOnPlayerRatingPerMT));
	memcpy(g_pilotData.objectStats.killsAssistOnPlayerRatingPerMT,
		   record.objectStats.killsAssistOnPlayerRatingPerMT,
		   sizeof(record.objectStats.killsAssistOnPlayerRatingPerMT));
	memcpy(g_pilotData.objectStats.killsFullOnAIRatingPerMT, record.objectStats.killsFullOnAIRatingPerMT,
		   sizeof(record.objectStats.killsFullOnAIRatingPerMT));
	memcpy(g_pilotData.objectStats.killsSharedOnAIRatingPerMT, record.objectStats.killsSharedOnAIRatingPerMT,
		   sizeof(record.objectStats.killsSharedOnAIRatingPerMT));
	memcpy(g_pilotData.objectStats.killsAssistOnAIRatingPerMT, record.objectStats.killsAssistOnAIRatingPerMT,
		   sizeof(record.objectStats.killsAssistOnAIRatingPerMT));
	memcpy(g_pilotData.objectStats.numSpecialInspectedPerMT, record.objectStats.numSpecialInspectedPerMT,
		   sizeof(record.objectStats.numSpecialInspectedPerMT));
	memcpy(g_pilotData.objectStats.energyHitsPerMT, record.objectStats.energyHitsPerMT,
		   sizeof(record.objectStats.energyHitsPerMT));
	memcpy(g_pilotData.objectStats.energyFiredPerMT, record.objectStats.energyFiredPerMT,
		   sizeof(record.objectStats.energyFiredPerMT));
	memcpy(g_pilotData.objectStats.warheadsHitsPerMT, record.objectStats.warheadsHitsPerMT,
		   sizeof(record.objectStats.warheadsHitsPerMT));
	memcpy(g_pilotData.objectStats.warheadsFiredPerMT, record.objectStats.warheadsFiredPerMT,
		   sizeof(record.objectStats.warheadsFiredPerMT));
	memcpy(g_pilotData.objectStats.totalCraftLossesPerMT, record.objectStats.totalCraftLossesPerMT,
		   sizeof(record.objectStats.totalCraftLossesPerMT));
	memcpy(g_pilotData.objectStats.lossesByCollisionsPerMT, record.objectStats.lossesByCollisionsPerMT,
		   sizeof(record.objectStats.lossesByCollisionsPerMT));
	memcpy(g_pilotData.objectStats.lossesByStarshipsPerMT, record.objectStats.lossesByStarshipsPerMT,
		   sizeof(record.objectStats.lossesByStarshipsPerMT));
	memcpy(g_pilotData.objectStats.lossesByMinesPerMT, record.objectStats.lossesByMinesPerMT,
		   sizeof(record.objectStats.lossesByMinesPerMT));
	memcpy(g_pilotData.objectStats.killedByPlayerRatingPerMT, record.objectStats.killedByPlayerRatingPerMT,
		   sizeof(record.objectStats.killedByPlayerRatingPerMT));
	memcpy(g_pilotData.objectStats.killedByAIRatingPerMT, record.objectStats.killedByAIRatingPerMT,
		   sizeof(record.objectStats.killedByAIRatingPerMT));
	memcpy(g_pilotData.networkPlayers, record.networkPlayers, sizeof(record.networkPlayers));
	memcpy(g_pilotData.teams, record.teams, sizeof(record.teams));
	g_pilotData.currentFactionId = record.currentFactionId;

	for (factionId = 0; factionId < FACTION_COUNT; ++factionId) {
		destinationFaction = &g_pilotData.factionStatistics[factionId];
		sourceFaction = &record.factionStatistics[factionId];
		destinationFaction->totalMissionsPlayedCount = sourceFaction->totalMissionsPlayedCount;
		memcpy(destinationFaction->meleePlaques, sourceFaction->meleePlaques,
			   sizeof(sourceFaction->meleePlaques) + sizeof(sourceFaction->tournamentTrophies) +
				   sizeof(sourceFaction->missionEvaluations) + sizeof(sourceFaction->battleMedallions));
		memcpy(destinationFaction->missionAwards, sourceFaction->missionAwards,
			   sizeof(sourceFaction->missionAwards));
		memcpy(destinationFaction->fieldBC, sourceFaction->fieldBC, sizeof(sourceFaction->fieldBC));
		destinationFaction->totalScore = sourceFaction->totalScore;
		memcpy(destinationFaction->stats.totalScorePerMT, sourceFaction->stats.totalScorePerMT,
			   sizeof(sourceFaction->stats.totalScorePerMT));
		memcpy(destinationFaction->stats.standaloneMissionsPlayedPerMT,
			   sourceFaction->stats.standaloneMissionsPlayedPerMT,
			   sizeof(sourceFaction->stats.standaloneMissionsPlayedPerMT));
		memcpy(destinationFaction->stats.sequenceMissionsPlayedPerMT,
			   sourceFaction->stats.sequenceMissionsPlayedPerMT,
			   sizeof(sourceFaction->stats.sequenceMissionsPlayedPerMT));
		memcpy(destinationFaction->stats.totalKillsPerMT, sourceFaction->stats.totalKillsPerMT,
			   sizeof(sourceFaction->stats.totalKillsPerMT));
		memcpy(destinationFaction->stats.totalFriendliesKilledPerMT,
			   sourceFaction->stats.totalFriendliesKilledPerMT,
			   sizeof(sourceFaction->stats.totalFriendliesKilledPerMT));

		for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
			preservedCraftStats.bWing =
				destinationFaction->stats.killsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX];
			preservedCraftStats.dreadnaught =
				destinationFaction->stats.killsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX];
			preservedCraftStats.modifiedCorvette =
				destinationFaction->stats.killsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
			preservedCraftStats.modifiedFrigate =
				destinationFaction->stats.killsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
			preservedCraftStats.carrackCruiser =
				destinationFaction->stats.killsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX];
			preservedCraftStats.superStarDestroyer =
				destinationFaction->stats.killsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preservedCraftStats.gunEmplacement =
				destinationFaction->stats.killsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destinationFaction->stats.killsPerCraftPerMT[missionType],
				   sourceFaction->stats.killsPerCraftPerMT[missionType],
				   sizeof(sourceFaction->stats.killsPerCraftPerMT[missionType]));
			destinationFaction->stats.killsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX] =
				preservedCraftStats.bWing;
			destinationFaction->stats.killsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX] =
				preservedCraftStats.dreadnaught;
			destinationFaction->stats.killsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preservedCraftStats.modifiedCorvette;
			destinationFaction->stats.killsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preservedCraftStats.modifiedFrigate;
			destinationFaction->stats.killsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
				preservedCraftStats.carrackCruiser;
			destinationFaction->stats.killsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preservedCraftStats.superStarDestroyer;
			destinationFaction->stats.killsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preservedCraftStats.gunEmplacement;

			preservedCraftStats.bWing =
				destinationFaction->stats.killsSharedPerCraftPerMT[missionType][B_WING_CRAFT_INDEX];
			preservedCraftStats.dreadnaught =
				destinationFaction->stats.killsSharedPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX];
			preservedCraftStats.modifiedCorvette =
				destinationFaction->stats
					.killsSharedPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
			preservedCraftStats.modifiedFrigate =
				destinationFaction->stats.killsSharedPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
			preservedCraftStats.carrackCruiser =
				destinationFaction->stats.killsSharedPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX];
			preservedCraftStats.superStarDestroyer =
				destinationFaction->stats
					.killsSharedPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preservedCraftStats.gunEmplacement =
				destinationFaction->stats.killsSharedPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destinationFaction->stats.killsSharedPerCraftPerMT[missionType],
				   sourceFaction->stats.killsSharedPerCraftPerMT[missionType],
				   sizeof(sourceFaction->stats.killsSharedPerCraftPerMT[missionType]));
			destinationFaction->stats.killsSharedPerCraftPerMT[missionType][B_WING_CRAFT_INDEX] =
				preservedCraftStats.bWing;
			destinationFaction->stats.killsSharedPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX] =
				preservedCraftStats.dreadnaught;
			destinationFaction->stats.killsSharedPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preservedCraftStats.modifiedCorvette;
			destinationFaction->stats.killsSharedPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preservedCraftStats.modifiedFrigate;
			destinationFaction->stats.killsSharedPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
				preservedCraftStats.carrackCruiser;
			destinationFaction->stats
				.killsSharedPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preservedCraftStats.superStarDestroyer;
			destinationFaction->stats.killsSharedPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preservedCraftStats.gunEmplacement;

			preservedCraftStats.bWing =
				destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX];
			preservedCraftStats.dreadnaught =
				destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX];
			preservedCraftStats.modifiedCorvette =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
			preservedCraftStats.modifiedFrigate =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
			preservedCraftStats.carrackCruiser =
				destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX];
			preservedCraftStats.superStarDestroyer =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preservedCraftStats.gunEmplacement =
				destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destinationFaction->stats.killsAssistsPerCraftPerMT[missionType],
				   sourceFaction->stats.killsAssistsPerCraftPerMT[missionType],
				   sizeof(sourceFaction->stats.killsAssistsPerCraftPerMT[missionType]));
			destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX] =
				preservedCraftStats.bWing;
			destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][DREADNAUGHT_CRAFT_INDEX] =
				preservedCraftStats.dreadnaught;
			destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preservedCraftStats.modifiedCorvette;
			destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preservedCraftStats.modifiedFrigate;
			destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
				preservedCraftStats.carrackCruiser;
			destinationFaction->stats
				.killsAssistsPerCraftPerMT[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preservedCraftStats.superStarDestroyer;
			destinationFaction->stats.killsAssistsPerCraftPerMT[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preservedCraftStats.gunEmplacement;
		}

		memcpy(destinationFaction->stats.killsFullOnPlayerRatingPerMT,
			   sourceFaction->stats.killsFullOnPlayerRatingPerMT,
			   sizeof(sourceFaction->stats.killsFullOnPlayerRatingPerMT));
		memcpy(destinationFaction->stats.killsSharedOnPlayerRatingPerMT,
			   sourceFaction->stats.killsSharedOnPlayerRatingPerMT,
			   sizeof(sourceFaction->stats.killsSharedOnPlayerRatingPerMT));
		memcpy(destinationFaction->stats.killsAssistOnPlayerRatingPerMT,
			   sourceFaction->stats.killsAssistOnPlayerRatingPerMT,
			   sizeof(sourceFaction->stats.killsAssistOnPlayerRatingPerMT));
		memcpy(destinationFaction->stats.killsFullOnAIRatingPerMT,
			   sourceFaction->stats.killsFullOnAIRatingPerMT,
			   sizeof(sourceFaction->stats.killsFullOnAIRatingPerMT));
		memcpy(destinationFaction->stats.killsSharedOnAIRatingPerMT,
			   sourceFaction->stats.killsSharedOnAIRatingPerMT,
			   sizeof(sourceFaction->stats.killsSharedOnAIRatingPerMT));
		memcpy(destinationFaction->stats.killsAssistOnAIRatingPerMT,
			   sourceFaction->stats.killsAssistOnAIRatingPerMT,
			   sizeof(sourceFaction->stats.killsAssistOnAIRatingPerMT));
		memcpy(destinationFaction->stats.numSpecialInspectedPerMT,
			   sourceFaction->stats.numSpecialInspectedPerMT,
			   sizeof(sourceFaction->stats.numSpecialInspectedPerMT));
		memcpy(destinationFaction->stats.energyHitsPerMT, sourceFaction->stats.energyHitsPerMT,
			   sizeof(sourceFaction->stats.energyHitsPerMT));
		memcpy(destinationFaction->stats.energyFiredPerMT, sourceFaction->stats.energyFiredPerMT,
			   sizeof(sourceFaction->stats.energyFiredPerMT));
		memcpy(destinationFaction->stats.warheadsHitsPerMT, sourceFaction->stats.warheadsHitsPerMT,
			   sizeof(sourceFaction->stats.warheadsHitsPerMT));
		memcpy(destinationFaction->stats.warheadsFiredPerMT, sourceFaction->stats.warheadsFiredPerMT,
			   sizeof(sourceFaction->stats.warheadsFiredPerMT));
		memcpy(destinationFaction->stats.totalCraftLossesPerMT, sourceFaction->stats.totalCraftLossesPerMT,
			   sizeof(sourceFaction->stats.totalCraftLossesPerMT));
		memcpy(destinationFaction->stats.lossesByCollisionsPerMT,
			   sourceFaction->stats.lossesByCollisionsPerMT,
			   sizeof(sourceFaction->stats.lossesByCollisionsPerMT));
		memcpy(destinationFaction->stats.lossesByStarshipsPerMT, sourceFaction->stats.lossesByStarshipsPerMT,
			   sizeof(sourceFaction->stats.lossesByStarshipsPerMT));
		memcpy(destinationFaction->stats.lossesByMinesPerMT, sourceFaction->stats.lossesByMinesPerMT,
			   sizeof(sourceFaction->stats.lossesByMinesPerMT));
		memcpy(destinationFaction->stats.killedByPlayerRatingPerMT,
			   sourceFaction->stats.killedByPlayerRatingPerMT,
			   sizeof(sourceFaction->stats.killedByPlayerRatingPerMT));
		memcpy(destinationFaction->stats.killedByAIRatingPerMT, sourceFaction->stats.killedByAIRatingPerMT,
			   sizeof(sourceFaction->stats.killedByAIRatingPerMT));
		memcpy(destinationFaction->field1558, sourceFaction->spTrainingData,
			   sizeof(sourceFaction->spTrainingData));
		memcpy(&destinationFaction->spTrainingMissions[99].field20, sourceFaction->spMeleeData,
			   sizeof(sourceFaction->spMeleeData));
		memcpy(&destinationFaction->spMeleeMissions[249].field20, sourceFaction->spCombatData,
			   sizeof(sourceFaction->spCombatData));
		memcpy(&destinationFaction->spCombatMissions[249].field20, sourceFaction->mpTrainingData,
			   sizeof(sourceFaction->mpTrainingData));
		memcpy(&destinationFaction->mpTrainingMissions[99].field2C, sourceFaction->mpMeleeData,
			   sizeof(sourceFaction->mpMeleeData));
		memcpy(&destinationFaction->mpMeleeMissions[249].field2C, sourceFaction->mpCombatData,
			   sizeof(sourceFaction->mpCombatData));
		memcpy(&destinationFaction->mpCombatMissions[249].field2C, sourceFaction->spTournamentData,
			   sizeof(sourceFaction->spTournamentData));
		memcpy(&destinationFaction->spTournaments[24].field24, sourceFaction->mpTournamentData,
			   sizeof(sourceFaction->mpTournamentData));
		memcpy(&destinationFaction->mpTournaments[24].field28, sourceFaction->spBattleData,
			   sizeof(sourceFaction->spBattleData));
		memcpy(&destinationFaction->spBattles[24].field20, sourceFaction->mpBattleData,
			   sizeof(sourceFaction->mpBattleData));
	}

	return 1;
}

// FUNCTION: XVT 0x4CB310
int Pilot_WriteXvtRecord(const char* fileName, XvtFile* stream) {
	PilotXvtRecord record;
	XvtFile* inputStream;
	int missionType;
	int factionId;

	memset(&record, 0, sizeof(record));
	File_ChangeToBaseGameInstallPath();
	inputStream = File_Open(fileName, g_fileModeReadBinary);
	if (inputStream != NULL) {
#ifdef XVT_MODERN
		if (!File_ReadCount(inputStream, &record, sizeof(record))) {
			File_Close(inputStream);
			return 0;
		}
#else
		File_ReadCount(inputStream, &record, sizeof(record));
#endif
		File_Close(inputStream);
	}
	File_ChangeToInstallPath();
	File_ChangeToBaseGameInstallPath();
#ifndef XVT_MODERN
	stream = File_Open(fileName, "wb");
#endif

	memcpy(record.name, g_pilotData.name, sizeof(record.name));
	record.totalScore = g_pilotData.totalScore;
	record.localPlayerId = g_pilotData.localPlayerId;
	record.launchSessionMarker = g_pilotData.launchSessionMarker;
	record.isHost = g_pilotData.isHost;
	record.numHumanPlayersLastMission = g_pilotData.numHumanPlayersLastMission;
	record.gameMode = g_pilotData.gameMode;
	memcpy(record.xvtRecordCombatPayload, g_pilotData.xvtRecordPayload,
		   sizeof(record.xvtRecordCombatPayload));
	memcpy(record.xvtRecordIdentityPayload, &g_pilotData.xvtRecordPayload[320],
		   sizeof(record.xvtRecordIdentityPayload));
	memcpy(record.xvtRecordObjectPayload, &g_pilotData.xvtRecordPayload[352],
		   sizeof(record.xvtRecordObjectPayload));
	record.currentRatingPromoPoints = g_pilotData.currentRatingPromoPoints;
	record.currentRatingWorsePromoPoints = g_pilotData.currentRatingWorsePromoPoints;
	record.promotionDelta = g_pilotData.promotionDelta;
	record.nextPromotionPercent = g_pilotData.nextPromotionPercent;

	memcpy(record.mainStats.totalScorePerMT, record.mainStats.totalScorePerMT,
		   sizeof(record.mainStats.totalScorePerMT));
	memcpy(record.mainStats.standaloneMissionsPlayedPerMT, record.mainStats.standaloneMissionsPlayedPerMT,
		   sizeof(record.mainStats.standaloneMissionsPlayedPerMT));
	memcpy(record.mainStats.sequenceMissionsPlayedPerMT, record.mainStats.sequenceMissionsPlayedPerMT,
		   sizeof(record.mainStats.sequenceMissionsPlayedPerMT));
	memcpy(record.mainStats.totalKillsPerMT, record.mainStats.totalKillsPerMT,
		   sizeof(record.mainStats.totalKillsPerMT));
	memcpy(record.mainStats.totalFriendliesKilledPerMT, record.mainStats.totalFriendliesKilledPerMT,
		   sizeof(record.mainStats.totalFriendliesKilledPerMT));

	for (missionType = 0; missionType < 3; ++missionType) {
		memcpy(record.mainStats.killsPerCraftPerMT[missionType],
			   g_pilotData.mainStats.killsPerCraftPerMT[missionType],
			   sizeof(record.mainStats.killsPerCraftPerMT[missionType]));
		memcpy(record.mainStats.killsSharedPerCraftPerMT[missionType],
			   g_pilotData.mainStats.killsSharedPerCraftPerMT[missionType],
			   sizeof(record.mainStats.killsSharedPerCraftPerMT[missionType]));
		memcpy(record.mainStats.killsAssistsPerCraftPerMT[missionType],
			   g_pilotData.mainStats.killsAssistsPerCraftPerMT[missionType],
			   sizeof(record.mainStats.killsAssistsPerCraftPerMT[missionType]));
		record.mainStats.killsPerCraftPerMT[missionType][78] = 0;
		record.mainStats.killsPerCraftPerMT[missionType][54] = 0;
		record.mainStats.killsPerCraftPerMT[missionType][45] = 0;
		record.mainStats.killsPerCraftPerMT[missionType][43] = 0;
		record.mainStats.killsPerCraftPerMT[missionType][41] = 0;
		record.mainStats.killsPerCraftPerMT[missionType][36] = 0;
		record.mainStats.killsPerCraftPerMT[missionType][4] = 0;
		record.mainStats.killsSharedPerCraftPerMT[missionType][78] = 0;
		record.mainStats.killsSharedPerCraftPerMT[missionType][54] = 0;
		record.mainStats.killsSharedPerCraftPerMT[missionType][45] = 0;
		record.mainStats.killsSharedPerCraftPerMT[missionType][43] = 0;
		record.mainStats.killsSharedPerCraftPerMT[missionType][41] = 0;
		record.mainStats.killsSharedPerCraftPerMT[missionType][36] = 0;
		record.mainStats.killsSharedPerCraftPerMT[missionType][4] = 0;
		record.mainStats.killsAssistsPerCraftPerMT[missionType][78] = 0;
		record.mainStats.killsAssistsPerCraftPerMT[missionType][54] = 0;
		record.mainStats.killsAssistsPerCraftPerMT[missionType][45] = 0;
		record.mainStats.killsAssistsPerCraftPerMT[missionType][43] = 0;
		record.mainStats.killsAssistsPerCraftPerMT[missionType][41] = 0;
		record.mainStats.killsAssistsPerCraftPerMT[missionType][36] = 0;
		record.mainStats.killsAssistsPerCraftPerMT[missionType][4] = 0;
	}
	memcpy(record.mainStats.killsFullOnPlayerRatingPerMT, g_pilotData.mainStats.killsFullOnPlayerRatingPerMT,
		   sizeof(record.mainStats.killsFullOnPlayerRatingPerMT));
	memcpy(record.mainStats.killsSharedOnPlayerRatingPerMT,
		   g_pilotData.mainStats.killsSharedOnPlayerRatingPerMT,
		   sizeof(record.mainStats.killsSharedOnPlayerRatingPerMT));
	memcpy(record.mainStats.killsAssistOnPlayerRatingPerMT,
		   g_pilotData.mainStats.killsAssistOnPlayerRatingPerMT,
		   sizeof(record.mainStats.killsAssistOnPlayerRatingPerMT));
	memcpy(record.mainStats.killsFullOnAIRatingPerMT, g_pilotData.mainStats.killsFullOnAIRatingPerMT,
		   sizeof(record.mainStats.killsFullOnAIRatingPerMT));
	memcpy(record.mainStats.killsSharedOnAIRatingPerMT, g_pilotData.mainStats.killsSharedOnAIRatingPerMT,
		   sizeof(record.mainStats.killsSharedOnAIRatingPerMT));
	memcpy(record.mainStats.killsAssistOnAIRatingPerMT, g_pilotData.mainStats.killsAssistOnAIRatingPerMT,
		   sizeof(record.mainStats.killsAssistOnAIRatingPerMT));
	memcpy(record.mainStats.numSpecialInspectedPerMT, g_pilotData.mainStats.numSpecialInspectedPerMT,
		   sizeof(record.mainStats.numSpecialInspectedPerMT));
	memcpy(record.mainStats.energyHitsPerMT, g_pilotData.mainStats.energyHitsPerMT,
		   sizeof(record.mainStats.energyHitsPerMT));
	memcpy(record.mainStats.energyFiredPerMT, g_pilotData.mainStats.energyFiredPerMT,
		   sizeof(record.mainStats.energyFiredPerMT));
	memcpy(record.mainStats.warheadsHitsPerMT, g_pilotData.mainStats.warheadsHitsPerMT,
		   sizeof(record.mainStats.warheadsHitsPerMT));
	memcpy(record.mainStats.warheadsFiredPerMT, g_pilotData.mainStats.warheadsFiredPerMT,
		   sizeof(record.mainStats.warheadsFiredPerMT));
	memcpy(record.mainStats.totalCraftLossesPerMT, g_pilotData.mainStats.totalCraftLossesPerMT,
		   sizeof(record.mainStats.totalCraftLossesPerMT));
	memcpy(record.mainStats.lossesByCollisionsPerMT, g_pilotData.mainStats.lossesByCollisionsPerMT,
		   sizeof(record.mainStats.lossesByCollisionsPerMT));
	memcpy(record.mainStats.lossesByStarshipsPerMT, g_pilotData.mainStats.lossesByStarshipsPerMT,
		   sizeof(record.mainStats.lossesByStarshipsPerMT));
	memcpy(record.mainStats.lossesByMinesPerMT, g_pilotData.mainStats.lossesByMinesPerMT,
		   sizeof(record.mainStats.lossesByMinesPerMT));
	memcpy(record.mainStats.killedByPlayerRatingPerMT, g_pilotData.mainStats.killedByPlayerRatingPerMT,
		   sizeof(record.mainStats.killedByPlayerRatingPerMT));
	memcpy(record.mainStats.killedByAIRatingPerMT, g_pilotData.mainStats.killedByAIRatingPerMT,
		   sizeof(record.mainStats.killedByAIRatingPerMT));

	record.rating = g_pilotData.rating;
	record.totalMissionsPlayedCount = g_pilotData.totalMissionsPlayedCount;
	memcpy(record.totalMissionsPlayedCountPerRating, g_pilotData.totalMissionsPlayedCountPerRating,
		   sizeof(record.totalMissionsPlayedCountPerRating));
	memcpy(record.ratingName, g_pilotData.ratingName, sizeof(record.ratingName));
	record.missionScore = g_pilotData.missionScore;
	memcpy(record.killsFullOnPlayer, g_pilotData.killsFullOnPlayer, sizeof(record.killsFullOnPlayer));
	memcpy(record.killsSharedOnPlayer, g_pilotData.killsSharedOnPlayer, sizeof(record.killsSharedOnPlayer));
	memcpy(record.killsFullOnFlightGroup, g_pilotData.killsFullOnFlightGroup,
		   sizeof(record.killsFullOnFlightGroup));
	memcpy(record.killsSharedOnFlightGroup, g_pilotData.killsSharedOnFlightGroup,
		   sizeof(record.killsSharedOnFlightGroup));
	memcpy(record.killsFullFromPlayer, g_pilotData.killsFullFromPlayer, sizeof(record.killsFullFromPlayer));
	memcpy(record.killsSharedFromPlayer, g_pilotData.killsSharedFromPlayer,
		   sizeof(record.killsSharedFromPlayer));
	memcpy(record.killsFullFromFlightGroup, g_pilotData.killsFullFromFlightGroup,
		   sizeof(record.killsFullFromFlightGroup));
	memcpy(record.killsSharedFromFlightGroup, g_pilotData.killsSharedFromFlightGroup,
		   sizeof(record.killsSharedFromFlightGroup));
	memcpy(record.flightGroupRating, g_pilotData.flightGroupRating, sizeof(record.flightGroupRating));

	memcpy(record.objectStats.totalScorePerMT, g_pilotData.objectStats.totalScorePerMT,
		   sizeof(record.objectStats.totalScorePerMT));
	memcpy(record.objectStats.standaloneMissionsPlayedPerMT,
		   g_pilotData.objectStats.standaloneMissionsPlayedPerMT,
		   sizeof(record.objectStats.standaloneMissionsPlayedPerMT));
	memcpy(record.objectStats.sequenceMissionsPlayedPerMT,
		   g_pilotData.objectStats.sequenceMissionsPlayedPerMT,
		   sizeof(record.objectStats.sequenceMissionsPlayedPerMT));
	memcpy(record.objectStats.totalKillsPerMT, g_pilotData.objectStats.totalKillsPerMT,
		   sizeof(record.objectStats.totalKillsPerMT));
	memcpy(record.objectStats.totalFriendliesKilledPerMT, g_pilotData.objectStats.totalFriendliesKilledPerMT,
		   sizeof(record.objectStats.totalFriendliesKilledPerMT));
	for (missionType = 0; missionType < 3; ++missionType) {
		memcpy(record.objectStats.killsPerCraftPerMT[missionType],
			   g_pilotData.objectStats.killsPerCraftPerMT[missionType],
			   sizeof(record.objectStats.killsPerCraftPerMT[missionType]));
		memcpy(record.objectStats.killsSharedPerCraftPerMT[missionType],
			   g_pilotData.objectStats.killsSharedPerCraftPerMT[missionType],
			   sizeof(record.objectStats.killsSharedPerCraftPerMT[missionType]));
		memcpy(record.objectStats.killsAssistsPerCraftPerMT[missionType],
			   g_pilotData.objectStats.killsAssistsPerCraftPerMT[missionType],
			   sizeof(record.objectStats.killsAssistsPerCraftPerMT[missionType]));
		record.objectStats.killsPerCraftPerMT[missionType][78] = 0;
		record.objectStats.killsPerCraftPerMT[missionType][54] = 0;
		record.objectStats.killsPerCraftPerMT[missionType][45] = 0;
		record.objectStats.killsPerCraftPerMT[missionType][43] = 0;
		record.objectStats.killsPerCraftPerMT[missionType][41] = 0;
		record.objectStats.killsPerCraftPerMT[missionType][36] = 0;
		record.objectStats.killsPerCraftPerMT[missionType][4] = 0;
		record.objectStats.killsSharedPerCraftPerMT[missionType][78] = 0;
		record.objectStats.killsSharedPerCraftPerMT[missionType][54] = 0;
		record.objectStats.killsSharedPerCraftPerMT[missionType][45] = 0;
		record.objectStats.killsSharedPerCraftPerMT[missionType][43] = 0;
		record.objectStats.killsSharedPerCraftPerMT[missionType][41] = 0;
		record.objectStats.killsSharedPerCraftPerMT[missionType][36] = 0;
		record.objectStats.killsSharedPerCraftPerMT[missionType][4] = 0;
		record.objectStats.killsAssistsPerCraftPerMT[missionType][78] = 0;
		record.objectStats.killsAssistsPerCraftPerMT[missionType][54] = 0;
		record.objectStats.killsAssistsPerCraftPerMT[missionType][45] = 0;
		record.objectStats.killsAssistsPerCraftPerMT[missionType][43] = 0;
		record.objectStats.killsAssistsPerCraftPerMT[missionType][41] = 0;
		record.objectStats.killsAssistsPerCraftPerMT[missionType][36] = 0;
		record.objectStats.killsAssistsPerCraftPerMT[missionType][4] = 0;
	}
	memcpy(record.objectStats.killsFullOnPlayerRatingPerMT,
		   g_pilotData.objectStats.killsFullOnPlayerRatingPerMT,
		   sizeof(record.objectStats.killsFullOnPlayerRatingPerMT));
	memcpy(record.objectStats.killsSharedOnPlayerRatingPerMT,
		   g_pilotData.objectStats.killsSharedOnPlayerRatingPerMT,
		   sizeof(record.objectStats.killsSharedOnPlayerRatingPerMT));
	memcpy(record.objectStats.killsAssistOnPlayerRatingPerMT,
		   g_pilotData.objectStats.killsAssistOnPlayerRatingPerMT,
		   sizeof(record.objectStats.killsAssistOnPlayerRatingPerMT));
	memcpy(record.objectStats.killsFullOnAIRatingPerMT, g_pilotData.objectStats.killsFullOnAIRatingPerMT,
		   sizeof(record.objectStats.killsFullOnAIRatingPerMT));
	memcpy(record.objectStats.killsSharedOnAIRatingPerMT, g_pilotData.objectStats.killsSharedOnAIRatingPerMT,
		   sizeof(record.objectStats.killsSharedOnAIRatingPerMT));
	memcpy(record.objectStats.killsAssistOnAIRatingPerMT, g_pilotData.objectStats.killsAssistOnAIRatingPerMT,
		   sizeof(record.objectStats.killsAssistOnAIRatingPerMT));
	memcpy(record.objectStats.numSpecialInspectedPerMT, g_pilotData.objectStats.numSpecialInspectedPerMT,
		   sizeof(record.objectStats.numSpecialInspectedPerMT));
	memcpy(record.objectStats.energyHitsPerMT, g_pilotData.objectStats.energyHitsPerMT,
		   sizeof(record.objectStats.energyHitsPerMT));
	memcpy(record.objectStats.energyFiredPerMT, g_pilotData.objectStats.energyFiredPerMT,
		   sizeof(record.objectStats.energyFiredPerMT));
	memcpy(record.objectStats.warheadsHitsPerMT, g_pilotData.objectStats.warheadsHitsPerMT,
		   sizeof(record.objectStats.warheadsHitsPerMT));
	memcpy(record.objectStats.warheadsFiredPerMT, g_pilotData.objectStats.warheadsFiredPerMT,
		   sizeof(record.objectStats.warheadsFiredPerMT));
	memcpy(record.objectStats.totalCraftLossesPerMT, g_pilotData.objectStats.totalCraftLossesPerMT,
		   sizeof(record.objectStats.totalCraftLossesPerMT));
	memcpy(record.objectStats.lossesByCollisionsPerMT, g_pilotData.objectStats.lossesByCollisionsPerMT,
		   sizeof(record.objectStats.lossesByCollisionsPerMT));
	memcpy(record.objectStats.lossesByStarshipsPerMT, g_pilotData.objectStats.lossesByStarshipsPerMT,
		   sizeof(record.objectStats.lossesByStarshipsPerMT));
	memcpy(record.objectStats.lossesByMinesPerMT, g_pilotData.objectStats.lossesByMinesPerMT,
		   sizeof(record.objectStats.lossesByMinesPerMT));
	memcpy(record.objectStats.killedByPlayerRatingPerMT, g_pilotData.objectStats.killedByPlayerRatingPerMT,
		   sizeof(record.objectStats.killedByPlayerRatingPerMT));
	memcpy(record.objectStats.killedByAIRatingPerMT, g_pilotData.objectStats.killedByAIRatingPerMT,
		   sizeof(record.objectStats.killedByAIRatingPerMT));
	memcpy(record.networkPlayers, g_pilotData.networkPlayers, sizeof(record.networkPlayers));
	memcpy(record.teams, g_pilotData.teams, sizeof(record.teams));
	record.currentFactionId = g_pilotData.currentFactionId;

	for (factionId = 0; factionId < 4; ++factionId) {
		PilotXvtFaction* destination = &record.factionStatistics[factionId];
		PilotFaction* source = &g_pilotData.factionStatistics[factionId];

		destination->totalMissionsPlayedCount = source->totalMissionsPlayedCount;
		memcpy(destination->meleePlaques, source->meleePlaques,
			   sizeof(destination->meleePlaques) + sizeof(destination->tournamentTrophies) +
				   sizeof(destination->missionEvaluations) + sizeof(destination->battleMedallions));
		memcpy(destination->missionAwards, source->missionAwards, sizeof(destination->missionAwards));
		memcpy(destination->fieldBC, source->fieldBC, sizeof(destination->fieldBC));
		destination->totalScore = source->totalScore;
		memcpy(destination->stats.totalScorePerMT, source->stats.totalScorePerMT,
			   sizeof(destination->stats.totalScorePerMT));
		memcpy(destination->stats.standaloneMissionsPlayedPerMT, source->stats.standaloneMissionsPlayedPerMT,
			   sizeof(destination->stats.standaloneMissionsPlayedPerMT));
		memcpy(destination->stats.sequenceMissionsPlayedPerMT, source->stats.sequenceMissionsPlayedPerMT,
			   sizeof(destination->stats.sequenceMissionsPlayedPerMT));
		memcpy(destination->stats.totalKillsPerMT, source->stats.totalKillsPerMT,
			   sizeof(destination->stats.totalKillsPerMT));
		memcpy(destination->stats.totalFriendliesKilledPerMT, source->stats.totalFriendliesKilledPerMT,
			   sizeof(destination->stats.totalFriendliesKilledPerMT));
		for (missionType = 0; missionType < 3; ++missionType) {
			memcpy(destination->stats.killsPerCraftPerMT[missionType],
				   source->stats.killsPerCraftPerMT[missionType],
				   sizeof(destination->stats.killsPerCraftPerMT[missionType]));
			memcpy(destination->stats.killsSharedPerCraftPerMT[missionType],
				   source->stats.killsSharedPerCraftPerMT[missionType],
				   sizeof(destination->stats.killsSharedPerCraftPerMT[missionType]));
			memcpy(destination->stats.killsAssistsPerCraftPerMT[missionType],
				   source->stats.killsAssistsPerCraftPerMT[missionType],
				   sizeof(destination->stats.killsAssistsPerCraftPerMT[missionType]));
			destination->stats.killsPerCraftPerMT[missionType][78] = 0;
			destination->stats.killsPerCraftPerMT[missionType][54] = 0;
			destination->stats.killsPerCraftPerMT[missionType][45] = 0;
			destination->stats.killsPerCraftPerMT[missionType][43] = 0;
			destination->stats.killsPerCraftPerMT[missionType][41] = 0;
			destination->stats.killsPerCraftPerMT[missionType][36] = 0;
			destination->stats.killsPerCraftPerMT[missionType][4] = 0;
			destination->stats.killsSharedPerCraftPerMT[missionType][78] = 0;
			destination->stats.killsSharedPerCraftPerMT[missionType][54] = 0;
			destination->stats.killsSharedPerCraftPerMT[missionType][45] = 0;
			destination->stats.killsSharedPerCraftPerMT[missionType][43] = 0;
			destination->stats.killsSharedPerCraftPerMT[missionType][41] = 0;
			destination->stats.killsSharedPerCraftPerMT[missionType][36] = 0;
			destination->stats.killsSharedPerCraftPerMT[missionType][4] = 0;
			destination->stats.killsAssistsPerCraftPerMT[missionType][78] = 0;
			destination->stats.killsAssistsPerCraftPerMT[missionType][54] = 0;
			destination->stats.killsAssistsPerCraftPerMT[missionType][45] = 0;
			destination->stats.killsAssistsPerCraftPerMT[missionType][43] = 0;
			destination->stats.killsAssistsPerCraftPerMT[missionType][41] = 0;
			destination->stats.killsAssistsPerCraftPerMT[missionType][36] = 0;
			destination->stats.killsAssistsPerCraftPerMT[missionType][4] = 0;
		}
		memcpy(destination->stats.killsFullOnPlayerRatingPerMT, source->stats.killsFullOnPlayerRatingPerMT,
			   sizeof(destination->stats.killsFullOnPlayerRatingPerMT));
		memcpy(destination->stats.killsSharedOnPlayerRatingPerMT,
			   source->stats.killsSharedOnPlayerRatingPerMT,
			   sizeof(destination->stats.killsSharedOnPlayerRatingPerMT));
		memcpy(destination->stats.killsAssistOnPlayerRatingPerMT,
			   source->stats.killsAssistOnPlayerRatingPerMT,
			   sizeof(destination->stats.killsAssistOnPlayerRatingPerMT));
		memcpy(destination->stats.killsFullOnAIRatingPerMT, source->stats.killsFullOnAIRatingPerMT,
			   sizeof(destination->stats.killsFullOnAIRatingPerMT));
		memcpy(destination->stats.killsSharedOnAIRatingPerMT, source->stats.killsSharedOnAIRatingPerMT,
			   sizeof(destination->stats.killsSharedOnAIRatingPerMT));
		memcpy(destination->stats.killsAssistOnAIRatingPerMT, source->stats.killsAssistOnAIRatingPerMT,
			   sizeof(destination->stats.killsAssistOnAIRatingPerMT));
		memcpy(destination->stats.numSpecialInspectedPerMT, source->stats.numSpecialInspectedPerMT,
			   sizeof(destination->stats.numSpecialInspectedPerMT));
		memcpy(destination->stats.energyHitsPerMT, source->stats.energyHitsPerMT,
			   sizeof(destination->stats.energyHitsPerMT));
		memcpy(destination->stats.energyFiredPerMT, source->stats.energyFiredPerMT,
			   sizeof(destination->stats.energyFiredPerMT));
		memcpy(destination->stats.warheadsHitsPerMT, source->stats.warheadsHitsPerMT,
			   sizeof(destination->stats.warheadsHitsPerMT));
		memcpy(destination->stats.warheadsFiredPerMT, source->stats.warheadsFiredPerMT,
			   sizeof(destination->stats.warheadsFiredPerMT));
		memcpy(destination->stats.totalCraftLossesPerMT, source->stats.totalCraftLossesPerMT,
			   sizeof(destination->stats.totalCraftLossesPerMT));
		memcpy(destination->stats.lossesByCollisionsPerMT, source->stats.lossesByCollisionsPerMT,
			   sizeof(destination->stats.lossesByCollisionsPerMT));
		memcpy(destination->stats.lossesByStarshipsPerMT, source->stats.lossesByStarshipsPerMT,
			   sizeof(destination->stats.lossesByStarshipsPerMT));
		memcpy(destination->stats.lossesByMinesPerMT, source->stats.lossesByMinesPerMT,
			   sizeof(destination->stats.lossesByMinesPerMT));
		memcpy(destination->stats.killedByPlayerRatingPerMT, source->stats.killedByPlayerRatingPerMT,
			   sizeof(destination->stats.killedByPlayerRatingPerMT));
		memcpy(destination->stats.killedByAIRatingPerMT, source->stats.killedByAIRatingPerMT,
			   sizeof(destination->stats.killedByAIRatingPerMT));
		memcpy(destination->spTrainingData, source->field1558, sizeof(destination->spTrainingData));
		memcpy(destination->spMeleeData, &source->spTrainingMissions[99].field20,
			   sizeof(destination->spMeleeData));
		memcpy(destination->spCombatData, &source->spMeleeMissions[249].field20,
			   sizeof(destination->spCombatData));
		memcpy(destination->mpTrainingData, &source->spCombatMissions[249].field20,
			   sizeof(destination->mpTrainingData));
		memcpy(destination->mpMeleeData, &source->mpTrainingMissions[99].field2C,
			   sizeof(destination->mpMeleeData));
		memcpy(destination->mpCombatData, &source->mpMeleeMissions[249].field2C,
			   sizeof(destination->mpCombatData));
		memcpy(destination->spTournamentData, &source->mpCombatMissions[249].field2C,
			   sizeof(destination->spTournamentData));
		memcpy(destination->mpTournamentData, &source->spTournaments[24].field24,
			   sizeof(destination->mpTournamentData));
		memcpy(destination->spBattleData, &source->mpTournaments[24].field28,
			   sizeof(destination->spBattleData));
		memcpy(destination->mpBattleData, &source->spBattles[24].field20, sizeof(destination->mpBattleData));
	}

#ifdef XVT_MODERN
	(void)stream;
	return XvtStorage_WriteAtomic(fileName, &record, sizeof(record));
#else
	File_WriteCount(stream, &record, sizeof(record));
	File_ChangeToInstallPath();
	File_Close(stream);
	return 1;
#endif
}
