#ifndef XVT_FRONTEND_FRONTEND_FILE_LIST_H
#define XVT_FRONTEND_FRONTEND_FILE_LIST_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One file in a frontend_file_list. */
struct frontend_file_list_node {
	/* Heap copy of the file's name, with the wildcard's folder in front of
	 * it. */
	char *path;
	/* Next file in strcmp order of path; NULL after the last. */
	struct frontend_file_list_node *next;
};

/* Files matching a wildcard, from frontend_file_list_build_sorted. */
struct frontend_file_list {
	struct frontend_file_list_node
		*head; /* First file; NULL when none matched. */
	int count;     /* Files in the list. */
};

struct frontend_file_list *
frontend_file_list_build_sorted(const char *wildcard);
void frontend_file_list_free(struct frontend_file_list *list);
void frontend_file_list_insert_node_sorted(
	struct frontend_file_list *list, struct frontend_file_list_node *node);

#ifdef __cplusplus
}
#endif

#endif
