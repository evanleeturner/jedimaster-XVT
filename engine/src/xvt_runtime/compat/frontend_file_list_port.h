#ifndef XVT_RUNTIME_COMPAT_FRONTEND_FILE_LIST_PORT_H
#define XVT_RUNTIME_COMPAT_FRONTEND_FILE_LIST_PORT_H

#include "xvt/xvt_typedefs.h"

/* The modern body of FrontendFileList_BuildSorted: lists the files, never folders, in USER that match
 * wildcard in any letter case, sorted by strcmp over their paths. Each node's path is the wildcard's
 * folder part plus the file name; the original stored the bare file name. Returns a list to release
 * with FrontendFileList_Free, empty when nothing matched; NULL when the wildcard is rejected, memory
 * runs out, or the listing fails. The original returned an empty list when nothing matched and kept a
 * partial list when memory ran out after the first file. */
FrontendFileList *FrontendFileList_BuildSortedModern(const char *wildcard);

#endif
