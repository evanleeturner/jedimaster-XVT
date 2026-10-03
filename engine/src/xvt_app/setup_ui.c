#include "xvt_app/setup_ui.h"

#include "xvt_runtime/config/config.h"
#include <stdio.h>

typedef enum xvt_setup_frame_result {
	XVT_SETUP_FRAME_PENDING,
	XVT_SETUP_FRAME_SUCCESS,
	XVT_SETUP_FRAME_CANCELLED,
} xvt_setup_frame_result;

static int xvt_setup_ui_accept_installation(const char *path, void *user,
					    char *error, size_t capacity)
{
	(void)user;
	char resolved[XVT_PATH_CAPACITY];
	return xvt_setup_resolve_installation(path, resolved, sizeof resolved,
					      error, capacity);
}

int xvt_setup_ui_open_picker(AeronUiFilePicker *picker, const char *path,
			     char *error, size_t capacity)
{
	const AeronUiFilePickerDesc desc = {
		.mode = AERON_UI_FILE_PICKER_SELECT_DIRECTORY,
		.title = "SELECT XvT INSTALLATION",
		.instructions =
			"Select the XvT installation folder or its BalanceOfPower folder.",
		.accept_label = "Use This Folder",
		.cancel_label = "Cancel",
		.initial_path = path && path[0] ? path : NULL,
		.accept_fn = xvt_setup_ui_accept_installation,
	};
	return AeronUiFilePicker_Open(picker, &desc, error, capacity);
}

static xvt_setup_frame_result
xvt_setup_ui_draw_recovery(AeronUiContext *ui, char *error, size_t capacity)
{
	xvt_setup_frame_result result = XVT_SETUP_FRAME_PENDING;
	const AeronUiWindowDesc window = {.width_ref = 920.0f, .centered = 1};
	if (!AeronUi_BeginWindow(ui, "OpenXvT configuration", &window)) {
		return result;
	}
	AeronUi_Help(ui, "The saved configuration could not be loaded.");
	AeronUi_Error(ui, error);
	AeronUi_Separator(ui);
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Keep file and quit")) {
		result = XVT_SETUP_FRAME_CANCELLED;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Reset to defaults") &&
	    xvt_config_reset_to_defaults(error, capacity)) {
		result = XVT_SETUP_FRAME_SUCCESS;
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndWindow(ui);
	return result;
}

static xvt_setup_frame_result
xvt_setup_ui_draw_installation(AeronUiContext *ui, AeronUiFilePicker *picker,
			       char *path, size_t path_capacity, int *valid,
			       char *error, size_t capacity)
{
	xvt_setup_frame_result result = XVT_SETUP_FRAME_PENDING;
	const AeronUiWindowDesc window = {.width_ref = 920.0f, .centered = 1};
	if (!AeronUi_BeginWindow(ui, "Welcome to OpenXvT", &window)) {
		return result;
	}
	AeronUi_Help(
		ui,
		"Select your original X-Wing vs. TIE Fighter installation with Balance of Power.");
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Original Installation");
	uint32_t path_result = AeronUi_InputTextWithAction(
		ui, "XvT", path, path_capacity, AERON_UI_INPUT_TEXT_READ_ONLY,
		"Browse...");
	if (path_result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED) {
		xvt_setup_ui_open_picker(picker, path, error, capacity);
	}
	AeronUi_Help(
		ui,
		"Choose the XvT installation folder or its BalanceOfPower folder.");
	if (*valid) {
		AeronUi_Help(ui, "Installation validated.");
	}
	if (error[0]) {
		AeronUi_Error(ui, error);
	}
	AeronUi_Separator(ui);
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Quit")) {
		result = XVT_SETUP_FRAME_CANCELLED;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_ButtonEnabled(ui, "Continue", *valid)) {
		*valid = xvt_setup_resolve_installation(
			path, path, path_capacity, error, capacity);
		if (*valid &&
		    xvt_config_set_game_data(path, 1, error, capacity)) {
			result = XVT_SETUP_FRAME_SUCCESS;
		}
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndWindow(ui);
	return result;
}

xvt_setup_result xvt_setup_ui_run(struct xvt_app_ui *ui, char *path,
				  size_t path_capacity, char *error,
				  size_t capacity)
{
	AeronUiContext *context = xvt_app_ui_context(ui);
	AeronUiFilePicker *picker = path ? AeronUiFilePicker_Create() : NULL;
	if (path && !picker) {
		snprintf(error, capacity, "Cannot create installation picker.");
		return XVT_SETUP_ERROR;
	}
	int valid = path && path[0] &&
		    xvt_setup_resolve_installation(path, path, path_capacity,
						   error, capacity);
	xvt_setup_result outcome = XVT_SETUP_ERROR;
	Aeron_SetHostCursorVisible(1);
	while (!Aeron_QuitRequested() && !Aeron_FatalErrorRequested()) {
		const int32_t delta_us = Aeron_BeginFrame();
		if (Aeron_QuitRequested() || Aeron_FatalErrorRequested()) {
			break;
		}
		AeronUi_BeginFrame(
			context, &(AeronUiFrameDesc){
					 .input = Aeron_InputSnapshot(),
					 .dt_seconds = (float)delta_us * 1e-6f,
				 });
		const xvt_setup_frame_result frame_result =
			path ? xvt_setup_ui_draw_installation(
				       context, picker, path, path_capacity,
				       &valid, error, capacity)
			     : xvt_setup_ui_draw_recovery(context, error,
							  capacity);
		if (picker) {
			const AeronUiFilePickerResult picked =
				AeronUiFilePicker_Draw(picker, context, path,
						       path_capacity, error,
						       capacity);
			if (picked == AERON_UI_FILE_PICKER_SELECTED) {
				valid = xvt_setup_resolve_installation(
					path, path, path_capacity, error,
					capacity);
			}
		}
		const AeronUiOutput output = AeronUi_EndFrame(context);
		if (!AeronUi_Submit(context) || !Aeron_Present()) {
			snprintf(
				error, capacity,
				"Could not render the installation setup dialog.");
			break;
		}
		if (frame_result != XVT_SETUP_FRAME_PENDING) {
			outcome = frame_result == XVT_SETUP_FRAME_SUCCESS
					  ? XVT_SETUP_SUCCESS
					  : XVT_SETUP_CANCELLED;
			break;
		}
		if (output.cancel_pressed) {
			outcome = XVT_SETUP_CANCELLED;
			break;
		}
		Aeron_WaitForNextFrame(16667);
	}
	if (Aeron_FatalErrorRequested()) {
		snprintf(error, capacity,
			 "Setup interrupted by a fatal host error.");
		outcome = XVT_SETUP_ERROR;
	} else if (Aeron_QuitRequested()) {
		outcome = XVT_SETUP_CANCELLED;
	}
	AeronUiFilePicker_Destroy(picker);
	if (outcome == XVT_SETUP_SUCCESS) {
		error[0] = 0;
	}
	return outcome;
}
