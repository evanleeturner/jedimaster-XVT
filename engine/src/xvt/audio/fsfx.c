#include "xvt/audio/fsfx.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_timing.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/config.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include <stdio.h>
#include <string.h>

/* Falloff distance, in world units, of flight sound ids 0 to 95, one entry per
 * id; fsfx_ComputeSourceVolume reads entries 0 to 94 (ids from 95 use 8192).
 * Closer than it, the volume rises from base >> 2 toward the base; from it to
 * twice it the sound plays at base >> 2, from twice to four times it at
 * base >> 3, and from four times it not at all. Ids 0 to 3 hold 0. */
// GLOBAL: XVT 0x520F18
uint16_t g_fsfxFalloffDistanceBySfxSlot[96] = {
	0,     0,     0,     0,	    8192,  8192,  8192,	 10240, 10240, 10240,
	10240, 10240, 12288, 12288, 12288, 8192,  10240, 10240, 8192,  8192,
	8192,  49152, 24576, 24576, 24576, 24576, 24576, 24576, 8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,
};
/* Base volume, 0 to 127, of flight sound ids 0 to 95, one entry per id.
 * fsfx_ComputeSourceVolume scales it by distance for ids under 95 (ids from 95
 * use 112) and by the interior volume setting for an interior sound of any
 * id. */
// GLOBAL: XVT 0x520FD8
uint8_t g_fsfxBaseVolumeBySfxSlot[96] = {
	0,   0,	  0,   0,   72,	 72,  96,  112, 80,  80,  80,  80,  96,	 96,
	96,  72,  80,  80,  111, 111, 127, 127, 127, 127, 127, 127, 127, 127,
	88,  127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 80,
	127, 95,  56,  56,  56,	 56,  56,  127, 127, 127, 72,  56,  88,	 72,
	56,  72,  56,  56,  56,	 56,  112, 112, 112, 112, 112, 112, 112, 112,
	112, 112, 112, 112, 112, 112, 112, 112, 127, 127, 112, 112, 112, 112,
	112, 112, 96,  96,  112, 112, 127, 127, 64,  127, 127, 112,
};
/* First line of each wingman voice category, 0 to 23, as an offset within a
 * pilot's 97-line voice list. */
// GLOBAL: XVT 0x521038
static const uint8_t g_fsfxVoiceCategoryBaseOffset[24] = {
	0x00, 0x06, 0x0E, 0x14, 0x18, 0x19, 0x1B, 0x1D, 0x1F, 0x20, 0x28, 0x2D,
	0x2E, 0x38, 0x39, 0x3A, 0x3C, 0x40, 0x46, 0x47, 0x4C, 0x52, 0x5A, 0x5C};
/* Number of lines in each wingman voice category, 0 to 23. */
// GLOBAL: XVT 0x521050
static const uint8_t g_fsfxVoiceCategoryVariantCount[24] = {
	0x06, 0x08, 0x06, 0x04, 0x01, 0x02, 0x02, 0x01, 0x01, 0x08, 0x05, 0x01,
	0x05, 0x01, 0x01, 0x02, 0x04, 0x06, 0x01, 0x05, 0x06, 0x08, 0x02, 0x05};
/* Plays of one line, per wingman voice category, after which
 * fsfx_SelectAvailableVoiceVariant passes it over; 0 never passes a line
 * over. */
// GLOBAL: XVT 0x521068
static const uint8_t g_fsfxVoiceCategoryRepeatThreshold[24] = {
	0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x01,
	0x01, 0x02, 0x01, 0x01, 0x02, 0x00, 0x01, 0x01, 0x00, 0x02, 0x01, 0x01};
/* Tactical officer line, as an offset from voice slot 696, that names a flight
 * group, indexed by the group's designation code; 0xFF for codes with no
 * line. */
// GLOBAL: XVT 0x521080
static const uint8_t g_fsfxDesignationToTacticalMessageId[24] = {
	0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0D,
	0x0E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
/* First line of each commander voice category, 0 to 9, as an offset from voice
 * slot 804. */
// GLOBAL: XVT 0x521098
static const uint8_t g_commanderVoiceSfxOffsetByCategory[10] = {
	0x00, 0x02, 0x0A, 0x0C, 0x0D, 0x0E, 0x12, 0x14, 0x1A, 0x1E};
/* Number of lines in each commander voice category, 0 to 9; a 0 count plays the
 * category's first line. */
// GLOBAL: XVT 0x5210A8
static const uint8_t g_commanderVoiceVariantCountByCategory[10] = {
	0x02, 0x08, 0x02, 0x00, 0x00, 0x04, 0x02, 0x06, 0x04, 0x04};
/* 1 once fsfx_LoadSfxList has passed a list line to Sound_LoadEffect; only that
 * function writes it, and nothing sets it back to 0. The voice queue runs only
 * while it is set. */
// GLOBAL: XVT 0x9ECC52
uint8_t g_fsfxLoaded = 0;
/* Entries in the voice queue, the five g_fsfxVoiceQueue arrays, 0 to 128. Four
 * functions write it: fsfx_QueueVoiceSfx adds one, fsfx_UpdateVoiceQueue takes
 * one off, fsfx_RemoveVoiceQueueEntryChain takes off a chain and
 * fsfx_ResetFlightSfxState sets 0. */
// GLOBAL: XVT 0x9A20A4
uint8_t g_fsfxVoiceQueueCount = 0;
/* Speaker type of the line fsfx_UpdateVoiceQueue last took from the queue; only
 * it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA0A870
static uint8_t g_fsfxCurrentVoiceSpeakerType = 0;
/* Chain flag of each queued voice line: 0 starts a message, nonzero continues
 * the one before it, so the lines are pruned together. */
// GLOBAL: XVT 0xA0A880
uint8_t g_fsfxVoiceQueueChainFlag[128] = {0};
/* Voice category of the line fsfx_UpdateVoiceQueue last took from the queue;
 * only it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA0A900
static uint8_t g_fsfxCurrentVoiceCategory = 0;
/* Object signature each queued voice line is about, 0xFFFF for none;
 * fsfx_PruneStaleVoiceQueueEntries drops a tactical status message whose object
 * is gone. */
// GLOBAL: XVT 0xA0A910
uint16_t g_fsfxVoiceQueueObjectSignature[128] = {0};
/* Mission time, in seconds, at which the tactical officer last reported each
 * craft slot, 0 for never; fsfx_SpeakTacticalOfficerEvent holds a report back
 * within 10 seconds of the last. fsfx_ResetFlightSfxState clears the slots
 * below g_activeRegionCraftObjectSlotEnd. */
// GLOBAL: XVT 0xA0A7F0
static int g_fsfxTacOfficerLastSpeakSecondsByObj[136] = {0};
/* Path fsfx_LoadSfxList builds for each effect it loads: the list line prefixed
 * with the wave folder. */
// GLOBAL: XVT 0xA0AB20
char g_fsfxSfxLoadPath[720] = {0};
/* Path of the mission file being flown: "DEMO.TIE" until Flight_Main (original
 * build) or XvtFlightEntry_CreateDevices (modern) copies in the mission path
 * from the launch arguments. FeDiskIo_InitResources, Flight_MainLoop and
 * XvtFlightLoading_Palette change its last three letters while they open the
 * mission's other files, then put them back. */
// GLOBAL: XVT 0x523448
char g_currentMissionFile[128] = "DEMO.TIE";
/* Speaker type of each queued voice line: 0 special, 1 wingman pilot, 2
 * tactical officer, 3 commander. */
// GLOBAL: XVT 0xA0AA20
uint8_t g_fsfxVoiceQueueSpeakerType[128] = {0};
/* Voice category of each queued voice line. */
// GLOBAL: XVT 0xA0AAA0
uint8_t g_fsfxVoiceQueueCategory[128] = {0};
/* Flight sound id of the voice line fsfx_UpdateVoiceQueue last started, 0 when
 * none; it starts no other line while this one plays. fsfx_ResetFlightSfxState
 * sets 0. */
// GLOBAL: XVT 0xA0AA10
static int g_fsfxCurrentVoiceSfxSlot = 0;
/* Counted plays of each wingman voice line, in six lists of 97 entries indexed
 * by craft ordinal: fsfx_SpeakWingmanEvent adds to it as it queues most lines,
 * and fsfx_SelectAvailableVoiceVariant compares it with the repeat thresholds.
 * fsfx_ResetFlightSfxState clears it. */
// GLOBAL: XVT 0xA0ABA0
static uint8_t g_fsfxVoiceLinePlayCounts[6 * 97] = {0};
/* Effect name of each flight sound id, the list line it was loaded from and the
 * name the Sound_ functions find the effect by; empty for an id never loaded.
 * fsfx_LoadSfxList fills it, fsfx_ClearSfxNameTable empties it. */
// GLOBAL: XVT 0xA0ADF0
char g_fsfxSfxNameTable[838][24] = {{0}};
/* What Sound_LoadEffect returned for each flight sound id: 1 when the effect
 * loaded, 0 when not. fsfx_LoadSfxList writes it and fsfx_ResetFlightSfxState
 * clears it; fsfx_PlaySound and the voice queue play nothing from an id holding
 * 0. */
// GLOBAL: XVT 0xA0FE80
uint16_t g_fsfxLoadedBySlot[838] = {0};
/* Flight sound id of each queued voice line. */
// GLOBAL: XVT 0xA0FC80
int g_fsfxVoiceQueueSfxSlot[128] = {0};
/* Object signature of the line fsfx_UpdateVoiceQueue last took from the queue;
 * only it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA1050C
static uint16_t g_fsfxCurrentVoiceObjectSignature = 0;
/* Chain flag of the line fsfx_UpdateVoiceQueue last took from the queue; only
 * it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA1050E
static uint8_t g_fsfxCurrentVoiceChainFlag = 0;
/* Object type of the local player's craft when fsfx_UpdatePlayerEngineLoop last
 * found one with an engine sound; it uses it to stop that sound once the craft
 * is gone. Only that function writes it, and nothing resets it. */
// GLOBAL: XVT 0x556350
uint8_t g_playerEngineLoopObjectType = 0;

/* Empties every name in g_fsfxSfxNameTable. Returns 0. Flight_MainLoop calls it
 * in the original build, XvtFlightLoading_Globals in the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x42DE40
int fsfx_ClearSfxNameTable(void)
{
	memset(g_fsfxSfxNameTable, 0, sizeof(g_fsfxSfxNameTable));
	return 0;
}

/* Calls Sound_UnloadAllEffects. FeDiskIo_FreeFlightResources is its only
 * caller. */
// FUNCTION: XVT 0x42DE70
void fsfx_UnloadAllEffects_Thunk(void) { Sound_UnloadAllEffects(); }

/* Clears the flight sound state for a new flight: g_fsfxLoadedBySlot,
 * g_fsfxVoiceLinePlayCounts, the entries of
 * g_fsfxTacOfficerLastSpeakSecondsByObj below g_activeRegionCraftObjectSlotEnd,
 * g_fsfxVoiceQueueCount and g_fsfxCurrentVoiceSfxSlot. Leaves g_fsfxLoaded.
 * FeDiskIo_InitGlobalBuffers is its only caller. */
// FUNCTION: XVT 0x42DE80
void fsfx_ResetFlightSfxState(void)
{
	unsigned int objectIndex;
	unsigned int voiceOffset;

	memset(g_fsfxLoadedBySlot, 0, sizeof(g_fsfxLoadedBySlot));
	for (voiceOffset = 0; voiceOffset < sizeof(g_fsfxVoiceLinePlayCounts);
	     voiceOffset += 97) {
		memset(&g_fsfxVoiceLinePlayCounts[voiceOffset], 0, 97);
	}
	for (objectIndex = 0;
	     objectIndex < (unsigned int)g_activeRegionCraftObjectSlotEnd;
	     ++objectIndex) {
		g_fsfxTacOfficerLastSpeakSecondsByObj[objectIndex] = 0;
	}
	g_fsfxVoiceQueueCount = 0;
	g_fsfxCurrentVoiceSfxSlot = 0;
}

/* Loads the effects a list file names, one per line, into consecutive flight
 * sound ids from firstSoundId. Each line that is not empty, cut at its first CR
 * or LF, becomes the id's name in g_fsfxSfxNameTable, and the file it names in
 * the wave folder, its path built in g_fsfxSfxLoadPath, loads with
 * Sound_LoadEffect, whose result goes in g_fsfxLoadedBySlot; after each load it
 * calls FlightLoading_PulseAndDrawProgressScreen and sets g_fsfxLoaded to 1.
 * With g_flightConfVoiceEnabled 0, ids over 0x5F are skipped without loading.
 * Returns the number of lines it passed to Sound_LoadEffect, or 0 when the list
 * does not open. Does not check that the ids stay under 838 or that a line fits
 * the 24-byte name. */
// FUNCTION: XVT 0x42DED0
int fsfx_LoadSfxList(char *fileNameBuffer, uint16_t firstSoundId)
{
	XvtFile *stream;
	char buffer[256];
	char *lineEnd;
	uint16_t loadedCount = 0;

	if (FeDiskIo_OpenGlobalStream(fileNameBuffer, "rb", 0, 0) == 0) {
		return 0;
	}
	stream = (XvtFile *)g_stream;
	while (File_Gets(buffer, sizeof(buffer), stream) != NULL) {
		lineEnd = buffer;
		while (*lineEnd != '\0' && *lineEnd != '\r' &&
		       *lineEnd != '\n') {
			++lineEnd;
		}
		*lineEnd = '\0';
		if (buffer[0] == '\0') {
			continue;
		}
		if (g_flightConfVoiceEnabled == 0 && firstSoundId > 0x5fu) {
			++firstSoundId;
			continue;
		}

		strcpy(g_fsfxSfxLoadPath, "wave\\");
		strcat(g_fsfxSfxLoadPath, buffer);
		strcpy(g_fsfxSfxNameTable[firstSoundId], buffer);
		g_fsfxLoadedBySlot[firstSoundId] = (uint16_t)Sound_LoadEffect(
			g_fsfxSfxLoadPath, g_fsfxSfxNameTable[firstSoundId]);
		++firstSoundId;
		FlightLoading_PulseAndDrawProgressScreen();
		g_fsfxLoaded = 1;
		++loadedCount;
	}
	FeDiskIo_CloseGlobalStream(0);
	return loadedCount;
}

/* Loads the mission's voice lists from the wave folder with fsfx_LoadSfxList;
 * does nothing when g_flightConfVoiceEnabled or the voice volume is 0. With the
 * tactical officer on it loads RTO1.LST or RTO2.LST for a player on IFF 0, else
 * ITO1.LST or ITO2.LST, chosen by GameRand2() & 1, at id 0x2B8 (696). With the
 * commander on it loads RCMD.LST for IFF 0, ICMD.LST for IFF 1, else PCMD.LST,
 * at 0x324 (804). With wingman voices on, when the player's flight group (the
 * first whose playerOwnerIdx is g_localPlayer) has more than one craft or more
 * than one group has a playerNumber, it loads one pilot list per craft of that
 * group, RSP1.LST to RSP6.LST (ISP for IFF 1) starting at a random one and
 * cycling, at ids 114, 211 and on, 97 apart, with no limit on the count. With
 * special voices on it loads the mission file's name, less anything up to and
 * including its first backslash, with its last three letters changed to "lst",
 * at 0x5F. Does not check that the mission name fits its 64-byte buffers; the
 * modern build changes the letters only for a name of 3 or more characters. */
// FUNCTION: XVT 0x42E070
void fsfx_LoadMissionVoiceSfx(void)
{
	char listName[64];
	char missionFileName[64];
	char path[64];
	int numberOfCraft;
	unsigned int playerGroup;
	int playerFlownGroupCount;
	uint16_t variant;
	int firstSoundId;
	int missionFileNameLength;
	int missionFileNameStart;
	int pathLength;

	if (g_flightConfVoiceEnabled == 0 || g_gameConfig.voiceVolume == 0) {
		return;
	}
	if (g_gameConfig.voiceTacticalOfficerLevel != 0) {
		strcpy(path, "wave\\");
		if (g_players[g_localPlayer].iff == 0) {
			if ((GameRand2() & 1) != 0) {
				strcat(path, "RTO1.LST");
			} else {
				strcat(path, "RTO2.LST");
			}
		} else {
			if ((GameRand2() & 1) != 0) {
				strcat(path, "ITO1.LST");
			} else {
				strcat(path, "ITO2.LST");
			}
		}
		fsfx_LoadSfxList(path, 0x2b8u);
	}
	if (g_gameConfig.voiceCommanderEnabled != 0) {
		strcpy(path, "wave\\");
		if (g_players[g_localPlayer].iff == 0) {
			strcat(path, "RCMD.LST");
		} else if (g_players[g_localPlayer].iff == 1) {
			strcat(path, "ICMD.LST");
		} else {
			strcat(path, "PCMD.LST");
		}
		fsfx_LoadSfxList(path, 0x324u);
	}
	if (g_gameConfig.voicePilotLevel != 0) {
		playerFlownGroupCount = 0;
		for (playerGroup = 0;
		     (int)playerGroup < g_missionHeader.numFlightGroups;
		     ++playerGroup) {
			if (g_missionFlightGroups[playerGroup]
				    .fg.playerNumber != 0) {
				++playerFlownGroupCount;
			}
		}
		for (playerGroup = 0;
		     (int)playerGroup < g_missionHeader.numFlightGroups;
		     ++playerGroup) {
			if (g_missionFlightGroups[playerGroup].playerOwnerIdx ==
			    g_localPlayer) {
				break;
			}
		}
		if ((int)playerGroup < g_missionHeader.numFlightGroups &&
		    (g_missionFlightGroups[playerGroup].fg.numberOfCraft > 1 ||
		     playerFlownGroupCount > 1)) {
			numberOfCraft = g_missionFlightGroups[playerGroup]
						.fg.numberOfCraft;
			variant = fsfx_RandomIndex(6);
			if (numberOfCraft-- != 0) {
				firstSoundId = 114;
				do {
					strcpy(path, "wave\\");
					if (g_players[g_localPlayer].iff == 1) {
						strcpy(listName, "ISP1.LST");
					} else {
						strcpy(listName, "RSP1.LST");
					}
					listName[3] = (char)('1' + variant);
					strcat(path, listName);
					fsfx_LoadSfxList(
						path, (uint16_t)firstSoundId);
					++variant;
					firstSoundId += 97;
					if (variant >= 6) {
						variant = 0;
					}
				} while (numberOfCraft-- != 0);
			}
		}
	}
	if (g_gameConfig.voiceSpecialEnabled != 0) {
		strcpy(path, "wave\\");
		strcpy(missionFileName, g_currentMissionFile);
		missionFileNameLength = strlen(missionFileName);
#ifdef XVT_MODERN
		if (missionFileNameLength >= 3) {
#endif
			missionFileName[missionFileNameLength - 3] = 'l';
			missionFileName[missionFileNameLength - 2] = 's';
			missionFileName[missionFileNameLength - 1] = 't';
#ifdef XVT_MODERN
		}
#endif
		missionFileNameStart = 0;
		while (missionFileName[missionFileNameStart] != '\\' &&
		       missionFileNameStart < missionFileNameLength) {
			++missionFileNameStart;
		}
		if (missionFileNameStart < missionFileNameLength) {
			++missionFileNameStart;
		} else {
			missionFileNameStart = 0;
		}
		pathLength = strlen(path);
		if (missionFileNameStart < missionFileNameLength) {
			missionFileNameLength -= missionFileNameStart;
			memcpy(&path[pathLength],
			       &missionFileName[missionFileNameStart],
			       missionFileNameLength);
			pathLength += missionFileNameLength;
		}
		path[pathLength] = '\0';
		fsfx_LoadSfxList(path, 0x5fu);
	}
}

/* Stops the hyperspace exit sounds, ids 81 and 84, when they play. Does nothing
 * when g_flightSimSideEffectsSuppressed is set, playerIdx is not g_localPlayer,
 * g_flightConfSfxEnabled is 0, or interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42E460
void fsfx_StopHyperspaceExitSounds(int playerIdx)
{
	if (g_flightSimSideEffectsSuppressed != 0) {
		return;
	}
	if (g_localPlayer != playerIdx) {
		return;
	}
	if (g_flightConfSfxEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorVolume == 0) {
		return;
	}
	if (Sound_GetParam(FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL, 256) != 0) {
		Sound_StopOldestInstanceById(
			FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL);
	}
	if (Sound_GetParam(FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL, 256) !=
	    0) {
		Sound_StopOldestInstanceById(
			FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL);
	}
}

/* Queues a flight sound for the local player with Sound_QueueEffect, restart
 * allowed, played once. Returns 0, queuing nothing, when the side-effect gate
 * shuts it out (g_flightSfxSideEffectGate 0 needs
 * g_flightSimSideEffectsSuppressed 0, 1 needs it 1, 2 never plays), playerIdx
 * is not g_localPlayer, g_flightConfSfxEnabled is 0, the id holds 0 in
 * g_fsfxLoadedBySlot, interior sounds (emitterObjIdx -1) or exterior ones (any
 * other) are off or at volume 0, an R2 sound (ids 86 to 89) comes while the
 * player has no craft or one of an object type other than 1 or 2, or, with a
 * nonzero volume, a flyby or engine-wash sound (ids 72 to 79) already plays. A
 * hyperspace exit sound (81, 84) first stops the matching entry sound (80, 83).
 * The volume comes from fsfx_ComputeSourceVolume and, when it is not 0, the pan
 * from fsfx_ComputeSourcePan, which may lower the volume. The priority is 127
 * for the danger warning (37) and 125 for another sound the local player makes
 * (no emitter, an emitter the player owns, or one whose mobile object's source
 * the player owns); for any other, the volume when under 125, else 124. Returns
 * 1 otherwise, also when the volume was 0 and nothing was queued. Does not
 * check soundId against 838. */
// FUNCTION: XVT 0x42E4D0
int fsfx_PlaySound(unsigned int soundId, int emitterObjIdx, int playerIdx)
{
	int objectIndex;
	int objectType;
	int priority;
	int pan;
	struct ObjectRecord *sourceObject;
	struct MobileObject *sourceMobileObject;
	int volume;

	if (g_flightSfxSideEffectGate != 0) {
		if (g_flightSimSideEffectsSuppressed == 0) {
			return 0;
		}
		if (g_flightSfxSideEffectGate == 2) {
			return 0;
		}
	} else if (g_flightSimSideEffectsSuppressed != 0) {
		return 0;
	}
	if (playerIdx != g_localPlayer) {
		return 0;
	}
	if (g_flightConfSfxEnabled == 0) {
		return 0;
	}
	if (g_fsfxLoadedBySlot[soundId] == 0) {
		return 0;
	}

	if (emitterObjIdx == -1) {
		if (g_gameConfig.sfxInteriorEnabled == 0) {
			return 0;
		}
		if (g_gameConfig.sfxInteriorVolume == 0) {
			return 0;
		}
	} else {
		if (g_gameConfig.sfxExteriorEnabled == 0) {
			return 0;
		}
		if (g_gameConfig.sfxExteriorVolume == 0) {
			return 0;
		}
	}

	if (soundId >= FLIGHT_SOUND_R2_HAPPY &&
	    soundId <= FLIGHT_SOUND_R2_HIT) {
		objectIndex = g_players[playerIdx].objectIndex;
		if (objectIndex == -1) {
			return 0;
		}
		objectType = g_objectTable[objectIndex].objectType;
		if (objectType != 1 && objectType != 2) {
			return 0;
		}
	}
	if (soundId == FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL) {
		Sound_StopOldestInstanceById(
			FLIGHT_SOUND_HYPERSPACE_ENTER_IMPERIAL);
	}
	if (soundId == FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL) {
		Sound_StopOldestInstanceById(
			FLIGHT_SOUND_HYPERSPACE_ENTER_NON_IMPERIAL);
	}

	volume = fsfx_ComputeSourceVolume(emitterObjIdx, soundId);
	if (volume != 0) {
		pan = fsfx_ComputeSourcePan(emitterObjIdx, &volume);
		priority = 124;
		if ((unsigned int)volume < 125) {
			priority = volume;
		}

		if (emitterObjIdx == -1 ||
		    g_objectTable[emitterObjIdx].playerOwnerIdx ==
			    g_localPlayer) {
			priority = soundId == FLIGHT_SOUND_DANGER_WARNING ? 127
									  : 125;
		} else {
			sourceObject = &g_objectTable[emitterObjIdx];
			sourceMobileObject = sourceObject->mobj;
			if (sourceMobileObject != NULL &&
			    g_objectTable[sourceMobileObject->sourceObjIdx]
					    .playerOwnerIdx == g_localPlayer) {
				priority = 125;
			}
		}

		if (Sound_GetParam(soundId, 256) != 0 &&
		    soundId >= FLIGHT_SOUND_TIE_FLYBY &&
		    soundId <= FLIGHT_SOUND_ENGINE_WASH_OTHER) {
			return 0;
		}
		Sound_QueueEffect(g_fsfxSfxNameTable[soundId], 1, 0, priority,
				  volume, pan);
	}

	return 1;
}

/* Plays the firing sound of a projectile through fsfx_PlaySound, with the
 * projectile as the emitter: object types 0x89 to 0x93 play id type - 133 (4 to
 * 14), 0x94 and 0x95 play type - 138 (10, 11), 0x96 and 0x97 play type - 135
 * (15, 16), and 0x98 to 0x9B play FLIGHT_SOUND_MAGNETIC_PULSE (17), returning
 * fsfx_PlaySound's result. Returns 0 when playerIdx is not g_localPlayer,
 * g_flightConfSfxEnabled is 0 or exterior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42E720
int fsfx_triggerweaponsfx(unsigned int projectileObjectIndex, int playerIdx)
{
	int result;

	if (playerIdx != g_localPlayer) {
		return 0;
	}
	if (g_flightConfSfxEnabled == 0) {
		return 0;
	}
	if (g_gameConfig.sfxExteriorEnabled == 0) {
		return 0;
	}
	if (g_gameConfig.sfxExteriorVolume == 0) {
		return 0;
	}

	/* result first holds the projectile's object type, from which the sound ids below are computed;
	 * a played sound replaces it with fsfx_PlaySound's return, and an unlisted type returns the type. */
	result = g_objectTable[projectileObjectIndex].objectType;
	switch (g_objectTable[projectileObjectIndex].objectType) {
	case 0x89:
	case 0x8a:
	case 0x8b:
	case 0x8c:
	case 0x8d:
	case 0x8e:
	case 0x8f:
	case 0x90:
	case 0x91:
	case 0x92:
	case 0x93:
		result = fsfx_PlaySound(result - 133, projectileObjectIndex,
					playerIdx);
		break;
	case 0x94:
	case 0x95:
		result = fsfx_PlaySound(result - 138, projectileObjectIndex,
					playerIdx);
		break;
	case 0x96:
	case 0x97:
		result = fsfx_PlaySound(result - 135, projectileObjectIndex,
					playerIdx);
		break;
	case 0x98:
	case 0x99:
	case 0x9a:
	case 0x9b:
		result = fsfx_PlaySound(FLIGHT_SOUND_MAGNETIC_PULSE,
					projectileObjectIndex, playerIdx);
		break;
	}
	return result;
}

/* Volume, 0 to 127, of a sound as the local player hears it. An interior sound
 * (emitterObjIdx -1) gets s * g_fsfxBaseVolumeBySfxSlot[soundId] / 127, s being
 * the interior volume setting times 13, or 127 for a setting of 10 or more;
 * that path does not check soundId against the table's 96 entries. For any
 * other, f and b are the falloff distance and base volume from the two tables
 * (8192 and 112 for ids from 95), and d is the rough distance
 * (collide_roughdistance3d) from the local player's camera to the emitter, at
 * its previous-step position when it has a mobile object, else where
 * Mission_ResolveObjectOrMissionPointWorldLoc puts it. It returns 0 when d >> 2
 * is at least f, b >> 3 when d >> 1 is, and b >> 2 when d is. Otherwise it
 * returns v = (b >> 2) + (f - d) * (b - (b >> 2)) / (f - (f >> 5)), times
 * e / 10 when the exterior volume setting e is not 10, capped at 127; e does
 * not scale the three far cases. */
// FUNCTION: XVT 0x42E810
unsigned int fsfx_ComputeSourceVolume(int emitterObjIdx, unsigned int soundId)
{
	unsigned int volumeScale;
	unsigned int falloffDistance;
	unsigned int baseVolume;
	unsigned int distance;
	unsigned int scaledDistance;
	unsigned int quarterVolume;
	unsigned int distanceSpan;
	unsigned int volumeRange;
	unsigned int volume;
	struct PlayerData *listener;
	int deltaX;
	int deltaY;
	int worldZ;

	if (emitterObjIdx == -1) {
		volumeScale = g_gameConfig.sfxInteriorVolume;
		if (volumeScale >= 10) {
			volumeScale = 127;
		} else {
			volumeScale *= 13;
		}
		return volumeScale * g_fsfxBaseVolumeBySfxSlot[soundId] / 127;
	}

	if (soundId >= 95) {
		falloffDistance = 8192;
		baseVolume = 112;
	} else {
		falloffDistance = g_fsfxFalloffDistanceBySfxSlot[soundId];
		baseVolume = g_fsfxBaseVolumeBySfxSlot[soundId];
	}
	if (g_objectTable[emitterObjIdx].mobj != NULL) {
		listener = &g_players[g_localPlayer];
		deltaX = g_objectTable[emitterObjIdx].mobj->prevWorldX -
			 listener->viewState.cameraWorldX;
		deltaY = g_objectTable[emitterObjIdx].mobj->prevWorldY -
			 listener->viewState.cameraWorldY;
		worldZ = g_objectTable[emitterObjIdx].mobj->prevWorldZ;
	} else {
		Mission_ResolveObjectOrMissionPointWorldLoc(emitterObjIdx, 0);
		listener = &g_players[g_localPlayer];
		deltaX = g_worldLocX - listener->viewState.cameraWorldX;
		deltaY = g_worldLocY - listener->viewState.cameraWorldY;
		worldZ = g_worldLocZ;
	}
	distance = collide_roughdistance3d(
		deltaX, deltaY, worldZ - listener->viewState.cameraWorldZ);
	scaledDistance = distance >> 2;
	if (scaledDistance >= falloffDistance) {
		return 0;
	}
	scaledDistance = distance >> 1;
	if (scaledDistance >= falloffDistance) {
		return baseVolume >> 3;
	}
	if (distance >= falloffDistance) {
		return baseVolume >> 2;
	}

	quarterVolume = baseVolume >> 2;
	distanceSpan = falloffDistance - distance;
	volumeRange = baseVolume - quarterVolume;
	volume = quarterVolume +
		 distanceSpan * volumeRange /
			 (falloffDistance - (falloffDistance >> 5));
	if (g_gameConfig.sfxExteriorVolume != 10) {
		volume = volume * g_gameConfig.sfxExteriorVolume / 10;
	}
	if (volume > 127) {
		volume = 127;
	}
	return volume;
}

/* Pan, 0 to 127 with 64 centered, of a sound from an object as the local
 * player hears it; 64 for emitterObjIdx -1. It takes the emitter's offset
 * from the local player's camera (placed as in fsfx_ComputeSourceVolume),
 * each axis cut to 16 bits, projects it with Math_Dot3Q15Wrapped on the
 * camera matrix's side row (g_camMatR0) and forward row (g_camMatR2), and
 * takes the angle trig2_arctan(side, forward). For an angle of 0x4000 or
 * more either way, a source behind, it mirrors the angle front to back,
 * keeping its side, and lowers *volume by (int16_t)(*volume * r) / 128,
 * where r = ((0x4000 - v) >> 8) * ((0x4000 - a) >> 8) / 64, a being the size
 * of the mirrored angle and v the size of 0x8000 minus the angle of the
 * offset along the camera's up row (g_camMatR1) against the forward one: up
 * to half the volume for a source straight behind, none for one level at the
 * side. Returns the angle >> 7, clamped to -64 to 63, plus 64. */
// FUNCTION: XVT 0x42E9A0
int fsfx_ComputeSourcePan(int emitterObjIdx, int *volume)
{
	struct MobileObject *sourceMobileObject;
	int dx;
	int dy;
	int dz;
	int16_t sideOffset;
	int16_t forwardOffset;
	int16_t panAngle;

	if (emitterObjIdx == -1) {
		return 64;
	}

	sourceMobileObject = g_objectTable[emitterObjIdx].mobj;
	if (sourceMobileObject != NULL) {
		dx = sourceMobileObject->prevWorldX -
		     g_players[g_localPlayer].viewState.cameraWorldX;
		dy = sourceMobileObject->prevWorldY -
		     g_players[g_localPlayer].viewState.cameraWorldY;
		dz = sourceMobileObject->prevWorldZ -
		     g_players[g_localPlayer].viewState.cameraWorldZ;
	} else {
		Mission_ResolveObjectOrMissionPointWorldLoc(emitterObjIdx, 0);
		dz = g_worldLocZ -
		     g_players[g_localPlayer].viewState.cameraWorldZ;
		dx = g_worldLocX -
		     g_players[g_localPlayer].viewState.cameraWorldX;
		dy = g_worldLocY -
		     g_players[g_localPlayer].viewState.cameraWorldY;
	}

	sideOffset = (int16_t)Math_Dot3Q15Wrapped((int16_t)dx, (int16_t)dy,
						  (int16_t)dz, g_camMatR0_X,
						  g_camMatR0_Y, g_camMatR0_Z);
	forwardOffset = (int16_t)Math_Dot3Q15Wrapped(
		(int16_t)dx, (int16_t)dy, (int16_t)dz, g_camMatR2_X,
		g_camMatR2_Y, g_camMatR2_Z);
	panAngle = trig2_arctan(sideOffset, forwardOffset);

	if (panAngle >= 0x4000 || panAngle <= -0x4000) {
		int16_t verticalAngle;
		int16_t rearAngle;
		int16_t rearScale;
		int16_t reduction;

		/* From here sideOffset holds the offset along the camera's up axis, for the vertical angle. */
		sideOffset = (int16_t)Math_Dot3Q15Wrapped(
			(int16_t)dx, (int16_t)dy, (int16_t)dz, g_camMatR1_X,
			g_camMatR1_Y, g_camMatR1_Z);
		verticalAngle = (int16_t)(0x8000 - trig2_arctan(sideOffset,
								forwardOffset));
		rearAngle = (int16_t)(0x8000 - panAngle);
		panAngle = (int16_t)(0x8000 - panAngle);
		if (verticalAngle < 0) {
			verticalAngle = (int16_t)-verticalAngle;
		}
		if (panAngle < 0) {
			rearAngle = (int16_t)-panAngle;
		}

		rearScale = (int16_t)(0x4000 - rearAngle);
		reduction = (int16_t)(0x4000 - verticalAngle);
		rearScale >>= 8;
		reduction >>= 8;
		reduction = (int16_t)(reduction * rearScale);
		reduction = (int16_t)(reduction / 64);
		reduction = (int16_t)(*volume * reduction);
		*volume -= reduction / 128;
	}

	panAngle >>= 7;
	if (panAngle < -64) {
		panAngle = -64;
	}
	if (panAngle > 63) {
		panAngle = 63;
	}
	return (int16_t)(panAngle + 64);
}

/* Plays or stops the targeting tone loops, ids 50 and 51. State 0 or 1 stops 51
 * when it plays and returns, else stops 50 when it plays. State 3 stops 50 and
 * queues 51 unless it plays; any other state stops 51 and queues 50 unless it
 * plays. Loops are queued looping, centered, at priority 125 and the interior
 * volume setting times 13, 127 from 10 up. Returns 1, or 0 when
 * g_flightConfSfxEnabled is 0 or interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42EC80
int fsfx_UpdateTargetingTone(unsigned int toneState)
{
	int volume;
	int interiorVolume;

	if (g_flightConfSfxEnabled == 0) {
		return 0;
	}
	if (g_gameConfig.sfxInteriorEnabled == 0) {
		return 0;
	}
	if (g_gameConfig.sfxInteriorVolume == 0) {
		return 0;
	}

	interiorVolume = g_gameConfig.sfxInteriorVolume;
	volume = 127;
	if (interiorVolume < 10) {
		volume = 13 * interiorVolume;
	}

	if (toneState == 0 || toneState == 1) {
		if (Sound_GetParam(51, 256) != 0) {
			Sound_StopOldestInstanceById(51);
			return 1;
		}
		if (Sound_GetParam(50, 256) != 0) {
			Sound_StopOldestInstanceById(50);
		}
	} else if (toneState == 3) {
		if (Sound_GetParam(50, 256) != 0) {
			Sound_StopOldestInstanceById(50);
		}
		if (Sound_GetParam(51, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[51], 1, 1, 125,
					  volume, 64);
			return 1;
		}
	} else {
		if (Sound_GetParam(51, 256) != 0) {
			Sound_StopOldestInstanceById(51);
		}
		if (Sound_GetParam(50, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[50], 1, 1, 125,
					  volume, 64);
			return 1;
		}
	}
	return 1;
}

/* Keeps the beam weapon's loop sounds in step with the local player's beam.
 * With active set and the beam subsystem working: a tractor beam (while its
 * fire sound, 52, is not playing) or a jamming beam (while 55 is not) plays 53
 * or 56 while g_localBeamTargetObjIdx is 0xFFFF and the next id, 54 or 57, once
 * the beam holds a target, stopping the other of the pair; a decoy beam queues
 * 59 unless 58 or 59 plays; any other beam type queues 61 unless 60 or 61
 * plays. Otherwise it stops every playing id from 52 to 61. Loops are queued
 * looping, centered, at priority 125 with fsfx_ComputeSourceVolume(-1, id).
 * Does nothing when g_flightSimSideEffectsSuppressed is set, playerIdx is not
 * g_localPlayer, g_flightConfSfxEnabled is 0 or interior sounds are off or at
 * volume 0. Does not check that the local player has a craft. */
// FUNCTION: XVT 0x42EDC0
void fsfx_UpdateBeamSystemLoop(int active, int playerIdx)
{
	struct CraftData *craft;
	BeamType beamType;
	int pairedSoundId;
	int soundId;
	int volume;
	int stopSoundId;

	if (g_flightSimSideEffectsSuppressed != 0) {
		return;
	}
	if (g_localPlayer != playerIdx) {
		return;
	}
	if (g_flightConfSfxEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorVolume == 0) {
		return;
	}

	craft = g_objectTable[g_players[g_localPlayer].objectIndex]
			.mobj->pCraft;
	if (active != 0 && (craft->workingSubsystems &
			    CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
		beamType = craft->beamTypeId;
		if (beamType == BEAM_TYPE_TRACTOR) {
			if (Sound_GetParam(52, 256) != 0) {
				return;
			}
			soundId = 53;
		} else if (beamType == BEAM_TYPE_JAMMING) {
			if (Sound_GetParam(55, 256) != 0) {
				return;
			}
			soundId = 56;
		} else if (beamType == BEAM_TYPE_DECOY) {
			if (Sound_GetParam(58, 256) == 0 &&
			    Sound_GetParam(59, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(-1, 59);
				Sound_QueueEffect(g_fsfxSfxNameTable[59], 1, 1,
						  125, volume, 64);
			}
			return;
		} else {
			if (Sound_GetParam(60, 256) == 0 &&
			    Sound_GetParam(61, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(-1, 61);
				Sound_QueueEffect(g_fsfxSfxNameTable[61], 1, 1,
						  125, volume, 64);
			}
			return;
		}

		if (g_localBeamTargetObjIdx == UINT16_MAX) {
			pairedSoundId = soundId + 1;
			if (Sound_GetParam(pairedSoundId, 256) != 0) {
				Sound_StopOldestInstanceById(pairedSoundId);
			}
			if (Sound_GetParam(soundId, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(-1, soundId);
				Sound_QueueEffect(g_fsfxSfxNameTable[soundId],
						  1, 1, 125, volume, 64);
			}
		} else {
			if (Sound_GetParam(soundId, 256) != 0) {
				Sound_StopOldestInstanceById(soundId);
			}
			pairedSoundId = soundId + 1;
			if (Sound_GetParam(pairedSoundId, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(
					-1, pairedSoundId);
				Sound_QueueEffect(
					g_fsfxSfxNameTable[soundId + 1], 1, 1,
					125, volume, 64);
			}
		}
		return;
	}

	for (stopSoundId = 52; stopSoundId <= 61; ++stopSoundId) {
		if (Sound_GetParam(stopSoundId, 256) != 0) {
			Sound_StopOldestInstanceById(stopSoundId);
		}
	}
}

/* State 0 stops the incoming-missile warning loops, ids 39 and 40. State 1
 * queues 40 at the interior volume / 3, any other state 39 at the interior
 * volume / 2, looping, centered, at priority 125, unless that id plays; it does
 * not stop the other loop. The interior volume is the setting times 13, 127
 * from 10 up. Does nothing when g_flightConfSfxEnabled is 0 or interior sounds
 * are off or at volume 0. */
// FUNCTION: XVT 0x42F030
void fsfx_UpdateIncomingMissileWarning(int warningState)
{
	int soundId;
	int volume;
	int interiorVolume;

	if (g_flightConfSfxEnabled && g_gameConfig.sfxInteriorEnabled != 0 &&
	    g_gameConfig.sfxInteriorVolume != 0) {
		if (warningState == 0) {
			if (Sound_GetParam(39, 256) != 0) {
				Sound_StopOldestInstanceById(39);
			}
			if (Sound_GetParam(40, 256) != 0) {
				Sound_StopOldestInstanceById(40);
			}
			return;
		}

		interiorVolume = g_gameConfig.sfxInteriorVolume;
		volume = 127;
		if (interiorVolume < 10) {
			volume = 13 * interiorVolume;
		}
		if (warningState == 1) {
			soundId = 40;
			volume /= 3;
		} else {
			soundId = 39;
			volume /= 2;
		}
		if (Sound_GetParam(soundId, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[soundId], 1, 1,
					  125, volume, 64);
		}
	}
}

/* Keeps the chaff loop, id 19, playing while the local player's chaff runs.
 * Stops 19 when the player has no craft or awaitingNewCraft is 1; does nothing
 * more for a craft whose countermeasure is not chaff. Otherwise, while the
 * craft's chaffActiveSeconds is nonzero, queues 19 looping, centered, at
 * priority 125 and a quarter of the interior volume (the setting times 13, 127
 * from 10 up) unless it plays, and stops it once that count is 0. Does nothing
 * when g_flightSimSideEffectsSuppressed is set, g_flightConfSfxEnabled is 0 or
 * interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42F110
void fsfx_UpdateChaffLoop(void)
{
	int playerObjectIndex;
	struct ObjectRecord *playerObject;
	struct CraftData *craft;
	int volume;
	int interiorVolume;

	if (g_flightSimSideEffectsSuppressed != 0) {
		return;
	}
	if (g_flightConfSfxEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorVolume == 0) {
		return;
	}

	playerObjectIndex = g_players[g_localPlayer].objectIndex;
	if (playerObjectIndex == -1) {
		if (Sound_GetParam(19, 256) != 0) {
			Sound_StopOldestInstanceById(19);
		}
		return;
	}

	playerObject = &g_objectTable[playerObjectIndex];
	craft = playerObject->mobj->pCraft;
	if (craft->cmTypeId != COUNTERMEASURE_TYPE_CHAFF) {
		return;
	}

	if (g_players[g_localPlayer].awaitingNewCraft == 1) {
		if (Sound_GetParam(19, 256) != 0) {
			Sound_StopOldestInstanceById(19);
		}
		return;
	}

	interiorVolume = g_gameConfig.sfxInteriorVolume;
	if (interiorVolume >= 10) {
		volume = 127;
	} else {
		volume = 13 * interiorVolume;
	}
	volume /= 4;

	if (craft->cmTypeId == COUNTERMEASURE_TYPE_CHAFF &&
	    craft->chaffActiveSeconds != 0) {
		if (Sound_GetParam(19, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[19], 1, 1, 125,
					  volume, 64);
		}
	} else if (Sound_GetParam(19, 256) != 0) {
		Sound_StopOldestInstanceById(19);
	}
}

/* Keeps the local player's engine loop in step with the throttle. The craft's
 * object type picks the sound and base frequency in hertz: types 1, 4, 14 and
 * 15 give 67 at 11000, 2 gives 68, 3 and 13 give 69, 5 to 9 give 70, each at
 * 5500, and 12 and 16 give 71 at 11000. For such a craft it stores the type in
 * g_playerEngineLoopObjectType; when the player is not awaiting a new craft and
 * the engines work, it sets the frequency of the effect's newest instance
 * (Sound_SetParam code 0x777) to 55 * (MATH2_ratioQ16(throttleSpeed,
 * 0xFFFF) / 655) + base, at most base + 5500, and then, when the sound is not
 * playing, queues it looping, centered, at priority 125 and volume
 * MATH2_fraction(e, throttleSpeed) >> 1, e being the engine volume setting
 * times 13, 127 from 10 up. Otherwise it stops the engine sound and the
 * engine-wash sounds 78 and 79. With no craft or one of another type, and
 * mapCameraState nonzero, it stops the sound for g_playerEngineLoopObjectType's
 * type and 78 and 79. Does nothing when g_flightSimSideEffectsSuppressed is
 * set, g_flightConfSfxEnabled is 0 or engine sounds are off or at volume 0. */
// FUNCTION: XVT 0x42F270
void fsfx_UpdatePlayerEngineLoop(void)
{
	int engineSoundId;
	int objectIndex;
	int baseFrequency;
	uint8_t objectType;
	struct CraftData *craft;
	uint16_t configVolume;
	int frequency;
	int volume;

	if (g_flightSimSideEffectsSuppressed != 0 ||
	    g_flightConfSfxEnabled == 0 || g_gameConfig.sfxEngineEnabled == 0 ||
	    g_gameConfig.sfxEngineVolume == 0) {
		return;
	}

	engineSoundId = -1;
	objectIndex = g_players[g_localPlayer].objectIndex;
	if (objectIndex != -1) {
		objectType = g_objectTable[objectIndex].objectType;
		switch (objectType) {
		case 1:
		case 4:
		case 14:
		case 15:
			engineSoundId = 67;
			baseFrequency = 11000;
			break;
		case 2:
			engineSoundId = 68;
			baseFrequency = 5500;
			break;
		case 3:
		case 13:
			engineSoundId = 69;
			baseFrequency = 5500;
			break;
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			engineSoundId = 70;
			baseFrequency = 5500;
			break;
		case 12:
		case 16:
			engineSoundId = 71;
			baseFrequency = 11000;
			break;
		}
	}

	if (engineSoundId != -1) {
		g_playerEngineLoopObjectType = objectType;
		if (g_players[g_localPlayer].awaitingNewCraft != 1) {
			craft = g_objectTable[objectIndex].mobj->pCraft;
			if ((craft->workingSubsystems &
			     CRAFT_SUBSYSTEM_FLAG_ENGINES) != 0) {
				configVolume = g_gameConfig.sfxEngineVolume;
				if (configVolume >= 10) {
					configVolume = 127;
				} else {
					configVolume *= 13;
				}
				frequency = 55 * (MATH2_ratioQ16(
							  craft->throttleSpeed,
							  0xffff) /
						  655) +
					    baseFrequency;
				configVolume = (uint16_t)MATH2_fraction(
					configVolume, craft->throttleSpeed);
				volume = configVolume >> 1;
				if (Sound_GetParam(engineSoundId, 256) == 0) {
					Sound_SetParam(engineSoundId, 1911,
						       frequency);
					Sound_QueueEffect(
						g_fsfxSfxNameTable
							[engineSoundId],
						1, 1, 125, volume, 64);
					return;
				}
				Sound_SetParam(engineSoundId, 1911, frequency);
				return;
			}
		}
		if (Sound_GetParam(engineSoundId, 256) != 0) {
			Sound_StopOldestInstanceById(engineSoundId);
		}
		if (Sound_GetParam(78, 256) != 0) {
			Sound_StopOldestInstanceById(78);
		}
		if (Sound_GetParam(79, 256) != 0) {
			Sound_StopOldestInstanceById(79);
		}
		return;
	} else {
		if (g_players[g_localPlayer].mapCameraState == 0) {
			return;
		}
		switch (g_playerEngineLoopObjectType) {
		case 1:
		case 4:
		case 14:
		case 15:
			engineSoundId = 67;
			break;
		case 2:
			engineSoundId = 68;
			break;
		case 3:
		case 13:
			engineSoundId = 69;
			break;
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			engineSoundId = 70;
			break;
		case 12:
		case 16:
			engineSoundId = 71;
			break;
		}
		if (Sound_GetParam(engineSoundId, 256) != 0) {
			Sound_StopOldestInstanceById(engineSoundId);
		}
		if (Sound_GetParam(78, 256) != 0) {
			Sound_StopOldestInstanceById(78);
		}
		if (Sound_GetParam(79, 256) != 0) {
			Sound_StopOldestInstanceById(79);
		}
	}
}

/* Keeps the loops for the beam effects on the local player's craft in step.
 * While beamEffectAccum[1] is nonzero it queues 63 unless it plays and,
 * when neither 64 nor 65 plays and GameRand2() < 0x1000 (in the modern
 * build only while XvtFlightTiming_ReferenceDue is true), queues id
 * (GameRand2() & 1) + 63, so 63 or 64; once it is 0 it stops 63, 64 and 65.
 * While beamEffectAccum[2] is nonzero it queues 66 at half volume unless it
 * plays, and stops it once that is 0. With no craft it stops 63 to 66.
 * Loops are queued looping, centered, at priority 125 and the interior
 * volume setting times 13, 127 from 10 up. Does nothing when
 * g_flightSimSideEffectsSuppressed is set, g_flightConfSfxEnabled is 0 or
 * interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42F5D0
void fsfx_UpdateBeamEffectLoops(void)
{
	int volume;
	int interiorVolume;
	int objectIndex;
	struct CraftData *craft;

	if (g_flightSimSideEffectsSuppressed != 0) {
		return;
	}
	if (g_flightConfSfxEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorEnabled == 0) {
		return;
	}
	if (g_gameConfig.sfxInteriorVolume == 0) {
		return;
	}

	interiorVolume = g_gameConfig.sfxInteriorVolume;
	volume = 127;
	if (interiorVolume < 10) {
		volume = 13 * interiorVolume;
	}

	objectIndex = g_players[g_localPlayer].objectIndex;
	if (objectIndex == -1) {
		if (Sound_GetParam(63, 256) != 0) {
			Sound_StopOldestInstanceById(63);
		}
		if (Sound_GetParam(64, 256) != 0) {
			Sound_StopOldestInstanceById(64);
		}
		if (Sound_GetParam(65, 256) != 0) {
			Sound_StopOldestInstanceById(65);
		}
		if (Sound_GetParam(66, 256) != 0) {
			Sound_StopOldestInstanceById(66);
		}
		return;
	}

	craft = g_objectTable[objectIndex].mobj->pCraft;
	if (craft->beamEffectAccum[1] != 0) {
		if (Sound_GetParam(63, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[63], 1, 1, 125,
					  volume, 64);
		}
		if (Sound_GetParam(64, 256) == 0 && Sound_GetParam(65, 256) == 0
#ifdef XVT_MODERN
		    && XvtFlightTiming_ReferenceDue()
#endif
		    && GameRand2() < 0x1000) {
			Sound_QueueEffect(
				g_fsfxSfxNameTable[(GameRand2() & 1) + 63], 1,
				1, 125, volume, 64);
		}
	} else {
		if (Sound_GetParam(63, 256) != 0) {
			Sound_StopOldestInstanceById(63);
		}
		if (Sound_GetParam(64, 256) != 0) {
			Sound_StopOldestInstanceById(64);
		}
		if (Sound_GetParam(65, 256) != 0) {
			Sound_StopOldestInstanceById(65);
		}
	}

	if (craft->beamEffectAccum[2] != 0) {
		if (Sound_GetParam(66, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[66], 1, 1, 125,
					  volume / 2, 64);
		}
	} else if (Sound_GetParam(66, 256) != 0) {
		Sound_StopOldestInstanceById(66);
	}
}

/* Runs the per-step flight sounds of the local player; returns at once when
 * the player has no craft. Calls fsfx_UpdateChaffLoop,
 * fsfx_UpdatePlayerEngineLoop and fsfx_UpdateBeamEffectLoops, then stops
 * there when g_flightSimSideEffectsSuppressed is set,
 * g_flightConfSfxEnabled is 0 or engine sounds are off or at volume 0.
 * Engine wash: while engineWashSourceObjIdx is not -1 it picks id 78 for a
 * source of object type 51 to 54, else 79, and a volume of 4 * the exterior
 * volume setting * engineWashStrength / 10, capped at 127. When that id is
 * not playing it emits in-flight message IFMSG_221 and queues the id
 * looping, centered, with that volume passed as the priority and 65 as the
 * volume; while it plays it sets its volume to that value (Sound_SetParam
 * code 0x600). With no wash source it stops 78 and 79. Flybys: for every
 * other craft slot below g_activeRegionCraftObjectSlotEnd holding an active
 * craft with working subsystems and nonzero speed whose object type has a
 * flyby sound (ids 72 to 77), it plays that sound through fsfx_PlaySound
 * when the craft's rough distance to the player is under its type's
 * maxBoundsExtent + 1024 and its distance at the previous step was not. */
// FUNCTION: XVT 0x42F810
void fsfx_UpdateFlightSfx(void)
{
	int playerObjectIndex;
	int washSoundId;
	int washVolume;
	int objectIndex;
	struct ObjectRecord *object;
	struct MobileObject *mobileObject;
	struct CraftData *craft;
	uint16_t flybySoundId;
	uint8_t objectType;
	int currentDistance;
	int previousDistance;
	int flybyDistance;

	playerObjectIndex = g_players[g_localPlayer].objectIndex;
	if (playerObjectIndex == -1) {
		return;
	}

	fsfx_UpdateChaffLoop();
	fsfx_UpdatePlayerEngineLoop();
	fsfx_UpdateBeamEffectLoops();
	if (g_flightSimSideEffectsSuppressed != 0 ||
	    g_flightConfSfxEnabled == 0 || g_gameConfig.sfxEngineEnabled == 0 ||
	    g_gameConfig.sfxEngineVolume == 0) {
		return;
	}

	if (g_players[g_localPlayer].engineWashSourceObjIdx != -1) {
		objectType = g_objectTable[(uint16_t)g_players[g_localPlayer]
						   .engineWashSourceObjIdx]
				     .objectType;
		if (objectType == 51 || objectType == 52 || objectType == 53) {
			washSoundId = 78;
		} else {
			washSoundId = 79;
			if (objectType == 54) {
				washSoundId = 78;
			}
		}
		washVolume = 4 * g_gameConfig.sfxExteriorVolume *
			     g_players[g_localPlayer].engineWashStrength / 10;
		if (washVolume > 127) {
			washVolume = 127;
		}
		if (Sound_GetParam(washSoundId, 256) == 0) {
			msg_emitInFlightMessage(
				IFMSG_221_YOU_RE_TAKING_DAMAGE_FROM_ENGINE_WASH,
				g_localPlayer);
			Sound_QueueEffect(g_fsfxSfxNameTable[washSoundId], 1, 1,
					  washVolume, 65, 64);
		} else {
			Sound_SetParam(washSoundId, 1536, washVolume);
		}
	} else {
		if (Sound_GetParam(78, 256) != 0) {
			Sound_StopOldestInstanceById(78);
		}
		if (Sound_GetParam(79, 256) != 0) {
			Sound_StopOldestInstanceById(79);
		}
	}

	objectIndex = 0;
	if (g_activeRegionCraftObjectSlotEnd > 0) {
		do {
			if (objectIndex != playerObjectIndex) {
				object = &g_objectTable[objectIndex];
				if (g_objectTable[objectIndex].objectType !=
				    0) {
					mobileObject = object->mobj;
					craft = mobileObject->pCraft;
					if (craft->objectKind ==
						    CRAFT_OBJECT_KIND_ACTIVE &&
					    craft->workingSubsystems != 0 &&
					    mobileObject->speed != 0) {
						flybySoundId = UINT16_MAX;
						objectType =
							g_objectTable[objectIndex]
								.objectType;
						switch (objectType) {
						case 1:
						case 4:
						case 14:
						case 15:
							flybySoundId =
								FLIGHT_SOUND_X_WING_FLYBY;
							break;
						case 2:
							flybySoundId =
								FLIGHT_SOUND_Y_WING_FLYBY;
							break;
						case 3:
						case 13:
							flybySoundId =
								FLIGHT_SOUND_A_WING_FLYBY;
							break;
						case 5:
						case 6:
						case 7:
						case 8:
						case 9:
							flybySoundId =
								FLIGHT_SOUND_TIE_FLYBY;
							break;
						case 12:
						case 16:
							flybySoundId =
								FLIGHT_SOUND_SHUTTLE_FLYBY;
							break;
						case 17:
						case 18:
						case 19:
						case 20:
						case 21:
						case 22:
						case 23:
						case 24:
						case 25:
						case 30:
							flybySoundId =
								FLIGHT_SOUND_SHUTTLE_FLYBY;
							break;
						case 38:
						case 39:
							flybySoundId =
								FLIGHT_SOUND_MILLENNIUM_FALCON_FLYBY;
							break;
						}
						if (flybySoundId !=
						    UINT16_MAX) {
							currentDistance = collide_roughdistance3d(
								object->world_x -
									g_objectTable[playerObjectIndex]
										.world_x,
								object->world_y -
									g_objectTable[playerObjectIndex]
										.world_y,
								object->world_z -
									g_objectTable[playerObjectIndex]
										.world_z);
							previousDistance = collide_roughdistance3d(
								g_objectTable[objectIndex]
										.mobj
										->prevWorldX -
									g_objectTable[playerObjectIndex]
										.mobj
										->prevWorldX,
								g_objectTable[objectIndex]
										.mobj
										->prevWorldY -
									g_objectTable[playerObjectIndex]
										.mobj
										->prevWorldY,
								g_objectTable[objectIndex]
										.mobj
										->prevWorldZ -
									g_objectTable[playerObjectIndex]
										.mobj
										->prevWorldZ);
							flybyDistance =
								g_objectTypeTable
									[objectType]
										.maxBoundsExtent +
								1024;
							if (flybyDistance >
							    currentDistance) {
								if (previousDistance >=
								    flybyDistance) {
									fsfx_PlaySound(
										flybySoundId,
										objectIndex,
										g_localPlayer);
								}
							}
						}
					}
				}
			}
			++objectIndex;
		} while (g_activeRegionCraftObjectSlotEnd > objectIndex);
	}
}

/* Queues a wingman's radio line, speaker type 1, about targetObjIdx. Returns 0
 * when wingman voices are off, playerIdx is -1 or not g_localPlayer, or the
 * player has no craft; unless probability is 0xFFFF it is halved at wingman
 * voice level 1 and the call returns 0 unless GameRand2() is under it. With
 * speakerObjIdx -1 it picks the speaker with fsfx_RandomIndex among the craft
 * slots from g_activeRegionObjectSlotStart below
 * g_activeRegionCraftObjectSlotEnd that hold a craft of mobile family 0 in the
 * player's flight group not owned by playerIdx, kept in a 6-entry array it does
 * not bound; with none, category 12 queues voice slot 37 twice, and it returns
 * 0. When the pick is the target it returns 0 if it is the only one, else keeps
 * it as the speaker: the next candidate it computes is not used. A given
 * speaker returns 0 unless its slot is below g_activeRegionCraftObjectSlotEnd
 * and, for a category other than 1, it flies in the player's group. The line
 * comes from the speaker's voice list at slot 97 * craftOrdinal + 114;
 * craftIndexInGroup over 6 counts as 0. With responseIndex -1 the category
 * picks the line: 3 and 5, which return 0 unless the voice queue is empty, take
 * fsfx_SelectAvailableVoiceVariant(category, the target's craftOrdinal) and,
 * when craftIndexInGroup is nonzero, count the play and queue the call sign
 * line (category 2) first, then the line chained; 6, which also needs the queue
 * empty, and 9, 10 and 21 take and count a variant; 12 takes
 * fsfx_RandomIndex(4); 16 takes and counts a variant, then chains category 17's
 * line for the target's craftOrdinal; a missing variant returns 0, and other
 * categories queue nothing. With a responseIndex, category 1 queues the call
 * sign (category 0) when craftIndexInGroup is nonzero, category 1's first line
 * when GameRand2() < 0x5555, then the response; 23 queues a random line of
 * category 21 (among the first 5) when GameRand2() < 0x8000, then the response,
 * counted under the speaker's craftOrdinal; other categories queue the
 * response. A response not under its category's variant count returns 0, after
 * the lines queued before it. Counts go to g_fsfxVoiceLinePlayCounts, under the
 * target's craftOrdinal (0 without a target) except for category 23. Returns 1
 * otherwise. */
// FUNCTION: XVT 0x42FB70
int fsfx_SpeakWingmanEvent(int playerIdx, int speakerObjIdx, int voiceCategory,
			   int responseIndex, int targetObjIdx,
			   uint16_t probability)
{
	int speakerVoiceListSlot;
	int playerObjIdx;
	int candidates[6];
	unsigned int craftIndexInGroup;
	int selectedResponse;
	int candidateCount;
	int baseOffset;
	unsigned int candidateIndex;
	unsigned int alternateIndex;
	struct CraftData *craft;
	int craftOrdinal;
	int targetCraftOrdinal;
	uint16_t objectSignature;

	if (g_gameConfig.voicePilotLevel == 0) {
		return 0;
	}
	if (playerIdx == -1) {
		return 0;
	}
	if (g_localPlayer != playerIdx) {
		return 0;
	}
	playerObjIdx = g_players[playerIdx].objectIndex;
	if (playerObjIdx == -1) {
		return 0;
	}
	if (probability != UINT16_MAX) {
		if (g_gameConfig.voicePilotLevel == 1) {
			probability >>= 1;
		}
		if (GameRand2() >= probability) {
			return 0;
		}
	}

	if (speakerObjIdx == -1) {
		candidateCount = 0;
		candidateIndex = g_activeRegionObjectSlotStart;
		if ((unsigned int)g_activeRegionCraftObjectSlotEnd >
		    candidateIndex) {
			do {
				if (g_objectTable[candidateIndex].objectType !=
					    0 &&
				    g_objectTable[candidateIndex]
						    .mobj->family == 0 &&
				    g_objectTable[candidateIndex]
						    .playerOwnerIdx !=
					    playerIdx &&
				    g_objectTable[playerObjIdx]
						    .flightGroupIdx ==
					    g_objectTable[candidateIndex]
						    .flightGroupIdx) {
					candidates[candidateCount++] =
						candidateIndex;
				}
				candidateIndex++;
			} while (
				(unsigned int)g_activeRegionCraftObjectSlotEnd >
				candidateIndex);
		}
		if (candidateCount == 0) {
			if (voiceCategory == 12) {
				fsfx_QueueVoiceSfx(37, 0, 0, 0, UINT16_MAX);
				fsfx_QueueVoiceSfx(37, 0, 0, 0, UINT16_MAX);
			}
			return 0;
		}
		candidateIndex = fsfx_RandomIndex(candidateCount);
		speakerObjIdx = candidates[candidateIndex];
		if (targetObjIdx == speakerObjIdx) {
			alternateIndex = candidateIndex + 1;
			if (alternateIndex >= (unsigned int)candidateCount) {
				alternateIndex = 0;
			}
			if (alternateIndex == candidateIndex) {
				return 0;
			}
		}
	} else {
		if (g_activeRegionCraftObjectSlotEnd <= speakerObjIdx) {
			return 0;
		}
		if (voiceCategory != 1 &&
		    g_objectTable[speakerObjIdx].flightGroupIdx !=
			    g_objectTable[playerObjIdx].flightGroupIdx) {
			return 0;
		}
	}

	craft = g_objectTable[speakerObjIdx].mobj->pCraft;
	craftOrdinal = craft->craftOrdinal;
	craftIndexInGroup = craft->craftIndexInGroup;
	if (craftIndexInGroup > 6) {
		craftIndexInGroup = 0;
	}
	speakerVoiceListSlot = 97 * craftOrdinal + 114;
	targetCraftOrdinal = 0;
	if (targetObjIdx == -1 ||
	    g_activeRegionCraftObjectSlotEnd <= targetObjIdx) {
		objectSignature = UINT16_MAX;
	} else {
		objectSignature = g_objectTable[targetObjIdx].objectSignature;
		targetCraftOrdinal =
			g_objectTable[targetObjIdx].mobj->pCraft->craftOrdinal;
	}
	baseOffset = g_fsfxVoiceCategoryBaseOffset[voiceCategory];

	selectedResponse = responseIndex;
	if (selectedResponse == -1) {
		switch (voiceCategory) {
		case 3:
		case 5:
			if (fsfx_IsVoiceQueueEmpty()) {
				selectedResponse =
					fsfx_SelectAvailableVoiceVariant(
						voiceCategory,
						targetCraftOrdinal);
				if (selectedResponse == -1) {
					return 0;
				}
				if (craftIndexInGroup != 0) {
					g_fsfxVoiceLinePlayCounts
						[selectedResponse +
						 97 * targetCraftOrdinal +
						 baseOffset]++;
					fsfx_QueueVoiceSfx(
						g_fsfxVoiceCategoryBaseOffset
								[2] +
							craftIndexInGroup +
							speakerVoiceListSlot -
							1,
						1, 2, 0, objectSignature);
				}
				fsfx_QueueVoiceSfx(
					baseOffset + speakerVoiceListSlot +
						selectedResponse,
					1, voiceCategory, 1, objectSignature);
				break;
			}
			return 0;
		case 6:
			if (fsfx_IsVoiceQueueEmpty()) {
				selectedResponse =
					fsfx_SelectAvailableVoiceVariant(
						voiceCategory,
						targetCraftOrdinal);
				if (selectedResponse == -1) {
					return 0;
				}
				g_fsfxVoiceLinePlayCounts
					[selectedResponse +
					 97 * targetCraftOrdinal +
					 baseOffset]++;
				fsfx_QueueVoiceSfx(
					baseOffset + speakerVoiceListSlot +
						selectedResponse,
					1, voiceCategory, 0, objectSignature);
				break;
			}
			return 0;
		case 9:
		case 10:
		case 21:
			selectedResponse = fsfx_SelectAvailableVoiceVariant(
				voiceCategory, targetCraftOrdinal);
			if (selectedResponse == -1) {
				return 0;
			}
			g_fsfxVoiceLinePlayCounts[selectedResponse +
						  97 * targetCraftOrdinal +
						  baseOffset]++;
			fsfx_QueueVoiceSfx(baseOffset + speakerVoiceListSlot +
						   selectedResponse,
					   1, voiceCategory, 0,
					   objectSignature);
			break;
		case 12:
			selectedResponse = fsfx_RandomIndex(4);
			fsfx_QueueVoiceSfx(baseOffset + speakerVoiceListSlot +
						   selectedResponse,
					   1, voiceCategory, 0,
					   objectSignature);
			break;
		case 16:
			selectedResponse = fsfx_SelectAvailableVoiceVariant(
				voiceCategory, targetCraftOrdinal);
			if (selectedResponse == -1) {
				return 0;
			}
			g_fsfxVoiceLinePlayCounts[selectedResponse +
						  97 * targetCraftOrdinal +
						  baseOffset]++;
			fsfx_QueueVoiceSfx(baseOffset + speakerVoiceListSlot +
						   selectedResponse,
					   1, voiceCategory, 0,
					   objectSignature);
			fsfx_QueueVoiceSfx(g_fsfxVoiceCategoryBaseOffset[17] +
						   targetCraftOrdinal +
						   speakerVoiceListSlot,
					   1, 17, 1, objectSignature);
			break;
		default:
			break;
		}
	} else {
		switch (voiceCategory) {
		case 1:
			if (craftIndexInGroup != 0) {
				fsfx_QueueVoiceSfx(
					g_fsfxVoiceCategoryBaseOffset[0] +
						craftIndexInGroup +
						speakerVoiceListSlot - 1,
					1, 0, 0, objectSignature);
			}
			if (GameRand2() < 0x5555) {
				fsfx_QueueVoiceSfx(
					g_fsfxVoiceCategoryBaseOffset[1] +
						speakerVoiceListSlot,
					1, 1, 1, objectSignature);
			}
			if (g_fsfxVoiceCategoryVariantCount[voiceCategory] >
			    selectedResponse) {
				fsfx_QueueVoiceSfx(
					baseOffset + speakerVoiceListSlot +
						selectedResponse,
					1, voiceCategory, 2, objectSignature);
				break;
			}
			return 0;
		case 23:
			if (GameRand2() < 0x8000) {
				int voiceVariant = fsfx_RandomIndex(
					g_fsfxVoiceCategoryVariantCount
						[voiceCategory]);
				fsfx_QueueVoiceSfx(
					g_fsfxVoiceCategoryBaseOffset[21] +
						speakerVoiceListSlot +
						voiceVariant,
					1, 21, 0, objectSignature);
			}
			if (g_fsfxVoiceCategoryVariantCount[voiceCategory] >
			    selectedResponse) {
				fsfx_QueueVoiceSfx(
					speakerVoiceListSlot +
						selectedResponse + baseOffset,
					1, voiceCategory, 1, objectSignature);
				g_fsfxVoiceLinePlayCounts[97 * craftOrdinal +
							  selectedResponse +
							  baseOffset]++;
				break;
			}
			return 0;
		default:
			if (g_fsfxVoiceCategoryVariantCount[voiceCategory] >
			    selectedResponse) {
				fsfx_QueueVoiceSfx(
					baseOffset + speakerVoiceListSlot +
						selectedResponse,
					1, voiceCategory, 0, objectSignature);
				break;
			}
			return 0;
		}
	}
	return 1;
}

/* Queues a tactical officer line, speaker type 2, at voice slot 696 plus the
 * id. Returns 0 when the tactical officer is off, the local player has no
 * craft, or probability is not 0xFFFF and GameRand2() is not under it. Category
 * 1 (status) also returns 0 when objIdx is -1 or not below
 * g_activeRegionCraftObjectSlotEnd, or when
 * g_fsfxDesignationToTacticalMessageId gives 0xFF for the object's flight
 * group's designation code on the player's team; except for messages 27 and 28
 * (destroyed, disabled) it returns 0 when the officer spoke of that object at
 * most 10 mission seconds ago, and otherwise stores the time in
 * g_fsfxTacOfficerLastSpeakSecondsByObj. Designation lines 4 and 5 become 10
 * and 11 for a group of another team. It queues the designation line, then
 * messageId's line chained, both with the object's signature. Categories 2 to 6
 * queue messageId's line alone with signature 0xFFFF; any other category queues
 * nothing. Returns 1 in those cases. */
// FUNCTION: XVT 0x430200
int fsfx_SpeakTacticalOfficerEvent(int voiceCategory, int messageId, int objIdx,
				   uint16_t probability)
{
	int designationMessageId;
	int elapsedSeconds;
	int lastSpeakSeconds;
	int16_t objectSignature;

	if (g_gameConfig.voiceTacticalOfficerLevel == 0) {
		return 0;
	}
	if (g_players[g_localPlayer].objectIndex == -1) {
		return 0;
	}
	if (probability != UINT16_MAX && GameRand2() >= probability) {
		return 0;
	}

	objectSignature = -1;
	if (voiceCategory == TACTICAL_VOICE_STATUS) {
		if (objIdx == -1 ||
		    objIdx >= g_activeRegionCraftObjectSlotEnd) {
			return 0;
		}
		designationMessageId = g_fsfxDesignationToTacticalMessageId
			[g_flightMissionState.runtime.teamFgDesignationCode
				 [(uint16_t)g_players[g_localPlayer].team]
				 [g_objectTable[objIdx].flightGroupIdx]];
		if (designationMessageId == 0xFF) {
			return 0;
		}
		objectSignature = g_objectTable[objIdx].objectSignature;
		elapsedSeconds =
			Mission_ClockToSeconds(g_missionElapsedClock.hours,
					       g_missionElapsedClock.minutes,
					       g_missionElapsedClock.seconds);
		if (messageId != TACTICAL_MSG_DESTROYED &&
		    messageId != TACTICAL_MSG_DISABLED) {
			lastSpeakSeconds =
				g_fsfxTacOfficerLastSpeakSecondsByObj[objIdx];
			if (lastSpeakSeconds != 0 &&
			    (unsigned int)(elapsedSeconds - lastSpeakSeconds) <=
				    10) {
				return 0;
			}
			g_fsfxTacOfficerLastSpeakSecondsByObj[objIdx] =
				elapsedSeconds;
		}
		if ((designationMessageId == 4 || designationMessageId == 5) &&
		    g_missionFlightGroups[g_objectTable[objIdx].flightGroupIdx]
				    .fg.team !=
			    (uint16_t)g_players[g_localPlayer].team) {
			if (designationMessageId == 4) {
				designationMessageId = 10;
			} else {
				designationMessageId = 11;
			}
		}
	}

	switch (voiceCategory) {
	case TACTICAL_VOICE_STATUS:
		fsfx_QueueVoiceSfx(designationMessageId + 696,
				   FLIGHT_VOICE_SPEAKER_TACTICAL, voiceCategory,
				   0, objectSignature);
		fsfx_QueueVoiceSfx(messageId + 696,
				   FLIGHT_VOICE_SPEAKER_TACTICAL, voiceCategory,
				   1, objectSignature);
		break;
	case TACTICAL_VOICE_ORDER:
	case 3:
	case 4:
	case 5:
	case 6:
		fsfx_QueueVoiceSfx(messageId + 696,
				   FLIGHT_VOICE_SPEAKER_TACTICAL, voiceCategory,
				   0, objectSignature);
		break;
	}
	return 1;
}

/* Queues a random line of commander voice category voiceCategory, speaker type
 * 3, signature 0xFFFF: voice slot 804 plus the category's offset plus an index
 * below its count (0 when the count is 0). Returns 1, or 0 when the commander
 * voice is off. A category over 9 queues nothing and still returns 1, after
 * reading the offset table past its 10 entries. objectSignature is ignored. */
// FUNCTION: XVT 0x430420
int fsfx_QueueCommanderVoiceCategory(int voiceCategory, int objectSignature)
{
	int baseOffset;
	uint8_t variantCount;
	uint16_t variantIndex;

	(void)objectSignature;
	if (g_gameConfig.voiceCommanderEnabled == 0) {
		return 0;
	}
	baseOffset = g_commanderVoiceSfxOffsetByCategory[voiceCategory];
	switch (voiceCategory) {
	case 0:
		variantCount =
			g_commanderVoiceVariantCountByCategory[voiceCategory];
		if (variantCount != 0) {
			variantIndex = fsfx_RandomIndex(variantCount);
		} else {
			variantIndex = 0;
		}
		fsfx_QueueVoiceSfx(baseOffset + variantIndex + 804, 3,
				   voiceCategory, 0, -1);
		return 1;
	case 1:
		variantCount =
			g_commanderVoiceVariantCountByCategory[voiceCategory];
		if (variantCount != 0) {
			variantIndex = fsfx_RandomIndex(variantCount);
		} else {
			variantIndex = 0;
		}
		fsfx_QueueVoiceSfx(baseOffset + variantIndex + 804, 3,
				   voiceCategory, 0, -1);
		return 1;
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
		variantCount =
			g_commanderVoiceVariantCountByCategory[voiceCategory];
		if (variantCount != 0) {
			variantIndex = fsfx_RandomIndex(variantCount);
		} else {
			variantIndex = 0;
		}
		fsfx_QueueVoiceSfx(baseOffset + variantIndex + 804, 3,
				   voiceCategory, 0, -1);
		break;
	default:
		break;
	}
	return 1;
}

/* Picks a line of a wingman voice category for a craft ordinal: a random index
 * below the category's variant count from fsfx_RandomIndex. With a repeat
 * threshold of 0 it returns that index. Otherwise, when the line's count in
 * g_fsfxVoiceLinePlayCounts (list 97 * craftOrdinal) has reached the threshold,
 * it steps on through the indexes, wrapping, and returns the first under it, or
 * -1 when every line has reached it. Writes nothing. */
// FUNCTION: XVT 0x430540
int fsfx_SelectAvailableVoiceVariant(int voiceCategory, int craftOrdinal)
{
	int baseOffset;
	int variantCount;
	int variantIndex;
	uint8_t repeatThreshold;
	int remainingVariants;
	int craftListOffset;

	variantCount = g_fsfxVoiceCategoryVariantCount[voiceCategory];
	baseOffset = g_fsfxVoiceCategoryBaseOffset[voiceCategory];
	variantIndex = fsfx_RandomIndex(variantCount);
	repeatThreshold = g_fsfxVoiceCategoryRepeatThreshold[voiceCategory];
	if (repeatThreshold != 0) {
		craftListOffset = craftOrdinal * 97;
		if (g_fsfxVoiceLinePlayCounts[craftListOffset + variantIndex +
					      baseOffset] >= repeatThreshold) {
			remainingVariants = variantCount;
			while (remainingVariants-- != 0) {
				++variantIndex;
				if (variantIndex >= variantCount) {
					variantIndex = 0;
				}
				if (g_fsfxVoiceLinePlayCounts[craftListOffset +
							      variantIndex +
							      baseOffset] <
				    repeatThreshold) {
					return variantIndex;
				}
			}
			return -1;
		}
	}

	return variantIndex;
}

/* Returns an index below count: r % q, where r is a GameRand2() value modulo
 * count and q that value divided by count, so r itself whenever q exceeds r; 0
 * when q is 0. Divides by count without checking it for 0. */
// FUNCTION: XVT 0x4305D0
uint16_t fsfx_RandomIndex(uint16_t count)
{
	uint16_t randomValue;
	uint16_t quotient;

	randomValue = GameRand2();
	quotient = randomValue / count;
	if (quotient == 0) {
		return 0;
	}

	return randomValue % quotient;
}

/* Returns 1 when g_fsfxVoiceQueueCount is 0, else 0. */
// FUNCTION: XVT 0x430600
int fsfx_IsVoiceQueueEmpty(void) { return g_fsfxVoiceQueueCount == 0; }

/* Appends a voice line to the voice queue, the five g_fsfxVoiceQueue arrays,
 * and returns 1. Returns 0, queuing nothing, when
 * g_flightSimSideEffectsSuppressed is set, g_flightConfVoiceEnabled or the
 * voice volume is 0, the slot holds 0 in g_fsfxLoadedBySlot, the queue holds
 * 128, or the line starts a tactical status message (speaker 2, category 1,
 * chainFlag 0) about the same object signature as the last queued line, itself
 * a tactical status line. Does not check sfxSlot against 838. */
// FUNCTION: XVT 0x430610
int fsfx_QueueVoiceSfx(int sfxSlot, char speakerType, char voiceCategory,
		       char chainFlag, uint16_t objectSignature)
{
	int queueIndex;
	uint8_t queueCount;

	if (g_flightSimSideEffectsSuppressed != 0) {
		return 0;
	}
	if (g_flightConfVoiceEnabled == 0) {
		return 0;
	}
	if (g_fsfxLoadedBySlot[sfxSlot] == 0) {
		return 0;
	}
	if (g_gameConfig.voiceVolume == 0) {
		return 0;
	}
	queueCount = g_fsfxVoiceQueueCount;
	if (queueCount == 128) {
		return 0;
	}
	if (queueCount != 0 && speakerType == FLIGHT_VOICE_SPEAKER_TACTICAL &&
	    voiceCategory == TACTICAL_VOICE_STATUS && chainFlag == 0 &&
	    g_fsfxVoiceQueueSpeakerType[queueCount - 1] ==
		    FLIGHT_VOICE_SPEAKER_TACTICAL &&
	    g_fsfxVoiceQueueCategory[queueCount - 1] == TACTICAL_VOICE_STATUS &&
	    g_fsfxVoiceQueueObjectSignature[queueCount - 1] ==
		    objectSignature) {
		return 0;
	}

	queueIndex = g_fsfxVoiceQueueCount;
	g_fsfxVoiceQueueObjectSignature[queueIndex] = objectSignature;
	g_fsfxVoiceQueueSfxSlot[queueIndex] = sfxSlot;
	g_fsfxVoiceQueueSpeakerType[queueIndex] = speakerType;
	g_fsfxVoiceQueueCategory[queueIndex] = voiceCategory;
	g_fsfxVoiceQueueChainFlag[queueIndex] = chainFlag;
	g_fsfxVoiceQueueCount = queueCount + 1;
	return 1;
}

/* Starts the next voice line once the last one ends. Does nothing when
 * g_fsfxLoaded is 0. Prunes the queue with fsfx_PruneStaleVoiceQueueEntries,
 * then returns while the line in g_fsfxCurrentVoiceSfxSlot still plays.
 * Otherwise it sets g_fsfxCurrentVoiceSfxSlot to 0 and, with a line queued,
 * takes the first, copying its speaker type, category, chain flag and signature
 * into the g_fsfxCurrentVoice globals, moves the rest up one and lowers
 * g_fsfxVoiceQueueCount; when that line's sound loaded and the voice volume is
 * not 0, it plays it at once with Sound_PlayEffectNow (restart allowed, once,
 * priority 126, centered) at the voice volume setting times 13, 127 from 10 up,
 * and stores its id in g_fsfxCurrentVoiceSfxSlot. */
// FUNCTION: XVT 0x430700
void fsfx_UpdateVoiceQueue(void)
{
	unsigned int queueIndex;
	int sfxSlot;
	uint8_t queueCount;
	int volume;

	if (g_fsfxLoaded == 0) {
		return;
	}
	fsfx_PruneStaleVoiceQueueEntries();
	if (g_fsfxCurrentVoiceSfxSlot != 0 &&
	    Sound_GetParam(g_fsfxCurrentVoiceSfxSlot, 256) != 0) {
		return;
	}

	queueIndex = 0;
	queueCount = g_fsfxVoiceQueueCount;
	g_fsfxCurrentVoiceSfxSlot = 0;
	if (queueCount == 0) {
		return;
	}

	sfxSlot = g_fsfxVoiceQueueSfxSlot[0];
	g_fsfxCurrentVoiceSpeakerType = g_fsfxVoiceQueueSpeakerType[0];
	--queueCount;
	g_fsfxCurrentVoiceCategory = g_fsfxVoiceQueueCategory[0];
	g_fsfxVoiceQueueCount = queueCount;
	g_fsfxCurrentVoiceChainFlag = g_fsfxVoiceQueueChainFlag[0];
	g_fsfxCurrentVoiceObjectSignature = g_fsfxVoiceQueueObjectSignature[0];

	while (queueIndex < queueCount) {
		g_fsfxVoiceQueueSfxSlot[queueIndex] =
			g_fsfxVoiceQueueSfxSlot[queueIndex + 1];
		g_fsfxVoiceQueueSpeakerType[queueIndex] =
			g_fsfxVoiceQueueSpeakerType[queueIndex + 1];
		g_fsfxVoiceQueueCategory[queueIndex] =
			g_fsfxVoiceQueueCategory[queueIndex + 1];
		g_fsfxVoiceQueueChainFlag[queueIndex] =
			g_fsfxVoiceQueueChainFlag[queueIndex + 1];
		g_fsfxVoiceQueueObjectSignature[queueIndex] =
			g_fsfxVoiceQueueObjectSignature[queueIndex + 1];
		++queueIndex;
	}

	if (g_fsfxLoadedBySlot[sfxSlot] == 0 || g_gameConfig.voiceVolume == 0) {
		return;
	}
	volume = 127;
	if (g_gameConfig.voiceVolume < 10) {
		volume = 13 * g_gameConfig.voiceVolume;
	}
	Sound_PlayEffectNow(g_fsfxSfxNameTable[sfxSlot], 1, 0, 126, volume, 64);
	g_fsfxCurrentVoiceSfxSlot = sfxSlot;
}

/* Drops queued tactical status messages about objects that are gone. Each
 * queued line that starts one (chainFlag 0, speaker 2, category 1) and whose
 * next line's id is not 723 or 732 is kept only while a craft slot from
 * g_activeRegionObjectSlotStart below g_activeRegionCraftObjectSlotEnd holds an
 * object with its signature that has a mobile object and craft and is not
 * breaking up or exploding; otherwise fsfx_RemoveVoiceQueueEntryChain removes
 * it with its chained lines and the same index is looked at again. When the
 * 128th queued line starts such a message it reads one entry past the end of
 * g_fsfxVoiceQueueSfxSlot. */
// FUNCTION: XVT 0x430830
void fsfx_PruneStaleVoiceQueueEntries(void)
{
	unsigned int queueIndex;
	int referencedObjectFound;
	unsigned int objectIndex;
	unsigned int objectSlotEnd;

	queueIndex = 0;
	if (g_fsfxVoiceQueueCount == 0) {
		return;
	}
	while (g_fsfxVoiceQueueCount > queueIndex) {
		if (g_fsfxVoiceQueueChainFlag[queueIndex] != 0 ||
		    g_fsfxVoiceQueueSpeakerType[queueIndex] !=
			    FLIGHT_VOICE_SPEAKER_TACTICAL ||
		    g_fsfxVoiceQueueCategory[queueIndex] !=
			    TACTICAL_VOICE_STATUS ||
		    g_fsfxVoiceQueueSfxSlot[queueIndex + 1] == 723 ||
		    g_fsfxVoiceQueueSfxSlot[queueIndex + 1] == 732) {
			++queueIndex;
			continue;
		}

		objectIndex = g_activeRegionObjectSlotStart;
		objectSlotEnd = g_activeRegionCraftObjectSlotEnd;
		referencedObjectFound = 0;
		if (objectIndex < objectSlotEnd) {
			do {
				if (g_objectTable[objectIndex]
						    .objectSignature ==
					    (uint16_t)
						    g_fsfxVoiceQueueObjectSignature
							    [queueIndex] &&
				    g_objectTable[objectIndex].objectType !=
					    0 &&
				    g_objectTable[objectIndex].mobj != NULL &&
				    g_objectTable[objectIndex].mobj->pCraft !=
					    NULL &&
				    g_objectTable[objectIndex]
						    .mobj->pCraft->objectKind !=
					    CRAFT_OBJECT_KIND_BREAKING_UP &&
				    g_objectTable[objectIndex]
						    .mobj->pCraft->objectKind !=
					    CRAFT_OBJECT_KIND_EXPLODING) {
					referencedObjectFound = 1;
					break;
				}
				++objectIndex;
			} while (objectIndex < objectSlotEnd);
		}

		if (referencedObjectFound != 0) {
			++queueIndex;
		} else {
			fsfx_RemoveVoiceQueueEntryChain(queueIndex);
		}
	}
}

/* Removes queued voice line queueIndex and the lines chained after it (each
 * following line with a nonzero chain flag, up to the first with 0), moves the
 * later lines up and lowers g_fsfxVoiceQueueCount by the number removed. Does
 * not check that queueIndex is queued. */
// FUNCTION: XVT 0x430930
void fsfx_RemoveVoiceQueueEntryChain(unsigned int queueIndex)
{
	int removedCount;
	int *destinationSfxSlot;
	uint8_t chainFlag;
	unsigned int scanIndex;
	unsigned int sourceIndex;
	int *sourceSfxSlot;
	uint16_t *destinationSignature;
	uint16_t *sourceSignature;
	unsigned int destinationIndex;

	removedCount = 1;
	destinationIndex = queueIndex;
	chainFlag = g_fsfxVoiceQueueChainFlag[queueIndex + 1];
	scanIndex = queueIndex + 1;
	if (chainFlag != 0) {
		while (scanIndex < g_fsfxVoiceQueueCount) {
			chainFlag = g_fsfxVoiceQueueChainFlag[scanIndex + 1];
			++removedCount;
			++scanIndex;
			if (chainFlag == 0) {
				break;
			}
		}
	}

	/* From here chainFlag holds the shrunken queue count, which also ends the copy loop below. */
	chainFlag = g_fsfxVoiceQueueCount;
	chainFlag -= (uint8_t)removedCount;
	g_fsfxVoiceQueueCount = chainFlag;
	if (queueIndex >= chainFlag) {
		return;
	}

	sourceIndex = queueIndex + removedCount;
	destinationSignature = &g_fsfxVoiceQueueObjectSignature[queueIndex];
	sourceSignature = &g_fsfxVoiceQueueObjectSignature[sourceIndex];
	sourceSfxSlot = &g_fsfxVoiceQueueSfxSlot[sourceIndex];
	destinationSfxSlot = &g_fsfxVoiceQueueSfxSlot[queueIndex];
	while (1) {
		*destinationSfxSlot++ = *sourceSfxSlot++;
		g_fsfxVoiceQueueSpeakerType[destinationIndex] =
			g_fsfxVoiceQueueSpeakerType[destinationIndex +
						    removedCount];
		g_fsfxVoiceQueueCategory[destinationIndex] =
			g_fsfxVoiceQueueCategory[destinationIndex +
						 removedCount];
		g_fsfxVoiceQueueChainFlag[destinationIndex] =
			g_fsfxVoiceQueueChainFlag[destinationIndex +
						  removedCount];
		*destinationSignature++ = *sourceSignature++;
		++destinationIndex;
		if (destinationIndex >= chainFlag) {
			break;
		}
	}
}
