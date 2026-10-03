#ifndef XVT_RUNTIME_HOST_CLOCK_H
#define XVT_RUNTIME_HOST_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The runtime's clock. It starts at 0 and moves only through AdvanceHostClock, which the port calls
 * from its frame. The game's calls to the Windows timeGetTime and GetTickCount are answered from it;
 * both are defined in host_clock.c. */

/* Sets the clock to 0. */
void xvt_time_reset(void);
/* Adds delta_us microseconds; zero or a negative delta is ignored. */
void xvt_time_advance_host_clock(int32_t delta_us);
/* Microseconds added since the last Reset. */
uint64_t xvt_time_get_elapsed_us(void);
/* Whole milliseconds since the last Reset, cut to 32 bits, so it wraps after about 49.7 days.
 * timeGetTime and GetTickCount return this value. */
uint32_t xvt_time_get_elapsed_ms(void);

#ifdef __cplusplus
}
#endif

#endif
