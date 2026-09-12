#ifndef XVT_CONTROLLER_SETTINGS_H
#define XVT_CONTROLLER_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/scene/ui.h"
#include "xvt_app/settings/bindings_editor.h"
#include "xvt_runtime/config/config.h"

#define XVT_CONTROLLER_SETTINGS_ERROR_CAPACITY 512

typedef struct XvtControllerSettings {
	XvtBindingsEditor editor;
	XvtControllerOptions original;
	XvtControllerOptions draft;
	XvtControllerProfile unconfigured;
	char selected_guid[33];
	uint32_t selected_instance;
	AeronControllerKind active_kind;
	char conflict_text[512];
	bool capacity_warned;
	int page;
	int axis;
	XvtInputAxis pending_axis;
	int pending_axis_source;
	AeronControllerDigitalSource pending_digital;
	XvtInputAction conflicting_action;
	uint32_t active_instance;
	int axis_conflict_open;
	int binding_conflict_open;
	int restore_modal_open;
	bool dirty;
	char error[XVT_CONTROLLER_SETTINGS_ERROR_CAPACITY];
} XvtControllerSettings;

void XvtControllerSettings_Discover(XvtControllerSettings* settings, AeronUiContext* ui,
									const AeronInputSnapshot* input, const XvtControllerProfile* defaults);
void XvtControllerSettings_Open(XvtControllerSettings* settings, const XvtSettings* config);
void XvtControllerSettings_Draw(XvtControllerSettings* settings, AeronUiContext* ui,
								const AeronInputSnapshot* input);
void XvtControllerSettings_DrawModals(XvtControllerSettings* settings, AeronUiContext* ui,
									  const AeronInputSnapshot* input, const XvtSettings* config);
bool XvtControllerSettings_Commit(XvtControllerSettings* settings, char* error, size_t error_capacity);
void XvtControllerSettings_CancelCapture(XvtControllerSettings* settings, AeronUiContext* ui);

#endif
