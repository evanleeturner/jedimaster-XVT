#ifndef XVT_RUNTIME_FILE_IO_H
#define XVT_RUNTIME_FILE_IO_H
#include "aeron/vfs.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
size_t XvtFile_Read(void* data, size_t size, size_t count, AeronFile* file);
size_t XvtFile_Write(const void* data, size_t size, size_t count, AeronFile* file);
int XvtFile_Getc(AeronFile* file);
int XvtFile_Putc(int value, AeronFile* file);
char* XvtFile_Gets(char* buffer, int capacity, AeronFile* file);
int XvtFile_Printf(AeronFile* file, const char* format, ...);
int XvtFile_Scanf(AeronFile* file, const char* format, ...);
int XvtFile_Seek(AeronFile* file, long offset, int origin);
long XvtFile_Tell(AeronFile* file);
int XvtFile_Close(AeronFile* file);
int XvtFile_Flush(AeronFile* file);
#ifdef __cplusplus
}
#endif

#endif
