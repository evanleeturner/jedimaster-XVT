#include "xvt_remaster/sky.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/scene/image_cache.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/effects.h"
#include "xvt_remaster/hyperspace.h"
#include "xvt_remaster/sky_stars.h"

static struct xvt_remaster_sky_stars *g_stars;
static AeronTexture *g_cube;
static char g_cube_path[XVT_SNAP_PATH];
static int g_draw_hyperspace, g_draw_stars;

static void background(AeronCommandBuffer *cmd, AeronRenderPass *pass, int w,
		       int h, void *user)
{
	(void)user;
	if (g_draw_hyperspace) {
		xvt_hyperspace_draw(cmd, pass, w, h, NULL);
	} else if (g_draw_stars) {
		xvt_remaster_sky_stars_draw(cmd, pass, w, h, g_stars);
	}
}

static void backdrops(AeronScene3D *scene, const struct xvt_render_snapshot *s,
		      const struct xvt_render_view *view)
{
	if (!s->sky.backdrop_enabled) {
		return;
	}
	const struct xvt_snap_camera *cam = &s->camera;
	unsigned record = 0;
	static const unsigned axes[3][3] = {{1, 0, 2}, {0, 1, 2}, {2, 1, 0}};
	/* BoP's atlas UV order starts at positive right/up. */
	static const int sx[4] = {1, -1, -1, 1}, sy[4] = {1, 1, -1, -1};
	float fx = ldexpf(1, cam->perspective_shift & 31),
	      aspect = cam->aspect_y_q16 && cam->aspect_y_q16 != UINT16_MAX
			       ? (float)cam->aspect_y_q16 / 65536.0f
			       : 1;
	for (unsigned face = 0; face < 6; ++face) {
		unsigned main = axes[face / 2][0], low = axes[face / 2][1],
			 high = axes[face / 2][2];
		float sign = face & 1 ? -1 : 1;
		unsigned count = s->sky.direction_counts[face];
		for (unsigned i = 0; i < count && record < XVT_SNAP_BACKDROPS;
		     ++i, ++record) {
			unsigned bits = s->sky.backdrop_directions[record],
				 type = s->sky.backdrop_types[record];
			struct xvt_effect_frame frame;
			if (!xvt_effects_frame(s, type, 0, &frame)) {
				continue;
			}
			float dir[3] = {0};
			dir[main] = sign * 8.0f;
			dir[low] = (bits & 8 ? -1 : 1) * (float)(bits & 7);
			dir[high] = (bits & 128 ? -1 : 1) *
				    (float)((bits >> 4) & 7);
			float length = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] +
					     dir[2] * dir[2]);
			for (unsigned a = 0; a < 3; ++a) {
				dir[a] /= length;
			}
			const float *m = view->view_proj;
			if (m[12] * dir[0] + m[13] * dir[1] + m[14] * dir[2] <=
			    1e-4f) {
				continue;
			}
			/* OpenTIE's fixed tangent frame: project the face's reference axis
			 * onto the tile plane so every texel keeps its sky direction. */
			float reference[3] = {0};
			reference[face / 2 == 1 ? 1 : 0] = 1;
			float up[3] = {
				dir[1] * reference[2] - dir[2] * reference[1],
				dir[2] * reference[0] - dir[0] * reference[2],
				dir[0] * reference[1] - dir[1] * reference[0],
			};
			length = sqrtf(up[0] * up[0] + up[1] * up[1] +
				       up[2] * up[2]);
			/* The face normal and reference axis are always distinct. */
			for (unsigned a = 0; a < 3; ++a) {
				up[a] /= length;
			}
			float right[3] = {
				up[1] * dir[2] - up[2] * dir[1],
				up[2] * dir[0] - up[0] * dir[2],
				up[0] * dir[1] - up[1] * dir[0],
			};
			/* Classic pixel dimensions define fixed angular half-extents. */
			float hw = frame.width * 0.5f / fx,
			      hh = frame.height * 0.5f / (fx * aspect);
			AeronSceneBillboardDesc b = {
				.stage = AERON_SCENE_BILLBOARD_STAGE_SKY};
			/* The main view's origin is the camera; SKY forces far depth. */
			for (unsigned c = 0; c < 4; ++c) {
				for (unsigned a = 0; a < 3; ++a) {
					b.corners[c][a] =
						65536.0f *
						(dir[a] +
						 right[a] * sx[c] * hw +
						 up[a] * sy[c] * hh);
				}
			}
			xvt_effects_set_frame(&b, &frame, 1, 1);
			AeronScene_AddBillboard(scene, &b);
		}
	}
}

int xvt_sky_prepare(AeronCommandBuffer *cmd, AeronScene3D *scene,
		    const struct xvt_render_snapshot *s,
		    const struct xvt_render_view *view)
{
	const struct xvt_sky_settings *p =
		&xvt_remaster_config_effective()->sky;
	g_draw_hyperspace =
		s->hyperspace.phase == XVT_SNAP_HYPERSPACE_TRANSITION;
	g_draw_stars = 0;
	AeronScene_SetSkyCube(scene, NULL, NULL, 1);
	AeronScene_SetPassHook(scene, AERON_SCENE_HOOK_BEFORE_OPAQUE,
			       background, NULL);
	if (!xvt_hyperspace_prepare(cmd, s, scene, view)) {
		return 0;
	}
	if (g_draw_hyperspace) {
		return 1;
	}
	if (p->enabled && p->mode == XVT_SKY_CUBE) {
		if (!g_cube || strcmp(g_cube_path, p->path)) {
			AeronTexture *cube = Aeron_ImageLoadCubemapKtx2Vfs(
				cmd, Aeron_GetVfs(), AERON_VFS_ROOT_ASSET,
				p->path, 256u * 1024u * 1024u);
			if (!cube) {
				return 0;
			}
			Aeron_DestroyTexture(g_cube);
			g_cube = cube;
			snprintf(g_cube_path, sizeof g_cube_path, "%s",
				 p->path);
		}
		AeronScene_SetSkyCube(scene, g_cube, NULL, p->exposure);
	} else if (p->enabled) {
		if (!g_stars) {
			g_stars = xvt_remaster_sky_stars_create();
		}
		if (!g_stars) {
			return 0;
		}
		struct xvt_remaster_sky_stars_params params = {
			.exposure = p->exposure,
			.brightness = p->star_brightness,
			.classic_pixel_scale = view->classic_pixel_scale,
			.density_divisor = s->sky.star_grid_divisor,
		};
		if (!xvt_remaster_sky_stars_prepare(g_stars, cmd, scene,
						    &params)) {
			return 0;
		}
		g_draw_stars = 1;
	}
	backdrops(scene, s, view);
	return 1;
}

void xvt_sky_shutdown(void)
{
	xvt_remaster_sky_stars_destroy(g_stars);
	g_stars = NULL;
	Aeron_DestroyTexture(g_cube);
	g_cube = NULL;
	g_cube_path[0] = 0;
	xvt_hyperspace_shutdown();
}
