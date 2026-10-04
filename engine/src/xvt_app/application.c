#include "xvt_app/application.h"

#include <stdio.h>

#include "aeron/compat/host.h"
#include "xvt_app/log_sink.h"
#include "xvt_app/settings/settings.h"
#include "xvt_app/setup.h"
#include "xvt_app/ui.h"
#include "xvt_remaster/xvt_remaster.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#include "xvt_runtime/storage/storage.h"

static uint64_t xvt_application_presentation_interval_us(void)
{
	double rate = Aeron_PresentationRate();
	/* Aeron normally supplies the display rate, including its 60 Hz fallback. */
	if (!(rate >= 1.0 && rate <= 1000.0)) {
		rate = 60.0;
	}
	return (uint64_t)(1000000.0 / rate + 0.5);
}

static void
xvt_application_discover_controllers(const AeronInputSnapshot *input)
{
	static char previous_error[512];
	const struct xvt_settings *settings = xvt_config_settings();
	if (!settings || xvt_settings_menu_is_open()) {
		return;
	}
	struct xvt_controller_options candidate = settings->controller;
	char error[512] = {0};
	bool ok = xvt_controller_options_add_new_gamepads(
		&candidate, &xvt_config_default_settings()->gamepad_defaults,
		input, error, sizeof error);
	if (!xvt_controller_options_equals(&candidate, &settings->controller)) {
		char store_error[512];
		if (xvt_config_set_controller(&candidate, store_error,
					      sizeof store_error)) {
			xvt_controller_mapping_set_options(
				&xvt_config_settings()->controller);
		} else {
			snprintf(error, sizeof error, "%s", store_error);
			ok = false;
		}
	}
	if (!ok && strcmp(previous_error, error)) {
		XVT_LOG_WARN("input.discovery_failed error=\"%s\"", error);
	}
	snprintf(previous_error, sizeof previous_error, "%s", ok ? "" : error);
}

static int xvt_application_frame_loop(void)
{
	while (!xvt_port_service_quit()) {
		int32_t delta_us = Aeron_BeginFrame();
		if (xvt_port_service_quit()) {
			break;
		}
		const AeronInputSnapshot *input = Aeron_InputSnapshot();
		xvt_application_discover_controllers(input);
		bool menu_opened = xvt_settings_menu_begin_frame(input);
		int debug_key = xvt_keyboard_mapping_find_shortcut_press(
			input, XVT_KEYBOARD_SHORTCUT_DEBUG);
		if (debug_key >= 0 && !xvt_settings_menu_captures_keyboard()) {
			Aeron_DebugUiToggle();
			xvt_input_block_key_until_released(debug_key);
		}
		xvt_remaster_begin_frame(input);
		xvt_port_update(delta_us);
		menu_opened |= xvt_settings_menu_consume_runtime_request();
		if (xvt_port_service_quit()) {
			break;
		}
		xvt_remaster_frame(delta_us);
		if (!menu_opened) {
			xvt_settings_menu_frame(input,
						(float)delta_us / 1000000.0f);
		}
		if (!Aeron_Present()) {
			Aeron_RequestFatalRendererError("frame presentation");
			break;
		}
		uint64_t wake_delay_us =
			xvt_application_presentation_interval_us();
		uint64_t task_delay_us = xvt_port_next_wake_delay_us();
		if (task_delay_us < wake_delay_us) {
			wake_delay_us = task_delay_us;
		}
		Aeron_WaitForNextFrame(wake_delay_us);
	}
	return xvt_port_get_exit_code();
}

int xvt_application_run(const struct xvt_launch_options *options)
{
	struct xvt_app_ui ui = {0};
	int exit_code = 1;
	char error[1024] = {0};
	if (!xvt_log_sink_install(options)) {
		return 2;
	}
	if (options->check_installation) {
		char resource_root[XVT_PATH_CAPACITY];
		if (!xvt_host_config_resolve_resource_root(
			    options, resource_root, sizeof(resource_root))) {
			fprintf(stderr,
				"OpenXvT: cannot resolve application resources.\n");
			return 1;
		}
		AeronVfsConfig vfs_config = {.org_name = "TotallyOpen",
					     .app_name = "OpenXvT",
					     .resource_root = resource_root};
		AeronVfs *vfs = AeronVfs_Create(&vfs_config);
		xvt_storage_bind(vfs);
		int success = vfs && (xvt_setup_run(options, NULL, error,
						    sizeof(error)) ==
				      XVT_SETUP_SUCCESS);
		if (!success) {
			fprintf(stderr, "OpenXvT: %s\n", error);
		}
		xvt_config_shutdown();
		xvt_storage_bind(NULL);
		AeronVfs_Destroy(vfs);
		return success ? 0 : 1;
	}
	AeronConfig config;
	xvt_host_config_fill_aeron_config(options, &config);
	XVT_LOG_INFO("app.start version=\"%s\"", OPENXVT_VERSION);
	if (!Aeron_Init(&config)) {
		/* Aeron unwinds its own partial initialization; shutdown is idempotent. */
		Aeron_Shutdown();
		return 1;
	}
	xvt_storage_bind(Aeron_GetVfs());
	xvt_setup_result setup_result =
		xvt_setup_run(options, &ui, error, sizeof error);
	if (setup_result != XVT_SETUP_SUCCESS) {
		if (setup_result == XVT_SETUP_CANCELLED) {
			exit_code = 0;
		} else {
			XVT_LOG_ERROR("setup.failed error=\"%s\"", error);
		}
		goto cleanup;
	}
	xvt_render_snapshot_init();
	if (!xvt_settings_menu_init(&ui, error, sizeof error)) {
		XVT_LOG_ERROR("settings.init_failed error=\"%s\"", error);
		goto cleanup;
	}
	if (!xvt_remaster_init()) {
		goto cleanup;
	}
	AeronWinmmCdAudioDesc cd = {Aeron_GetVfs(), AERON_VFS_ROOT_ASSET,
				    "BalanceOfPower/MUSIC"};
	if (!AeronWinmm_ConfigureCdAudio(&cd)) {
		XVT_LOG_ERROR("setup.music_failed");
		goto cleanup;
	}
	xvt_port_set_skip_intro(options->skip_intro ||
				xvt_config_settings()->skip_intro);
	if (xvt_port_init()) {
		XVT_LOG_INFO("app.ready");
		exit_code = xvt_application_frame_loop();
	}
cleanup:
	xvt_settings_menu_flush_for_exit();
	xvt_settings_menu_shutdown();
	xvt_port_shutdown();
	xvt_remaster_shutdown();
	xvt_render_snapshot_shutdown();
	xvt_app_ui_shutdown(&ui);
	if (xvt_port_get_exit_code()) {
		exit_code = xvt_port_get_exit_code();
	}
	AeronWinmm_Shutdown();
	xvt_config_shutdown();
	xvt_storage_bind(NULL);
	Aeron_Shutdown();
	return exit_code;
}
