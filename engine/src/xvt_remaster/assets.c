#include "xvt_remaster/assets.h"
#include "aeron/aeron.h"
#include "xvt_remaster/opt_mesh.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ImageVariant {
	AeronRuntimeAtlas atlas;
	uint32_t palette[256];
	uint16_t key, key_alt;
	int committed;
	uint8_t map_style;
	uint8_t frontend_kind;
	uint32_t tint_color;
	struct ImageVariant* next;
} ImageVariant;

typedef struct ImageAsset {
	uint64_t id, seen;
	int texture, pending_new;
	uint32_t kind;
	uint8_t frontend_pixel_format_555;
	char path[XVT_SNAP_PATH];
	XvtOriginal2d decoded;
	XvtFontAtlas foreground, shadow;
	uint32_t default_palette[256];
	ImageVariant* variants;
} ImageAsset;

/* Leave room for replacements until their upload submission retires the old set. */
static ImageAsset g_images[2 * (XVT_SNAP_ASSETS + XVT_SNAP_TYPES)];
static uint64_t g_generations[2] = { UINT64_MAX, UINT64_MAX };
static uint64_t g_batchGeneration[2];
static int g_batchActive[2];
static uint32_t g_palette[256], g_pendingPalette[256];

static int PalettesMatch(const XvtOriginal2d* image, const uint32_t* a, const uint32_t* b) {
	if (!image->external_palette)
		return 1;
	for (unsigned i = 0; i < 256; ++i) {
		if (i >= image->palette_first && i < image->palette_first + image->palette_count)
			continue;
		if (a[i] != b[i])
			return 0;
	}
	return 1;
}

static ImageAsset* Find(uint64_t id) {
	if (!id)
		return NULL;
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i)
		if (g_images[i].id == id)
			return &g_images[i];
	return NULL;
}

static void Release(ImageAsset* image) {
	while (image->variants) {
		ImageVariant* next = image->variants->next;
		Aeron_RuntimeAtlasRelease(&image->variants->atlas);
		free(image->variants);
		image->variants = next;
	}
	AeronFontAtlas_Release(&image->foreground.atlas);
	AeronFontAtlas_Release(&image->shadow.atlas);
	free(image->foreground.glyphs);
	free(image->shadow.glyphs);
	XvtOriginal2d_Free(&image->decoded);
	memset(image, 0, sizeof *image);
}

static ImageVariant* FindVariant(ImageAsset* image, const uint32_t palette[256], uint16_t key, uint16_t alt) {
	if (!palette)
		palette = image->default_palette;
	for (ImageVariant* v = image->variants; v; v = v->next)
		if (!v->map_style && !v->frontend_kind && v->key == key && v->key_alt == alt &&
			PalettesMatch(&image->decoded, v->palette, palette))
			return v;
	return NULL;
}

const AeronRuntimeAtlas* XvtRemasterAssets_PrepareImage(AeronCommandBuffer* cmd, uint64_t id,
														const uint32_t palette[256], uint16_t key,
														uint16_t alt) {
	ImageAsset* image = Find(id);
	if (!image || !image->decoded.images.count)
		return NULL;
	ImageVariant* v = FindVariant(image, palette, key, alt);
	if (v) {
		return &v->atlas;
	}
	if (!cmd)
		return NULL;
	v = calloc(1, sizeof *v);
	if (!v) {
		Aeron_CommandBufferSetFailure(cmd, "image variant allocation");
		return NULL;
	}
	memcpy(v->palette, palette ? palette : image->default_palette, sizeof v->palette);
	v->key = key;
	v->key_alt = alt;
	if (!XvtOriginal2d_BuildAtlas(&image->decoded, cmd, v->palette, key, alt, 1, image->path, &v->atlas)) {
		Aeron_RuntimeAtlasRelease(&v->atlas);
		free(v);
		Aeron_CommandBufferSetFailure(cmd, "runtime image atlas build");
		return NULL;
	}
	v->next = image->variants;
	image->variants = v;
	return &v->atlas;
}

const AeronRuntimeAtlas* XvtRemasterAssets_Image(uint64_t id, const uint32_t palette[256], uint16_t key,
												 uint16_t alt) {
	ImageAsset* image = Find(id);
	ImageVariant* v = image ? FindVariant(image, palette, key, alt) : NULL;
	return v && v->committed ? &v->atlas : NULL;
}

const XvtFontAtlas* XvtRemasterAssets_Font(uint64_t id, int shadow) {
	ImageAsset* image = Find(id);
	if (!image || image->pending_new)
		return NULL;
	const XvtFontAtlas* font = shadow ? &image->shadow : &image->foreground;
	return font->atlas.loaded ? font : NULL;
}

static uint32_t DecodeFrontendColor(uint32_t color, int pixel_format_555) {
	unsigned red = (color >> (pixel_format_555 ? 10 : 11)) & 31;
	unsigned green = (color >> 5) & (pixel_format_555 ? 31 : 63);
	unsigned blue = color & 31;
	red = (red << 3) | (red >> 2);
	green = pixel_format_555 ? (green << 3) | (green >> 2) : (green << 2) | (green >> 4);
	blue = (blue << 3) | (blue >> 2);
	return 0xff000000u | (red << 16) | (green << 8) | blue;
}

static unsigned GetFrontendVariantKind(const XvtSnapSprite* sprite) {
	/* Translucency is applied by the compositor to the keyed image. */
	return 1 + (sprite->kind == XVT_SPRITE_FRONT_TRANSLUCENT ? XVT_SPRITE_FRONT_KEYED : sprite->kind);
}

static ImageVariant* FindFrontendVariant(const ImageAsset* image, const XvtSnapSprite* sprite) {
	unsigned kind = GetFrontendVariantKind(sprite);
	for (ImageVariant* variant = image->variants; variant; variant = variant->next)
		if (variant->frontend_kind == kind && variant->tint_color == sprite->tint_color)
			return variant;
	return NULL;
}

const AeronRuntimeAtlas* XvtRemasterAssets_FindFrontendImage(const XvtSnapSprite* sprite) {
	const ImageAsset* image = Find(sprite->asset_id);
	const ImageVariant* variant = image ? FindFrontendVariant(image, sprite) : NULL;
	return variant && variant->committed ? &variant->atlas : NULL;
}

int XvtRemasterAssets_PrepareFrontendImage(AeronCommandBuffer* cmd, const XvtSnapSprite* sprite) {
	ImageAsset* image = Find(sprite->asset_id);
	if (!image || !image->decoded.images.count)
		return 0;
	if (FindFrontendVariant(image, sprite))
		return 1;
	ImageVariant* variant = calloc(1, sizeof *variant);
	if (!variant)
		return 0;
	uint32_t colors[256];
	const uint32_t* palette = image->default_palette;
	if (sprite->kind == XVT_SPRITE_FRONT_TINTED) {
		int format_555 = image->frontend_pixel_format_555;
		unsigned shift = format_555 ? 10 : 11;
		unsigned tint = sprite->tint_color;
		for (unsigned i = 0; i < 256; ++i) {
			unsigned intensity = (image->default_palette[i] & 255) >> 3;
			unsigned red = intensity * (tint >> shift) / 31;
			unsigned green = intensity * ((tint >> 5) & (format_555 ? 31 : 63)) / 31;
			unsigned blue = intensity * (tint & 31) / 31;
			colors[i] = DecodeFrontendColor((red << shift) | (green << 5) | blue, format_555);
		}
		palette = colors;
	}
	unsigned key =
		sprite->kind == XVT_SPRITE_FRONT_OPAQUE || image->kind == XVT_IMAGE_BUILTIN_CURSOR ? UINT16_MAX : 0;
	/* Frontend pixel art uses nearest sampling without mip levels that mix sprite-sheet regions. */
	if (!XvtOriginal2d_BuildAtlas(&image->decoded, cmd, palette, key, UINT16_MAX, 0, image->path,
								  &variant->atlas)) {
		Aeron_RuntimeAtlasRelease(&variant->atlas);
		free(variant);
		return 0;
	}
	variant->frontend_kind = GetFrontendVariantKind(sprite);
	variant->tint_color = sprite->tint_color;
	variant->next = image->variants;
	image->variants = variant;
	return 1;
}

static int MapPaletteMatches(const ImageAsset* image, const ImageVariant* variant,
							 const uint32_t palette[256], int remap) {
	for (unsigned index = 0; index < 256; ++index)
		if (image->decoded.map_palette_used[index]) {
			unsigned color = (index + (remap ? 4 : 0)) & 255;
			if (variant->palette[color] != palette[color])
				return 0;
		}
	return 1;
}

const AeronRuntimeAtlas* XvtRemasterAssets_PrepareMapIcons(AeronCommandBuffer* cmd, uint64_t id,
														   const uint32_t palette[256], int remap) {
	ImageAsset* image = Find(id);
	if (!image || !image->decoded.panel_bytes)
		return NULL;
	unsigned style = remap ? 2 : 1;
	for (ImageVariant* variant = image->variants; variant; variant = variant->next)
		if (variant->map_style == style && MapPaletteMatches(image, variant, palette, remap)) {
			return &variant->atlas;
		}
	ImageVariant* variant = calloc(1, sizeof *variant);
	if (!variant)
		return NULL;
	if (!XvtOriginal2d_BuildMapIcons(&image->decoded, cmd, palette, remap, &variant->atlas)) {
		Aeron_RuntimeAtlasRelease(&variant->atlas);
		free(variant);
		return NULL;
	}
	variant->map_style = (uint8_t)style;
	memcpy(variant->palette, palette, sizeof variant->palette);
	variant->next = image->variants;
	image->variants = variant;
	return &variant->atlas;
}

const XvtOriginal2d* XvtRemasterAssets_FindDecodedImage(uint64_t id) {
	const ImageAsset* image = Find(id);
	return image ? &image->decoded : NULL;
}

static int Load(AeronCommandBuffer* cmd, const XvtSnapImageAsset* source, const XvtRenderSnapshot* snapshot,
				int texture) {
	ImageAsset* image = Find(source->id);
	if (image) {
		image->seen = g_batchGeneration[texture];
		if (image->kind == XVT_IMAGE_BMP || image->kind == XVT_IMAGE_BUILTIN_CURSOR)
			return 1;
		memcpy(image->default_palette, snapshot->flight_palette_argb, sizeof image->default_palette);
		if (image->foreground.atlas.loaded || image->decoded.panel_bytes)
			return 1;
		if (!XvtRemasterAssets_PrepareImage(cmd, source->id, NULL, UINT16_MAX, UINT16_MAX))
			return 0;
		if (!texture && source->kind != XVT_IMAGE_BUILTIN_CURSOR)
			if (!XvtRemasterAssets_PrepareImage(cmd, source->id, NULL, 0, UINT16_MAX))
				return 0;
		return 1;
	}
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i)
		if (!g_images[i].id) {
			image = &g_images[i];
			break;
		}
	if (!image) {
		Aeron_CommandBufferSetFailure(cmd, "image cache capacity exceeded");
		return 0;
	}
	image->id = source->id;
	image->texture = texture;
	image->kind = source->kind;
	image->pending_new = 1;
	image->seen = g_batchGeneration[texture];
	snprintf(image->path, sizeof image->path, "%s", source->path);
	memcpy(image->default_palette, snapshot->flight_palette_argb, sizeof image->default_palette);
	if (source->kind == XVT_IMAGE_BMP) {
		XvtFrontendImageColors colors;
		if (!XvtRenderAssets_CopyFrontendColors(source->id, &colors)) {
			Aeron_CommandBufferSetFailure(cmd, "frontend image color table unavailable");
			return 0;
		}
		image->frontend_pixel_format_555 = colors.pixel_format_555;
		for (unsigned color = 0; color < 256; ++color)
			image->default_palette[color] =
				DecodeFrontendColor(colors.color_lut[color], colors.pixel_format_555);
	}
	char error[1280];
	int ok = texture ? XvtOriginal2d_LoadAct(source->path, &image->decoded, error, sizeof error)
					 : XvtOriginal2d_Load(source, &image->decoded, error, sizeof error);
	if (!ok) {
		Aeron_LogError("xvt.remaster", "%s", error);
		Aeron_CommandBufferSetFailure(cmd, error);
		return 0;
	}
	if (source->kind == XVT_IMAGE_BMP || source->kind == XVT_IMAGE_BUILTIN_CURSOR)
		return 1;
	if (source->kind == XVT_IMAGE_LFD) {
		if (!XvtCockpitAssets_ApplyMask(&image->decoded, &source->cockpit_viewport)) {
			Aeron_CommandBufferSetFailure(cmd, "cockpit viewport mask decode failed");
			return 0;
		}
	}
	if (image->decoded.font.foreground) {
		if (!XvtOriginal2d_BuildFont(&image->decoded.font, cmd, 0, image->path, &image->foreground) ||
			!XvtOriginal2d_BuildFont(&image->decoded.font, cmd, 1, image->path, &image->shadow))
			return 0;
		AeronDecodedFont_Free(&image->decoded.font);
		return 1;
	}
	if (image->decoded.panel_bytes)
		return 1;
	if (!XvtRemasterAssets_PrepareImage(cmd, source->id, NULL, UINT16_MAX, UINT16_MAX))
		return 0;
	if (source->kind == XVT_IMAGE_LFD || source->kind == XVT_IMAGE_PNL || source->kind == XVT_IMAGE_ICO)
		if (!XvtRemasterAssets_PrepareImage(cmd, source->id, NULL, 0, UINT16_MAX))
			return 0;
	return 1;
}

int XvtRemasterAssets_Init(void) {
	char error[256];
	if (!XvtRemasterOptMesh_Init(Aeron_GetVfs(), error, sizeof error)) {
		Aeron_LogError("xvt.remaster", "%s", error);
		return 0;
	}
	return 1;
}

int XvtRemasterAssets_ImagesNeedSync(const XvtRenderSnapshot* snapshot) {
	return snapshot && snapshot->image_asset_generation != g_generations[0];
}

int XvtRemasterAssets_TexturesNeedSync(const XvtRenderSnapshot* s) {
	return s && s->texture_asset_generation != g_generations[1];
}

int XvtRemasterAssets_SyncImages(AeronCommandBuffer* cmd, const XvtRenderSnapshot* s) {
	if (!XvtRemasterAssets_ImagesNeedSync(s))
		return 1;
	if (g_batchActive[0]) {
		Aeron_CommandBufferSetFailure(cmd, "unfinished image upload batch");
		return 0;
	}
	g_batchActive[0] = 1;
	g_batchGeneration[0] = s->image_asset_generation;
	memcpy(g_pendingPalette, s->flight_palette_argb, sizeof g_pendingPalette);
	for (unsigned i = 0; i < s->image_asset_count; ++i)
		if (!Load(cmd, &s->image_assets[i], s, 0))
			return 0;
	return 1;
}

int XvtRemasterAssets_SyncTextures(AeronCommandBuffer* cmd, const XvtRenderSnapshot* s) {
	if (!XvtRemasterAssets_TexturesNeedSync(s))
		return 1;
	if (g_batchActive[1]) {
		Aeron_CommandBufferSetFailure(cmd, "unfinished texture upload batch");
		return 0;
	}
	g_batchActive[1] = 1;
	g_batchGeneration[1] = s->texture_asset_generation;
	for (unsigned i = 0; i < s->texture_asset_count; ++i) {
		XvtSnapImageAsset source = { 0 };
		source.id = s->texture_assets[i].id;
		memcpy(source.path, s->texture_assets[i].path, sizeof source.path);
		if (!Load(cmd, &source, s, 1))
			return 0;
	}
	return 1;
}

static void Commit(int texture) {
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i) {
		ImageAsset* image = &g_images[i];
		if (!image->id || image->texture != texture)
			continue;
		if (g_batchActive[texture] && image->seen != g_batchGeneration[texture]) {
			Release(image);
			continue;
		}
		image->pending_new = 0;
		for (ImageVariant* variant = image->variants; variant; variant = variant->next)
			variant->committed = 1;
	}
	if (!texture && g_batchActive[0])
		memcpy(g_palette, g_pendingPalette, sizeof g_palette);
	if (g_batchActive[texture])
		g_generations[texture] = g_batchGeneration[texture];
	g_batchActive[texture] = 0;
}

void XvtRemasterAssets_CommitImages(void) { Commit(0); }

void XvtRemasterAssets_CommitTextures(void) { Commit(1); }

void XvtRemasterAssets_Abort(void) {
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i) {
		ImageAsset* image = &g_images[i];
		if (image->pending_new) {
			Release(image);
			continue;
		}
		if (!image->texture && image->kind != XVT_IMAGE_BMP)
			memcpy(image->default_palette, g_palette, sizeof image->default_palette);
		ImageVariant** link = &image->variants;
		while (*link) {
			ImageVariant* v = *link;
			if (!v->committed) {
				*link = v->next;
				Aeron_RuntimeAtlasRelease(&v->atlas);
				free(v);
			} else {
				link = &v->next;
			}
		}
	}
	memset(g_batchActive, 0, sizeof g_batchActive);
	XvtRemasterShip_Abort();
}

void XvtRemasterAssets_Shutdown(void) {
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i)
		Release(&g_images[i]);
	g_generations[0] = g_generations[1] = UINT64_MAX;
	memset(g_batchActive, 0, sizeof g_batchActive);
	XvtRemasterShip_Shutdown();
	XvtRemasterOptMesh_Shutdown();
}
