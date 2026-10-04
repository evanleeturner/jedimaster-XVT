#include "xvt_runtime/snapshot/render_assets.h"

#include <stdio.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/render/flight_palette.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_cockpit_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include "xvt_runtime/storage/storage.h"

enum {
	SOURCE_OPT = 100,
	SOURCE_ACT,
	SOURCE_CAPACITY = XVT_SNAP_ASSETS * 2 + XVT_SNAP_TYPES
};

struct source {
	struct xvt_snap_image_asset image;
	struct xvt_frontend_image_colors frontend_colors;
	const void *owner;
	uint16_t handle;
	uint8_t retired;
};

static struct source g_sources[SOURCE_CAPACITY];
static uint64_t g_bindings[XVT_SNAP_TYPES];
static uint64_t g_next_id;
static uint64_t g_opt_generation;
static uint64_t g_texture_generation;
static uint64_t g_image_generation;
static uint64_t g_exported_snapshot_serial;
static uint64_t g_consumed_snapshot_serial;
static int g_initialized;

static void changed(uint32_t kind)
{
	if (kind == SOURCE_OPT) {
		++g_opt_generation;
	} else if (kind == SOURCE_ACT) {
		++g_texture_generation;
	} else {
		++g_image_generation;
	}
}

static void retire(struct source *source)
{
	if (!source->image.id || source->retired) {
		return;
	}
	source->retired = 1;
	changed(source->image.kind);
	for (unsigned i = 0; i < XVT_SNAP_TYPES; ++i) {
		if (g_bindings[i] == source->image.id) {
			g_bindings[i] = 0;
		}
	}
	xvt_render_cockpit_forget(source->image.id);
}

void xvt_render_assets_init(void)
{
	memset(g_sources, 0, sizeof g_sources);
	memset(g_bindings, 0, sizeof g_bindings);
	g_next_id = 1;
	g_opt_generation = g_texture_generation = g_image_generation = 1;
	g_exported_snapshot_serial = g_consumed_snapshot_serial = UINT64_MAX;
	g_initialized = 1;
	xvt_render_cockpit_reset();
	xvt_render_assets_register_image(g_default_cursor_bitmap, 0, "",
					 XVT_IMAGE_BUILTIN_CURSOR, 0, 1, 0, 0,
					 0);
}

void xvt_render_assets_shutdown(void)
{
	g_initialized = 0;
	memset(g_sources, 0, sizeof g_sources);
	xvt_render_cockpit_reset();
}

static int
snapshot_references_source(const struct xvt_render_snapshot *snapshot,
			   uint64_t id)
{
	if (!snapshot) {
		return 0;
	}
	if (snapshot->flight_valid) {
		for (unsigned type = 0; type < XVT_SNAP_TYPES; ++type) {
			if (snapshot->types[type].model_asset_id == id ||
			    snapshot->types[type].texture_asset_id == id) {
				return 1;
			}
		}
		if (snapshot->map.icon_asset_id == id ||
		    snapshot->map.font_asset_id == id) {
			return 1;
		}
	}
	const struct xvt_cockpit_state *cockpit = &snapshot->cockpit;
	if (cockpit->valid) {
		const struct xvt_cockpit_definition *definition =
			&cockpit->definition;
		for (unsigned panel = 0; panel < XVT_HUD_PANEL_BINDINGS;
		     ++panel) {
			if (definition->panels[panel].asset_id == id) {
				return 1;
			}
		}
		for (unsigned view = 0; view < 28; ++view) {
			if (definition->layout.descriptors[view].lfd_asset_id ==
			    id) {
				return 1;
			}
		}
		for (unsigned tier = 0; tier < XVT_HUD_FONT_TIERS; ++tier) {
			if (definition->fonts[tier].asset_id == id) {
				return 1;
			}
		}
		for (unsigned glyph = 0;
		     glyph < cockpit->page_content.glyph_count; ++glyph) {
			if (cockpit->page_content.glyphs[glyph].font_asset_id ==
			    id) {
				return 1;
			}
		}
		for (unsigned glyph = 0;
		     glyph < cockpit->overlay_content.glyph_count; ++glyph) {
			if (cockpit->overlay_content.glyphs[glyph]
				    .font_asset_id == id) {
				return 1;
			}
		}
		if (cockpit->crt.valid && cockpit->crt.opt_asset_id == id) {
			return 1;
		}
	}
	return 0;
}

void xvt_render_assets_begin_frame(void)
{
	if (!g_initialized ||
	    g_consumed_snapshot_serial != g_exported_snapshot_serial) {
		return;
	}
	/* A held presentation can still reference an original source after its
	 * classic handle is freed. Keep its descriptor until both snapshots retire it. */
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		if (g_sources[i].retired &&
		    !snapshot_references_source(xvt_render_snapshot_current(),
						g_sources[i].image.id) &&
		    !snapshot_references_source(xvt_render_snapshot_previous(),
						g_sources[i].image.id)) {
			changed(g_sources[i].image.kind);
			memset(&g_sources[i], 0, sizeof g_sources[i]);
		}
	}
}

void xvt_render_assets_consumed(uint64_t snapshot_serial)
{
	g_consumed_snapshot_serial = snapshot_serial;
}

static uint64_t register_asset(const void *owner, uint16_t handle,
			       const char *path, uint32_t kind, uint32_t first,
			       uint32_t count, uint16_t point_size,
			       uint8_t row_bytes, int make_palette,
			       const struct xvt_snap_rect *viewport)
{
	if (!g_initialized || (!owner && !handle)) {
		return 0;
	}
	char resolved[XVT_SNAP_PATH];
	if (kind == XVT_IMAGE_BUILTIN_CURSOR) {
		resolved[0] = 0;
	} else if (xvt_storage_resolve_asset(path, resolved, sizeof resolved) !=
		   1) {
		XVT_LOG_ERROR("snapshot.asset_unresolved path=\"%s\"",
			      path ? path : "");
		Aeron_RequestFatalRendererError("loaded asset path resolution");
		return 0;
	}
	/* The asset VFS is case-insensitive; use one cache key for spelling variants. */
	for (unsigned i = 0; resolved[i]; ++i) {
		if (resolved[i] >= 'A' && resolved[i] <= 'Z') {
			resolved[i] += 'a' - 'A';
		}
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		struct source *s = &g_sources[i];
		if (!s->image.id || s->retired) {
			continue;
		}
		if (owner ? s->owner == owner
			  : (!s->owner && s->handle == handle)) {
			if (s->handle == handle && s->image.kind == kind &&
			    strcmp(s->image.path, resolved) == 0 &&
			    s->image.first_record == first &&
			    s->image.record_count == count &&
			    s->image.font_point_size == point_size &&
			    s->image.font_row_bytes == row_bytes &&
			    s->image.make_palette == (make_palette != 0) &&
			    (!viewport ||
			     memcmp(&s->image.cockpit_viewport, viewport,
				    sizeof *viewport) == 0)) {
				return s->image.id;
			}
			retire(s);
		}
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		struct source *s = &g_sources[i];
		if (s->image.id) {
			continue;
		}
		s->owner = owner;
		s->handle = handle;
		s->image.id = g_next_id++;
		s->image.kind = kind;
		snprintf(s->image.path, sizeof s->image.path, "%s", resolved);
		s->image.first_record = first;
		s->image.record_count = count;
		s->image.font_point_size = point_size;
		s->image.font_row_bytes = row_bytes;
		s->image.make_palette = make_palette != 0;
		if (viewport) {
			s->image.cockpit_viewport = *viewport;
		}
		changed(kind);
		return s->image.id;
	}
	Aeron_RequestFatalRendererError(
		"render source registry capacity exceeded");
	return 0;
}

uint64_t xvt_render_assets_register_image(const void *owner, uint16_t handle,
					  const char *path,
					  xvt_snap_image_kind kind,
					  uint32_t first, uint32_t count,
					  uint16_t point_size,
					  uint8_t row_bytes, int make_palette)
{
	return register_asset(owner, handle, path, kind, first, count,
			      point_size, row_bytes, make_palette, NULL);
}

void xvt_render_assets_register_frontend_image(
	const struct image_resource *image, const char *path, int make_palette,
	int pixel_format_555)
{
	uint64_t id = register_asset(image, 0, path, XVT_IMAGE_BMP, 0, 1, 0, 0,
				     make_palette, NULL);
	if (!id) {
		return;
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		struct source *source = &g_sources[i];
		if (source->image.id != id) {
			continue;
		}
		source->frontend_colors.pixel_format_555 =
			pixel_format_555 != 0;
		for (unsigned color = 0; color < 256; ++color) {
			source->frontend_colors.color_lut[color] =
				(uint16_t)image->color_lut[color];
		}
		return;
	}
}

int xvt_render_assets_copy_frontend_colors(
	uint64_t id, struct xvt_frontend_image_colors *colors)
{
	if (!g_initialized || !id || !colors) {
		return 0;
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		const struct source *source = &g_sources[i];
		if (source->image.id == id &&
		    source->image.kind == XVT_IMAGE_BMP) {
			/* Retired sources remain readable until their exported frame is consumed. */
			*colors = source->frontend_colors;
			return 1;
		}
	}
	return 0;
}

uint64_t
xvt_render_assets_register_cockpit(const void *owner, uint16_t handle,
				   const char *path,
				   const struct xvt_snap_rect *viewport)
{
	return register_asset(owner, handle, path, XVT_IMAGE_LFD, 0, 1, 0, 0, 0,
			      viewport);
}

void xvt_render_assets_register_opt(uint16_t handle, const char *path)
{
	if (handle) {
		register_asset(NULL, handle, path, SOURCE_OPT, 0, 0, 0, 0, 0,
			       NULL);
	}
}

void xvt_render_assets_register_texture(uint16_t handle, const char *path)
{
	if (handle) {
		register_asset(NULL, handle, path, SOURCE_ACT, 0, 0, 0, 0, 0,
			       NULL);
	}
}

uint64_t xvt_render_assets_handle_id(uint16_t handle)
{
	if (!handle) {
		return 0;
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		if (g_sources[i].image.id && !g_sources[i].retired &&
		    !g_sources[i].owner && g_sources[i].handle == handle) {
			return g_sources[i].image.id;
		}
	}
	return 0;
}

void xvt_render_assets_bind_type(uint16_t type, uint16_t handle)
{
	if (type < XVT_SNAP_TYPES) {
		g_bindings[type] = xvt_render_assets_handle_id(handle);
	}
}

void xvt_render_assets_retire_handle(unsigned int handle)
{
	if (!g_initialized || !handle || handle > UINT16_MAX) {
		return;
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		if (g_sources[i].handle == handle) {
			retire(&g_sources[i]);
		}
	}
}

void xvt_render_assets_retire_image(const void *owner)
{
	if (!g_initialized || !owner) {
		return;
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		if (g_sources[i].owner == owner) {
			retire(&g_sources[i]);
		}
	}
}

uint64_t xvt_render_assets_image_id(const void *owner)
{
	if (!owner) {
		return 0;
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		if (!g_sources[i].retired && g_sources[i].owner == owner) {
			return g_sources[i].image.id;
		}
	}
	return 0;
}

void xvt_render_assets_clear_mission(void)
{
	memset(g_bindings, 0, sizeof g_bindings);
	++g_opt_generation;
	++g_texture_generation;
	xvt_render_cockpit_reset();
}

const uint8_t *xvt_render_assets_default_cursor(void)
{
	return g_default_cursor_bitmap;
}

void xvt_render_assets_export(struct xvt_render_snapshot *snapshot)
{
	if (!g_initialized || !snapshot) {
		return;
	}
	snapshot->opt_asset_count = snapshot->texture_asset_count =
		snapshot->image_asset_count = 0;
	if (!snapshot->flight_valid) {
		for (unsigned i = 0; i < XVT_SNAP_TYPES; ++i) {
			snapshot->types[i].model_asset_id =
				snapshot->types[i].texture_asset_id = 0;
		}
	}
	for (unsigned i = 0; i < SOURCE_CAPACITY; ++i) {
		const struct source *s = &g_sources[i];
		if (!s->image.id) {
			continue;
		}
		if (s->image.kind == SOURCE_OPT &&
		    snapshot->opt_asset_count < XVT_SNAP_ASSETS) {
			struct xvt_snap_opt_asset *out =
				&snapshot->opt_assets
					 [snapshot->opt_asset_count++];
			out->id = s->image.id;
			out->classic_handle = s->handle;
			memcpy(out->path, s->image.path, sizeof out->path);
		} else if (s->image.kind == SOURCE_ACT &&
			   snapshot->texture_asset_count < XVT_SNAP_TYPES) {
			struct xvt_snap_texture_asset *out =
				&snapshot->texture_assets
					 [snapshot->texture_asset_count++];
			out->id = s->image.id;
			out->classic_handle = s->handle;
			out->model_type = UINT16_MAX;
			memcpy(out->path, s->image.path, sizeof out->path);
			for (unsigned t = 0; t < XVT_SNAP_TYPES; ++t) {
				if (g_bindings[t] == out->id) {
					out->model_type = (uint16_t)t;
					break;
				}
			}
		} else if (s->image.kind < SOURCE_OPT &&
			   snapshot->image_asset_count < XVT_SNAP_ASSETS) {
			snapshot->image_assets[snapshot->image_asset_count++] =
				s->image;
		} else {
			++snapshot->dropped_records;
			Aeron_RequestFatalRendererError(
				"snapshot asset capacity exceeded");
		}
		for (unsigned t = 0; t < XVT_SNAP_TYPES; ++t) {
			if (snapshot->flight_valid ||
			    g_bindings[t] != s->image.id) {
				continue;
			}
			if (s->image.kind == SOURCE_OPT) {
				snapshot->types[t].model_asset_id = s->image.id;
			} else if (s->image.kind == SOURCE_ACT) {
				snapshot->types[t].texture_asset_id =
					s->image.id;
			}
		}
	}
	snapshot->opt_asset_generation = g_opt_generation;
	snapshot->texture_asset_generation = g_texture_generation;
	snapshot->image_asset_generation = g_image_generation;
	for (unsigned i = 0; i < 256; ++i) {
		snapshot->flight_palette_argb[i] = xvt_render_draw_color(i);
	}

	g_exported_snapshot_serial = snapshot->snapshot_serial;
}
