#include "xvt/flight/player/flight_player.h"

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x46C140
int16_t FlightPlayer_HasDisabledSubsystem(void) {
	int objectIndex;
	CraftData* craft;
	int16_t systemId;
	int16_t displaySlot;
	int16_t allInstalledSystemsOperational;
	uint16_t systemIdByDisplaySlot[CRAFT_SUBSYSTEM_COUNT];

	objectIndex = g_players[g_localPlayer].objectIndex;
	if (objectIndex == -1)
		return 0;
	craft = g_objectTable[objectIndex].mobj->pCraft;
	if (craft == NULL)
		return 0;
	for (systemId = 0; systemId < CRAFT_SUBSYSTEM_COUNT; ++systemId)
		systemIdByDisplaySlot[craft->systemDisplaySlotBySystem[systemId]] = systemId;
	allInstalledSystemsOperational = 1;
	for (displaySlot = 0; displaySlot < CRAFT_SUBSYSTEM_COUNT; ++displaySlot) {
		if (craft->systemHealth[systemIdByDisplaySlot[displaySlot]] == 0 &&
			(g_subsystemIdToFlag[systemIdByDisplaySlot[displaySlot]] & craft->systemFlags) != 0)
			allInstalledSystemsOperational = 0;
	}
	return allInstalledSystemsOperational == 0;
}

// FUNCTION: XVT 0x46C200
void nullsub_8(const char* message) { (void)message; }

// FUNCTION: XVT 0x481D90
void FlightPlayer_IncreaseThrottleSpeed(int16_t step, int playerIdx) {
	PlayerData* player;
	uint16_t* throttleSpeedPtr;
	uint16_t throttleSpeed;

	player = &g_players[playerIdx];
	throttleSpeedPtr = &g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed;
	throttleSpeed = *throttleSpeedPtr;
	*throttleSpeedPtr = (uint16_t)(throttleSpeed + step);
	if (g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed < throttleSpeed)
		g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed = UINT16_MAX;
}

// FUNCTION: XVT 0x481E10
void FlightPlayer_DecreaseThrottleSpeed(int16_t step, int playerIdx) {
	PlayerData* player;
	uint16_t* throttleSpeedPtr;
	uint16_t throttleSpeed;

	player = &g_players[playerIdx];
	throttleSpeedPtr = &g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed;
	throttleSpeed = *throttleSpeedPtr;
	*throttleSpeedPtr = (uint16_t)(throttleSpeed - step);
	if (g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed > throttleSpeed)
		g_objectTable[player->objectIndex].mobj->pCraft->throttleSpeed = 0;
}
