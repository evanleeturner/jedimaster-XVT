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

/* Forty pilot names; Pilot_ParseCommandLine picks one at random for a pilot
 * named "joiner" or "host" on the command line. */
// GLOBAL: XVT 0x52AF78
const char *const g_randomPilotNames[40] = {
	"Luke",	       "Han Solo",    "Darth Vader", "Leia",	     "Lando",
	"Boba Fett",   "Chewbacca",   "R2-D2",	     "C-3PO",	     "Jabba",
	"Greedo",      "Thrawn",      "Ackbar",	     "Wedge",	     "Bollux",
	"Blue Max",    "Bossk",	      "C'Baoth",     "Palpatine",    "Biggs",
	"Dodonna",     "Bib Fortuna", "Mara Jade",   "Talon Karrde", "Obi-Wan",
	"Lobot",       "Crix Madine", "Mon Mothma",  "Nien Nunb",    "Pellaeon",
	"Anakin Solo", "Jacen Solo",  "Jaina Solo",  "Tarkin",	     "Yoda",
	"Zaarin",      "Harkov",      "Tarrak",	     "Xizor",	     "Guri",
};

/* Deletes the selected pilot's files and clears g_pilotData. Finds the pilot's
 * name in g_pilotListDisplayNames, ignoring case, and deletes that entry's .plt
 * file in the base game folder and the file of the same name ending in '2'
 * (.pl2) in the install folder. Then clears g_pilotData and rebuilds the pilot
 * list. Returns 1; the modern build returns 0, leaving g_pilotData as it was,
 * when a file cannot be removed. Also zeroes a 253,754-byte local record that
 * it never uses. */
// FUNCTION: XVT 0x4BF260
int Pilot_DeleteCurrent(void)
{
	uint8_t xvtPilotRecord[0x3DF3A];
	FrontendFileListNode *node;
	int selectedIndex;

	if (g_pilotListDisplayNames != NULL && g_pilotData.name[0] != '\0' &&
	    g_pilotFileList != NULL) {
		node = g_pilotFileList->head;
		selectedIndex = 0;
		if (g_pilotListDisplayNames != NULL) {
			while (g_pilotFileList->count > selectedIndex) {
#ifdef XVT_MODERN
				if (strcasecmp(g_pilotListDisplayNames
						       [selectedIndex],
					       g_pilotData.name) == 0) {
#else
				if (_strcmpi(g_pilotListDisplayNames
						     [selectedIndex],
					     g_pilotData.name) == 0) {
#endif
					File_ChangeToBaseGameInstallPath();
#ifdef XVT_MODERN
					if (!Pilot_RemoveFileModern(
						    node->path)) {
						return 0;
					}
#else
					File_Remove(node->path);
#endif
					File_ChangeToInstallPath();
					strcpy(g_frontendScratchBuffer,
					       node->path);
					g_frontendScratchBuffer
						[strlen(g_frontendScratchBuffer) -
						 1] = '2';
#ifdef XVT_MODERN
					if (!Pilot_RemoveFileModern(
						    g_frontendScratchBuffer)) {
						return 0;
					}
#else
					File_Remove(g_frontendScratchBuffer);
#endif
					break;
				}
				node = node->next;
				++selectedIndex;
				if (g_pilotListDisplayNames[selectedIndex] ==
				    NULL) {
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

/* Creates a pilot named pilotName and makes it the current one. Picks the first
 * NAMEn.pl2 that does not exist, counting n from 0; the original build opens it
 * for writing and returns 0 when it cannot. Clears g_pilotData and sets the
 * name, the Trainee rating, and the game and host names (the name with
 * FRONTSTR_470_S_GAME after it). The first mission of the training exercises
 * list becomes the choice of the pilot and of faction record 0, that of the
 * list loaded for faction 1 record 1's, and that of the multiplayer list record
 * 2's. Writes g_pilotData to the file, then sets combat engagement choices (the
 * list's third mission for the pilot, the first for factions 0 and 1) and saves
 * again with Pilot_Save(0). Returns 1 in the original build, or Pilot_Save's
 * result in the modern one, which also returns 0 when the first write fails.
 * Leaves g_frontendMissionSessionMode at none. Checks neither the name's length
 * nor that the combat list holds three missions. */
// FUNCTION: XVT 0x4BF3A0
int Pilot_CreateNew(const char *pilotName)
{
	char pilotPath[32];
	XvtFile *probeStream;
#ifndef XVT_MODERN
	XvtFile *stream;
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
		if (probeStream == NULL) {
			break;
		}
#ifdef XVT_MODERN
		File_Close(probeStream);
#else
		File_RawClose(probeStream);
#endif
		++fileIndex;
	}

#ifndef XVT_MODERN
	stream = File_Open(pilotPath, "wb");
	if (stream == NULL) {
		return 0;
	}
#endif

	memset(&g_pilotData, 0, sizeof(g_pilotData));
	strcpy(g_pilotData.name, pilotName);
	g_pilotData.rating = PILOT_RATING_TRAINEE;
	strcpy(g_pilotData.ratingName,
	       FrontendString_Get(FRONTSTR_124_TRAINEE));
	sprintf(g_pilotData.multiplayerGameName, "%s%s", pilotName,
		FrontendString_Get(FRONTSTR_470_S_GAME));
	strcpy(g_pilotData.multiplayerHostName,
	       g_pilotData.multiplayerGameName);

	MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
	if (g_missionList != NULL) {
		g_pilotData.missionDescriptionIds
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_missionList[0].missionIdx;
		g_pilotData.factionStatistics[0].missionDescriptionIds
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}

	g_pilotData.currentFactionId = 1;
	MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
	g_pilotData.currentFactionId = 0;
	if (g_missionList != NULL) {
		g_pilotData.factionStatistics[1].missionDescriptionIds
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}

	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NET_HOST;
	MissionSetup_LoadMissionList(MISSION_DIRECTORY_TRAINING_EXERCISES);
	if (g_missionList != NULL) {
		g_pilotData.factionStatistics[2].missionDescriptionIds
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_NONE;
#ifdef XVT_MODERN
	if (!XvtStorage_WriteAtomic(pilotPath, &g_pilotData,
				    sizeof(g_pilotData))) {
		return 0;
	}
#else
	File_WriteBytes(stream, &g_pilotData, sizeof(g_pilotData));
#endif

	MissionSetup_LoadMissionList(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	if (g_missionList != NULL) {
		g_pilotData.missionDescriptionIds
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
			g_missionList[2].missionIdx;
		g_pilotData.factionStatistics[0].missionDescriptionIds
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
			g_missionList[0].missionIdx;
		free(g_missionList);
		g_missionList = NULL;
	}

	g_pilotData.currentFactionId = 1;
	MissionSetup_LoadMissionList(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	g_pilotData.currentFactionId = 0;
	if (g_missionList != NULL) {
		g_pilotData.factionStatistics[1].missionDescriptionIds
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
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

/* Saves g_pilotData to the pilot's .pl2 file and the base game's record to its
 * .plt file. Returns 0 without a pilot name. With useTemporaryFile 0, the .pl2
 * is the *.pl2 file in the current folder whose first 14 bytes hold the pilot's
 * name, ignoring case; otherwise "__temp__.tmp". With none found, it is the
 * name of the matching *.plt in the base game folder with its last letter made
 * '2', or else NAME0.pl2. The original build returns 0 when the file does not
 * open; the modern build writes it whole or returns 0. Then writes the record
 * with Pilot_WriteXvtRecord to the same name with its last letter made 't'.
 * Returns 1 in the original build, ignoring that write, or
 * Pilot_WriteXvtRecord's result in the modern one. Every caller passes 0. */
// FUNCTION: XVT 0x4BF650
int Pilot_Save(int useTemporaryFile)
{
	char pilotPath[30];
	FrontendFileList *fileList;
	FrontendFileListNode *node;
	XvtFile *stream;
	int fileIndex;

	if (g_pilotData.name[0] == '\0') {
		return 0;
	}

	memset(pilotPath, 0, sizeof(pilotPath));
	if (useTemporaryFile == 0) {
		fileList = FrontendFileList_BuildSorted("*.pl2");
		if (fileList != NULL) {
			node = fileList->head;
			fileIndex = 0;
			while (fileList->count > fileIndex) {
				stream = File_Open(node->path,
						   g_fileModeReadBinary);
				if (stream != NULL) {
					File_ReadBytes(
						stream, g_frontendScratchBuffer,
						sizeof(g_pilotData.name));
					File_Close(stream);
#ifdef XVT_MODERN
					if (strcasecmp(g_frontendScratchBuffer,
						       g_pilotData.name) == 0) {
						snprintf(pilotPath,
							 sizeof(pilotPath),
							 "%s", node->path);
#else
					if (_strcmpi(g_frontendScratchBuffer,
						     g_pilotData.name) == 0) {
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
				stream = File_Open(node->path,
						   g_fileModeReadBinary);
				if (stream != NULL) {
					File_ReadBytes(
						stream, g_frontendScratchBuffer,
						sizeof(g_pilotData.name));
					File_Close(stream);
#ifdef XVT_MODERN
					if (strcasecmp(g_frontendScratchBuffer,
						       g_pilotData.name) == 0) {
						snprintf(pilotPath,
							 sizeof(pilotPath),
							 "%s", node->path);
#else
					if (_strcmpi(g_frontendScratchBuffer,
						     g_pilotData.name) == 0) {
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
			snprintf(pilotPath, sizeof(pilotPath), "%s0.pl2",
				 g_pilotData.name);
#else
			sprintf(pilotPath, "%s0.pl2", g_pilotData.name);
#endif
		} else {
			pilotPath[strlen(pilotPath) - 1] = '2';
		}
	}

#ifndef XVT_MODERN
	stream = File_Open(pilotPath, "wb");
	if (stream == NULL) {
		return 0;
	}
#endif
#ifdef XVT_MODERN
	if (!XvtStorage_WriteAtomic(pilotPath, &g_pilotData,
				    sizeof(g_pilotData))) {
		return 0;
	}
#else
	File_WriteBytes(stream, &g_pilotData, sizeof(g_pilotData));
#endif
#ifndef XVT_MODERN
	File_Close(stream);
#endif
	if (pilotPath[0] == '\0') {
#ifdef XVT_MODERN
		snprintf(pilotPath, sizeof(pilotPath), "%s0.plt",
			 g_pilotData.name);
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

/* Loads the pilot whose .plt file in the base game folder holds pilotName in
 * its first 14 bytes, ignoring case, through Pilot_LoadFromPath; the first
 * match decides. Returns 1 when it loaded, 0 when the file list cannot be
 * built, pilotName is NULL or empty, nothing matches, or the load fails. */
// FUNCTION: XVT 0x4BF8C0
int Pilot_FindAndLoadByName(const char *pilotName)
{
	FrontendFileList *fileList;
	FrontendFileListNode *node;
	XvtFile *stream;
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
			File_ReadBytes(stream, g_frontendScratchBuffer,
				       sizeof(g_pilotData.name));
			File_Close(stream);
#ifdef XVT_MODERN
			if (strcasecmp(g_frontendScratchBuffer, pilotName) ==
			    0) {
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

/* Reads the quoted options of the command line. "a=ADDRESS" copies the address
 * into g_gameConfig.ipAddress; any other quoted option whose second character
 * is '=' gives a pilot name. With a pilot name, when no last pilot is set or it
 * cannot be loaded, it makes the given name, or a random one of
 * g_randomPilotNames for "joiner" or "host", the last pilot and creates it;
 * when the last pilot loads, the given name is ignored. With both a name and an
 * address it sets TCP/IP and internet play; otherwise it clears g_optIsHost and
 * g_optIsClient. Returns 1 on every path. */
// FUNCTION: XVT 0x4C9C50
int Pilot_ParseCommandLine(const char *cmdLine)
{
	int commandIndex;
	int commandLength;
	int hasNetworkAddress;

	struct ParsedCommandLine {
		int hasPilotName; /* 1 once an option gave a pilot name. */
		/* Name from the last name option, at most 12 characters. */
		char pilotName[16];
		/* The quoted option being read, at most 255 characters. */
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
					if (parameterIndex >=
					    (int)sizeof(parsed.parameter) - 1) {
						break;
					}
					parsed.parameter[parameterIndex] =
						cmdLine[commandIndex];
					++parameterIndex;
					++commandIndex;
				}

				parsed.parameter[parameterIndex] = '\0';
				if (parsed.parameter[0] == 'a' &&
				    parsed.parameter[1] == '=') {
					strncpy(g_gameConfig.ipAddress,
						&parsed.parameter[2],
						sizeof(g_gameConfig.ipAddress) -
							1);
					g_gameConfig.ipAddress
						[sizeof(g_gameConfig
								.ipAddress) -
						 1] = '\0';
					hasNetworkAddress = 1;
				} else {
					/* The original parser accepts every non-address option using the x= form. */
					parsed.parameter[0] =
						parsed.parameter[1] == '=';
					if (parsed.parameter[0] != 0) {
						strncpy(parsed.pilotName,
							&parsed.parameter[2],
							sizeof(g_gameConfig
								       .lastPilotName) -
								1);
						parsed.pilotName
							[sizeof(g_gameConfig
									.lastPilotName) -
							 1] = '\0';
						parsed.hasPilotName = 1;
					}
				}
			}

			++commandIndex;
		} while (commandIndex < commandLength);
	}

	if (parsed.hasPilotName != 0) {
		if (g_gameConfig.lastPilotName[0] == '\0') {
			if (strcmp(parsed.pilotName, "joiner") == 0 ||
			    strcmp(parsed.pilotName, "host") == 0) {
				srand(GetTickCount());
				strcpy(g_gameConfig.lastPilotName,
				       g_randomPilotNames
					       [rand() %
						RANDOM_PILOT_NAME_COUNT]);
			} else {
				strcpy(g_gameConfig.lastPilotName,
				       parsed.pilotName);
			}
			Pilot_CreateNew(g_gameConfig.lastPilotName);
		} else if (Pilot_FindAndLoadByName(
				   g_gameConfig.lastPilotName) == 0) {
			if (strcmp(parsed.pilotName, "joiner") == 0 ||
			    strcmp(parsed.pilotName, "host") == 0) {
				srand(GetTickCount());
				strcpy(g_gameConfig.lastPilotName,
				       g_randomPilotNames
					       [rand() %
						RANDOM_PILOT_NAME_COUNT]);
			} else {
				strcpy(g_gameConfig.lastPilotName,
				       parsed.pilotName);
			}
			Pilot_CreateNew(g_gameConfig.lastPilotName);
		}
	}

	if (parsed.hasPilotName != 0 && hasNetworkAddress != 0) {
		g_gameConfig.networkType = NET_TRANSPORT_TCPIP;
		g_gameConfig.internetPlay = 1;
		return 1;
	}

	g_optIsHost = 0;
	g_optIsClient = 0;
	return 1;
}

/* Loads the pilot whose .plt file in the base game folder is basePilotPath.
 * Clears g_pilotData and reads the .pl2 file of the same name ending in '2', in
 * the install folder, into it when that opens. When the .plt opens, a pilot
 * without a .pl2 first gets a new pilot's rating and training mission choices,
 * then Pilot_LoadXvtRecord copies the record over g_pilotData, and the pilot
 * without a .pl2 gets its game and host names. Returns 0 when neither file
 * opens, and in the modern build when a read fails; else 1. */
// FUNCTION: XVT 0x4CB0C0
int Pilot_LoadFromPath(const char *basePilotPath)
{
	char expansionPilotPath[128];
	XvtFile *expansionStream;
	XvtFile *xvtStream;
	int hasExpansionRecord;

	hasExpansionRecord = 0;
	memset(&g_pilotData, 0, sizeof(g_pilotData));
	strcpy(expansionPilotPath, basePilotPath);
	expansionPilotPath[strlen(expansionPilotPath) - 1] = '2';
	expansionStream = File_Open(expansionPilotPath, g_fileModeReadBinary);
	if (expansionStream != NULL) {
		hasExpansionRecord = 1;
#ifdef XVT_MODERN
		if (!File_ReadBytes(expansionStream, &g_pilotData,
				    sizeof(g_pilotData))) {
			File_Close(expansionStream);
			memset(&g_pilotData, 0, sizeof(g_pilotData));
			return 0;
		}
#else
		File_ReadBytes(expansionStream, &g_pilotData,
			       sizeof(g_pilotData));
#endif
		File_Close(expansionStream);
	}

	File_ChangeToBaseGameInstallPath();
	xvtStream = File_Open(basePilotPath, g_fileModeReadBinary);
	File_ChangeToInstallPath();
	if (xvtStream != NULL) {
		if (hasExpansionRecord == 0) {
			g_pilotData.rating = PILOT_RATING_TRAINEE;
			strcpy(g_pilotData.ratingName,
			       FrontendString_Get(FRONTSTR_124_TRAINEE));
			MissionSetup_LoadMissionList(
				MISSION_DIRECTORY_TRAINING_EXERCISES);
			if (g_missionList != NULL) {
				g_pilotData.missionDescriptionIds
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				g_pilotData.factionStatistics[0].missionDescriptionIds
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				free(g_missionList);
				g_missionList = NULL;
			}

			g_pilotData.currentFactionId = 1;
			MissionSetup_LoadMissionList(
				MISSION_DIRECTORY_TRAINING_EXERCISES);
			g_pilotData.currentFactionId = 0;
			if (g_missionList != NULL) {
				g_pilotData.factionStatistics[1].missionDescriptionIds
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				free(g_missionList);
				g_missionList = NULL;
			}

			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NET_HOST;
			MissionSetup_LoadMissionList(
				MISSION_DIRECTORY_TRAINING_EXERCISES);
			if (g_missionList != NULL) {
				g_pilotData.factionStatistics[2].missionDescriptionIds
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_missionList->missionIdx;
				free(g_missionList);
				g_missionList = NULL;
			}
			g_frontendMissionSessionMode =
				FRONTEND_MISSION_SESSION_NONE;
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
			sprintf(g_pilotData.multiplayerGameName, "%s%s",
				g_pilotData.name,
				FrontendString_Get(FRONTSTR_470_S_GAME));
			strcpy(g_pilotData.multiplayerHostName,
			       g_pilotData.multiplayerGameName);
		}
	} else if (hasExpansionRecord == 0) {
		return 0;
	}

	return 1;
}

#pragma pack(push, 1)

/* PilotStats as the base game's pilot file stores it: the same tables in the
 * same order, each by the three mission types, but 88 craft types in each
 * per-craft table where PilotStats has 100. Pilot_LoadXvtRecord and
 * Pilot_WriteXvtRecord copy each field to and from the PilotStats field of the
 * same name, with the exceptions their comments give. */
typedef struct PilotXvtStats {
	int totalScorePerMT[3]; /* Score. */
	/* Missions flown on their own. */
	int standaloneMissionsPlayedPerMT[3];
	int sequenceMissionsPlayedPerMT[3];  /* Missions flown in a sequence. */
	int totalKillsPerMT[3];		     /* Full kills. */
	int totalFriendliesKilledPerMT[3];   /* Friendly craft killed. */
	int killsPerCraftPerMT[3][88];	     /* Full kills by craft type. */
	int killsSharedPerCraftPerMT[3][88]; /* Shared kills by craft type. */
	int killsAssistsPerCraftPerMT[3][88]; /* Assists by craft type. */
	/* Full kills of players, by the victim's rating. */
	int killsFullOnPlayerRatingPerMT[3][25];
	/* Shared kills of players, by the victim's rating. */
	int killsSharedOnPlayerRatingPerMT[3][25];
	/* Assists on players, by the victim's rating. */
	int killsAssistOnPlayerRatingPerMT[3][25];
	/* Full kills of AI craft, by the victim's AI rating. */
	int killsFullOnAIRatingPerMT[3][6];
	/* Shared kills of AI craft, by the victim's AI rating. */
	int killsSharedOnAIRatingPerMT[3][6];
	/* Assists on AI craft, by the victim's AI rating. */
	int killsAssistOnAIRatingPerMT[3][6];
	int numSpecialInspectedPerMT[3]; /* Special craft inspected. */
	int energyHitsPerMT[3];		 /* Laser and ion hits. */
	int energyFiredPerMT[3];	 /* Laser and ion shots fired. */
	int warheadsHitsPerMT[3];	 /* Warhead hits. */
	int warheadsFiredPerMT[3];	 /* Warheads fired. */
	int totalCraftLossesPerMT[3];	 /* Craft lost. */
	int lossesByCollisionsPerMT[3];	 /* Craft lost to collisions. */
	int lossesByStarshipsPerMT[3];	 /* Craft lost to starships. */
	int lossesByMinesPerMT[3];	 /* Craft lost to mines. */
	/* Times killed by players, by the killer's rating. */
	int killedByPlayerRatingPerMT[3][25];
	/* Times killed by AI craft, by the killer's AI rating. */
	int killedByAIRatingPerMT[3][6];
} PilotXvtStats;

/* One faction record of the base game's pilot file. The history blocks at the
 * end are copied to and from g_pilotData's PilotFaction starting at the place
 * each comment names: 4 bytes before the array of the same history there. */
typedef struct PilotXvtFaction {
	/* Missions flown; copied with PilotFaction's. */
	int totalMissionsPlayedCount;
	/* Never read or written by name; a save keeps the file's bytes. */
	uint8_t selectionState[68];
	/* Plaque counts; copied, with the three tables after it, as one
	 * 96-byte block to and from PilotFaction.meleePlaques. */
	int meleePlaques[6];
	/* Never read or written by name; copied in meleePlaques' block. */
	int tournamentTrophies[6];
	/* Never read or written by name; copied in meleePlaques' block. */
	int missionEvaluations[6];
	/* Never read or written by name; copied in meleePlaques' block. */
	int battleMedallions[6];
	int missionAwards[4]; /* Copied with PilotFaction's. */
	uint8_t fieldBC[16];  /* Copied with PilotFaction's. */
	int totalScore;	      /* Faction score; copied with PilotFaction's. */
	PilotXvtStats stats;  /* Copied with PilotFaction.stats. */
	/* Single-player training history, at field1558. */
	uint8_t spTrainingData[3600];
	/* Single-player melee history, at spTrainingMissions[99].field20. */
	uint8_t spMeleeData[9000];
	/* Single-player combat history, at spMeleeMissions[249].field20. */
	uint8_t spCombatData[9000];
	/* Multiplayer training history, at spCombatMissions[249].field20. */
	uint8_t mpTrainingData[4800];
	/* Multiplayer melee history, at mpTrainingMissions[99].field2C. */
	uint8_t mpMeleeData[12000];
	/* Multiplayer combat history, at mpMeleeMissions[249].field2C. */
	uint8_t mpCombatData[12000];
	/* Single-player tournament history, at
	 * mpCombatMissions[249].field2C. */
	uint8_t spTournamentData[1000];
	/* Multiplayer tournament history, at spTournaments[24].field24. */
	uint8_t mpTournamentData[1100];
	/* Single-player battle history, at mpTournaments[24].field28. */
	uint8_t spBattleData[900];
	/* Multiplayer battle history, at spBattles[24].field20. */
	uint8_t mpBattleData[1000];
} PilotXvtFaction;

/* The base game's pilot file (.plt), 253,754 bytes. Pilot_LoadXvtRecord copies
 * it into g_pilotData and Pilot_WriteXvtRecord back out; a field is copied to
 * and from PilotData's field of the same name unless its comment says
 * otherwise. */
typedef struct PilotXvtRecord {
	char name[14];	/* Pilot name. */
	int totalScore; /* Total score. */
	/* Local DirectPlay player id, stored when a mission launches. */
	int localPlayerId;
	/* Set to 1 when a mission launches; only the file copies read it. */
	int launchSessionMarker;
	/* Net_IsHost at the last launch from the debriefing, 1 in single
	 * player. */
	int isHost;
	/* Human players in the last mission launched. */
	unsigned int numHumanPlayersLastMission;
	/* g_frontendMissionSessionMode at the last launch. */
	int sessionMode;
	/* Bytes 0 to 319 of PilotData.xvtRecordPayload. */
	uint8_t xvtRecordCombatPayload[320];
	/* Bytes 320 to 351 of PilotData.xvtRecordPayload. */
	uint8_t xvtRecordIdentityPayload[32];
	/* Bytes 352 to 671 of PilotData.xvtRecordPayload. */
	uint8_t xvtRecordObjectPayload[320];
	/* Never read or written by name; a save keeps the file's bytes. */
	uint8_t legacyRatingState[100];
	int currentRatingPromoPoints; /* Points toward the next rank. */
	/* Points from worseRatingPromoPoints toward the next rank. */
	int currentRatingWorsePromoPoints;
	PilotPromotionDelta promotionDelta; /* Last rank change: -1, 0 or 1. */
	int nextPromotionPercent; /* Percent of the way to the next rank. */
	/* Lifetime statistics. A save leaves the first five tables as the
	 * file had them (see Pilot_WriteXvtRecord). */
	PilotXvtStats mainStats;
	/* Never read or written by name; a save keeps the file's bytes. */
	uint8_t missionSequenceState[3348];
	PilotRating rating;	      /* Rank. */
	int totalMissionsPlayedCount; /* Missions flown. */
	/* Mission count at which each rank was reached. */
	int ratingAchievedOnMission[25];
	char ratingName[32]; /* Rank name. */
	int missionScore;    /* Score of the last mission. */
	/* The last mission's kill tables, by player or flight group; zeroed
	 * before each mission by FrontendMission_InitPlayerState. */
	int killsFullOnPlayer[8];
	int killsSharedOnPlayer[8];	    /* As killsFullOnPlayer. */
	int killsFullOnFlightGroup[48];	    /* As killsFullOnPlayer. */
	int killsSharedOnFlightGroup[48];   /* As killsFullOnPlayer. */
	int killsFullFromPlayer[8];	    /* As killsFullOnPlayer. */
	int killsSharedFromPlayer[8];	    /* As killsFullOnPlayer. */
	int killsFullFromFlightGroup[48];   /* As killsFullOnPlayer. */
	int killsSharedFromFlightGroup[48]; /* As killsFullOnPlayer. */
	/* AI rating of each flight group, shown in the debriefing. */
	int flightGroupRating[48];
	PilotXvtStats lastMissionStats;	      /* Last mission's statistics. */
	PilotNetworkPlayer networkPlayers[8]; /* Last mission's players. */
	PilotTeam teams[10];		      /* Last mission's teams. */
	int currentFactionId;		      /* Faction record in use. */
	/* The four faction records, copied field by field with
	 * PilotData.factionStatistics. */
	PilotXvtFaction factionStatistics[4];
} PilotXvtRecord;

#pragma pack(pop)

typedef char xvt_size_PilotXvtStats[(sizeof(PilotXvtStats) == 4824) ? 1 : -1];
typedef char
	xvt_size_PilotXvtFaction[(sizeof(PilotXvtFaction) == 59428) ? 1 : -1];
typedef char
	xvt_size_PilotXvtRecord[(sizeof(PilotXvtRecord) == 0x3DF3A) ? 1 : -1];

/* Reads a whole base game pilot record from stream and copies it into
 * g_pilotData: identity, payloads, promotion state, both stats blocks, rating,
 * the kill tables, network players, teams, current faction and the four faction
 * records. In each per-craft table the record's 88 entries replace
 * g_pilotData's first 88, except entries 4, 36, 41, 43, 45, 54 and 78, which
 * keep g_pilotData's values. Each faction's history blocks go in starting 4
 * bytes before the matching arrays of its PilotFaction. legacyRatingState,
 * missionSequenceState and selectionState are not copied. Returns 1; the modern
 * build returns 0 when the read fails. Does not check the record's size or
 * contents. */
// FUNCTION: XVT 0x4C9F80
int Pilot_LoadXvtRecord(XvtFile *stream)
{
	PilotXvtFaction *sourceFaction;
	PilotFaction *destinationFaction;
	int mainMissionType;
	int missionType;
	int factionId;

	/* g_pilotData's entries for the seven craft the record must not
	 * overwrite, saved before each per-craft table is copied and put
	 * back after. */
	struct PreservedCraftStats {
		int bWing;		/* Entry 4. */
		int superStarDestroyer; /* Entry 54. */
		int modifiedFrigate;	/* Entry 43. */
		int carrackCruiser;	/* Entry 45. */
		int modifiedCorvette;	/* Entry 41. */
		int dreadnaught;	/* Entry 36. */
		int gunEmplacement;	/* Entry 78. */
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
	if (!File_ReadBytes(stream, &record, sizeof(record))) {
		return 0;
	}
#else
	File_ReadBytes(stream, &record, sizeof(record));
#endif
	memcpy(g_pilotData.name, record.name, sizeof(record.name));
	g_pilotData.totalScore = record.totalScore;
	g_pilotData.localPlayerId = record.localPlayerId;
	g_pilotData.launchSessionMarker = record.launchSessionMarker;
	g_pilotData.isHost = record.isHost;
	g_pilotData.numHumanPlayersLastMission =
		record.numHumanPlayersLastMission;
	g_pilotData.sessionMode = record.sessionMode;
	memcpy(g_pilotData.xvtRecordPayload, record.xvtRecordCombatPayload,
	       sizeof(record.xvtRecordCombatPayload));
	memcpy(&g_pilotData.xvtRecordPayload[sizeof(
		       record.xvtRecordCombatPayload)],
	       record.xvtRecordIdentityPayload,
	       sizeof(record.xvtRecordIdentityPayload));
	memcpy(&g_pilotData.xvtRecordPayload
			[sizeof(record.xvtRecordCombatPayload) +
			 sizeof(record.xvtRecordIdentityPayload)],
	       record.xvtRecordObjectPayload,
	       sizeof(record.xvtRecordObjectPayload));
	g_pilotData.currentRatingPromoPoints = record.currentRatingPromoPoints;
	g_pilotData.currentRatingWorsePromoPoints =
		record.currentRatingWorsePromoPoints;
	g_pilotData.promotionDelta = record.promotionDelta;
	g_pilotData.nextPromotionPercent = record.nextPromotionPercent;

	memcpy(g_pilotData.mainStats.totalScorePerMT,
	       record.mainStats.totalScorePerMT,
	       sizeof(record.mainStats.totalScorePerMT));
	memcpy(g_pilotData.mainStats.standaloneMissionsPlayedPerMT,
	       record.mainStats.standaloneMissionsPlayedPerMT,
	       sizeof(record.mainStats.standaloneMissionsPlayedPerMT));
	memcpy(g_pilotData.mainStats.sequenceMissionsPlayedPerMT,
	       record.mainStats.sequenceMissionsPlayedPerMT,
	       sizeof(record.mainStats.sequenceMissionsPlayedPerMT));
	memcpy(g_pilotData.mainStats.totalKillsPerMT,
	       record.mainStats.totalKillsPerMT,
	       sizeof(record.mainStats.totalKillsPerMT));
	memcpy(g_pilotData.mainStats.totalFriendliesKilledPerMT,
	       record.mainStats.totalFriendliesKilledPerMT,
	       sizeof(record.mainStats.totalFriendliesKilledPerMT));
	for (mainMissionType = 0; mainMissionType < MISSION_TYPE_COUNT;
	     ++mainMissionType) {
		preservedCraftStats.bWing =
			g_pilotData.mainStats
				.killsPerCraftPerMT[mainMissionType]
						   [B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.mainStats
				.killsPerCraftPerMT[mainMissionType]
						   [DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.mainStats.killsPerCraftPerMT
				[mainMissionType]
				[MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.mainStats.killsPerCraftPerMT
				[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.mainStats.killsPerCraftPerMT
				[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.mainStats.killsPerCraftPerMT
				[mainMissionType]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.mainStats.killsPerCraftPerMT
				[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.mainStats
			       .killsPerCraftPerMT[mainMissionType],
		       record.mainStats.killsPerCraftPerMT[mainMissionType],
		       sizeof(record.mainStats
				      .killsPerCraftPerMT[mainMissionType]));
		g_pilotData.mainStats.killsPerCraftPerMT[mainMissionType]
							[B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.mainStats
			.killsPerCraftPerMT[mainMissionType]
					   [DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.mainStats
			.killsPerCraftPerMT[mainMissionType]
					   [MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.mainStats
			.killsPerCraftPerMT[mainMissionType]
					   [MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.mainStats
			.killsPerCraftPerMT[mainMissionType]
					   [CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.mainStats
			.killsPerCraftPerMT[mainMissionType]
					   [SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.mainStats
			.killsPerCraftPerMT[mainMissionType]
					   [GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.mainStats
				.killsSharedPerCraftPerMT[mainMissionType]
							 [B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.mainStats.killsSharedPerCraftPerMT
				[mainMissionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.mainStats.killsSharedPerCraftPerMT
				[mainMissionType]
				[MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.mainStats.killsSharedPerCraftPerMT
				[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.mainStats.killsSharedPerCraftPerMT
				[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.mainStats.killsSharedPerCraftPerMT
				[mainMissionType]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.mainStats.killsSharedPerCraftPerMT
				[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.mainStats
			       .killsSharedPerCraftPerMT[mainMissionType],
		       record.mainStats
			       .killsSharedPerCraftPerMT[mainMissionType],
		       sizeof(record.mainStats.killsSharedPerCraftPerMT
				      [mainMissionType]));
		g_pilotData.mainStats
			.killsSharedPerCraftPerMT[mainMissionType]
						 [B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.mainStats
			.killsSharedPerCraftPerMT[mainMissionType]
						 [DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.mainStats.killsSharedPerCraftPerMT
			[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.mainStats.killsSharedPerCraftPerMT
			[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.mainStats
			.killsSharedPerCraftPerMT[mainMissionType]
						 [CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.mainStats.killsSharedPerCraftPerMT
			[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.mainStats
			.killsSharedPerCraftPerMT[mainMissionType]
						 [GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.mainStats
				.killsAssistsPerCraftPerMT[mainMissionType]
							  [B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT
				[mainMissionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT
				[mainMissionType]
				[MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT
				[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT
				[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT
				[mainMissionType]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.mainStats.killsAssistsPerCraftPerMT
				[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.mainStats
			       .killsAssistsPerCraftPerMT[mainMissionType],
		       record.mainStats
			       .killsAssistsPerCraftPerMT[mainMissionType],
		       sizeof(record.mainStats.killsAssistsPerCraftPerMT
				      [mainMissionType]));
		g_pilotData.mainStats
			.killsAssistsPerCraftPerMT[mainMissionType]
						  [B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.mainStats
			.killsAssistsPerCraftPerMT[mainMissionType]
						  [DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT
			[mainMissionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT
			[mainMissionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT
			[mainMissionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT
			[mainMissionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.mainStats.killsAssistsPerCraftPerMT
			[mainMissionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;
	}

	memcpy(g_pilotData.mainStats.killsFullOnPlayerRatingPerMT,
	       record.mainStats.killsFullOnPlayerRatingPerMT,
	       sizeof(record.mainStats.killsFullOnPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killsSharedOnPlayerRatingPerMT,
	       record.mainStats.killsSharedOnPlayerRatingPerMT,
	       sizeof(record.mainStats.killsSharedOnPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killsAssistOnPlayerRatingPerMT,
	       record.mainStats.killsAssistOnPlayerRatingPerMT,
	       sizeof(record.mainStats.killsAssistOnPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killsFullOnAIRatingPerMT,
	       record.mainStats.killsFullOnAIRatingPerMT,
	       sizeof(record.mainStats.killsFullOnAIRatingPerMT));
	memcpy(g_pilotData.mainStats.killsSharedOnAIRatingPerMT,
	       record.mainStats.killsSharedOnAIRatingPerMT,
	       sizeof(record.mainStats.killsSharedOnAIRatingPerMT));
	memcpy(g_pilotData.mainStats.killsAssistOnAIRatingPerMT,
	       record.mainStats.killsAssistOnAIRatingPerMT,
	       sizeof(record.mainStats.killsAssistOnAIRatingPerMT));
	memcpy(g_pilotData.mainStats.numSpecialInspectedPerMT,
	       record.mainStats.numSpecialInspectedPerMT,
	       sizeof(record.mainStats.numSpecialInspectedPerMT));
	memcpy(g_pilotData.mainStats.energyHitsPerMT,
	       record.mainStats.energyHitsPerMT,
	       sizeof(record.mainStats.energyHitsPerMT));
	memcpy(g_pilotData.mainStats.energyFiredPerMT,
	       record.mainStats.energyFiredPerMT,
	       sizeof(record.mainStats.energyFiredPerMT));
	memcpy(g_pilotData.mainStats.warheadsHitsPerMT,
	       record.mainStats.warheadsHitsPerMT,
	       sizeof(record.mainStats.warheadsHitsPerMT));
	memcpy(g_pilotData.mainStats.warheadsFiredPerMT,
	       record.mainStats.warheadsFiredPerMT,
	       sizeof(record.mainStats.warheadsFiredPerMT));
	memcpy(g_pilotData.mainStats.totalCraftLossesPerMT,
	       record.mainStats.totalCraftLossesPerMT,
	       sizeof(record.mainStats.totalCraftLossesPerMT));
	memcpy(g_pilotData.mainStats.lossesByCollisionsPerMT,
	       record.mainStats.lossesByCollisionsPerMT,
	       sizeof(record.mainStats.lossesByCollisionsPerMT));
	memcpy(g_pilotData.mainStats.lossesByStarshipsPerMT,
	       record.mainStats.lossesByStarshipsPerMT,
	       sizeof(record.mainStats.lossesByStarshipsPerMT));
	memcpy(g_pilotData.mainStats.lossesByMinesPerMT,
	       record.mainStats.lossesByMinesPerMT,
	       sizeof(record.mainStats.lossesByMinesPerMT));
	memcpy(g_pilotData.mainStats.killedByPlayerRatingPerMT,
	       record.mainStats.killedByPlayerRatingPerMT,
	       sizeof(record.mainStats.killedByPlayerRatingPerMT));
	memcpy(g_pilotData.mainStats.killedByAIRatingPerMT,
	       record.mainStats.killedByAIRatingPerMT,
	       sizeof(record.mainStats.killedByAIRatingPerMT));

	g_pilotData.rating = record.rating;
	g_pilotData.totalMissionsPlayedCount = record.totalMissionsPlayedCount;
	memcpy(g_pilotData.ratingAchievedOnMission,
	       record.ratingAchievedOnMission,
	       sizeof(record.ratingAchievedOnMission));
	memcpy(g_pilotData.ratingName, record.ratingName,
	       sizeof(record.ratingName));
	g_pilotData.missionScore = record.missionScore;
	memcpy(g_pilotData.killsFullOnPlayer, record.killsFullOnPlayer,
	       sizeof(record.killsFullOnPlayer));
	memcpy(g_pilotData.killsSharedOnPlayer, record.killsSharedOnPlayer,
	       sizeof(record.killsSharedOnPlayer));
	memcpy(g_pilotData.killsFullOnFlightGroup,
	       record.killsFullOnFlightGroup,
	       sizeof(record.killsFullOnFlightGroup));
	memcpy(g_pilotData.killsSharedOnFlightGroup,
	       record.killsSharedOnFlightGroup,
	       sizeof(record.killsSharedOnFlightGroup));
	memcpy(g_pilotData.killsFullFromPlayer, record.killsFullFromPlayer,
	       sizeof(record.killsFullFromPlayer));
	memcpy(g_pilotData.killsSharedFromPlayer, record.killsSharedFromPlayer,
	       sizeof(record.killsSharedFromPlayer));
	memcpy(g_pilotData.killsFullFromFlightGroup,
	       record.killsFullFromFlightGroup,
	       sizeof(record.killsFullFromFlightGroup));
	memcpy(g_pilotData.killsSharedFromFlightGroup,
	       record.killsSharedFromFlightGroup,
	       sizeof(record.killsSharedFromFlightGroup));
	memcpy(g_pilotData.flightGroupRating, record.flightGroupRating,
	       sizeof(record.flightGroupRating));

	memcpy(g_pilotData.lastMissionStats.totalScorePerMT,
	       record.lastMissionStats.totalScorePerMT,
	       sizeof(record.lastMissionStats.totalScorePerMT));
	memcpy(g_pilotData.lastMissionStats.standaloneMissionsPlayedPerMT,
	       record.lastMissionStats.standaloneMissionsPlayedPerMT,
	       sizeof(record.lastMissionStats.standaloneMissionsPlayedPerMT));
	memcpy(g_pilotData.lastMissionStats.sequenceMissionsPlayedPerMT,
	       record.lastMissionStats.sequenceMissionsPlayedPerMT,
	       sizeof(record.lastMissionStats.sequenceMissionsPlayedPerMT));
	memcpy(g_pilotData.lastMissionStats.totalKillsPerMT,
	       record.lastMissionStats.totalKillsPerMT,
	       sizeof(record.lastMissionStats.totalKillsPerMT));
	memcpy(g_pilotData.lastMissionStats.totalFriendliesKilledPerMT,
	       record.lastMissionStats.totalFriendliesKilledPerMT,
	       sizeof(record.lastMissionStats.totalFriendliesKilledPerMT));
	for (missionType = 0; missionType < MISSION_TYPE_COUNT; ++missionType) {
		preservedCraftStats.bWing =
			g_pilotData.lastMissionStats
				.killsPerCraftPerMT[missionType]
						   [B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.lastMissionStats
				.killsPerCraftPerMT[missionType]
						   [DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.lastMissionStats.killsPerCraftPerMT
				[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.lastMissionStats.killsPerCraftPerMT
				[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.lastMissionStats.killsPerCraftPerMT
				[missionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.lastMissionStats.killsPerCraftPerMT
				[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.lastMissionStats.killsPerCraftPerMT
				[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.lastMissionStats
			       .killsPerCraftPerMT[missionType],
		       record.lastMissionStats.killsPerCraftPerMT[missionType],
		       sizeof(record.lastMissionStats
				      .killsPerCraftPerMT[missionType]));
		g_pilotData.lastMissionStats
			.killsPerCraftPerMT[missionType][B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.lastMissionStats
			.killsPerCraftPerMT[missionType]
					   [DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.lastMissionStats
			.killsPerCraftPerMT[missionType]
					   [MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.lastMissionStats
			.killsPerCraftPerMT[missionType]
					   [MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.lastMissionStats
			.killsPerCraftPerMT[missionType]
					   [CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.lastMissionStats
			.killsPerCraftPerMT[missionType]
					   [SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.lastMissionStats
			.killsPerCraftPerMT[missionType]
					   [GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.lastMissionStats
				.killsSharedPerCraftPerMT[missionType]
							 [B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
				[missionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
				[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
				[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
				[missionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
				[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
				[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.lastMissionStats
			       .killsSharedPerCraftPerMT[missionType],
		       record.lastMissionStats
			       .killsSharedPerCraftPerMT[missionType],
		       sizeof(record.lastMissionStats
				      .killsSharedPerCraftPerMT[missionType]));
		g_pilotData.lastMissionStats
			.killsSharedPerCraftPerMT[missionType]
						 [B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.lastMissionStats
			.killsSharedPerCraftPerMT[missionType]
						 [DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
			[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
			[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.lastMissionStats
			.killsSharedPerCraftPerMT[missionType]
						 [CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.lastMissionStats.killsSharedPerCraftPerMT
			[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.lastMissionStats
			.killsSharedPerCraftPerMT[missionType]
						 [GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;

		preservedCraftStats.bWing =
			g_pilotData.lastMissionStats
				.killsAssistsPerCraftPerMT[missionType]
							  [B_WING_CRAFT_INDEX];
		preservedCraftStats.dreadnaught =
			g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
				[missionType][DREADNAUGHT_CRAFT_INDEX];
		preservedCraftStats.modifiedCorvette =
			g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
				[missionType][MODIFIED_CORVETTE_CRAFT_INDEX];
		preservedCraftStats.modifiedFrigate =
			g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
				[missionType][MODIFIED_FRIGATE_CRAFT_INDEX];
		preservedCraftStats.carrackCruiser =
			g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
				[missionType][CARRACK_CRUISER_CRAFT_INDEX];
		preservedCraftStats.superStarDestroyer =
			g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
				[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preservedCraftStats.gunEmplacement =
			g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
				[missionType][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilotData.lastMissionStats
			       .killsAssistsPerCraftPerMT[missionType],
		       record.lastMissionStats
			       .killsAssistsPerCraftPerMT[missionType],
		       sizeof(record.lastMissionStats
				      .killsAssistsPerCraftPerMT[missionType]));
		g_pilotData.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType]
						  [B_WING_CRAFT_INDEX] =
			preservedCraftStats.bWing;
		g_pilotData.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType]
						  [DREADNAUGHT_CRAFT_INDEX] =
			preservedCraftStats.dreadnaught;
		g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
			[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preservedCraftStats.modifiedCorvette;
		g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
			[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preservedCraftStats.modifiedFrigate;
		g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
			[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
			preservedCraftStats.carrackCruiser;
		g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
			[missionType][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preservedCraftStats.superStarDestroyer;
		g_pilotData.lastMissionStats.killsAssistsPerCraftPerMT
			[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preservedCraftStats.gunEmplacement;
	}

	memcpy(g_pilotData.lastMissionStats.killsFullOnPlayerRatingPerMT,
	       record.lastMissionStats.killsFullOnPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killsFullOnPlayerRatingPerMT));
	memcpy(g_pilotData.lastMissionStats.killsSharedOnPlayerRatingPerMT,
	       record.lastMissionStats.killsSharedOnPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killsSharedOnPlayerRatingPerMT));
	memcpy(g_pilotData.lastMissionStats.killsAssistOnPlayerRatingPerMT,
	       record.lastMissionStats.killsAssistOnPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killsAssistOnPlayerRatingPerMT));
	memcpy(g_pilotData.lastMissionStats.killsFullOnAIRatingPerMT,
	       record.lastMissionStats.killsFullOnAIRatingPerMT,
	       sizeof(record.lastMissionStats.killsFullOnAIRatingPerMT));
	memcpy(g_pilotData.lastMissionStats.killsSharedOnAIRatingPerMT,
	       record.lastMissionStats.killsSharedOnAIRatingPerMT,
	       sizeof(record.lastMissionStats.killsSharedOnAIRatingPerMT));
	memcpy(g_pilotData.lastMissionStats.killsAssistOnAIRatingPerMT,
	       record.lastMissionStats.killsAssistOnAIRatingPerMT,
	       sizeof(record.lastMissionStats.killsAssistOnAIRatingPerMT));
	memcpy(g_pilotData.lastMissionStats.numSpecialInspectedPerMT,
	       record.lastMissionStats.numSpecialInspectedPerMT,
	       sizeof(record.lastMissionStats.numSpecialInspectedPerMT));
	memcpy(g_pilotData.lastMissionStats.energyHitsPerMT,
	       record.lastMissionStats.energyHitsPerMT,
	       sizeof(record.lastMissionStats.energyHitsPerMT));
	memcpy(g_pilotData.lastMissionStats.energyFiredPerMT,
	       record.lastMissionStats.energyFiredPerMT,
	       sizeof(record.lastMissionStats.energyFiredPerMT));
	memcpy(g_pilotData.lastMissionStats.warheadsHitsPerMT,
	       record.lastMissionStats.warheadsHitsPerMT,
	       sizeof(record.lastMissionStats.warheadsHitsPerMT));
	memcpy(g_pilotData.lastMissionStats.warheadsFiredPerMT,
	       record.lastMissionStats.warheadsFiredPerMT,
	       sizeof(record.lastMissionStats.warheadsFiredPerMT));
	memcpy(g_pilotData.lastMissionStats.totalCraftLossesPerMT,
	       record.lastMissionStats.totalCraftLossesPerMT,
	       sizeof(record.lastMissionStats.totalCraftLossesPerMT));
	memcpy(g_pilotData.lastMissionStats.lossesByCollisionsPerMT,
	       record.lastMissionStats.lossesByCollisionsPerMT,
	       sizeof(record.lastMissionStats.lossesByCollisionsPerMT));
	memcpy(g_pilotData.lastMissionStats.lossesByStarshipsPerMT,
	       record.lastMissionStats.lossesByStarshipsPerMT,
	       sizeof(record.lastMissionStats.lossesByStarshipsPerMT));
	memcpy(g_pilotData.lastMissionStats.lossesByMinesPerMT,
	       record.lastMissionStats.lossesByMinesPerMT,
	       sizeof(record.lastMissionStats.lossesByMinesPerMT));
	memcpy(g_pilotData.lastMissionStats.killedByPlayerRatingPerMT,
	       record.lastMissionStats.killedByPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killedByPlayerRatingPerMT));
	memcpy(g_pilotData.lastMissionStats.killedByAIRatingPerMT,
	       record.lastMissionStats.killedByAIRatingPerMT,
	       sizeof(record.lastMissionStats.killedByAIRatingPerMT));
	memcpy(g_pilotData.networkPlayers, record.networkPlayers,
	       sizeof(record.networkPlayers));
	memcpy(g_pilotData.teams, record.teams, sizeof(record.teams));
	g_pilotData.currentFactionId = record.currentFactionId;

	for (factionId = 0; factionId < FACTION_COUNT; ++factionId) {
		destinationFaction = &g_pilotData.factionStatistics[factionId];
		sourceFaction = &record.factionStatistics[factionId];
		destinationFaction->totalMissionsPlayedCount =
			sourceFaction->totalMissionsPlayedCount;
		memcpy(destinationFaction->meleePlaques,
		       sourceFaction->meleePlaques,
		       sizeof(sourceFaction->meleePlaques) +
			       sizeof(sourceFaction->tournamentTrophies) +
			       sizeof(sourceFaction->missionEvaluations) +
			       sizeof(sourceFaction->battleMedallions));
		memcpy(destinationFaction->missionAwards,
		       sourceFaction->missionAwards,
		       sizeof(sourceFaction->missionAwards));
		memcpy(destinationFaction->fieldBC, sourceFaction->fieldBC,
		       sizeof(sourceFaction->fieldBC));
		destinationFaction->totalScore = sourceFaction->totalScore;
		memcpy(destinationFaction->stats.totalScorePerMT,
		       sourceFaction->stats.totalScorePerMT,
		       sizeof(sourceFaction->stats.totalScorePerMT));
		memcpy(destinationFaction->stats.standaloneMissionsPlayedPerMT,
		       sourceFaction->stats.standaloneMissionsPlayedPerMT,
		       sizeof(sourceFaction->stats
				      .standaloneMissionsPlayedPerMT));
		memcpy(destinationFaction->stats.sequenceMissionsPlayedPerMT,
		       sourceFaction->stats.sequenceMissionsPlayedPerMT,
		       sizeof(sourceFaction->stats
				      .sequenceMissionsPlayedPerMT));
		memcpy(destinationFaction->stats.totalKillsPerMT,
		       sourceFaction->stats.totalKillsPerMT,
		       sizeof(sourceFaction->stats.totalKillsPerMT));
		memcpy(destinationFaction->stats.totalFriendliesKilledPerMT,
		       sourceFaction->stats.totalFriendliesKilledPerMT,
		       sizeof(sourceFaction->stats.totalFriendliesKilledPerMT));

		for (missionType = 0; missionType < MISSION_TYPE_COUNT;
		     ++missionType) {
			preservedCraftStats.bWing =
				destinationFaction->stats
					.killsPerCraftPerMT[missionType]
							   [B_WING_CRAFT_INDEX];
			preservedCraftStats.dreadnaught =
				destinationFaction->stats.killsPerCraftPerMT
					[missionType][DREADNAUGHT_CRAFT_INDEX];
			preservedCraftStats.modifiedCorvette =
				destinationFaction->stats.killsPerCraftPerMT
					[missionType]
					[MODIFIED_CORVETTE_CRAFT_INDEX];
			preservedCraftStats.modifiedFrigate =
				destinationFaction->stats.killsPerCraftPerMT
					[missionType]
					[MODIFIED_FRIGATE_CRAFT_INDEX];
			preservedCraftStats.carrackCruiser =
				destinationFaction->stats.killsPerCraftPerMT
					[missionType]
					[CARRACK_CRUISER_CRAFT_INDEX];
			preservedCraftStats.superStarDestroyer =
				destinationFaction->stats.killsPerCraftPerMT
					[missionType]
					[SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preservedCraftStats.gunEmplacement =
				destinationFaction->stats.killsPerCraftPerMT
					[missionType]
					[GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destinationFaction->stats
				       .killsPerCraftPerMT[missionType],
			       sourceFaction->stats
				       .killsPerCraftPerMT[missionType],
			       sizeof(sourceFaction->stats.killsPerCraftPerMT
					      [missionType]));
			destinationFaction->stats
				.killsPerCraftPerMT[missionType]
						   [B_WING_CRAFT_INDEX] =
				preservedCraftStats.bWing;
			destinationFaction->stats
				.killsPerCraftPerMT[missionType]
						   [DREADNAUGHT_CRAFT_INDEX] =
				preservedCraftStats.dreadnaught;
			destinationFaction->stats.killsPerCraftPerMT
				[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preservedCraftStats.modifiedCorvette;
			destinationFaction->stats.killsPerCraftPerMT
				[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preservedCraftStats.modifiedFrigate;
			destinationFaction->stats.killsPerCraftPerMT
				[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
				preservedCraftStats.carrackCruiser;
			destinationFaction->stats.killsPerCraftPerMT
				[missionType]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preservedCraftStats.superStarDestroyer;
			destinationFaction->stats.killsPerCraftPerMT
				[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preservedCraftStats.gunEmplacement;

			preservedCraftStats.bWing =
				destinationFaction->stats
					.killsSharedPerCraftPerMT
						[missionType]
						[B_WING_CRAFT_INDEX];
			preservedCraftStats.dreadnaught =
				destinationFaction->stats
					.killsSharedPerCraftPerMT
						[missionType]
						[DREADNAUGHT_CRAFT_INDEX];
			preservedCraftStats.modifiedCorvette =
				destinationFaction->stats
					.killsSharedPerCraftPerMT
						[missionType]
						[MODIFIED_CORVETTE_CRAFT_INDEX];
			preservedCraftStats.modifiedFrigate =
				destinationFaction->stats
					.killsSharedPerCraftPerMT
						[missionType]
						[MODIFIED_FRIGATE_CRAFT_INDEX];
			preservedCraftStats.carrackCruiser =
				destinationFaction->stats
					.killsSharedPerCraftPerMT
						[missionType]
						[CARRACK_CRUISER_CRAFT_INDEX];
			preservedCraftStats.superStarDestroyer =
				destinationFaction->stats.killsSharedPerCraftPerMT
					[missionType]
					[SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preservedCraftStats.gunEmplacement =
				destinationFaction->stats
					.killsSharedPerCraftPerMT
						[missionType]
						[GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destinationFaction->stats
				       .killsSharedPerCraftPerMT[missionType],
			       sourceFaction->stats
				       .killsSharedPerCraftPerMT[missionType],
			       sizeof(sourceFaction->stats
					      .killsSharedPerCraftPerMT
						      [missionType]));
			destinationFaction->stats
				.killsSharedPerCraftPerMT[missionType]
							 [B_WING_CRAFT_INDEX] =
				preservedCraftStats.bWing;
			destinationFaction->stats.killsSharedPerCraftPerMT
				[missionType][DREADNAUGHT_CRAFT_INDEX] =
				preservedCraftStats.dreadnaught;
			destinationFaction->stats.killsSharedPerCraftPerMT
				[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preservedCraftStats.modifiedCorvette;
			destinationFaction->stats.killsSharedPerCraftPerMT
				[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preservedCraftStats.modifiedFrigate;
			destinationFaction->stats.killsSharedPerCraftPerMT
				[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
				preservedCraftStats.carrackCruiser;
			destinationFaction->stats.killsSharedPerCraftPerMT
				[missionType]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preservedCraftStats.superStarDestroyer;
			destinationFaction->stats.killsSharedPerCraftPerMT
				[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preservedCraftStats.gunEmplacement;

			preservedCraftStats.bWing =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT
						[missionType]
						[B_WING_CRAFT_INDEX];
			preservedCraftStats.dreadnaught =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT
						[missionType]
						[DREADNAUGHT_CRAFT_INDEX];
			preservedCraftStats.modifiedCorvette =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT
						[missionType]
						[MODIFIED_CORVETTE_CRAFT_INDEX];
			preservedCraftStats.modifiedFrigate =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT
						[missionType]
						[MODIFIED_FRIGATE_CRAFT_INDEX];
			preservedCraftStats.carrackCruiser =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT
						[missionType]
						[CARRACK_CRUISER_CRAFT_INDEX];
			preservedCraftStats.superStarDestroyer =
				destinationFaction->stats.killsAssistsPerCraftPerMT
					[missionType]
					[SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preservedCraftStats.gunEmplacement =
				destinationFaction->stats
					.killsAssistsPerCraftPerMT
						[missionType]
						[GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destinationFaction->stats
				       .killsAssistsPerCraftPerMT[missionType],
			       sourceFaction->stats
				       .killsAssistsPerCraftPerMT[missionType],
			       sizeof(sourceFaction->stats
					      .killsAssistsPerCraftPerMT
						      [missionType]));
			destinationFaction->stats
				.killsAssistsPerCraftPerMT[missionType]
							  [B_WING_CRAFT_INDEX] =
				preservedCraftStats.bWing;
			destinationFaction->stats.killsAssistsPerCraftPerMT
				[missionType][DREADNAUGHT_CRAFT_INDEX] =
				preservedCraftStats.dreadnaught;
			destinationFaction->stats.killsAssistsPerCraftPerMT
				[missionType][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preservedCraftStats.modifiedCorvette;
			destinationFaction->stats.killsAssistsPerCraftPerMT
				[missionType][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preservedCraftStats.modifiedFrigate;
			destinationFaction->stats.killsAssistsPerCraftPerMT
				[missionType][CARRACK_CRUISER_CRAFT_INDEX] =
				preservedCraftStats.carrackCruiser;
			destinationFaction->stats.killsAssistsPerCraftPerMT
				[missionType]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preservedCraftStats.superStarDestroyer;
			destinationFaction->stats.killsAssistsPerCraftPerMT
				[missionType][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preservedCraftStats.gunEmplacement;
		}

		memcpy(destinationFaction->stats.killsFullOnPlayerRatingPerMT,
		       sourceFaction->stats.killsFullOnPlayerRatingPerMT,
		       sizeof(sourceFaction->stats
				      .killsFullOnPlayerRatingPerMT));
		memcpy(destinationFaction->stats.killsSharedOnPlayerRatingPerMT,
		       sourceFaction->stats.killsSharedOnPlayerRatingPerMT,
		       sizeof(sourceFaction->stats
				      .killsSharedOnPlayerRatingPerMT));
		memcpy(destinationFaction->stats.killsAssistOnPlayerRatingPerMT,
		       sourceFaction->stats.killsAssistOnPlayerRatingPerMT,
		       sizeof(sourceFaction->stats
				      .killsAssistOnPlayerRatingPerMT));
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
		memcpy(destinationFaction->stats.energyHitsPerMT,
		       sourceFaction->stats.energyHitsPerMT,
		       sizeof(sourceFaction->stats.energyHitsPerMT));
		memcpy(destinationFaction->stats.energyFiredPerMT,
		       sourceFaction->stats.energyFiredPerMT,
		       sizeof(sourceFaction->stats.energyFiredPerMT));
		memcpy(destinationFaction->stats.warheadsHitsPerMT,
		       sourceFaction->stats.warheadsHitsPerMT,
		       sizeof(sourceFaction->stats.warheadsHitsPerMT));
		memcpy(destinationFaction->stats.warheadsFiredPerMT,
		       sourceFaction->stats.warheadsFiredPerMT,
		       sizeof(sourceFaction->stats.warheadsFiredPerMT));
		memcpy(destinationFaction->stats.totalCraftLossesPerMT,
		       sourceFaction->stats.totalCraftLossesPerMT,
		       sizeof(sourceFaction->stats.totalCraftLossesPerMT));
		memcpy(destinationFaction->stats.lossesByCollisionsPerMT,
		       sourceFaction->stats.lossesByCollisionsPerMT,
		       sizeof(sourceFaction->stats.lossesByCollisionsPerMT));
		memcpy(destinationFaction->stats.lossesByStarshipsPerMT,
		       sourceFaction->stats.lossesByStarshipsPerMT,
		       sizeof(sourceFaction->stats.lossesByStarshipsPerMT));
		memcpy(destinationFaction->stats.lossesByMinesPerMT,
		       sourceFaction->stats.lossesByMinesPerMT,
		       sizeof(sourceFaction->stats.lossesByMinesPerMT));
		memcpy(destinationFaction->stats.killedByPlayerRatingPerMT,
		       sourceFaction->stats.killedByPlayerRatingPerMT,
		       sizeof(sourceFaction->stats.killedByPlayerRatingPerMT));
		memcpy(destinationFaction->stats.killedByAIRatingPerMT,
		       sourceFaction->stats.killedByAIRatingPerMT,
		       sizeof(sourceFaction->stats.killedByAIRatingPerMT));
		memcpy(destinationFaction->field1558,
		       sourceFaction->spTrainingData,
		       sizeof(sourceFaction->spTrainingData));
		memcpy(&destinationFaction->spTrainingMissions[99].field20,
		       sourceFaction->spMeleeData,
		       sizeof(sourceFaction->spMeleeData));
		memcpy(&destinationFaction->spMeleeMissions[249].field20,
		       sourceFaction->spCombatData,
		       sizeof(sourceFaction->spCombatData));
		memcpy(&destinationFaction->spCombatMissions[249].field20,
		       sourceFaction->mpTrainingData,
		       sizeof(sourceFaction->mpTrainingData));
		memcpy(&destinationFaction->mpTrainingMissions[99].field2C,
		       sourceFaction->mpMeleeData,
		       sizeof(sourceFaction->mpMeleeData));
		memcpy(&destinationFaction->mpMeleeMissions[249].field2C,
		       sourceFaction->mpCombatData,
		       sizeof(sourceFaction->mpCombatData));
		memcpy(&destinationFaction->mpCombatMissions[249].field2C,
		       sourceFaction->spTournamentData,
		       sizeof(sourceFaction->spTournamentData));
		memcpy(&destinationFaction->spTournaments[24].field24,
		       sourceFaction->mpTournamentData,
		       sizeof(sourceFaction->mpTournamentData));
		memcpy(&destinationFaction->mpTournaments[24].field28,
		       sourceFaction->spBattleData,
		       sizeof(sourceFaction->spBattleData));
		memcpy(&destinationFaction->spBattles[24].field20,
		       sourceFaction->mpBattleData,
		       sizeof(sourceFaction->mpBattleData));
	}

	return 1;
}

/* Updates the base game pilot record fileName, in the base game folder, from
 * g_pilotData: reads the existing record when it opens, else starts from zeros,
 * copies in the same fields Pilot_LoadXvtRecord copies out, and writes it back.
 * Per-craft entries 4, 36, 41, 43, 45, 54 and 78 are written as 0. The five
 * per-mission-type totals at the start of mainStats are copied from the record
 * onto themselves, so they keep the old file's values. The original build opens
 * the file itself, ignoring stream, writes without checking the open, and
 * returns 1; the modern build ignores stream and returns the result of writing
 * the file whole. */
// FUNCTION: XVT 0x4CB310
int Pilot_WriteXvtRecord(const char *fileName, XvtFile *stream)
{
	PilotXvtRecord record;
	XvtFile *inputStream;
	int missionType;
	int factionId;

	memset(&record, 0, sizeof(record));
	File_ChangeToBaseGameInstallPath();
	inputStream = File_Open(fileName, g_fileModeReadBinary);
	if (inputStream != NULL) {
#ifdef XVT_MODERN
		if (!File_ReadBytes(inputStream, &record, sizeof(record))) {
			File_Close(inputStream);
			return 0;
		}
#else
		File_ReadBytes(inputStream, &record, sizeof(record));
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
	record.numHumanPlayersLastMission =
		g_pilotData.numHumanPlayersLastMission;
	record.sessionMode = g_pilotData.sessionMode;
	memcpy(record.xvtRecordCombatPayload, g_pilotData.xvtRecordPayload,
	       sizeof(record.xvtRecordCombatPayload));
	memcpy(record.xvtRecordIdentityPayload,
	       &g_pilotData.xvtRecordPayload[320],
	       sizeof(record.xvtRecordIdentityPayload));
	memcpy(record.xvtRecordObjectPayload,
	       &g_pilotData.xvtRecordPayload[352],
	       sizeof(record.xvtRecordObjectPayload));
	record.currentRatingPromoPoints = g_pilotData.currentRatingPromoPoints;
	record.currentRatingWorsePromoPoints =
		g_pilotData.currentRatingWorsePromoPoints;
	record.promotionDelta = g_pilotData.promotionDelta;
	record.nextPromotionPercent = g_pilotData.nextPromotionPercent;

	memcpy(record.mainStats.totalScorePerMT,
	       record.mainStats.totalScorePerMT,
	       sizeof(record.mainStats.totalScorePerMT));
	memcpy(record.mainStats.standaloneMissionsPlayedPerMT,
	       record.mainStats.standaloneMissionsPlayedPerMT,
	       sizeof(record.mainStats.standaloneMissionsPlayedPerMT));
	memcpy(record.mainStats.sequenceMissionsPlayedPerMT,
	       record.mainStats.sequenceMissionsPlayedPerMT,
	       sizeof(record.mainStats.sequenceMissionsPlayedPerMT));
	memcpy(record.mainStats.totalKillsPerMT,
	       record.mainStats.totalKillsPerMT,
	       sizeof(record.mainStats.totalKillsPerMT));
	memcpy(record.mainStats.totalFriendliesKilledPerMT,
	       record.mainStats.totalFriendliesKilledPerMT,
	       sizeof(record.mainStats.totalFriendliesKilledPerMT));

	for (missionType = 0; missionType < 3; ++missionType) {
		memcpy(record.mainStats.killsPerCraftPerMT[missionType],
		       g_pilotData.mainStats.killsPerCraftPerMT[missionType],
		       sizeof(record.mainStats
				      .killsPerCraftPerMT[missionType]));
		memcpy(record.mainStats.killsSharedPerCraftPerMT[missionType],
		       g_pilotData.mainStats
			       .killsSharedPerCraftPerMT[missionType],
		       sizeof(record.mainStats
				      .killsSharedPerCraftPerMT[missionType]));
		memcpy(record.mainStats.killsAssistsPerCraftPerMT[missionType],
		       g_pilotData.mainStats
			       .killsAssistsPerCraftPerMT[missionType],
		       sizeof(record.mainStats
				      .killsAssistsPerCraftPerMT[missionType]));
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
	memcpy(record.mainStats.killsFullOnPlayerRatingPerMT,
	       g_pilotData.mainStats.killsFullOnPlayerRatingPerMT,
	       sizeof(record.mainStats.killsFullOnPlayerRatingPerMT));
	memcpy(record.mainStats.killsSharedOnPlayerRatingPerMT,
	       g_pilotData.mainStats.killsSharedOnPlayerRatingPerMT,
	       sizeof(record.mainStats.killsSharedOnPlayerRatingPerMT));
	memcpy(record.mainStats.killsAssistOnPlayerRatingPerMT,
	       g_pilotData.mainStats.killsAssistOnPlayerRatingPerMT,
	       sizeof(record.mainStats.killsAssistOnPlayerRatingPerMT));
	memcpy(record.mainStats.killsFullOnAIRatingPerMT,
	       g_pilotData.mainStats.killsFullOnAIRatingPerMT,
	       sizeof(record.mainStats.killsFullOnAIRatingPerMT));
	memcpy(record.mainStats.killsSharedOnAIRatingPerMT,
	       g_pilotData.mainStats.killsSharedOnAIRatingPerMT,
	       sizeof(record.mainStats.killsSharedOnAIRatingPerMT));
	memcpy(record.mainStats.killsAssistOnAIRatingPerMT,
	       g_pilotData.mainStats.killsAssistOnAIRatingPerMT,
	       sizeof(record.mainStats.killsAssistOnAIRatingPerMT));
	memcpy(record.mainStats.numSpecialInspectedPerMT,
	       g_pilotData.mainStats.numSpecialInspectedPerMT,
	       sizeof(record.mainStats.numSpecialInspectedPerMT));
	memcpy(record.mainStats.energyHitsPerMT,
	       g_pilotData.mainStats.energyHitsPerMT,
	       sizeof(record.mainStats.energyHitsPerMT));
	memcpy(record.mainStats.energyFiredPerMT,
	       g_pilotData.mainStats.energyFiredPerMT,
	       sizeof(record.mainStats.energyFiredPerMT));
	memcpy(record.mainStats.warheadsHitsPerMT,
	       g_pilotData.mainStats.warheadsHitsPerMT,
	       sizeof(record.mainStats.warheadsHitsPerMT));
	memcpy(record.mainStats.warheadsFiredPerMT,
	       g_pilotData.mainStats.warheadsFiredPerMT,
	       sizeof(record.mainStats.warheadsFiredPerMT));
	memcpy(record.mainStats.totalCraftLossesPerMT,
	       g_pilotData.mainStats.totalCraftLossesPerMT,
	       sizeof(record.mainStats.totalCraftLossesPerMT));
	memcpy(record.mainStats.lossesByCollisionsPerMT,
	       g_pilotData.mainStats.lossesByCollisionsPerMT,
	       sizeof(record.mainStats.lossesByCollisionsPerMT));
	memcpy(record.mainStats.lossesByStarshipsPerMT,
	       g_pilotData.mainStats.lossesByStarshipsPerMT,
	       sizeof(record.mainStats.lossesByStarshipsPerMT));
	memcpy(record.mainStats.lossesByMinesPerMT,
	       g_pilotData.mainStats.lossesByMinesPerMT,
	       sizeof(record.mainStats.lossesByMinesPerMT));
	memcpy(record.mainStats.killedByPlayerRatingPerMT,
	       g_pilotData.mainStats.killedByPlayerRatingPerMT,
	       sizeof(record.mainStats.killedByPlayerRatingPerMT));
	memcpy(record.mainStats.killedByAIRatingPerMT,
	       g_pilotData.mainStats.killedByAIRatingPerMT,
	       sizeof(record.mainStats.killedByAIRatingPerMT));

	record.rating = g_pilotData.rating;
	record.totalMissionsPlayedCount = g_pilotData.totalMissionsPlayedCount;
	memcpy(record.ratingAchievedOnMission,
	       g_pilotData.ratingAchievedOnMission,
	       sizeof(record.ratingAchievedOnMission));
	memcpy(record.ratingName, g_pilotData.ratingName,
	       sizeof(record.ratingName));
	record.missionScore = g_pilotData.missionScore;
	memcpy(record.killsFullOnPlayer, g_pilotData.killsFullOnPlayer,
	       sizeof(record.killsFullOnPlayer));
	memcpy(record.killsSharedOnPlayer, g_pilotData.killsSharedOnPlayer,
	       sizeof(record.killsSharedOnPlayer));
	memcpy(record.killsFullOnFlightGroup,
	       g_pilotData.killsFullOnFlightGroup,
	       sizeof(record.killsFullOnFlightGroup));
	memcpy(record.killsSharedOnFlightGroup,
	       g_pilotData.killsSharedOnFlightGroup,
	       sizeof(record.killsSharedOnFlightGroup));
	memcpy(record.killsFullFromPlayer, g_pilotData.killsFullFromPlayer,
	       sizeof(record.killsFullFromPlayer));
	memcpy(record.killsSharedFromPlayer, g_pilotData.killsSharedFromPlayer,
	       sizeof(record.killsSharedFromPlayer));
	memcpy(record.killsFullFromFlightGroup,
	       g_pilotData.killsFullFromFlightGroup,
	       sizeof(record.killsFullFromFlightGroup));
	memcpy(record.killsSharedFromFlightGroup,
	       g_pilotData.killsSharedFromFlightGroup,
	       sizeof(record.killsSharedFromFlightGroup));
	memcpy(record.flightGroupRating, g_pilotData.flightGroupRating,
	       sizeof(record.flightGroupRating));

	memcpy(record.lastMissionStats.totalScorePerMT,
	       g_pilotData.lastMissionStats.totalScorePerMT,
	       sizeof(record.lastMissionStats.totalScorePerMT));
	memcpy(record.lastMissionStats.standaloneMissionsPlayedPerMT,
	       g_pilotData.lastMissionStats.standaloneMissionsPlayedPerMT,
	       sizeof(record.lastMissionStats.standaloneMissionsPlayedPerMT));
	memcpy(record.lastMissionStats.sequenceMissionsPlayedPerMT,
	       g_pilotData.lastMissionStats.sequenceMissionsPlayedPerMT,
	       sizeof(record.lastMissionStats.sequenceMissionsPlayedPerMT));
	memcpy(record.lastMissionStats.totalKillsPerMT,
	       g_pilotData.lastMissionStats.totalKillsPerMT,
	       sizeof(record.lastMissionStats.totalKillsPerMT));
	memcpy(record.lastMissionStats.totalFriendliesKilledPerMT,
	       g_pilotData.lastMissionStats.totalFriendliesKilledPerMT,
	       sizeof(record.lastMissionStats.totalFriendliesKilledPerMT));
	for (missionType = 0; missionType < 3; ++missionType) {
		memcpy(record.lastMissionStats.killsPerCraftPerMT[missionType],
		       g_pilotData.lastMissionStats
			       .killsPerCraftPerMT[missionType],
		       sizeof(record.lastMissionStats
				      .killsPerCraftPerMT[missionType]));
		memcpy(record.lastMissionStats
			       .killsSharedPerCraftPerMT[missionType],
		       g_pilotData.lastMissionStats
			       .killsSharedPerCraftPerMT[missionType],
		       sizeof(record.lastMissionStats
				      .killsSharedPerCraftPerMT[missionType]));
		memcpy(record.lastMissionStats
			       .killsAssistsPerCraftPerMT[missionType],
		       g_pilotData.lastMissionStats
			       .killsAssistsPerCraftPerMT[missionType],
		       sizeof(record.lastMissionStats
				      .killsAssistsPerCraftPerMT[missionType]));
		record.lastMissionStats.killsPerCraftPerMT[missionType][78] = 0;
		record.lastMissionStats.killsPerCraftPerMT[missionType][54] = 0;
		record.lastMissionStats.killsPerCraftPerMT[missionType][45] = 0;
		record.lastMissionStats.killsPerCraftPerMT[missionType][43] = 0;
		record.lastMissionStats.killsPerCraftPerMT[missionType][41] = 0;
		record.lastMissionStats.killsPerCraftPerMT[missionType][36] = 0;
		record.lastMissionStats.killsPerCraftPerMT[missionType][4] = 0;
		record.lastMissionStats
			.killsSharedPerCraftPerMT[missionType][78] = 0;
		record.lastMissionStats
			.killsSharedPerCraftPerMT[missionType][54] = 0;
		record.lastMissionStats
			.killsSharedPerCraftPerMT[missionType][45] = 0;
		record.lastMissionStats
			.killsSharedPerCraftPerMT[missionType][43] = 0;
		record.lastMissionStats
			.killsSharedPerCraftPerMT[missionType][41] = 0;
		record.lastMissionStats
			.killsSharedPerCraftPerMT[missionType][36] = 0;
		record.lastMissionStats
			.killsSharedPerCraftPerMT[missionType][4] = 0;
		record.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType][78] = 0;
		record.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType][54] = 0;
		record.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType][45] = 0;
		record.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType][43] = 0;
		record.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType][41] = 0;
		record.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType][36] = 0;
		record.lastMissionStats
			.killsAssistsPerCraftPerMT[missionType][4] = 0;
	}
	memcpy(record.lastMissionStats.killsFullOnPlayerRatingPerMT,
	       g_pilotData.lastMissionStats.killsFullOnPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killsFullOnPlayerRatingPerMT));
	memcpy(record.lastMissionStats.killsSharedOnPlayerRatingPerMT,
	       g_pilotData.lastMissionStats.killsSharedOnPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killsSharedOnPlayerRatingPerMT));
	memcpy(record.lastMissionStats.killsAssistOnPlayerRatingPerMT,
	       g_pilotData.lastMissionStats.killsAssistOnPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killsAssistOnPlayerRatingPerMT));
	memcpy(record.lastMissionStats.killsFullOnAIRatingPerMT,
	       g_pilotData.lastMissionStats.killsFullOnAIRatingPerMT,
	       sizeof(record.lastMissionStats.killsFullOnAIRatingPerMT));
	memcpy(record.lastMissionStats.killsSharedOnAIRatingPerMT,
	       g_pilotData.lastMissionStats.killsSharedOnAIRatingPerMT,
	       sizeof(record.lastMissionStats.killsSharedOnAIRatingPerMT));
	memcpy(record.lastMissionStats.killsAssistOnAIRatingPerMT,
	       g_pilotData.lastMissionStats.killsAssistOnAIRatingPerMT,
	       sizeof(record.lastMissionStats.killsAssistOnAIRatingPerMT));
	memcpy(record.lastMissionStats.numSpecialInspectedPerMT,
	       g_pilotData.lastMissionStats.numSpecialInspectedPerMT,
	       sizeof(record.lastMissionStats.numSpecialInspectedPerMT));
	memcpy(record.lastMissionStats.energyHitsPerMT,
	       g_pilotData.lastMissionStats.energyHitsPerMT,
	       sizeof(record.lastMissionStats.energyHitsPerMT));
	memcpy(record.lastMissionStats.energyFiredPerMT,
	       g_pilotData.lastMissionStats.energyFiredPerMT,
	       sizeof(record.lastMissionStats.energyFiredPerMT));
	memcpy(record.lastMissionStats.warheadsHitsPerMT,
	       g_pilotData.lastMissionStats.warheadsHitsPerMT,
	       sizeof(record.lastMissionStats.warheadsHitsPerMT));
	memcpy(record.lastMissionStats.warheadsFiredPerMT,
	       g_pilotData.lastMissionStats.warheadsFiredPerMT,
	       sizeof(record.lastMissionStats.warheadsFiredPerMT));
	memcpy(record.lastMissionStats.totalCraftLossesPerMT,
	       g_pilotData.lastMissionStats.totalCraftLossesPerMT,
	       sizeof(record.lastMissionStats.totalCraftLossesPerMT));
	memcpy(record.lastMissionStats.lossesByCollisionsPerMT,
	       g_pilotData.lastMissionStats.lossesByCollisionsPerMT,
	       sizeof(record.lastMissionStats.lossesByCollisionsPerMT));
	memcpy(record.lastMissionStats.lossesByStarshipsPerMT,
	       g_pilotData.lastMissionStats.lossesByStarshipsPerMT,
	       sizeof(record.lastMissionStats.lossesByStarshipsPerMT));
	memcpy(record.lastMissionStats.lossesByMinesPerMT,
	       g_pilotData.lastMissionStats.lossesByMinesPerMT,
	       sizeof(record.lastMissionStats.lossesByMinesPerMT));
	memcpy(record.lastMissionStats.killedByPlayerRatingPerMT,
	       g_pilotData.lastMissionStats.killedByPlayerRatingPerMT,
	       sizeof(record.lastMissionStats.killedByPlayerRatingPerMT));
	memcpy(record.lastMissionStats.killedByAIRatingPerMT,
	       g_pilotData.lastMissionStats.killedByAIRatingPerMT,
	       sizeof(record.lastMissionStats.killedByAIRatingPerMT));
	memcpy(record.networkPlayers, g_pilotData.networkPlayers,
	       sizeof(record.networkPlayers));
	memcpy(record.teams, g_pilotData.teams, sizeof(record.teams));
	record.currentFactionId = g_pilotData.currentFactionId;

	for (factionId = 0; factionId < 4; ++factionId) {
		PilotXvtFaction *destination =
			&record.factionStatistics[factionId];
		PilotFaction *source =
			&g_pilotData.factionStatistics[factionId];

		destination->totalMissionsPlayedCount =
			source->totalMissionsPlayedCount;
		memcpy(destination->meleePlaques, source->meleePlaques,
		       sizeof(destination->meleePlaques) +
			       sizeof(destination->tournamentTrophies) +
			       sizeof(destination->missionEvaluations) +
			       sizeof(destination->battleMedallions));
		memcpy(destination->missionAwards, source->missionAwards,
		       sizeof(destination->missionAwards));
		memcpy(destination->fieldBC, source->fieldBC,
		       sizeof(destination->fieldBC));
		destination->totalScore = source->totalScore;
		memcpy(destination->stats.totalScorePerMT,
		       source->stats.totalScorePerMT,
		       sizeof(destination->stats.totalScorePerMT));
		memcpy(destination->stats.standaloneMissionsPlayedPerMT,
		       source->stats.standaloneMissionsPlayedPerMT,
		       sizeof(destination->stats
				      .standaloneMissionsPlayedPerMT));
		memcpy(destination->stats.sequenceMissionsPlayedPerMT,
		       source->stats.sequenceMissionsPlayedPerMT,
		       sizeof(destination->stats.sequenceMissionsPlayedPerMT));
		memcpy(destination->stats.totalKillsPerMT,
		       source->stats.totalKillsPerMT,
		       sizeof(destination->stats.totalKillsPerMT));
		memcpy(destination->stats.totalFriendliesKilledPerMT,
		       source->stats.totalFriendliesKilledPerMT,
		       sizeof(destination->stats.totalFriendliesKilledPerMT));
		for (missionType = 0; missionType < 3; ++missionType) {
			memcpy(destination->stats
				       .killsPerCraftPerMT[missionType],
			       source->stats.killsPerCraftPerMT[missionType],
			       sizeof(destination->stats.killsPerCraftPerMT
					      [missionType]));
			memcpy(destination->stats
				       .killsSharedPerCraftPerMT[missionType],
			       source->stats
				       .killsSharedPerCraftPerMT[missionType],
			       sizeof(destination->stats
					      .killsSharedPerCraftPerMT
						      [missionType]));
			memcpy(destination->stats
				       .killsAssistsPerCraftPerMT[missionType],
			       source->stats
				       .killsAssistsPerCraftPerMT[missionType],
			       sizeof(destination->stats
					      .killsAssistsPerCraftPerMT
						      [missionType]));
			destination->stats.killsPerCraftPerMT[missionType][78] =
				0;
			destination->stats.killsPerCraftPerMT[missionType][54] =
				0;
			destination->stats.killsPerCraftPerMT[missionType][45] =
				0;
			destination->stats.killsPerCraftPerMT[missionType][43] =
				0;
			destination->stats.killsPerCraftPerMT[missionType][41] =
				0;
			destination->stats.killsPerCraftPerMT[missionType][36] =
				0;
			destination->stats.killsPerCraftPerMT[missionType][4] =
				0;
			destination->stats
				.killsSharedPerCraftPerMT[missionType][78] = 0;
			destination->stats
				.killsSharedPerCraftPerMT[missionType][54] = 0;
			destination->stats
				.killsSharedPerCraftPerMT[missionType][45] = 0;
			destination->stats
				.killsSharedPerCraftPerMT[missionType][43] = 0;
			destination->stats
				.killsSharedPerCraftPerMT[missionType][41] = 0;
			destination->stats
				.killsSharedPerCraftPerMT[missionType][36] = 0;
			destination->stats
				.killsSharedPerCraftPerMT[missionType][4] = 0;
			destination->stats
				.killsAssistsPerCraftPerMT[missionType][78] = 0;
			destination->stats
				.killsAssistsPerCraftPerMT[missionType][54] = 0;
			destination->stats
				.killsAssistsPerCraftPerMT[missionType][45] = 0;
			destination->stats
				.killsAssistsPerCraftPerMT[missionType][43] = 0;
			destination->stats
				.killsAssistsPerCraftPerMT[missionType][41] = 0;
			destination->stats
				.killsAssistsPerCraftPerMT[missionType][36] = 0;
			destination->stats
				.killsAssistsPerCraftPerMT[missionType][4] = 0;
		}
		memcpy(destination->stats.killsFullOnPlayerRatingPerMT,
		       source->stats.killsFullOnPlayerRatingPerMT,
		       sizeof(destination->stats.killsFullOnPlayerRatingPerMT));
		memcpy(destination->stats.killsSharedOnPlayerRatingPerMT,
		       source->stats.killsSharedOnPlayerRatingPerMT,
		       sizeof(destination->stats
				      .killsSharedOnPlayerRatingPerMT));
		memcpy(destination->stats.killsAssistOnPlayerRatingPerMT,
		       source->stats.killsAssistOnPlayerRatingPerMT,
		       sizeof(destination->stats
				      .killsAssistOnPlayerRatingPerMT));
		memcpy(destination->stats.killsFullOnAIRatingPerMT,
		       source->stats.killsFullOnAIRatingPerMT,
		       sizeof(destination->stats.killsFullOnAIRatingPerMT));
		memcpy(destination->stats.killsSharedOnAIRatingPerMT,
		       source->stats.killsSharedOnAIRatingPerMT,
		       sizeof(destination->stats.killsSharedOnAIRatingPerMT));
		memcpy(destination->stats.killsAssistOnAIRatingPerMT,
		       source->stats.killsAssistOnAIRatingPerMT,
		       sizeof(destination->stats.killsAssistOnAIRatingPerMT));
		memcpy(destination->stats.numSpecialInspectedPerMT,
		       source->stats.numSpecialInspectedPerMT,
		       sizeof(destination->stats.numSpecialInspectedPerMT));
		memcpy(destination->stats.energyHitsPerMT,
		       source->stats.energyHitsPerMT,
		       sizeof(destination->stats.energyHitsPerMT));
		memcpy(destination->stats.energyFiredPerMT,
		       source->stats.energyFiredPerMT,
		       sizeof(destination->stats.energyFiredPerMT));
		memcpy(destination->stats.warheadsHitsPerMT,
		       source->stats.warheadsHitsPerMT,
		       sizeof(destination->stats.warheadsHitsPerMT));
		memcpy(destination->stats.warheadsFiredPerMT,
		       source->stats.warheadsFiredPerMT,
		       sizeof(destination->stats.warheadsFiredPerMT));
		memcpy(destination->stats.totalCraftLossesPerMT,
		       source->stats.totalCraftLossesPerMT,
		       sizeof(destination->stats.totalCraftLossesPerMT));
		memcpy(destination->stats.lossesByCollisionsPerMT,
		       source->stats.lossesByCollisionsPerMT,
		       sizeof(destination->stats.lossesByCollisionsPerMT));
		memcpy(destination->stats.lossesByStarshipsPerMT,
		       source->stats.lossesByStarshipsPerMT,
		       sizeof(destination->stats.lossesByStarshipsPerMT));
		memcpy(destination->stats.lossesByMinesPerMT,
		       source->stats.lossesByMinesPerMT,
		       sizeof(destination->stats.lossesByMinesPerMT));
		memcpy(destination->stats.killedByPlayerRatingPerMT,
		       source->stats.killedByPlayerRatingPerMT,
		       sizeof(destination->stats.killedByPlayerRatingPerMT));
		memcpy(destination->stats.killedByAIRatingPerMT,
		       source->stats.killedByAIRatingPerMT,
		       sizeof(destination->stats.killedByAIRatingPerMT));
		memcpy(destination->spTrainingData, source->field1558,
		       sizeof(destination->spTrainingData));
		memcpy(destination->spMeleeData,
		       &source->spTrainingMissions[99].field20,
		       sizeof(destination->spMeleeData));
		memcpy(destination->spCombatData,
		       &source->spMeleeMissions[249].field20,
		       sizeof(destination->spCombatData));
		memcpy(destination->mpTrainingData,
		       &source->spCombatMissions[249].field20,
		       sizeof(destination->mpTrainingData));
		memcpy(destination->mpMeleeData,
		       &source->mpTrainingMissions[99].field2C,
		       sizeof(destination->mpMeleeData));
		memcpy(destination->mpCombatData,
		       &source->mpMeleeMissions[249].field2C,
		       sizeof(destination->mpCombatData));
		memcpy(destination->spTournamentData,
		       &source->mpCombatMissions[249].field2C,
		       sizeof(destination->spTournamentData));
		memcpy(destination->mpTournamentData,
		       &source->spTournaments[24].field24,
		       sizeof(destination->mpTournamentData));
		memcpy(destination->spBattleData,
		       &source->mpTournaments[24].field28,
		       sizeof(destination->spBattleData));
		memcpy(destination->mpBattleData,
		       &source->spBattles[24].field20,
		       sizeof(destination->mpBattleData));
	}

#ifdef XVT_MODERN
	(void)stream;
	return XvtStorage_WriteAtomic(fileName, &record, sizeof(record));
#else
	File_WriteBytes(stream, &record, sizeof(record));
	File_ChangeToInstallPath();
	File_Close(stream);
	return 1;
#endif
}
