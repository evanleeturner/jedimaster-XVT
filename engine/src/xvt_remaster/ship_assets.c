#include "aeron/aeron.h"
#include "xvt_remaster/assets.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/opt_mesh.h"
#include <stdio.h>
#include <string.h>

typedef struct MeshEntry {
	char path[XVT_SNAP_PATH];
	XvtMeshAsset asset;
	int pending_new;
} MeshEntry;

static MeshEntry g_meshes[XVT_SNAP_ASSETS], g_pending[XVT_SNAP_ASSETS];
static char g_desired[XVT_SNAP_ASSETS][XVT_SNAP_PATH];
static unsigned g_count, g_pendingCount;
static uint64_t g_generation = UINT64_MAX, g_pendingGeneration = UINT64_MAX;
static int g_batchActive, g_batchCompletes;
static XvtModelSettings g_policy, g_pendingPolicy;

static int SamePolicy(const XvtModelSettings* a, const XvtModelSettings* b) {
	/* Projectile/glow strengths are submission settings, not mesh build inputs. */
	return a->smooth_angle_degrees == b->smooth_angle_degrees &&
		   a->opt_emissive_strength == b->opt_emissive_strength;
}

static void Destroy(MeshEntry* entry) {
	if (entry->asset.mesh)
		AeronScene_MeshDestroy(entry->asset.mesh);
	memset(entry, 0, sizeof *entry);
}

static void DiscardPending(void) {
	for (unsigned i = 0; i < g_pendingCount; ++i)
		if (g_pending[i].pending_new)
			Destroy(&g_pending[i]);
	memset(g_pending, 0, sizeof g_pending);
	g_pendingCount = 0;
	g_pendingGeneration = UINT64_MAX;
}

int XvtRemasterShip_AssetsNeedSync(const XvtRenderSnapshot* snapshot) {
	return snapshot && (snapshot->opt_asset_generation != g_generation ||
						!SamePolicy(&g_policy, &XvtRemasterConfig_Effective()->models));
}

static int LoadMesh(AeronCommandBuffer* cmd, MeshEntry* entry) {
	AeronFlightModel model = { 0 };
	char error[256];
	if (!XvtRemasterOptMesh_Build(Aeron_GetVfs(), entry->path, &g_pendingPolicy, &model, error,
								  sizeof error)) {
		Aeron_LogError("xvt.remaster", "OPT '%s': %s", entry->path, error);
		Aeron_CommandBufferSetFailure(cmd, error);
		return 0;
	}
	AeronSceneMeshCreateStatus status;
	entry->asset.mesh = AeronScene_MeshCreate(cmd, &model, entry->path, &status);
	entry->asset.component_count = model.component_count;
	entry->asset.bridge_component = model.bridge_component;
	/* Aeron retains mesh slots, rotations, bounds, material variants and engine glows. */
	Aeron_FlightModelFree(&model);
	if (!entry->asset.mesh) {
		Aeron_LogError("xvt.remaster", "OPT '%s': GPU creation failed (%d)", entry->path, status);
		Aeron_CommandBufferSetFailure(cmd, "OPT GPU creation failed");
		return 0;
	}
	return 1;
}

XvtAssetSyncResult XvtRemasterShip_SyncAssets(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot,
											  uint64_t byte_budget, uint32_t copy_budget) {
	if (!cmd || !snapshot)
		return XVT_ASSET_SYNC_FAILED;
	if (!XvtRemasterShip_AssetsNeedSync(snapshot))
		return XVT_ASSET_SYNC_COMPLETE;
	if (g_batchActive) {
		Aeron_CommandBufferSetFailure(cmd, "unfinished mesh upload batch");
		return XVT_ASSET_SYNC_FAILED;
	}
	const XvtModelSettings* policy = &XvtRemasterConfig_Effective()->models;
	if (g_pendingGeneration != snapshot->opt_asset_generation || !SamePolicy(&g_pendingPolicy, policy)) {
		DiscardPending();
		g_pendingGeneration = snapshot->opt_asset_generation;
		g_pendingPolicy = *policy;
	}
	unsigned desired_count = 0;
	for (unsigned i = 0; i < snapshot->opt_asset_count; ++i) {
		const char* path = snapshot->opt_assets[i].path;
		unsigned j;
		for (j = 0; j < desired_count; ++j)
			if (strcmp(g_desired[j], path) == 0)
				break;
		if (j < desired_count)
			continue;
		if (desired_count == XVT_SNAP_ASSETS) {
			Aeron_CommandBufferSetFailure(cmd, "mesh cache capacity exceeded");
			return XVT_ASSET_SYNC_FAILED;
		}
		snprintf(g_desired[desired_count++], XVT_SNAP_PATH, "%s", path);
	}
	g_batchActive = 1;
	g_batchCompletes = 1;
	for (unsigned i = 0; i < desired_count; ++i) {
		unsigned found;
		for (found = 0; found < g_pendingCount; ++found)
			if (strcmp(g_pending[found].path, g_desired[i]) == 0)
				break;
		if (found < g_pendingCount)
			continue;
		MeshEntry* entry = &g_pending[g_pendingCount];
		if (SamePolicy(&g_policy, policy)) {
			for (unsigned j = 0; j < g_count; ++j) {
				if (strcmp(g_meshes[j].path, g_desired[i]) != 0)
					continue;
				*entry = g_meshes[j];
				entry->pending_new = 0;
				++g_pendingCount;
				break;
			}
		}
		if (g_pendingCount > found)
			continue;
		snprintf(entry->path, sizeof entry->path, "%s", g_desired[i]);
		if (!LoadMesh(cmd, entry)) {
			memset(entry, 0, sizeof *entry);
			return XVT_ASSET_SYNC_FAILED;
		}
		entry->pending_new = 1;
		++g_pendingCount;
		AeronCommandBufferUploadUsage usage;
		if (!Aeron_CommandBufferGetUploadUsage(cmd, &usage)) {
			Aeron_CommandBufferSetFailure(cmd, "mesh upload usage query failed");
			return XVT_ASSET_SYNC_FAILED;
		}
		if ((byte_budget && usage.staged_bytes >= byte_budget) ||
			(copy_budget && usage.copy_count >= copy_budget)) {
			for (unsigned j = i + 1; j < desired_count; ++j) {
				unsigned k;
				for (k = 0; k < g_pendingCount; ++k)
					if (strcmp(g_pending[k].path, g_desired[j]) == 0)
						break;
				if (k == g_pendingCount) {
					g_batchCompletes = 0;
					return XVT_ASSET_SYNC_MORE;
				}
			}
		}
	}
	return XVT_ASSET_SYNC_COMPLETE;
}

void XvtRemasterShip_CommitSyncBatch(void) {
	if (!g_batchActive)
		return;
	if (g_batchCompletes) {
		for (unsigned i = 0; i < g_count; ++i) {
			unsigned j;
			for (j = 0; j < g_pendingCount; ++j)
				if (g_meshes[i].asset.mesh == g_pending[j].asset.mesh)
					break;
			if (j == g_pendingCount)
				Destroy(&g_meshes[i]);
		}
		memset(g_meshes, 0, sizeof g_meshes);
		g_count = g_pendingCount;
		for (unsigned i = 0; i < g_count; ++i) {
			g_meshes[i] = g_pending[i];
			g_meshes[i].pending_new = 0;
		}
		g_generation = g_pendingGeneration;
		g_policy = g_pendingPolicy;
		memset(g_pending, 0, sizeof g_pending);
		g_pendingCount = 0;
		g_pendingGeneration = UINT64_MAX;
		Aeron_LogDebug("xvt.remaster", "mesh assets: generation=%llu unique=%u",
					   (unsigned long long)g_generation, g_count);
	}
	g_batchActive = g_batchCompletes = 0;
}

void XvtRemasterShip_Abort(void) {
	DiscardPending();
	g_batchActive = g_batchCompletes = 0;
}

void XvtRemasterShip_Shutdown(void) {
	XvtRemasterShip_Abort();
	for (unsigned i = 0; i < g_count; ++i)
		Destroy(&g_meshes[i]);
	g_count = 0;
	g_generation = UINT64_MAX;
	memset(&g_policy, 0, sizeof g_policy);
}

const XvtMeshAsset* XvtRemasterShip_Mesh(const XvtRenderSnapshot* snapshot, uint64_t id) {
	if (!snapshot || !id)
		return NULL;
	for (unsigned i = 0; i < snapshot->opt_asset_count; ++i) {
		if (snapshot->opt_assets[i].id != id)
			continue;
		for (unsigned j = 0; j < g_count; ++j)
			if (strcmp(g_meshes[j].path, snapshot->opt_assets[i].path) == 0)
				return &g_meshes[j].asset;
		break;
	}
	return NULL;
}
