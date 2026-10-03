#include "xvt_runtime/compat/frontend_file_list_port.h"

#include "aeron/vfs.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt_runtime/storage/storage.h"

#include <stdlib.h>
#include <string.h>

struct frontend_file_list_build_state {
	struct frontend_file_list *list;
	char directory[XVT_PATH_CAPACITY];
};

static int frontend_file_list_collect_modern_file(void *userdata,
						  const AeronVfsEntry *entry)
{
	struct frontend_file_list_build_state *state;
	struct frontend_file_list_node *node;

	state = (struct frontend_file_list_build_state *)userdata;
	node = (struct frontend_file_list_node *)malloc(sizeof(*node));
	if (node == NULL) {
		return 0;
	}
	node->path = (char *)malloc(strlen(state->directory) +
				    strlen(entry->name) + 2);
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
		frontend_file_list_insert_node_sorted(state->list, node);
	}
	return 1;
}

struct frontend_file_list *
frontend_file_list_build_sorted_modern(const char *wildcard)
{
	struct frontend_file_list_build_state state;

	state.list = (struct frontend_file_list *)malloc(sizeof(*state.list));
	if (state.list == NULL) {
		return NULL;
	}
	state.list->head = NULL;
	state.list->count = 0;
	if (!xvt_storage_normalize(wildcard, state.directory,
				   sizeof(state.directory))) {
		free(state.list);
		return NULL;
	}
	char *slash = strrchr(state.directory, '/');
	if (slash) {
		slash[1] = 0;
	} else {
		state.directory[0] = 0;
	}
	if (!xvt_storage_glob(AERON_VFS_ROOT_USER, wildcard,
			      frontend_file_list_collect_modern_file, &state)) {
		struct frontend_file_list_node *node = state.list->head;
		while (node) {
			struct frontend_file_list_node *next = node->next;
			free(node->path);
			free(node);
			node = next;
		}
		free(state.list);
		return NULL;
	}
	return state.list;
}
