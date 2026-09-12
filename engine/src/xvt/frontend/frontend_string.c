#include "xvt/frontend/frontend_string.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/frontend_state.h"

#include <stdlib.h>
#include <string.h>

// FUNCTION: XVT 0x4DD9D0
void FrontendString_LoadTable(char* fileName) {
	unsigned int totalDataSize;
	XvtFile* stream;
	char* writePosition;
	char* copyDestination;
	char* resizedData;
	unsigned int* resizedOffsets;
	unsigned int lineLength;
	unsigned int copyLength;
	unsigned int stringOffset;
	char line[1024];

	totalDataSize = 0;
	stream = File_Open(fileName, "r");
	if (stream == NULL)
		return;

	FrontendString_UnloadTable();
	g_frontState.uiStringOffsets = malloc(64 * sizeof(*g_frontState.uiStringOffsets));
	if (g_frontState.uiStringOffsets == NULL) {
		File_Close(stream);
		return;
	}

	g_frontState.uiStringCapacity = 64;
	g_frontState.uiStringData = malloc(File_GetSize(stream));
	if (g_frontState.uiStringData == NULL) {
		FrontendString_UnloadTable();
		File_Close(stream);
		return;
	}

	writePosition = g_frontState.uiStringData;
	for (;;) {
		if (File_Gets(line, sizeof(line), stream) == NULL)
			break;
		line[sizeof(line) - 1] = '\0';
		if (line[0] != '/' || line[1] != '/') {
			lineLength = strlen(line) + 1;
			copyLength = lineLength - 1;
			if (line[lineLength - 2] == '\n') {
				--copyLength;
				line[lineLength - 2] = '\0';
			}

			stringOffset = writePosition - g_frontState.uiStringData;
			copyDestination = writePosition;
			totalDataSize += copyLength + 1;
			writePosition += copyLength + 1;
			g_frontState.uiStringOffsets[g_frontState.uiStringCount] = stringOffset;
			memcpy(copyDestination, line, copyLength + 1);
			++g_frontState.uiStringCount;
			if (g_frontState.uiStringCapacity == g_frontState.uiStringCount) {
				resizedOffsets =
					realloc(g_frontState.uiStringOffsets,
							(g_frontState.uiStringCapacity + 64) * sizeof(*g_frontState.uiStringOffsets));
				if (resizedOffsets == NULL)
					break;
				g_frontState.uiStringOffsets = resizedOffsets;
				g_frontState.uiStringCapacity += 64;
			}
		}
	}

	resizedData = realloc(g_frontState.uiStringData, totalDataSize);
	if (resizedData != NULL) {
		g_frontState.uiStringData = resizedData;
	} else {
		free(g_frontState.uiStringData);
		free(g_frontState.uiStringOffsets);
		g_frontState.uiStringCount = 0;
		g_frontState.uiStringData = NULL;
		g_frontState.uiStringOffsets = NULL;
	}

	File_Close(stream);
}

// FUNCTION: XVT 0x4DDBC0
void FrontendString_UnloadTable(void) {
	unsigned int** offsets;
	char** data;

	offsets = &g_frontState.uiStringOffsets;
	data = &g_frontState.uiStringData;
	if (*offsets) {
		free(*offsets);
		*offsets = 0;
	}

	if (*data) {
		free(*data);
		*data = 0;
	}

	g_frontState.uiStringCount = 0;
	g_frontState.uiStringCapacity = 0;
}

// FUNCTION: XVT 0x4DDC10
const char* FrontendString_Get(FrontendStringId index) {
	if ((unsigned int)index >= g_frontState.uiStringCount) {
		return "No text.";
	}
	return &g_frontState.uiStringData[g_frontState.uiStringOffsets[index]];
}
