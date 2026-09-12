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

// GLOBAL: XVT 0x520F18
uint16_t g_fsfxMinDistanceOrRolloffBySfxSlot[96] = {
	0,     0,    0,     0,     8192, 8192, 8192, 10240, 10240, 10240, 10240, 10240, 12288, 12288,
	12288, 8192, 10240, 10240, 8192, 8192, 8192, 49152, 24576, 24576, 24576, 24576, 24576, 24576,
	8192,  8192, 8192,  8192,  8192, 8192, 8192, 8192,  8192,  8192,  8192,  8192,  8192,  8192,
	8192,  8192, 8192,  8192,  8192, 8192, 8192, 8192,  8192,  8192,  8192,  8192,  8192,  8192,
	8192,  8192, 8192,  8192,  8192, 8192, 8192, 8192,  8192,  8192,  8192,  8192,  8192,  8192,
	8192,  8192, 8192,  8192,  8192, 8192, 8192, 8192,  8192,  8192,  8192,  8192,  8192,  8192,
	8192,  8192, 8192,  8192,  8192, 8192, 8192, 8192,  8192,  8192,  8192,  8192,
};
// GLOBAL: XVT 0x520FD8
uint8_t g_fsfxBaseVolumeBySfxSlot[96] = {
	0,   0,   0,   0,   72,  72,  96,  112, 80,  80,  80,  80,  96,  96,  96,  72,  80,  80,  111, 111,
	127, 127, 127, 127, 127, 127, 127, 127, 88,  127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
	127, 80,  127, 95,  56,  56,  56,  56,  56,  127, 127, 127, 72,  56,  88,  72,  56,  72,  56,  56,
	56,  56,  112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 127, 127,
	112, 112, 112, 112, 112, 112, 96,  96,  112, 112, 127, 127, 64,  127, 127, 112,
};
// GLOBAL: XVT 0x521038
static const uint8_t g_fsfxVoiceCategoryBaseOffset[24] = { 0x00, 0x06, 0x0E, 0x14, 0x18, 0x19, 0x1B, 0x1D,
														   0x1F, 0x20, 0x28, 0x2D, 0x2E, 0x38, 0x39, 0x3A,
														   0x3C, 0x40, 0x46, 0x47, 0x4C, 0x52, 0x5A, 0x5C };
// GLOBAL: XVT 0x521050
static const uint8_t g_fsfxVoiceCategoryVariantCount[24] = { 0x06, 0x08, 0x06, 0x04, 0x01, 0x02, 0x02, 0x01,
															 0x01, 0x08, 0x05, 0x01, 0x05, 0x01, 0x01, 0x02,
															 0x04, 0x06, 0x01, 0x05, 0x06, 0x08, 0x02, 0x05 };
// GLOBAL: XVT 0x521068
static const uint8_t g_fsfxVoiceCategoryRepeatThreshold[24] = { 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
																0x01, 0x01, 0x01, 0x02, 0x02, 0x01,
																0x01, 0x02, 0x01, 0x01, 0x02, 0x00,
																0x01, 0x01, 0x00, 0x02, 0x01, 0x01 };
// GLOBAL: XVT 0x521080
static const uint8_t g_fsfxDesignationToVoiceVariant[24] = { 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
															 0x07, 0x08, 0x09, 0x0D, 0x0E, 0xFF, 0xFF, 0xFF,
															 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 };
// GLOBAL: XVT 0x521098
static const uint8_t g_commanderVoiceSfxOffsetByCategory[10] = { 0x00, 0x02, 0x0A, 0x0C, 0x0D,
																 0x0E, 0x12, 0x14, 0x1A, 0x1E };
// GLOBAL: XVT 0x5210A8
static const uint8_t g_commanderVoiceVariantCountByCategory[10] = { 0x02, 0x08, 0x02, 0x00, 0x00,
																	0x04, 0x02, 0x06, 0x04, 0x04 };
// GLOBAL: XVT 0x9ECC52
uint8_t g_fsfxLoaded = 0;
// GLOBAL: XVT 0x9A20A4
uint8_t g_fsfxVoiceQueueCount = 0;
// GLOBAL: XVT 0xA0A870
static uint8_t g_fsfxCurrentVoiceSpeakerType = 0;
// GLOBAL: XVT 0xA0A880
uint8_t g_fsfxVoiceQueueChainFlag[128] = { 0 };
// GLOBAL: XVT 0xA0A900
static uint8_t g_fsfxCurrentVoiceCategory = 0;
// GLOBAL: XVT 0xA0A910
uint16_t g_fsfxVoiceQueueObjectSerial[128] = { 0 };
// GLOBAL: XVT 0xA0A7F0
static int g_fsfxTacOfficerLastSpeakSecondsByObj[136] = { 0 };
// GLOBAL: XVT 0xA0AB20
char g_fsfxSfxLoadPath[720] = { 0 };
// GLOBAL: XVT 0x523448
char g_currentMissionFile[128] = "DEMO.TIE";
// GLOBAL: XVT 0xA0AA20
uint8_t g_fsfxVoiceQueueSpeakerType[128] = { 0 };
// GLOBAL: XVT 0xA0AAA0
uint8_t g_fsfxVoiceQueueCategory[128] = { 0 };
// GLOBAL: XVT 0xA0AA10
static int g_fsfxCurrentVoiceSfxSlot = 0;
// GLOBAL: XVT 0xA0ABA0
static uint8_t g_fsfxVoiceLinePlayCounts[6 * 97] = { 0 };
// GLOBAL: XVT 0xA0ADF0
char g_fsfxSfxNameTable[838][24] = { { 0 } };
// GLOBAL: XVT 0xA0FE80
uint16_t g_fsfxLoadedBySlot[838] = { 0 };
// GLOBAL: XVT 0xA0FC80
int g_fsfxVoiceQueueSfxSlot[128] = { 0 };
// GLOBAL: XVT 0xA1050C
static uint16_t g_fsfxCurrentVoiceObjectSerial = 0;
// GLOBAL: XVT 0xA1050E
static uint8_t g_fsfxCurrentVoiceChainFlag = 0;
// GLOBAL: XVT 0x556350
uint8_t g_playerEngineLoopObjectType = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x42DE40
int fsfx_ClearSfxNameTable(void) {
	memset(g_fsfxSfxNameTable, 0, sizeof(g_fsfxSfxNameTable));
	return 0;
}

// FUNCTION: XVT 0x42DE70
void fsfx_UnloadAllEffects_Thunk(void) { Sound_UnloadAllEffects(); }

// FUNCTION: XVT 0x42DE80
void fsfx_ResetFlightSfxState(void) {
	unsigned int objectIndex;
	unsigned int voiceOffset;

	memset(g_fsfxLoadedBySlot, 0, sizeof(g_fsfxLoadedBySlot));
	for (voiceOffset = 0; voiceOffset < sizeof(g_fsfxVoiceLinePlayCounts); voiceOffset += 97)
		memset(&g_fsfxVoiceLinePlayCounts[voiceOffset], 0, 97);
	for (objectIndex = 0; objectIndex < (unsigned int)g_activeRegionCraftObjectSlotEnd; ++objectIndex)
		g_fsfxTacOfficerLastSpeakSecondsByObj[objectIndex] = 0;
	g_fsfxVoiceQueueCount = 0;
	g_fsfxCurrentVoiceSfxSlot = 0;
}

// FUNCTION: XVT 0x42DED0
int fsfx_LoadSfxList(char* fileNameBuffer, uint16_t firstSoundId) {
	XvtFile* stream;
	char buffer[256];
	char* lineEnd;
	uint16_t loadedCount = 0;

	if (File_OpenGlobalStream(fileNameBuffer, "rb", 0, 0) == 0)
		return 0;
	stream = (XvtFile*)g_stream;
	while (File_Gets(buffer, sizeof(buffer), stream) != NULL) {
		lineEnd = buffer;
		while (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n')
			++lineEnd;
		*lineEnd = '\0';
		if (buffer[0] == '\0')
			continue;
		if (g_flightConfVoiceEnabled == 0 && firstSoundId > 0x5fu) {
			++firstSoundId;
			continue;
		}

		strcpy(g_fsfxSfxLoadPath, "wave\\");
		strcat(g_fsfxSfxLoadPath, buffer);
		strcpy(g_fsfxSfxNameTable[firstSoundId], buffer);
		g_fsfxLoadedBySlot[firstSoundId] =
			(uint16_t)Sound_LoadEffect(g_fsfxSfxLoadPath, g_fsfxSfxNameTable[firstSoundId]);
		++firstSoundId;
		FlightLoading_PulseAndDrawProgressScreen();
		g_fsfxLoaded = 1;
		++loadedCount;
	}
	FeDiskIo_CloseGlobalStream(0);
	return loadedCount;
}

// FUNCTION: XVT 0x42E070
void fsfx_LoadMissionVoiceSfx(void) {
	char listName[64];
	char missionFileName[64];
	char path[64];
	int numberOfCraft;
	unsigned int playerGroup;
	int activeGroupCount;
	uint16_t variant;
	int firstSoundId;
	int missionFileNameLength;
	int missionFileNameStart;
	int pathLength;

	if (g_flightConfVoiceEnabled == 0 || g_gameConfig.voiceVolume == 0)
		return;
	if (g_gameConfig.voiceTacticalOfficerEnabled != 0) {
		strcpy(path, "wave\\");
		if (g_players[g_localPlayer].iff == 0) {
			if ((GameRand2() & 1) != 0)
				strcat(path, "RTO1.LST");
			else
				strcat(path, "RTO2.LST");
		} else {
			if ((GameRand2() & 1) != 0)
				strcat(path, "ITO1.LST");
			else
				strcat(path, "ITO2.LST");
		}
		fsfx_LoadSfxList(path, 0x2b8u);
	}
	if (g_gameConfig.voiceCommanderEnabled != 0) {
		strcpy(path, "wave\\");
		if (g_players[g_localPlayer].iff == 0)
			strcat(path, "RCMD.LST");
		else if (g_players[g_localPlayer].iff == 1)
			strcat(path, "ICMD.LST");
		else
			strcat(path, "PCMD.LST");
		fsfx_LoadSfxList(path, 0x324u);
	}
	if (g_gameConfig.voicePilotEnabled != 0) {
		activeGroupCount = 0;
		for (playerGroup = 0; (int)playerGroup < g_missionHeader.numFlightGroups; ++playerGroup) {
			if (g_missionFlightGroups[playerGroup].fg.playerNumber != 0)
				++activeGroupCount;
		}
		for (playerGroup = 0; (int)playerGroup < g_missionHeader.numFlightGroups; ++playerGroup) {
			if (g_missionFlightGroups[playerGroup].playerOwnerIdx == g_localPlayer)
				break;
		}
		if ((int)playerGroup < g_missionHeader.numFlightGroups &&
			(g_missionFlightGroups[playerGroup].fg.numberOfCraft > 1 || activeGroupCount > 1)) {
			numberOfCraft = g_missionFlightGroups[playerGroup].fg.numberOfCraft;
			variant = fsfx_RandomIndex(6);
			if (numberOfCraft-- != 0) {
				firstSoundId = 114;
				do {
					strcpy(path, "wave\\");
					if (g_players[g_localPlayer].iff == 1)
						strcpy(listName, "ISP1.LST");
					else
						strcpy(listName, "RSP1.LST");
					listName[3] = (char)('1' + variant);
					strcat(path, listName);
					fsfx_LoadSfxList(path, (uint16_t)firstSoundId);
					++variant;
					firstSoundId += 97;
					if (variant >= 6)
						variant = 0;
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
		while (missionFileName[missionFileNameStart] != '\\' && missionFileNameStart < missionFileNameLength)
			++missionFileNameStart;
		if (missionFileNameStart < missionFileNameLength)
			++missionFileNameStart;
		else
			missionFileNameStart = 0;
		pathLength = strlen(path);
		if (missionFileNameStart < missionFileNameLength) {
			missionFileNameLength -= missionFileNameStart;
			memcpy(&path[pathLength], &missionFileName[missionFileNameStart], missionFileNameLength);
			pathLength += missionFileNameLength;
		}
		path[pathLength] = '\0';
		fsfx_LoadSfxList(path, 0x5fu);
	}
}

// FUNCTION: XVT 0x42E460
void fsfx_StopHyperZoomImp(int playerIdx) {
	if (g_flightSimSideEffectsSuppressed != 0)
		return;
	if (g_localPlayer != playerIdx)
		return;
	if (g_flightConfSfxEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorVolume == 0)
		return;
	if (Sound_GetParam(FLIGHT_SOUND_HYPERSPACE_EXIT_ALLIANCE, 256) != 0)
		Sound_StopOldestInstanceById(FLIGHT_SOUND_HYPERSPACE_EXIT_ALLIANCE);
	if (Sound_GetParam(FLIGHT_SOUND_HYPERSPACE_EXIT_EMPIRE, 256) != 0)
		Sound_StopOldestInstanceById(FLIGHT_SOUND_HYPERSPACE_EXIT_EMPIRE);
}

// FUNCTION: XVT 0x42E4D0
int fsfx_PlaySound(unsigned int soundId, int objOrMissionPointRef, int playerIdx) {
	int objectIndex;
	int objectType;
	int priority;
	int pan;
	ObjectRecord* sourceObject;
	MobileObject* sourceMobileObject;
	int volume;

	if (g_flightSfxSideEffectGate != 0) {
		if (g_flightSimSideEffectsSuppressed == 0)
			return 0;
		if (g_flightSfxSideEffectGate == 2)
			return 0;
	} else if (g_flightSimSideEffectsSuppressed != 0) {
		return 0;
	}
	if (playerIdx != g_localPlayer)
		return 0;
	if (g_flightConfSfxEnabled == 0)
		return 0;
	if (g_fsfxLoadedBySlot[soundId] == 0)
		return 0;

	if (objOrMissionPointRef == -1) {
		if (g_gameConfig.sfxInteriorEnabled == 0)
			return 0;
		if (g_gameConfig.sfxInteriorVolume == 0)
			return 0;
	} else {
		if (g_gameConfig.sfxExteriorEnabled == 0)
			return 0;
		if (g_gameConfig.sfxExteriorVolume == 0)
			return 0;
	}

	if (soundId >= FLIGHT_SOUND_R2_HAPPY && soundId <= FLIGHT_SOUND_R2_HIT) {
		objectIndex = g_players[playerIdx].objectIndex;
		if (objectIndex == -1)
			return 0;
		objectType = g_objectTable[objectIndex].objectType;
		if (objectType != 1 && objectType != 2)
			return 0;
	}
	if (soundId == FLIGHT_SOUND_HYPERSPACE_EXIT_ALLIANCE)
		Sound_StopOldestInstanceById(FLIGHT_SOUND_HYPERSPACE_ENTER_ALLIANCE);
	if (soundId == FLIGHT_SOUND_HYPERSPACE_EXIT_EMPIRE)
		Sound_StopOldestInstanceById(FLIGHT_SOUND_HYPERSPACE_ENTER_EMPIRE);

	volume = fsfx_ComputeSourceVolume(objOrMissionPointRef, soundId);
	if (volume != 0) {
		pan = fsfx_ComputeSourcePan(objOrMissionPointRef, &volume);
		priority = 124;
		if ((unsigned int)volume < 125)
			priority = volume;

		if (objOrMissionPointRef == -1 ||
			g_objectTable[objOrMissionPointRef].playerOwnerIdx == g_localPlayer) {
			priority = soundId == FLIGHT_SOUND_DANGER_WARNING ? 127 : 125;
		} else {
			sourceObject = &g_objectTable[objOrMissionPointRef];
			sourceMobileObject = sourceObject->mobj;
			if (sourceMobileObject != NULL &&
				g_objectTable[sourceMobileObject->sourceObjIdx].playerOwnerIdx == g_localPlayer)
				priority = 125;
		}

		if (Sound_GetParam(soundId, 256) != 0 && soundId >= FLIGHT_SOUND_TIE_FLYBY &&
			soundId <= FLIGHT_SOUND_ENGINE_WASH_OTHER)
			return 0;
		Sound_QueueEffect(g_fsfxSfxNameTable[soundId], 1, 0, priority, volume, pan);
	}

	return 1;
}

// FUNCTION: XVT 0x42E720
int fsfx_triggerweaponsfx(unsigned int objOrMissionPointRef, int playerIdx) {
	int result;

	if (playerIdx != g_localPlayer)
		return 0;
	if (g_flightConfSfxEnabled == 0)
		return 0;
	if (g_gameConfig.sfxExteriorEnabled == 0)
		return 0;
	if (g_gameConfig.sfxExteriorVolume == 0)
		return 0;

	result = g_objectTable[objOrMissionPointRef].objectType;
	switch (g_objectTable[objOrMissionPointRef].objectType) {
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
			result = fsfx_PlaySound(result - 133, objOrMissionPointRef, playerIdx);
			break;
		case 0x94:
		case 0x95:
			result = fsfx_PlaySound(result - 138, objOrMissionPointRef, playerIdx);
			break;
		case 0x96:
		case 0x97:
			result = fsfx_PlaySound(result - 135, objOrMissionPointRef, playerIdx);
			break;
		case 0x98:
		case 0x99:
		case 0x9a:
		case 0x9b:
			result = fsfx_PlaySound(FLIGHT_SOUND_MAGNETIC_PULSE, objOrMissionPointRef, playerIdx);
			break;
	}
	return result;
}

// FUNCTION: XVT 0x42E810
unsigned int fsfx_ComputeSourceVolume(int objOrMissionPointRef, unsigned int soundId) {
	unsigned int volumeScale;
	unsigned int minDistance;
	unsigned int baseVolume;
	unsigned int distance;
	unsigned int scaledDistance;
	unsigned int quarterVolume;
	unsigned int distanceSpan;
	unsigned int volumeRange;
	unsigned int volume;
	PlayerData* listener;
	int deltaX;
	int deltaY;
	int worldZ;

	if (objOrMissionPointRef == -1) {
		volumeScale = g_gameConfig.sfxInteriorVolume;
		if (volumeScale >= 10) {
			volumeScale = 127;
		} else {
			volumeScale *= 13;
		}
		return volumeScale * g_fsfxBaseVolumeBySfxSlot[soundId] / 127;
	}

	if (soundId >= 95) {
		minDistance = 8192;
		baseVolume = 112;
	} else {
		minDistance = g_fsfxMinDistanceOrRolloffBySfxSlot[soundId];
		baseVolume = g_fsfxBaseVolumeBySfxSlot[soundId];
	}
	if (g_objectTable[objOrMissionPointRef].mobj != NULL) {
		listener = &g_players[g_localPlayer];
		deltaX = g_objectTable[objOrMissionPointRef].mobj->prevWorldX - listener->viewState.savedTargetX;
		deltaY = g_objectTable[objOrMissionPointRef].mobj->prevWorldY - listener->viewState.savedTargetY;
		worldZ = g_objectTable[objOrMissionPointRef].mobj->prevWorldZ;
	} else {
		Mission_ResolveObjectOrMissionPointWorldLoc(objOrMissionPointRef, 0);
		listener = &g_players[g_localPlayer];
		deltaX = worldlocx - listener->viewState.savedTargetX;
		deltaY = worldlocy - listener->viewState.savedTargetY;
		worldZ = worldlocz;
	}
	distance = collide_roughdistance3d(deltaX, deltaY, worldZ - listener->viewState.savedTargetZ);
	scaledDistance = distance >> 2;
	if (scaledDistance >= minDistance)
		return 0;
	scaledDistance = distance >> 1;
	if (scaledDistance >= minDistance)
		return baseVolume >> 3;
	if (distance >= minDistance)
		return baseVolume >> 2;

	quarterVolume = baseVolume >> 2;
	distanceSpan = minDistance - distance;
	volumeRange = baseVolume - quarterVolume;
	volume = quarterVolume + distanceSpan * volumeRange / (minDistance - (minDistance >> 5));
	if (g_gameConfig.sfxExteriorVolume != 10) {
		volume = volume * g_gameConfig.sfxExteriorVolume / 10;
	}
	if (volume > 127)
		volume = 127;
	return volume;
}

// FUNCTION: XVT 0x42E9A0
int fsfx_ComputeSourcePan(int objOrMissionPointRef, int* volume) {
	MobileObject* sourceMobileObject;
	int dx;
	int dy;
	int dz;
	int16_t angleY;
	int16_t angleX;
	int16_t panAngle;

	if (objOrMissionPointRef == -1)
		return 64;

	sourceMobileObject = g_objectTable[objOrMissionPointRef].mobj;
	if (sourceMobileObject != NULL) {
		dx = sourceMobileObject->prevWorldX - g_players[g_localPlayer].viewState.savedTargetX;
		dy = sourceMobileObject->prevWorldY - g_players[g_localPlayer].viewState.savedTargetY;
		dz = sourceMobileObject->prevWorldZ - g_players[g_localPlayer].viewState.savedTargetZ;
	} else {
		Mission_ResolveObjectOrMissionPointWorldLoc(objOrMissionPointRef, 0);
		dz = worldlocz - g_players[g_localPlayer].viewState.savedTargetZ;
		dx = worldlocx - g_players[g_localPlayer].viewState.savedTargetX;
		dy = worldlocy - g_players[g_localPlayer].viewState.savedTargetY;
	}

	angleY = (int16_t)Math_Dot3Q15Wrapped((int16_t)dx, (int16_t)dy, (int16_t)dz, g_camMatR0_X, g_camMatR0_Y,
										  g_camMatR0_Z);
	angleX = (int16_t)Math_Dot3Q15Wrapped((int16_t)dx, (int16_t)dy, (int16_t)dz, g_camMatR2_X, g_camMatR2_Y,
										  g_camMatR2_Z);
	panAngle = trig2_arctan(angleY, angleX);

	if (panAngle >= 0x4000 || panAngle <= -0x4000) {
		int16_t verticalAngle;
		int16_t rearAngle;
		int16_t rearScale;
		int16_t reduction;

		angleY = (int16_t)Math_Dot3Q15Wrapped((int16_t)dx, (int16_t)dy, (int16_t)dz, g_camMatR1_X,
											  g_camMatR1_Y, g_camMatR1_Z);
		verticalAngle = (int16_t)(0x8000 - trig2_arctan(angleY, angleX));
		rearAngle = (int16_t)(0x8000 - panAngle);
		panAngle = (int16_t)(0x8000 - panAngle);
		if (verticalAngle < 0)
			verticalAngle = (int16_t)-verticalAngle;
		if (panAngle < 0)
			rearAngle = (int16_t)-panAngle;

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
	if (panAngle < -64)
		panAngle = -64;
	if (panAngle > 63)
		panAngle = 63;
	return (int16_t)(panAngle + 64);
}

// FUNCTION: XVT 0x42EC80
int fsfx_UpdateTargetingTone(unsigned int toneState) {
	int volume;
	int interiorVolume;

	if (g_flightConfSfxEnabled == 0)
		return 0;
	if (g_gameConfig.sfxInteriorEnabled == 0)
		return 0;
	if (g_gameConfig.sfxInteriorVolume == 0)
		return 0;

	interiorVolume = g_gameConfig.sfxInteriorVolume;
	volume = 127;
	if (interiorVolume < 10)
		volume = 13 * interiorVolume;

	if (toneState == 0 || toneState == 1) {
		if (Sound_GetParam(51, 256) != 0) {
			Sound_StopOldestInstanceById(51);
			return 1;
		}
		if (Sound_GetParam(50, 256) != 0)
			Sound_StopOldestInstanceById(50);
	} else if (toneState == 3) {
		if (Sound_GetParam(50, 256) != 0)
			Sound_StopOldestInstanceById(50);
		if (Sound_GetParam(51, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[51], 1, 1, 125, volume, 64);
			return 1;
		}
	} else {
		if (Sound_GetParam(51, 256) != 0)
			Sound_StopOldestInstanceById(51);
		if (Sound_GetParam(50, 256) == 0) {
			Sound_QueueEffect(g_fsfxSfxNameTable[50], 1, 1, 125, volume, 64);
			return 1;
		}
	}
	return 1;
}

// FUNCTION: XVT 0x42EDC0
void fsfx_UpdateBeamSystemLoop(int active, int playerIdx) {
	CraftData* craft;
	BeamType beamType;
	int pairedSoundId;
	int soundId;
	int volume;
	int stopSoundId;

	if (g_flightSimSideEffectsSuppressed != 0)
		return;
	if (g_localPlayer != playerIdx)
		return;
	if (g_flightConfSfxEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorVolume == 0)
		return;

	craft = g_objectTable[g_players[g_localPlayer].objectIndex].mobj->pCraft;
	if (active != 0 && (craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
		beamType = craft->beamTypeId;
		if (beamType == BEAM_TYPE_TRACTOR) {
			if (Sound_GetParam(52, 256) != 0)
				return;
			soundId = 53;
		} else if (beamType == BEAM_TYPE_JAMMING) {
			if (Sound_GetParam(55, 256) != 0)
				return;
			soundId = 56;
		} else if (beamType == BEAM_TYPE_DECOY) {
			if (Sound_GetParam(58, 256) == 0 && Sound_GetParam(59, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(-1, 59);
				Sound_QueueEffect(g_fsfxSfxNameTable[59], 1, 1, 125, volume, 64);
			}
			return;
		} else {
			if (Sound_GetParam(60, 256) == 0 && Sound_GetParam(61, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(-1, 61);
				Sound_QueueEffect(g_fsfxSfxNameTable[61], 1, 1, 125, volume, 64);
			}
			return;
		}

		if (g_localBeamTargetObjIdx == UINT16_MAX) {
			pairedSoundId = soundId + 1;
			if (Sound_GetParam(pairedSoundId, 256) != 0)
				Sound_StopOldestInstanceById(pairedSoundId);
			if (Sound_GetParam(soundId, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(-1, soundId);
				Sound_QueueEffect(g_fsfxSfxNameTable[soundId], 1, 1, 125, volume, 64);
			}
		} else {
			if (Sound_GetParam(soundId, 256) != 0)
				Sound_StopOldestInstanceById(soundId);
			pairedSoundId = soundId + 1;
			if (Sound_GetParam(pairedSoundId, 256) == 0) {
				volume = fsfx_ComputeSourceVolume(-1, pairedSoundId);
				Sound_QueueEffect(g_fsfxSfxNameTable[soundId + 1], 1, 1, 125, volume, 64);
			}
		}
		return;
	}

	for (stopSoundId = 52; stopSoundId <= 61; ++stopSoundId) {
		if (Sound_GetParam(stopSoundId, 256) != 0)
			Sound_StopOldestInstanceById(stopSoundId);
	}
}

// FUNCTION: XVT 0x42F030
void fsfx_UpdateIncomingMissileWarning(int warningState) {
	int soundId;
	int volume;
	int interiorVolume;

	if (g_flightConfSfxEnabled && g_gameConfig.sfxInteriorEnabled != 0 &&
		g_gameConfig.sfxInteriorVolume != 0) {
		if (warningState == 0) {
			if (Sound_GetParam(39, 256) != 0)
				Sound_StopOldestInstanceById(39);
			if (Sound_GetParam(40, 256) != 0)
				Sound_StopOldestInstanceById(40);
			return;
		}

		interiorVolume = g_gameConfig.sfxInteriorVolume;
		volume = 127;
		if (interiorVolume < 10)
			volume = 13 * interiorVolume;
		if (warningState == 1) {
			soundId = 40;
			volume /= 3;
		} else {
			soundId = 39;
			volume /= 2;
		}
		if (Sound_GetParam(soundId, 256) == 0)
			Sound_QueueEffect(g_fsfxSfxNameTable[soundId], 1, 1, 125, volume, 64);
	}
}

// FUNCTION: XVT 0x42F110
void fsfx_UpdateChaffLoop(void) {
	int playerObjectIndex;
	ObjectRecord* playerObject;
	CraftData* craft;
	int volume;
	int interiorVolume;

	if (g_flightSimSideEffectsSuppressed != 0)
		return;
	if (g_flightConfSfxEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorVolume == 0)
		return;

	playerObjectIndex = g_players[g_localPlayer].objectIndex;
	if (playerObjectIndex == -1) {
		if (Sound_GetParam(19, 256) != 0)
			Sound_StopOldestInstanceById(19);
		return;
	}

	playerObject = &g_objectTable[playerObjectIndex];
	craft = playerObject->mobj->pCraft;
	if (craft->cmTypeId != COUNTERMEASURE_TYPE_CHAFF)
		return;

	if (g_players[g_localPlayer].regionSessionId == 1) {
		if (Sound_GetParam(19, 256) != 0)
			Sound_StopOldestInstanceById(19);
		return;
	}

	interiorVolume = g_gameConfig.sfxInteriorVolume;
	if (interiorVolume >= 10)
		volume = 127;
	else
		volume = 13 * interiorVolume;
	volume /= 4;

	if (craft->cmTypeId == COUNTERMEASURE_TYPE_CHAFF && craft->chaffActiveTimer != 0) {
		if (Sound_GetParam(19, 256) == 0)
			Sound_QueueEffect(g_fsfxSfxNameTable[19], 1, 1, 125, volume, 64);
	} else if (Sound_GetParam(19, 256) != 0) {
		Sound_StopOldestInstanceById(19);
	}
}

// FUNCTION: XVT 0x42F270
void fsfx_UpdatePlayerEngineLoop(void) {
	int engineSoundId;
	int objectIndex;
	int baseFrequency;
	uint8_t objectType;
	CraftData* craft;
	uint16_t configVolume;
	int frequency;
	int volume;

	if (g_flightSimSideEffectsSuppressed != 0 || g_flightConfSfxEnabled == 0 ||
		g_gameConfig.sfxEngineEnabled == 0 || g_gameConfig.sfxEngineVolume == 0)
		return;

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
		if (g_players[g_localPlayer].regionSessionId != 1) {
			craft = g_objectTable[objectIndex].mobj->pCraft;
			if ((craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_ENGINES) != 0) {
				configVolume = g_gameConfig.sfxEngineVolume;
				if (configVolume >= 10)
					configVolume = 127;
				else
					configVolume *= 13;
				frequency = 55 * (MATH2_divide(craft->throttleSpeed, 0xffff) / 655) + baseFrequency;
				configVolume = (uint16_t)MATH2_fraction(configVolume, craft->throttleSpeed);
				volume = configVolume >> 1;
				if (Sound_GetParam(engineSoundId, 256) == 0) {
					Sound_SetParam(engineSoundId, 1911, frequency);
					Sound_QueueEffect(g_fsfxSfxNameTable[engineSoundId], 1, 1, 125, volume, 64);
					return;
				}
				Sound_SetParam(engineSoundId, 1911, frequency);
				return;
			}
		}
		if (Sound_GetParam(engineSoundId, 256) != 0)
			Sound_StopOldestInstanceById(engineSoundId);
		if (Sound_GetParam(78, 256) != 0)
			Sound_StopOldestInstanceById(78);
		if (Sound_GetParam(79, 256) != 0)
			Sound_StopOldestInstanceById(79);
		return;
	} else {
		if (g_players[g_localPlayer].mapCameraState == 0)
			return;
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
		if (Sound_GetParam(engineSoundId, 256) != 0)
			Sound_StopOldestInstanceById(engineSoundId);
		if (Sound_GetParam(78, 256) != 0)
			Sound_StopOldestInstanceById(78);
		if (Sound_GetParam(79, 256) != 0)
			Sound_StopOldestInstanceById(79);
	}
}

// FUNCTION: XVT 0x42F5D0
void fsfx_UpdateBeamEffectLoops(void) {
	int volume;
	int interiorVolume;
	int objectIndex;
	CraftData* craft;

	if (g_flightSimSideEffectsSuppressed != 0)
		return;
	if (g_flightConfSfxEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorEnabled == 0)
		return;
	if (g_gameConfig.sfxInteriorVolume == 0)
		return;

	interiorVolume = g_gameConfig.sfxInteriorVolume;
	volume = 127;
	if (interiorVolume < 10)
		volume = 13 * interiorVolume;

	objectIndex = g_players[g_localPlayer].objectIndex;
	if (objectIndex == -1) {
		if (Sound_GetParam(63, 256) != 0)
			Sound_StopOldestInstanceById(63);
		if (Sound_GetParam(64, 256) != 0)
			Sound_StopOldestInstanceById(64);
		if (Sound_GetParam(65, 256) != 0)
			Sound_StopOldestInstanceById(65);
		if (Sound_GetParam(66, 256) != 0)
			Sound_StopOldestInstanceById(66);
		return;
	}

	craft = g_objectTable[objectIndex].mobj->pCraft;
	if (craft->beamEffectAccum[1] != 0) {
		if (Sound_GetParam(63, 256) == 0)
			Sound_QueueEffect(g_fsfxSfxNameTable[63], 1, 1, 125, volume, 64);
		if (Sound_GetParam(64, 256) == 0 && Sound_GetParam(65, 256) == 0
#ifdef XVT_MODERN
			&& XvtFlightTiming_ReferenceDue()
#endif
			&& GameRand2() < 0x1000) {
			Sound_QueueEffect(g_fsfxSfxNameTable[(GameRand2() & 1) + 63], 1, 1, 125, volume, 64);
		}
	} else {
		if (Sound_GetParam(63, 256) != 0)
			Sound_StopOldestInstanceById(63);
		if (Sound_GetParam(64, 256) != 0)
			Sound_StopOldestInstanceById(64);
		if (Sound_GetParam(65, 256) != 0)
			Sound_StopOldestInstanceById(65);
	}

	if (craft->beamEffectAccum[2] != 0) {
		if (Sound_GetParam(66, 256) == 0)
			Sound_QueueEffect(g_fsfxSfxNameTable[66], 1, 1, 125, volume / 2, 64);
	} else if (Sound_GetParam(66, 256) != 0) {
		Sound_StopOldestInstanceById(66);
	}
}

// FUNCTION: XVT 0x42F810
void fsfx_UpdateFlightSfx(void) {
	int playerObjectIndex;
	int washSoundId;
	int washVolume;
	int objectIndex;
	ObjectRecord* object;
	MobileObject* mobileObject;
	CraftData* craft;
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
	if (g_flightSimSideEffectsSuppressed != 0 || g_flightConfSfxEnabled == 0 ||
		g_gameConfig.sfxEngineEnabled == 0 || g_gameConfig.sfxEngineVolume == 0) {
		return;
	}

	if (g_players[g_localPlayer].engineWashSourceObjIdx != -1) {
		objectType = g_objectTable[(uint16_t)g_players[g_localPlayer].engineWashSourceObjIdx].objectType;
		if (objectType == 51 || objectType == 52 || objectType == 53) {
			washSoundId = 78;
		} else {
			washSoundId = 79;
			if (objectType == 54) {
				washSoundId = 78;
			}
		}
		washVolume = 4 * g_gameConfig.sfxExteriorVolume * g_players[g_localPlayer].engineWashStrength / 10;
		if (washVolume > 127) {
			washVolume = 127;
		}
		if (Sound_GetParam(washSoundId, 256) == 0) {
			msg_emitInFlightMessage(IFMSG_221_YOU_RE_TAKING_DAMAGE_FROM_ENGINE_WASH, g_localPlayer);
			Sound_QueueEffect(g_fsfxSfxNameTable[washSoundId], 1, 1, washVolume, 65, 64);
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
				if (g_objectTable[objectIndex].objectType != 0) {
					mobileObject = object->mobj;
					craft = mobileObject->pCraft;
					if (craft->objectKind == CRAFT_OBJECT_KIND_ACTIVE && craft->workingSubsystems != 0 &&
						mobileObject->speed != 0) {
						flybySoundId = UINT16_MAX;
						objectType = g_objectTable[objectIndex].objectType;
						switch (objectType) {
							case 1:
							case 4:
							case 14:
							case 15:
								flybySoundId = FLIGHT_SOUND_X_WING_FLYBY;
								break;
							case 2:
								flybySoundId = FLIGHT_SOUND_Y_WING_FLYBY;
								break;
							case 3:
							case 13:
								flybySoundId = FLIGHT_SOUND_A_WING_FLYBY;
								break;
							case 5:
							case 6:
							case 7:
							case 8:
							case 9:
								flybySoundId = FLIGHT_SOUND_TIE_FLYBY;
								break;
							case 12:
							case 16:
								flybySoundId = FLIGHT_SOUND_SHUTTLE_FLYBY;
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
								flybySoundId = FLIGHT_SOUND_SHUTTLE_FLYBY;
								break;
							case 38:
							case 39:
								flybySoundId = FLIGHT_SOUND_MILLENNIUM_FALCON_FLYBY;
								break;
						}
						if (flybySoundId != UINT16_MAX) {
							currentDistance = collide_roughdistance3d(
								object->world_x - g_objectTable[playerObjectIndex].world_x,
								object->world_y - g_objectTable[playerObjectIndex].world_y,
								object->world_z - g_objectTable[playerObjectIndex].world_z);
							previousDistance = collide_roughdistance3d(
								g_objectTable[objectIndex].mobj->prevWorldX -
									g_objectTable[playerObjectIndex].mobj->prevWorldX,
								g_objectTable[objectIndex].mobj->prevWorldY -
									g_objectTable[playerObjectIndex].mobj->prevWorldY,
								g_objectTable[objectIndex].mobj->prevWorldZ -
									g_objectTable[playerObjectIndex].mobj->prevWorldZ);
							flybyDistance = g_modelTypeTable[objectType].maxBoundsExtent + 1024;
							if (flybyDistance > currentDistance) {
								if (previousDistance >= flybyDistance) {
									fsfx_PlaySound(flybySoundId, objectIndex, g_localPlayer);
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

// FUNCTION: XVT 0x42FB70
int fsfx_speakorderack(int playerIdx, int speakerObjIdx, int voiceCategory, int responseIndex,
					   int targetObjIdx, uint16_t probability) {
	int messageOffset;
	int playerObjIdx;
	int candidates[6];
	unsigned int craftIndexInGroup;
	int selectedResponse;
	int candidateCount;
	int baseOffset;
	unsigned int candidateIndex;
	unsigned int alternateIndex;
	CraftData* craft;
	int waveNumber;
	int targetWaveNumber;
	uint16_t objectSignature;

	if (g_gameConfig.voicePilotEnabled == 0) {
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
		if (g_gameConfig.voicePilotEnabled == 1) {
			probability >>= 1;
		}
		if (GameRand2() >= probability) {
			return 0;
		}
	}

	if (speakerObjIdx == -1) {
		candidateCount = 0;
		candidateIndex = g_activeRegionObjectSlotStart;
		if ((unsigned int)g_activeRegionCraftObjectSlotEnd > candidateIndex) {
			do {
				if (g_objectTable[candidateIndex].objectType != 0 &&
					g_objectTable[candidateIndex].mobj->state == 0 &&
					g_objectTable[candidateIndex].playerOwnerIdx != playerIdx &&
					g_objectTable[playerObjIdx].flightGroupIdx == g_objectTable[candidateIndex].flightGroupIdx) {
					candidates[candidateCount++] = candidateIndex;
				}
				candidateIndex++;
			} while ((unsigned int)g_activeRegionCraftObjectSlotEnd > candidateIndex);
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
			g_objectTable[speakerObjIdx].flightGroupIdx != g_objectTable[playerObjIdx].flightGroupIdx) {
			return 0;
		}
	}

	craft = g_objectTable[speakerObjIdx].mobj->pCraft;
	waveNumber = craft->waveNumber;
	craftIndexInGroup = craft->craftIndexInGroup;
	if (craftIndexInGroup > 6) {
		craftIndexInGroup = 0;
	}
	messageOffset = 97 * waveNumber + 114;
	targetWaveNumber = 0;
	if (targetObjIdx == -1 || g_activeRegionCraftObjectSlotEnd <= targetObjIdx) {
		objectSignature = UINT16_MAX;
	} else {
		objectSignature = g_objectTable[targetObjIdx].objectSignature;
		targetWaveNumber = g_objectTable[targetObjIdx].mobj->pCraft->waveNumber;
	}
	baseOffset = g_fsfxVoiceCategoryBaseOffset[voiceCategory];

	selectedResponse = responseIndex;
	if (selectedResponse == -1) {
		switch (voiceCategory) {
			case 3:
			case 5:
				if (fsfx_IsVoiceQueueEmpty()) {
					selectedResponse = fsfx_SelectAvailableVoiceVariant(voiceCategory, targetWaveNumber);
					if (selectedResponse == -1) {
						return 0;
					}
					if (craftIndexInGroup != 0) {
						g_fsfxVoiceLinePlayCounts[selectedResponse + 97 * targetWaveNumber + baseOffset]++;
						fsfx_QueueVoiceSfx(g_fsfxVoiceCategoryBaseOffset[2] + craftIndexInGroup +
											   messageOffset - 1,
										   1, 2, 0, objectSignature);
					}
					fsfx_QueueVoiceSfx(baseOffset + messageOffset + selectedResponse, 1, voiceCategory, 1,
									   objectSignature);
					break;
				}
				return 0;
			case 6:
				if (fsfx_IsVoiceQueueEmpty()) {
					selectedResponse = fsfx_SelectAvailableVoiceVariant(voiceCategory, targetWaveNumber);
					if (selectedResponse == -1) {
						return 0;
					}
					g_fsfxVoiceLinePlayCounts[selectedResponse + 97 * targetWaveNumber + baseOffset]++;
					fsfx_QueueVoiceSfx(baseOffset + messageOffset + selectedResponse, 1, voiceCategory, 0,
									   objectSignature);
					break;
				}
				return 0;
			case 9:
			case 10:
			case 21:
				selectedResponse = fsfx_SelectAvailableVoiceVariant(voiceCategory, targetWaveNumber);
				if (selectedResponse == -1) {
					return 0;
				}
				g_fsfxVoiceLinePlayCounts[selectedResponse + 97 * targetWaveNumber + baseOffset]++;
				fsfx_QueueVoiceSfx(baseOffset + messageOffset + selectedResponse, 1, voiceCategory, 0,
								   objectSignature);
				break;
			case 12:
				selectedResponse = fsfx_RandomIndex(4);
				fsfx_QueueVoiceSfx(baseOffset + messageOffset + selectedResponse, 1, voiceCategory, 0,
								   objectSignature);
				break;
			case 16:
				selectedResponse = fsfx_SelectAvailableVoiceVariant(voiceCategory, targetWaveNumber);
				if (selectedResponse == -1) {
					return 0;
				}
				g_fsfxVoiceLinePlayCounts[selectedResponse + 97 * targetWaveNumber + baseOffset]++;
				fsfx_QueueVoiceSfx(baseOffset + messageOffset + selectedResponse, 1, voiceCategory, 0,
								   objectSignature);
				fsfx_QueueVoiceSfx(g_fsfxVoiceCategoryBaseOffset[17] + targetWaveNumber + messageOffset, 1,
								   17, 1, objectSignature);
				break;
			default:
				break;
		}
	} else {
		switch (voiceCategory) {
			case 1:
				if (craftIndexInGroup != 0) {
					fsfx_QueueVoiceSfx(g_fsfxVoiceCategoryBaseOffset[0] + craftIndexInGroup + messageOffset - 1,
									   1, 0, 0, objectSignature);
				}
				if (GameRand2() < 0x5555) {
					fsfx_QueueVoiceSfx(g_fsfxVoiceCategoryBaseOffset[1] + messageOffset, 1, 1, 1,
									   objectSignature);
				}
				if (g_fsfxVoiceCategoryVariantCount[voiceCategory] > selectedResponse) {
					fsfx_QueueVoiceSfx(baseOffset + messageOffset + selectedResponse, 1, voiceCategory, 2,
									   objectSignature);
					break;
				}
				return 0;
			case 23:
				if (GameRand2() < 0x8000) {
					int voiceVariant = fsfx_RandomIndex(g_fsfxVoiceCategoryVariantCount[voiceCategory]);
					fsfx_QueueVoiceSfx(g_fsfxVoiceCategoryBaseOffset[21] + messageOffset + voiceVariant, 1, 21, 0,
									   objectSignature);
				}
				if (g_fsfxVoiceCategoryVariantCount[voiceCategory] > selectedResponse) {
					fsfx_QueueVoiceSfx(messageOffset + selectedResponse + baseOffset, 1, voiceCategory, 1,
									   objectSignature);
					g_fsfxVoiceLinePlayCounts[97 * waveNumber + selectedResponse + baseOffset]++;
					break;
				}
				return 0;
			default:
				if (g_fsfxVoiceCategoryVariantCount[voiceCategory] > selectedResponse) {
					fsfx_QueueVoiceSfx(baseOffset + messageOffset + selectedResponse, 1, voiceCategory, 0,
									   objectSignature);
					break;
				}
				return 0;
		}
	}
	return 1;
}

// FUNCTION: XVT 0x430200
int fsfx_SpeakTacticalOfficerEvent(int voiceCategory, int messageId, int objIdx, uint16_t probability) {
	int voiceVariant;
	int elapsedSeconds;
	int lastSpeakSeconds;
	int16_t objectSignature;

	if (g_gameConfig.voiceTacticalOfficerEnabled == 0) {
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
		if (objIdx == -1 || objIdx >= g_activeRegionCraftObjectSlotEnd) {
			return 0;
		}
		voiceVariant = g_fsfxDesignationToVoiceVariant
			[g_flightMissionState.runtime.teamFgDesignationCode[(uint16_t)g_players[g_localPlayer].playerIff]
															   [g_objectTable[objIdx].flightGroupIdx]];
		if (voiceVariant == 0xFF) {
			return 0;
		}
		objectSignature = g_objectTable[objIdx].objectSignature;
		elapsedSeconds = Mission_GameTimeToSeconds(g_missionElapsedClock.hours, g_missionElapsedClock.minutes,
												   g_missionElapsedClock.seconds);
		if (messageId != TACTICAL_MSG_DESTROYED && messageId != TACTICAL_MSG_DISABLED) {
			lastSpeakSeconds = g_fsfxTacOfficerLastSpeakSecondsByObj[objIdx];
			if (lastSpeakSeconds != 0 && (unsigned int)(elapsedSeconds - lastSpeakSeconds) <= 10) {
				return 0;
			}
			g_fsfxTacOfficerLastSpeakSecondsByObj[objIdx] = elapsedSeconds;
		}
		if ((voiceVariant == 4 || voiceVariant == 5) &&
			g_missionFlightGroups[g_objectTable[objIdx].flightGroupIdx].fg.team !=
				(uint16_t)g_players[g_localPlayer].playerIff) {
			if (voiceVariant == 4) {
				voiceVariant = 10;
			} else {
				voiceVariant = 11;
			}
		}
	}

	switch (voiceCategory) {
		case TACTICAL_VOICE_STATUS:
			fsfx_QueueVoiceSfx(voiceVariant + 696, FLIGHT_VOICE_SPEAKER_TACTICAL, voiceCategory, 0,
							   objectSignature);
			fsfx_QueueVoiceSfx(messageId + 696, FLIGHT_VOICE_SPEAKER_TACTICAL, voiceCategory, 1,
							   objectSignature);
			break;
		case TACTICAL_VOICE_ORDER:
		case 3:
		case 4:
		case 5:
		case 6:
			fsfx_QueueVoiceSfx(messageId + 696, FLIGHT_VOICE_SPEAKER_TACTICAL, voiceCategory, 0,
							   objectSignature);
			break;
	}
	return 1;
}

// FUNCTION: XVT 0x430420
int fsfx_QueueCommanderVoiceCategory(int voiceCategory, int objectSignature) {
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
			variantCount = g_commanderVoiceVariantCountByCategory[voiceCategory];
			if (variantCount != 0) {
				variantIndex = fsfx_RandomIndex(variantCount);
			} else {
				variantIndex = 0;
			}
			fsfx_QueueVoiceSfx(baseOffset + variantIndex + 804, 3, voiceCategory, 0, -1);
			return 1;
		case 1:
			variantCount = g_commanderVoiceVariantCountByCategory[voiceCategory];
			if (variantCount != 0) {
				variantIndex = fsfx_RandomIndex(variantCount);
			} else {
				variantIndex = 0;
			}
			fsfx_QueueVoiceSfx(baseOffset + variantIndex + 804, 3, voiceCategory, 0, -1);
			return 1;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			variantCount = g_commanderVoiceVariantCountByCategory[voiceCategory];
			if (variantCount != 0) {
				variantIndex = fsfx_RandomIndex(variantCount);
			} else {
				variantIndex = 0;
			}
			fsfx_QueueVoiceSfx(baseOffset + variantIndex + 804, 3, voiceCategory, 0, -1);
			break;
		default:
			break;
	}
	return 1;
}

// FUNCTION: XVT 0x430540
int fsfx_SelectAvailableVoiceVariant(int voiceCategory, int waveNumber) {
	int baseOffset;
	int variantCount;
	int variantIndex;
	uint8_t repeatThreshold;
	int remainingVariants;
	int waveOffset;

	variantCount = g_fsfxVoiceCategoryVariantCount[voiceCategory];
	baseOffset = g_fsfxVoiceCategoryBaseOffset[voiceCategory];
	variantIndex = fsfx_RandomIndex(variantCount);
	repeatThreshold = g_fsfxVoiceCategoryRepeatThreshold[voiceCategory];
	if (repeatThreshold != 0) {
		waveOffset = waveNumber * 97;
		if (g_fsfxVoiceLinePlayCounts[waveOffset + variantIndex + baseOffset] >= repeatThreshold) {
			remainingVariants = variantCount;
			while (remainingVariants-- != 0) {
				++variantIndex;
				if (variantIndex >= variantCount) {
					variantIndex = 0;
				}
				if (g_fsfxVoiceLinePlayCounts[waveOffset + variantIndex + baseOffset] < repeatThreshold) {
					return variantIndex;
				}
			}
			return -1;
		}
	}

	return variantIndex;
}

// FUNCTION: XVT 0x4305D0
uint16_t fsfx_RandomIndex(uint16_t count) {
	uint16_t randomValue;
	uint16_t quotient;

	randomValue = GameRand2();
	quotient = randomValue / count;
	if (quotient == 0) {
		return 0;
	}

	return randomValue % quotient;
}

// FUNCTION: XVT 0x430600
int fsfx_IsVoiceQueueEmpty(void) { return g_fsfxVoiceQueueCount == 0; }

// FUNCTION: XVT 0x430610
int fsfx_QueueVoiceSfx(int sfxSlot, char speakerType, char voiceCategory, char chainFlag,
					   uint16_t objectSerial) {
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
		g_fsfxVoiceQueueSpeakerType[queueCount - 1] == FLIGHT_VOICE_SPEAKER_TACTICAL &&
		g_fsfxVoiceQueueCategory[queueCount - 1] == TACTICAL_VOICE_STATUS &&
		g_fsfxVoiceQueueObjectSerial[queueCount - 1] == objectSerial) {
		return 0;
	}

	queueIndex = g_fsfxVoiceQueueCount;
	g_fsfxVoiceQueueObjectSerial[queueIndex] = objectSerial;
	g_fsfxVoiceQueueSfxSlot[queueIndex] = sfxSlot;
	g_fsfxVoiceQueueSpeakerType[queueIndex] = speakerType;
	g_fsfxVoiceQueueCategory[queueIndex] = voiceCategory;
	g_fsfxVoiceQueueChainFlag[queueIndex] = chainFlag;
	g_fsfxVoiceQueueCount = queueCount + 1;
	return 1;
}

// FUNCTION: XVT 0x430700
void fsfx_UpdateVoiceQueue(void) {
	unsigned int queueIndex;
	int sfxSlot;
	uint8_t queueCount;
	int volume;

	if (g_fsfxLoaded == 0) {
		return;
	}
	fsfx_PruneStaleVoiceQueueEntries();
	if (g_fsfxCurrentVoiceSfxSlot != 0 && Sound_GetParam(g_fsfxCurrentVoiceSfxSlot, 256) != 0) {
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
	g_fsfxCurrentVoiceObjectSerial = g_fsfxVoiceQueueObjectSerial[0];

	while (queueIndex < queueCount) {
		g_fsfxVoiceQueueSfxSlot[queueIndex] = g_fsfxVoiceQueueSfxSlot[queueIndex + 1];
		g_fsfxVoiceQueueSpeakerType[queueIndex] = g_fsfxVoiceQueueSpeakerType[queueIndex + 1];
		g_fsfxVoiceQueueCategory[queueIndex] = g_fsfxVoiceQueueCategory[queueIndex + 1];
		g_fsfxVoiceQueueChainFlag[queueIndex] = g_fsfxVoiceQueueChainFlag[queueIndex + 1];
		g_fsfxVoiceQueueObjectSerial[queueIndex] = g_fsfxVoiceQueueObjectSerial[queueIndex + 1];
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

// FUNCTION: XVT 0x430830
void fsfx_PruneStaleVoiceQueueEntries(void) {
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
			g_fsfxVoiceQueueSpeakerType[queueIndex] != FLIGHT_VOICE_SPEAKER_TACTICAL ||
			g_fsfxVoiceQueueCategory[queueIndex] != TACTICAL_VOICE_STATUS ||
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
				if (g_objectTable[objectIndex].objectSignature ==
						(uint16_t)g_fsfxVoiceQueueObjectSerial[queueIndex] &&
					g_objectTable[objectIndex].objectType != 0 && g_objectTable[objectIndex].mobj != NULL &&
					g_objectTable[objectIndex].mobj->pCraft != NULL &&
					g_objectTable[objectIndex].mobj->pCraft->objectKind != CRAFT_OBJECT_KIND_BREAKING_UP &&
					g_objectTable[objectIndex].mobj->pCraft->objectKind != CRAFT_OBJECT_KIND_EXPLODING) {
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

// FUNCTION: XVT 0x430930
void fsfx_RemoveVoiceQueueEntryChain(unsigned int queueIndex) {
	int removedCount;
	int* destinationSfxSlot;
	uint8_t chainFlag;
	unsigned int scanIndex;
	unsigned int sourceIndex;
	int* sourceSfxSlot;
	uint16_t* destinationSerial;
	uint16_t* sourceSerial;
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

	chainFlag = g_fsfxVoiceQueueCount;
	chainFlag -= (uint8_t)removedCount;
	g_fsfxVoiceQueueCount = chainFlag;
	if (queueIndex >= chainFlag) {
		return;
	}

	sourceIndex = queueIndex + removedCount;
	destinationSerial = &g_fsfxVoiceQueueObjectSerial[queueIndex];
	sourceSerial = &g_fsfxVoiceQueueObjectSerial[sourceIndex];
	sourceSfxSlot = &g_fsfxVoiceQueueSfxSlot[sourceIndex];
	destinationSfxSlot = &g_fsfxVoiceQueueSfxSlot[queueIndex];
	while (1) {
		*destinationSfxSlot++ = *sourceSfxSlot++;
		g_fsfxVoiceQueueSpeakerType[destinationIndex] =
			g_fsfxVoiceQueueSpeakerType[destinationIndex + removedCount];
		g_fsfxVoiceQueueCategory[destinationIndex] =
			g_fsfxVoiceQueueCategory[destinationIndex + removedCount];
		g_fsfxVoiceQueueChainFlag[destinationIndex] =
			g_fsfxVoiceQueueChainFlag[destinationIndex + removedCount];
		*destinationSerial++ = *sourceSerial++;
		++destinationIndex;
		if (destinationIndex >= chainFlag) {
			break;
		}
	}
}
