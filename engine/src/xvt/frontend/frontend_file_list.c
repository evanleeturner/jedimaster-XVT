#include "xvt/frontend/frontend_file_list.h"
#ifdef XVT_MODERN
#include "xvt_runtime/compat/frontend_file_list_port.h"
#endif

#include <stdlib.h>
#include <string.h>
#ifndef XVT_MODERN
#include <direct.h>

typedef int FrontendFindHandle;

/* The original build's copy of the Windows find record (WIN32_FIND_DATAA),
 * which FindFirstFileA and FindNextFileA fill; only the file name is used. */
struct FrontendFindData {
	uint32_t fileAttributes;     /* Never read or written by name. */
	uint32_t creationTimeLow;    /* Never read or written by name. */
	uint32_t creationTimeHigh;   /* Never read or written by name. */
	uint32_t lastAccessTimeLow;  /* Never read or written by name. */
	uint32_t lastAccessTimeHigh; /* Never read or written by name. */
	uint32_t lastWriteTimeLow;   /* Never read or written by name. */
	uint32_t lastWriteTimeHigh;  /* Never read or written by name. */
	uint32_t fileSizeHigh;	     /* Never read or written by name. */
	uint32_t fileSizeLow;	     /* Never read or written by name. */
	uint32_t reserved0;	     /* Never read or written by name. */
	uint32_t reserved1;	     /* Never read or written by name. */
	/* Name of the file found, without its folder, ended by a 0. */
	char fileName[260];
	char alternateFileName[14]; /* Never read or written by name. */
	uint8_t trailingPadding[2]; /* Never read or written by name. */
};

__declspec(dllimport) FrontendFindHandle __stdcall
FindFirstFileA(const char *fileName, struct FrontendFindData *findData);
__declspec(dllimport) int __stdcall
FindNextFileA(FrontendFindHandle findHandle, struct FrontendFindData *findData);
__declspec(dllimport) int __stdcall FindClose(FrontendFindHandle findHandle);

#endif

/* Lists the files matching wildcard in a new list for FrontendFileList_Free,
 * sorted by strcmp of their names: byte order, so capitals sort before small
 * letters. The original build stores bare file names, lists matching folders
 * too, returns an empty list when nothing matches, NULL when memory runs out
 * before the first name is stored, and keeps the names it has when memory
 * runs out later. It saves the working directory and changes back to it on
 * every path, though nothing between changes it. The modern build hands the
 * whole job to FrontendFileList_BuildSortedModern, which differs as its
 * header says. */
// FUNCTION: XVT 0x4DFA10
struct FrontendFileList *FrontendFileList_BuildSorted(const char *wildcard)
{
#ifdef XVT_MODERN
	return FrontendFileList_BuildSortedModern(wildcard);
#else
	char currentDirectory[256];
	struct FrontendFileList *list;
	struct FrontendFileListNode *node;
	FrontendFindHandle findHandle;
	extern struct FrontendFindData FindFileData;

	_getcwd(currentDirectory, sizeof(currentDirectory));
	list = (struct FrontendFileList *)malloc(sizeof(*list));
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
	node = (struct FrontendFileListNode *)malloc(sizeof(*node));
	if (node == NULL) {
		_chdir(currentDirectory);
		free(list);
		FindClose(findHandle);
		return NULL;
	}
	node->path = (char *)malloc(strlen(FindFileData.fileName) + 2);
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
		node = (struct FrontendFileListNode *)malloc(sizeof(*node));
		if (node == NULL) {
			break;
		}
		node->path = (char *)malloc(strlen(FindFileData.fileName) + 2);
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

/* Frees every node's path, every node and the list itself; does nothing for
 * NULL. */
// FUNCTION: XVT 0x4DFC30
void FrontendFileList_Free(struct FrontendFileList *list)
{
	struct FrontendFileListNode *node;
	struct FrontendFileListNode *next;

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
/* A node whose path equals one already listed goes after it. Called by
 * FrontendFileList_BuildSorted for every file after the first; in the modern
 * build by FrontendFileList_CollectModernFile. */
// FUNCTION: XVT 0x4DFC70
void FrontendFileList_InsertNodeSorted(struct FrontendFileList *list,
				       struct FrontendFileListNode *node)
{
	struct FrontendFileListNode *cursor;

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
