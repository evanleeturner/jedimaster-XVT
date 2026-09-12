#include "xvt/frontend/frontend_file_list.h"
#ifdef XVT_MODERN
#include "xvt_runtime/compat/frontend_file_list_port.h"
#endif

#include <stdlib.h>
#include <string.h>
#ifndef XVT_MODERN
#include <direct.h>

typedef int FrontendFindHandle;

typedef struct FrontendFindData {
	uint32_t fileAttributes;
	uint32_t creationTimeLow;
	uint32_t creationTimeHigh;
	uint32_t lastAccessTimeLow;
	uint32_t lastAccessTimeHigh;
	uint32_t lastWriteTimeLow;
	uint32_t lastWriteTimeHigh;
	uint32_t fileSizeHigh;
	uint32_t fileSizeLow;
	uint32_t reserved0;
	uint32_t reserved1;
	char fileName[260];
	char alternateFileName[14];
	uint8_t trailingPadding[2];
} FrontendFindData;

__declspec(dllimport) FrontendFindHandle __stdcall FindFirstFileA(const char* fileName,
																  FrontendFindData* findData);
__declspec(dllimport) int __stdcall FindNextFileA(FrontendFindHandle findHandle, FrontendFindData* findData);
__declspec(dllimport) int __stdcall FindClose(FrontendFindHandle findHandle);

#endif

// FUNCTION: XVT 0x4DFA10
FrontendFileList* FrontendFileList_BuildSorted(const char* wildcard) {
#ifdef XVT_MODERN
	return FrontendFileList_BuildSortedModern(wildcard);
#else
	char currentDirectory[256];
	FrontendFileList* list;
	FrontendFileListNode* node;
	FrontendFindHandle findHandle;
	extern FrontendFindData FindFileData;

	_getcwd(currentDirectory, sizeof(currentDirectory));
	list = (FrontendFileList*)malloc(sizeof(*list));
	if (list == NULL) {
		_chdir(currentDirectory);
		return NULL;
	}
	findHandle = FindFirstFileA(wildcard, &FindFileData);
	if (findHandle == -1) {
		_chdir(currentDirectory);
		list->head = NULL;
		list->count = 0;
		return list;
	}
	node = (FrontendFileListNode*)malloc(sizeof(*node));
	if (node == NULL) {
		_chdir(currentDirectory);
		free(list);
		FindClose(findHandle);
		return NULL;
	}
	node->path = (char*)malloc(strlen(FindFileData.fileName) + 2);
	if (node->path == NULL) {
		_chdir(currentDirectory);
		free(node);
		free(list);
		FindClose(findHandle);
		return NULL;
	}
	strcpy(node->path, FindFileData.fileName);
	node->next = NULL;
	list->head = node;
	list->count = 1;
	while (FindNextFileA(findHandle, &FindFileData)) {
		node = (FrontendFileListNode*)malloc(sizeof(*node));
		if (node == NULL) {
			break;
		}
		node->path = (char*)malloc(strlen(FindFileData.fileName) + 2);
		if (node->path == NULL) {
			free(node);
			break;
		}
		strcpy(node->path, FindFileData.fileName);
		node->next = NULL;
		FrontendFileList_InsertNodeSorted(list, node);
	}
	_chdir(currentDirectory);
	FindClose(findHandle);
	return list;
#endif
}

// FUNCTION: XVT 0x4DFC30
void FrontendFileList_Free(FrontendFileList* list) {
	FrontendFileListNode* node;
	FrontendFileListNode* next;

	if (list != NULL) {
		node = list->head;
		while (node != NULL) {
			next = node->next;
			free(node->path);
			free(node);
			node = next;
		}
		free(list);
	}
}

/* Inserts a filename node into a lexicographically sorted singly linked list
 * and increments the list count. The head node is assumed to exist. */
// FUNCTION: XVT 0x4DFC70
void FrontendFileList_InsertNodeSorted(FrontendFileList* list, FrontendFileListNode* node) {
	FrontendFileListNode* cursor;

	cursor = list->head;
	if (strcmp(node->path, cursor->path) < 0) {
		node->next = cursor;
		list->head = node;
		++list->count;
		return;
	}
	while (cursor->next != NULL) {
		if (strcmp(node->path, cursor->next->path) < 0) {
			node->next = cursor->next;
			cursor->next = node;
			++list->count;
			return;
		}
		cursor = cursor->next;
	}
	cursor->next = node;
	++list->count;
}
