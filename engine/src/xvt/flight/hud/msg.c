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

/* Index in g_messageLogRecords of the newest logged message, 0 to 299, or
 * 0xFFFF before the first. Flight start sets 0xFFFF: Flight_MainLoop in the
 * original build, XvtFlightLoading_MissionSetup in the modern one. Only
 * msg_emitInFlightMessage advances it, back to 0 after 299. */
// GLOBAL: XVT 0x5235D0
uint16_t g_messageLogWriteIndex;
/* Messages logged since flight start, not wrapped at 300; only
 * msg_emitInFlightMessage raises it. Flight start sets 0: Flight_MainLoop in
 * the original build, XvtFlightLoading_MissionSetup in the modern one.
 * Mfd_DrawMessageLogPage redraws when it changes. */
// GLOBAL: XVT 0x5235D4
uint16_t g_messageLogTotalCount;
/* Set to 1 by msg_emitInFlightMessage, its only writer, when
 * g_messageLogWriteIndex first wraps; the message log page then offers all
 * 300 records. Nothing sets it back to 0, so it carries into later flights
 * of the same run. */
// GLOBAL: XVT 0x5235D8
uint16_t g_messageLogWrapped = 0;
/* Arguments of the next in-flight message, taken in order by its '*' and '&'
 * marks. A value below 0x8000 is the number for '&', or for '*' a message
 * whose text it inserts; slot + 0x8000, set by msg_addMessagePtr, makes '*'
 * insert the text at g_msgPtrs[slot]. Many functions write it, chiefly the
 * msg functions and Flight_ProcessPlayerActions, just before they emit. */
// GLOBAL: XVT 0xA07BE0
uint16_t g_msgArgTable[4];
/* Second name buffer for messages that name two objects: paiman_boardmaneuver
 * and Player_HandleHyperspaceCommand write a name into it with
 * msg_formatObjectName and pass it as argument 1. */
// GLOBAL: XVT 0x9EC4D0
char g_flightSecondaryObjectNameBuffer[256] = {0};
/* Text for '*' arguments marked slot + 0x8000 in g_msgArgTable; only
 * msg_addMessagePtr writes it. A model definition or flight group passed
 * here reads as its name, the first field of each. */
// GLOBAL: XVT 0xA08120
static const void *g_msgPtrs[4];
/* The message log: a ring of 300 records in the memory of
 * g_messageLogHandle, MESSAGE_LOG_BUFFER_BYTES (32,000) long. Set by locking
 * the handle in msg_emitInFlightMessage and Mfd_DrawMessageLogPage;
 * msg_emitInFlightMessage writes each logged message at
 * g_messageLogWriteIndex. */
// GLOBAL: XVT 0x9993FC
HudInFlightMessageRecord *g_messageLogRecords = NULL;
/* IFF of the next message's sender, copied into its senderIff. Many
 * functions write it, chiefly the msg functions and
 * Flight_ProcessPlayerActions; nothing resets it, so a message whose caller
 * does not set it takes the last sender's. */
// GLOBAL: XVT 0xA08292
uint16_t g_msgSenderIff = 0;
/* By target designation, the offset of its message from
 * IFMSG_309_TARGET_DESCRIPTION: 1 when msg_BuildTargetDescription, its only
 * reader, puts "Our", "Friendly" or "Enemy" before it. */
// GLOBAL: XVT 0x5240B8
const uint8_t g_targetDescDesignationUsesRelationText[24] = {
	0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0};
/* Voice sound id that msg_emitInFlightMessage stores in messages 196 and 207
 * (IFMSG_196_CODE_02_ARGUMENT, IFMSG_207_CODE_01_ARGUMENT);
 * Hud_ShowFlightMessagePane plays it when such a message is first shown.
 * Only Mission_UpdateLogic writes it. */
// GLOBAL: XVT 0x9D7684
uint16_t g_pendingHudMessageVoiceSfxId = 0;
/* In-flight message templates by InFlightMessageId, filled by
 * StringTable_LoadGameStrings. The first byte is the pane type; '*' inserts
 * an argument's text, '&' and a count byte an argument's number, and '[' and
 * ']' switch the text color when the message is drawn. */
// GLOBAL: XVT 0x9A1840
const char *g_strInFlightMessages[417] = {0};

/* Appends the message log to the first of msglog0.txt to msglog99.txt that
 * does not exist yet, or to msglog99.txt when all do; the modern build looks
 * in the player's files. Writes records 0 to g_messageLogWriteIndex - 1 of
 * g_messageLogRecords, one line each: the text without its pane type byte
 * (and, after type 1, a digit 0 to 3), a tab, and the mission clock as
 * hours:minutes:seconds. Called at a wrap it writes all 300; called
 * otherwise it leaves out the newest record, at g_messageLogWriteIndex. Does
 * nothing when no file opens. Every existing file it opens to test, but
 * msglog99.txt, stays open. Does not check for the 0xFFFF index before the
 * first logged message, which makes it read 65,535 records. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x450550
void msg_writeMessageLogFile(void)
{
	char fileName[16];
	int logIndex;
	XvtFile *stream;
	int messageIndex;
	int recordOffset;
	HudInFlightMessageRecord *record;
	int prefix;
	char *text;

	logIndex = 0;
	do {
		sprintf(fileName, "msglog%ld.txt", (long)logIndex);
#ifdef XVT_MODERN
		stream =
			XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, fileName, "r");
#else
		stream = File_RawOpen(fileName, "r");
#endif
		if (stream == NULL) {
			stream = File_RawOpen(fileName, "a");
			break;
		}
		if (logIndex == 99) {
			if (stream != NULL) {
				File_RawClose(stream);
			}
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
				record =
					(HudInFlightMessageRecord
						 *)((uint8_t *)
							    g_messageLogRecords +
						    recordOffset);
				prefix = record->text[0];
				text = record->text;
				if (prefix < 9) {
					++text;
					if (prefix == 1 && *text >= '0' &&
					    *text <= '3') {
						++text;
					}
				}
				recordOffset += sizeof(*record);
				++messageIndex;
				File_Printf(stream, "%s\t%ld:%ld:%ld\n", text,
					    (long)record->clockHour,
					    (long)record->clockMinute,
					    (long)record->clockSecond);
			} while ((uint16_t)g_messageLogWriteIndex >
				 messageIndex);
		}
		File_RawClose(stream);
	}
}

/* Builds in-flight message messageId from its template and the arguments in
 * g_msgArgTable, logs it, and hands it to a HUD pane. Does nothing when
 * g_flightSimSideEffectsSuppressed is set or playerIdx is not g_localPlayer.
 * The record takes the mission clock (the countdown clock when the mission
 * has a time limit, else the elapsed clock), g_msgSenderIff, and for
 * messages 196 and 207 g_pendingHudMessageVoiceSfxId. The template's first
 * byte is the pane type and, below 9, stays as the text's first character;
 * '*' inserts the next argument's text, and '&' with a count byte its value
 * in that many places, dropping zeros in front. Text past 69 characters is
 * cut. The modern build swaps in its own text for message 1, the pause
 * notice. A pane type of 9 or more becomes 6.
 *
 * Types 1 and 2 enter the message log unless g_replayViewMode is set:
 * g_messageLogTotalCount rises and g_messageLogWriteIndex steps on; at 300
 * it goes back to 0 and g_messageLogWrapped to 1, after
 * msg_writeMessageLogFile when g_radioMessageBackupEnabled is set. Types 3,
 * 4 and 7 replace g_systemMessagePane when system messages are on (or this
 * is message 400) and the pane is empty, holds no type 4, or the new one is
 * type 7; else they are dropped. Type 8 replaces g_flightGroupMessagePane.
 * Other types go to g_readyMessagePaneQueue: an empty slot 0 takes the
 * message and shows it; else, by the type in slot 0, it waits behind it (at
 * most 9 wait; past that it is lost) or takes slot 0 and is shown, after
 * Hud_ShiftReadyMessageQueueForReplacement when slot 0 holds a type 1, 2 or
 * 5. Behind a type 0 it is dropped. Does not check argument slots against
 * the 4-entry tables or an '&' count against g_flightTextDecimalDivisors. */
// FUNCTION: XVT 0x450650
void msg_emitInFlightMessage(InFlightMessageId messageId, int playerIdx)
{
	HudInFlightMessageRecord message;
	const uint8_t *templateCursor;
	const char *argumentText;
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

	if (g_flightSimSideEffectsSuppressed != 0 ||
	    playerIdx != g_localPlayer) {
		return;
	}

	message.stateOrMessageId = (uint16_t)messageId;
	if (g_flightMissionState.missionTimeLimitMinutes != 0) {
		message.clockSubsecondTicks =
			(uint16_t)g_missionCountdownClock.subsecondTicks;
		message.clockSecond = g_missionCountdownClock.seconds;
		message.clockMinute = g_missionCountdownClock.minutes;
		message.clockHour = g_missionCountdownClock.hours;
	} else {
		message.clockSubsecondTicks =
			(uint16_t)g_missionElapsedClock.subsecondTicks;
		message.clockSecond = g_missionElapsedClock.seconds;
		message.clockMinute = g_missionElapsedClock.minutes;
		message.clockHour = g_missionElapsedClock.hours;
	}
	message.ageSeconds = 0;
	message.showCount = 0;
	message.senderIff = g_msgSenderIff;
	if (messageId == IFMSG_207_CODE_01_ARGUMENT ||
	    messageId == IFMSG_196_CODE_02_ARGUMENT) {
		message.voiceSfxId = g_pendingHudMessageVoiceSfxId;
	} else {
		message.voiceSfxId = 0;
	}

	textLength = 0;
	argumentIndex = 0;
	templateCursor = (const uint8_t *)g_strInFlightMessages[messageId];
	paneType = *templateCursor;
#ifdef XVT_MODERN
	if (messageId == IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE) {
		message.text[textLength++] = (char)paneType;
		templateCursor =
			(const uint8_t
				 *)"Mission paused. Press your pause key or button to continue.";
	}
#endif
	while (*templateCursor != '\0' && textLength < sizeof(message.text)) {
		if (*templateCursor == '*') {
			++templateCursor;
			argumentValue = g_msgArgTable[argumentIndex++];
			if (argumentValue < 0x8000) {
				argumentText =
					g_strInFlightMessages[argumentValue];
			} else {
				argumentText = (const char *)
					g_msgPtrs[argumentValue & 0x7FFF];
			}
			while (*argumentText != '\0' &&
			       textLength < sizeof(message.text)) {
				message.text[textLength++] = *argumentText++;
			}
		} else if (*templateCursor == '&') {
			digitCount = templateCursor[1];
			templateCursor += 2;
			digitStarted = 0;
			remainder = g_msgArgTable[argumentIndex++];
			while (digitCount != 0 &&
			       textLength < sizeof(message.text)) {
				divisor =
					g_flightTextDecimalDivisors[digitCount];
				digitValue = remainder / divisor;
				remainder %= divisor;
				if (digitStarted != 0 || digitCount <= 1 ||
				    digitValue != 0) {
					digitStarted = 1;
					if (digitValue > 9) {
						digitValue = 9;
					}
					outputChar = digitValue + '0';
				} else {
					outputChar = ' ';
				}
				if (outputChar != ' ') {
					message.text[textLength++] =
						(char)outputChar;
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
	if (g_replayViewMode == 0 &&
	    (normalizedPaneType == 2 || normalizedPaneType == 1)) {
		++g_messageLogTotalCount;
		if (++g_messageLogWriteIndex == 300) {
			if (g_radioMessageBackupEnabled != 0) {
				msg_writeMessageLogFile();
			}
			g_messageLogWriteIndex = 0;
			g_messageLogWrapped = 1;
		}
		g_messageLogRecords =
			(HudInFlightMessageRecord *)Memory_LockHandle(
				g_messageLogHandle);
		Memory_UnlockHandle(g_messageLogHandle);
		g_messageLogRecords[g_messageLogWriteIndex] = message;
	}

	if (normalizedPaneType == 3 || normalizedPaneType == 4 ||
	    normalizedPaneType == 7) {
		if ((g_systemMessageDisplayEnabled != 0 ||
		     messageId ==
			     IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF) &&
		    (g_systemMessagePane.stateOrMessageId == UINT16_MAX ||
		     g_systemMessagePane.paneType != 4 ||
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
			g_readyMessagePaneQueue[g_readyMessageQueueCount + 1] =
				message;
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
			g_readyMessagePaneQueue[g_readyMessageQueueCount + 1] =
				message;
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

/* Announces flight group flightGroupIndex's arrival to the local player. Finds
 * where it is with Mission_ResolveObjectOrMissionPointWorldLoc: for arrival
 * method 0 with 0x8000, else with the group's craft in the active region that
 * follows no other; with none found, the last resolved place stands. Takes the
 * distance from the player's craft, or from the player's camera while
 * mapCameraState is not 0, and shows (distance * 161 / 65,536 + 50) / 100 km,
 * at least 1; this leaves trig2_polardistance multiplied by 161. Emits message
 * 114 or 115, new craft alert for one craft or several, when the group's IFF is
 * not the player's, else 235 or 236, entering area. Sets g_msgSenderIff to the
 * group's IFF and fills the message arguments with the craft count,
 * modelIndex's long name, the group and the range. */
// FUNCTION: XVT 0x451940
void msg_reportfgcreation(uint16_t flightGroupIndex, uint16_t modelIndex)
{
	int flightGroupIdx;
	uint16_t objectIndex;
	ObjectRecord *object;
	CraftData *craft;
	ObjectRecord *localPlayerObject;
	uint16_t rangeKm;
	uint8_t iff;
	uint16_t numberOfCraft;
	int distanceHundredths;

	flightGroupIdx = flightGroupIndex;
	if (g_missionFlightGroups[flightGroupIdx].fg.arrivalMethod == 0) {
		Mission_ResolveObjectOrMissionPointWorldLoc(0x8000,
							    flightGroupIndex);
	} else {
		objectIndex = (uint16_t)g_activeRegionObjectSlotStart;
		while (objectIndex < g_activeRegionCraftObjectSlotEnd) {
			object = &g_objectTable[objectIndex];
			if (object->objectType != 0) {
				craft = object->mobj->pCraft;
				if (object->flightGroupIdx ==
					    flightGroupIndex &&
				    craft->leader_obj_idx == UINT8_MAX) {
					Mission_ResolveObjectOrMissionPointWorldLoc(
						objectIndex, flightGroupIndex);
					break;
				}
			}
			++objectIndex;
		}
	}

	if (g_players[g_localPlayer].mapCameraState == 0) {
		localPlayerObject =
			&g_objectTable[g_players[g_localPlayer].objectIndex];
		trig2_ctop(g_worldLocX - localPlayerObject->world_x,
			   g_worldLocY - localPlayerObject->world_y,
			   g_worldLocZ - localPlayerObject->world_z);
	} else {
		trig2_ctop(
			g_worldLocX -
				g_players[g_localPlayer].viewState.cameraWorldX,
			g_worldLocY -
				g_players[g_localPlayer].viewState.cameraWorldY,
			g_worldLocZ - g_players[g_localPlayer]
					      .viewState.cameraWorldZ);
	}
	trig2_polardistance *= 161;
	distanceHundredths = (trig2_polardistance >> 16) & 0xFFFF;
	rangeKm = (uint16_t)((distanceHundredths + 50) / 100);
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
			msg_emitInFlightMessage(
				IFMSG_114_NEW_CRAFT_ALERT_ARG_ARG_AT_ARG_KM,
				g_localPlayer);
		} else {
			msg_emitInFlightMessage(
				IFMSG_115_NEW_CRAFT_ALERT_ARG_ARG_S_AT_ARG_KM,
				g_localPlayer);
		}
	} else if (numberOfCraft == 1) {
		msg_addMessagePtr(0, g_modelDefs[modelIndex].nameLong);
		msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
		g_msgArgTable[2] = rangeKm;
		msg_emitInFlightMessage(
			IFMSG_235_ARG_ARG_ENTERING_AREA_AT_ARG_KM,
			g_localPlayer);
	} else {
		msg_addMessagePtr(1, g_modelDefs[modelIndex].nameLong);
		msg_addMessagePtr(2, &g_missionFlightGroups[flightGroupIdx]);
		g_msgArgTable[3] = rangeKm;
		msg_emitInFlightMessage(
			IFMSG_236_ARG_ARG_S_FROM_FG_ARG_ENTERING_AREA_AT_ARG_KM,
			g_localPlayer);
	}
}

/* Makes argument slot insert the text at value: stores value in g_msgPtrs and
 * slot + 0x8000 in g_msgArgTable. Does not check slot against the 4
 * entries. */
// FUNCTION: XVT 0x451BF0
void msg_addMessagePtr(uint16_t slot, const void *value)
{
	g_msgArgTable[slot] = slot + 0x8000;
	g_msgPtrs[slot] = value;
}

/* Emits a message naming a craft, its model's short name then its flight
 * group, with its number when Hud_MissionFG_GetCraftNumberIfShown gives one
 * (message 133, else 134), followed by message msgTemplateId's text. Sets
 * g_msgSenderIff to the object's IFF and fills the message arguments. */
// FUNCTION: XVT 0x451C20
void msg_emitCraftMessage(uint16_t objIdx, CraftData *craft,
			  int16_t msgTemplateId)
{
	ObjectRecord *object;
	int flightGroupIdx;
	uint16_t craftNumber;

	object = &g_objectTable[objIdx];
	flightGroupIdx = object->flightGroupIdx;
	g_msgSenderIff = (uint8_t)object->mobj->iff;
	msg_addMessagePtr(0, &g_modelDefs[craft->modelIndex]);
	msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
	craftNumber = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(
		flightGroupIdx, craft);
	if (craftNumber != 0) {
		g_msgArgTable[2] = craftNumber;
		g_msgArgTable[3] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_133_CRAFT_EVENT_WITH_NUMBER,
					g_localPlayer);
	} else {
		g_msgArgTable[2] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_134_CRAFT_EVENT_WITHOUT_NUMBER,
					g_localPlayer);
	}
}

/* Shows a wingman's acknowledgment of a command and has it spoken. Sets
 * g_msgSenderIff to the sender's flight group's IFF, then does nothing more,
 * speech included, unless that group shares the local player's IFF and team.
 * With multipleRecipients set, emits message 269, acknowledged, with the
 * group and commandId's text; else 147 or 148, Roger, with the sender's
 * model, group and number when shown, and commandId's text. Then calls
 * fsfx_SpeakWingmanEvent with responseIndex. Takes the model index from
 * byte 4 of senderCraft. */
// FUNCTION: XVT 0x451D00
void msg_radioMessage(uint16_t senderObjIdx, uint8_t *senderCraft,
		      uint16_t commandId, uint16_t responseIndex,
		      int16_t multipleRecipients)
{
	int flightGroupIdx;
	uint16_t craftNumber;

	flightGroupIdx = g_objectTable[senderObjIdx].flightGroupIdx;
	g_msgSenderIff = g_missionFlightGroups[flightGroupIdx].fg.iff;
	if (g_players[g_localPlayer].iff != g_msgSenderIff ||
	    g_missionFlightGroups[flightGroupIdx].fg.team !=
		    g_players[g_localPlayer].team) {
		return;
	}
	if (multipleRecipients != 0) {
		msg_addMessagePtr(0, &g_missionFlightGroups[flightGroupIdx]);
		g_msgArgTable[1] = commandId;
		msg_emitInFlightMessage(
			IFMSG_269_MESSAGE_ACKNOWLEDGED_FLIGHT_GROUP_ARG_ARG,
			g_localPlayer);
	} else {
		msg_addMessagePtr(0, &g_modelDefs[senderCraft[4]]);
		msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
		craftNumber = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(
			flightGroupIdx, (CraftData *)senderCraft);
		if (craftNumber != 0) {
			g_msgArgTable[2] = craftNumber;
			g_msgArgTable[3] = commandId;
			msg_emitInFlightMessage(
				IFMSG_147_ROGER_CRAFT_WITH_NUMBER,
				g_localPlayer);
		} else {
			g_msgArgTable[2] = commandId;
			msg_emitInFlightMessage(
				IFMSG_148_ROGER_CRAFT_WITHOUT_NUMBER,
				g_localPlayer);
		}
	}
	fsfx_SpeakWingmanEvent(g_localPlayer, senderObjIdx, 1, responseIndex,
			       senderObjIdx, UINT16_MAX);
}

/* As msg_emitCraftMessage, with messages 157 and 158, reporting in, and
 * g_msgSenderIff set from the flight group's IFF instead of the object's. */
// FUNCTION: XVT 0x451E70
void msg_reportmessage(uint16_t objIdx, CraftData *craft, int16_t msgTemplateId)
{
	int flightGroupIdx;
	uint16_t craftNumber;

	flightGroupIdx = g_objectTable[objIdx].flightGroupIdx;
	g_msgSenderIff = g_missionFlightGroups[flightGroupIdx].fg.iff;
	msg_addMessagePtr(0, &g_modelDefs[craft->modelIndex]);
	msg_addMessagePtr(1, &g_missionFlightGroups[flightGroupIdx]);
	craftNumber = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(
		flightGroupIdx, craft);
	if (craftNumber != 0) {
		g_msgArgTable[2] = craftNumber;
		g_msgArgTable[3] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_157_CRAFT_REPORT_WITH_NUMBER,
					g_localPlayer);
	} else {
		g_msgArgTable[2] = (uint16_t)msgTemplateId;
		msg_emitInFlightMessage(IFMSG_158_CRAFT_REPORT_WITHOUT_NUMBER,
					g_localPlayer);
	}
}

/* Builds the target description of playerIdx's target: its name, its
 * designation, and what the player's goals want done with it. Returns 0 at once
 * for an object in the projectile slots. Otherwise fills the message arguments:
 * slot 0 the name, from msg_formatObjectName mode 2 into
 * g_flightTextScratchBuffer; slot 1 "Our", "Friendly" or "Enemy" when
 * g_targetDescDesignationUsesRelationText asks for it; slot 2 the designation's
 * message, from the team's designation table or, when that gives 0, by kind
 * (mine, satellite, probe, nav buoy, wingman, friendly craft, cargo, craft);
 * slot 3 the goal phrase, blank when there is none.
 *
 * The phrase comes from the target group's pending goals for the player's team
 * and the team's global goal triggers that match the group: inspect, else
 * disable (worded for capture or boarding when those apply), else capture,
 * board or destroy. For a craft, an inspection the team has done drops out,
 * capture or boarding of a moving craft asks to disable it, and with a special
 * cargo goal only the special cargo craft gets capture, board, disable or
 * destroy. When an order of the player's flight group whose built-in plan is 19
 * (disable) or 69 (destroy) targets it, the phrase moves to the next message,
 * which tells the player to act, and the target is actionable; inspect always
 * is. With emitHudMessage set for the local player, emits message 309, sets the
 * player's targetDescriptionRefreshTimer to 1,180 ticks and
 * g_targetDescriptionMessageId to the phrase. Returns the actionable flag when
 * returnActionableOnly is set, else the phrase's message id. Does not check a
 * designation from the table against the 24 entries of
 * g_targetDescDesignationUsesRelationText. */
// FUNCTION: XVT 0x451F50
int msg_BuildTargetDescription(uint16_t targetObjIdx, int playerIdx,
			       int emitHudMessage, int returnActionableOnly)
{
	int flightGroupIdx;
	int team;
	CraftData *craft;
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
	if ((int)g_projectileObjectSlotStart <= targetObjIdx &&
	    (int)g_projectileObjectSlotEnd > targetObjIdx) {
		return 0;
	}
	if (g_activeRegionCraftObjectSlotEnd > targetObjIdx) {
		craft = g_objectTable[targetObjIdx].mobj->pCraft;
	} else {
		craft = NULL;
	}
	flightGroupIdx = g_objectTable[targetObjIdx].flightGroupIdx;
	team = g_missionFlightGroups[flightGroupIdx].fg.team;
	designation =
		g_flightMissionState.runtime.teamFgDesignationCode
			[(uint16_t)g_players[playerIdx].team][flightGroupIdx];
	if (designation == 0) {
		if (craft == NULL) {
			if (g_objectTable[targetObjIdx].genusId ==
			    CRAFT_GENUS_MINE) {
				designation = IFMSG_326_MINE -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_objectTable[targetObjIdx].objectType >=
					   CRAFT_SPECIES_COMM_SAT_1 &&
				   g_objectTable[targetObjIdx].objectType <=
					   CRAFT_SPECIES_SAT_5) {
				designation = IFMSG_327_SATELLITE -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_objectTable[targetObjIdx].objectType >=
					   CRAFT_SPECIES_PROBE &&
				   g_objectTable[targetObjIdx].objectType <=
					   CRAFT_SPECIES_PROBE_3) {
				designation = IFMSG_328_PROBE -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_objectTable[targetObjIdx].objectType >=
					   CRAFT_SPECIES_NAV_BUOY_TYPE_1 &&
				   g_objectTable[targetObjIdx].objectType <=
					   CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
				designation = IFMSG_329_NAV_BUOY -
					      IFMSG_309_TARGET_DESCRIPTION;
			}
		} else {
			if (g_players[playerIdx].boundFlightGroupIdx ==
			    flightGroupIdx) {
				designation = IFMSG_323_YOUR_WINGMAN -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (team ==
				   (uint16_t)g_players[playerIdx].team) {
				designation = IFMSG_322_FRIENDLY_CRAFT -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else if (g_objectTable[targetObjIdx].genusId ==
					   CRAFT_GENUS_FREIGHTER &&
				   craft->aiFlight.maxSpeedCache == 0) {
				designation = IFMSG_325_CARGO -
					      IFMSG_309_TARGET_DESCRIPTION;
			} else {
				designation = IFMSG_324_CRAFT -
					      IFMSG_309_TARGET_DESCRIPTION;
			}
		}
	}
	msg_formatObjectName(targetObjIdx, 2, g_flightTextScratchBuffer);
	msg_addMessagePtr(0, g_flightTextScratchBuffer);
	g_msgArgTable[1] = IFMSG_331_BLANK;
	if (designation != 0) {
		if (g_targetDescDesignationUsesRelationText[designation] != 0) {
			if (team == (uint16_t)g_players[playerIdx].team) {
				g_msgArgTable[1] = IFMSG_334_OUR;
			} else {
				int currentTeam =
					g_missionFlightGroups
						[g_objectTable[targetObjIdx]
							 .flightGroupIdx]
							.fg.team;
				int isEnemy =
					(uint16_t)g_players[playerIdx].team !=
						currentTeam &&
					g_missionTeams[(uint16_t)g_players
							       [playerIdx]
								       .team]
							.allies[currentTeam] <
						1;
				g_msgArgTable[1] = IFMSG_333_FRIENDLY;
				if (isEnemy) {
					g_msgArgTable[1] = IFMSG_332_ENEMY;
				}
			}
		}
		g_msgArgTable[2] =
			(uint16_t)(designation + IFMSG_309_TARGET_DESCRIPTION);
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
		FlightGroupGoal *goal = &g_missionFlightGroups[flightGroupIdx]
						 .fg.goals[goalIndex];
		int eventCondition;
		int playerTeam = (uint16_t)g_players[playerIdx].team;
		if (goal->enabledTeams[playerTeam] == 0 ||
		    goal->goalKind != 0 ||
		    g_missionFgStats[flightGroupIdx]
				    .goalState[8 * playerTeam + goalIndex] !=
			    4) {
			continue;
		}
		if (goal->amount == GOAL_AMT_ALL_SPECIAL_CARGO) {
			if (g_missionFgStats[flightGroupIdx].specialCargoOutcome
				    [FLIGHT_GROUP_OUTCOME_INSPECTED] == 0) {
				inspectFlag = 1;
			}
			specialCargoRelevant = 1;
		}
		eventCondition = goal->eventCondition;
		if (eventCondition == 2) {
			destroyFlag = 1;
		} else if (eventCondition != 3) {
			if (eventCondition == 8) {
				disableFlag = 1;
			} else if (eventCondition == 5) {
				inspectFlag = 1;
			} else if (eventCondition == 4 ||
				   eventCondition == 44) {
				captureFlag = 1;
			} else if (eventCondition == 6) {
				boardedFlag = 1;
			}
		}
	}
	{
		unsigned int pairOffset;
		for (pairOffset = 0;
		     pairOffset < 2 * sizeof(MissionTriggerPair);
		     pairOffset += sizeof(MissionTriggerPair)) {
			unsigned int triggerOffset;
			for (triggerOffset = 0;
			     triggerOffset < 2 * sizeof(MissionTrigger);
			     triggerOffset += sizeof(MissionTrigger)) {
				unsigned int triggerByteIndex =
					pairOffset + triggerOffset +
					sizeof(g_missionGlobalGoals[0]) *
						(uint16_t)g_players[playerIdx]
							.team;
				const uint8_t *globalGoalBytes =
					(const uint8_t *)g_missionGlobalGoals;
				int eventCondition =
					globalGoalBytes[triggerByteIndex +
							offsetof(MissionTrigger,
								 condition)];
				if (eventCondition != 10 &&
				    Mission_FlightGroupMatchesTriggerVariable(
					    flightGroupIdx,
					    globalGoalBytes
						    [triggerByteIndex +
						     offsetof(MissionTrigger,
							      variableType)],
					    globalGoalBytes
						    [triggerByteIndex +
						     offsetof(MissionTrigger,
							      variable)]) !=
					    0) {
					if (eventCondition == 2) {
						destroyFlag = 1;
					} else if (eventCondition != 3) {
						if (eventCondition == 8) {
							disableFlag = 1;
						} else if (eventCondition ==
							   5) {
							inspectFlag = 1;
						} else if (eventCondition ==
								   4 ||
							   eventCondition ==
								   44) {
							captureFlag = 1;
						} else if (eventCondition ==
							   6) {
							boardedFlag = 1;
						}
					}
				}
			}
		}
	}
	if (g_activeRegionCraftObjectSlotEnd > targetObjIdx) {
		if (inspectFlag != 0 &&
		    craft->identifiedOrderByTeam[(uint16_t)g_players[playerIdx]
							 .team] != 0) {
			inspectFlag = 0;
		}
		if (captureFlag != 0 || boardedFlag != 0) {
			if (g_objectTable[targetObjIdx].mobj->speed != 0) {
				disableFlag = 1;
			}
			if (specialCargoRelevant != 0 &&
			    g_missionFlightGroups[flightGroupIdx]
					    .fg.specialCargoCraft !=
				    craft->craftOrdinal) {
				captureFlag = 0;
				boardedFlag = 0;
			}
		}
		if (disableFlag != 0 && specialCargoRelevant != 0 &&
		    g_missionFlightGroups[flightGroupIdx]
				    .fg.specialCargoCraft !=
			    craft->craftOrdinal) {
			disableFlag = 0;
		}
		if (destroyFlag != 0 && specialCargoRelevant != 0 &&
		    g_missionFlightGroups[flightGroupIdx]
				    .fg.specialCargoCraft !=
			    craft->craftOrdinal) {
			destroyFlag = 0;
		}
	}
	if (inspectFlag != 0) {
		g_msgArgTable[3] = IFMSG_344_INSPECT_IT;
		actionable = 1;
	} else if (disableFlag != 0) {
		if (captureFlag != 0) {
			g_msgArgTable[3] = IFMSG_341_TO_BE_CAPTURED;
		} else if (boardedFlag != 0) {
			g_msgArgTable[3] = IFMSG_345_TO_BE_BOARDED;
		} else {
			g_msgArgTable[3] = IFMSG_347_OTHERS_WILL_DISABLE_IT;
		}
		if (pai_SetupContextAndFindOrderPlanOnTarget(
			    g_players[playerIdx].objectIndex, 19,
			    targetObjIdx) == 1) {
			++g_msgArgTable[3];
			actionable = 1;
		}
	} else if (captureFlag != 0) {
		g_msgArgTable[3] = IFMSG_341_TO_BE_CAPTURED;
	} else if (boardedFlag != 0) {
		g_msgArgTable[3] = IFMSG_345_TO_BE_BOARDED;
	} else if (destroyFlag != 0) {
		g_msgArgTable[3] = IFMSG_337_OTHERS_WILL_DESTROY_IT;
		if (pai_SetupContextAndFindOrderPlanOnTarget(
			    g_players[playerIdx].objectIndex, 69,
			    targetObjIdx) == 1) {
			++g_msgArgTable[3];
			actionable = 1;
		}
	}
	if (emitHudMessage != 0 && playerIdx == g_localPlayer) {
		msg_emitInFlightMessage(IFMSG_309_TARGET_DESCRIPTION,
					playerIdx);
		g_playerFlightTransientTimers[g_localPlayer]
			.targetDescriptionRefreshTimer = 1180;
		g_targetDescriptionMessageId = g_msgArgTable[3];
	}
	if (returnActionableOnly != 0) {
		return actionable;
	}
	return g_msgArgTable[3];
}

/* Writes an object's display name into outName, emptying it first. For a
 * craft: its model's long name (nameMode 1) or short name (0), its flight
 * group's name when it has one, and its number when
 * Hud_MissionFG_GetCraftNumberIfShown gives one, space separated; other
 * modes leave out the model. Another mobile object gets its warhead name
 * (types 0x8F to 0x9B) or satellite, mine, probe or buoy name, else nothing.
 * An object with no mobile object gets its group's name in modes other than
 * 0 and 1, else the satellite-to-buoy name for its type and the group's
 * name. Does not check outName's size, or the type of an object with no
 * mobile object. */
// FUNCTION: XVT 0x4525A0
void msg_formatObjectName(uint16_t objIdx, uint16_t nameMode, char *outName)
{
	int16_t namePartCount;
	int objectIndex;
	ObjectRecord *object;
	MobileObject *mobileObject;
	uint16_t objectType;
	CraftData *craft;
	uint16_t flightGroupIdx;
	MissionFlightGroup *flightGroup;
	uint16_t craftNumber;

	namePartCount = 0;
	*outName = '\0';
	objectIndex = objIdx;
	object = &g_objectTable[objectIndex];
	mobileObject = object->mobj;
	if (mobileObject != NULL) {
		objectType = object->objectType;
		if (mobileObject->family == 0) {
			craft = mobileObject->pCraft;
			flightGroupIdx = object->flightGroupIdx;
			if (nameMode == 1) {
				msg_AppendString(
					g_modelDefs[craft->modelIndex].nameLong,
					outName);
				namePartCount = 1;
			} else if (nameMode == 0) {
				msg_AppendString(
					g_modelDefs[craft->modelIndex].name,
					outName);
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

			craftNumber =
				(uint16_t)Hud_MissionFG_GetCraftNumberIfShown(
					flightGroupIdx, craft);
			if (craftNumber != 0) {
				if (namePartCount != 0) {
					msg_AppendChar(' ', outName);
				}
				if (craftNumber >= 10) {
					msg_AppendChar(craftNumber / 10 + '0',
						       outName);
					msg_AppendChar(craftNumber % 10 + '0',
						       outName);
					return;
				}
				msg_AppendChar(craftNumber + '0', outName);
			}
			return;
		}

		if (objectType >= 0x8f && objectType <= 0x9b) {
			msg_AppendString(g_strWarheadNames[objectType - 0x8f],
					 outName);
		} else if (objectType >= CRAFT_SPECIES_COMM_SAT_1 &&
			   objectType <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
			msg_AppendString(
				g_strSatMineProbeBuoyPilotNames
					[objectType - CRAFT_SPECIES_COMM_SAT_1],
				outName);
		}
		return;
	}

	if (nameMode != 1 && nameMode != 0) {
		msg_AppendString(
			g_missionFlightGroups[object->flightGroupIdx].fg.name,
			outName);
		return;
	}

	msg_AppendString(
		g_strSatMineProbeBuoyPilotNames[object->objectType -
						CRAFT_SPECIES_COMM_SAT_1],
		outName);
	flightGroup = &g_missionFlightGroups[g_objectTable[objectIndex]
						     .flightGroupIdx];
	if (flightGroup->fg.name[0] != '\0') {
		msg_AppendChar(' ', outName);
		msg_AppendString(
			g_missionFlightGroups[g_objectTable[objectIndex]
						      .flightGroupIdx]
				.fg.name,
			outName);
	}
}

/* Appends source to the string in destination; does not check its size. */
// FUNCTION: XVT 0x452830
void msg_AppendString(const char *source, char *destination)
{
	while (*destination != '\0') {
		destination++;
	}
	while (*source != '\0') {
		*destination++ = *source++;
	}
	*destination = '\0';
}

/* Appends ch to the string in destination; does not check its size. */
// FUNCTION: XVT 0x452860
void msg_AppendChar(char ch, char *destination)
{
	while (*destination != '\0') {
		++destination;
	}
	*destination = ch;
	destination[1] = '\0';
}

/* Emits messageId with arguments naming the local player's craft: its
 * model's short name in slot 0, its flight group in slot 1 and its number,
 * 0 when not shown, in slot 2. Sets g_msgSenderIff to the craft's IFF. */
// FUNCTION: XVT 0x452880
void msg_emitLocalPlayerCraftMessage(InFlightMessageId messageId)
{
	int objectIndex;

	objectIndex = (int)g_players[g_localPlayer].objectIndex;
	g_msgSenderIff = (uint8_t)g_objectTable[objectIndex].mobj->iff;
	msg_addMessagePtr(0, &g_modelDefs[g_objectTable[objectIndex]
						  .mobj->pCraft->modelIndex]);
	msg_addMessagePtr(1, &g_missionFlightGroups[g_objectTable[objectIndex]
							    .flightGroupIdx]);
	g_msgArgTable[2] = (uint16_t)Hud_MissionFG_GetCraftNumberIfShown(
		g_objectTable[objectIndex].flightGroupIdx,
		g_objectTable[objectIndex].mobj->pCraft);
	msg_emitInFlightMessage(messageId, g_localPlayer);
}
