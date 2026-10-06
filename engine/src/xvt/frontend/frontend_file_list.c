#include "xvt/frontend/frontend_file_list.h"

#include <stdlib.h>
#include <string.h>

#include "xvt_runtime/compat/frontend_file_list_port.h"

/* Lists the files matching wildcard in a new list for frontend_file_list_free,
 * sorted by strcmp of their names: byte order, so capitals sort before small
 * letters. It hands the whole job to frontend_file_list_build_sorted_modern,
 * which differs from the 1997 game as its header says. */
// FUNCTION: XVT 0x4DFA10
struct frontend_file_list *frontend_file_list_build_sorted(const char *wildcard)
{
	return frontend_file_list_build_sorted_modern(wildcard);
}

/* Frees every node's path, every node and the list itself; does nothing for
 * NULL. */
// FUNCTION: XVT 0x4DFC30
void frontend_file_list_free(struct frontend_file_list *list)
{
	if (list != NULL) {
		struct frontend_file_list_node *node = list->head;
		while (node != NULL) {
			struct frontend_file_list_node *next = node->next;
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
 * frontend_file_list_collect_modern_file. */
// FUNCTION: XVT 0x4DFC70
void frontend_file_list_insert_node_sorted(struct frontend_file_list *list,
					   struct frontend_file_list_node *node)
{
	struct frontend_file_list_node *cursor = list->head;
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
