#ifndef XVT_RUNTIME_STORAGE_H
#define XVT_RUNTIME_STORAGE_H

#include "aeron/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the game's files live. Paths are relative, with either slash;
 * Normalize cleans each one before use. The VFS roots used here: ASSET, the
 * game's data, read only; USER, the player's writable files; TEMP, scratch
 * files. xvt_storage_open picks the place by extension, letter case ignored,
 * first match:
 *   .tmp, .tmt                    TEMP
 *   .pal, .act, .inv, .bin, .plo  USER under cache/; a read that cannot open
 *                                 there falls back to ASSET
 *   .plt, .pl2, or any write      USER
 *   any other read                ASSET, BalanceOfPower/<path> first, then <path>
 * Every open is binary, with no newline translation. Not thread-safe: the last
 * path is shared state. */

#define XVT_PATH_CAPACITY 1024

/* All handles borrow the application's VFS; shutdown follows their consumers. */
/* Sets the VFS every call here uses and clears the last path; NULL unbinds.
 * Probe and the Open calls fail while none is bound. */
void xvt_storage_bind(AeronVfs *vfs);
/* The bound VFS, or NULL. */
AeronVfs *xvt_storage_vfs(void);
/* Cleans a relative path: either slash separates, empty and "." parts are
 * dropped, and the result joins the rest with '/'. Returns 1, or 0 for NULL, a
 * leading slash, any ':', any ".." part, an empty result, or a result that does
 * not fit in capacity with its terminator; on 0, output may hold an
 * unterminated partial copy. */
int xvt_storage_normalize(const char *path, char *output, size_t capacity);
/* 1 when path names a file in root. 0 only when its absence is proven: the
 * parent folder exists and lists no entry of that name in any letter case, or a
 * missing ancestor is proven the same way. -1 otherwise: a folder, a rejected
 * path, no bound VFS, a parent that is a file, a failed listing, or a listed
 * entry that the exact lookup missed. */
int xvt_storage_probe(AeronVfsRoot root, const char *path);
/* Looks up an asset the way Open looks up a read of an ordinary file:
 * BalanceOfPower/<path>, then <path>, in ASSET. Returns 1, with the path that
 * opened in resolved; 0 when Probe proves both absent; -1 otherwise. resolved
 * is meaningful only on 1. */
int xvt_storage_resolve_asset(const char *path, char *resolved,
			      size_t capacity);
/* The game's fopen: opens path in the place the note at the top of this file
 * gives. Modes as fopen, except that "a+" opens for appending only. Returns
 * NULL for a NULL mode, a mode that does not start with r, w or a, a rejected
 * path, or a failed open; a read that falls back returns the first place that
 * opens. */
AeronFile *xvt_storage_open(const char *path, const char *mode);
/* Opens path in one root. A mode that writes ("w", "a", or any "+") is refused
 * outside USER and TEMP, first creates missing parent folders, and logs an
 * error when the open fails. A read refuses a folder. Returns NULL on refusal
 * or failure, for a mode that does not start with r, w or a, a rejected path,
 * or no bound VFS. Modes as in Open. */
AeronFile *xvt_storage_open_root(AeronVfsRoot root, const char *path,
				 const char *mode);
/* Replaces the file at path, placed as Open places a write, with size bytes: it
 * writes and flushes <path>.tmp beside it, then renames that over the file.
 * Returns 1, or 0 on failure; a failure after the path was accepted is
 * logged. */
int xvt_storage_write_atomic(const char *path, const void *data, size_t size);
/* Removes the file (or empty folder) at path, placed as Open places a write; a
 * cache file is removed from USER's cache/ folder, never from ASSET. Returns 0,
 * or -1. */
int xvt_storage_remove(const char *path);
/* Renames within USER only, without the cache/ folder or the TEMP placing that
 * Open and Remove apply: a .tmp file is looked for in USER, not in TEMP where
 * Open writes it. Returns 0, or -1. */
int xvt_storage_rename(const char *old_path, const char *new_path);
/* Calls callback for each file, never a folder, in the folder of root that
 * wildcard names, whose name matches the last part of wildcard in any letter
 * case. Returns 0 for a rejected wildcard, a failed listing, or a callback that
 * returned 0 to stop; otherwise nonzero. An entry's name is valid only during
 * its callback. */
int xvt_storage_glob(AeronVfsRoot root, const char *wildcard,
		     AeronVfsGlobCallback callback, void *context);
/* Remembers the last path and root as the file CloseGlobalStream may remove. Call it straight after
 * opening that stream. */
void xvt_storage_capture_global_stream(void);
/* Closes stream. Returns 1 when the stream's error flag was set or the close
 * failed, else 0; 0 also for NULL. On such a failure with remove_on_error,
 * removes the remembered file when it lies in USER or TEMP, and logs when that
 * removal fails. */
int xvt_storage_close_global_stream(AeronFile *stream, int remove_on_error);
/* The path of the latest open, for error messages: Open sets it to path as
 * given, then to each place it tries; OpenRoot and ResolveAsset set it to the
 * last place they try. Empty after Bind. When OpenRoot rejects a path, it can
 * hold the start of the rejected path over the rest of the old one. */
const char *xvt_storage_last_path(void);
/* The root that goes with the last path. */
AeronVfsRoot xvt_storage_last_root(void);
/* Report through a native message box, then terminate without returning to game code. */
/* Logs message (a default when NULL) and, below it, the last path, as an error
 * under xvt.files; shows the same text in a message box titled OpenXvT; then
 * exits with exit_code, or EXIT_FAILURE when that is not positive. */
#if defined(_MSC_VER)
__declspec(noreturn)
#else
__attribute__((noreturn))
#endif
void xvt_storage_fatal(const char *message, int exit_code);

#ifdef __cplusplus
}
#endif

#endif
