#ifndef XVT_REMASTER_ASSETS_H
#define XVT_REMASTER_ASSETS_H
#include "aeron/scene/mesh.h"
#include "xvt_remaster/original_2d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XvtAssetSyncResult {
	XVT_ASSET_SYNC_FAILED = -1,
	XVT_ASSET_SYNC_COMPLETE,
	XVT_ASSET_SYNC_MORE
} XvtAssetSyncResult;

typedef struct XvtMeshAsset {
	AeronSceneMesh* mesh;
	uint32_t component_count;
	int32_t bridge_component;
} XvtMeshAsset;

int XvtRemasterAssets_Init(void);
void XvtRemasterAssets_Shutdown(void);
int XvtRemasterAssets_ImagesNeedSync(const XvtRenderSnapshot* snapshot);
int XvtRemasterAssets_TexturesNeedSync(const XvtRenderSnapshot* snapshot);
int XvtRemasterAssets_SyncImages(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot);
int XvtRemasterAssets_SyncTextures(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot);
void XvtRemasterAssets_CommitImages(void);
void XvtRemasterAssets_CommitTextures(void);
void XvtRemasterAssets_Abort(void);
/* Prepare outside passes; palette and both key indices are part of atlas identity.
 * UINT16_MAX means no color key. NULL palette selects the source's default. */
const AeronRuntimeAtlas* XvtRemasterAssets_PrepareImage(AeronCommandBuffer* cmd, uint64_t id,
														const uint32_t palette[256], uint16_t key,
														uint16_t key_alt);
const AeronRuntimeAtlas* XvtRemasterAssets_Image(uint64_t id, const uint32_t palette[256], uint16_t key,
												 uint16_t key_alt);
const XvtFontAtlas* XvtRemasterAssets_Font(uint64_t id, int shadow);
const AeronRuntimeAtlas* XvtRemasterAssets_FindFrontendImage(const XvtSnapSprite* sprite);
int XvtRemasterAssets_PrepareFrontendImage(AeronCommandBuffer* cmd, const XvtSnapSprite* sprite);
/* Borrowed CPU source, also available during its pending upload batch. */
const XvtOriginal2d* XvtRemasterAssets_FindDecodedImage(uint64_t id);
const AeronRuntimeAtlas* XvtRemasterAssets_PrepareMapIcons(AeronCommandBuffer* cmd, uint64_t id,
														   const uint32_t palette[256], int remap);
int XvtRemasterShip_AssetsNeedSync(const XvtRenderSnapshot* snapshot);
XvtAssetSyncResult XvtRemasterShip_SyncAssets(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot,
											  uint64_t byte_budget, uint32_t copy_budget);
void XvtRemasterShip_CommitSyncBatch(void);
void XvtRemasterShip_Abort(void);
void XvtRemasterShip_Shutdown(void);
const XvtMeshAsset* XvtRemasterShip_Mesh(const XvtRenderSnapshot* snapshot, uint64_t id);
#ifdef __cplusplus
}
#endif
#endif
