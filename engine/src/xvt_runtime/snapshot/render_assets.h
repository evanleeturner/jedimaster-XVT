#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_ASSETS_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_ASSETS_H

#include "xvt_runtime/snapshot/render_snapshot.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registry of the classic game's loaded asset sources: OPT models, ACT textures, images, fonts
 * and cockpit files. Each source gets a nonzero id the remaster renders by. A source is keyed
 * by its owner pointer when it has one, otherwise by its classic memory handle. Export copies
 * the registry into each snapshot.
 *
 * Invariants:
 *   - ids are never reused between one Init and the next
 *   - a freed source is retired, not dropped: it stays exported and readable until the renderer
 *     has consumed the last export and neither the current nor the previous snapshot uses it
 *   - any change to a kind's sources raises that kind's generation (OPT, texture, image)
 *
 * The Register* calls share one rule: registering the same file with the same parameters
 * returns the existing id; anything else under the same key retires the old source and gets
 * a new id. Paths are resolved through storage and compared case-insensitively. An
 * unresolvable path or a full registry requests a fatal renderer error and yields no id. */

/* Clears the registry and type bindings, restarts ids and generations at 1, resets the cockpit
 * bindings, and registers the built-in default cursor. */
void xvt_render_assets_init(void);
/* Clears the registry and cockpit bindings; the registry takes no new source until Init. Type
 * bindings are not cleared. */
void xvt_render_assets_shutdown(void);
/* Once the renderer has consumed the last export, removes the retired sources that neither the
 * current nor the previous snapshot uses. Does nothing before Init. */
void xvt_render_assets_begin_frame(void);
/* Writes every registered source, retired ones included, into snapshot's OPT, texture and
 * image asset lists; a texture entry names the first model type bound to it (UINT16_MAX when
 * none). Without a valid flight view it also refills the model types' asset ids from the
 * bindings. Stamps the three generations and the flight palette, and records the snapshot's
 * serial as the export awaiting consumption. A source past its list's capacity counts a dropped
 * record and requests a fatal renderer error. Does nothing before Init or for NULL. */
void xvt_render_assets_export(struct xvt_render_snapshot *snapshot);
/* Records that the renderer is done with the export of snapshot_serial. */
void xvt_render_assets_consumed(uint64_t snapshot_serial);
/* Register the model or texture file loaded into a classic handle, keyed by that handle.
 * Handle 0 is ignored. */
void xvt_render_assets_register_opt(uint16_t handle, const char *path);
void xvt_render_assets_register_texture(uint16_t handle, const char *path);
/* Binds a model type to the source now registered for handle; 0 or an unknown handle unbinds.
 * Types from XVT_SNAP_TYPES up are ignored. */
void xvt_render_assets_bind_type(uint16_t type, uint16_t handle);
/* Retires every source carrying this handle, owner-keyed ones included, and drops the type and
 * cockpit bindings to them. Does nothing before Init, for 0, or above 65535. */
void xvt_render_assets_retire_handle(unsigned int handle);
/* Clears all type bindings, raises the OPT and texture generations, and resets the cockpit
 * bindings. Retires no source. */
void xvt_render_assets_clear_mission(void);
/* Returns the id of the live handle-keyed source for handle, or 0. Owner-keyed and retired
 * sources never match. */
uint64_t xvt_render_assets_handle_id(uint16_t handle);
/* Returns the id of the live source keyed by owner, or 0. */
uint64_t xvt_render_assets_image_id(const void *owner);
/* Retires every source keyed by owner. Does nothing before Init or for NULL. */
void xvt_render_assets_retire_image(const void *owner);
struct image_resource;

struct xvt_frontend_image_colors {
	uint16_t color_lut[256];
	uint8_t pixel_format_555;
};

/* Registers a frontend BMP keyed by image and stores its 256-entry color table and pixel-format
 * flag for CopyFrontendColors. Stores nothing when registration fails. */
void xvt_render_assets_register_frontend_image(
	const struct image_resource *image, const char *path, int make_palette,
	int pixel_format_555);
/* Host-thread asset preparation copies registry-owned data before marking the frame consumed. */
/* Copies the stored colors of BMP source id, retired or not. Returns 1 on success; 0 before
 * Init, for id 0 or NULL colors, or when id is not a registered BMP. */
int xvt_render_assets_copy_frontend_colors(
	uint64_t id, struct xvt_frontend_image_colors *colors);
/* Registers a non-model source (image, font, cursor, panel, icon) keyed by owner, or by handle when owner is
 * NULL, and returns its id. Returns 0 before Init, when owner and handle are both 0, or on failure. */
uint64_t xvt_render_assets_register_image(const void *owner, uint16_t handle,
					  const char *path,
					  xvt_snap_image_kind kind,
					  uint32_t first, uint32_t count,
					  uint16_t point_size,
					  uint8_t row_bytes, int make_palette);
/* Original pointer keys are used only during capture, never by the remaster. */
/* Copies the live HUD layout into the cockpit layout: elements 0-287, the 28 cockpit resource
 * descriptors, the panel sprite info and span masks 0 and 1; with auxiliary, elements 288-431
 * and span mask 2 instead. Marks the layout valid and raises the layout and resource
 * generations. */
void xvt_render_assets_capture_cockpit(int auxiliary);
/* Registers the cockpit LFD file whose entry table is entries, with its resource's viewport,
 * and binds it to that resource's descriptor. A resource without a handle registers under the
 * flight scratch screen buffer's handle. Does nothing when entries is not one of the 28 resources. */
void xvt_render_assets_register_lfd(const char *path, uint8_t **entries);
/* Registers the panel sprite file for panel slots first to first + count - 1 of 265, binding
 * slot first + i to frame skip + i; when first is 0 it also sets the layout's panel asset. Does nothing
 * when count is 0 or the range leaves the 265 slots. */
void xvt_render_assets_register_panel(const char *path, uint16_t first_sprite,
				      uint16_t count, uint16_t skip);
/* Registers the map icon file and binds icon i to frame i for i below count; icons past count
 * keep their old binding, except a binding to a source this call retires (the same frames table
 * registered again with other parameters): retiring a source clears every icon bound to it. Does
 * nothing for 0. More than XVT_SNAP_MAP_ICON_FRAMES logs an error and requests a fatal error. */
void xvt_render_assets_register_icons(const char *path, uint8_t **frames,
				      uint16_t count);
/* Registers the micro and small software flight fonts, and the medium one except at 320x240. */
void xvt_render_assets_register_flight_fonts(void);
/* Returns the source id bound to map icon index and stores its frame in *frame when frame is
 * not NULL; 0 and frame 0 for an unbound or out-of-range index. */
uint64_t xvt_render_assets_map_icon_frame(unsigned index, uint32_t *frame);
/* Returns the built-in default cursor bitmap. */
const uint8_t *xvt_render_assets_default_cursor(void);

#ifdef __cplusplus
}
#endif
#endif
