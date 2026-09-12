#include "xvt_remaster/original_2d.h"
#include "aeron/aeron.h"
#include "aeron/asset/abp_font.h"
#include "aeron/asset/act.h"
#include "aeron/asset/bitmap_font.h"
#include "aeron/asset/bmp.h"
#include "aeron/asset/pnl.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void XvtOriginal2d_Free(XvtOriginal2d* source) {
	if (!source)
		return;
	AeronIndexedFrames_Free(&source->images);
	AeronDecodedFont_Free(&source->font);
	free(source->cockpit_mask);
	AeronPnl_Free(&source->panel_records);
	free(source->panel_bytes);
	memset(source, 0, sizeof *source);
}

static int Error(char* error, size_t capacity, const char* label, const char* reason) {
	if (error && capacity)
		snprintf(error, capacity, "%s: %s", label, reason);
	return 0;
}

static int Read(const char* path, uint8_t** bytes, size_t* size, char* error, size_t capacity) {
	if (!AeronVfs_ReadAll(Aeron_GetVfs(), AERON_VFS_ROOT_ASSET, path, 64u * 1024u * 1024u, bytes, size))
		return Error(error, capacity, path, "original resource is unavailable");
	return 1;
}

static int EmptyFrame(AeronIndexedFrame* frame, uint16_t index) {
	frame->indices = calloc(1, 1);
	frame->coverage = calloc(1, 1);
	frame->width = frame->height = 1;
	frame->frame_index = index;
	return frame->indices && frame->coverage;
}

static int DecodePnl(const XvtSnapImageAsset* source, const void* bytes, size_t size, XvtOriginal2d* out,
					 AeronDecodeError* error) {
	if (!source->record_count || source->first_record > 4096 ||
		source->record_count > 4096 - source->first_record)
		return 0;
	out->panel_bytes = malloc(size);
	if (!out->panel_bytes)
		return 0;
	memcpy(out->panel_bytes, bytes, size);
	out->panel_size = size;
	out->external_palette = 1;
	if (!AeronPnl_Parse(out->panel_bytes, size, source->first_record + source->record_count,
						&out->panel_records, error))
		return 0;
	if (source->kind == XVT_IMAGE_PNL)
		return 1;
	/* Map atlas preparation needs the original record IDs; rasterization belongs
	 * to its explicit palette/key preparation, just like cockpit parts. */
	out->images.frames = calloc(source->record_count, sizeof *out->images.frames);
	if (!out->images.frames)
		return 0;
	out->images.count = (uint16_t)source->record_count;
	for (unsigned index = 0; index < source->record_count; ++index) {
		out->images.frames[index].frame_index = (uint16_t)(source->first_record + index);
		unsigned record = source->first_record + index;
		if (record >= out->panel_records.count)
			continue;
		AeronByteSpan bytes = out->panel_records.bitmaps[record];
		if (bytes.size == 1 && bytes.data[0] == 0xff)
			continue;
		AeronIndexedFrame bitmap = { 0 };
		if (!AeronPnl_DecodeIndexed(bytes.data, bytes.size, 0, 0, &bitmap, error))
			return 0;
		for (size_t pixel = 0; pixel < (size_t)bitmap.width * bitmap.height; ++pixel)
			if (bitmap.coverage[pixel])
				out->map_palette_used[bitmap.indices[pixel]] = 1;
		AeronIndexedFrame_Free(&bitmap);
	}
	return 1;
}

static int DecodeCursor(XvtOriginal2d* out) {
	out->images.frames = calloc(1, sizeof *out->images.frames);
	if (!out->images.frames)
		return 0;
	out->images.count = 1;
	AeronIndexedFrame* frame = out->images.frames;
	frame->indices = malloc(100);
	frame->coverage = malloc(100);
	if (!frame->indices || !frame->coverage)
		return 0;
	frame->width = frame->height = 10;
	frame->palette_count = 2;
	memset(frame->palette[0], 255, 4);
	/* The classic 16-bit cursor uses 0x001F for mask value one. */
	frame->palette[1][2] = 255;
	frame->palette[1][3] = 255;
	const uint8_t* source = XvtRenderAssets_DefaultCursor();
	for (unsigned i = 0; i < 100; ++i) {
		frame->indices[i] = source[i] == 1;
		frame->coverage[i] = source[i] ? 255 : 0;
	}
	return 1;
}

int XvtOriginal2d_BuildMapIcons(const XvtOriginal2d* source, AeronCommandBuffer* cmd,
								const uint32_t palette[256], int remap, AeronRuntimeAtlas* out) {
	unsigned count = source->images.count;
	AeronRuntimeAtlasFrame* frames = calloc(count, sizeof *frames);
	if (!frames || !count) {
		free(frames);
		return 0;
	}
	int ok = 1;
	for (unsigned index = 0; index < count && ok; ++index) {
		unsigned frame = source->images.frames[index].frame_index;
		AeronPnlBitmap bitmap = { 0 };
		AeronDecodeError error = { 0 };
		const AeronPnlList* list = &source->panel_records;
		if (frame >= list->count || (list->bitmaps[frame].size == 1 && list->bitmaps[frame].data[0] == 0xff))
			ok = EmptyFrame(&bitmap, (uint16_t)frame);
		else
			ok = AeronPnl_DecodeIndexed(list->bitmaps[frame].data, list->bitmaps[frame].size, 0, 0, &bitmap,
										&error);
		size_t pixels = (size_t)bitmap.width * bitmap.height;
		uint8_t* rgba = ok ? calloc(pixels, 4) : NULL;
		if (!rgba)
			ok = 0;
		for (size_t pixel = 0; ok && pixel < pixels; ++pixel) {
			if (!bitmap.coverage[pixel])
				continue;
			uint8_t color = (uint8_t)(bitmap.indices[pixel] + (remap ? 4 : 0));
			rgba[pixel * 4] = (uint8_t)(palette[color] >> 16);
			rgba[pixel * 4 + 1] = (uint8_t)(palette[color] >> 8);
			rgba[pixel * 4 + 2] = (uint8_t)palette[color];
			rgba[pixel * 4 + 3] = 255;
		}
		frames[index] = (AeronRuntimeAtlasFrame) { rgba, bitmap.width, bitmap.height, (int32_t)frame, 0, 0 };
		AeronIndexedFrame_Free(&bitmap);
	}
	AeronRuntimeAtlasOptions options = { .format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
										 .color_space = AERON_COLOR_SPACE_SRGB,
										 .alpha_mode = AERON_IMAGE_ALPHA_PREMULTIPLIED,
										 .generate_mips = true,
										 .debug_name = "xvt.map.icons" };
	if (ok)
		ok = Aeron_RuntimeAtlasBuild(out, cmd, frames, (int)count, &options);
	for (unsigned index = 0; index < count; ++index)
		free((void*)frames[index].rgba);
	free(frames);
	return ok;
}

int XvtOriginal2d_LoadAct(const char* path, XvtOriginal2d* out, char* error, size_t capacity) {
	memset(out, 0, sizeof *out);
	uint8_t* bytes = NULL;
	size_t size = 0;
	if (!Read(path, &bytes, &size, error, capacity))
		return 0;
	AeronDecodeError decode = { 0 };
	int ok = AeronAct_Decode(bytes, size, &out->images, &decode);
	free(bytes);
	if (!ok) {
		XvtOriginal2d_Free(out);
		return Error(error, capacity, path, decode.message);
	}
	return 1;
}

int XvtOriginal2d_Load(const XvtSnapImageAsset* source, XvtOriginal2d* out, char* error, size_t capacity) {
	memset(out, 0, sizeof *out);
	uint8_t* bytes = NULL;
	size_t size = 0;
	if (source->kind != XVT_IMAGE_BUILTIN_CURSOR && !Read(source->path, &bytes, &size, error, capacity))
		return 0;
	AeronDecodeError decode = { 0 };
	int ok = 0;
	switch (source->kind) {
		case XVT_IMAGE_BMP:
			/* Frontend preparation supplies the asset's captured LUT or its explicit tint variant. */
			out->external_palette = 1;
			out->images.frames = calloc(1, sizeof *out->images.frames);
			if (out->images.frames) {
				out->images.count = 1;
				ok = AeronBmp_Decode(bytes, size, out->images.frames, &decode);
			}
			break;
		case XVT_IMAGE_LFD:
			ok = XvtCockpitAssets_DecodeLfd(bytes, size, out, &decode);
			break;
		case XVT_IMAGE_PNL:
		case XVT_IMAGE_ICO:
			ok = DecodePnl(source, bytes, size, out, &decode);
			break;
		case XVT_IMAGE_ABP:
			ok = AeronAbpFont_Decode(bytes, size, &out->font, &decode);
			/* XvT's ABP blitter fills every covered run with the resolved color. */
			if (ok)
				for (size_t i = 0; i < (size_t)out->font.width * out->font.height; ++i)
					out->font.foreground[i] = out->font.foreground[i] ? 255 : 0;
			break;
		case XVT_IMAGE_MICRO_FNT:
			ok = AeronBitmapFont_Decode(bytes, size, source->font_row_bytes, 32, &out->font, &decode);
			break;
		case XVT_IMAGE_BUILTIN_CURSOR:
			ok = DecodeCursor(out);
			break;
	}
	free(bytes);
	if (!ok) {
		XvtOriginal2d_Free(out);
		return Error(error, capacity, source->path,
					 decode.message[0] ? decode.message : "image decode/allocation failed");
	}
	return 1;
}

int XvtOriginal2d_BuildAtlas(const XvtOriginal2d* source, AeronCommandBuffer* cmd,
							 const uint32_t palette[256], uint16_t key, uint16_t key_alt, int generate_mips,
							 const char* label, AeronRuntimeAtlas* out) {
	unsigned count = source->images.count;
	AeronRuntimeAtlasFrame* frames = calloc(count, sizeof *frames);
	if (!frames || !count) {
		free(frames);
		return 0;
	}
	int ok = 1;
	int binary_alpha = 1;
	for (unsigned i = 0; i < count; ++i) {
		const AeronIndexedFrame* image = &source->images.frames[i];
		size_t pixels = (size_t)image->width * image->height;
		uint8_t* rgba = calloc(pixels, 4);
		if (!rgba) {
			ok = 0;
			break;
		}
		frames[i] = (AeronRuntimeAtlasFrame) {
			rgba, image->width, image->height, image->frame_index, image->anchor_x, image->anchor_y
		};
		for (size_t p = 0; p < pixels; ++p) {
			uint8_t index = image->indices[p];
			if (!image->coverage[p] || index == key || index == key_alt)
				continue;
			if (image->coverage[p] != 255)
				binary_alpha = 0;
			if (source->external_palette &&
				(index < source->palette_first || index >= source->palette_first + source->palette_count)) {
				uint32_t color = palette[index];
				rgba[p * 4] = (uint8_t)(color >> 16);
				rgba[p * 4 + 1] = (uint8_t)(color >> 8);
				rgba[p * 4 + 2] = (uint8_t)color;
			} else
				memcpy(rgba + p * 4, image->palette[index], 3);
			rgba[p * 4 + 3] = image->coverage[p];
		}
	}
	AeronRuntimeAtlasOptions options = { .format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
										 .color_space = AERON_COLOR_SPACE_SRGB,
										 .alpha_mode = binary_alpha ? AERON_IMAGE_ALPHA_PREMULTIPLIED
																	: AERON_IMAGE_ALPHA_STRAIGHT,
										 .generate_mips = generate_mips != 0,
										 .debug_name = label };
	if (ok)
		ok = Aeron_RuntimeAtlasBuild(out, cmd, frames, (int)count, &options);
	for (unsigned i = 0; i < count; ++i)
		free((void*)frames[i].rgba);
	free(frames);
	return ok;
}

int XvtOriginal2d_BuildFont(const AeronDecodedFont* source, AeronCommandBuffer* cmd, int shadow,
							const char* label, XvtFontAtlas* out) {
	enum { ORIGINAL_FONT_EXPANSION = 4 };

	const uint8_t* coverage = shadow ? source->shadow : source->foreground;
	if (!coverage)
		return 1;
	if (!source->width || !source->height || source->width > UINT16_MAX / ORIGINAL_FONT_EXPANSION ||
		source->height > UINT16_MAX / ORIGINAL_FONT_EXPANSION - 2 ||
		source->cell_width > UINT16_MAX / ORIGINAL_FONT_EXPANSION ||
		source->cell_height > UINT16_MAX / ORIGINAL_FONT_EXPANSION ||
		source->baseline > UINT16_MAX / ORIGINAL_FONT_EXPANSION)
		return 0;
	size_t pixels = (size_t)source->width * source->height;
	uint8_t* rgba = calloc(pixels + (size_t)source->width * 2, 4);
	uint8_t* expanded = NULL;
	AeronFontGlyph* atlas_glyphs = calloc(source->glyph_count, sizeof *atlas_glyphs);
	XvtFontGlyph* layout_glyphs = calloc(source->glyph_count, sizeof *layout_glyphs);
	if (!rgba || !atlas_glyphs || !layout_glyphs)
		goto failed;
	for (size_t i = 0; i < pixels; ++i)
		memset(rgba + i * 4, coverage[i], 4);
	/* A transparent guard row separates glyphs from the solid fill strip. */
	memset(rgba + (pixels + source->width) * 4, 255, (size_t)source->width * 4);
	for (unsigned i = 0; i < source->glyph_count; ++i) {
		const AeronDecodedGlyph* glyph = &source->glyphs[i];
		if (glyph->x > UINT16_MAX / ORIGINAL_FONT_EXPANSION ||
			glyph->y > UINT16_MAX / ORIGINAL_FONT_EXPANSION ||
			glyph->width > UINT16_MAX / ORIGINAL_FONT_EXPANSION ||
			glyph->height > UINT16_MAX / ORIGINAL_FONT_EXPANSION ||
			glyph->advance > UINT16_MAX / ORIGINAL_FONT_EXPANSION)
			goto failed;
		atlas_glyphs[i] =
			(AeronFontGlyph) { glyph->x * ORIGINAL_FONT_EXPANSION, glyph->y * ORIGINAL_FONT_EXPANSION,
							   glyph->width * ORIGINAL_FONT_EXPANSION,
							   glyph->height * ORIGINAL_FONT_EXPANSION,
							   glyph->advance * ORIGINAL_FONT_EXPANSION };
		layout_glyphs[i] = (XvtFontGlyph) { glyph->width, glyph->height, glyph->advance };
	}
	int width, height;
	expanded = Aeron_ImageUpscaleNearestRgba8(rgba, source->width, source->height + 2,
											  ORIGINAL_FONT_EXPANSION, &width, &height);
	if (!expanded)
		goto failed;
	AeronFontAtlasRgba8Desc desc = { .pixels = expanded,
									 .width = width,
									 .height = height,
									 .pitch = (size_t)width * 4,
									 .first_char = source->first_char,
									 .cell_w = source->cell_width * ORIGINAL_FONT_EXPANSION,
									 .cell_h = source->cell_height * ORIGINAL_FONT_EXPANSION,
									 .baseline = source->baseline * ORIGINAL_FONT_EXPANSION,
									 .glyphs = atlas_glyphs,
									 .glyph_count = source->glyph_count,
									 .format = AERON_TEXTURE_FORMAT_RGBA8_UNORM,
									 .color_space = AERON_COLOR_SPACE_LINEAR_SRGB,
									 .alpha_mode = AERON_IMAGE_ALPHA_PREMULTIPLIED,
									 .generate_mips = false,
									 .debug_name = label };
	if (!AeronFontAtlas_InitRgba8(&out->atlas, cmd, &desc))
		goto failed;
	out->glyphs = layout_glyphs;
	out->cell_height = source->cell_height;
	out->white_uv[0] = 0.5f;
	out->white_uv[1] = (height - ORIGINAL_FONT_EXPANSION * 0.5f) / height;
	free(rgba);
	free(expanded);
	free(atlas_glyphs);
	return 1;
failed:
	free(rgba);
	free(expanded);
	free(atlas_glyphs);
	free(layout_glyphs);
	return 0;
}
