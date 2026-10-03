#include "xvt_runtime/storage/storage.h"

#include "aeron/log.h"
#include "xvt_runtime/log/log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AeronVfs *g_vfs;
static char g_last_path[XVT_PATH_CAPACITY];
static AeronVfsRoot g_last_root;
static char g_stream_path[XVT_PATH_CAPACITY];
static AeronVfsRoot g_stream_root;

void xvt_storage_bind(AeronVfs *vfs)
{
	g_vfs = vfs;
	g_last_path[0] = 0;
}

AeronVfs *xvt_storage_vfs(void) { return g_vfs; }

const char *xvt_storage_last_path(void) { return g_last_path; }

AeronVfsRoot xvt_storage_last_root(void) { return g_last_root; }

void xvt_storage_fatal(const char *message, int exit_code)
{
	char detail[XVT_PATH_CAPACITY + 1024];
	snprintf(detail, sizeof(detail), "%s%s%s",
		 message ? message : "File operation failed",
		 g_last_path[0] ? "\n\n" : "", g_last_path);
	XVT_LOG_ERROR("files.fatal message=\"%s\" path=\"%s\"",
		      message ? message : "File operation failed", g_last_path);
	Aeron_FatalError("OpenXvT", detail);
	exit(exit_code > 0 ? exit_code : EXIT_FAILURE);
}

int xvt_storage_normalize(const char *path, char *output, size_t capacity)
{
	size_t used = 0;
	if (!path || !path[0] || path[0] == '/' || path[0] == '\\' ||
	    strchr(path, ':')) {
		return 0;
	}
	while (*path) {
		const char *start = path;
		size_t length;
		while (*path && *path != '/' && *path != '\\') {
			++path;
		}
		length = (size_t)(path - start);
		if (length == 2 && start[0] == '.' && start[1] == '.') {
			return 0;
		}
		if (length && !(length == 1 && *start == '.')) {
			if (used + length + (used != 0) >= capacity) {
				return 0;
			}
			if (used) {
				output[used++] = '/';
			}
			memcpy(output + used, start, length);
			used += length;
		}
		if (*path) {
			++path;
		}
	}
	if (!used) {
		return 0;
	}
	output[used] = 0;
	return 1;
}

static int xvt_storage_found(void *context, const AeronVfsEntry *entry)
{
	(void)entry;
	*(int *)context = 1;
	return 1;
}

/* Stat alone cannot distinguish absence from failure. A successful parent
 * listing lets setup and USER-file callers distinguish the two. */
int xvt_storage_probe(AeronVfsRoot root, const char *path)
{
	AeronFileInfo info;
	char normalized[XVT_PATH_CAPACITY];
	char *slash;
	const char *name;
	const char *folder = "";
	int found = 0;
	if (!g_vfs ||
	    !xvt_storage_normalize(path, normalized, sizeof(normalized))) {
		return -1;
	}
	if (AeronVfs_Stat(g_vfs, root, normalized, &info) && info.exists) {
		return info.is_directory ? -1 : 1;
	}
	slash = strrchr(normalized, '/');
	name = path;
	if (slash) {
		*slash = 0;
		folder = normalized;
		name = slash + 1;
		if (!AeronVfs_Stat(g_vfs, root, folder, &info) ||
		    !info.exists) {
			/* Only a confirmed missing ancestor proves this path is absent. */
			int status = xvt_storage_probe(root, folder);
			return status == 0 ? 0 : -1;
		}
		if (!info.is_directory) {
			return -1;
		}
	} else {
		name = normalized;
	}
	if (!AeronVfs_Glob(g_vfs, root, folder, name,
			   AERON_VFS_GLOB_CASE_INSENSITIVE, xvt_storage_found,
			   &found)) {
		return -1;
	}
	return found ? -1 : 0;
}

static AeronFile *xvt_storage_open_asset(const char *path, const char *mode,
					 char *resolved, size_t capacity)
{
	char normalized[XVT_PATH_CAPACITY];
	AeronFile *file;
	if (capacity) {
		resolved[0] = 0;
	}
	if (!xvt_storage_normalize(path, normalized, sizeof(normalized))) {
		return NULL;
	}
	int length =
		snprintf(resolved, capacity, "BalanceOfPower/%s", normalized);
	if (length >= 0 && (size_t)length < capacity) {
		file = xvt_storage_open_root(AERON_VFS_ROOT_ASSET, resolved,
					     mode);
		if (file) {
			return file;
		}
	}
	if (strlen(normalized) >= capacity) {
		return NULL;
	}
	strcpy(resolved, normalized);
	return xvt_storage_open_root(AERON_VFS_ROOT_ASSET, resolved, mode);
}

int xvt_storage_resolve_asset(const char *path, char *resolved, size_t capacity)
{
	AeronFile *file =
		xvt_storage_open_asset(path, "rb", resolved, capacity);
	if (file) {
		AeronVfs_Close(file);
		return 1;
	}
	char expansion[XVT_PATH_CAPACITY];
	int length = snprintf(expansion, sizeof(expansion), "BalanceOfPower/%s",
			      path ? path : "");
	if (length < 0 || (size_t)length >= sizeof(expansion)) {
		return -1;
	}
	return xvt_storage_probe(AERON_VFS_ROOT_ASSET, expansion) == 0 &&
			       xvt_storage_probe(AERON_VFS_ROOT_ASSET, path) ==
				       0
		       ? 0
		       : -1;
}

static int xvt_storage_has_extension(const char *path, const char *extension)
{
	const char *dot = strrchr(path, '.');
	if (!dot) {
		return 0;
	}
	while (*dot && *extension &&
	       tolower((unsigned char)*dot) == *extension) {
		++dot;
		++extension;
	}
	return !*dot && !*extension;
}

static int xvt_storage_is_cache(const char *path)
{
	return xvt_storage_has_extension(path, ".pal") ||
	       xvt_storage_has_extension(path, ".act") ||
	       xvt_storage_has_extension(path, ".inv") ||
	       xvt_storage_has_extension(path, ".bin") ||
	       xvt_storage_has_extension(path, ".plo");
}

static AeronVfsRoot xvt_storage_writable_path(const char *path, char *output,
					      size_t capacity)
{
	int length = snprintf(output, capacity, "%s%s",
			      xvt_storage_is_cache(path) ? "cache/" : "", path);
	if (length < 0 || (size_t)length >= capacity) {
		output[0] = 0;
	}
	return (xvt_storage_has_extension(path, ".tmp") ||
		xvt_storage_has_extension(path, ".tmt"))
		       ? AERON_VFS_ROOT_TEMP
		       : AERON_VFS_ROOT_USER;
}

AeronFile *xvt_storage_open_root(AeronVfsRoot root, const char *path,
				 const char *mode)
{
	char parent[XVT_PATH_CAPACITY];
	char *slash;
	AeronFile *file = NULL;
	AeronVfsOpenMode open_mode;
	AeronFileInfo info;
	int writable;
	if (!g_vfs || !mode || !strchr("rwa", mode[0]) || !mode[0] ||
	    !xvt_storage_normalize(path, g_last_path, sizeof(g_last_path))) {
		return NULL;
	}
	g_last_root = root;
	writable = mode[0] != 'r' || strchr(mode, '+') != NULL;
	/* Windows fopen rejects directories; some host file APIs open them for reading. */
	if (!writable && AeronVfs_Stat(g_vfs, root, g_last_path, &info) &&
	    info.is_directory) {
		return NULL;
	}
	if (writable && root != AERON_VFS_ROOT_USER &&
	    root != AERON_VFS_ROOT_TEMP) {
		return NULL;
	}
	open_mode = mode[0] == 'w'   ? (strchr(mode, '+') ? AERON_VFS_WRITE_READ
							  : AERON_VFS_WRITE)
		    : mode[0] == 'a' ? AERON_VFS_APPEND
		    : strchr(mode, '+') ? AERON_VFS_READ_WRITE
					: AERON_VFS_READ;
	if (writable) {
		strcpy(parent, g_last_path);
		slash = strrchr(parent, '/');
		if (slash) {
			*slash = 0;
			if (!AeronVfs_CreateDirectory(g_vfs, root, parent)) {
				return NULL;
			}
		}
	}
	if (!AeronVfs_Open(g_vfs, root, g_last_path, open_mode, &file) &&
	    writable) {
		XVT_LOG_ERROR("files.write_failed path=\"%s\"", g_last_path);
	}
	return file;
}

AeronFile *xvt_storage_open(const char *path, const char *mode)
{
	char normalized[XVT_PATH_CAPACITY];
	char resolved[XVT_PATH_CAPACITY];
	AeronVfsRoot root;
	snprintf(g_last_path, sizeof(g_last_path), "%s", path ? path : "");
	g_last_root = AERON_VFS_ROOT_ASSET;
	if (!mode ||
	    !xvt_storage_normalize(path, normalized, sizeof(normalized))) {
		return NULL;
	}
	root = xvt_storage_writable_path(normalized, resolved,
					 sizeof(resolved));
	if (mode[0] != 'r' || strchr(mode, '+') ||
	    xvt_storage_has_extension(path, ".plt") ||
	    xvt_storage_has_extension(path, ".pl2") ||
	    root == AERON_VFS_ROOT_TEMP) {
		return xvt_storage_open_root(root, resolved, mode);
	}
	if (xvt_storage_is_cache(normalized)) {
		AeronFile *file = xvt_storage_open_root(root, resolved, mode);
		if (file) {
			return file;
		}
	}
	return xvt_storage_open_asset(normalized, mode, resolved,
				      sizeof(resolved));
}

int xvt_storage_remove(const char *path)
{
	char normalized[XVT_PATH_CAPACITY], resolved[XVT_PATH_CAPACITY];
	AeronVfsRoot root;
	if (!xvt_storage_normalize(path, normalized, sizeof(normalized))) {
		return -1;
	}
	root = xvt_storage_writable_path(normalized, resolved,
					 sizeof(resolved));
	return AeronVfs_Remove(g_vfs, root, resolved) ? 0 : -1;
}

int xvt_storage_rename(const char *old_path, const char *new_path)
{
	char old_name[XVT_PATH_CAPACITY], new_name[XVT_PATH_CAPACITY];
	if (!xvt_storage_normalize(old_path, old_name, sizeof(old_name)) ||
	    !xvt_storage_normalize(new_path, new_name, sizeof(new_name))) {
		return -1;
	}
	return AeronVfs_Rename(g_vfs, AERON_VFS_ROOT_USER, old_name, new_name)
		       ? 0
		       : -1;
}

int xvt_storage_glob(AeronVfsRoot root, const char *wildcard,
		     AeronVfsGlobCallback callback, void *context)
{
	char directory[XVT_PATH_CAPACITY];
	char *slash;
	if (!xvt_storage_normalize(wildcard, directory, sizeof(directory))) {
		return 0;
	}
	slash = strrchr(directory, '/');
	if (slash) {
		*slash = 0;
	}
	return AeronVfs_Glob(g_vfs, root, slash ? directory : "",
			     slash ? slash + 1 : directory,
			     AERON_VFS_GLOB_FILES |
				     AERON_VFS_GLOB_CASE_INSENSITIVE,
			     callback, context);
}

void xvt_storage_capture_global_stream(void)
{
	strcpy(g_stream_path, g_last_path);
	g_stream_root = g_last_root;
}

int xvt_storage_close_global_stream(AeronFile *stream, int remove_on_error)
{
	if (!stream) {
		return 0;
	}
	int failed = AeronVfs_HasError(stream);
	int close_failed = !AeronVfs_Close(stream);
	if ((failed || close_failed) && remove_on_error &&
	    (g_stream_root == AERON_VFS_ROOT_USER ||
	     g_stream_root == AERON_VFS_ROOT_TEMP)) {
		if (!AeronVfs_Remove(g_vfs, g_stream_root, g_stream_path)) {
			XVT_LOG_ERROR("files.remove_failed path=\"%s\"",
				      g_stream_path);
		}
	}
	return failed || close_failed;
}

int xvt_storage_write_atomic(const char *path, const void *data, size_t size)
{
	char normalized[XVT_PATH_CAPACITY], resolved[XVT_PATH_CAPACITY];
	if (!xvt_storage_normalize(path, normalized, sizeof(normalized))) {
		return 0;
	}
	AeronVfsRoot root = xvt_storage_writable_path(normalized, resolved,
						      sizeof(resolved));
	int result = AeronVfs_WriteAllAtomic(g_vfs, root, resolved, data, size);
	if (!result) {
		XVT_LOG_ERROR("files.save_failed path=\"%s\"", resolved);
	}
	return result;
}
