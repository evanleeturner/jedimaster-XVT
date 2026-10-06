#include "xvt/assets/file.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend_state.h"

/* The mode string "rb" that many of the game's file opens pass; never
 * written. */
// GLOBAL: XVT 0x51A854
const char g_file_mode_read_binary[3] = "rb";

/* Opens fileName with mode and returns the stream, or NULL when it fails. It
 * returns xvt_storage_open's result. */
// FUNCTION: XVT 0x4CC4A0
xvt_file *file_open(const char *file_name, const char *mode)
{
	return xvt_storage_open(file_name, mode);
}

/* Closes stream. Returns 0 on success and EOF on failure, and 0 for NULL. */
// FUNCTION: XVT 0x4CC590
int16_t file_close(xvt_file *stream) { return (int16_t)xvt_file_close(stream); }

/* Moves stream's position to offset from origin (SEEK_SET, SEEK_CUR or
 * SEEK_END); returns 0 on success, nonzero on failure. */
// FUNCTION: XVT 0x4CC5C0
int file_seek(xvt_file *stream, int offset, int16_t origin)
{
	return FILE_RAW_SEEK(stream, offset, origin);
}

/* Returns stream's position in bytes, or -1 on failure or when the position is
 * negative or over INT_MAX. */
// FUNCTION: XVT 0x4CC5E0
int file_tell(xvt_file *stream)
{
	int64_t value = AeronVfs_Tell(stream);
	return value < 0 || value > INT_MAX ? -1 : (int)value;
}

/* Returns stream's size in bytes, from AeronVfs_GetSize, or -1 when that is
 * negative or over INT_MAX. */
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

/* Writes count bytes from buffer; returns 1 when all were written, else 0. */
// FUNCTION: XVT 0x4CC790
int16_t file_write_bytes(xvt_file *stream, const void *buffer, size_t count)
{
	return !((int16_t)(FILE_RAW_WRITE(buffer, 1, count, stream) != count));
}

/* Returns 1 when wave\PBC\Pb1los07.wav and ivfiles\cal.opt can both be found
 * through xvt_storage_resolve_asset, else 0. skip_movie_checks is ignored. */
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

/* Fills g_front_state's cd_drive_letter, install_drive_letter, install_path and
 * base_game_install_path: it sets the letters to 0, install_path to
 * "BalanceOfPower" and base_game_install_path to "". required_cd_file_path is
 * ignored. */
// FUNCTION: XVT 0x4CCCC0
void file_detect_game_and_cd_paths(const char *required_cd_file_path)
{
	(void)required_cd_file_path;
	g_front_state.cd_drive_letter = 0;
	g_front_state.install_drive_letter = 0;
	strcpy(g_front_state.install_path, "BalanceOfPower");
	g_front_state.base_game_install_path[0] = 0;
}

/* Does nothing and returns 1; the 1997 game made
 * g_front_state.base_game_install_path the current folder. */
// FUNCTION: XVT 0x4CCF50
int file_change_to_base_game_install_path(void) { return 1; }

/* Does nothing and returns 1; the 1997 game made g_front_state.install_path the
 * current folder. */
// FUNCTION: XVT 0x4CCF70
int file_change_to_install_path(void) { return 1; }
