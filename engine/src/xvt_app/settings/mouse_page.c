#include "xvt_app/settings/mouse_page.h"
#include "xvt_runtime/config/config.h"

void XvtMousePage_Draw(AeronUiContext* ui, const AeronInputSnapshot* input) {
	(void)input;
	XvtMouseOptions options = XvtConfig_Settings()->mouse;
	AeronUi_Header(ui, "Mouse Flight Control");
	bool changed = AeronUi_Toggle(ui, "Mouse Flight Control", &options.mouse_flight_enabled);
	changed |= AeronUi_SliderInt(ui, "Mouse Sensitivity", &options.mouse_sensitivity, 1, 9, 1, "%d");
	changed |= AeronUi_Toggle(ui, "Invert Mouse Y", &options.mouse_invert_y);
	AeronUi_Help(ui, "Mouse up pitches up; with Invert Mouse Y enabled, mouse up pitches down.");
	AeronUi_Help(ui, "Left button: fire.");
	AeronUi_Help(ui, "Right button: tap to target under crosshair, hold to roll.");
	AeronUi_Help(ui, "Middle button: target nearest fighter or mine.");
	AeronUi_Help(ui, "Side button: toggle cockpit.");
	AeronUi_Help(ui, "Ctrl+Alt+M releases or captures the pointer.");
	if (AeronUi_Button(ui, "Restore Defaults")) {
		options = XvtConfig_DefaultSettings()->mouse;
		changed = true;
	}
	if (changed) {
		char error[512];
		if (!XvtConfig_SetMouse(&options, error, sizeof error))
			XvtSettingsMenu_ReportError(error);
	}
}
