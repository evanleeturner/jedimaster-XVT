#ifndef XVT_RUNTIME_COMPAT_FRAMEBUFFER_ADDRESS_H
#define XVT_RUNTIME_COMPAT_FRAMEBUFFER_ADDRESS_H

#include <stdint.h>
#include <string.h>

/* How the software flight renderer addresses its framebuffer: always as linear
 * memory. (The 1997 game could also draw through the banked VGA window at
 * 0xA0000, a page at a time.) */
typedef uint8_t *xvt_framebuffer_address;

/* Modern surfaces are always linear, so the banked VGA aperture never applies. */
static __inline int xvt_framebuffer_address_is_legacy_base(const uint8_t *base)
{
	(void)base;
	return 0;
}

/* offset bytes past base. (The 1997 game cleared the lowest bit of base first.) */
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

#endif
