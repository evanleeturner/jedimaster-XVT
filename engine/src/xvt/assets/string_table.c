#include "xvt/assets/string_table.h"
#include "xvt/assets/file.h"

#include "xvt/assets/opt_model.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/util/memory.h"
#include <string.h>

// GLOBAL: XVT 0xA60A40
char* g_strFileErrorMessages[4] = { 0 };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4248B0
void StringTable_LoadGameStrings(int loadFromDisk) {
	enum {
		STRING_LINE_CAPACITY = 1024,
		DEFAULT_STRING_DATA_CAPACITY = 0x7D00,
		FILE_ERROR_STRING_COUNT = 36,
		FILE_ERROR_PREFIX_COUNT = 4,
		MODEL_STRING_COUNT = 73,
		GOAL_CONDITION_COUNT = 188,
		GOAL_VARIANT_COUNT = 47,
	};

	XvtFile* stream;
	int hasGender;
	int entryIndex;
	char* writePtr;
	char** modelName;
	char line[STRING_LINE_CAPACITY];
	size_t fileSize;
	int lineLength;
	int conditionVariantIndex;
	int variantIndex;

	if (loadFromDisk == 0) {
		return;
	}

	File_OpenGlobalStream("strings.txt", "r", 1, 0);
	stream = (XvtFile*)g_stream;
	if (stream != NULL) {
		File_RawSeek(stream, 0, SEEK_END);
		fileSize = (size_t)File_RawTell(stream);
		if (fileSize > DEFAULT_STRING_DATA_CAPACITY) {
			Memory_FreeHandle(g_stringDataHandle);
			g_stringDataHandle = Memory_AllocHandle(fileSize, 0);
			if (g_stringDataHandle == 0) {
				FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
			}
		}
		writePtr = (char*)Memory_LockHandle(g_stringDataHandle);
		File_RawSeek(stream, 0, SEEK_SET);
		if (File_RawTell(stream) != 0) {
			File_RawClose(stream);
			File_OpenGlobalStream("strings.txt", "r", 1, 0);
			stream = (XvtFile*)g_stream;
		}
		if (stream != NULL) {
			if (writePtr != NULL) {
				for (entryIndex = 0;
					 entryIndex < (int)(sizeof(g_strDamageSystemNames) / sizeof(g_strDamageSystemNames[0]));
					 ++entryIndex) {
					int length;

					length = StringTable_ReadNonCommentLine(stream, line);
					if (length == -1) {
						writePtr = NULL;
						break;
					}
					memcpy(writePtr, line, (size_t)length + 1);
					g_strDamageSystemNames[entryIndex] = writePtr;
					writePtr += length + 1;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < FILE_ERROR_STRING_COUNT) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					if (entryIndex < FILE_ERROR_PREFIX_COUNT) {
						memcpy(writePtr, line, lineLength + 1);
						g_strFileErrorMessages[entryIndex] = writePtr;
					} else {
						memcpy(writePtr, line, lineLength + 1);
						g_strDiskIoMessages[entryIndex - FILE_ERROR_PREFIX_COUNT] = writePtr;
					}
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_provingGroundsStatusLabels) /
										  sizeof(g_provingGroundsStatusLabels[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_provingGroundsStatusLabels[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < 1) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalEscape[0] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				for (entryIndex = 0; entryIndex < GOAL_CONDITION_COUNT; ++entryIndex) {
					conditionVariantIndex = entryIndex % GOAL_VARIANT_COUNT;
					variantIndex = 0;
					while (variantIndex < g_goalConditionTextVariantCount[conditionVariantIndex]) {
						if (File_Gets(line, sizeof(line), stream) == NULL) {
							writePtr = NULL;
							break;
						}
						line[sizeof(line) - 1] = '\0';
						if (line[0] == '/' && line[1] == '/') {
							continue;
						}
						lineLength = (int)strlen(line);
						if (line[lineLength - 1] == '\n') {
							line[--lineLength] = '\0';
						}
						memcpy(writePtr, line, lineLength + 1);
						g_strGoalCondMasculine[entryIndex][variantIndex] = writePtr;
						writePtr += lineLength + 1;
						++variantIndex;
					}
					if (writePtr == NULL) {
						break;
					}
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strGoalPercentages) / sizeof(g_strGoalPercentages[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalPercentages[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strGoalOperators) / sizeof(g_strGoalOperators[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalOperators[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strGoalTitles) / sizeof(g_strGoalTitles[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalTitles[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strGoalConjunctions) / sizeof(g_strGoalConjunctions[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalConjunctions[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strGoalSides) / sizeof(g_strGoalSides[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalSides[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex <
					   (int)(sizeof(g_strGoalFamilyNames0To6) / sizeof(g_strGoalFamilyNames0To6[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalFamilyNames0To6[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex <
					   (int)(sizeof(g_strGoalFamilyNames7To22) / sizeof(g_strGoalFamilyNames7To22[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strGoalFamilyNames7To22[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strMapRoomText) / sizeof(g_strMapRoomText[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strMapRoomText[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strInFlightMessages) / sizeof(g_strInFlightMessages[0]))) {
					int sourceIndex;
					char* readPtr;

					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					g_strInFlightMessages[entryIndex] = writePtr;
					readPtr = line;
					sourceIndex = 0;
					while (lineLength > sourceIndex) {
						if (*readPtr == '\\') {
							if (readPtr[1] == '0') {
								*writePtr++ = (char)(readPtr[2] - '0');
							} else {
								*writePtr++ = (char)(readPtr[2] - '(');
							}
							readPtr += 3;
							sourceIndex += 3;
						} else {
							*writePtr++ = *readPtr++;
							++sourceIndex;
						}
					}
					*writePtr++ = '\0';
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex <
					   (int)(sizeof(g_strCmdThreatDisplayText) / sizeof(g_strCmdThreatDisplayText[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strCmdThreatDisplayText[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strWaypointNames) / sizeof(g_strWaypointNames[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strWaypointNames[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex <
					   (int)(sizeof(g_strMeshComponentNames) / sizeof(g_strMeshComponentNames[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strMeshComponentNames[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex <
					   (int)(sizeof(g_strCockpitOverlayText) / sizeof(g_strCockpitOverlayText[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strCockpitOverlayText[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex <
					   (int)(sizeof(g_strThreatDisplayText) / sizeof(g_strThreatDisplayText[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strThreatDisplayText[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strStatusStrings) / sizeof(g_strStatusStrings[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strStatusStrings[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strWarheadNames) / sizeof(g_strWarheadNames[0])) + 1) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					if (entryIndex == (int)(sizeof(g_strWarheadNames) / sizeof(g_strWarheadNames[0]))) {
						memcpy(writePtr, line, lineLength + 1);
						g_strWarheadUnknown = writePtr;
					} else {
						memcpy(writePtr, line, lineLength + 1);
						g_strWarheadNames[entryIndex] = writePtr;
					}
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strBuoyNames) / sizeof(g_strBuoyNames[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strBuoyNames[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				for (entryIndex = 0; entryIndex < MODEL_STRING_COUNT;) {
					modelName = &g_modelDefs[entryIndex].nameLong;
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					*modelName = writePtr;
					if (line[0] == 'm') {
						g_craftGender[entryIndex] = CRAFT_GENDER_MASCULINE;
					} else if (line[0] == 'f') {
						g_craftGender[entryIndex] = CRAFT_GENDER_FEMININE;
					} else if (line[0] == 'n') {
						g_craftGender[entryIndex] = CRAFT_GENDER_NEUTERED;
					} else {
						FeDiskIo_FatalError(FILE_ERROR_STR_PRESS_KEY_TO_EXIT);
					}
					memcpy(writePtr, &line[2], lineLength - 1);
					writePtr += lineLength - 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex <
					   (int)(sizeof(g_strSpeciesNamesPlural) / sizeof(g_strSpeciesNamesPlural[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strSpeciesNamesPlural[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				while (entryIndex < (int)(sizeof(g_strWingmanCommands) / sizeof(g_strWingmanCommands[0]))) {
					if (File_Gets(line, sizeof(line), stream) == NULL) {
						writePtr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					lineLength = (int)strlen(line);
					if (line[lineLength - 1] == '\n') {
						line[--lineLength] = '\0';
					}
					memcpy(writePtr, line, lineLength + 1);
					g_strWingmanCommands[entryIndex] = writePtr;
					writePtr += lineLength + 1;
					++entryIndex;
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				hasGender = 0;
				while (entryIndex < MODEL_STRING_COUNT) {
					if (g_craftGender[entryIndex] == CRAFT_GENDER_FEMININE) {
						hasGender = 1;
						break;
					}
					++entryIndex;
				}
				if (hasGender != 0) {
					for (entryIndex = 0; entryIndex < GOAL_CONDITION_COUNT; ++entryIndex) {
						conditionVariantIndex = entryIndex % GOAL_VARIANT_COUNT;
						variantIndex = 0;
						while (variantIndex < g_goalConditionTextVariantCount[conditionVariantIndex]) {
							if (File_Gets(line, sizeof(line), stream) == NULL) {
								writePtr = NULL;
								break;
							}
							line[sizeof(line) - 1] = '\0';
							if (line[0] == '/' && line[1] == '/') {
								continue;
							}
							lineLength = (int)strlen(line);
							if (line[lineLength - 1] == '\n') {
								line[--lineLength] = '\0';
							}
							memcpy(writePtr, line, lineLength + 1);
							g_strGoalCondFeminine[entryIndex][variantIndex] = writePtr;
							writePtr += lineLength + 1;
							++variantIndex;
						}
						if (writePtr == NULL) {
							break;
						}
					}
				}
			}

			if (writePtr != NULL) {
				entryIndex = 0;
				hasGender = 0;
				while (entryIndex < MODEL_STRING_COUNT) {
					if (g_craftGender[entryIndex] == CRAFT_GENDER_NEUTERED) {
						hasGender = 1;
						break;
					}
					++entryIndex;
				}
				if (hasGender != 0) {
					for (entryIndex = 0; entryIndex < GOAL_CONDITION_COUNT; ++entryIndex) {
						conditionVariantIndex = entryIndex % GOAL_VARIANT_COUNT;
						variantIndex = 0;
						while (variantIndex < g_goalConditionTextVariantCount[conditionVariantIndex]) {
							if (File_Gets(line, sizeof(line), stream) == NULL) {
								writePtr = NULL;
								break;
							}
							line[sizeof(line) - 1] = '\0';
							if (line[0] == '/' && line[1] == '/') {
								continue;
							}
							lineLength = (int)strlen(line);
							if (line[lineLength - 1] == '\n') {
								line[--lineLength] = '\0';
							}
							memcpy(writePtr, line, lineLength + 1);
							g_strGoalCondNeutered[entryIndex][variantIndex] = writePtr;
							writePtr += lineLength + 1;
							++variantIndex;
						}
						if (writePtr == NULL) {
							break;
						}
					}
				}
			}

			if (writePtr != NULL) {
				File_RawClose(stream);
				return;
			}
		}
	}
	File_RawClose(stream);
	FeDiskIo_FatalError(FILE_ERROR_STR_STRINGS_OUT_OF_SYNC);
}

// FUNCTION: XVT 0x425A70
int StringTable_ReadNonCommentLine(XvtFile* stream, char* buffer) {
	int lineLength;

	do {
		if (File_Gets(buffer, 1024, stream) == 0) {
			return -1;
		}
		buffer[1023] = 0;
	} while (buffer[0] == '/' && buffer[1] == '/');

	lineLength = strlen(buffer);
	if (buffer[lineLength - 1] == '\n') {
		buffer[lineLength - 1] = 0;
		--lineLength;
	}
	return lineLength;
}
