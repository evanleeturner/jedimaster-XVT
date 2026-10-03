#ifndef XVT_RUNTIME_COMPAT_WIN_MESSAGE_PORT_H
#define XVT_RUNTIME_COMPAT_WIN_MESSAGE_PORT_H

#include <stdint.h>

/* Rebuild a pointer-valued Win32 message argument without assuming that host
 * pointers are 32 bits. The legacy value occupies the low 32 bits. */
static __inline void *xvt_port_win_message_param_as_pointer(uintptr_t value)
{
	return (void *)value;
}

#endif
