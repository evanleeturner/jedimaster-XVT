#ifndef XVT_RUNTIME_INPUT_CONTROLLER_OPTIONS_H
#define XVT_RUNTIME_INPUT_CONTROLLER_OPTIONS_H
#include "aeron/input.h"
#include "xvt_runtime/input/actions.h"
#include <stddef.h>

/* Saved controller settings: up to XVT_CONTROLLER_MODEL_CAP models, each a controller model by GUID
 * with its kind (gamepad or joystick), four flight axes and its digital bindings. An axis source of -1
 * is unbound; a flight axis may be driven by only one model. */

typedef enum XvtInputAxis {
	XVT_INPUT_AXIS_YAW,
	XVT_INPUT_AXIS_PITCH,
	XVT_INPUT_AXIS_ROLL,
	XVT_INPUT_AXIS_THROTTLE,
	XVT_INPUT_AXIS_COUNT
} XvtInputAxis;

typedef struct XvtInputAxisBinding {
	int8_t source;
	bool invert;
	float deadzone;
} XvtInputAxisBinding;

typedef struct XvtInputMapping {
	XvtInputAxisBinding axes[XVT_INPUT_AXIS_COUNT];
} XvtInputMapping;

enum {
	XVT_CONTROLLER_BINDING_CAP =
		AERON_CONTROLLER_BUTTON_MAX + 2 * AERON_CONTROLLER_AXIS_MAX + 4 * AERON_CONTROLLER_HAT_MAX
};

typedef struct XvtInputActionBinding {
	AeronControllerDigitalSource source;
	XvtInputAction action;
} XvtInputActionBinding;

typedef struct XvtControllerProfile {
	XvtInputMapping mapping;
	XvtInputActionBinding bindings[XVT_CONTROLLER_BINDING_CAP];
	size_t binding_count;
} XvtControllerProfile;

enum { XVT_CONTROLLER_MODEL_CAP = 8 };

typedef struct XvtControllerModel {
	char guid[33];
	char name[AERON_CONTROLLER_NAME_CAPACITY];
	AeronControllerKind kind; /* Saved Aeron kind; must match the connected snapshot. */
	XvtControllerProfile profile;
} XvtControllerModel;

typedef struct XvtControllerOptions {
	XvtControllerModel models[XVT_CONTROLLER_MODEL_CAP];
	size_t count;
} XvtControllerOptions;

/* true when both list the same models in the same order; two NULLs are equal. */
bool XvtControllerOptions_Equals(const XvtControllerOptions* left, const XvtControllerOptions* right);
/* Checks a profile for a gamepad or joystick: axis sources -1 or within the kind's axis count and not
 * shared, deadzones 0 to 1 (throttle 0), at most XVT_CONTROLLER_BINDING_CAP bindings, each with an
 * action, a threshold in (0, 1], an index within the kind's limits, a hat only on a joystick, and no
 * source bound twice. Writes a plain message to error on failure. */
bool XvtControllerOptions_ValidateProfile(const XvtControllerProfile* profile, AeronControllerKind kind,
										  char* error, size_t capacity);
/* invert, flipped for a gamepad's pitch axis. */
bool XvtControllerOptions_EffectiveAxisInvert(AeronControllerKind kind, XvtInputAxis axis, bool invert);
/* true when the axes match and the bindings match in the same order, thresholds included. */
bool XvtControllerOptions_ProfileEqual(const XvtControllerProfile* left, const XvtControllerProfile* right);
/* Zeroes the profile and unbinds every axis; a joystick's throttle starts inverted. */
void XvtControllerOptions_ClearProfile(XvtControllerProfile* profile, AeronControllerKind kind);
/* The index of the model with exactly that GUID, or -1. */
int XvtControllerOptions_FindModel(const XvtControllerOptions* options, const char* guid);
/* Checks every model: a GUID of 32 lower-case hex digits, not all zero and not repeated; a terminated
 * name; a valid profile; and no flight axis driven by two models. */
bool XvtControllerOptions_Validate(const XvtControllerOptions* options, char* error, size_t capacity);
/* Appends a model for device with a cleared profile; true with nothing added when its GUID is already
 * listed. Fails for a device without a valid GUID or when the list is full. */
bool XvtControllerOptions_EnsureModel(XvtControllerOptions* options, const AeronControllerSnapshot* device,
									  char* error, size_t capacity);
/* Adds each connected gamepad in input that has no model yet, in GUID order, with the defaults profile,
 * leaving unbound any axis an earlier model already drives. Fails only when a model cannot be added;
 * true for NULL input. */
bool XvtControllerOptions_AddNewGamepads(XvtControllerOptions* options, const XvtControllerProfile* defaults,
										 const AeronInputSnapshot* input, char* error, size_t capacity);
#endif
