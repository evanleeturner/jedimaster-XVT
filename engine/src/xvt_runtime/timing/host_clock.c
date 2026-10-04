#include "xvt_runtime/timing/host_clock.h"

#include "xvt/util/time.h"

static uint64_t g_xvt_elapsed_us;

void xvt_time_reset(void) { g_xvt_elapsed_us = 0; }

void xvt_time_advance_host_clock(int32_t delta_us)
{
	if (delta_us > 0) {
		g_xvt_elapsed_us += (uint32_t)delta_us;
	}
}

uint64_t xvt_time_get_elapsed_us(void) { return g_xvt_elapsed_us; }

uint32_t xvt_time_get_elapsed_ms(void)
{
	return (uint32_t)(g_xvt_elapsed_us / 1000u);
}

/* drift-ok: camelcase -- Windows' name; the 1997 code calls it */
uint32_t timeGetTime(void) { return xvt_time_get_elapsed_ms(); }

/* drift-ok: camelcase -- Windows' name; the 1997 code calls it */
uint32_t GetTickCount(void) { return xvt_time_get_elapsed_ms(); }
