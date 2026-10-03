#ifndef XVT_RUNTIME_PRESENTATION_H
#define XVT_RUNTIME_PRESENTATION_H
#include "aeron/aeron.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XVT_CLASSIC_WIDTH 640
#define XVT_CLASSIC_HEIGHT 480

/* The logical frame the game presents into: 480 high, as wide as the window's aspect allows
 * between 640 and 1706 (32:9), with the classic 640x480 image centered horizontally. */

/* Resets the frame to 640x480 and configures Aeron's DirectX 5 layer to present into the
 * centered classic rectangle. */
void xvt_presentation_init(void);
/* Sets the frame width to 480 * width / height, rounded, made even and clamped to 640 through
 * 1706, and tells Aeron when it changes. Ignored for a non-positive size. */
void xvt_presentation_sync_to_window(int width, int height);
/* The current logical frame. */
AeronRectI xvt_presentation_logical_rect(void);
/* The 640x480 classic rectangle, centered horizontally in the frame. */
AeronRectI xvt_presentation_classic_rect(void);
/* rect moved from classic coordinates into frame coordinates. */
AeronRectI xvt_presentation_from_classic(AeronRectI rect);
/* Maps the raw window mouse position into classic coordinates through the largest 4:3 area
 * centered in the window. Returns 1 when the mouse is inside the content and that area; x and y
 * are written whenever the window size is valid, even outside it. Returns 0 without writing for
 * NULL input or a window too small to map. */
int xvt_presentation_mouse_to_classic(const AeronInputSnapshot *input, int *x,
				      int *y);
/* Moves the mouse to classic point x, y; returns Aeron's result. */
int xvt_presentation_warp_classic(int x, int y);
/* Called before standalone UI, surface replacement and task teardown draws. */
/* Lifts the suppression of classic flight rendering. */
void xvt_presentation_require_classic(void);
/* The movie owns this host frame even if playback ends during its tick. */
/* Ends the DirectX 5 frame unless a movie presented it or is still active. */
void xvt_presentation_end_frame(int movie_presented);
/* Lifts the flight rendering suppression, shuts down the DirectX 5 layer and removes its
 * configuration. */
void xvt_presentation_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
