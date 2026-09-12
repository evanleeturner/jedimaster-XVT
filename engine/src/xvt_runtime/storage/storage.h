#ifndef XVT_RUNTIME_STORAGE_H
#define XVT_RUNTIME_STORAGE_H

#include "aeron/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XVT_PATH_CAPACITY 1024

/* All handles borrow the application's VFS; shutdown follows their consumers. */
void XvtStorage_Bind(AeronVfs* vfs);
AeronVfs* XvtStorage_Vfs(void);
int XvtStorage_Normalize(const char* path, char* output, size_t capacity);
int XvtStorage_Probe(AeronVfsRoot root, const char* path);
int XvtStorage_ResolveAsset(const char* path, char* resolved, size_t capacity);
AeronFile* XvtStorage_Open(const char* path, const char* mode);
AeronFile* XvtStorage_OpenRoot(AeronVfsRoot root, const char* path, const char* mode);
int XvtStorage_WriteAtomic(const char* path, const void* data, size_t size);
int XvtStorage_Remove(const char* path);
int XvtStorage_Rename(const char* old_path, const char* new_path);
int XvtStorage_Glob(AeronVfsRoot root, const char* wildcard, AeronVfsGlobCallback callback, void* context);
void XvtStorage_CaptureGlobalStream(void);
int XvtStorage_CloseGlobalStream(AeronFile* stream, int remove_on_error);
const char* XvtStorage_LastPath(void);
AeronVfsRoot XvtStorage_LastRoot(void);
/* Report through a native message box, then terminate without returning to game code. */
#if defined(_MSC_VER)
__declspec(noreturn)
#else
__attribute__((noreturn))
#endif
void XvtStorage_Fatal(const char* message, int exit_code);

#ifdef __cplusplus
}
#endif

#endif
