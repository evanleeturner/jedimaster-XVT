#ifndef XVT_KEYBOARD_CONFIG_H
#define XVT_KEYBOARD_CONFIG_H
#include "aeron/config_file.h"
#include "xvt_runtime/input/keyboard_mapping.h"

/* Keyboard bindings in the settings document: input.keyboard maps each action name to a key name, or
 * to a list of sources, each {key, modifiers}, the modifiers from shift, ctrl, alt and gui. Error
 * messages here name the problem but not the file or line. */

/* Reads input.keyboard into *profile, sorted. Among other checks, it fails for an action that cannot take
 * a key, an unknown key or source field, a repeated modifier, a reserved or unsupported source, a source
 * bound twice, or more than XVT_KEYBOARD_BINDING_CAP bindings; *profile then holds the bindings read
 * before the failure. */
bool XvtKeyboardConfig_Read(const AeronConfigFile *document,
			    XvtKeyboardBindings *profile, char *error,
			    size_t capacity);
/* Replaces input.keyboard with profile, listing every action that can take a key, those with no source
 * as empty lists, so the result replaces every default. Fails, with the reason in error->message, for
 * too many bindings or an invalid action or source. */
bool XvtKeyboardConfig_Write(AeronConfigFile *document,
			     const XvtKeyboardBindings *profile,
			     AeronConfigError *error);
/* Resolve user precedence into the temporary merged document without changing user overrides. */
/* When user has input.keyboard, writes merged's input.keyboard as the user's bindings plus each
 * default whose action the user does not list and whose source the user has not bound; an action the
 * user lists, even as an empty list, loses its defaults. Without user bindings, changes nothing. */
bool XvtKeyboardConfig_Resolve(const XvtKeyboardBindings *defaults,
			       const AeronConfigFile *user,
			       AeronConfigFile *merged, char *error,
			       size_t capacity);
#endif
