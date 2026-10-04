#include "xvt_remaster/effects.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/asset/opt_model.h"
#include "aeron/scene/world.h"
#include "xvt_remaster/assets.h"
#include "xvt_remaster/config.h"

static const float k_tau = 6.2831853071795864769f;

int xvt_effects_frame(const struct xvt_render_snapshot *s, unsigned type,
		      unsigned frame, struct xvt_effect_frame *out)
{
	if (type >= XVT_SNAP_TYPES) {
		return 0;
	}
	const AeronRuntimeAtlas *a = xvt_remaster_assets_image(
		s->types[type].texture_asset_id, NULL, UINT16_MAX, UINT16_MAX);
	if (!a) {
		return 0;
	}
	for (int i = 0; i < a->layout.frame_count; ++i) {
		if ((unsigned)a->layout.ids[i] != frame) {
			continue;
		}
		const AeronRuntimeAtlasPage *page =
			&a->pages[a->layout.pages[i]];
		const AeronSpriteRect *rect = &a->layout.frames[i];
		*out = (struct xvt_effect_frame){
			page->texture,
			rect->x / page->width,
			rect->y / page->height,
			(rect->x + rect->w) / page->width,
			(rect->y + rect->h) / page->height,
			a->layout.classic_w[i],
			a->layout.classic_h[i]};
		return 1;
	}
	return 0;
}

void xvt_effects_quad(const struct xvt_snap_camera *cam, const float center[3],
		      float hw, float hh, float angle, float out[4][3])
{
	static const int sx[4] = {1, -1, -1, 1};
	static const int sy[4] = {1, 1, -1, -1};
	float c = cosf(angle);
	float sn = sinf(angle);
	float aspect = cam->aspect_y_q16 && cam->aspect_y_q16 != UINT16_MAX
			       ? (float)cam->aspect_y_q16 / 65536.0f
			       : 1;
	for (int corner = 0; corner < 4; ++corner) {
		float x = c * sx[corner] * hw + sn * sy[corner] * hh * aspect;
		float y = c * sy[corner] * hh - sn * sx[corner] * hw / aspect;
		for (int axis = 0; axis < 3; ++axis) {
			out[corner][axis] = center[axis] + cam->rows[axis] * x +
					    cam->rows[3 + axis] * y;
		}
	}
}

void xvt_effects_set_frame(AeronSceneBillboardDesc *b,
			   const struct xvt_effect_frame *f, float strength,
			   float alpha)
{
	b->texture = f->texture;
	b->blend = AERON_SCENE_BILLBOARD_BLEND_ALPHA;
	const float uv[4][2] = {
		{f->u0, f->v0}, {f->u1, f->v0}, {f->u1, f->v1}, {f->u0, f->v1}};
	memcpy(b->uv, uv, sizeof uv);
	for (int i = 0; i < 4; ++i) {
		b->colors[i][2] = strength;
		b->colors[i][1] = b->colors[i][2];
		b->colors[i][0] = b->colors[i][1];
		b->colors[i][3] = alpha;
	}
}

static float angle(const struct xvt_snap_object *o,
		   const struct xvt_snap_camera *cam)
{
	float m[16];
	float r[2][3];
	xvt_render_math_object_matrix(o, cam->world_pos, m);
	for (int col = 0; col < 2; ++col) {
		for (int row = 0; row < 3; ++row) {
			r[col][row] = cam->rows[row * 3] * m[col] +
				      cam->rows[row * 3 + 1] * m[4 + col] +
				      cam->rows[row * 3 + 2] * m[8 + col];
		}
	}
	const float *v = fabsf(r[1][2]) > fabsf(r[0][2]) ? r[0] : r[1];
	return (v[0] < 0 ? 1.0f : -1.0f) * atan2f(v[1], fabsf(v[0]));
}

static uint16_t frame_code(const struct xvt_render_snapshot *s,
			   const struct xvt_snap_object *o)
{
	if (o->object_type >= XVT_SNAP_TYPES) {
		return XVT_SNAP_INVALID_TEXTURE_FRAME;
	}
	if (o->object_type == XVT_SNAP_TYPE_COMPONENT) {
		/* QueueObjectTextured tests the resolved source type again for its follow-up. */
		if (o->source_type != XVT_SNAP_TYPE_COMPONENT ||
		    o->type_specific[1] >=
			    s->types[XVT_SNAP_TYPE_COMPONENT_FOLLOWUP]
				    .sequence_count) {
			return XVT_SNAP_INVALID_TEXTURE_FRAME;
		}
		return (uint16_t)s->types[XVT_SNAP_TYPE_COMPONENT_FOLLOWUP]
			.sequence[o->type_specific[1]];
	}
	const struct xvt_snap_type *t = &s->types[o->object_type];
	return o->type_specific[0] < t->sequence_count
		       ? (uint16_t)t->sequence[o->type_specific[0]]
		       : XVT_SNAP_INVALID_TEXTURE_FRAME;
}

static int corners(const struct xvt_render_snapshot *s,
		   const struct xvt_snap_object *o,
		   const struct xvt_snap_camera *cam, const int32_t origin[3],
		   uint16_t code, unsigned base_size, float roll,
		   float corners[4][3], struct xvt_effect_frame *frame)
{
	if (code < XVT_SNAP_TEXTURE_FRAME_BIT ||
	    code >= XVT_SNAP_INVALID_TEXTURE_FRAME) {
		return 0;
	}
	unsigned type = (code & 0x7fff) >> 7;
	unsigned index = code & 0x7f;
	if (!xvt_effects_frame(s, type, index, frame)) {
		return 0;
	}
	float local[3];
	AeronWorld_LocalI32(cam->world_pos, o->world_pos, local);
	float depth = local[0] * cam->rows[6] + local[1] * cam->rows[7] +
		      local[2] * cam->rows[8];
	if (depth <= 0 || (double)depth > INT32_MAX) {
		return 0;
	}
	int d = (int)depth >> 8;
	unsigned q = d ? (uint16_t)s->types[type].max_extent / (unsigned)d : 0;
	unsigned size = (base_size * q) >> 8;
	if (size > 1024) {
		size = 1024;
	}
	float fx = ldexpf(1, cam->perspective_shift & 31);
	float aspect = cam->aspect_y_q16 && cam->aspect_y_q16 != UINT16_MAX
			       ? (float)cam->aspect_y_q16 / 65536.0f
			       : 1;
	float hw = (float)((size * (unsigned)frame->width) >> 9) * depth / fx;
	float hh = (float)((size * (unsigned)frame->height) >> 9) * depth /
		   (fx * aspect);
	if (hw <= 0 || hh <= 0) {
		return 0;
	}
	AeronWorld_LocalI32(origin, o->world_pos, local);
	xvt_effects_quad(cam, local, hw, hh, roll, corners);
	return 1;
}

static const struct xvt_snap_object *
effects_previous(const struct xvt_render_snapshot *p,
		 const struct xvt_snap_object *o)
{
	if (!p) {
		return NULL;
	}
	for (unsigned i = 0; i < p->object_count; ++i) {
		if (p->objects[i].id.slot == o->id.slot &&
		    p->objects[i].id.signature == o->id.signature &&
		    p->objects[i].object_type == o->object_type) {
			return &p->objects[i];
		}
	}
	return NULL;
}

static unsigned base_size(const struct xvt_snap_object *o)
{
	unsigned base = o->light_scale ? o->light_scale << 6 : 256;
	return o->light_scale && base >= 256 ? base + 256 : base;
}

static uint16_t engine_flame_frame_code(const struct xvt_render_snapshot *s,
					const struct xvt_snap_object *o,
					int ordinal)
{
	if (!o->has_craft || o->object_type >= XVT_SNAP_TYPES ||
	    o->id.slot >= s->sky.craft_slot_end) {
		return XVT_SNAP_INVALID_TEXTURE_FRAME;
	}
	const struct xvt_mesh_asset *mesh = xvt_remaster_ship_mesh(
		s, s->types[o->object_type].model_asset_id);
	if (!mesh || mesh->component_count >= XVT_SNAP_COMPONENTS) {
		return XVT_SNAP_INVALID_TEXTURE_FRAME;
	}
	unsigned frame = o->component_state[mesh->component_count];
	if (frame >= 25) {
		return XVT_SNAP_INVALID_TEXTURE_FRAME;
	}
	for (unsigned i = 0; i < mesh->component_count; ++i) {
		if (!o->component_state[i] &&
		    mesh->mesh->mesh_rot[i].mesh_type ==
			    XVT_SNAP_MESH_FUSELAGE) {
			if (--ordinal == 0) {
				return (uint16_t)s->fuselage_sequence[frame];
			}
		}
	}
	return XVT_SNAP_INVALID_TEXTURE_FRAME;
}

static void submit_one(AeronScene3D *scene, const struct xvt_render_snapshot *s,
		       const struct xvt_render_snapshot *p,
		       const struct xvt_snap_object *o,
		       const struct xvt_snap_camera *cam,
		       const struct xvt_snap_camera *pc, uint16_t code,
		       int flame_ordinal, int regenerate)
{
	AeronSceneBillboardDesc b = {
		.stage = AERON_SCENE_BILLBOARD_STAGE_OVERLAY};
	struct xvt_effect_frame frame;
	float roll = angle(o, cam) + (flame_ordinal
					      ? flame_ordinal * (float)o->roll *
							k_tau / 65536.0f
					      : 0);
	if (!corners(s, o, cam, cam->world_pos, code,
		     flame_ordinal ? 256 : base_size(o), roll, b.corners,
		     &frame)) {
		return;
	}
	float alpha = 1;
	if (o->genus == CRAFT_GENUS_EXPLOSION && o->type_specific[0] < 32) {
		static const uint8_t envelope[32] = {
			208, 224, 240, 240, 224, 208, 176, 144, 112, 80, 48,
			48,  48,  48,  48,  48,	 48,  48,  48,	48,  48, 48,
			48,  48,  48,  48,  48,	 48,  48,  48,	48,  48};
		alpha = envelope[o->type_specific[0]] / 255.0f;
	}
	float strength = o->genus == CRAFT_GENUS_EXPLOSION
				 ? xvt_remaster_config_effective()
					   ->explosion_emissive_strength
				 : 1;
	xvt_effects_set_frame(&b, &frame, strength, alpha);
	float previous[4][3];
	const struct xvt_snap_object *old = effects_previous(p, o);
	if (regenerate && old && pc) {
		uint16_t old_code =
			flame_ordinal
				? engine_flame_frame_code(p, old, flame_ordinal)
				: frame_code(p, old);
		float old_roll =
			angle(old, pc) +
			(flame_ordinal ? flame_ordinal * (float)old->roll *
						 k_tau / 65536.0f
				       : 0);
		struct xvt_effect_frame old_frame;
		if (corners(p, old, pc, cam->world_pos, old_code,
			    flame_ordinal ? 256 : base_size(old), old_roll,
			    previous, &old_frame)) {
			b.prev_corners = previous;
		}
	}
	AeronScene_AddBillboard(scene, &b);
}

struct effect_order {
	unsigned index;
	float depth;
};

static int effects_order(const void *left, const void *right)
{
	const struct effect_order *a = left;
	const struct effect_order *b = right;
	if (a->depth != b->depth) {
		return a->depth > b->depth ? -1 : 1;
	}
	return a->index > b->index ? -1 : a->index < b->index;
}

void xvt_effects_submit(AeronScene3D *scene,
			const struct xvt_render_snapshot *s,
			const struct xvt_render_snapshot *p,
			const struct xvt_snap_camera *cam,
			const struct xvt_snap_camera *pc, int regenerate,
			const struct xvt_snap_preview *crt)
{
	if (p && (p->world_generation != s->world_generation ||
		  p->mission_generation != s->mission_generation)) {
		p = NULL;
	}
	struct effect_order order[XVT_SNAP_OBJECTS];
	for (unsigned i = 0; i < s->object_count; ++i) {
		float pos[3];
		AeronWorld_LocalI32(cam->world_pos, s->objects[i].world_pos,
				    pos);
		order[i] = (struct effect_order){
			i, pos[0] * cam->rows[6] + pos[1] * cam->rows[7] +
				   pos[2] * cam->rows[8]};
	}
	qsort(order, s->object_count, sizeof order[0], effects_order);
	for (unsigned i = 0; i < s->object_count; ++i) {
		const struct xvt_snap_object *o = &s->objects[order[i].index];
		if (!crt && o->id.slot == cam->focus.slot && !cam->external &&
		    !cam->replay_view) {
			continue;
		}
		if (o->slot_class == XVT_SLOT_LOCAL_TRANSIENT &&
		    (!s->sky.debris_enabled || s->sky.proving_grounds ||
		     cam->hyperspace_phase == XVT_SNAP_HYPERSPACE_TRANSITION)) {
			continue;
		}
		if (crt && o->id.slot != crt->object.slot &&
		    o->genus != CRAFT_GENUS_EXPLOSION) {
			continue;
		}
		if (o->genus == CRAFT_GENUS_SMALL_DEBRIS ||
		    o->genus == CRAFT_GENUS_EXPLOSION ||
		    (o->slot_class == XVT_SLOT_STATIC &&
		     o->genus >= CRAFT_GENUS_MINE &&
		     o->genus <= CRAFT_GENUS_SMALL_DEBRIS)) {
			submit_one(scene, s, p, o, cam, pc, frame_code(s, o), 0,
				   regenerate);
		}
		if (!o->has_craft || o->id.slot >= s->sky.craft_slot_end ||
		    o->object_type >= XVT_SNAP_TYPES) {
			continue;
		}
		if (o->genus == CRAFT_GENUS_OBSTACLE &&
		    o->id.slot != s->sky.checkpoint_slot &&
		    o->id.slot != (unsigned)s->sky.checkpoint_slot + 1) {
			continue;
		}
		const struct xvt_mesh_asset *mesh = xvt_remaster_ship_mesh(
			s, s->types[o->object_type].model_asset_id);
		if (!mesh || mesh->component_count >= XVT_SNAP_COMPONENTS) {
			continue;
		}
		unsigned flame = o->component_state[mesh->component_count];
		if (flame >= 25) {
			continue;
		}
		int ordinal = 0;
		for (unsigned m = 0; m < mesh->component_count; ++m) {
			if (!o->component_state[m] &&
			    mesh->mesh->mesh_rot[m].mesh_type ==
				    XVT_SNAP_MESH_FUSELAGE) {
				++ordinal;
			}
		}
		for (; ordinal > 0; --ordinal) {
			submit_one(scene, s, p, o, cam, pc,
				   (uint16_t)s->fuselage_sequence[flame],
				   ordinal, regenerate);
		}
	}
}

void xvt_effects_projectile_matrix(const struct xvt_snap_object *o,
				   const int32_t camera[3],
				   const int32_t origin[3], float out[16])
{
	struct xvt_snap_object aligned = *o;
	float m[16];
	float delta[3];
	xvt_render_math_object_matrix(o, origin, m);
	AeronWorld_DeltaI32(camera, o->world_pos, delta);
	float side = m[0] * delta[0] + m[4] * delta[1] + m[8] * delta[2];
	float up = m[2] * delta[0] + m[6] * delta[1] + m[10] * delta[2];
	aligned.roll =
		(uint16_t)(o->roll +
			   (int)(atan2f(up, side) * 65536.0f / k_tau) - 0x4000);
	aligned.orient_dirty = 1;
	xvt_render_math_object_matrix(&aligned, origin, out);
}

void xvt_effects_map_object(AeronScene3D *scene,
			    const struct xvt_render_snapshot *s,
			    const struct xvt_snap_object *o)
{
	submit_one(scene, s, NULL, o, &s->camera, NULL, frame_code(s, o), 0, 0);
	for (int ordinal = 1; ordinal <= XVT_SNAP_COMPONENTS; ++ordinal) {
		uint16_t code = engine_flame_frame_code(s, o, ordinal);
		if (code == XVT_SNAP_INVALID_TEXTURE_FRAME) {
			break;
		}
		submit_one(scene, s, NULL, o, &s->camera, NULL, code, ordinal,
			   0);
	}
}
