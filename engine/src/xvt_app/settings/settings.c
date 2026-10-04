#include "xvt_app/settings/settings.h"

#include <stdio.h>
#include <string.h>

#include "xvt_app/settings/controller_page.h"
#include "xvt_app/settings/installation_page.h"
#include "xvt_app/settings/keyboard_page.h"
#include "xvt_app/settings/mouse_page.h"
#include "xvt_app/settings/video_options.h"
#include "xvt_app/settings/video_page.h"
#include "xvt_remaster/config.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/timing/flight_timing.h"

static struct {
	AeronUiContext *ui;
	xvt_settings_page_fn pages[5];
	bool open;
	bool close_requested;
	bool capture_owns_frame;
	bool ready;
	int page;
	int exit_confirmation_open;
	char error[1024];
	struct xvt_controller_settings controller;
	struct xvt_keyboard_settings keyboard;

	struct {
		uint32_t instance;
		bool down;
		bool armed;
	} start[AERON_CONTROLLER_MAX];
} g_menu;

static void xvt_settings_menu_draw_game(AeronUiContext *ui,
					const AeronInputSnapshot *input)
{
	static const char *const rates[] = {"Native (31.25 Hz)", "Unlocked"};
	int skip_intro = xvt_config_settings()->skip_intro;
	AeronUi_Header(ui, "Startup");
	if (AeronUi_Toggle(ui, "Skip intro cutscenes on launch", &skip_intro)) {
		char error[1024];
		if (!xvt_config_set_skip_intro(skip_intro != 0, error,
					       sizeof error)) {
			xvt_settings_menu_report_error(error);
		}
	}
	AeronUi_Spacer(ui, 8.0f);
	int unlocked = xvt_config_settings()->flight_unlocked;
	AeronUi_Header(ui, "Flight");
	if (AeronUi_Selector(ui, "Flight Rate", &unlocked, rates, 2)) {
		char error[1024];
		if (!xvt_config_set_flight_rate(unlocked != 0, error,
						sizeof error)) {
			xvt_settings_menu_report_error(error);
		}
	}
	AeronUi_Help(ui, "Applies to the next mission.");
	if (xvt_flight_task_is_active() && !xvt_flight_task_is_loading()) {
		xvt_flight_timing_profile profile =
			xvt_flight_timing_session_profile();
		AeronUi_Help(ui, profile == XVT_FLIGHT_TIMING_NETWORK_125
					 ? "Active mission: 125 Hz"
				 : profile == XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED
					 ? "Active mission: Unlocked"
					 : "Active mission: 31.25 Hz");
	}
	AeronUi_Spacer(ui, 8.0f);
	xvt_installation_page_draw(ui, input);
}

static void xvt_settings_menu_draw_controller(AeronUiContext *ui,
					      const AeronInputSnapshot *input)
{
	xvt_controller_settings_draw(&g_menu.controller, ui, input);
}

static void xvt_settings_menu_draw_keyboard(AeronUiContext *ui,
					    const AeronInputSnapshot *input)
{
	(void)input;
	xvt_keyboard_settings_draw(&g_menu.keyboard, ui);
}

bool xvt_settings_menu_init(struct xvt_app_ui *ui, char *error, size_t capacity)
{
	memset(&g_menu, 0, sizeof g_menu);
	g_menu.ui = xvt_app_ui_context(ui);
	g_menu.pages[0] = xvt_settings_menu_draw_game;
	g_menu.pages[1] = xvt_video_page_draw;
	g_menu.pages[2] = xvt_settings_menu_draw_controller;
	g_menu.pages[3] = xvt_settings_menu_draw_keyboard;
	g_menu.pages[4] = xvt_mouse_page_draw;
	xvt_video_options_configure(xvt_remaster_config_apply_video);
	g_menu.ready = xvt_installation_page_init(error, capacity);
	return g_menu.ready;
}

void xvt_settings_menu_flush_for_exit(void)
{
	if (!g_menu.ready) {
		return;
	}
	char error[1024];
	if (!xvt_video_options_flush(true, error, sizeof error)) {
		XVT_LOG_ERROR("settings.save_failed part=video error=\"%s\"",
			      error);
	}
	if (g_menu.open) {
		if (!xvt_keyboard_settings_commit(&g_menu.keyboard, error,
						  sizeof error)) {
			XVT_LOG_ERROR(
				"settings.save_failed part=keyboard error=\"%s\"",
				error);
		}
		if (!xvt_controller_settings_commit(&g_menu.controller, error,
						    sizeof error)) {
			XVT_LOG_ERROR(
				"settings.save_failed part=controller error=\"%s\"",
				error);
		}
		if (!xvt_installation_page_flush(error, sizeof error)) {
			XVT_LOG_ERROR(
				"settings.save_failed part=installation error=\"%s\"",
				error);
		}
	}
	if (!xvt_config_save(error, sizeof error)) {
		XVT_LOG_ERROR("settings.save_failed part=config error=\"%s\"",
			      error);
	}
}

void xvt_settings_menu_shutdown(void)
{
	xvt_installation_page_shutdown();
	xvt_keyboard_settings_cancel_capture(&g_menu.keyboard, g_menu.ui);
	if (g_menu.ui) {
		AeronUi_CancelControllerCapture(g_menu.ui);
	}
	memset(&g_menu, 0, sizeof g_menu);
}

void xvt_settings_menu_set_page(int page, xvt_settings_page_fn draw)
{
	if ((unsigned)page < 5) {
		g_menu.pages[page] = draw;
	}
}

bool xvt_settings_menu_is_open(void) { return g_menu.open; }

bool xvt_settings_menu_captures_keyboard(void)
{
	return g_menu.open && AeronUi_KeyboardCaptureActive(g_menu.ui);
}

bool xvt_settings_menu_capture_owns_frame(void)
{
	return g_menu.open && g_menu.capture_owns_frame;
}

void xvt_settings_menu_show(void)
{
	if (g_menu.ui && !g_menu.open) {
		g_menu.open = true;
		xvt_installation_page_open();
		xvt_controller_settings_open(&g_menu.controller,
					     xvt_config_settings());
		xvt_keyboard_settings_open(&g_menu.keyboard,
					   xvt_config_settings());
		g_menu.error[0] = 0;
		xvt_input_set_captured(true);
		xvt_port_set_settings_open(1);
		Aeron_SetHostCursorVisible(1);
	}
}

void xvt_settings_menu_request_close(void)
{
	if (g_menu.open) {
		g_menu.close_requested = true;
	}
}

bool xvt_settings_menu_close_requested(void) { return g_menu.close_requested; }

void xvt_settings_menu_complete_close(void)
{
	xvt_keyboard_settings_cancel_capture(&g_menu.keyboard, g_menu.ui);
	xvt_controller_settings_cancel_capture(&g_menu.controller, g_menu.ui);
	xvt_installation_page_cancel_picker();
	AeronUi_CancelControllerCapture(g_menu.ui);
	g_menu.open = g_menu.close_requested = g_menu.capture_owns_frame =
		false;
	g_menu.exit_confirmation_open = 0;
	xvt_port_set_settings_open(0);
}

void xvt_settings_menu_report_error(const char *error)
{
	snprintf(g_menu.error, sizeof g_menu.error, "%s", error);
	g_menu.close_requested = false;
}

static void xvt_settings_menu_draw_exit_confirmation(AeronUiContext *ui)
{
	if (!AeronUi_BeginModal(ui, "EXIT GAME", &g_menu.exit_confirmation_open,
				NULL)) {
		return;
	}
	AeronUi_Help(ui, "Exit OpenXvT and return to the desktop?");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Cancel")) {
		g_menu.exit_confirmation_open = 0;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Exit Game##exit-confirmation")) {
		g_menu.exit_confirmation_open = 0;
		Aeron_RequestQuit();
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

void xvt_settings_menu_frame(const AeronInputSnapshot *input, float seconds)
{
	if (!g_menu.open || !input) {
		return;
	}
	static const char *pages[] = {"Game", "Video", "Controller", "Keyboard",
				      "Mouse"};
	AeronUi_BeginFrame(
		g_menu.ui,
		&(AeronUiFrameDesc){.input = input, .dt_seconds = seconds});
	if (AeronUi_BeginWindow(g_menu.ui, "OpenXvT SETTINGS",
				&(AeronUiWindowDesc){.width_ref = 980,
						     .height_ref = 1000,
						     .centered = 1})) {
		AeronUi_BeginTabBar(g_menu.ui, "pages", pages, 5, &g_menu.page);
		if (g_menu.page != 2) {
			xvt_controller_settings_cancel_capture(
				&g_menu.controller, g_menu.ui);
			g_menu.controller.editor.binding_modal_open = 0;
			g_menu.controller.restore_modal_open = 0;
		}
		if (g_menu.page != 3) {
			xvt_keyboard_settings_cancel_capture(&g_menu.keyboard,
							     g_menu.ui);
		}
		if (g_menu.pages[g_menu.page]) {
			g_menu.pages[g_menu.page](g_menu.ui, input);
		}
		AeronUi_EndTabBar(g_menu.ui);
		if (xvt_port_network_requires_progress()) {
			AeronUi_Help(
				g_menu.ui,
				"Multiplayer continues while settings are open.");
		}
		if (g_menu.error[0]) {
			AeronUi_Error(g_menu.ui, g_menu.error);
		}
		AeronUi_Separator(g_menu.ui);
		if (g_menu.page == 0) {
			AeronUi_BeginColumns(g_menu.ui, 2, NULL);
			if (AeronUi_Button(g_menu.ui, "Exit Game")) {
				g_menu.exit_confirmation_open = 1;
			}
			AeronUi_NextColumn(g_menu.ui);
			if (AeronUi_Button(g_menu.ui, "Close")) {
				xvt_settings_menu_request_close();
			}
			AeronUi_EndColumns(g_menu.ui);
		} else if (AeronUi_Button(g_menu.ui, "Close")) {
			xvt_settings_menu_request_close();
		}
		if (g_menu.page == 2) {
			xvt_controller_settings_draw_modals(
				&g_menu.controller, g_menu.ui, input,
				xvt_config_settings());
		}
		if (g_menu.page == 3) {
			xvt_keyboard_settings_draw_modals(&g_menu.keyboard,
							  g_menu.ui);
		}
		if (g_menu.exit_confirmation_open) {
			xvt_settings_menu_draw_exit_confirmation(g_menu.ui);
		}
		AeronUi_EndWindow(g_menu.ui);
	}
	xvt_installation_page_draw_picker(g_menu.ui);
	AeronUiOutput output = AeronUi_EndFrame(g_menu.ui);
	g_menu.capture_owns_frame = output.capture_all != 0;
	if (output.cancel_pressed) {
		xvt_settings_menu_request_close();
	}
	AeronUi_Submit(g_menu.ui);
}

static bool xvt_settings_menu_poll_start_press(const AeronInputSnapshot *input)
{
	if (!input) {
		memset(g_menu.start, 0, sizeof g_menu.start);
		return false;
	}
	const struct xvt_controller_options *options =
		xvt_controller_mapping_options();
	bool pressed = false;
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		const AeronControllerSnapshot *device = &input->controllers[i];
		int model = device->connected
				    ? xvt_controller_options_find_model(
					      options, device->guid)
				    : -1;
		bool eligible = model >= 0 &&
				options->models[model].kind == device->kind &&
				device->kind == AERON_CONTROLLER_KIND_GAMEPAD;
		uint32_t instance = eligible ? device->instance_id : 0;
		bool down = eligible && (device->gamepad_buttons &
					 (1u << AERON_GAMEPAD_BUTTON_START));
		if (instance != g_menu.start[i].instance || !input->has_focus) {
			g_menu.start[i].instance = instance;
			g_menu.start[i].armed = !down;
		} else if (!down) {
			g_menu.start[i].armed = true;
		} else if (g_menu.start[i].armed && !g_menu.start[i].down) {
			pressed = true;
		}
		g_menu.start[i].down = down;
	}
	return pressed;
}

bool xvt_settings_menu_begin_frame(const AeronInputSnapshot *input)
{
	bool was_open = g_menu.open;
	if (g_menu.open) {
		xvt_controller_settings_discover(
			&g_menu.controller, g_menu.ui, input,
			&xvt_config_default_settings()->gamepad_defaults);
	}
	xvt_controller_mapping_apply_pending();
	char apply_error[1024];
	if (!xvt_video_options_apply_pending(apply_error, sizeof apply_error)) {
		xvt_settings_menu_report_error(apply_error);
	}
	if (g_menu.close_requested) {
		char error[1024];
		if (xvt_video_options_flush(false, error, sizeof error) &&
		    xvt_controller_settings_commit(&g_menu.controller, error,
						   sizeof error) &&
		    xvt_keyboard_settings_commit(&g_menu.keyboard, error,
						 sizeof error) &&
		    xvt_installation_page_flush(error, sizeof error) &&
		    xvt_config_save(error, sizeof error)) {
			xvt_settings_menu_complete_close();
		} else {
			xvt_settings_menu_report_error(error);
		}
	}
	bool opened = false;
	bool start_pressed = xvt_settings_menu_poll_start_press(input);
	if (input && input->has_focus && !xvt_dialog_is_active()) {

		if (!g_menu.close_requested && start_pressed &&
		    !g_menu.capture_owns_frame &&
		    !xvt_settings_menu_captures_keyboard() &&
		    !xvt_installation_page_picker_open() &&
		    (g_menu.open || !xvt_flight_task_is_active())) {
			if (g_menu.open) {
				xvt_settings_menu_request_close();
			} else if (!was_open) {
				xvt_settings_menu_show();
				opened = true;
			}
		}
	}
	if (input && input->has_focus && !was_open && !g_menu.open &&
	    xvt_keyboard_mapping_find_shortcut_press(
		    input, XVT_KEYBOARD_SHORTCUT_SETTINGS) >= 0) {
		xvt_settings_menu_show();
		opened = true;
	}
	xvt_input_begin_capture_frame(
		input, was_open || g_menu.open || !input || !input->has_focus);
	xvt_controller_mapping_update(input);
	if (was_open && !g_menu.open) {
		Aeron_SetHostCursorVisible(!xvt_flight_task_is_active());
	}
	return opened;
}

bool xvt_settings_menu_consume_runtime_request(void)
{
	if (!xvt_port_consume_settings_request()) {
		return false;
	}
	bool was_open = g_menu.open;
	xvt_settings_menu_show();
	return !was_open && g_menu.open;
}
