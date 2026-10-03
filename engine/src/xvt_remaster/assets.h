#ifndef XVT_REMASTER_ASSETS_H
#define XVT_REMASTER_ASSETS_H
#include "aeron/scene/mesh.h"
#include "xvt_remaster/original_2d.h"

#ifdef __cplusplus
extern "C" {
#endif
/* The image cache: every original 2D image and texture the snapshot lists, decoded once per asset id
 * and kept across generations with its variants (atlases per palette and color keys, frontend variants
 * per sprite kind and tint, map-icon variants per palette and remap) and its fonts; synced in upload
 * batches that are committed after submission or aborted. The OPT mesh cache (ship_assets.c) is
 * declared here too, under the XvtRemasterShip_ names. */

/* FAILED: the command buffer is marked failed. COMPLETE: every asset is pending or resident. MORE: the
 * upload budget stopped the batch; submit, commit and call again. */
typedef enum xvt_asset_sync_result {
	XVT_ASSET_SYNC_FAILED = -1,
	XVT_ASSET_SYNC_COMPLETE,
	XVT_ASSET_SYNC_MORE
} xvt_asset_sync_result;

/* mesh: the scene mesh. component_count: its mesh slots. bridge_component: the B-wing bridge slot the
 * model builder found, or -1. */
struct xvt_mesh_asset {
	AeronSceneMesh *mesh;
	uint32_t component_count;
	int32_t bridge_component;
};

/* Loads the OPT mesh policy. Returns 0, logging the error, when it fails. */
int xvt_remaster_assets_init(void);
/* Releases every image, the mesh cache and the OPT policy. */
void xvt_remaster_assets_shutdown(void);
/* Whether the snapshot's image asset generation differs from the committed one. */
int xvt_remaster_assets_images_need_sync(
	const struct xvt_render_snapshot *snapshot);
/* Whether the snapshot's texture asset generation differs from the committed one. */
int xvt_remaster_assets_textures_need_sync(
	const struct xvt_render_snapshot *snapshot);
/* Opens a batch for the snapshot's image generation and loads every image asset not yet held (decoded,
 * its fonts built, its default atlases built: no color key, and key 0 for LFD, PNL and ICO), marking
 * held ones seen and refreshing the default palette of those that are not frontend images. Returns 1
 * when no sync is needed; 0, marking cmd failed, while a batch is open or when a load fails. */
int xvt_remaster_assets_sync_images(AeronCommandBuffer *cmd,
				    const struct xvt_render_snapshot *snapshot);
/* The same for the texture assets: ACT files, one default atlas each. */
int xvt_remaster_assets_sync_textures(
	AeronCommandBuffer *cmd, const struct xvt_render_snapshot *snapshot);
/* Ends the image batch: releases held images the batch did not see, commits every variant, adopts the
 * batch's palette and generation. Without a batch, only commits variants. */
void xvt_remaster_assets_commit_images(void);
/* Ends the texture batch likewise. */
void xvt_remaster_assets_commit_textures(void);
/* Releases the assets and variants added since the last commit, restores the committed palette, ends
 * both batches, and aborts the mesh batch. */
void xvt_remaster_assets_abort(void);
/* Prepare outside passes; palette and both key indices are part of atlas identity.
 * UINT16_MAX means no color key. NULL palette selects the source's default. */
/* Returns the variant's atlas, built with cmd when absent. NULL for an unknown or frameless asset, or,
 * when building, a NULL cmd, an allocation failure, or a failed build, cmd marked failed. */
const AeronRuntimeAtlas *
xvt_remaster_assets_prepare_image(AeronCommandBuffer *cmd, uint64_t id,
				  const uint32_t palette[256], uint16_t key,
				  uint16_t key_alt);
/* The committed variant's atlas, or NULL. */
const AeronRuntimeAtlas *xvt_remaster_assets_image(uint64_t id,
						   const uint32_t palette[256],
						   uint16_t key,
						   uint16_t key_alt);
/* The asset's foreground or shadow font once committed and loaded, else NULL. */
const struct xvt_font_atlas *xvt_remaster_assets_font(uint64_t id, int shadow);
/* The committed frontend variant for the sprite's asset, kind and tint, or NULL. */
const AeronRuntimeAtlas *
xvt_remaster_assets_find_frontend_image(const struct xvt_snap_sprite *sprite);
/* Builds the sprite's frontend variant when absent: no mips, nearest sampling; a tinted sprite recolors
 * the palette by its 16-bit tint; opaque sprites and the built-in cursor keep index 0, others key it
 * out. Returns 0 for an unknown or frameless asset or a failed build. */
int xvt_remaster_assets_prepare_frontend_image(
	AeronCommandBuffer *cmd, const struct xvt_snap_sprite *sprite);
/* Borrowed CPU source, also available during its pending upload batch. */
/* NULL for an unknown asset. */
const struct xvt_original2d *
xvt_remaster_assets_find_decoded_image(uint64_t id);
/* The asset's map-icon atlas under palette and remap, built when no variant matches on the indices the
 * icons use. NULL for an asset without panel records or a failed build. */
const AeronRuntimeAtlas *
xvt_remaster_assets_prepare_map_icons(AeronCommandBuffer *cmd, uint64_t id,
				      const uint32_t palette[256], int remap);
/* Whether the snapshot's OPT generation or the model build policy (smoothing angle, emissive strength)
 * differs from the committed cache. */
int xvt_remaster_ship_assets_need_sync(
	const struct xvt_render_snapshot *snapshot);
/* Loads the snapshot's OPT models not yet pending: reusing committed meshes when the policy is
 * unchanged, else building them; stops with MORE once the staged bytes or copies reach a nonzero
 * budget while models remain. Returns COMPLETE when nothing is needed or every model is pending;
 * FAILED for a NULL argument (cmd unmarked); FAILED, marking cmd, for an open batch, more unique paths
 * than XVT_SNAP_ASSETS, a build failure or a failed upload-usage query. */
xvt_asset_sync_result
xvt_remaster_ship_sync_assets(AeronCommandBuffer *cmd,
			      const struct xvt_render_snapshot *snapshot,
			      uint64_t byte_budget, uint32_t copy_budget);
/* Ends the batch. When it completed, the pending set replaces the committed one (meshes no longer used
 * are destroyed) and the generation and policy are adopted; after MORE the pending meshes stay for
 * the next call. */
void xvt_remaster_ship_commit_sync_batch(void);
/* Destroys the meshes built in the pending batch and ends it. */
void xvt_remaster_ship_abort(void);
/* Aborts and destroys every mesh. */
void xvt_remaster_ship_shutdown(void);
/* The committed mesh for the snapshot's OPT asset id, matched by path; NULL when unknown or not
 * committed. */
const struct xvt_mesh_asset *
xvt_remaster_ship_mesh(const struct xvt_render_snapshot *snapshot, uint64_t id);
#ifdef __cplusplus
}
#endif
#endif
