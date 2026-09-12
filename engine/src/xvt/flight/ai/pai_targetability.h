#ifndef XVT_FLIGHT_AI_PAI_TARGETABILITY_H
#define XVT_FLIGHT_AI_PAI_TARGETABILITY_H

#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/object/object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Balance of Power expands this XvT helper at its call sites. Do not copy it into callers. */
static __inline int pai_IsObjectTargetable(unsigned int objIdx) {
	ObjectRecord* object;
	CraftData* craft;
	AiController* controller;
	uint16_t objectKind;

	if (objIdx == UINT16_MAX) {
		return 0;
	}
	object = &g_objectTable[objIdx];
	if (object->mobj != NULL) {
		if (object->objectType == 0) {
			return 0;
		}
		craft = object->mobj->pCraft;
		if (craft != NULL) {
			controller = &craft->aiController;
			if (controller->maneuverMode == AI_MANEUVER_MODE_INTO_HYPERSPACE &&
				controller->maneuverPhase != 0) {
				return 0;
			}
			objectKind = craft->objectKind;
			if (objectKind == CRAFT_OBJECT_KIND_UNKNOWN_1 || objectKind == CRAFT_OBJECT_KIND_BREAKING_UP ||
				objectKind == CRAFT_OBJECT_KIND_EXPLODING) {
				return 0;
			}
			if (craft->beamTypeId == BEAM_TYPE_DECOY && craft->beamActive != 0) {
				uint8_t sourceGenus;

				pai_ObjectRefUpdateApproxRangeScore(objIdx, g_paiContext.objectIndex);
				sourceGenus = g_objectTable[g_paiContext.objectIndex].genusId;
				if (sourceGenus == CRAFT_GENUS_STARFIGHTER || sourceGenus == CRAFT_GENUS_TRANSPORT ||
					sourceGenus == CRAFT_GENUS_UTILITY_VEHICLE) {
					if (g_targetRangeScore > AI_DECOY_RANGE_SMALL) {
						return 0;
					}
				} else if (g_targetRangeScore > AI_DECOY_RANGE_LARGE) {
					return 0;
				}
			}
			if (g_objectTable[objIdx].playerOwnerIdx != -1) {
				return 1;
			}
			return craft->hullDamage <= g_objectTable[objIdx].mobj->pCraft->hullMax;
		} else {
			return 1;
		}
	} else {
		return object->objectType != 0;
	}
}

#ifdef __cplusplus
}
#endif

#endif
