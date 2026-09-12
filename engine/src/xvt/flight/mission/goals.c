#include "xvt/flight/mission/goals.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/render/renderer.h"

// GLOBAL: XVT 0x51BE80
uint8_t g_goalTitleColorByIndex[8] = { 0x4A, 0x4E, 0x46, 0x52, 0, 0, 0, 0 };

// GLOBAL: XVT 0x51BEA0
uint8_t g_goalConditionTextVariantCount[48] = { 1, 14, 14, 14, 14, 14, 14, 14, 14, 1, 1, 1,  14, 1,  1,  1,
												1, 1,  1,  14, 1,  1,  1,  1,  1,  1, 1, 1,  1,  1,  1,  1,
												1, 1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 14, 14, 14, 14, 0 };
// GLOBAL: XVT 0x51BED0
const uint16_t g_goalAmountTextVariantByOp[20] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 10, 11, 12, 13,
};
// GLOBAL: XVT 0x51BEF8
const uint16_t g_goalStatusConditionRowBlock[6] = { 0, 2, 0, 0, 1, 3 };
// GLOBAL: XVT 0xA60A60
const char* g_strGoalCondFeminine[188][14] = { 0 };
// GLOBAL: XVT 0xA633C0
const char* g_strGoalCondNeutered[188][14] = { 0 };
// GLOBAL: XVT 0xA65CE0
const char* g_strGoalCondMasculine[188][14] = { 0 };
// GLOBAL: XVT 0xA686E0
uint8_t g_craftGender[80] = { 0 };
// GLOBAL: XVT 0xA63380
const char* g_strGoalOperators[2] = { 0 };
// GLOBAL: XVT 0xA63390
const char* g_strGoalTitles[9] = { 0 };
// GLOBAL: XVT 0xA68610
const char* g_strGoalPercentages[14] = { 0 };

// GLOBAL: XVT 0xA68650
const char* g_strGoalFamilyNames0To6[7] = { 0 };
// GLOBAL: XVT 0xA68670
const char* g_strGoalFamilyNames7To22[16] = { 0 };
// GLOBAL: XVT 0xA686B0
const char* g_strGoalConjunctions[8] = { 0 };
// GLOBAL: XVT 0xA686D4
const char* g_strGoalEscape[3] = { 0 };
// GLOBAL: XVT 0xA68730
const char* g_strGoalSides[3] = { 0 };

// GLOBAL: XVT 0xA607AC
const char* g_strWarheadUnknown = 0;
// GLOBAL: XVT 0xA607B0
const char* g_strBuoyNames[16] = { 0 };
// GLOBAL: XVT 0xA607F0
const char* g_strStatusStrings[9] = { 0 };
// GLOBAL: XVT 0xA60820
const char* g_strWarheadNames[13] = { 0 };
// GLOBAL: XVT 0xA60860
const char* g_strSpeciesNamesPlural[73] = { 0 };
// GLOBAL: XVT 0xA60990
const char* g_strWingmanCommands[10] = { 0 };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4150D0
int16_t goals_outputgoal(uint16_t targetId, uint16_t condition, uint16_t targetType, uint16_t goalStatus,
						 uint16_t amountOp, uint16_t timeLimit5SecUnits, const char* conditionTextOverride,
						 int percentComplete, int goalTitleIndex) {
	int16_t consumedHeight;
	uint16_t amountTextVariant;
	char timeText[32];
	char percentText[76];

	amountTextVariant = g_goalAmountTextVariantByOp[(uint16_t)amountOp];
	consumedHeight = (int16_t)(g_flightFontLineHeight + 2);
	goalStatus = (uint16_t)(47 * g_goalStatusConditionRowBlock[goalStatus]);

	if (targetType == GOAL_TARGET_FLIGHT_GROUP) {
		if (amountOp == GOAL_AMT_ALL_SPECIAL_CARGO) {
			goals_DrawObjectTypeName(g_missionFlightGroups[targetId].fg.craftType, 0,
									 conditionTextOverride != NULL);
			g_flightDrawCharFn(' ');
			FlightText_DrawString(g_missionFlightGroups[targetId].fg.name);
			g_flightDrawCharFn(' ');
			if (Mission_GetFlightGroupSpecialCargoOutcome8(
					targetId, g_missionFlightGroups[targetId].fg.specialCargoCraft) != 0) {
				g_flightDrawCharFn(g_missionFlightGroups[targetId].fg.specialCargoCraft + '1');
			} else {
				g_flightDrawCharFn('?');
			}
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight + FlightText_GetWrapHeightForString(conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight =
					(int16_t)(consumedHeight +
							  goals_DrawConditionText(g_missionFlightGroups[targetId].fg.craftType, condition,
													  amountTextVariant, (int16_t)goalStatus));
			}
		} else if (amountOp == GOAL_AMT_ALL_NON_SPECIAL) {
			goals_DrawObjectTypeName(g_missionFlightGroups[targetId].fg.craftType, 0,
									 conditionTextOverride != NULL);
			g_flightDrawCharFn(' ');
			FlightText_DrawString(g_missionFlightGroups[targetId].fg.name);
			g_flightDrawCharFn(' ');
			if (Mission_GetFlightGroupSpecialCargoOutcome8(
					targetId, g_missionFlightGroups[targetId].fg.specialCargoCraft) != 0) {
				g_flightDrawCharFn(g_missionFlightGroups[targetId].fg.specialCargoCraft + '1');
			} else {
				g_flightDrawCharFn('?');
			}
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight + FlightText_GetWrapHeightForString(conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight =
					(int16_t)(consumedHeight +
							  goals_DrawConditionText(g_missionFlightGroups[targetId].fg.craftType, condition,
													  amountTextVariant, (int16_t)goalStatus));
			}
		} else if (g_missionFgStats[targetId].outcomeCount[FLIGHT_GROUP_OUTCOME_TOTAL] > 1u) {
			int16_t usePlural =
				(amountOp == GOAL_AMT_ALL_BUT_1 || amountOp == GOAL_AMT_ALL_EXCEPT_PLAYER) ? 10 : 0;

			goals_DrawObjectTypeName(g_missionFlightGroups[targetId].fg.craftType, usePlural,
									 conditionTextOverride != NULL);
			g_flightDrawCharFn(' ');
			FlightText_DrawString(g_missionFlightGroups[targetId].fg.name);
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight + FlightText_GetWrapHeightForString(conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight =
					(int16_t)(consumedHeight +
							  goals_DrawConditionText(g_missionFlightGroups[targetId].fg.craftType, condition,
													  amountTextVariant, (int16_t)goalStatus));
			}
		} else {
			consumedHeight = (int16_t)(consumedHeight +
									   goals_DrawObjectTypeName(g_missionFlightGroups[targetId].fg.craftType,
																0, conditionTextOverride != NULL));
			g_flightDrawCharFn(' ');
			FlightText_DrawString(g_missionFlightGroups[targetId].fg.name);
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight + FlightText_GetWrapHeightForString(conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight = (int16_t)(consumedHeight + goals_DrawConditionText(
																g_missionFlightGroups[targetId].fg.craftType,
																condition, 9, (int16_t)goalStatus));
			}
		}

		if (timeLimit5SecUnits != 0) {
			uint16_t timeSeconds = (uint16_t)(5 * timeLimit5SecUnits);
			uint16_t minutes = (uint16_t)(timeSeconds / 60);
			uint16_t seconds = (uint16_t)(timeSeconds - minutes * 60);

			if (seconds < 10) {
				sprintf(timeText, " (%s %ld:0%ld)", g_strGoalConjunctions[GOAL_CONJ_STR_LESS_THAN],
						(long)minutes, (long)seconds);
			} else {
				sprintf(timeText, " (%s %ld:%ld)", g_strGoalConjunctions[GOAL_CONJ_STR_LESS_THAN],
						(long)minutes, (long)seconds);
			}
			consumedHeight = (int16_t)(consumedHeight + FlightText_GetWrapHeightForString(timeText));
			FlightText_DrawString(timeText);
		}
	} else if (targetType == GOAL_TARGET_GLOBAL_GROUP) {
		if (conditionTextOverride != NULL) {
			consumedHeight =
				(int16_t)(consumedHeight +
						  goals_DrawObjectTypeName(g_missionFlightGroups[targetId].fg.craftType, 0, 0));
			consumedHeight = (int16_t)(consumedHeight + FlightText_GetWrapHeightForString(
															g_missionFlightGroups[targetId].fg.name));
			FlightText_DrawString(g_missionFlightGroups[targetId].fg.name);
			FlightText_DrawString(": ");
			consumedHeight =
				(int16_t)(consumedHeight + FlightText_GetWrapHeightForString(conditionTextOverride));
			FlightText_DrawString(conditionTextOverride);
		} else {
			uint16_t matchingCount = 0;
			uint16_t flightGroupIndex = 0;

			if ((int16_t)g_missionHeader.numFlightGroups > 0) {
				int flightGroupCount = (int16_t)g_missionHeader.numFlightGroups;

				do {
					if (g_missionFlightGroups[flightGroupIndex].fg.globalGroup == targetId &&
						g_missionFgStats[flightGroupIndex].arrivalEnabled != 0) {
						++matchingCount;
					}
					++flightGroupIndex;
				} while (flightGroupIndex < flightGroupCount);
			}

			flightGroupIndex = 0;

			if ((int16_t)g_missionHeader.numFlightGroups > 0) {
				do {
					if (g_missionFlightGroups[flightGroupIndex].fg.globalGroup == targetId &&
						g_missionFgStats[flightGroupIndex].arrivalEnabled != 0) {
						if (g_missionFlightGroups[flightGroupIndex].fg.numberOfCraft > 1) {
							consumedHeight =
								(int16_t)(consumedHeight +
										  goals_DrawObjectTypeName(
											  g_missionFlightGroups[flightGroupIndex].fg.craftType, 0, 0));
							consumedHeight =
								(int16_t)(consumedHeight + FlightText_GetWrapHeightForString(
															   g_strGoalConjunctions[GOAL_CONJ_STR_GROUP]));
							FlightText_DrawString(g_strGoalConjunctions[GOAL_CONJ_STR_GROUP]);
						} else {
							consumedHeight =
								(int16_t)(consumedHeight +
										  goals_DrawObjectTypeName(
											  g_missionFlightGroups[flightGroupIndex].fg.craftType, 0, 0));
						}
						--matchingCount;
						consumedHeight =
							(int16_t)(consumedHeight + FlightText_GetWrapHeightForString(
														   g_missionFlightGroups[flightGroupIndex].fg.name));
						FlightText_DrawString(g_missionFlightGroups[flightGroupIndex].fg.name);

						if (matchingCount == 1) {
							int16_t separatorHeight =
								FlightText_GetWrapHeightForString(g_strGoalConjunctions[GOAL_CONJ_STR_AND]);
							consumedHeight = (int16_t)(consumedHeight + separatorHeight);
							if (separatorHeight == 0) {
								g_flightDrawCharFn(' ');
							}
							FlightText_DrawString(g_strGoalConjunctions[GOAL_CONJ_STR_AND]);
						} else if (matchingCount > 1) {
							FlightText_DrawString(g_strGoalConjunctions[GOAL_CONJ_STR_COMMA]);
						}
					}
					++flightGroupIndex;
				} while ((int16_t)g_missionHeader.numFlightGroups > flightGroupIndex);
			}
			FlightText_DrawString(": ");
			consumedHeight =
				(int16_t)(consumedHeight +
						  goals_DrawConditionText(g_missionFlightGroups[targetId].fg.craftType, condition,
												  amountTextVariant, (int16_t)goalStatus));
		}
	} else {
		switch (targetType) {
			case GOAL_TARGET_SPECIES:
				consumedHeight =
					(int16_t)(consumedHeight + goals_DrawObjectTypeName((uint16_t)(targetId + 1), 1, 0));
				FlightText_DrawString(": ");
				break;

			case GOAL_TARGET_GENUS:
				FlightText_DrawString(g_strGoalFamilyNames7To22[g_genusConvert[targetId]]);
				FlightText_DrawString(": ");
				break;

			case GOAL_TARGET_FAMILY:
				FlightText_DrawString(g_strGoalFamilyNames0To6[g_familyConvert[targetId]]);
				FlightText_DrawString(": ");
				break;

			case GOAL_TARGET_IFF:
				if (targetId >= 2) {
					uint16_t nameOffset = (uint16_t)(g_missionHeader.iffNames[targetId - 2][0] == '1');
					FlightText_DrawString(&g_missionHeader.iffNames[targetId - 2][nameOffset]);
					FlightText_DrawString(g_strGoalSides[GOAL_SIDE_STR_CRAFT]);
				} else {
					FlightText_DrawString(g_strGoalSides[targetId]);
				}
				FlightText_DrawString(": ");
				break;

			case GOAL_TARGET_CRAFT_WHEN:
			case GOAL_TARGET_STATUS:
				consumedHeight = (int16_t)(consumedHeight + goals_DrawConditionText((uint16_t)(targetId + 1),
																					0, amountTextVariant,
																					(int16_t)goalStatus));
				break;

			case GOAL_TARGET_AI_LEVEL:
				consumedHeight = (int16_t)(consumedHeight +
										   FlightText_GetWrapHeightForString(g_strGoalEscape[targetId - 1]));
				FlightText_DrawString(g_strGoalEscape[targetId - 1]);
				break;

			default:
				break;
		}
		consumedHeight =
			(int16_t)(consumedHeight + goals_DrawConditionText((uint16_t)(targetId + 1), condition,
															   amountTextVariant, (int16_t)goalStatus));
	}

	if (percentComplete >= 0) {
		sprintf(percentText, " (%ld%%)", (long)percentComplete);
		if (goalTitleIndex == 0 || goalTitleIndex == 2) {
			FlightText_SetColor(0x4A);
		} else if (goalTitleIndex == 3 || goalTitleIndex == 1) {
			FlightText_SetColor(0x52);
		} else {
			FlightText_SetColor(0x43);
		}
		consumedHeight = (int16_t)(consumedHeight + FlightText_GetWrapHeightForString(percentText));
		FlightText_DrawString(percentText);
		FlightText_SetColor(g_goalTitleColorByIndex[goalTitleIndex]);
	}
	g_flightDrawCharFn('\n');
	return consumedHeight;
}

// FUNCTION: XVT 0x415A70
int16_t goals_DrawConditionText(unsigned int craftSpecies, uint16_t condition, uint16_t amountTextVariant,
								int16_t conditionRowBase) {
	const char* text;
	int16_t result;
	ModelIndex modelIndex;

	text = NULL;
	result = 0;
	if (g_goalConditionTextVariantCount[condition] == 1) {
		amountTextVariant = 0;
	}

	condition = (uint16_t)(condition + conditionRowBase);
	modelIndex = GetModelIndexFromType(craftSpecies);
	if (modelIndex != MODEL_INDEX_NONE) {
		switch (g_craftGender[modelIndex]) {
			case CRAFT_GENDER_MASCULINE:
				text = g_strGoalCondMasculine[condition][amountTextVariant];
				break;
			case CRAFT_GENDER_FEMININE:
				text = g_strGoalCondFeminine[condition][amountTextVariant];
				break;
			case CRAFT_GENDER_NEUTERED:
				text = g_strGoalCondNeutered[condition][amountTextVariant];
				break;
		}

		result = FlightText_GetWrapHeightForString(text);
		FlightText_DrawString(text);
		return result;
	}

	if ((uint16_t)craftSpecies >= CRAFT_SPECIES_COMM_SAT_1 &&
		(uint16_t)craftSpecies <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
		text = g_strGoalCondMasculine[condition][amountTextVariant];
		result = FlightText_GetWrapHeightForString(text);
		FlightText_DrawString(text);
	}
	return result;
}

// FUNCTION: XVT 0x415BA0
int16_t goals_DrawObjectTypeName(uint16_t craftSpecies, int16_t usePluralName, int16_t useShortName) {
	ModelIndex modelIndex;
	int16_t wrapHeight;
	const char* displayName;

#ifdef XVT_MODERN
	displayName = "";
#endif
	modelIndex = GetModelIndexFromType(craftSpecies);
	if (modelIndex != MODEL_INDEX_NONE) {
		if (usePluralName != 0) {
			displayName = g_strSpeciesNamesPlural[modelIndex];
		} else if (useShortName != 0) {
			displayName = g_modelDefs[modelIndex].name;
		} else {
			displayName = g_modelDefs[modelIndex].nameLong;
		}
	} else if (craftSpecies >= CRAFT_SPECIES_COMM_SAT_1 && craftSpecies <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
		displayName = g_strBuoyNames[craftSpecies - CRAFT_SPECIES_COMM_SAT_1];
	}

	wrapHeight = FlightText_GetWrapHeightForString(displayName);
	FlightText_DrawString(displayName);
	return wrapHeight;
}
