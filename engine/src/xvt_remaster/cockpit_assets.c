#include "aeron/asset/lfd.h"
#include "aeron/asset/pnl.h"
#include "xvt_remaster/original_2d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int XvtCockpitAssets_DecodeLfd(const void* bytes, size_t size, XvtOriginal2d* out, AeronDecodeError* error) {
	AeronLfd lfd = { 0 };
	if (!AeronLfd_Parse(bytes, size, &lfd, error))
		return 0;
	const AeronLfdEntry* panl = AeronLfd_Find(&lfd, AERON_LFD_FOURCC('P', 'A', 'N', 'L'));
	const AeronLfdEntry* pltt = AeronLfd_Find(&lfd, AERON_LFD_FOURCC('P', 'L', 'T', 'T'));
	const AeronLfdEntry* mask = AeronLfd_Find(&lfd, AERON_LFD_FOURCC('M', 'A', 'S', 'K'));
	int ok = 0;
	if (!panl || !pltt || !mask || pltt->size < 2 || pltt->data[1] < pltt->data[0])
		goto failed;
	unsigned first = pltt->data[0], count = pltt->data[1] - first + 1;
	if (count * 3 > pltt->size - 2)
		goto failed;
	out->images.frames = calloc(1, sizeof *out->images.frames);
	if (!out->images.frames)
		goto failed;
	out->images.count = 1;
	out->panel_bytes = malloc(panl->size);
	if (!out->panel_bytes)
		goto failed;
	memcpy(out->panel_bytes, panl->data, panl->size);
	out->panel_size = panl->size;
	out->panel_records.bitmaps = calloc(1, sizeof *out->panel_records.bitmaps);
	if (!out->panel_records.bitmaps)
		goto failed;
	out->panel_records.count = 1;
	out->panel_records.bitmaps[0] = (AeronByteSpan) { out->panel_bytes, out->panel_size };
	out->cockpit_mask = malloc(mask->size ? mask->size : 1);
	if (!out->cockpit_mask)
		goto failed;
	memcpy(out->cockpit_mask, mask->data, mask->size);
	out->cockpit_mask_size = mask->size;
	if (!AeronPnl_Decode(panl->data, panl->size, out->images.frames, error))
		goto done;
	AeronIndexedFrame* frame = out->images.frames;
	out->external_palette = 1;
	out->palette_first = (uint16_t)first;
	out->palette_count = (uint16_t)count;
	frame->palette_count = (uint16_t)(first + count);
	for (unsigned i = 0; i < count; ++i) {
		/* The recovered LFD loader converts these bytes to six-bit VGA DAC values. */
		for (unsigned c = 0; c < 3; ++c) {
			unsigned value = pltt->data[2 + i * 3 + c] >> 2;
			frame->palette[first + i][c] = (uint8_t)((value << 2) | (value >> 4));
		}
		frame->palette[first + i][3] = 255;
	}
	ok = 1;
	goto done;
failed:
	if (error) {
		error->code = 1;
		snprintf(error->message, sizeof error->message, "invalid cockpit PANL/PLTT resource");
	}
done:
	AeronLfd_Free(&lfd);
	return ok;
}

/* Raw LFD mask runs use the conversion performed by FlightSw_CopyViewportSpanMaskRle.
 * Positive parity is the world opening. Mirrored views mirror the finished image. */
static int MaskRun(const uint8_t** cursor, const uint8_t* end, int screen_width) {
	if (*cursor == end)
		return -1;
	unsigned value = *(*cursor)++;
	if (value)
		return (int)value;
	if (*cursor == end)
		return -1;
	unsigned extension = *(*cursor)++;
	if (screen_width == 320)
		return 255 + (uint8_t)(extension + 1);
	if (extension == 255)
		return 511;
	if (extension)
		return 256 + (int)extension;
	if (*cursor == end)
		return -1;
	return 511 + (uint8_t)(*(*cursor)++ + 1);
}

int XvtCockpitAssets_ApplyMask(XvtOriginal2d* image, const XvtSnapRect* rect) {
	if (!image->images.count || !image->cockpit_mask || !rect)
		return 0;
	AeronIndexedFrame* frame = image->images.frames;
	if (rect->x < 0 || rect->y < 0 || rect->width < 0 || rect->height < 0 || rect->x > frame->width ||
		rect->y > frame->height || rect->width > frame->width - rect->x ||
		rect->height > frame->height - rect->y)
		return 0;
	const uint8_t *p = image->cockpit_mask, *end = p + image->cockpit_mask_size;
	for (int y = 0; y < rect->height; ++y) {
		if (p == end)
			return 1;
		int8_t state = (int8_t)*p++;
		for (int x = 0; x < rect->width;) {
			int run = MaskRun(&p, end, frame->width);
			if (run <= 0)
				return 0;
			int visible = run < rect->width - x ? run : rect->width - x;
			if (state >= 0)
				memset(frame->coverage + (size_t)(rect->y + y) * frame->width + rect->x + x, 0,
					   (size_t)visible);
			x += visible;
			state = (int8_t)-state;
		}
	}
	return 1;
}
