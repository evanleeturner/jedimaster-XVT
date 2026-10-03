#ifndef XVT_RUNTIME_COMPAT_POINTER_KEY_H
#define XVT_RUNTIME_COMPAT_POINTER_KEY_H

#include <stdint.h>

/* The original 32-bit build hashed cache entries straight from the pointer value.
   Truncating to the low 32 bits keeps that hash well defined on 64-bit hosts. */
static __inline int xvt_pointer_key_low_bits(const void *pointer)
{
	return (int)(uint32_t)(uintptr_t)pointer;
}

#endif
