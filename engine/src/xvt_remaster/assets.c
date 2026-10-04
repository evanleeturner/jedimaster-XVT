#include "xvt_remaster/assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt_remaster/opt_mesh.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_assets.h"

struct image_variant {
	AeronRuntimeAtlas atlas;
	uint32_t palette[256];
	uint16_t key;
	uint16_t key_alt;
	int committed;
	uint8_t map_style;
	uint8_t frontend_kind;
	uint32_t tint_color;
	struct image_variant *next;
};

struct image_asset {
	uint64_t id;
	uint64_t seen_generation;
	int is_texture;
	int pending_new;
	uint32_t kind;
	uint8_t frontend_pixel_format_555;
	char path[XVT_SNAP_PATH];
	struct xvt_original2d decoded;
	struct xvt_font_atlas foreground;
	struct xvt_font_atlas shadow;
	uint32_t default_palette[256];
	struct image_variant *variants;
};

/* Leave room for replacements until their upload submission retires the old set. */
static struct image_asset g_images[2 * (XVT_SNAP_ASSETS + XVT_SNAP_TYPES)];
static uint64_t g_generations[2] = {UINT64_MAX, UINT64_MAX};
static uint64_t g_batch_generation[2];
static int g_batch_active[2];
static uint32_t g_palette[256];
static uint32_t g_pending_palette[256];

static int palettes_match(const struct xvt_original2d *image, const uint32_t *a,
			  const uint32_t *b)
{
	if (!image->external_palette) {
		return 1;
	}
	for (unsigned i = 0; i < 256; ++i) {
		if (i >= image->palette_first &&
		    i < image->palette_first + image->palette_count) {
			continue;
		}
		if (a[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

static struct image_asset *find(uint64_t id)
{
	if (!id) {
		return NULL;
	}
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i) {
		if (g_images[i].id == id) {
			return &g_images[i];
		}
	}
	return NULL;
}

static void release(struct image_asset *image)
{
	while (image->variants) {
		struct image_variant *next = image->variants->next;
		Aeron_RuntimeAtlasRelease(&image->variants->atlas);
		free(image->variants);
		image->variants = next;
	}
	AeronFontAtlas_Release(&image->foreground.atlas);
	AeronFontAtlas_Release(&image->shadow.atlas);
	free(image->foreground.glyphs);
	free(image->shadow.glyphs);
	xvt_original2d_free(&image->decoded);
	memset(image, 0, sizeof *image);
}

static struct image_variant *find_variant(struct image_asset *image,
					  const uint32_t palette[256],
					  uint16_t key, uint16_t alt)
{
	if (!palette) {
		palette = image->default_palette;
	}
	for (struct image_variant *v = image->variants; v; v = v->next) {
		if (!v->map_style && !v->frontend_kind && v->key == key &&
		    v->key_alt == alt &&
		    palettes_match(&image->decoded, v->palette, palette)) {
			return v;
		}
	}
	return NULL;
}

const AeronRuntimeAtlas *
xvt_remaster_assets_prepare_image(AeronCommandBuffer *cmd, uint64_t id,
				  const uint32_t palette[256], uint16_t key,
				  uint16_t alt)
{
	struct image_asset *image = find(id);
	if (!image || !image->decoded.images.count) {
		return NULL;
	}
	struct image_variant *v = find_variant(image, palette, key, alt);
	if (v) {
		return &v->atlas;
	}
	if (!cmd) {
		return NULL;
	}
	v = calloc(1, sizeof *v);
	if (!v) {
		Aeron_CommandBufferSetFailure(cmd, "image variant allocation");
		return NULL;
	}
	memcpy(v->palette, palette ? palette : image->default_palette,
	       sizeof v->palette);
	v->key = key;
	v->key_alt = alt;
	if (!xvt_original2d_build_atlas(&image->decoded, cmd, v->palette, key,
					alt, 1, image->path, &v->atlas)) {
		Aeron_RuntimeAtlasRelease(&v->atlas);
		free(v);
		Aeron_CommandBufferSetFailure(cmd, "runtime image atlas build");
		return NULL;
	}
	v->next = image->variants;
	image->variants = v;
	return &v->atlas;
}

const AeronRuntimeAtlas *xvt_remaster_assets_image(uint64_t id,
						   const uint32_t palette[256],
						   uint16_t key, uint16_t alt)
{
	struct image_asset *image = find(id);
	struct image_variant *v =
		image ? find_variant(image, palette, key, alt) : NULL;
	return v && v->committed ? &v->atlas : NULL;
}

const struct xvt_font_atlas *xvt_remaster_assets_font(uint64_t id, int shadow)
{
	struct image_asset *image = find(id);
	if (!image || image->pending_new) {
		return NULL;
	}
	const struct xvt_font_atlas *font =
		shadow ? &image->shadow : &image->foreground;
	return font->atlas.loaded ? font : NULL;
}

static uint32_t decode_frontend_color(uint32_t color, int pixel_format_555)
{
	unsigned red = (color >> (pixel_format_555 ? 10 : 11)) & 31;
	unsigned green = (color >> 5) & (pixel_format_555 ? 31 : 63);
	unsigned blue = color & 31;
	red = (red << 3) | (red >> 2);
	green = pixel_format_555 ? (green << 3) | (green >> 2)
				 : (green << 2) | (green >> 4);
	blue = (blue << 3) | (blue >> 2);
	return 0xff000000u | (red << 16) | (green << 8) | blue;
}

static unsigned get_frontend_variant_kind(const struct xvt_snap_sprite *sprite)
{
	/* Translucency is applied by the compositor to the keyed image. */
	return 1 + (sprite->kind == XVT_SPRITE_FRONT_TRANSLUCENT
			    ? XVT_SPRITE_FRONT_KEYED
			    : sprite->kind);
}

static struct image_variant *
find_frontend_variant(const struct image_asset *image,
		      const struct xvt_snap_sprite *sprite)
{
	unsigned kind = get_frontend_variant_kind(sprite);
	for (struct image_variant *variant = image->variants; variant;
	     variant = variant->next) {
		if (variant->frontend_kind == kind &&
		    variant->tint_color == sprite->tint_color) {
			return variant;
		}
	}
	return NULL;
}

const AeronRuntimeAtlas *
xvt_remaster_assets_find_frontend_image(const struct xvt_snap_sprite *sprite)
{
	const struct image_asset *image = find(sprite->asset_id);
	const struct image_variant *variant =
		image ? find_frontend_variant(image, sprite) : NULL;
	return variant && variant->committed ? &variant->atlas : NULL;
}

int xvt_remaster_assets_prepare_frontend_image(
	AeronCommandBuffer *cmd, const struct xvt_snap_sprite *sprite)
{
	struct image_asset *image = find(sprite->asset_id);
	if (!image || !image->decoded.images.count) {
		return 0;
	}
	if (find_frontend_variant(image, sprite)) {
		return 1;
	}
	struct image_variant *variant = calloc(1, sizeof *variant);
	if (!variant) {
		return 0;
	}
	uint32_t colors[256];
	const uint32_t *palette = image->default_palette;
	if (sprite->kind == XVT_SPRITE_FRONT_TINTED) {
		int format_555 = image->frontend_pixel_format_555;
		unsigned shift = format_555 ? 10 : 11;
		unsigned tint = sprite->tint_color;
		for (unsigned i = 0; i < 256; ++i) {
			unsigned intensity =
				(image->default_palette[i] & 255) >> 3;
			unsigned red = intensity * (tint >> shift) / 31;
			unsigned green =
				intensity *
				((tint >> 5) & (format_555 ? 31 : 63)) / 31;
			unsigned blue = intensity * (tint & 31) / 31;
			colors[i] = decode_frontend_color(
				(red << shift) | (green << 5) | blue,
				format_555);
		}
		palette = colors;
	}
	unsigned key = sprite->kind == XVT_SPRITE_FRONT_OPAQUE ||
				       image->kind == XVT_IMAGE_BUILTIN_CURSOR
			       ? UINT16_MAX
			       : 0;
	/* Frontend pixel art uses nearest sampling without mip levels that mix sprite-sheet regions. */
	if (!xvt_original2d_build_atlas(&image->decoded, cmd, palette, key,
					UINT16_MAX, 0, image->path,
					&variant->atlas)) {
		Aeron_RuntimeAtlasRelease(&variant->atlas);
		free(variant);
		return 0;
	}
	variant->frontend_kind = get_frontend_variant_kind(sprite);
	variant->tint_color = sprite->tint_color;
	variant->next = image->variants;
	image->variants = variant;
	return 1;
}

static int map_palette_matches(const struct image_asset *image,
			       const struct image_variant *variant,
			       const uint32_t palette[256], int remap)
{
	for (unsigned index = 0; index < 256; ++index) {
		if (image->decoded.map_palette_used[index]) {
			unsigned color = (index + (remap ? 4 : 0)) & 255;
			if (variant->palette[color] != palette[color]) {
				return 0;
			}
		}
	}
	return 1;
}

const AeronRuntimeAtlas *
xvt_remaster_assets_prepare_map_icons(AeronCommandBuffer *cmd, uint64_t id,
				      const uint32_t palette[256], int remap)
{
	struct image_asset *image = find(id);
	if (!image || !image->decoded.panel_bytes) {
		return NULL;
	}
	unsigned style = remap ? 2 : 1;
	for (struct image_variant *variant = image->variants; variant;
	     variant = variant->next) {
		if (variant->map_style == style &&
		    map_palette_matches(image, variant, palette, remap)) {
			return &variant->atlas;
		}
	}
	struct image_variant *variant = calloc(1, sizeof *variant);
	if (!variant) {
		return NULL;
	}
	if (!xvt_original2d_build_map_icons(&image->decoded, cmd, palette,
					    remap, &variant->atlas)) {
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

const struct xvt_original2d *xvt_remaster_assets_find_decoded_image(uint64_t id)
{
	const struct image_asset *image = find(id);
	return image ? &image->decoded : NULL;
}

static int load(AeronCommandBuffer *cmd,
		const struct xvt_snap_image_asset *source,
		const struct xvt_render_snapshot *snapshot, int is_texture)
{
	struct image_asset *image = find(source->id);
	if (image) {
		image->seen_generation = g_batch_generation[is_texture];
		if (image->kind == XVT_IMAGE_BMP ||
		    image->kind == XVT_IMAGE_BUILTIN_CURSOR) {
			return 1;
		}
		memcpy(image->default_palette, snapshot->flight_palette_argb,
		       sizeof image->default_palette);
		if (image->foreground.atlas.loaded ||
		    image->decoded.panel_bytes) {
			return 1;
		}
		if (!xvt_remaster_assets_prepare_image(
			    cmd, source->id, NULL, UINT16_MAX, UINT16_MAX)) {
			return 0;
		}
		if (!is_texture && source->kind != XVT_IMAGE_BUILTIN_CURSOR) {
			if (!xvt_remaster_assets_prepare_image(
				    cmd, source->id, NULL, 0, UINT16_MAX)) {
				return 0;
			}
		}
		return 1;
	}
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i) {
		if (!g_images[i].id) {
			image = &g_images[i];
			break;
		}
	}
	if (!image) {
		Aeron_CommandBufferSetFailure(cmd,
					      "image cache capacity exceeded");
		return 0;
	}
	image->id = source->id;
	image->is_texture = is_texture;
	image->kind = source->kind;
	image->pending_new = 1;
	image->seen_generation = g_batch_generation[is_texture];
	snprintf(image->path, sizeof image->path, "%s", source->path);
	memcpy(image->default_palette, snapshot->flight_palette_argb,
	       sizeof image->default_palette);
	if (source->kind == XVT_IMAGE_BMP) {
		struct xvt_frontend_image_colors colors;
		if (!xvt_render_assets_copy_frontend_colors(source->id,
							    &colors)) {
			Aeron_CommandBufferSetFailure(
				cmd, "frontend image color table unavailable");
			return 0;
		}
		image->frontend_pixel_format_555 = colors.pixel_format_555;
		for (unsigned color = 0; color < 256; ++color) {
			image->default_palette[color] =
				decode_frontend_color(colors.color_lut[color],
						      colors.pixel_format_555);
		}
	}
	char error[1280];
	int ok = is_texture ? xvt_original2d_load_act(source->path,
						      &image->decoded, error,
						      sizeof error)
			    : xvt_original2d_load(source, &image->decoded,
						  error, sizeof error);
	if (!ok) {
		XVT_LOG_ERROR("remaster.asset_failed error=\"%s\"", error);
		Aeron_CommandBufferSetFailure(cmd, error);
		return 0;
	}
	if (source->kind == XVT_IMAGE_BMP ||
	    source->kind == XVT_IMAGE_BUILTIN_CURSOR) {
		return 1;
	}
	if (source->kind == XVT_IMAGE_LFD) {
		if (!xvt_cockpit_assets_apply_mask(&image->decoded,
						   &source->cockpit_viewport)) {
			Aeron_CommandBufferSetFailure(
				cmd, "cockpit viewport mask decode failed");
			return 0;
		}
	}
	if (image->decoded.font.foreground) {
		if (!xvt_original2d_build_font(&image->decoded.font, cmd, 0,
					       image->path,
					       &image->foreground) ||
		    !xvt_original2d_build_font(&image->decoded.font, cmd, 1,
					       image->path, &image->shadow)) {
			return 0;
		}
		AeronDecodedFont_Free(&image->decoded.font);
		return 1;
	}
	if (image->decoded.panel_bytes) {
		return 1;
	}
	if (!xvt_remaster_assets_prepare_image(cmd, source->id, NULL,
					       UINT16_MAX, UINT16_MAX)) {
		return 0;
	}
	if (source->kind == XVT_IMAGE_LFD || source->kind == XVT_IMAGE_PNL ||
	    source->kind == XVT_IMAGE_ICO) {
		if (!xvt_remaster_assets_prepare_image(cmd, source->id, NULL, 0,
						       UINT16_MAX)) {
			return 0;
		}
	}
	return 1;
}

int xvt_remaster_assets_init(void)
{
	char error[256];
	if (!xvt_remaster_opt_mesh_init(Aeron_GetVfs(), error, sizeof error)) {
		XVT_LOG_ERROR("remaster.opt_init_failed error=\"%s\"", error);
		return 0;
	}
	return 1;
}

int xvt_remaster_assets_images_need_sync(
	const struct xvt_render_snapshot *snapshot)
{
	return snapshot && snapshot->image_asset_generation != g_generations[0];
}

int xvt_remaster_assets_textures_need_sync(const struct xvt_render_snapshot *s)
{
	return s && s->texture_asset_generation != g_generations[1];
}

int xvt_remaster_assets_sync_images(AeronCommandBuffer *cmd,
				    const struct xvt_render_snapshot *s)
{
	if (!xvt_remaster_assets_images_need_sync(s)) {
		return 1;
	}
	if (g_batch_active[0]) {
		Aeron_CommandBufferSetFailure(cmd,
					      "unfinished image upload batch");
		return 0;
	}
	g_batch_active[0] = 1;
	g_batch_generation[0] = s->image_asset_generation;
	memcpy(g_pending_palette, s->flight_palette_argb,
	       sizeof g_pending_palette);
	for (unsigned i = 0; i < s->image_asset_count; ++i) {
		if (!load(cmd, &s->image_assets[i], s, 0)) {
			return 0;
		}
	}
	return 1;
}

int xvt_remaster_assets_sync_textures(AeronCommandBuffer *cmd,
				      const struct xvt_render_snapshot *s)
{
	if (!xvt_remaster_assets_textures_need_sync(s)) {
		return 1;
	}
	if (g_batch_active[1]) {
		Aeron_CommandBufferSetFailure(
			cmd, "unfinished texture upload batch");
		return 0;
	}
	g_batch_active[1] = 1;
	g_batch_generation[1] = s->texture_asset_generation;
	for (unsigned i = 0; i < s->texture_asset_count; ++i) {
		struct xvt_snap_image_asset source = {0};
		source.id = s->texture_assets[i].id;
		memcpy(source.path, s->texture_assets[i].path,
		       sizeof source.path);
		if (!load(cmd, &source, s, 1)) {
			return 0;
		}
	}
	return 1;
}

static void commit(int is_texture)
{
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i) {
		struct image_asset *image = &g_images[i];
		if (!image->id || image->is_texture != is_texture) {
			continue;
		}
		if (g_batch_active[is_texture] &&
		    image->seen_generation != g_batch_generation[is_texture]) {
			release(image);
			continue;
		}
		image->pending_new = 0;
		for (struct image_variant *variant = image->variants; variant;
		     variant = variant->next) {
			variant->committed = 1;
		}
	}
	if (!is_texture && g_batch_active[0]) {
		memcpy(g_palette, g_pending_palette, sizeof g_palette);
	}
	if (g_batch_active[is_texture]) {
		g_generations[is_texture] = g_batch_generation[is_texture];
	}
	g_batch_active[is_texture] = 0;
}

void xvt_remaster_assets_commit_images(void) { commit(0); }

void xvt_remaster_assets_commit_textures(void) { commit(1); }

void xvt_remaster_assets_abort(void)
{
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i) {
		struct image_asset *image = &g_images[i];
		if (image->pending_new) {
			release(image);
			continue;
		}
		if (!image->is_texture && image->kind != XVT_IMAGE_BMP) {
			memcpy(image->default_palette, g_palette,
			       sizeof image->default_palette);
		}
		struct image_variant **link = &image->variants;
		while (*link) {
			struct image_variant *v = *link;
			if (!v->committed) {
				*link = v->next;
				Aeron_RuntimeAtlasRelease(&v->atlas);
				free(v);
			} else {
				link = &v->next;
			}
		}
	}
	memset(g_batch_active, 0, sizeof g_batch_active);
	xvt_remaster_ship_abort();
}

void xvt_remaster_assets_shutdown(void)
{
	for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; ++i) {
		release(&g_images[i]);
	}
	g_generations[0] = g_generations[1] = UINT64_MAX;
	memset(g_batch_active, 0, sizeof g_batch_active);
	xvt_remaster_ship_shutdown();
	xvt_remaster_opt_mesh_shutdown();
}
