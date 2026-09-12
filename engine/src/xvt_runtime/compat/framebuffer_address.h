#ifndef XVT_RUNTIME_COMPAT_FRAMEBUFFER_ADDRESS_H
#define XVT_RUNTIME_COMPAT_FRAMEBUFFER_ADDRESS_H

#include <stdint.h>
#include <string.h>

#ifdef XVT_MODERN
typedef uint8_t* XvtFramebufferAddress;

/* Modern surfaces are always linear, so the banked VGA aperture never applies. */
static __inline int XvtFramebufferAddress_IsLegacyBase(uint8_t* base) {
	(void)base;
	return 0;
}

static __inline XvtFramebufferAddress XvtFramebufferAddress_FromBase(uint8_t* base, unsigned int offset) {
	return base + offset;
}

static __inline void XvtFramebufferAddress_Store16(XvtFramebufferAddress* address, uint16_t color) {
	memcpy(*address, &color, sizeof(color));
}
#else
typedef uint8_t* XvtFramebufferAddress;

static __inline int XvtFramebufferAddress_IsLegacyBase(uint8_t* base) {
	return base == (uint8_t*)(uintptr_t)0xA0000;
}

static __inline XvtFramebufferAddress XvtFramebufferAddress_FromBase(uint8_t* base, unsigned int offset) {
	return (uint8_t*)((uintptr_t)base & ~(uintptr_t)1) + offset;
}

static __inline void XvtFramebufferAddress_Store16(XvtFramebufferAddress* address, uint16_t color) {
	*(uint16_t*)*address = color;
}
#endif

#endif
