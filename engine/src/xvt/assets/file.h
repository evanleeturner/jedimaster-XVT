#ifndef XVT_ASSETS_FILE_H
#define XVT_ASSETS_FILE_H

#include "xvt/xvt_typedefs.h"

#ifdef XVT_MODERN
#include "aeron/vfs.h"
#include "xvt_runtime/storage/file_io.h"
#include "xvt_runtime/storage/storage.h"
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef XVT_MODERN
typedef AeronFile XvtFile;
#define File_RawOpen XvtStorage_Open
#define File_RawClose XvtFile_Close
#define File_RawRead XvtFile_Read
#define File_RawWrite XvtFile_Write
#define File_RawSeek XvtFile_Seek
#define File_RawTell XvtFile_Tell
#define File_Gets XvtFile_Gets
#define File_Getc XvtFile_Getc
#define File_Putc XvtFile_Putc
#define File_Scanf XvtFile_Scanf
#define File_Printf XvtFile_Printf
#define File_Flush XvtFile_Flush
#define File_Eof AeronVfs_Eof
#define File_HasError AeronVfs_HasError
#define File_ClearError AeronVfs_ClearError
#define File_Rename XvtStorage_Rename
#define File_Remove XvtStorage_Remove
#else
typedef FILE XvtFile;
#define File_RawOpen fopen
#define File_RawClose fclose
#define File_RawRead fread
#define File_RawWrite fwrite
#define File_RawSeek fseek
#define File_RawTell ftell
#define File_Gets fgets
#define File_Getc fgetc
#define File_Putc fputc
#define File_Scanf fscanf
#define File_Printf fprintf
#define File_Flush fflush
#define File_Eof feof
#define File_HasError ferror
#define File_ClearError clearerr
#define File_Rename rename
#define File_Remove remove
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const char g_fileModeReadBinary[3];

/* Stored as int16_t in the binary (IDB enum FileErrorStringId). */
typedef int16_t FileErrorStringId;

enum {
	FILE_ERROR_STR_NOT_ENOUGH_MEMORY = 0x0,
	FILE_ERROR_STR_FILE_MISSING = 0x1,
	FILE_ERROR_STR_STRINGS_OUT_OF_SYNC = 0x2,
	FILE_ERROR_STR_PRESS_KEY_TO_EXIT = 0x3,
};

XvtFile* File_Open(const char* fileName, const char* mode);
int16_t File_Close(XvtFile* stream);
int File_Seek(XvtFile* stream, int offset, int16_t origin);
int File_Tell(XvtFile* stream);
int File_GetSize(XvtFile* stream);
int16_t File_ReadByte(XvtFile* stream, uint8_t* value);
int16_t File_ReadWord(XvtFile* stream, uint16_t* value);
int16_t File_ReadDword(XvtFile* stream, unsigned int* value);
int16_t File_ReadCount(XvtFile* stream, void* buffer, size_t count);
int16_t File_WriteByte(XvtFile* stream, char value);
int16_t File_WriteWord(XvtFile* stream, int value);
int16_t File_WriteDword(XvtFile* stream, int value);
int16_t File_WriteCount(XvtFile* stream, const void* buffer, size_t count);
int File_CheckRequiredCdMovieAssetsPresent(void);
int File_CheckGameCdPresent(int skipMovieChecks);
char File_GetCdDriveLetter(void);
char File_GetInstallDriveLetter(void);
const char* File_GetInstallPath(void);
int File_FindCdDriveLetter(const char* relativeCdFilePath);
void File_DetectGameAndCdPaths(const char* requiredCdFilePath);
const char* File_GetBaseGameInstallPath(void);
int File_ChangeToBaseGameInstallPath(void);
int File_ChangeToInstallPath(void);

#ifdef __cplusplus
}
#endif

#endif
