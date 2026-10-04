#include <stdio.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt_remaster/assets.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/opt_mesh.h"
#include "xvt_runtime/log/log.h"

struct mesh_entry {
	char path[XVT_SNAP_PATH];
	struct xvt_mesh_asset asset;
	int pending_new;
};

static struct mesh_entry g_meshes[XVT_SNAP_ASSETS];
static struct mesh_entry g_pending[XVT_SNAP_ASSETS];
static char g_desired[XVT_SNAP_ASSETS][XVT_SNAP_PATH];
static unsigned g_count;
static unsigned g_pending_count;
static uint64_t g_generation = UINT64_MAX;
static uint64_t g_pending_generation = UINT64_MAX;
static int g_batch_active;
static int g_batch_completes;
static struct xvt_model_settings g_policy;
static struct xvt_model_settings g_pending_policy;

static int same_policy(const struct xvt_model_settings *a,
		       const struct xvt_model_settings *b)
{
	/* Projectile/glow strengths are submission settings, not mesh build inputs. */
	return a->smooth_angle_degrees == b->smooth_angle_degrees &&
	       a->opt_emissive_strength == b->opt_emissive_strength;
}

static void destroy(struct mesh_entry *entry)
{
	if (entry->asset.mesh) {
		AeronScene_MeshDestroy(entry->asset.mesh);
	}
	memset(entry, 0, sizeof *entry);
}

static void discard_pending(void)
{
	for (unsigned i = 0; i < g_pending_count; ++i) {
		if (g_pending[i].pending_new) {
			destroy(&g_pending[i]);
		}
	}
	memset(g_pending, 0, sizeof g_pending);
	g_pending_count = 0;
	g_pending_generation = UINT64_MAX;
}

int xvt_remaster_ship_assets_need_sync(
	const struct xvt_render_snapshot *snapshot)
{
	return snapshot &&
	       (snapshot->opt_asset_generation != g_generation ||
		!same_policy(&g_policy,
			     &xvt_remaster_config_effective()->models));
}

static int load_mesh(AeronCommandBuffer *cmd, struct mesh_entry *entry)
{
	AeronFlightModel model = {0};
	char error[256];
	if (!xvt_remaster_opt_mesh_build(Aeron_GetVfs(), entry->path,
					 &g_pending_policy, &model, error,
					 sizeof error)) {
		XVT_LOG_ERROR("remaster.opt_failed path=\"%s\" error=\"%s\"",
			      entry->path, error);
		Aeron_CommandBufferSetFailure(cmd, error);
		return 0;
	}
	AeronSceneMeshCreateStatus status;
	entry->asset.mesh =
		AeronScene_MeshCreate(cmd, &model, entry->path, &status);
	entry->asset.component_count = model.component_count;
	entry->asset.bridge_component = model.bridge_component;
	/* Aeron retains mesh slots, rotations, bounds, material variants and engine glows. */
	Aeron_FlightModelFree(&model);
	if (!entry->asset.mesh) {
		XVT_LOG_ERROR("remaster.opt_gpu_failed path=\"%s\" status=%d",
			      entry->path, status);
		Aeron_CommandBufferSetFailure(cmd, "OPT GPU creation failed");
		return 0;
	}
	return 1;
}

xvt_asset_sync_result
xvt_remaster_ship_sync_assets(AeronCommandBuffer *cmd,
			      const struct xvt_render_snapshot *snapshot,
			      uint64_t byte_budget, uint32_t copy_budget)
{
	if (!cmd || !snapshot) {
		return XVT_ASSET_SYNC_FAILED;
	}
	if (!xvt_remaster_ship_assets_need_sync(snapshot)) {
		return XVT_ASSET_SYNC_COMPLETE;
	}
	if (g_batch_active) {
		Aeron_CommandBufferSetFailure(cmd,
					      "unfinished mesh upload batch");
		return XVT_ASSET_SYNC_FAILED;
	}
	const struct xvt_model_settings *policy =
		&xvt_remaster_config_effective()->models;
	if (g_pending_generation != snapshot->opt_asset_generation ||
	    !same_policy(&g_pending_policy, policy)) {
		discard_pending();
		g_pending_generation = snapshot->opt_asset_generation;
		g_pending_policy = *policy;
	}
	unsigned desired_count = 0;
	for (unsigned i = 0; i < snapshot->opt_asset_count; ++i) {
		const char *path = snapshot->opt_assets[i].path;
		unsigned j;
		for (j = 0; j < desired_count; ++j) {
			if (strcmp(g_desired[j], path) == 0) {
				break;
			}
		}
		if (j < desired_count) {
			continue;
		}
		if (desired_count == XVT_SNAP_ASSETS) {
			Aeron_CommandBufferSetFailure(
				cmd, "mesh cache capacity exceeded");
			return XVT_ASSET_SYNC_FAILED;
		}
		snprintf(g_desired[desired_count++], XVT_SNAP_PATH, "%s", path);
	}
	g_batch_active = 1;
	g_batch_completes = 1;
	for (unsigned i = 0; i < desired_count; ++i) {
		unsigned found;
		for (found = 0; found < g_pending_count; ++found) {
			if (strcmp(g_pending[found].path, g_desired[i]) == 0) {
				break;
			}
		}
		if (found < g_pending_count) {
			continue;
		}
		struct mesh_entry *entry = &g_pending[g_pending_count];
		if (same_policy(&g_policy, policy)) {
			for (unsigned j = 0; j < g_count; ++j) {
				if (strcmp(g_meshes[j].path, g_desired[i]) !=
				    0) {
					continue;
				}
				*entry = g_meshes[j];
				entry->pending_new = 0;
				++g_pending_count;
				break;
			}
		}
		if (g_pending_count > found) {
			continue;
		}
		snprintf(entry->path, sizeof entry->path, "%s", g_desired[i]);
		if (!load_mesh(cmd, entry)) {
			memset(entry, 0, sizeof *entry);
			return XVT_ASSET_SYNC_FAILED;
		}
		entry->pending_new = 1;
		++g_pending_count;
		AeronCommandBufferUploadUsage usage;
		if (!Aeron_CommandBufferGetUploadUsage(cmd, &usage)) {
			Aeron_CommandBufferSetFailure(
				cmd, "mesh upload usage query failed");
			return XVT_ASSET_SYNC_FAILED;
		}
		if ((byte_budget && usage.staged_bytes >= byte_budget) ||
		    (copy_budget && usage.copy_count >= copy_budget)) {
			for (unsigned j = i + 1; j < desired_count; ++j) {
				unsigned k;
				for (k = 0; k < g_pending_count; ++k) {
					if (strcmp(g_pending[k].path,
						   g_desired[j]) == 0) {
						break;
					}
				}
				if (k == g_pending_count) {
					g_batch_completes = 0;
					return XVT_ASSET_SYNC_MORE;
				}
			}
		}
	}
	return XVT_ASSET_SYNC_COMPLETE;
}

void xvt_remaster_ship_commit_sync_batch(void)
{
	if (!g_batch_active) {
		return;
	}
	if (g_batch_completes) {
		for (unsigned i = 0; i < g_count; ++i) {
			unsigned j;
			for (j = 0; j < g_pending_count; ++j) {
				if (g_meshes[i].asset.mesh ==
				    g_pending[j].asset.mesh) {
					break;
				}
			}
			if (j == g_pending_count) {
				destroy(&g_meshes[i]);
			}
		}
		memset(g_meshes, 0, sizeof g_meshes);
		g_count = g_pending_count;
		for (unsigned i = 0; i < g_count; ++i) {
			g_meshes[i] = g_pending[i];
			g_meshes[i].pending_new = 0;
		}
		g_generation = g_pending_generation;
		g_policy = g_pending_policy;
		memset(g_pending, 0, sizeof g_pending);
		g_pending_count = 0;
		g_pending_generation = UINT64_MAX;
		XVT_LOG_DEBUG("remaster.mesh_assets generation=%llu unique=%u",
			      (unsigned long long)g_generation, g_count);
	}
	g_batch_active = 0;
	g_batch_completes = 0;
}

void xvt_remaster_ship_abort(void)
{
	discard_pending();
	g_batch_active = 0;
	g_batch_completes = 0;
}

void xvt_remaster_ship_shutdown(void)
{
	xvt_remaster_ship_abort();
	for (unsigned i = 0; i < g_count; ++i) {
		destroy(&g_meshes[i]);
	}
	g_count = 0;
	g_generation = UINT64_MAX;
	memset(&g_policy, 0, sizeof g_policy);
}

const struct xvt_mesh_asset *
xvt_remaster_ship_mesh(const struct xvt_render_snapshot *snapshot, uint64_t id)
{
	if (!snapshot || !id) {
		return NULL;
	}
	for (unsigned i = 0; i < snapshot->opt_asset_count; ++i) {
		if (snapshot->opt_assets[i].id != id) {
			continue;
		}
		for (unsigned j = 0; j < g_count; ++j) {
			if (strcmp(g_meshes[j].path,
				   snapshot->opt_assets[i].path) == 0) {
				return &g_meshes[j].asset;
			}
		}
		break;
	}
	return NULL;
}
