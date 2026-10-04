#include "xvt_app/setup.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/config_file.h"
#include "xvt/util/memory.h"
#include "xvt_app/setup_ui.h"
#include "xvt_runtime/assets/opt_native.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/storage/storage.h"

static char g_installation[XVT_PATH_CAPACITY];

const char *xvt_setup_installation(void) { return g_installation; }

static AeronFile *xvt_setup_open_asset(AeronVfs *vfs, const char *path,
				       char *resolved, size_t capacity)
{
	snprintf(resolved, capacity, "BalanceOfPower/%s", path);
	AeronFile *file = NULL;
	if (AeronVfs_Open(vfs, AERON_VFS_ROOT_ASSET, resolved, AERON_VFS_READ,
			  &file)) {
		return file;
	}
	snprintf(resolved, capacity, "%s", path);
	AeronVfs_Open(vfs, AERON_VFS_ROOT_ASSET, resolved, AERON_VFS_READ,
		      &file);
	return file;
}

static int xvt_setup_probe_installation(AeronVfs *vfs, const char *path,
					char *error, size_t capacity)
{
	static const char *exact[] = {"BalanceOfPower/fronttxt.txt",
				      "BalanceOfPower/frontres/top.lst",
				      "BalanceOfPower/frontres/campawds.lst",
				      "sfx/sfx.lst"};
	static const char *assets[] = {"ivfiles/cal.opt", "train/1ta01bf.tie",
				       "wave/PBC/Pb1los07.wav"};
	if (!path || !path[0] ||
	    (path[0] != '/' &&
	     !(isalpha((unsigned char)path[0]) && path[1] == ':')) ||
	    !AeronVfs_SetRoot(vfs, AERON_VFS_ROOT_ASSET, path) ||
	    !AeronVfs_SetRootOptions(
		    vfs, AERON_VFS_ROOT_ASSET,
		    AERON_VFS_ROOT_OPTION_CASE_INSENSITIVE_LOOKUP)) {
		snprintf(
			error, capacity,
			"Could not open '%s'. Choose the main XvT installation folder.",
			path ? path : "");
		return 0;
	}
	for (size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); ++i) {
		AeronFile *file = NULL;
		if (!AeronVfs_Open(vfs, AERON_VFS_ROOT_ASSET, exact[i],
				   AERON_VFS_READ, &file)) {
			snprintf(
				error, capacity,
				"Could not read '%s' in '%s'. Choose the main XvT folder containing BalanceOfPower, sfx and ivfiles.",
				exact[i], path);
			return 0;
		}
		int64_t size = AeronVfs_GetSize(file);
		if (!AeronVfs_Close(file) || size <= 0) {
			snprintf(
				error, capacity,
				"Required game file '%s' is empty or could not be read in '%s'.",
				exact[i], path);
			return 0;
		}
	}
	char resolved[XVT_PATH_CAPACITY];
	for (size_t i = 0; i < sizeof(assets) / sizeof(assets[0]); ++i) {
		AeronFile *file;
		if (!(file = xvt_setup_open_asset(vfs, assets[i], resolved,
						  sizeof resolved))) {
			snprintf(
				error, capacity,
				"Required game file '%s' is missing or could not be read in '%s' or its BalanceOfPower folder. Choose a complete installation, including missions and voices.",
				assets[i], path);
			return 0;
		}
		int64_t size = AeronVfs_GetSize(file);
		if (!AeronVfs_Close(file) || size <= 0) {
			snprintf(
				error, capacity,
				"Required game file '%s' is empty or could not be read.",
				resolved);
			return 0;
		}
	}
	int version;
	unsigned int native_size;
	AeronFile *model_file = xvt_setup_open_asset(vfs, "ivfiles/cal.opt",
						     resolved, sizeof resolved);
	uint16_t model =
		xvt_opt_read(model_file, resolved, &version, &native_size);
	if (!model) {
		snprintf(
			error, capacity,
			"Could not load the required ship model ivfiles/cal.opt in '%s'.",
			path);
		return 0;
	}
	memory_free_handle(model);
	return 1;
}

int xvt_setup_resolve_installation(const char *path, char *resolved,
				   size_t resolved_capacity, char *error,
				   size_t capacity)
{
	char candidate[XVT_PATH_CAPACITY];
	if (!path || !path[0] || strlen(path) >= sizeof candidate ||
	    !resolved || !resolved_capacity) {
		snprintf(
			error, capacity,
			"Choose an XvT installation folder or its BalanceOfPower folder.");
		return 0;
	}
	strcpy(candidate, path);
	for (char *cursor = candidate; *cursor; ++cursor) {
		if (*cursor == '\\') {
			*cursor = '/';
		}
	}
	size_t length = strlen(candidate);
	while (length > 1 && candidate[length - 1] == '/' &&
	       !(length == 3 && candidate[1] == ':')) {
		candidate[--length] = 0;
	}
	AeronVfs *vfs = AeronVfs_Create(&(AeronVfsConfig){
		.org_name = "TotallyOpen", .app_name = "OpenXvT"});
	if (!vfs) {
		snprintf(error, capacity,
			 "Could not check this installation. Try again.");
		return 0;
	}
	int success =
		xvt_setup_probe_installation(vfs, candidate, error, capacity);
	char *separator = strrchr(candidate, '/');
	if (!success && separator) {
		const char *name = separator + 1;
		const char *expected = "balanceofpower";
		while (*name && *expected &&
		       tolower((unsigned char)*name) == *expected) {
			++name;
			++expected;
		}
		if (!*name && !*expected) {
			/* Preserve POSIX and drive roots when selecting their BoP child. */
			if (separator == candidate ||
			    (separator == candidate + 2 &&
			     candidate[1] == ':')) {
				separator[1] = 0;
			} else {
				*separator = 0;
			}
			success = xvt_setup_probe_installation(vfs, candidate,
							       error, capacity);
		}
	}
	if (success) {
		if (strlen(candidate) >= resolved_capacity) {
			snprintf(error, capacity,
				 "Game-data directory is too long.");
			success = 0;
		} else {
			strcpy(resolved, candidate);
			if (capacity) {
				error[0] = 0;
			}
		}
	}
	AeronVfs_Destroy(vfs);
	return success;
}

/* Import a selected pilot and its counterpart together. Existing names are
 * checked before either write; collisions require an explicit new basename. */
static int xvt_setup_import_pilot(const char *path, const char *pilot_name,
				  char *error, size_t capacity)
{
	char source[XVT_PATH_CAPACITY];
	char source_pair[XVT_PATH_CAPACITY];
	char target[64];
	char target_pair[64];
	uint8_t *data[2] = {NULL, NULL};
	size_t sizes[2] = {0, 0};
	const char *sources[2] = {source, source_pair};
	const char *targets[2] = {target, target_pair};
	int success = 0;
	int written = 0;
	if (!xvt_storage_normalize(path, source, sizeof(source))) {
		goto done;
	}
	char *extension = strrchr(source, '.');
	if (!extension || strlen(extension) != 4 ||
	    tolower((unsigned char)extension[1]) != 'p' ||
	    tolower((unsigned char)extension[2]) != 'l' ||
	    (tolower((unsigned char)extension[3]) != 't' &&
	     extension[3] != '2')) {
		goto done;
	}
	const char *basename = strrchr(source, '/');
	basename = basename ? basename + 1 : source;
	if (pilot_name) {
		if (!pilot_name[0] || strlen(pilot_name) > 24 ||
		    strpbrk(pilot_name, "/\\:.?*")) {
			goto done;
		}
		snprintf(target, sizeof(target), "%s%s", pilot_name, extension);
	} else {
		if (strlen(basename) >= sizeof(target)) {
			goto done;
		}
		strcpy(target, basename);
	}
	strcpy(source_pair, source);
	strcpy(target_pair, target);
	source_pair[strlen(source_pair) - 1] = extension[3] == '2' ? 't' : '2';
	target_pair[strlen(target_pair) - 1] = extension[3] == '2' ? 't' : '2';
	for (int i = 0; i < 2; ++i) {
		int status =
			xvt_storage_probe(AERON_VFS_ROOT_ASSET, sources[i]);
		if (i && status == 0) {
			char resolved[XVT_PATH_CAPACITY];
			const char *source_basename = strrchr(source_pair, '/');
			status = xvt_storage_resolve_asset(
				source_basename ? source_basename + 1
						: source_pair,
				resolved, sizeof(resolved));
			if (status == 1) {
				strcpy(source_pair, resolved);
			}
		}
		if (xvt_storage_probe(AERON_VFS_ROOT_USER, targets[i]) != 0) {
			goto done;
		}
		if (i && status == 0 && extension[3] != '2') {
			continue;
		}
		if (status != 1 ||
		    !AeronVfs_ReadAll(xvt_storage_vfs(), AERON_VFS_ROOT_ASSET,
				      sources[i], 296238, &data[i],
				      &sizes[i])) {
			goto done;
		}
		size_t expected = sources[i][strlen(sources[i]) - 1] == '2'
					  ? 296238
					  : 0x3df3a;
		if (sizes[i] != expected || !memchr(data[i], 0, 14)) {
			goto done;
		}
	}
	for (int i = 0; i < 2; ++i) {
		if (data[i] && !AeronVfs_WriteAllAtomic(
				       xvt_storage_vfs(), AERON_VFS_ROOT_USER,
				       targets[i], data[i], sizes[i])) {
			goto done;
		}
		written = i + 1;
	}
	success = 1;
done:
	if (!success) {
		for (int i = 0; i < written; ++i) {
			if (data[i]) {
				AeronVfs_Remove(xvt_storage_vfs(),
						AERON_VFS_ROOT_USER,
						targets[i]);
			}
		}
		snprintf(
			error, capacity,
			"Could not import pilot '%s': check its record and companion, or use --pilot-name <unused basename> to resolve a USER filename collision.",
			path);
	}
	free(data[0]);
	free(data[1]);
	return success;
}

xvt_setup_result xvt_setup_run(const struct xvt_launch_options *options,
			       struct xvt_app_ui *ui, char *error,
			       size_t capacity)
{
	AeronVfs *vfs = xvt_storage_vfs();
	int save_due = options->save_config;
	if (!AeronVfs_SetRootOptions(
		    vfs, AERON_VFS_ROOT_USER,
		    AERON_VFS_ROOT_OPTION_CASE_INSENSITIVE_LOOKUP)) {
		return XVT_SETUP_ERROR;
	}
	int loaded = xvt_config_load(vfs, error, capacity);
	if (!loaded && !xvt_config_can_reset_to_defaults()) {
		return XVT_SETUP_ERROR;
	}
	if (options->reset_config) {
		if (!xvt_config_reset_to_defaults(error, capacity)) {
			return XVT_SETUP_ERROR;
		}
		loaded = 1;
		save_due = 1;
		error[0] = 0;
	}
	if (ui) {
		const struct xvt_settings *settings =
			loaded ? xvt_config_settings()
			       : xvt_config_default_settings();
		if (!xvt_app_ui_init(ui, settings->ui_font, error, capacity)) {
			return XVT_SETUP_ERROR;
		}
	}
	if (!loaded) {
		if (!ui) {
			return XVT_SETUP_ERROR;
		}
		xvt_setup_result result =
			xvt_setup_ui_run(ui, NULL, 0, error, capacity);
		if (result != XVT_SETUP_SUCCESS) {
			return result;
		}
		save_due = 1;
	}
	const char *candidate = options->game_data
					? options->game_data
					: xvt_config_settings()->game_data;
	char selected[XVT_PATH_CAPACITY];
	if (strlen(candidate) >= sizeof(selected)) {
		snprintf(error, capacity, "Game-data directory is too long.");
		return XVT_SETUP_ERROR;
	}
	strcpy(selected, candidate);
	if (options->setup || !selected[0] ||
	    !xvt_setup_resolve_installation(selected, selected, sizeof selected,
					    error, capacity)) {
		if (!ui || options->game_data) {
			if (!selected[0]) {
				snprintf(
					error, capacity,
					"No installation configured. Use --game-data <XvT root> --save-config, or launch the setup window.");
			}
			return XVT_SETUP_ERROR;
		}
		xvt_setup_result result = xvt_setup_ui_run(
			ui, selected, sizeof selected, error, capacity);
		if (result != XVT_SETUP_SUCCESS) {
			return result;
		}
		/* The dialog saved the path; a requested legacy config import below
		 * still needs its own save after applying the imported settings. */
		save_due = options->import_config != NULL;
	}
	if (!AeronVfs_SetRoot(vfs, AERON_VFS_ROOT_ASSET, selected) ||
	    !AeronVfs_SetRootOptions(
		    vfs, AERON_VFS_ROOT_ASSET,
		    AERON_VFS_ROOT_OPTION_CASE_INSENSITIVE_LOOKUP)) {
		snprintf(error, capacity, "Cannot mount selected installation");
		return XVT_SETUP_ERROR;
	}
	snprintf(g_installation, sizeof g_installation, "%s", selected);
	if (options->import_config &&
	    !xvt_config_import(options->import_config, error, capacity)) {
		return XVT_SETUP_ERROR;
	}
	if (options->import_pilot &&
	    !xvt_setup_import_pilot(options->import_pilot, options->pilot_name,
				    error, capacity)) {
		return XVT_SETUP_ERROR;
	}
	if (save_due) {
		if (!xvt_config_set_game_data(selected, 1, error, capacity)) {
			return XVT_SETUP_ERROR;
		}
	}
	if (ui && !Aeron_SetFullscreen(xvt_config_settings()->fullscreen)) {
		snprintf(error, capacity, "Cannot apply video.window_mode.");
		return XVT_SETUP_ERROR;
	}
	XVT_LOG_INFO("setup.installed root=\"%s\"", selected);
	return XVT_SETUP_SUCCESS;
}
