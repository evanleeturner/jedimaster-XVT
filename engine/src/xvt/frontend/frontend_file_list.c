#include "xvt/frontend/frontend_file_list.h"
#ifdef XVT_MODERN
#include "xvt_runtime/compat/frontend_file_list_port.h"
#endif

#include <stdlib.h>
#include <string.h>
#ifndef XVT_MODERN
#include <direct.h>

typedef int frontend_find_handle;

/* The original build's copy of the Windows find record (WIN32_FIND_DATAA),
 * which FindFirstFileA and FindNextFileA fill; only the file name is used. */
struct frontend_find_data {
	uint32_t file_attributes;	/* Never read or written by name. */
	uint32_t creation_time_low;	/* Never read or written by name. */
	uint32_t creation_time_high;	/* Never read or written by name. */
	uint32_t last_access_time_low;	/* Never read or written by name. */
	uint32_t last_access_time_high; /* Never read or written by name. */
	uint32_t last_write_time_low;	/* Never read or written by name. */
	uint32_t last_write_time_high;	/* Never read or written by name. */
	uint32_t file_size_high;	/* Never read or written by name. */
	uint32_t file_size_low;		/* Never read or written by name. */
	uint32_t reserved0;		/* Never read or written by name. */
	uint32_t reserved1;		/* Never read or written by name. */
	/* Name of the file found, without its folder, ended by a 0. */
	char file_name[260];
	char alternate_file_name[14]; /* Never read or written by name. */
	uint8_t trailing_padding[2];  /* Never read or written by name. */
};

__declspec(dllimport) frontend_find_handle __stdcall
FindFirstFileA(const char *file_name, struct frontend_find_data *find_data);
__declspec(dllimport) int __stdcall
FindNextFileA(frontend_find_handle find_handle,
	      struct frontend_find_data *find_data);
__declspec(dllimport) int __stdcall FindClose(frontend_find_handle find_handle);

#endif

/* Lists the files matching wildcard in a new list for frontend_file_list_free,
 * sorted by strcmp of their names: byte order, so capitals sort before small
 * letters. The original build stores bare file names, lists matching folders
 * too, returns an empty list when nothing matches, NULL when memory runs out
 * before the first name is stored, and keeps the names it has when memory
 * runs out later. It saves the working directory and changes back to it on
 * every path, though nothing between changes it. The modern build hands the
 * whole job to frontend_file_list_build_sorted_modern, which differs as its
 * header says. */
// FUNCTION: XVT 0x4DFA10
struct frontend_file_list *frontend_file_list_build_sorted(const char *wildcard)
{
#ifdef XVT_MODERN
	return frontend_file_list_build_sorted_modern(wildcard);
#else
	char current_directory[256];
	struct frontend_file_list *list;
	struct frontend_file_list_node *node;
	frontend_find_handle find_handle;
	extern struct frontend_find_data find_file_data;

	_getcwd(current_directory, sizeof(current_directory));
	list = (struct frontend_file_list *)malloc(sizeof(*list));
	if (list == NULL) {
		_chdir(current_directory);
		return NULL;
	}
	find_handle = FindFirstFileA(wildcard, &find_file_data);
	if (find_handle == -1) {
		_chdir(current_directory);
		list->head = NULL;
		list->count = 0;
		return list;
	}
	node = (struct frontend_file_list_node *)malloc(sizeof(*node));
	if (node == NULL) {
		_chdir(current_directory);
		free(list);
		FindClose(find_handle);
		return NULL;
	}
	node->path = (char *)malloc(strlen(find_file_data.file_name) + 2);
	if (node->path == NULL) {
		_chdir(current_directory);
		free(node);
		free(list);
		FindClose(find_handle);
		return NULL;
	}
	strcpy(node->path, find_file_data.file_name);
	node->next = NULL;
	list->head = node;
	list->count = 1;
	while (FindNextFileA(find_handle, &find_file_data)) {
		node = (struct frontend_file_list_node *)malloc(sizeof(*node));
		if (node == NULL) {
			break;
		}
		node->path =
			(char *)malloc(strlen(find_file_data.file_name) + 2);
		if (node->path == NULL) {
			free(node);
			break;
		}
		strcpy(node->path, find_file_data.file_name);
		node->next = NULL;
		frontend_file_list_insert_node_sorted(list, node);
	}
	_chdir(current_directory);
	FindClose(find_handle);
	return list;
#endif
}

/* Frees every node's path, every node and the list itself; does nothing for
 * NULL. */
// FUNCTION: XVT 0x4DFC30
void frontend_file_list_free(struct frontend_file_list *list)
{
	struct frontend_file_list_node *node;
	struct frontend_file_list_node *next;

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
 * frontend_file_list_build_sorted for every file after the first; in the modern
 * build by frontend_file_list_collect_modern_file. */
// FUNCTION: XVT 0x4DFC70
void frontend_file_list_insert_node_sorted(struct frontend_file_list *list,
					   struct frontend_file_list_node *node)
{
	struct frontend_file_list_node *cursor;

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
