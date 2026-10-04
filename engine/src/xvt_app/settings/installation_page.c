#include "xvt_app/settings/installation_page.h"

#include <stdio.h>
#include <string.h>

#include "aeron/scene/ui_file_picker.h"
#include "xvt_app/settings/settings.h"
#include "xvt_app/setup_ui.h"
#include "xvt_runtime/config/config.h"
static AeronUiFilePicker *g_picker;
static char g_edited_path[XVT_PATH_CAPACITY], g_accepted[XVT_PATH_CAPACITY];

bool xvt_installation_page_init(char *error, size_t capacity)
{
	g_picker = AeronUiFilePicker_Create();
	if (!g_picker) {
		snprintf(error, capacity, "Cannot create installation picker");
	}
	return g_picker != NULL;
}

void xvt_installation_page_shutdown(void)
{
	AeronUiFilePicker_Destroy(g_picker);
	g_picker = NULL;
}

void xvt_installation_page_open(void)
{
	snprintf(g_accepted, sizeof g_accepted, "%s",
		 xvt_config_settings()->game_data[0]
			 ? xvt_config_settings()->game_data
			 : xvt_setup_installation());
	snprintf(g_edited_path, sizeof g_edited_path, "%s", g_accepted);
}

void xvt_installation_page_cancel_picker(void)
{
	AeronUiFilePicker_Cancel(g_picker);
}

bool xvt_installation_page_picker_open(void)
{
	return AeronUiFilePicker_IsOpen(g_picker) != 0;
}

void xvt_installation_page_draw(AeronUiContext *ui,
				const AeronInputSnapshot *input)
{
	(void)input;
	AeronUi_Header(ui, "Original Installation");
	uint32_t result = AeronUi_InputTextWithAction(
		ui, "XvT", g_edited_path, sizeof g_edited_path, 0, "Browse...");
	if (result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED) {
		char error[1024];
		if (!xvt_setup_ui_open_picker(g_picker, g_edited_path, error,
					      sizeof error)) {
			xvt_settings_menu_report_error(error);
		}
	}
	if (strcmp(g_edited_path, xvt_setup_installation())) {
		char message[XVT_PATH_CAPACITY + 100];
		snprintf(message, sizeof message, "Current installation: %s",
			 xvt_setup_installation());
		AeronUi_Help(ui, message);
		AeronUi_Help(ui, "Requires restart.");
	}
}

void xvt_installation_page_draw_picker(AeronUiContext *ui)
{
	char selected[XVT_PATH_CAPACITY], error[1024];
	AeronUiFilePickerResult result = AeronUiFilePicker_Draw(
		g_picker, ui, selected, sizeof selected, error, sizeof error);
	if (result == AERON_UI_FILE_PICKER_SELECTED) {
		if (!xvt_setup_resolve_installation(selected, g_edited_path,
						    sizeof g_edited_path, error,
						    sizeof error)) {
			xvt_settings_menu_report_error(error);
		}
	} else if (result == AERON_UI_FILE_PICKER_ERROR) {
		xvt_settings_menu_report_error(error);
	}
}

bool xvt_installation_page_flush(char *error, size_t capacity)
{
	if (!strcmp(g_edited_path, g_accepted)) {
		return true;
	}
	if (!xvt_setup_resolve_installation(g_edited_path, g_edited_path,
					    sizeof g_edited_path, error,
					    capacity) ||
	    !xvt_config_set_game_data(g_edited_path, 0, error, capacity)) {
		return false;
	}
	snprintf(g_accepted, sizeof g_accepted, "%s", g_edited_path);
	return true;
}
