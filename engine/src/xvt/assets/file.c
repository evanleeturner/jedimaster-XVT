#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend_state.h"
#ifndef XVT_MODERN
#include <direct.h>
#else
#include <limits.h>
#endif
#include <ctype.h>
#include <string.h>

#ifndef XVT_MODERN
__declspec(dllimport) int __stdcall
RegCreateKeyA(uintptr_t key, const char *subKey, void **result);
__declspec(dllimport) int __stdcall
RegOpenKeyExA(uintptr_t key, const char *subKey, unsigned int options,
	      unsigned int access, void **result);
__declspec(dllimport) int __stdcall
RegQueryValueExA(void *key, const char *valueName, unsigned int *reserved,
		 unsigned int *type, void *data, unsigned int *dataSize);
__declspec(dllimport) int __stdcall RegCloseKey(void *key);
#endif

#ifndef XVT_MODERN
/* Raised by each File_Open that succeeds and lowered by each File_Close of a
 * non-NULL stream; nothing reads it. The original build only. */
// GLOBAL: XVT 0x52B6B8
static int16_t g_openFileCount;
#endif
/* The mode string "rb" that many of the game's file opens pass; never
 * written. */
// GLOBAL: XVT 0x51A854
const char g_fileModeReadBinary[3] = "rb";

/* Opens fileName with mode and returns the stream, or NULL when every try
 * fails. The modern build returns XvtStorage_Open's result. The original build
 * tries the current folder, then the base game's install folder (changing to it
 * and back to g_frontState.installPath). Then, when mode starts with neither
 * 'w' nor 'a' and g_frontState.cdDriveLetter is nonzero, it suspends CD audio,
 * tries "<letter>:\BalanceOfPower\<fileName>" and then "<letter>\<fileName>",
 * with no colon, and resumes the audio; with the '.' File_FindCdDriveLetter
 * gives, the last is ".\<fileName>". Each success adds 1 to g_openFileCount. */
// FUNCTION: XVT 0x4CC4A0
XvtFile *File_Open(const char *fileName, const char *mode)
{
#ifdef XVT_MODERN
	return XvtStorage_Open(fileName, mode);
#else

	XvtFile *stream;
	char path[256];

	stream = File_RawOpen(fileName, mode);
	if (stream != NULL) {
		++g_openFileCount;
	} else {
		File_ChangeToBaseGameInstallPath();
		stream = File_RawOpen(fileName, mode);
		File_ChangeToInstallPath();
		if (stream != NULL) {
			++g_openFileCount;
		} else if (*mode != 'w' && *mode != 'a' &&
			   g_frontState.cdDriveLetter != 0) {
			CDAudio_SuspendPlayback();
			sprintf(path, "%c:\\BalanceOfPower\\%s",
				(uint8_t)g_frontState.cdDriveLetter, fileName);
			stream = File_RawOpen(path, mode);
			if (stream == NULL) {
				sprintf(path, "%c\\%s",
					(uint8_t)g_frontState.cdDriveLetter,
					fileName);
				stream = File_RawOpen(path, mode);
			}
			if (stream != NULL) {
				++g_openFileCount;
			}
			CDAudio_RequestResumePlayback();
		}
	}
	return stream;

#endif
}

/* Closes stream. Returns 0 on success and EOF on failure. The modern build also
 * returns 0 for NULL; the original build lowers g_openFileCount for a non-NULL
 * stream and, for NULL, returns its uninitialized local result. */
// FUNCTION: XVT 0x4CC590
int16_t File_Close(XvtFile *stream)
{
#ifdef XVT_MODERN
	return (int16_t)XvtFile_Close(stream);
#else

	int16_t result;

	if (stream != 0) {
		--g_openFileCount;
		result = (int16_t)File_RawClose(stream);
	}
	return result;

#endif
}

/* Moves stream's position to offset from origin (SEEK_SET, SEEK_CUR or
 * SEEK_END); returns 0 on success, nonzero on failure. */
// FUNCTION: XVT 0x4CC5C0
int File_Seek(XvtFile *stream, int offset, int16_t origin)
{
	return File_RawSeek(stream, offset, origin);
}

/* Returns stream's position in bytes, or -1 on failure; the modern build also
 * returns -1 when the position is negative or over INT_MAX. */
// FUNCTION: XVT 0x4CC5E0
int File_Tell(XvtFile *stream)
{
#ifdef XVT_MODERN
	int64_t value = AeronVfs_Tell(stream);
	return value < 0 || value > INT_MAX ? -1 : (int)value;
#else
	return (int)File_RawTell(stream);
#endif
}

/* Returns stream's size in bytes. The original build seeks to the end, takes
 * the position and seeks back to where it was, without checking either seek;
 * the modern build asks AeronVfs_GetSize and returns -1 when that is negative
 * or over INT_MAX. */
// FUNCTION: XVT 0x4CC5F0
int File_GetSize(XvtFile *stream)
{
#ifdef XVT_MODERN
	int64_t value = AeronVfs_GetSize(stream);
	return value < 0 || value > INT_MAX ? -1 : (int)value;
#else

	int size;
	int originalPosition;

	originalPosition = File_Tell(stream);
	File_Seek(stream, 0, SEEK_END);
	size = File_Tell(stream);
	File_Seek(stream, originalPosition, SEEK_SET);
	return size;

#endif
}

/* Reads one byte into *value; returns 1 when it was read, else 0. */
// FUNCTION: XVT 0x4CC630
int16_t File_ReadByte(XvtFile *stream, uint8_t *value)
{
	return !((int16_t)(File_RawRead(value, 1, 1, stream) != 1));
}

/* Reads two bytes into *value as they lie in the file; returns 1 when both were
 * read, else 0. */
// FUNCTION: XVT 0x4CC660
int16_t File_ReadWord(XvtFile *stream, uint16_t *value)
{
	return !((int16_t)(File_RawRead(value, 1, 2, stream) != 2));
}

/* Reads four bytes into *value as they lie in the file; returns 1 when all four
 * were read, else 0. Nothing calls this. */
// FUNCTION: XVT 0x4CC690
int16_t File_ReadDword(XvtFile *stream, unsigned int *value)
{
	return !((int16_t)(File_RawRead(value, 1, 4, stream) != 4));
}

/* Reads count bytes into buffer; returns 1 when all were read, else 0. */
// FUNCTION: XVT 0x4CC6C0
int16_t File_ReadBytes(XvtFile *stream, void *buffer, size_t count)
{
	return !((int16_t)(File_RawRead(buffer, 1, count, stream) != count));
}

/* Writes value as one byte; returns 1 when it was written, else 0. */
// FUNCTION: XVT 0x4CC700
int16_t File_WriteByte(XvtFile *stream, char value)
{
	return !((int16_t)(File_RawWrite(&value, 1, 1, stream) != 1));
}

/* Writes the first 2 bytes of value as stored in memory: on a little-endian
 * machine its low 16 bits, low byte first. Returns 1 when both were written,
 * else 0. Nothing calls this. */
// FUNCTION: XVT 0x4CC730
int16_t File_WriteWord(XvtFile *stream, int value)
{
	return !((int16_t)(File_RawWrite(&value, 1, 2, stream) != 2));
}

/* Writes value's 4 bytes as stored in memory; returns 1 when all four were
 * written, else 0. Nothing calls this. */
// FUNCTION: XVT 0x4CC760
int16_t File_WriteDword(XvtFile *stream, int value)
{
	return !((int16_t)(File_RawWrite(&value, 1, 4, stream) != 4));
}

/* Writes count bytes from buffer; returns 1 when all were written, else 0. */
// FUNCTION: XVT 0x4CC790
int16_t File_WriteBytes(XvtFile *stream, const void *buffer, size_t count)
{
	return !((int16_t)(File_RawWrite(buffer, 1, count, stream) != count));
}

/* Returns 1 when wave\PBC\Pb1los07.wav and movies\imp1snd.smk can both be
 * opened, else 0. The original build looks for them at the root of
 * g_frontState.cdDriveLetter's drive, with CD audio suspended meanwhile, and
 * returns 0 when that letter is 0; the modern build asks
 * XvtStorage_ResolveAsset. Only the original build calls this. */
// FUNCTION: XVT 0x4CC930
int File_CheckRequiredCdMovieAssetsPresent(void)
{
#ifdef XVT_MODERN
	char path[XVT_PATH_CAPACITY];
	return XvtStorage_ResolveAsset("wave/PBC/Pb1los07.wav", path,
				       sizeof(path)) == 1 &&
	       XvtStorage_ResolveAsset("movies/imp1snd.smk", path,
				       sizeof(path)) == 1;
#else

	char fileName[80];
	XvtFile *stream;

	if (g_frontState.cdDriveLetter == 0) {
		return 0;
	}
	CDAudio_SuspendPlayback();
	strcpy(fileName, "c:\\wave\\PBC\\Pb1los07.wav");
	fileName[0] = g_frontState.cdDriveLetter;
	stream = File_RawOpen(fileName, "rb");
	if (stream == 0) {
		CDAudio_RequestResumePlayback();
		return 0;
	}
	File_RawClose(stream);
	strcpy(fileName, "b:\\movies\\imp1snd.smk");
	fileName[0] = g_frontState.cdDriveLetter;
	stream = File_RawOpen(fileName, "rb");
	if (stream == 0) {
		CDAudio_RequestResumePlayback();
		return 0;
	}
	CDAudio_RequestResumePlayback();
	File_RawClose(stream);
	return 1;

#endif
}

/* Returns 1 when wave\PBC\Pb1los07.wav and ivfiles\cal.opt can both be opened,
 * else 0. The original build looks for them at the root of
 * g_frontState.cdDriveLetter's drive, returns 0 when that letter is 0, and
 * unless skipMovieChecks is nonzero also needs amovie\a.wrk and bmovie\a.wrk
 * there. The modern build ignores skipMovieChecks and asks
 * XvtStorage_ResolveAsset. */
// FUNCTION: XVT 0x4CC9F0
int File_CheckGameCdPresent(int skipMovieChecks)
{
#ifdef XVT_MODERN
	char path[XVT_PATH_CAPACITY];
	(void)skipMovieChecks;
	return XvtStorage_ResolveAsset("wave/PBC/Pb1los07.wav", path,
				       sizeof(path)) == 1 &&
	       XvtStorage_ResolveAsset("ivfiles/cal.opt", path, sizeof(path)) ==
		       1;
#else

	XvtFile *stream;
	char fileName[80];

	if (g_frontState.cdDriveLetter == 0) {
		return 0;
	}
	if (!skipMovieChecks) {
		strcpy(fileName, "c:\\");
		fileName[0] = g_frontState.cdDriveLetter;
		strcat(fileName, "\\amovie\\a.wrk");
		stream = File_RawOpen(fileName, "rb");
		if (stream == NULL) {
			return 0;
		}
		File_RawClose(stream);

		strcpy(fileName, "b:\\bmovie\\a.wrk");
		fileName[0] = g_frontState.cdDriveLetter;
		stream = File_RawOpen(fileName, "rb");
		if (stream == NULL) {
			return 0;
		}
		File_RawClose(stream);
	}

	strcpy(fileName, "c:\\wave\\PBC\\Pb1los07.wav");
	fileName[0] = g_frontState.cdDriveLetter;
	stream = File_RawOpen(fileName, "rb");
	if (stream == NULL) {
		return 0;
	}
	File_RawClose(stream);

	strcpy(fileName, "c:\\ivfiles\\cal.opt");
	fileName[0] = g_frontState.cdDriveLetter;
	stream = File_RawOpen(fileName, "rb");
	if (stream == NULL) {
		return 0;
	}
	File_RawClose(stream);

	return 1;

#endif
}

/* Returns g_frontState.cdDriveLetter. Only the original build calls this. */
// FUNCTION: XVT 0x4CCB90
char File_GetCdDriveLetter(void) { return g_frontState.cdDriveLetter; }

/* Returns g_frontState.installDriveLetter. Nothing calls this. */
// FUNCTION: XVT 0x4CCBA0
char File_GetInstallDriveLetter(void)
{
	return g_frontState.installDriveLetter;
}

/* Returns g_frontState.installPath. Nothing calls this. */
// FUNCTION: XVT 0x4CCBB0
const char *File_GetInstallPath(void) { return g_frontState.installPath; }

/* The original's CD drive search is not reconstructed: this ignores relativeCdFilePath and always
 * returns '.', which File_DetectGameAndCdPaths, outside XVT_MODERN, stores as the CD drive letter. */
/* Only the original build calls this. */
// FUNCTION: XVT 0x4CCBC0
int File_FindCdDriveLetter(const char *relativeCdFilePath)
{
	(void)relativeCdFilePath;

	return '.';
}

/* Fills g_frontState's cdDriveLetter, installDriveLetter, installPath and
 * baseGameInstallPath. The modern build sets the letters to 0, installPath to
 * "BalanceOfPower" and baseGameInstallPath to "". The original build sets
 * cdDriveLetter from File_FindCdDriveLetter when requiredCdFilePath is not
 * NULL, and creates the registry key of version 2.0 under HKEY_LOCAL_MACHINE
 * (SOFTWARE\LucasArts Entertainment Company\X-Wing vs. TIE Fighter) when
 * missing. When that key's "Install Path" value cannot be read, it takes the
 * current folder as installPath, the current drive's letter, lowercase, as
 * installDriveLetter and the folder above as baseGameInstallPath, and stops.
 * Otherwise it drops a final backslash from installPath, takes its first
 * letter, lowercase, as installDriveLetter, and reads baseGameInstallPath from
 * the 1.0 key's "Install Path", falling back to the folder above installPath.
 * Either way it leaves installPath the current folder. */
// FUNCTION: XVT 0x4CCCC0
void File_DetectGameAndCdPaths(const char *requiredCdFilePath)
{
#ifdef XVT_MODERN
	(void)requiredCdFilePath;
	g_frontState.cdDriveLetter = 0;
	g_frontState.installDriveLetter = 0;
	strcpy(g_frontState.installPath, "BalanceOfPower");
	g_frontState.baseGameInstallPath[0] = 0;
#else

	void *registryKey;
	unsigned int dataSize;
	char versionSubKey[256];
	uint8_t installPathFirstCharacter;

	if (requiredCdFilePath != NULL) {
		g_frontState.cdDriveLetter =
			File_FindCdDriveLetter(requiredCdFilePath);
	}

	sprintf(versionSubKey,
		"SOFTWARE\\LucasArts Entertainment Company\\X-Wing vs. TIE Fighter\\%d.%d",
		2, 0);
	RegCreateKeyA(0x80000002u, versionSubKey, &registryKey);
	RegCloseKey(registryKey);
	if (RegOpenKeyExA(
		    0x80000002u,
		    "SOFTWARE\\LucasArts Entertainment Company\\X-Wing vs. TIE Fighter\\2.0",
		    0, 0x20019u, &registryKey) != 0) {
		g_frontState.installDriveLetter = _getdrive() + 'a' - 1;
		_getcwd(g_frontState.installPath,
			sizeof(g_frontState.installPath));
		_chdir("..");
		_getcwd(g_frontState.baseGameInstallPath,
			sizeof(g_frontState.baseGameInstallPath));
		_chdir(g_frontState.installPath);
		return;
	}

	dataSize = sizeof(g_frontState.installPath);
	if (RegQueryValueExA(registryKey, "Install Path", NULL, NULL,
			     g_frontState.installPath, &dataSize) != 0) {
		g_frontState.installDriveLetter = _getdrive() + 'a' - 1;
		_getcwd(g_frontState.installPath,
			sizeof(g_frontState.installPath));
		_chdir("..");
		_getcwd(g_frontState.baseGameInstallPath,
			sizeof(g_frontState.baseGameInstallPath));
		_chdir(g_frontState.installPath);
		RegCloseKey(registryKey);
		return;
	}

	installPathFirstCharacter = g_frontState.installPath[0];
	if (g_frontState.installPath[strlen(g_frontState.installPath) - 1] ==
	    '\\') {
		g_frontState.installPath[strlen(g_frontState.installPath) - 1] =
			'\0';
	}
	g_frontState.installDriveLetter =
		(char)tolower(installPathFirstCharacter);
	RegCloseKey(registryKey);
	_chdir(g_frontState.installPath);
	if (RegOpenKeyExA(
		    0x80000002u,
		    "SOFTWARE\\LucasArts Entertainment Company\\X-Wing vs. TIE Fighter\\1.0",
		    0, 0x20019u, &registryKey) != 0) {
		_chdir("..");
		_getcwd(g_frontState.baseGameInstallPath,
			sizeof(g_frontState.baseGameInstallPath));
		_chdir(g_frontState.installPath);
		return;
	}

	dataSize = sizeof(g_frontState.baseGameInstallPath);
	if (RegQueryValueExA(registryKey, "Install Path", NULL, NULL,
			     g_frontState.baseGameInstallPath,
			     &dataSize) != 0) {
		_chdir("..");
		_getcwd(g_frontState.baseGameInstallPath,
			sizeof(g_frontState.baseGameInstallPath));
		_chdir(g_frontState.installPath);
		RegCloseKey(registryKey);
		return;
	}
	RegCloseKey(registryKey);

#endif
}

/* Returns g_frontState.baseGameInstallPath. Only the original build calls
 * this. */
// FUNCTION: XVT 0x4CCF40
const char *File_GetBaseGameInstallPath(void)
{
	return g_frontState.baseGameInstallPath;
}

/* In the original build, makes g_frontState.baseGameInstallPath the current
 * folder; the modern build does nothing. Returns 1 either way, even when the
 * change fails. */
// FUNCTION: XVT 0x4CCF50
int File_ChangeToBaseGameInstallPath(void)
{
#ifndef XVT_MODERN
	_chdir(g_frontState.baseGameInstallPath);
#endif
	return 1;
}

/* In the original build, makes g_frontState.installPath the current folder; the
 * modern build does nothing. Returns 1 either way, even when the change
 * fails. */
// FUNCTION: XVT 0x4CCF70
int File_ChangeToInstallPath(void)
{
#ifndef XVT_MODERN
	_chdir(g_frontState.installPath);
#endif
	return 1;
}
