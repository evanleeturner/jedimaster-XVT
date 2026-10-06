#include "xvt/assets/file.h"

#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend_state.h"
#include <limits.h>
#include <ctype.h>
#include <string.h>


/* The mode string "rb" that many of the game's file opens pass; never
 * written. */
// GLOBAL: XVT 0x51A854
const char g_file_mode_read_binary[3] = "rb";

/* Opens fileName with mode and returns the stream, or NULL when every try
 * fails. The modern build returns xvt_storage_open's result. The original build
 * tries the current folder, then the base game's install folder (changing to it
 * and back to g_front_state.install_path). Then, when mode starts with neither
 * 'w' nor 'a' and g_front_state.cd_drive_letter is nonzero, it suspends CD audio,
 * tries "<letter>:\BalanceOfPower\<fileName>" and then "<letter>\<fileName>",
 * with no colon, and resumes the audio; with the '.' file_find_cd_drive_letter
 * gives, the last is ".\<fileName>". Each success adds 1 to g_open_file_count. */
// FUNCTION: XVT 0x4CC4A0
xvt_file *file_open(const char *file_name, const char *mode)
{
	return xvt_storage_open(file_name, mode);
}

/* Closes stream. Returns 0 on success and EOF on failure. The modern build also
 * returns 0 for NULL; the original build lowers g_open_file_count for a non-NULL
 * stream and, for NULL, returns its uninitialized local result. */
// FUNCTION: XVT 0x4CC590
int16_t file_close(xvt_file *stream)
{
	return (int16_t)xvt_file_close(stream);
}

/* Moves stream's position to offset from origin (SEEK_SET, SEEK_CUR or
 * SEEK_END); returns 0 on success, nonzero on failure. */
// FUNCTION: XVT 0x4CC5C0
int file_seek(xvt_file *stream, int offset, int16_t origin)
{
	return FILE_RAW_SEEK(stream, offset, origin);
}

/* Returns stream's position in bytes, or -1 on failure; the modern build also
 * returns -1 when the position is negative or over INT_MAX. */
// FUNCTION: XVT 0x4CC5E0
int file_tell(xvt_file *stream)
{
	int64_t value = AeronVfs_Tell(stream);
	return value < 0 || value > INT_MAX ? -1 : (int)value;
}

/* Returns stream's size in bytes. The original build seeks to the end, takes
 * the position and seeks back to where it was, without checking either seek;
 * the modern build asks AeronVfs_GetSize and returns -1 when that is negative
 * or over INT_MAX. */
// FUNCTION: XVT 0x4CC5F0
int file_get_size(xvt_file *stream)
{
	int64_t value = AeronVfs_GetSize(stream);
	return value < 0 || value > INT_MAX ? -1 : (int)value;
}

/* Reads one byte into *value; returns 1 when it was read, else 0. */
// FUNCTION: XVT 0x4CC630
int16_t file_read_byte(xvt_file *stream, uint8_t *value)
{
	return !((int16_t)(FILE_RAW_READ(value, 1, 1, stream) != 1));
}

/* Reads two bytes into *value as they lie in the file; returns 1 when both were
 * read, else 0. */
// FUNCTION: XVT 0x4CC660
int16_t file_read_word(xvt_file *stream, uint16_t *value)
{
	return !((int16_t)(FILE_RAW_READ(value, 1, 2, stream) != 2));
}

/* Reads four bytes into *value as they lie in the file; returns 1 when all four
 * were read, else 0. Nothing calls this. */
// FUNCTION: XVT 0x4CC690
int16_t file_read_dword(xvt_file *stream, unsigned int *value)
{
	return !((int16_t)(FILE_RAW_READ(value, 1, 4, stream) != 4));
}

/* Reads count bytes into buffer; returns 1 when all were read, else 0. */
// FUNCTION: XVT 0x4CC6C0
int16_t file_read_bytes(xvt_file *stream, void *buffer, size_t count)
{
	return !((int16_t)(FILE_RAW_READ(buffer, 1, count, stream) != count));
}

/* Writes value as one byte; returns 1 when it was written, else 0. */
// FUNCTION: XVT 0x4CC700
int16_t file_write_byte(xvt_file *stream, char value)
{
	return !((int16_t)(FILE_RAW_WRITE(&value, 1, 1, stream) != 1));
}

/* Writes the first 2 bytes of value as stored in memory: on a little-endian
 * machine its low 16 bits, low byte first. Returns 1 when both were written,
 * else 0. Nothing calls this. */
// FUNCTION: XVT 0x4CC730
int16_t file_write_word(xvt_file *stream, int value)
{
	return !((int16_t)(FILE_RAW_WRITE(&value, 1, 2, stream) != 2));
}

/* Writes value's 4 bytes as stored in memory; returns 1 when all four were
 * written, else 0. Nothing calls this. */
// FUNCTION: XVT 0x4CC760
int16_t file_write_dword(xvt_file *stream, int value)
{
	return !((int16_t)(FILE_RAW_WRITE(&value, 1, 4, stream) != 4));
}

/* Writes count bytes from buffer; returns 1 when all were written, else 0. */
// FUNCTION: XVT 0x4CC790
int16_t file_write_bytes(xvt_file *stream, const void *buffer, size_t count)
{
	return !((int16_t)(FILE_RAW_WRITE(buffer, 1, count, stream) != count));
}

/* Returns 1 when wave\PBC\Pb1los07.wav and movies\imp1snd.smk can both be
 * opened, else 0. The original build looks for them at the root of
 * g_front_state.cd_drive_letter's drive, with CD audio suspended meanwhile, and
 * returns 0 when that letter is 0; the modern build asks
 * xvt_storage_resolve_asset. Only the original build calls this. */
// FUNCTION: XVT 0x4CC930
int file_check_required_cd_movie_assets_present(void)
{
	char path[XVT_PATH_CAPACITY];
	return xvt_storage_resolve_asset("wave/PBC/Pb1los07.wav", path,
					 sizeof(path)) == 1 &&
	       xvt_storage_resolve_asset("movies/imp1snd.smk", path,
					 sizeof(path)) == 1;
}

/* Returns 1 when wave\PBC\Pb1los07.wav and ivfiles\cal.opt can both be opened,
 * else 0. The original build looks for them at the root of
 * g_front_state.cd_drive_letter's drive, returns 0 when that letter is 0, and
 * unless skip_movie_checks is nonzero also needs amovie\a.wrk and bmovie\a.wrk
 * there. The modern build ignores skip_movie_checks and asks
 * xvt_storage_resolve_asset. */
// FUNCTION: XVT 0x4CC9F0
int file_check_game_cd_present(int skip_movie_checks)
{
	(void)skip_movie_checks;
	char path[XVT_PATH_CAPACITY];
	return xvt_storage_resolve_asset("wave/PBC/Pb1los07.wav", path,
					 sizeof(path)) == 1 &&
	       xvt_storage_resolve_asset("ivfiles/cal.opt", path,
					 sizeof(path)) == 1;
}

/* Returns g_front_state.cd_drive_letter. Only the original build calls this. */
// FUNCTION: XVT 0x4CCB90
char file_get_cd_drive_letter(void) { return g_front_state.cd_drive_letter; }

/* Returns g_front_state.install_drive_letter. Nothing calls this. */
// FUNCTION: XVT 0x4CCBA0
char file_get_install_drive_letter(void)
{
	return g_front_state.install_drive_letter;
}

/* Returns g_front_state.install_path. Nothing calls this. */
// FUNCTION: XVT 0x4CCBB0
const char *file_get_install_path(void) { return g_front_state.install_path; }

/* The original's CD drive search is not reconstructed: this ignores
 * relative_cd_file_path and always returns '.', which
 * file_detect_game_and_cd_paths, outside XVT_MODERN, stores as the CD drive
 * letter. */
/* Only the original build calls this. */
// FUNCTION: XVT 0x4CCBC0
int file_find_cd_drive_letter(const char *relative_cd_file_path)
{
	(void)relative_cd_file_path;

	return '.';
}

/* Fills g_front_state's cd_drive_letter, install_drive_letter, install_path and
 * base_game_install_path. The modern build sets the letters to 0, install_path to
 * "BalanceOfPower" and base_game_install_path to "". The original build sets
 * cd_drive_letter from file_find_cd_drive_letter when required_cd_file_path is not
 * NULL, and creates the registry key of version 2.0 under HKEY_LOCAL_MACHINE
 * (SOFTWARE\LucasArts Entertainment Company\X-Wing vs. TIE Fighter) when
 * missing. When that key's "Install Path" value cannot be read, it takes the
 * current folder as install_path, the current drive's letter, lowercase, as
 * install_drive_letter and the folder above as base_game_install_path, and stops.
 * Otherwise it drops a final backslash from install_path, takes its first
 * letter, lowercase, as install_drive_letter, and reads base_game_install_path from
 * the 1.0 key's "Install Path", falling back to the folder above install_path.
 * Either way it leaves install_path the current folder. */
// FUNCTION: XVT 0x4CCCC0
void file_detect_game_and_cd_paths(const char *required_cd_file_path)
{
	(void)required_cd_file_path;
	g_front_state.cd_drive_letter = 0;
	g_front_state.install_drive_letter = 0;
	strcpy(g_front_state.install_path, "BalanceOfPower");
	g_front_state.base_game_install_path[0] = 0;
}

/* Returns g_front_state.base_game_install_path. Only the original build calls
 * this. */
// FUNCTION: XVT 0x4CCF40
const char *file_get_base_game_install_path(void)
{
	return g_front_state.base_game_install_path;
}

/* In the original build, makes g_front_state.base_game_install_path the current
 * folder; the modern build does nothing. Returns 1 either way, even when the
 * change fails. */
// FUNCTION: XVT 0x4CCF50
int file_change_to_base_game_install_path(void)
{
	return 1;
}

/* In the original build, makes g_front_state.install_path the current folder; the
 * modern build does nothing. Returns 1 either way, even when the change
 * fails. */
// FUNCTION: XVT 0x4CCF70
int file_change_to_install_path(void)
{
	return 1;
}
