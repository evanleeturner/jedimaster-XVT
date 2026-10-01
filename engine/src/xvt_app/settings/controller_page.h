#ifndef XVT_CONTROLLER_SETTINGS_H
#define XVT_CONTROLLER_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/scene/ui.h"
#include "xvt_app/settings/bindings_editor.h"
#include "xvt_runtime/config/config.h"

#define XVT_CONTROLLER_SETTINGS_ERROR_CAPACITY 512

/* The Controller page: edits a draft of the saved controller models through a device selector, the Axes
 * page (one flight axis at a time: a captured source, a live meter, Invert, Deadzone, Clear Binding) and
 * the Bindings page (the shared editor over the selected device's profile), with conflict dialogs for an
 * axis already assigned and a control already bound, and a per-model restore. Each edit is validated and
 * handed to the mapping, which installs it at the next frame; the draft is stored when the menu closes.
 * A device without a model edits the unconfigured profile, which becomes a model on its first change. */
/* original and draft are the saved models; unconfigured is the profile edited for a device with no model;
 * selected_guid and selected_instance name the device chosen in the selector, active_instance and
 * active_kind the one whose edit state is current; pending_axis, pending_axis_source, pending_digital,
 * conflicting_action and conflict_text feed the conflict dialogs; capacity_warned marks an error that
 * Discover owns and clears. */
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

/* Adds each connected gamepad without a model to the draft, with the defaults profile; when the count
 * grew, resets the edit state, marks the draft dirty and hands it to the mapping. A failure's message
 * stays in error until a later call succeeds. */
void XvtControllerSettings_Discover(XvtControllerSettings* settings, AeronUiContext* ui,
									const AeronInputSnapshot* input, const XvtControllerProfile* defaults);
/* Nothing for a NULL argument. Zeroes settings, takes config's controller models as the original and the
 * draft, clears the editor, starts at the Yaw axis and clears the unconfigured profile as a joystick's. */
void XvtControllerSettings_Open(XvtControllerSettings* settings, const XvtSettings* config);
/* Nothing for a NULL argument. Draws the Device selector over the connected controllers (a repeated name
 * gets "#<slot>"; with none, a hint, and the selection and edit state are dropped), resets the edit state
 * when the selected device or its kind changed, notes when several controllers share the model and which
 * one supplies analog input, and warns of configured controls the device lacks or of more controls than
 * the game supports. A model saved for another device kind blocks editing until a restore. Otherwise the
 * Axes or Bindings page follows (switching cancels capture and closes the detail modal). The Axes page
 * selects a flight axis (Yaw, Pitch, Roll, Throttle) and edits its source by capture from the selected
 * device (a source already driving another model's same axis, or another axis of this profile, opens the
 * axis conflict dialog), shows the live input (the throttle as a lever position), and offers Invert,
 * Deadzone (not for the throttle) and Clear Binding. Then the error with Dismiss Error, and Restore
 * Controller Defaults, enabled with a device selected, which opens the restore modal. */
void XvtControllerSettings_Draw(XvtControllerSettings* settings, AeronUiContext* ui,
								const AeronInputSnapshot* input);
/* Nothing for a NULL argument. With no compatible device selected, cancels the capture and closes the
 * conflict and detail modals. Draws at most one modal: the axis conflict (Replace installs the pending
 * source on the pending axis, unbinding that axis on every other model and that source from this
 * profile's other axes); else the binding conflict (Replace rebinds the pending control to the selected
 * action); else the per-model restore (Reset to defaults clears the device's profile, a gamepad's to the
 * shipped gamepad defaults minus the axes other models drive; the modal closes by itself when the device
 * is gone); else, with a compatible device, the detail modal: the action's bindings with an Axis
 * Threshold slider (5 to 100 %) for an axis-direction binding, Remove, and Add Binding, where a control
 * bound to another action opens the conflict dialog, one bound to this action is highlighted, and a full
 * profile is refused with a message. */
void XvtControllerSettings_DrawModals(XvtControllerSettings* settings, AeronUiContext* ui,
									  const AeronInputSnapshot* input, const XvtSettings* config);
/* false with "controller settings are unavailable" for NULL settings or no loaded settings. Nothing when
 * the draft is unchanged. Stores the draft as the user's controller list (false with error on failure);
 * on success the stored list becomes the original and the draft. */
bool XvtControllerSettings_Commit(XvtControllerSettings* settings, char* error, size_t error_capacity);
/* Cancels the UI's controller capture (with a ui) and closes the axis conflict, binding conflict and
 * detail modals (with settings). */
void XvtControllerSettings_CancelCapture(XvtControllerSettings* settings, AeronUiContext* ui);

#endif
