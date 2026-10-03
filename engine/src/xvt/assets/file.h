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
typedef AeronFile xvt_file;
#define FILE_RAW_OPEN xvt_storage_open
#define FILE_RAW_CLOSE xvt_file_close
#define FILE_RAW_READ xvt_file_read
#define FILE_RAW_WRITE xvt_file_write
#define FILE_RAW_SEEK xvt_file_seek
#define FILE_RAW_TELL xvt_file_tell
#define FILE_GETS xvt_file_gets
#define FILE_GETC xvt_file_getc
#define FILE_PUTC xvt_file_putc
#define FILE_SCANF xvt_file_scanf
#define FILE_PRINTF xvt_file_printf
#define FILE_FLUSH xvt_file_flush
#define FILE_EOF AeronVfs_Eof
#define FILE_HAS_ERROR AeronVfs_HasError
#define FILE_CLEAR_ERROR AeronVfs_ClearError
#define FILE_RENAME xvt_storage_rename
#define FILE_REMOVE xvt_storage_remove
#else
typedef FILE xvt_file;
#define FILE_RAW_OPEN fopen
#define FILE_RAW_CLOSE fclose
#define FILE_RAW_READ fread
#define FILE_RAW_WRITE fwrite
#define FILE_RAW_SEEK fseek
#define FILE_RAW_TELL ftell
#define FILE_GETS fgets
#define FILE_GETC fgetc
#define FILE_PUTC fputc
#define FILE_SCANF fscanf
#define FILE_PRINTF fprintf
#define FILE_FLUSH fflush
#define FILE_EOF feof
#define FILE_HAS_ERROR ferror
#define FILE_CLEAR_ERROR clearerr
#define FILE_RENAME rename
#define FILE_REMOVE remove
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const char g_file_mode_read_binary[3];

/* Stored as int16_t in the binary (IDB enum file_error_string_id). */
typedef int16_t file_error_string_id;

enum {
	FILE_ERROR_STR_NOT_ENOUGH_MEMORY = 0x0,
	FILE_ERROR_STR_FILE_MISSING = 0x1,
	FILE_ERROR_STR_STRINGS_OUT_OF_SYNC = 0x2,
	FILE_ERROR_STR_PRESS_KEY_TO_EXIT = 0x3,
};

xvt_file *file_open(const char *file_name, const char *mode);
int16_t file_close(xvt_file *stream);
int file_seek(xvt_file *stream, int offset, int16_t origin);
int file_tell(xvt_file *stream);
int file_get_size(xvt_file *stream);
int16_t file_read_byte(xvt_file *stream, uint8_t *value);
int16_t file_read_word(xvt_file *stream, uint16_t *value);
int16_t file_read_dword(xvt_file *stream, unsigned int *value);
int16_t file_read_bytes(xvt_file *stream, void *buffer, size_t count);
int16_t file_write_byte(xvt_file *stream, char value);
int16_t file_write_word(xvt_file *stream, int value);
int16_t file_write_dword(xvt_file *stream, int value);
int16_t file_write_bytes(xvt_file *stream, const void *buffer, size_t count);
int file_check_required_cd_movie_assets_present(void);
int file_check_game_cd_present(int skip_movie_checks);
char file_get_cd_drive_letter(void);
char file_get_install_drive_letter(void);
const char *file_get_install_path(void);
int file_find_cd_drive_letter(const char *relative_cd_file_path);
void file_detect_game_and_cd_paths(const char *required_cd_file_path);
const char *file_get_base_game_install_path(void);
int file_change_to_base_game_install_path(void);
int file_change_to_install_path(void);

#ifdef __cplusplus
}
#endif

#endif
