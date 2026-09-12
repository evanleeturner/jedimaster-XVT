#include "xvt_runtime/compat/frontend_file_list_port.h"

#include "aeron/vfs.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt_runtime/storage/storage.h"

#include <stdlib.h>
#include <string.h>

typedef struct FrontendFileListBuildState {
	FrontendFileList* list;
	char directory[XVT_PATH_CAPACITY];
} FrontendFileListBuildState;

static int FrontendFileList_CollectModernFile(void* userdata, const AeronVfsEntry* entry) {
	FrontendFileListBuildState* state;
	FrontendFileListNode* node;

	state = (FrontendFileListBuildState*)userdata;
	node = (FrontendFileListNode*)malloc(sizeof(*node));
	if (node == NULL) {
		return 0;
	}
	node->path = (char*)malloc(strlen(state->directory) + strlen(entry->name) + 2);
	if (node->path == NULL) {
		free(node);
		return 0;
	}
	strcpy(node->path, state->directory);
	strcat(node->path, entry->name);
	node->next = NULL;
	if (state->list->head == NULL) {
		state->list->head = node;
		state->list->count = 1;
	} else {
		FrontendFileList_InsertNodeSorted(state->list, node);
	}
	return 1;
}

FrontendFileList* FrontendFileList_BuildSortedModern(const char* wildcard) {
	FrontendFileListBuildState state;

	state.list = (FrontendFileList*)malloc(sizeof(*state.list));
	if (state.list == NULL) {
		return NULL;
	}
	state.list->head = NULL;
	state.list->count = 0;
	if (!XvtStorage_Normalize(wildcard, state.directory, sizeof(state.directory))) {
		free(state.list);
		return NULL;
	}
	char* slash = strrchr(state.directory, '/');
	if (slash)
		slash[1] = 0;
	else
		state.directory[0] = 0;
	if (!XvtStorage_Glob(AERON_VFS_ROOT_USER, wildcard, FrontendFileList_CollectModernFile, &state)) {
		FrontendFileListNode* node = state.list->head;
		while (node) {
			FrontendFileListNode* next = node->next;
			free(node->path);
			free(node);
			node = next;
		}
		free(state.list);
		return NULL;
	}
	return state.list;
}
