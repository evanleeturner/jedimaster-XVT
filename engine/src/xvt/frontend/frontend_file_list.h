#ifndef XVT_FRONTEND_FRONTEND_FILE_LIST_H
#define XVT_FRONTEND_FRONTEND_FILE_LIST_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One file in a FrontendFileList. */
struct FrontendFileListNode {
	/* Heap copy of the file's name; the modern build puts the wildcard's
	 * folder in front of it. */
	char *path;
	/* Next file in strcmp order of path; NULL after the last. */
	struct FrontendFileListNode *next;
};

/* Files matching a wildcard, from FrontendFileList_BuildSorted. */
struct FrontendFileList {
	FrontendFileListNode *head; /* First file; NULL when none matched. */
	int count;		    /* Files in the list. */
};

FrontendFileList *FrontendFileList_BuildSorted(const char *wildcard);
void FrontendFileList_Free(FrontendFileList *list);
void FrontendFileList_InsertNodeSorted(FrontendFileList *list,
				       FrontendFileListNode *node);

#ifdef __cplusplus
}
#endif

#endif
