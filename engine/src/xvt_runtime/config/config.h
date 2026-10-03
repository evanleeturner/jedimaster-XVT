#ifndef XVT_RUNTIME_CONFIG_H
#define XVT_RUNTIME_CONFIG_H
#include "aeron/config_file.h"
#include "xvt/frontend/config.h"
#include "xvt_runtime/config/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The settings, in two YAML documents: the shipped defaults, RESOURCE/config.yaml, and the player's
 * USER/config.yaml, which holds only overrides. The resolved document is the user's laid over the
 * defaults. Every change goes through xvt_config_update_user, which checks the whole resolved result before
 * anything changes, so a failed call leaves every document and setting as it was. SetController,
 * SetKeyboard, RestoreKeyboard, SetFlightRate and SetSkipIntro change memory only; Save, Write, Import,
 * and SetGameData with save, write the file. Failures return 0 or false with a message in error naming
 * the file and, where known, the line. */

/* Read views remain valid until the next successful update or shutdown. */
/* Clears any loaded settings, sets the keyboard mapping policy from whether Aeron's debug UI exists,
 * then loads RESOURCE/aeron/scene3d_defaults.yaml, the shipped defaults (which must be valid format 3)
 * and the user file. A missing user file starts empty overrides in memory without writing one; an older
 * one is upgraded in memory only. A user file that cannot be inspected, read or accepted is an error and
 * stays untouched on disk; the defaults then stay loaded and CanResetToDefaults returns 1. */
int xvt_config_load(AeronVfs *vfs, char *error, size_t capacity);
/* Replaces the user overrides with an empty format-3 document, in memory only. Needs the defaults. */
int xvt_config_reset_to_defaults(char *error, size_t capacity);
/* Frees every document; Settings then returns NULL and the setters fail until the next Load. The
 * generation is kept. */
void xvt_config_shutdown(void);
/* The user overrides, or NULL before any are accepted. */
const AeronConfigFile *xvt_config_user_document(void);
/* Merged game/user tree. Typed settings also include the Aeron scene baseline. */
/* The user overrides laid over the defaults, or NULL before any are accepted. */
const AeronConfigFile *xvt_config_resolved_document(void);
/* The typed settings of the resolved document, or NULL before any overrides are accepted. */
const struct xvt_settings *xvt_config_settings(void);
/* The typed settings of the shipped defaults, or NULL before they load. */
const struct xvt_settings *xvt_config_default_settings(void);
/* Counts accepted updates; never reset, not even by Shutdown. */
uint64_t xvt_config_generation(void);
/* 1 while the shipped defaults are loaded, so ResetToDefaults can work. */
int xvt_config_can_reset_to_defaults(void);
/* Copies and validates overrides before optional atomic USER/config.yaml save. */
/* Needs the shipped defaults; candidate must be a map with a version from 1 to 3. Obsolete keys are
 * dropped; controller settings and joystick buttons from before version 3 are dropped and the controller
 * list starts empty; version 1's keyboard bindings are dropped; a user input.gamepad_defaults is dropped
 * with a warning; the version becomes 3. The resolved result must then pass every check: each game
 * option present and in range, the typed settings, and the keyboard bindings. With save, the file is
 * written before anything else changes. On success the new documents and settings take effect, the
 * setters are enabled, and the generation rises. */
int xvt_config_update_user(const AeronConfigFile *candidate, int save,
			   char *error, size_t capacity);
/* Checks options, then stores them as the user's input.controllers list, in memory only. */
bool xvt_config_set_controller(const struct xvt_controller_options *options,
			       char *error, size_t capacity);
/* Stores bindings in the user overrides, in memory only. */
bool xvt_config_set_keyboard(const struct xvt_keyboard_bindings *bindings,
			     char *error, size_t capacity);
/* Removes the user's keyboard bindings so the shipped ones apply, in memory only. */
bool xvt_config_restore_keyboard(char *error, size_t capacity);
/* Sets paths.game_data to path; with save, writes the user file. */
int xvt_config_set_game_data(const char *path, int save, char *error,
			     size_t capacity);
/* Updates the preference consumed at the next mission start; Save persists it. */
/* Removes flight.update_rate when unlocked matches the shipped default, else sets it to "unlocked" or
 * "native". */
bool xvt_config_set_flight_rate(bool unlocked, char *error, size_t capacity);
/* Updates the preference consumed at the next launch; Save persists it. */
/* Removes startup.skip_intro when enabled matches the shipped default, else sets it. */
bool xvt_config_set_skip_intro(bool enabled, char *error, size_t capacity);
/* Copies every game option from the resolved document into *game, checking each; limits each window
 * size to its screen resolution, sets the network type to TCP/IP and clears the IP address. On failure
 * *game is unchanged. */
int xvt_config_apply(struct game_config *game, char *error, size_t capacity);
/* Stores *game's options as user overrides, removing any that equal the shipped default, then saves.
 * Each must be in range and each string terminated. */
int xvt_config_write(const struct game_config *game, char *error,
		     size_t capacity);
/* Checks the user overrides again and writes them to USER/config.yaml. */
int xvt_config_save(char *error, size_t capacity);
/* Imports a legacy "name value" text file at path in ASSET into the user overrides, then saves. Only
 * the original option names are read; other lines are ignored. Nothing changes when no name is
 * recognized, a value is invalid or out of range, a line is longer than 510 characters, or reading
 * fails. From a file named config.cfg, joystick button values 124 to 229 move up by 4. */
int xvt_config_import(const char *path, char *error, size_t capacity);
#ifdef __cplusplus
}
#endif

#endif
