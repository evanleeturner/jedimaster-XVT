#ifndef XVT_FRONTEND_FRONTEND_FILE_LIST_H
#define XVT_FRONTEND_FRONTEND_FILE_LIST_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FrontendFileListNode {
	char* path;
	struct FrontendFileListNode* next;
};

struct FrontendFileList {
	FrontendFileListNode* head;
	int count;
};

FrontendFileList* FrontendFileList_BuildSorted(const char* wildcard);
void FrontendFileList_Free(FrontendFileList* list);
void FrontendFileList_InsertNodeSorted(FrontendFileList* list, FrontendFileListNode* node);

#ifdef __cplusplus
}
#endif

#endif
