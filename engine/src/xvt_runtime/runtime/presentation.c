#include "xvt_runtime/runtime/presentation.h"

#include "aeron/compat/host.h"
#include "xvt_runtime/runtime/movie_task.h"

static AeronRectI g_logical_rect = {0, 0, XVT_CLASSIC_WIDTH,
				    XVT_CLASSIC_HEIGHT};

static AeronDx5Rect classic_rect(void *context, int width, int height)
{
	AeronRectI rect = xvt_presentation_classic_rect();
	(void)context;
	(void)width;
	(void)height;
	return (AeronDx5Rect){rect.x, rect.y, rect.width, rect.height};
}

void xvt_presentation_sync_to_window(int width, int height)
{
	if (width <= 0 || height <= 0) {
		return;
	}
	int w = (int)(((int64_t)480 * width + height / 2) / height) & ~1;
	if (w < 640) {
		w = 640;
	}
	if (w > 32 * 480 / 9) {
		w = (32 * 480 / 9) & ~1;
	}
	if (g_logical_rect.width != w) {
		g_logical_rect.width = w;
		Aeron_SetLogicalSize(w, 480);
	}
}

AeronRectI xvt_presentation_logical_rect(void) { return g_logical_rect; }

AeronRectI xvt_presentation_classic_rect(void)
{
	return (AeronRectI){(g_logical_rect.width - 640) / 2, 0, 640, 480};
}

AeronRectI xvt_presentation_from_classic(AeronRectI r)
{
	r.x += xvt_presentation_classic_rect().x;
	return r;
}

int xvt_presentation_mouse_to_classic(const AeronInputSnapshot *in, int *x,
				      int *y)
{
	if (!in || in->window_width <= 0 || in->window_height <= 0) {
		return 0;
	}
	/* Raw window coordinates avoid using last frame's logical width on resize. */
	int w = in->window_width, h = in->window_height;
	if ((int64_t)w * 480 > (int64_t)h * 640) {
		w = h * 640 / 480;
	} else {
		h = w * 480 / 640;
	}
	if (!w || !h) {
		return 0;
	}
	int px = in->mouse.raw_x - (in->window_width - w) / 2,
	    py = in->mouse.raw_y - (in->window_height - h) / 2;
	*x = (int)((int64_t)px * 640 / w);
	*y = (int)((int64_t)py * 480 / h);
	return in->mouse.inside_content && px >= 0 && py >= 0 && px < w &&
	       py < h;
}

int xvt_presentation_warp_classic(int x, int y)
{
	return Aeron_WarpMouseLogical(x + xvt_presentation_classic_rect().x, y);
}

void xvt_presentation_require_classic(void)
{
	AeronDx5_SetClassicFlightRenderingSuppressed(0);
}

void xvt_presentation_init(void)
{
	AeronDx5Config config = {0};
	g_logical_rect = (AeronRectI){0, 0, 640, 480};
	config.presentation_rect = classic_rect;
	AeronDx5_Configure(&config);
	AeronDx5_ResetPresentationState();
}

void xvt_presentation_end_frame(int movie_presented)
{
	if (!movie_presented && !xvt_movie_task_is_active()) {
		AeronDx5_EndFrame();
	}
}

void xvt_presentation_shutdown(void)
{
	AeronDx5_SetClassicFlightRenderingSuppressed(0);
	AeronDx5_Shutdown();
	AeronDx5_Configure(NULL);
}
