#include "xvt/flight/hud/msg.h"
#include "xvt/assets/file.h"

#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/trig2.h"
#include "xvt/util/memory.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// GLOBAL: XVT 0x5235D0
uint16_t g_messageLogWriteIndex;
// GLOBAL: XVT 0x5235D4
uint16_t g_messageLogTotalCount;
// GLOBAL: XVT 0x5235D8
uint16_t g_messageLogWrapped = 0;
// GLOBAL: XVT 0xA07BE0
uint16_t g_msgArgTable[4];
// GLOBAL: XVT 0x9EC4D0
char g_flightSecondaryObjectNameBuffer[256] = { 0 };
// GLOBAL: XVT 0xA08120
static const void* g_msgPtrs[4];
// GLOBAL: XVT 0x9993FC
HudInFlightMessageRecord* g_messageLogRecords = NULL;
// GLOBAL: XVT 0xA08292
uint16_t g_msgSenderIff = 0;
// GLOBAL: XVT 0x5240B8
const uint8_t g_targetDescDesignationUsesRelationText[24] = { 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1,
															  1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0 };
// GLOBAL: XVT 0x9D7684
uint16_t g_pendingHudMessageVoiceSfxId = 0;
// GLOBAL: XVT 0x9A1840
const char* g_strInFlightMessages[417] = { 0 };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x450550
void msg_writeMessageLogFile(void) {
	char fileName[16];
	int logIndex;
	XvtFile* stream;
	int messageIndex;
	int recordOffset;
	HudInFlightMessageRecord* record;
	int prefix;
	char* text;

	logIndex = 0;
	do {
		sprintf(fileName, "msglog%ld.txt", (long)logIndex);
#ifdef XVT_MODERN
		stream = XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, fileName, "r");
#else
		stream = File_RawOpen(fileName, "r");
#endif
		if (stream == NULL) {
			stream = File_RawOpen(fileName, "a");
			break;
		}
		if (logIndex == 99) {
			if (stream != NULL)
				File_RawClose(stream);
			stream = File_RawOpen(fileName, "a");
			break;
		}
		++logIndex;
	} while (logIndex < 100);

	messageIndex = 0;
	if (stream != NULL) {
		if (g_messageLogWriteIndex > (uint16_t)messageIndex) {
			recordOffset = 0;
			do {
				record = (HudInFlightMessageRecord*)((uint8_t*)g_messageLogRecords + recordOffset);
				prefix = record->text[0];
				text = record->text;
				if (prefix < 9) {
					++text;
					if (prefix == 1 && *text >= '0' && *text <= '3')
						++text;
				}
				recordOffset += sizeof(*record);
				++messageIndex;
				File_Printf(stream, "%s\t%ld:%ld:%ld\n", text, (long)record->clockHour,
							(long)record->clockMinute, (long)record->clockTick);
			} while ((uint16_t)g_messageLogWriteIndex > messageIndex);
		}
		File_RawClose(stream);
	}
}

// FUNCTION: XVT 0x450650
void msg_emitInFlightMessage(InFlightMessageId messageId, int playerIdx) {
	HudInFlightMessageRecord message;
	const uint8_t* templateCursor;
	const char* argumentText;
	uint16_t textLength;
	uint16_t argumentIndex;
	uint16_t argumentValue;
	uint16_t digitCount;
	uint16_t divisor;
	uint16_t digitValue;
	uint16_t outputChar;
	uint16_t remainder;
	uint8_t paneType;
	uint16_t normalizedPaneType;
	uint8_t newQueueCount;
	int digitStarted;

	if (g_flightSimSideEffectsSuppressed != 0 || playerIdx != g_localPlayer) {
		return;
	}

	message.stateOrMessageId = (uint16_t)messageId;
	if (g_flightMissionState.missionTimeLimitMinutes != 0) {
		message.clockWord = (uint16_t)g_missionCountdownClock.subsecondTicks;
		message.clockTick = g_missionCountdownClock.seconds;
		message.clockMinute = g_missionCountdownClock.minutes;
		message.clockHour = g_missionCountdownClock.hours;
	} else {
		message.clockWord = (uint16_t)g_missionElapsedClock.subsecondTicks;
		message.clockTick = g_missionElapsedClock.seconds;
		message.clockMinute = g_missionElapsedClock.minutes;
		message.clockHour = g_missionElapsedClock.hours;
	}
	message.ageTicks = 0;
	message.showCount = 0;
	message.senderIff = g_msgSenderIff;
	if (messageId == IFMSG_207_CODE_01_ARGUMENT || messageId == IFMSG_196_CODE_02_ARGUMENT) {
		message.voiceSfxId = g_pendingHudMessageVoiceSfxId;
	} else {
		message.voiceSfxId = 0;
	}

	textLength = 0;
	argumentIndex = 0;
	templateCursor = (const uint8_t*)g_strInFlightMessages[messageId];
	paneType = *templateCursor;
#ifdef XVT_MODERN
	if (messageId == IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE) {
		message.text[textLength++] = (char)paneType;
		templateCursor = (const uint8_t*)"Mission paused. Press your pause key or button to continue.";
	}
#endif
	while (*templateCursor != '\0' && textLength < sizeof(message.text)) {
		if (*templateCursor == '*') {
			++templateCursor;
			argumentValue = g_msgArgTable[argumentIndex++];
			if (argumentValue < 0x8000) {
				argumentText = g_strInFlightMessages[argumentValue];
			} else {
				argumentText = (const char*)g_msgPtrs[argumentValue & 0x7FFF];
			}
			while (*argumentText != '\0' && textLength < sizeof(message.text)) {
				message.text[textLength++] = *argumentText++;
			}
		} else if (*templateCursor == '&') {
			digitCount = templateCursor[1];
			templateCursor += 2;
			digitStarted = 0;
			remainder = g_msgArgTable[argumentIndex++];
			while (digitCount != 0 && textLength < sizeof(message.text)) {
				divisor = g_flightTextDecimalDivisors[digitCount];
				digitValue = remainder / divisor;
				remainder %= divisor;
				if (digitStarted != 0 || digitCount <= 1 || digitValue != 0) {
					digitStarted = 1;
					if (digitValue > 9) {
						digitValue = 9;
					}
					outputChar = digitValue + '0';
				} else {
					outputChar = ' ';
				}
				if (outputChar != ' ') {
					message.text[textLength++] = (char)outputChar;
				}
				--digitCount;
			}
		} else {
			message.text[textLength++] = (char)*templateCursor++;
		}
	}
	if (textLength >= sizeof(message.text)) {
		message.text[sizeof(message.text) - 1] = '\0';
	} else {
		message.text[textLength] = '\0';
	}

	message.paneType = paneType < 9 ? paneType : 6;
	normalizedPaneType = message.paneType;
	if (g_replayViewMode == 0 && (normalizedPaneType == 2 || normalizedPaneType == 1)) {
		++g_messageLogTotalCount;
		if (++g_messageLogWriteIndex == 300) {
			if (g_radioMessageBackupEnabled != 0) {
				msg_writeMessageLogFile();
			}
			g_messageLogWriteIndex = 0;
			g_messageLogWrapped = 1;
		}
		g_messageLogRecords = (HudInFlightMessageRecord*)Memory_LockHandle(g_messageLogHandle);
		Memory_UnlockHandle(g_messageLogHandle);
		g_messageLogRecords[g_messageLogWriteIndex] = message;
	}

	if (normalizedPaneType == 3 || normalizedPaneType == 4 || normalizedPaneType == 7) {
		if ((g_systemMessageDisplayEnabled != 0 ||
			 messageId == IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF) &&
			(g_systemMessagePane.stateOrMessageId == UINT16_MAX || g_systemMessagePane.paneType != 4 ||
			 normalizedPaneType == 7)) {
			g_systemMessagePane = message;
			Hud_ShowFlightMessagePane((int16_t)normalizedPaneType);
		}
		return;
	}
	if (normalizedPaneType == 8) {
		g_flightGroupMessagePane = message;
		Hud_ShowFlightMessagePane((int16_t)normalizedPaneType);
		return;
	}
	if (g_readyMessagePaneQueue[0].stateOrMessageId == UINT16_MAX) {
		g_readyMessagePaneQueue[0] = message;
		Hud_ShowFlightMessagePane((int16_t)normalizedPaneType);
		return;
	}

	switch (g_readyMessagePaneQueue[0].paneType) {
		case 1:
			if (normalizedPaneType != 2 && normalizedPaneType != 1) {
				Hud_ShiftReadyMessageQueueForReplacement();
				g_readyMessagePaneQueue[0] = message;
				Hud_ShowFlightMessagePane((int16_t)normalizedPaneType);
				break;
			}
			g_readyMessagePaneQueue[g_readyMessageQueueCount + 1] = message;
			newQueueCount = g_readyMessageQueueCount + 1;
			g_readyMessageQueueCount = newQueueCount;
			if (newQueueCount >= 10) {
				g_readyMessageQueueCount = newQueueCount - 1;
			}
			break;

		case 2:
		case 5:
			if (normalizedPaneType == 2) {
				g_readyMessagePaneQueue[g_readyMessageQueueCount + 1] = message;
				newQueueCount = g_readyMessageQueueCount + 1;
				g_readyMessageQueueCount = newQueueCount;
				if (newQueueCount >= 10) {
					g_readyMessageQueueCount = newQueueCount - 1;
				}
			} else {
				Hud_ShiftReadyMessageQueueForReplacement();
				g_readyMessagePaneQueue[0] = message;
				Hud_ShowFlightMessagePane((int16_t)normalizedPaneType);
			}
			break;

		case 3:
		case 6:
		case 7:
		case 8:
			g_readyMessagePaneQueue[0] = message;
			Hud_ShowFlightMessagePane((int16_t)normalizedPaneType);
			break;

		case 4:
			if (normalizedPaneType == 2 || normalizedPaneType == 1) {
				g_readyMessagePaneQueue[g_readyMessageQueueCount + 1] = message;
				newQueueCount = g_readyMessageQueueCount + 1;
				g_readyMessageQueueCount = newQueueCount;
				if (newQueueCount >= 10) {
					g_readyMessageQueueCount = newQueueCount - 1;
				}
			} else {
				g_readyMessagePaneQueue[0] = message;
				Hud_ShowFlightMessagePane((int16_t)normalizedPaneType);
			}
			break;

		default:
			break;
	}
}

// FUNCTION: XVT 0x451940
void msg_reportfgcreation(uint16_t flightGroupIndex, uint16_t modelIndex) {
	int flightGroupIdx;
	uint16_t objectIndex;
	ObjectRecord* object;
	CraftData* craft;
	ObjectRecord* localPlayerObject;
	uint16_t rangeKm;
	uint8_t iff;
	uint16_t numberOfCraft;
	int highDistance;

	flightGroupIdx = flightGroupIndex;
	if (g_missionFlightGroups[flightGroupIdx].fg.arrivalMethod == 0) {
		Mission_ResolveObjectOrMissionPointWorldLoc(0x8000, flightGroupIndex);
	} else {
		objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		while (objectIndex < g_activeRegionCraftObjectSlotEnd) {
			object = &g_objectTable[objectIndex];
			if (object->objectType != 0) {
				craft = object->mobj->pCraft;
				if (object->flightGroupIdx == flightGroupIndex && craft->leader_obj_idx == UINT8_MAX) {
					Mission_ResolveObjectOrMissionPointWorldLoc(objectIndex, flightGroupIndex);
					break;
				}
			}
			++objectIndex;
		}
	}

	if (g_players[g_localPlayer].mapCameraState == 0) {
		localPlayerObject = &g_objectTable[g_players[g_localPlayer].objectIndex];
		trig2_ctop(worldlocx - localPlayerObject->world_x, worldlocy - localPlayerObject->world_y,
				   worldlocz - localPlayerObject->world_z);
	} else {
		trig2_ctop(worldlocx - g_players[g_localPlayer].viewState.savedTargetX,
				   worldlocy - g_players[g_localPlayer].viewState.savedTargetY,
				   worldlocz - g_players[g_localPlayer].viewState.savedTargetZ);
	}
	trig2_polardistance *= 161;
	highDistance = (trig2_polardistance >> 16) & 0xFFFF;
	rangeKm = (uint16_t)((highDistance + 50) / 100);
	if (rangeKm == 0) {
		rangeKm = 1;
	}
	iff = g_missionFlightGroups[flightGroupIdx].fg.iff;
	numberOfCraft = g_missionFlightGroups[flightGroupIdx].fg.numberOfCraft;
	g_msgArgTable[0] = numberOfCraft;
	g_msgSenderIff = iff;
	if ((uint16_t)g_players[g_localPlayer].iff != iff) {
		msg_addMessagePtr(1, g_modelDefs[modelIndex].nameLong);
		g_msgArgTable[2] = rangeKm;
		if (numberOfCraft == 1) {
			msg_emitInFlightMessage(IFMSG_114_NEW_CRAFT_ALERT_ARG_ARG_AT_ARG_KM, g_localPlayer);
		} else {
			msg_emitInFlightMessage(IFMSG_115_NEW_CRAFT_ALERT_ARG_ARG_S_AT_ARG_KM, g_localPlayer);
		}
	} else if (numberOfCraft == 1) {
		msg_addMessagePtr(0, g_modelDefs[modelIndex].nameLong);
		msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
		g_msgArgTable[2] = rangeKm;
		msg_emitInFlightMessage(IFMSG_235_ARG_ARG_ENTERING_AREA_AT_ARG_KM, g_localPlayer);
	} else {
		msg_addMessagePtr(1, g_modelDefs[modelIndex].nameLong);
		msg_addMessagePtr(2, &g_missionFlightGroups[flightGroupIdx]);
		g_msgArgTable[3] = rangeKm;
		msg_emitInFlightMessage(IFMSG_236_ARG_ARG_S_FROM_FG_ARG_ENTERING_AREA_AT_ARG_KM, g_localPlayer);
	}
}

// FUNCTION: XVT 0x451BF0
void msg_addMessagePtr(uint16_t slot, const void* value) {
	g_msgArgTable[slot] = slot + 0x8000;
	g_msgPtrs[slot] = value;
}

// FUNCTION: XVT 0x451C20
void msg_emitCraftMessage(uint16_t objIdx, CraftData* craft, int16_t msgTemplateId) {
	ObjectRecord* object;
	int flightGroupIdx;
	uint16_t craftNumber;

	object = &g_objectTable[objIdx];
	flightGroupIdx = object->flightGroupIdx;
	g_msgSenderIff = (uint8_t)object->mobj->iff;
	msg_addMessagePtr(0, &g_modelDefs[craft->modelIndex]);
	msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
	craftNumber = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(flightGroupIdx, craft);
	if (craftNumber != 0) {
		g_msgArgTable[2] = craftNumber;
		g_msgArgTable[3] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_133_CRAFT_EVENT_WITH_NUMBER, g_localPlayer);
	} else {
		g_msgArgTable[2] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_134_CRAFT_EVENT_WITHOUT_NUMBER, g_localPlayer);
	}
}

// FUNCTION: XVT 0x451D00
void msg_radioMessage(uint16_t senderObjIdx, uint8_t* craftDescriptor, uint16_t commandId,
					  uint16_t responseIndex, int16_t multipleRecipients) {
	int flightGroupIdx;
	uint16_t craftNumber;

	flightGroupIdx = g_objectTable[senderObjIdx].flightGroupIdx;
	g_msgSenderIff = g_missionFlightGroups[flightGroupIdx].fg.iff;
	if (g_players[g_localPlayer].iff != g_msgSenderIff ||
		g_missionFlightGroups[flightGroupIdx].fg.team != g_players[g_localPlayer].playerIff) {
		return;
	}
	if (multipleRecipients != 0) {
		msg_addMessagePtr(0, &g_missionFlightGroups[flightGroupIdx]);
		g_msgArgTable[1] = commandId;
		msg_emitInFlightMessage(IFMSG_269_MESSAGE_ACKNOWLEDGED_FLIGHT_GROUP_ARG_ARG, g_localPlayer);
	} else {
		msg_addMessagePtr(0, &g_modelDefs[craftDescriptor[4]]);
		msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
		craftNumber =
			(uint16_t)Hud_MissionFG_GetCraftNumberIfShown(flightGroupIdx, (CraftData*)craftDescriptor);
		if (craftNumber != 0) {
			g_msgArgTable[2] = craftNumber;
			g_msgArgTable[3] = commandId;
			msg_emitInFlightMessage(IFMSG_147_ROGER_CRAFT_WITH_NUMBER, g_localPlayer);
		} else {
			g_msgArgTable[2] = commandId;
			msg_emitInFlightMessage(IFMSG_148_ROGER_CRAFT_WITHOUT_NUMBER, g_localPlayer);
		}
	}
	fsfx_speakorderack(g_localPlayer, senderObjIdx, 1, responseIndex, senderObjIdx, UINT16_MAX);
}

// FUNCTION: XVT 0x451E70
void msg_reportmessage(uint16_t objIdx, CraftData* craft, int16_t msgTemplateId) {
	int flightGroupIdx;
	uint16_t craftNumber;

	flightGroupIdx = g_objectTable[objIdx].flightGroupIdx;
	g_msgSenderIff = g_missionFlightGroups[flightGroupIdx].fg.iff;
	msg_addMessagePtr(0, &g_modelDefs[craft->modelIndex]);
	msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
	craftNumber = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(flightGroupIdx, craft);
	if (craftNumber != 0) {
		g_msgArgTable[2] = craftNumber;
		g_msgArgTable[3] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_157_CRAFT_REPORT_WITH_NUMBER, g_localPlayer);
	} else {
		g_msgArgTable[2] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_158_CRAFT_REPORT_WITHOUT_NUMBER, g_localPlayer);
	}
}

// FUNCTION: XVT 0x451F50
int msg_BuildTargetDescription(uint16_t targetObjIdx, int playerIdx, int emitHudMessage,
							   int returnActionableOnly) {
	int flightGroupIdx;
	int team;
	CraftData* craft;
	int inspectFlag;
	int disableFlag;
	int captureFlag;
	int boardedFlag;
	int destroyFlag;
	int specialCargoRelevant;
	int actionable;
	int designation;
	unsigned int goalIndex;

	g_msgArgTable[3] = IFMSG_331_BLANK;
	actionable = 0;
	if ((int)g_projectileObjectSlotStart <= targetObjIdx && (int)g_projectileObjectSlotEnd > targetObjIdx)
		return 0;
	if (g_activeRegionCraftObjectSlotEnd > targetObjIdx)
		craft = g_objectTable[targetObjIdx].mobj->pCraft;
	else
		craft = NULL;
	flightGroupIdx = g_objectTable[targetObjIdx].flightGroupIdx;
	team = g_missionFlightGroups[flightGroupIdx].fg.team;
	designation = g_flightMissionState.runtime
					  .teamFgDesignationCode[(uint16_t)g_players[playerIdx].playerIff][flightGroupIdx];
	if (designation == 0) {
		if (craft == NULL) {
			if (g_objectTable[targetObjIdx].genusId == CRAFT_GENUS_MINE) {
				designation = IFMSG_326_MINE - IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_objectTable[targetObjIdx].objectType >= CRAFT_SPECIES_COMM_SAT_1 &&
					   g_objectTable[targetObjIdx].objectType <= CRAFT_SPECIES_SAT_5) {
				designation = IFMSG_327_SATELLITE - IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_objectTable[targetObjIdx].objectType >= CRAFT_SPECIES_PROBE &&
					   g_objectTable[targetObjIdx].objectType <= CRAFT_SPECIES_PROBE_3) {
				designation = IFMSG_328_PROBE - IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_objectTable[targetObjIdx].objectType >= CRAFT_SPECIES_NAV_BUOY_TYPE_1 &&
					   g_objectTable[targetObjIdx].objectType <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
				designation = IFMSG_329_NAV_BUOY - IFMSG_309_TARGET_DESCRIPTION;
			}
		} else {
			if (g_players[playerIdx].boundFlightGroupIdx == flightGroupIdx)
				designation = IFMSG_323_YOUR_WINGMAN - IFMSG_309_TARGET_DESCRIPTION;
			else if (team == (uint16_t)g_players[playerIdx].playerIff)
				designation = IFMSG_322_FRIENDLY_CRAFT - IFMSG_309_TARGET_DESCRIPTION;
			else if (g_objectTable[targetObjIdx].genusId == CRAFT_GENUS_FREIGHTER &&
					 craft->aiFlight.maxSpeedCache == 0)
				designation = IFMSG_325_CARGO - IFMSG_309_TARGET_DESCRIPTION;
			else
				designation = IFMSG_324_CRAFT - IFMSG_309_TARGET_DESCRIPTION;
		}
	}
	msg_formatObjectName(targetObjIdx, 2, g_flightTextScratchBuffer);
	msg_addMessagePtr(0, g_flightTextScratchBuffer);
	g_msgArgTable[1] = IFMSG_331_BLANK;
	if (designation != 0) {
		if (g_targetDescDesignationUsesRelationText[designation] != 0) {
			if (team == (uint16_t)g_players[playerIdx].playerIff)
				g_msgArgTable[1] = IFMSG_334_OUR;
			else {
				int currentTeam = g_missionFlightGroups[g_objectTable[targetObjIdx].flightGroupIdx].fg.team;
				int isEnemy =
					(uint16_t)g_players[playerIdx].playerIff != currentTeam &&
					g_missionTeams[(uint16_t)g_players[playerIdx].playerIff].allies[currentTeam] < 1;
				g_msgArgTable[1] = IFMSG_333_FRIENDLY;
				if (isEnemy)
					g_msgArgTable[1] = IFMSG_332_ENEMY;
			}
		}
		g_msgArgTable[2] = (uint16_t)(designation + IFMSG_309_TARGET_DESCRIPTION);
	} else {
		g_msgArgTable[2] = IFMSG_331_BLANK;
	}

	inspectFlag = 0;
	destroyFlag = 0;
	disableFlag = 0;
	captureFlag = 0;
	boardedFlag = 0;
	specialCargoRelevant = 0;
	for (goalIndex = 0; goalIndex < 8; ++goalIndex) {
		FlightGroupGoal* goal = &g_missionFlightGroups[flightGroupIdx].fg.goals[goalIndex];
		int eventCondition;
		int playerIff = (uint16_t)g_players[playerIdx].playerIff;
		if (goal->enabledTeams[playerIff] == 0 || goal->type != 0 ||
			g_missionFgStats[flightGroupIdx].goalState[8 * playerIff + goalIndex] != 4)
			continue;
		if (goal->amount == GOAL_AMT_ALL_SPECIAL_CARGO) {
			if (g_missionFgStats[flightGroupIdx].specialCargoOutcome[FLIGHT_GROUP_OUTCOME_INSPECTED] == 0)
				inspectFlag = 1;
			specialCargoRelevant = 1;
		}
		eventCondition = goal->eventCondition;
		if (eventCondition == 2) {
			destroyFlag = 1;
		} else if (eventCondition != 3) {
			if (eventCondition == 8)
				disableFlag = 1;
			else if (eventCondition == 5)
				inspectFlag = 1;
			else if (eventCondition == 4 || eventCondition == 44)
				captureFlag = 1;
			else if (eventCondition == 6)
				boardedFlag = 1;
		}
	}
	{
		unsigned int pairOffset;
		for (pairOffset = 0; pairOffset < 2 * sizeof(MissionTriggerPair);
			 pairOffset += sizeof(MissionTriggerPair)) {
			unsigned int triggerOffset;
			for (triggerOffset = 0; triggerOffset < 2 * sizeof(MissionTrigger);
				 triggerOffset += sizeof(MissionTrigger)) {
				unsigned int triggerByteIndex =
					pairOffset + triggerOffset +
					sizeof(g_missionGlobalGoals[0]) * (uint16_t)g_players[playerIdx].playerIff;
				const uint8_t* globalGoalBytes = (const uint8_t*)g_missionGlobalGoals;
				int eventCondition = globalGoalBytes[triggerByteIndex + offsetof(MissionTrigger, condition)];
				if (eventCondition != 10 &&
					Mission_FlightGroupMatchesTriggerVariable(
						flightGroupIdx,
						globalGoalBytes[triggerByteIndex + offsetof(MissionTrigger, variableType)],
						globalGoalBytes[triggerByteIndex + offsetof(MissionTrigger, variable)]) != 0) {
					if (eventCondition == 2) {
						destroyFlag = 1;
					} else if (eventCondition != 3) {
						if (eventCondition == 8)
							disableFlag = 1;
						else if (eventCondition == 5)
							inspectFlag = 1;
						else if (eventCondition == 4 || eventCondition == 44)
							captureFlag = 1;
						else if (eventCondition == 6)
							boardedFlag = 1;
					}
				}
			}
		}
	}
	if (g_activeRegionCraftObjectSlotEnd > targetObjIdx) {
		if (inspectFlag != 0 && craft->iffVisibility[(uint16_t)g_players[playerIdx].playerIff] != 0)
			inspectFlag = 0;
		if (captureFlag != 0 || boardedFlag != 0) {
			if (g_objectTable[targetObjIdx].mobj->speed != 0)
				disableFlag = 1;
			if (specialCargoRelevant != 0 &&
				g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft != craft->waveNumber) {
				captureFlag = 0;
				boardedFlag = 0;
			}
		}
		if (disableFlag != 0 && specialCargoRelevant != 0 &&
			g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft != craft->waveNumber)
			disableFlag = 0;
		if (destroyFlag != 0 && specialCargoRelevant != 0 &&
			g_missionFlightGroups[flightGroupIdx].fg.specialCargoCraft != craft->waveNumber)
			destroyFlag = 0;
	}
	if (inspectFlag != 0) {
		g_msgArgTable[3] = IFMSG_344_INSPECT_IT;
		actionable = 1;
	} else if (disableFlag != 0) {
		if (captureFlag != 0)
			g_msgArgTable[3] = IFMSG_341_TO_BE_CAPTURED;
		else if (boardedFlag != 0)
			g_msgArgTable[3] = IFMSG_345_TO_BE_BOARDED;
		else
			g_msgArgTable[3] = IFMSG_347_OTHERS_WILL_DISABLE_IT;
		if (pai_OrderSlotMatchingObjectHasOrderClass(g_players[playerIdx].objectIndex, 19, targetObjIdx) ==
			1) {
			++g_msgArgTable[3];
			actionable = 1;
		}
	} else if (captureFlag != 0) {
		g_msgArgTable[3] = IFMSG_341_TO_BE_CAPTURED;
	} else if (boardedFlag != 0) {
		g_msgArgTable[3] = IFMSG_345_TO_BE_BOARDED;
	} else if (destroyFlag != 0) {
		g_msgArgTable[3] = IFMSG_337_OTHERS_WILL_DESTROY_IT;
		if (pai_OrderSlotMatchingObjectHasOrderClass(g_players[playerIdx].objectIndex, 69, targetObjIdx) ==
			1) {
			++g_msgArgTable[3];
			actionable = 1;
		}
	}
	if (emitHudMessage != 0 && playerIdx == g_localPlayer) {
		msg_emitInFlightMessage(IFMSG_309_TARGET_DESCRIPTION, playerIdx);
		g_playerFlightTransientTimers[g_localPlayer].targetDescriptionRefreshTimer = 1180;
		g_targetDescriptionMessageId = g_msgArgTable[3];
	}
	if (returnActionableOnly != 0)
		return actionable;
	return g_msgArgTable[3];
}

// FUNCTION: XVT 0x4525A0
void msg_formatObjectName(uint16_t objIdx, uint16_t nameMode, char* outName) {
	int16_t namePartCount;
	int objectIndex;
	ObjectRecord* object;
	MobileObject* mobileObject;
	uint16_t objectType;
	CraftData* craft;
	uint16_t flightGroupIdx;
	MissionFlightGroup* flightGroup;
	uint16_t craftNumber;

	namePartCount = 0;
	*outName = '\0';
	objectIndex = objIdx;
	object = &g_objectTable[objectIndex];
	mobileObject = object->mobj;
	if (mobileObject != NULL) {
		objectType = object->objectType;
		if (mobileObject->state == 0) {
			craft = mobileObject->pCraft;
			flightGroupIdx = object->flightGroupIdx;
			if (nameMode == 1) {
				msg_AppendString(g_modelDefs[craft->modelIndex].nameLong, outName);
				namePartCount = 1;
			} else if (nameMode == 0) {
				msg_AppendString(g_modelDefs[craft->modelIndex].name, outName);
				namePartCount = 1;
			}

			flightGroup = &g_missionFlightGroups[flightGroupIdx];
			if (flightGroup->fg.name[0] != '\0') {
				if (namePartCount != 0) {
					namePartCount = 0;
					msg_AppendChar(' ', outName);
				}
				++namePartCount;
				msg_AppendString(flightGroup->fg.name, outName);
			}

			craftNumber = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(flightGroupIdx, craft);
			if (craftNumber != 0) {
				if (namePartCount != 0)
					msg_AppendChar(' ', outName);
				if (craftNumber >= 10) {
					msg_AppendChar(craftNumber / 10 + '0', outName);
					msg_AppendChar(craftNumber % 10 + '0', outName);
					return;
				}
				msg_AppendChar(craftNumber + '0', outName);
			}
			return;
		}

		if (objectType >= 0x8f && objectType <= 0x9b) {
			msg_AppendString(g_strWarheadNames[objectType - 0x8f], outName);
		} else if (objectType >= CRAFT_SPECIES_COMM_SAT_1 && objectType <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
			msg_AppendString(g_strBuoyNames[objectType - CRAFT_SPECIES_COMM_SAT_1], outName);
		}
		return;
	}

	if (nameMode != 1 && nameMode != 0) {
		msg_AppendString(g_missionFlightGroups[object->flightGroupIdx].fg.name, outName);
		return;
	}

	msg_AppendString(g_strBuoyNames[object->objectType - CRAFT_SPECIES_COMM_SAT_1], outName);
	flightGroup = &g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx];
	if (flightGroup->fg.name[0] != '\0') {
		msg_AppendChar(' ', outName);
		msg_AppendString(g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx].fg.name, outName);
	}
}

// FUNCTION: XVT 0x452830
void msg_AppendString(const char* source, char* destination) {
	while (*destination != '\0') {
		destination++;
	}
	while (*source != '\0') {
		*destination++ = *source++;
	}
	*destination = '\0';
}

// FUNCTION: XVT 0x452860
void msg_AppendChar(char ch, char* destination) {
	while (*destination != '\0')
		++destination;
	*destination = ch;
	destination[1] = '\0';
}

// FUNCTION: XVT 0x452880
void msg_emitLocalPlayerCraftMessage(InFlightMessageId messageId) {
	int objectIndex;

	objectIndex = (int)g_players[g_localPlayer].objectIndex;
	g_msgSenderIff = (uint8_t)g_objectTable[objectIndex].mobj->iff;
	msg_addMessagePtr(0, &g_modelDefs[g_objectTable[objectIndex].mobj->pCraft->modelIndex]);
	msg_addMessagePtr(1, &g_missionFlightGroups[g_objectTable[objectIndex].flightGroupIdx]);
	g_msgArgTable[2] = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(
		g_objectTable[objectIndex].flightGroupIdx, g_objectTable[objectIndex].mobj->pCraft);
	msg_emitInFlightMessage(messageId, g_localPlayer);
}
