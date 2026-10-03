#include "xvt/flight/player/flight_player.h"

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"

/* Returns 1 when the local player's craft has an installed subsystem (its flag
 * set in systemFlags) whose systemHealth is 0, else 0. Also returns 0 when the
 * local player has no craft or the object has no craft record. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x46C140
int16_t FlightPlayer_HasDisabledSubsystem(void)
{
	int objectIndex;
	CraftData *craft;
	int16_t systemId;
	int16_t displaySlot;
	int16_t allInstalledSystemsOperational;
	uint16_t systemIdByDisplaySlot[CRAFT_SUBSYSTEM_COUNT];

	objectIndex = g_players[g_localPlayer].objectIndex;
	if (objectIndex == -1) {
		return 0;
	}
	craft = g_objectTable[objectIndex].mobj->pCraft;
	if (craft == NULL) {
		return 0;
	}
	for (systemId = 0; systemId < CRAFT_SUBSYSTEM_COUNT; ++systemId) {
		systemIdByDisplaySlot
			[craft->systemDisplaySlotBySystem[systemId]] = systemId;
	}
	allInstalledSystemsOperational = 1;
	for (displaySlot = 0; displaySlot < CRAFT_SUBSYSTEM_COUNT;
	     ++displaySlot) {
		if (craft->systemHealth[systemIdByDisplaySlot[displaySlot]] ==
			    0 &&
		    (g_subsystemIdToFlag[systemIdByDisplaySlot[displaySlot]] &
		     craft->systemFlags) != 0) {
			allInstalledSystemsOperational = 0;
		}
	}
	return allInstalledSystemsOperational == 0;
}

/* Does nothing with its message. Nothing calls this. */
// FUNCTION: XVT 0x46C200
void nullsub_8(const char *message) { (void)message; }

/* Adds step to the throttleSpeed of the player's craft, holding at 0xFFFF when
 * the sum would wrap past it. Does not check that the player has a craft. */
// FUNCTION: XVT 0x481D90
void FlightPlayer_IncreaseThrottleSpeed(int16_t step, int playerIdx)
{
	PlayerData *player;
	uint16_t *throttleSpeedPtr;
	uint16_t throttleSpeed;

	player = &g_players[playerIdx];
	throttleSpeedPtr =
		&g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed;
	throttleSpeed = *throttleSpeedPtr;
	*throttleSpeedPtr = (uint16_t)(throttleSpeed + step);
	if (g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed <
	    throttleSpeed) {
		g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed =
			UINT16_MAX;
	}
}

/* Takes step from the throttleSpeed of the player's craft, holding at 0 when
 * the result would wrap below it. Does not check that the player has a
 * craft. */
// FUNCTION: XVT 0x481E10
void FlightPlayer_DecreaseThrottleSpeed(int16_t step, int playerIdx)
{
	PlayerData *player;
	uint16_t *throttleSpeedPtr;
	uint16_t throttleSpeed;

	player = &g_players[playerIdx];
	throttleSpeedPtr =
		&g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed;
	throttleSpeed = *throttleSpeedPtr;
	*throttleSpeedPtr = (uint16_t)(throttleSpeed - step);
	if (g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed >
	    throttleSpeed) {
		g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed =
			0;
	}
}
