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
typedef enum XvtAssetSyncResult {
	XVT_ASSET_SYNC_FAILED = -1,
	XVT_ASSET_SYNC_COMPLETE,
	XVT_ASSET_SYNC_MORE
} XvtAssetSyncResult;

/* mesh: the scene mesh. component_count: its mesh slots. bridge_component: the B-wing bridge slot the
 * model builder found, or -1. */
typedef struct XvtMeshAsset {
	AeronSceneMesh* mesh;
	uint32_t component_count;
	int32_t bridge_component;
} XvtMeshAsset;

/* Loads the OPT mesh policy. Returns 0, logging the error, when it fails. */
int XvtRemasterAssets_Init(void);
/* Releases every image, the mesh cache and the OPT policy. */
void XvtRemasterAssets_Shutdown(void);
/* Whether the snapshot's image asset generation differs from the committed one. */
int XvtRemasterAssets_ImagesNeedSync(const XvtRenderSnapshot* snapshot);
/* Whether the snapshot's texture asset generation differs from the committed one. */
int XvtRemasterAssets_TexturesNeedSync(const XvtRenderSnapshot* snapshot);
/* Opens a batch for the snapshot's image generation and loads every image asset not yet held (decoded,
 * its fonts built, its default atlases built: no color key, and key 0 for LFD, PNL and ICO), marking
 * held ones seen and refreshing the default palette of those that are not frontend images. Returns 1
 * when no sync is needed; 0, marking cmd failed, while a batch is open or when a load fails. */
int XvtRemasterAssets_SyncImages(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot);
/* The same for the texture assets: ACT files, one default atlas each. */
int XvtRemasterAssets_SyncTextures(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot);
/* Ends the image batch: releases held images the batch did not see, commits every variant, adopts the
 * batch's palette and generation. Without a batch, only commits variants. */
void XvtRemasterAssets_CommitImages(void);
/* Ends the texture batch likewise. */
void XvtRemasterAssets_CommitTextures(void);
/* Releases the assets and variants added since the last commit, restores the committed palette, ends
 * both batches, and aborts the mesh batch. */
void XvtRemasterAssets_Abort(void);
/* Prepare outside passes; palette and both key indices are part of atlas identity.
 * UINT16_MAX means no color key. NULL palette selects the source's default. */
/* Returns the variant's atlas, built with cmd when absent. NULL for an unknown or frameless asset, or,
 * when building, a NULL cmd, an allocation failure, or a failed build, cmd marked failed. */
const AeronRuntimeAtlas* XvtRemasterAssets_PrepareImage(AeronCommandBuffer* cmd, uint64_t id,
														const uint32_t palette[256], uint16_t key,
														uint16_t key_alt);
/* The committed variant's atlas, or NULL. */
const AeronRuntimeAtlas* XvtRemasterAssets_Image(uint64_t id, const uint32_t palette[256], uint16_t key,
												 uint16_t key_alt);
/* The asset's foreground or shadow font once committed and loaded, else NULL. */
const XvtFontAtlas* XvtRemasterAssets_Font(uint64_t id, int shadow);
/* The committed frontend variant for the sprite's asset, kind and tint, or NULL. */
const AeronRuntimeAtlas* XvtRemasterAssets_FindFrontendImage(const XvtSnapSprite* sprite);
/* Builds the sprite's frontend variant when absent: no mips, nearest sampling; a tinted sprite recolors
 * the palette by its 16-bit tint; opaque sprites and the built-in cursor keep index 0, others key it
 * out. Returns 0 for an unknown or frameless asset or a failed build. */
int XvtRemasterAssets_PrepareFrontendImage(AeronCommandBuffer* cmd, const XvtSnapSprite* sprite);
/* Borrowed CPU source, also available during its pending upload batch. */
/* NULL for an unknown asset. */
const XvtOriginal2d* XvtRemasterAssets_FindDecodedImage(uint64_t id);
/* The asset's map-icon atlas under palette and remap, built when no variant matches on the indices the
 * icons use. NULL for an asset without panel records or a failed build. */
const AeronRuntimeAtlas* XvtRemasterAssets_PrepareMapIcons(AeronCommandBuffer* cmd, uint64_t id,
														   const uint32_t palette[256], int remap);
/* Whether the snapshot's OPT generation or the model build policy (smoothing angle, emissive strength)
 * differs from the committed cache. */
int XvtRemasterShip_AssetsNeedSync(const XvtRenderSnapshot* snapshot);
/* Loads the snapshot's OPT models not yet pending: reusing committed meshes when the policy is
 * unchanged, else building them; stops with MORE once the staged bytes or copies reach a nonzero
 * budget while models remain. Returns COMPLETE when nothing is needed or every model is pending;
 * FAILED for a NULL argument (cmd unmarked); FAILED, marking cmd, for an open batch, more unique paths
 * than XVT_SNAP_ASSETS, a build failure or a failed upload-usage query. */
XvtAssetSyncResult XvtRemasterShip_SyncAssets(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot,
											  uint64_t byte_budget, uint32_t copy_budget);
/* Ends the batch. When it completed, the pending set replaces the committed one (meshes no longer used
 * are destroyed) and the generation and policy are adopted; after MORE the pending meshes stay for
 * the next call. */
void XvtRemasterShip_CommitSyncBatch(void);
/* Destroys the meshes built in the pending batch and ends it. */
void XvtRemasterShip_Abort(void);
/* Aborts and destroys every mesh. */
void XvtRemasterShip_Shutdown(void);
/* The committed mesh for the snapshot's OPT asset id, matched by path; NULL when unknown or not
 * committed. */
const XvtMeshAsset* XvtRemasterShip_Mesh(const XvtRenderSnapshot* snapshot, uint64_t id);
#ifdef __cplusplus
}
#endif
#endif
