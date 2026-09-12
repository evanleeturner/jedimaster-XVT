#include "xvt_runtime/storage/storage.h"

#include "aeron/log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AeronVfs* g_vfs;
static char g_lastPath[XVT_PATH_CAPACITY];
static AeronVfsRoot g_lastRoot;
static char g_streamPath[XVT_PATH_CAPACITY];
static AeronVfsRoot g_streamRoot;

void XvtStorage_Bind(AeronVfs* vfs) {
	g_vfs = vfs;
	g_lastPath[0] = 0;
}

AeronVfs* XvtStorage_Vfs(void) { return g_vfs; }

const char* XvtStorage_LastPath(void) { return g_lastPath; }

AeronVfsRoot XvtStorage_LastRoot(void) { return g_lastRoot; }

void XvtStorage_Fatal(const char* message, int exit_code) {
	char detail[XVT_PATH_CAPACITY + 1024];
	snprintf(detail, sizeof(detail), "%s%s%s", message ? message : "File operation failed",
			 g_lastPath[0] ? "\n\n" : "", g_lastPath);
	Aeron_LogError("xvt.files", "%s", detail);
	Aeron_FatalError("OpenXvT", detail);
	exit(exit_code > 0 ? exit_code : EXIT_FAILURE);
}

int XvtStorage_Normalize(const char* path, char* output, size_t capacity) {
	size_t used = 0;
	if (!path || !path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, ':'))
		return 0;
	while (*path) {
		const char* start = path;
		size_t length;
		while (*path && *path != '/' && *path != '\\')
			++path;
		length = (size_t)(path - start);
		if (length == 2 && start[0] == '.' && start[1] == '.')
			return 0;
		if (length && !(length == 1 && *start == '.')) {
			if (used + length + (used != 0) >= capacity)
				return 0;
			if (used)
				output[used++] = '/';
			memcpy(output + used, start, length);
			used += length;
		}
		if (*path)
			++path;
	}
	if (!used)
		return 0;
	output[used] = 0;
	return 1;
}

static int XvtStorage_Found(void* context, const AeronVfsEntry* entry) {
	(void)entry;
	*(int*)context = 1;
	return 1;
}

/* Stat alone cannot distinguish absence from failure. A successful parent
 * listing lets setup and USER-file callers distinguish the two. */
int XvtStorage_Probe(AeronVfsRoot root, const char* path) {
	AeronFileInfo info;
	char parent[XVT_PATH_CAPACITY];
	char* slash;
	const char* name;
	int found = 0;
	if (!g_vfs || !XvtStorage_Normalize(path, parent, sizeof(parent)))
		return -1;
	if (AeronVfs_Stat(g_vfs, root, parent, &info) && info.exists)
		return info.is_directory ? -1 : 1;
	slash = strrchr(parent, '/');
	name = path;
	if (slash) {
		*slash = 0;
		name = slash + 1;
		if (!AeronVfs_Stat(g_vfs, root, parent, &info) || !info.exists) {
			/* Only a confirmed missing ancestor proves this path is absent. */
			int status = XvtStorage_Probe(root, parent);
			return status == 0 ? 0 : -1;
		}
		if (!info.is_directory)
			return -1;
	} else {
		name = parent;
	}
	if (!AeronVfs_Glob(g_vfs, root, slash ? parent : "", name, AERON_VFS_GLOB_CASE_INSENSITIVE,
					   XvtStorage_Found, &found))
		return -1;
	return found ? -1 : 0;
}

static AeronFile* XvtStorage_OpenAsset(const char* path, const char* mode, char* resolved, size_t capacity) {
	char normalized[XVT_PATH_CAPACITY];
	AeronFile* file;
	if (capacity)
		resolved[0] = 0;
	if (!XvtStorage_Normalize(path, normalized, sizeof(normalized)))
		return NULL;
	int length = snprintf(resolved, capacity, "BalanceOfPower/%s", normalized);
	if (length >= 0 && (size_t)length < capacity) {
		file = XvtStorage_OpenRoot(AERON_VFS_ROOT_ASSET, resolved, mode);
		if (file)
			return file;
	}
	if (strlen(normalized) >= capacity)
		return NULL;
	strcpy(resolved, normalized);
	return XvtStorage_OpenRoot(AERON_VFS_ROOT_ASSET, resolved, mode);
}

int XvtStorage_ResolveAsset(const char* path, char* resolved, size_t capacity) {
	AeronFile* file = XvtStorage_OpenAsset(path, "rb", resolved, capacity);
	if (file) {
		AeronVfs_Close(file);
		return 1;
	}
	char expansion[XVT_PATH_CAPACITY];
	int length = snprintf(expansion, sizeof(expansion), "BalanceOfPower/%s", path ? path : "");
	if (length < 0 || (size_t)length >= sizeof(expansion))
		return -1;
	return XvtStorage_Probe(AERON_VFS_ROOT_ASSET, expansion) == 0 &&
				   XvtStorage_Probe(AERON_VFS_ROOT_ASSET, path) == 0
			   ? 0
			   : -1;
}

static int XvtStorage_Extension(const char* path, const char* extension) {
	const char* dot = strrchr(path, '.');
	if (!dot)
		return 0;
	while (*dot && *extension && tolower((unsigned char)*dot) == *extension) {
		++dot;
		++extension;
	}
	return !*dot && !*extension;
}

static int XvtStorage_IsCache(const char* path) {
	return XvtStorage_Extension(path, ".pal") || XvtStorage_Extension(path, ".act") ||
		   XvtStorage_Extension(path, ".inv") || XvtStorage_Extension(path, ".bin") ||
		   XvtStorage_Extension(path, ".plo");
}

static AeronVfsRoot XvtStorage_WritablePath(const char* path, char* output, size_t capacity) {
	int length = snprintf(output, capacity, "%s%s", XvtStorage_IsCache(path) ? "cache/" : "", path);
	if (length < 0 || (size_t)length >= capacity)
		output[0] = 0;
	return (XvtStorage_Extension(path, ".tmp") || XvtStorage_Extension(path, ".tmt")) ? AERON_VFS_ROOT_TEMP
																					  : AERON_VFS_ROOT_USER;
}

AeronFile* XvtStorage_OpenRoot(AeronVfsRoot root, const char* path, const char* mode) {
	char parent[XVT_PATH_CAPACITY];
	char* slash;
	AeronFile* file = NULL;
	AeronVfsOpenMode open_mode;
	AeronFileInfo info;
	int writable;
	if (!g_vfs || !mode || !strchr("rwa", mode[0]) || !mode[0] ||
		!XvtStorage_Normalize(path, g_lastPath, sizeof(g_lastPath)))
		return NULL;
	g_lastRoot = root;
	writable = mode[0] != 'r' || strchr(mode, '+') != NULL;
	/* Windows fopen rejects directories; some host file APIs open them for reading. */
	if (!writable && AeronVfs_Stat(g_vfs, root, g_lastPath, &info) && info.is_directory)
		return NULL;
	if (writable && root != AERON_VFS_ROOT_USER && root != AERON_VFS_ROOT_TEMP)
		return NULL;
	open_mode = mode[0] == 'w'      ? (strchr(mode, '+') ? AERON_VFS_WRITE_READ : AERON_VFS_WRITE)
				: mode[0] == 'a'    ? AERON_VFS_APPEND
				: strchr(mode, '+') ? AERON_VFS_READ_WRITE
									: AERON_VFS_READ;
	if (writable) {
		strcpy(parent, g_lastPath);
		slash = strrchr(parent, '/');
		if (slash) {
			*slash = 0;
			if (!AeronVfs_CreateDirectory(g_vfs, root, parent))
				return NULL;
		}
	}
	if (!AeronVfs_Open(g_vfs, root, g_lastPath, open_mode, &file) && writable)
		Aeron_LogError("xvt.files", "Cannot write %s", g_lastPath);
	return file;
}

AeronFile* XvtStorage_Open(const char* path, const char* mode) {
	char normalized[XVT_PATH_CAPACITY];
	char resolved[XVT_PATH_CAPACITY];
	AeronVfsRoot root;
	snprintf(g_lastPath, sizeof(g_lastPath), "%s", path ? path : "");
	g_lastRoot = AERON_VFS_ROOT_ASSET;
	if (!mode || !XvtStorage_Normalize(path, normalized, sizeof(normalized)))
		return NULL;
	root = XvtStorage_WritablePath(normalized, resolved, sizeof(resolved));
	if (mode[0] != 'r' || strchr(mode, '+') || XvtStorage_Extension(path, ".plt") ||
		XvtStorage_Extension(path, ".pl2") || root == AERON_VFS_ROOT_TEMP)
		return XvtStorage_OpenRoot(root, resolved, mode);
	if (XvtStorage_IsCache(normalized)) {
		AeronFile* file = XvtStorage_OpenRoot(root, resolved, mode);
		if (file)
			return file;
	}
	return XvtStorage_OpenAsset(normalized, mode, resolved, sizeof(resolved));
}

int XvtStorage_Remove(const char* path) {
	char normalized[XVT_PATH_CAPACITY], resolved[XVT_PATH_CAPACITY];
	AeronVfsRoot root;
	if (!XvtStorage_Normalize(path, normalized, sizeof(normalized)))
		return -1;
	root = XvtStorage_WritablePath(normalized, resolved, sizeof(resolved));
	return AeronVfs_Remove(g_vfs, root, resolved) ? 0 : -1;
}

int XvtStorage_Rename(const char* old_path, const char* new_path) {
	char old_name[XVT_PATH_CAPACITY], new_name[XVT_PATH_CAPACITY];
	if (!XvtStorage_Normalize(old_path, old_name, sizeof(old_name)) ||
		!XvtStorage_Normalize(new_path, new_name, sizeof(new_name)))
		return -1;
	return AeronVfs_Rename(g_vfs, AERON_VFS_ROOT_USER, old_name, new_name) ? 0 : -1;
}

int XvtStorage_Glob(AeronVfsRoot root, const char* wildcard, AeronVfsGlobCallback callback, void* context) {
	char directory[XVT_PATH_CAPACITY];
	char* slash;
	if (!XvtStorage_Normalize(wildcard, directory, sizeof(directory)))
		return 0;
	slash = strrchr(directory, '/');
	if (slash)
		*slash = 0;
	return AeronVfs_Glob(g_vfs, root, slash ? directory : "", slash ? slash + 1 : directory,
						 AERON_VFS_GLOB_FILES | AERON_VFS_GLOB_CASE_INSENSITIVE, callback, context);
}

void XvtStorage_CaptureGlobalStream(void) {
	strcpy(g_streamPath, g_lastPath);
	g_streamRoot = g_lastRoot;
}

int XvtStorage_CloseGlobalStream(AeronFile* stream, int remove_on_error) {
	if (!stream)
		return 0;
	int failed = AeronVfs_HasError(stream);
	int close_failed = !AeronVfs_Close(stream);
	if ((failed || close_failed) && remove_on_error &&
		(g_streamRoot == AERON_VFS_ROOT_USER || g_streamRoot == AERON_VFS_ROOT_TEMP)) {
		if (!AeronVfs_Remove(g_vfs, g_streamRoot, g_streamPath))
			Aeron_LogError("xvt.files", "Cannot remove failed output: %s", g_streamPath);
	}
	return failed || close_failed;
}

int XvtStorage_WriteAtomic(const char* path, const void* data, size_t size) {
	char normalized[XVT_PATH_CAPACITY], resolved[XVT_PATH_CAPACITY];
	if (!XvtStorage_Normalize(path, normalized, sizeof(normalized)))
		return 0;
	AeronVfsRoot root = XvtStorage_WritablePath(normalized, resolved, sizeof(resolved));
	int result = AeronVfs_WriteAllAtomic(g_vfs, root, resolved, data, size);
	if (!result)
		Aeron_LogError("xvt.files", "Cannot save %s", resolved);
	return result;
}
