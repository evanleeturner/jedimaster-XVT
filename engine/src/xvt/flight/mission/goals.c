#include "xvt/flight/mission/goals.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/render/renderer.h"

/* Text color of each goals-page section, as the color letter
 * FlightText_SetColor takes: 'J', 'N', 'F' and 'R' (0x4A, 0x4E, 0x46, 0x52)
 * for sections 0 to 3, and 0 for 4 to 7. Nothing writes it. Read by
 * Mfd_DrawMissionGoalsPage and by goals_outputgoal, which sets it back after
 * drawing a percentage. */
// GLOBAL: XVT 0x51BE80
uint8_t g_goalTitleColorByIndex[8] = {0x4A, 0x4E, 0x46, 0x52, 0, 0, 0, 0};

/* Per mission condition (a MISSION_COND_ value, 0 to 47), how many wordings
 * its row of condition text has in strings.txt: 14 (one per amount wording)
 * or 1; entry 47 is 0. StringTable_LoadGameStrings reads that many lines per
 * row, and goals_DrawConditionText uses wording 0 when the count is 1.
 * Nothing writes it. */
// GLOBAL: XVT 0x51BEA0
uint8_t g_goalConditionTextVariantCount[48] = {
	1, 14, 14, 14, 14, 14, 14, 14, 14, 1, 1, 1,  14, 1,  1,	 1,
	1, 1,  1,  14, 1,  1,  1,  1,  1,  1, 1, 1,  1,	 1,  1,	 1,
	1, 1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 14, 14, 14, 14, 0};
/* Maps a goal amount (a GOAL_AMT_ value, 0 to 19) to the column of the
 * condition text tables: 0 to 9 keep their value, the subset amounts 10 to
 * 15 use the columns of 0 to 5, and 16 to 19 use 10 to 13. Read only by
 * goals_outputgoal. */
// GLOBAL: XVT 0x51BED0
const uint16_t g_goalAmountTextVariantByOp[20] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 10, 11, 12, 13,
};
/* Maps goals_outputgoal's goal status, 0 to 5, to a block of 47 rows in the
 * condition text tables: statuses 0, 2 and 3 use block 0, status 4 block 1,
 * status 1 block 2 and status 5 block 3. */
// GLOBAL: XVT 0x51BEF8
const uint16_t g_goalStatusConditionRowBlock[6] = {0, 2, 0, 0, 1, 3};
/* Condition text for craft whose name is feminine (g_craftGender), from
 * strings.txt: row 47 times the status block plus the condition, column the
 * amount wording. StringTable_LoadGameStrings fills it only when some craft
 * is feminine; columns past a row's count in
 * g_goalConditionTextVariantCount stay NULL. */
// GLOBAL: XVT 0xA60A60
const char *g_strGoalCondFeminine[188][14] = {0};
/* Condition text for craft whose name is neuter, laid out as
 * g_strGoalCondFeminine. StringTable_LoadGameStrings fills it only when some
 * craft is neuter. */
// GLOBAL: XVT 0xA633C0
const char *g_strGoalCondNeutered[188][14] = {0};
/* Condition text for craft whose name is masculine, and for species
 * CRAFT_SPECIES_COMM_SAT_1 to CRAFT_SPECIES_NAV_BUOY_TYPE_2 that have no model
 * index, laid out as g_strGoalCondFeminine. StringTable_LoadGameStrings
 * always fills it. */
// GLOBAL: XVT 0xA65CE0
const char *g_strGoalCondMasculine[188][14] = {0};
/* Per model index, the grammatical gender of the craft's name, a
 * CRAFT_GENDER_ value. StringTable_LoadGameStrings sets entries 0 to 72 from
 * the first letter, m, f or n, of each model name line in strings.txt and
 * stops the game on any other letter; entries 73 to 79 stay 0, masculine. */
// GLOBAL: XVT 0xA686E0
uint8_t g_craftGender[80] = {0};
/* The two operator words (GOAL_OPERATOR_STR_ values) from strings.txt,
 * filled by StringTable_LoadGameStrings. Nothing reads it. */
// GLOBAL: XVT 0xA63380
const char *g_strGoalOperators[2] = {0};
/* Section titles and outcome words of the goals page (GOAL_TITLE_STR_
 * values), from strings.txt by StringTable_LoadGameStrings. Read by
 * Mfd_DrawMissionGoalsPage. */
// GLOBAL: XVT 0xA63390
const char *g_strGoalTitles[9] = {0};
/* The amount words (GOAL_PERCENT_STR_ values) from strings.txt, filled by
 * StringTable_LoadGameStrings. Nothing reads it. */
// GLOBAL: XVT 0xA68610
const char *g_strGoalPercentages[14] = {0};

/* Family names from strings.txt, indexed through g_familyConvert; filled by
 * StringTable_LoadGameStrings and drawn by goals_outputgoal for a goal on a
 * family. */
// GLOBAL: XVT 0xA68650
const char *g_strGoalFamilyNames[7] = {0};
/* Genus names from strings.txt, indexed through g_genusConvert; filled by
 * StringTable_LoadGameStrings and drawn by goals_outputgoal for a goal on a
 * genus. */
// GLOBAL: XVT 0xA68670
const char *g_strGoalGenusNames[16] = {0};
/* Joining words of a goal line (GOAL_CONJ_STR_ values) from strings.txt,
 * filled by StringTable_LoadGameStrings. goals_outputgoal uses the "less
 * than" word before a time limit and the group, "and" and comma words in a
 * list of flight groups. */
// GLOBAL: XVT 0xA686B0
const char *g_strGoalConjunctions[8] = {0};
/* StringTable_LoadGameStrings fills only entry 0, from strings.txt; entries 1
 * and 2 stay NULL. goals_outputgoal draws entry targetId - 1 for a goal whose
 * target type is GOAL_TARGET_AI_LEVEL. */
// GLOBAL: XVT 0xA686D4
const char *g_strGoalEscape[3] = {0};
/* Side words (GOAL_SIDE_STR_ values) from strings.txt, filled by
 * StringTable_LoadGameStrings. goals_outputgoal draws entry 0 or 1 for a goal
 * on IFF 0 or 1, and entry 2 after the mission's own name for a higher IFF. */
// GLOBAL: XVT 0xA68730
const char *g_strGoalSides[3] = {0};

/* The word for an unknown value, the line after the warhead names in
 * strings.txt, set by StringTable_LoadGameStrings. Drawn by
 * Hud_UpdateTargetingComputerDisplay and Hud_DrawCmdTargetDetails where a
 * target's cargo or time is not known. */
// GLOBAL: XVT 0xA607AC
const char *g_strUnknown = 0;
/* Names of the species from CRAFT_SPECIES_COMM_SAT_1 (0x46) on, indexed by
 * species minus 0x46; from strings.txt by StringTable_LoadGameStrings. Read
 * by goals_DrawObjectTypeName for such a species with no model index, and by
 * Hud_FormatObjectDisplayName and msg_formatObjectName. */
// GLOBAL: XVT 0xA607B0
const char *g_strSatMineProbeBuoyPilotNames[16] = {0};
/* Status words from strings.txt, filled by StringTable_LoadGameStrings.
 * Nothing reads it. */
// GLOBAL: XVT 0xA607F0
const char *g_strStatusStrings[9] = {0};
/* Warhead names from strings.txt, for object types 0x8F to 0x9B, filled by
 * StringTable_LoadGameStrings. Read by Hud_FormatObjectDisplayName and
 * msg_formatObjectName. */
// GLOBAL: XVT 0xA60820
const char *g_strWarheadNames[13] = {0};
/* Plural craft name per model index, from strings.txt by
 * StringTable_LoadGameStrings. Read only by goals_DrawObjectTypeName. */
// GLOBAL: XVT 0xA60860
const char *g_strSpeciesNamesPlural[73] = {0};
/* Wingman command words from strings.txt, filled by
 * StringTable_LoadGameStrings. Nothing reads it. */
// GLOBAL: XVT 0xA60990
const char *g_strWingmanCommands[10] = {0};

/* Draws one goal line of the goals page at the flight text cursor and ends it
 * with a new line. A flight group target (targetType 1) draws the craft type,
 * the group's name and the condition; for the special-cargo amounts (6 and 7)
 * it also draws the special craft's number when
 * Mission_IsSpecialCargoInspected returns nonzero, else "?"; then any
 * time limit as "m:ss" (timeLimit5SecUnits counts 5 seconds). A global group
 * target (targetType 8) lists every flight group of that global group whose
 * arrivalEnabled is set, joined with commas and "and", then the condition;
 * with conditionTextOverride set it draws flight group targetId's craft type
 * and name instead. Other targets draw a species, genus, family or IFF name,
 * or entry targetId - 1 of g_strGoalEscape (types 7 and 10 draw condition 0's
 * text), then the condition for species targetId + 1. conditionTextOverride,
 * when not NULL, replaces the condition text for flight group and global
 * group targets. A percentComplete of 0 or more is drawn as " (n%)" in a
 * color picked by goalTitleIndex, then the color goes back to
 * g_goalTitleColorByIndex[goalTitleIndex]. Returns the font line height plus
 * 2, plus the extra height FlightText_GetWrapHeightForString reports for the
 * pieces it measures. Writes no globals of its own. Does not check targetId,
 * goalStatus (0 to 5) or amountOp (0 to 19) against their tables. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4150D0
int16_t goals_outputgoal(uint16_t targetId, uint16_t condition,
			 uint16_t targetType, uint16_t goalStatus,
			 uint16_t amountOp, uint16_t timeLimit5SecUnits,
			 const char *conditionTextOverride, int percentComplete,
			 int goalTitleIndex)
{
	int16_t consumedHeight;
	uint16_t amountTextVariant;
	char timeText[32];
	char percentText[76];

	amountTextVariant = g_goalAmountTextVariantByOp[(uint16_t)amountOp];
	consumedHeight = (int16_t)(g_flightFontLineHeight + 2);
	/* From here goalStatus holds a row offset into the condition text tables (47 rows per status block),
	 * passed below as the condition row base. */
	goalStatus = (uint16_t)(47 * g_goalStatusConditionRowBlock[goalStatus]);

	if (targetType == GOAL_TARGET_FLIGHT_GROUP) {
		if (amountOp == GOAL_AMT_ALL_SPECIAL_CARGO) {
			goals_DrawObjectTypeName(
				g_missionFlightGroups[targetId].fg.craftType, 0,
				conditionTextOverride != NULL);
			g_flightDrawCharFn(' ');
			FlightText_DrawString(
				g_missionFlightGroups[targetId].fg.name);
			g_flightDrawCharFn(' ');
			if (Mission_IsSpecialCargoInspected(
				    targetId, g_missionFlightGroups[targetId]
						      .fg.specialCargoCraft) !=
			    0) {
				g_flightDrawCharFn(
					g_missionFlightGroups[targetId]
						.fg.specialCargoCraft +
					'1');
			} else {
				g_flightDrawCharFn('?');
			}
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight +
						  FlightText_GetWrapHeightForString(
							  conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight =
					(int16_t)(consumedHeight +
						  goals_DrawConditionText(
							  g_missionFlightGroups
								  [targetId]
									  .fg
									  .craftType,
							  condition,
							  amountTextVariant,
							  (int16_t)goalStatus));
			}
		} else if (amountOp == GOAL_AMT_ALL_NON_SPECIAL) {
			goals_DrawObjectTypeName(
				g_missionFlightGroups[targetId].fg.craftType, 0,
				conditionTextOverride != NULL);
			g_flightDrawCharFn(' ');
			FlightText_DrawString(
				g_missionFlightGroups[targetId].fg.name);
			g_flightDrawCharFn(' ');
			if (Mission_IsSpecialCargoInspected(
				    targetId, g_missionFlightGroups[targetId]
						      .fg.specialCargoCraft) !=
			    0) {
				g_flightDrawCharFn(
					g_missionFlightGroups[targetId]
						.fg.specialCargoCraft +
					'1');
			} else {
				g_flightDrawCharFn('?');
			}
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight +
						  FlightText_GetWrapHeightForString(
							  conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight =
					(int16_t)(consumedHeight +
						  goals_DrawConditionText(
							  g_missionFlightGroups
								  [targetId]
									  .fg
									  .craftType,
							  condition,
							  amountTextVariant,
							  (int16_t)goalStatus));
			}
		} else if (g_missionFgStats[targetId]
				   .outcomeCount[FLIGHT_GROUP_OUTCOME_TOTAL] >
			   1u) {
			int16_t usePlural =
				(amountOp == GOAL_AMT_ALL_BUT_1 ||
				 amountOp == GOAL_AMT_ALL_EXCEPT_PLAYER)
					? 10
					: 0;

			goals_DrawObjectTypeName(
				g_missionFlightGroups[targetId].fg.craftType,
				usePlural, conditionTextOverride != NULL);
			g_flightDrawCharFn(' ');
			FlightText_DrawString(
				g_missionFlightGroups[targetId].fg.name);
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight +
						  FlightText_GetWrapHeightForString(
							  conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight =
					(int16_t)(consumedHeight +
						  goals_DrawConditionText(
							  g_missionFlightGroups
								  [targetId]
									  .fg
									  .craftType,
							  condition,
							  amountTextVariant,
							  (int16_t)goalStatus));
			}
		} else {
			consumedHeight =
				(int16_t)(consumedHeight +
					  goals_DrawObjectTypeName(
						  g_missionFlightGroups
							  [targetId]
								  .fg.craftType,
						  0,
						  conditionTextOverride !=
							  NULL));
			g_flightDrawCharFn(' ');
			FlightText_DrawString(
				g_missionFlightGroups[targetId].fg.name);
			FlightText_DrawString(": ");
			if (conditionTextOverride != NULL) {
				consumedHeight =
					(int16_t)(consumedHeight +
						  FlightText_GetWrapHeightForString(
							  conditionTextOverride));
				FlightText_DrawString(conditionTextOverride);
			} else {
				consumedHeight =
					(int16_t)(consumedHeight +
						  goals_DrawConditionText(
							  g_missionFlightGroups
								  [targetId]
									  .fg
									  .craftType,
							  condition, 9,
							  (int16_t)goalStatus));
			}
		}

		if (timeLimit5SecUnits != 0) {
			uint16_t timeSeconds =
				(uint16_t)(5 * timeLimit5SecUnits);
			uint16_t minutes = (uint16_t)(timeSeconds / 60);
			uint16_t seconds =
				(uint16_t)(timeSeconds - minutes * 60);

			if (seconds < 10) {
				sprintf(timeText, " (%s %ld:0%ld)",
					g_strGoalConjunctions
						[GOAL_CONJ_STR_LESS_THAN],
					(long)minutes, (long)seconds);
			} else {
				sprintf(timeText, " (%s %ld:%ld)",
					g_strGoalConjunctions
						[GOAL_CONJ_STR_LESS_THAN],
					(long)minutes, (long)seconds);
			}
			consumedHeight =
				(int16_t)(consumedHeight +
					  FlightText_GetWrapHeightForString(
						  timeText));
			FlightText_DrawString(timeText);
		}
	} else if (targetType == GOAL_TARGET_GLOBAL_GROUP) {
		if (conditionTextOverride != NULL) {
			consumedHeight =
				(int16_t)(consumedHeight +
					  goals_DrawObjectTypeName(
						  g_missionFlightGroups
							  [targetId]
								  .fg.craftType,
						  0, 0));
			consumedHeight =
				(int16_t)(consumedHeight +
					  FlightText_GetWrapHeightForString(
						  g_missionFlightGroups
							  [targetId]
								  .fg.name));
			FlightText_DrawString(
				g_missionFlightGroups[targetId].fg.name);
			FlightText_DrawString(": ");
			consumedHeight =
				(int16_t)(consumedHeight +
					  FlightText_GetWrapHeightForString(
						  conditionTextOverride));
			FlightText_DrawString(conditionTextOverride);
		} else {
			uint16_t matchingCount = 0;
			uint16_t flightGroupIndex = 0;

			if ((int16_t)g_missionHeader.numFlightGroups > 0) {
				int flightGroupCount =
					(int16_t)
						g_missionHeader.numFlightGroups;

				do {
					if (g_missionFlightGroups
							    [flightGroupIndex]
								    .fg
								    .globalGroup ==
						    targetId &&
					    g_missionFgStats[flightGroupIndex]
							    .arrivalEnabled !=
						    0) {
						++matchingCount;
					}
					++flightGroupIndex;
				} while (flightGroupIndex < flightGroupCount);
			}

			flightGroupIndex = 0;

			if ((int16_t)g_missionHeader.numFlightGroups > 0) {
				do {
					if (g_missionFlightGroups
							    [flightGroupIndex]
								    .fg
								    .globalGroup ==
						    targetId &&
					    g_missionFgStats[flightGroupIndex]
							    .arrivalEnabled !=
						    0) {
						if (g_missionFlightGroups
							    [flightGroupIndex]
								    .fg
								    .numberOfCraft >
						    1) {
							consumedHeight =
								(int16_t)(consumedHeight +
									  goals_DrawObjectTypeName(
										  g_missionFlightGroups[flightGroupIndex]
											  .fg
											  .craftType,
										  0,
										  0));
							consumedHeight =
								(int16_t)(consumedHeight +
									  FlightText_GetWrapHeightForString(
										  g_strGoalConjunctions
											  [GOAL_CONJ_STR_GROUP]));
							FlightText_DrawString(
								g_strGoalConjunctions
									[GOAL_CONJ_STR_GROUP]);
						} else {
							consumedHeight =
								(int16_t)(consumedHeight +
									  goals_DrawObjectTypeName(
										  g_missionFlightGroups[flightGroupIndex]
											  .fg
											  .craftType,
										  0,
										  0));
						}
						--matchingCount;
						consumedHeight =
							(int16_t)(consumedHeight +
								  FlightText_GetWrapHeightForString(
									  g_missionFlightGroups[flightGroupIndex]
										  .fg
										  .name));
						FlightText_DrawString(
							g_missionFlightGroups
								[flightGroupIndex]
									.fg
									.name);

						if (matchingCount == 1) {
							int16_t separatorHeight = FlightText_GetWrapHeightForString(
								g_strGoalConjunctions
									[GOAL_CONJ_STR_AND]);
							consumedHeight =
								(int16_t)(consumedHeight +
									  separatorHeight);
							if (separatorHeight ==
							    0) {
								g_flightDrawCharFn(
									' ');
							}
							FlightText_DrawString(
								g_strGoalConjunctions
									[GOAL_CONJ_STR_AND]);
						} else if (matchingCount > 1) {
							FlightText_DrawString(
								g_strGoalConjunctions
									[GOAL_CONJ_STR_COMMA]);
						}
					}
					++flightGroupIndex;
				} while ((int16_t)g_missionHeader
						 .numFlightGroups >
					 flightGroupIndex);
			}
			FlightText_DrawString(": ");
			consumedHeight =
				(int16_t)(consumedHeight +
					  goals_DrawConditionText(
						  g_missionFlightGroups
							  [targetId]
								  .fg.craftType,
						  condition, amountTextVariant,
						  (int16_t)goalStatus));
		}
	} else {
		switch (targetType) {
		case GOAL_TARGET_SPECIES:
			consumedHeight =
				(int16_t)(consumedHeight +
					  goals_DrawObjectTypeName(
						  (uint16_t)(targetId + 1), 1,
						  0));
			FlightText_DrawString(": ");
			break;

		case GOAL_TARGET_GENUS:
			FlightText_DrawString(
				g_strGoalGenusNames[g_genusConvert[targetId]]);
			FlightText_DrawString(": ");
			break;

		case GOAL_TARGET_FAMILY:
			FlightText_DrawString(
				g_strGoalFamilyNames
					[g_familyConvert[targetId]]);
			FlightText_DrawString(": ");
			break;

		case GOAL_TARGET_IFF:
			if (targetId >= 2) {
				uint16_t nameOffset =
					(uint16_t)(g_missionHeader
							   .iffNames[targetId -
								     2][0] ==
						   '1');
				FlightText_DrawString(
					&g_missionHeader.iffNames[targetId - 2]
								 [nameOffset]);
				FlightText_DrawString(
					g_strGoalSides[GOAL_SIDE_STR_CRAFT]);
			} else {
				FlightText_DrawString(g_strGoalSides[targetId]);
			}
			FlightText_DrawString(": ");
			break;

		case GOAL_TARGET_CRAFT_WHEN:
		case GOAL_TARGET_STATUS:
			consumedHeight =
				(int16_t)(consumedHeight +
					  goals_DrawConditionText(
						  (uint16_t)(targetId + 1), 0,
						  amountTextVariant,
						  (int16_t)goalStatus));
			break;

		case GOAL_TARGET_AI_LEVEL:
			consumedHeight =
				(int16_t)(consumedHeight +
					  FlightText_GetWrapHeightForString(
						  g_strGoalEscape[targetId -
								  1]));
			FlightText_DrawString(g_strGoalEscape[targetId - 1]);
			break;

		default:
			break;
		}
		consumedHeight = (int16_t)(consumedHeight +
					   goals_DrawConditionText(
						   (uint16_t)(targetId + 1),
						   condition, amountTextVariant,
						   (int16_t)goalStatus));
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
		consumedHeight = (int16_t)(consumedHeight +
					   FlightText_GetWrapHeightForString(
						   percentText));
		FlightText_DrawString(percentText);
		FlightText_SetColor(g_goalTitleColorByIndex[goalTitleIndex]);
	}
	g_flightDrawCharFn('\n');
	return consumedHeight;
}

/* Draws the condition text for a craft species and returns
 * FlightText_GetWrapHeightForString's extra height for it. The row is
 * condition plus conditionRowBase; the column is amountTextVariant, or 0 when
 * the condition has one wording in g_goalConditionTextVariantCount. A species
 * with a model index takes the table of its g_craftGender; species
 * CRAFT_SPECIES_COMM_SAT_1 to CRAFT_SPECIES_NAV_BUOY_TYPE_2 with none take
 * g_strGoalCondMasculine. Any other species draws nothing and returns 0. Does
 * not check condition (0 to 47) or that the chosen entry was loaded. */
// FUNCTION: XVT 0x415A70
int16_t goals_DrawConditionText(unsigned int craftSpecies, uint16_t condition,
				uint16_t amountTextVariant,
				int16_t conditionRowBase)
{
	const char *text;
	int16_t wrapHeight;
	ModelIndex modelIndex;

	text = NULL;
	wrapHeight = 0;
	if (g_goalConditionTextVariantCount[condition] == 1) {
		amountTextVariant = 0;
	}

	condition = (uint16_t)(condition + conditionRowBase);
	modelIndex = GetModelIndexFromType(craftSpecies);
	if (modelIndex != MODEL_INDEX_NONE) {
		switch (g_craftGender[modelIndex]) {
		case CRAFT_GENDER_MASCULINE:
			text = g_strGoalCondMasculine[condition]
						     [amountTextVariant];
			break;
		case CRAFT_GENDER_FEMININE:
			text = g_strGoalCondFeminine[condition]
						    [amountTextVariant];
			break;
		case CRAFT_GENDER_NEUTERED:
			text = g_strGoalCondNeutered[condition]
						    [amountTextVariant];
			break;
		}

		wrapHeight = FlightText_GetWrapHeightForString(text);
		FlightText_DrawString(text);
		return wrapHeight;
	}

	if ((uint16_t)craftSpecies >= CRAFT_SPECIES_COMM_SAT_1 &&
	    (uint16_t)craftSpecies <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
		text = g_strGoalCondMasculine[condition][amountTextVariant];
		wrapHeight = FlightText_GetWrapHeightForString(text);
		FlightText_DrawString(text);
	}
	return wrapHeight;
}

/* Draws a craft species' name and returns FlightText_GetWrapHeightForString's
 * extra height for it. A species with a model index draws its plural name
 * when usePluralName is nonzero, else its short name when useShortName is
 * nonzero, else its long name; species CRAFT_SPECIES_COMM_SAT_1 to
 * CRAFT_SPECIES_NAV_BUOY_TYPE_2 with none draw their
 * g_strSatMineProbeBuoyPilotNames entry. For any other species the modern
 * build draws an empty string; the original build leaves the name pointer
 * unset. */
// FUNCTION: XVT 0x415BA0
int16_t goals_DrawObjectTypeName(uint16_t craftSpecies, int16_t usePluralName,
				 int16_t useShortName)
{
	ModelIndex modelIndex;
	int16_t wrapHeight;
	const char *displayName;

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
	} else if (craftSpecies >= CRAFT_SPECIES_COMM_SAT_1 &&
		   craftSpecies <= CRAFT_SPECIES_NAV_BUOY_TYPE_2) {
		displayName = g_strSatMineProbeBuoyPilotNames
			[craftSpecies - CRAFT_SPECIES_COMM_SAT_1];
	}

	wrapHeight = FlightText_GetWrapHeightForString(displayName);
	FlightText_DrawString(displayName);
	return wrapHeight;
}
