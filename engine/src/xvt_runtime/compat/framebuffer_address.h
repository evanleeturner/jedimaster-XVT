#ifndef XVT_RUNTIME_COMPAT_FRAMEBUFFER_ADDRESS_H
#define XVT_RUNTIME_COMPAT_FRAMEBUFFER_ADDRESS_H

#include <stdint.h>
#include <string.h>

/* How the software flight renderer addresses its framebuffer. The original build could draw through
 * the banked VGA window at 0xA0000, a page at a time; the XVT_MODERN arm always draws into linear
 * memory. */
#ifdef XVT_MODERN
typedef uint8_t *xvt_framebuffer_address;

/* Modern surfaces are always linear, so the banked VGA aperture never applies. */
static __inline int xvt_framebuffer_address_is_legacy_base(const uint8_t *base)
{
	(void)base;
	return 0;
}

/* offset bytes past base. The original arm clears the lowest bit of base first; this one does not. */
static __inline xvt_framebuffer_address
xvt_framebuffer_address_from_base(uint8_t *base, unsigned int offset)
{
	return base + offset;
}

/* Stores color at *address as 2 bytes in host order, with no alignment requirement. The address is
 * not advanced; callers step it themselves. */
static __inline void
xvt_framebuffer_address_store16(xvt_framebuffer_address *address,
				uint16_t color)
{
	memcpy(*address, &color, sizeof(color));
}
#else
typedef uint8_t *xvt_framebuffer_address;

static __inline int xvt_framebuffer_address_is_legacy_base(const uint8_t *base)
{
	return base == (uint8_t *)(uintptr_t)0xA0000;
}

static __inline xvt_framebuffer_address
xvt_framebuffer_address_from_base(uint8_t *base, unsigned int offset)
{
	return (uint8_t *)((uintptr_t)base & ~(uintptr_t)1) + offset;
}

static __inline void
xvt_framebuffer_address_store16(xvt_framebuffer_address *address,
				uint16_t color)
{
	*(uint16_t *)*address = color;
}
#endif

#endif
