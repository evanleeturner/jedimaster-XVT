#include "xvt_remaster/render_math.h"
#include "aeron/asset/opt_model.h"
#include "aeron/scene/world.h"
#include "xvt_remaster/component_animation.h"
#include <math.h>
#include <string.h>

static float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static int Normalize(float v[3]) {
	float length = sqrtf(Dot(v, v));
	if (!isfinite(length) || length < 1e-6f)
		return 0;
	for (int i = 0; i < 3; ++i)
		v[i] /= length;
	return 1;
}

static void fl_rows_to_quat(const float m[9], float q[4]) {
	const float trace = m[0] + m[4] + m[8];
	if (trace > 0.0f) {
		const float sq = sqrtf(trace + 1.0f) * 2.0f;
		q[0] = 0.25f * sq;
		q[1] = (m[7] - m[5]) / sq;
		q[2] = (m[2] - m[6]) / sq;
		q[3] = (m[3] - m[1]) / sq;
	} else if (m[0] > m[4] && m[0] > m[8]) {
		const float sq = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2.0f;
		q[0] = (m[7] - m[5]) / sq;
		q[1] = 0.25f * sq;
		q[2] = (m[1] + m[3]) / sq;
		q[3] = (m[2] + m[6]) / sq;
	} else if (m[4] > m[8]) {
		const float sq = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2.0f;
		q[0] = (m[2] - m[6]) / sq;
		q[1] = (m[1] + m[3]) / sq;
		q[2] = 0.25f * sq;
		q[3] = (m[5] + m[7]) / sq;
	} else {
		const float sq = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2.0f;
		q[0] = (m[3] - m[1]) / sq;
		q[1] = (m[2] + m[6]) / sq;
		q[2] = (m[5] + m[7]) / sq;
		q[3] = 0.25f * sq;
	}
}

static void fl_curmat_from_cached(const int16_t rows[9], float cur[3][3]) {
	const float q = 1.0f / 32768.0f;
	cur[0][0] = rows[0] * q;
	cur[0][1] = rows[1] * q;
	cur[0][2] = rows[2] * q;
	cur[1][0] = rows[6] * q;
	cur[1][1] = rows[7] * q;
	cur[1][2] = rows[8] * q;
	cur[2][0] = -rows[3] * q;
	cur[2][1] = -rows[4] * q;
	cur[2][2] = -rows[5] * q;
}

#define FL_Q16_TO_RAD (2.0f * 3.14159265358979323846f / 65536.0f)

/* Rodrigues rotation of all three curMat rows about `axis` by a Q16
 * angle — the float mirror of FVIEW_transformaxes (new = M.row with
 * new_x = m00 x + m10 y + m20 z, i.e. M applied transposed). */
static void fl_transformaxes(float cur[3][3], const float axis[3], int angle_q16) {
	if ((int16_t)angle_q16 == 0) {
		return;
	}
	const float a = (float)(int16_t)angle_q16 * FL_Q16_TO_RAD;
	const float c = cosf(a);
	const float sn = sinf(a);
	const float t = 1.0f - c;
	const float x = axis[0], y = axis[1], z = axis[2];
	/* m[i][j] laid out as FVIEW_transformaxes computes m00..m22. */
	const float m[3][3] = {
		{ c + t * x * x, sn * z + t * y * x, -sn * y + t * z * x },
		{ -sn * z + t * y * x, c + t * y * y, sn * x + t * z * y },
		{ sn * y + t * z * x, -sn * x + t * z * y, c + t * z * z },
	};
	for (int r = 0; r < 3; r++) {
		const float ox = cur[r][0], oy = cur[r][1], oz = cur[r][2];
		/* new_x = m00 ox + m10 oy + m20 oz (the engine's application). */
		cur[r][0] = m[0][0] * ox + m[1][0] * oy + m[2][0] * oz;
		cur[r][1] = m[0][1] * ox + m[1][1] * oy + m[2][1] * oz;
		cur[r][2] = m[0][2] * ox + m[1][2] * oy + m[2][2] * oz;
	}
}

/* curMat rows from the record's Q16 Euler angles — the float mirror of
 * FVIEW_calcrotatemove + FVIEW_calcrotateorient (statics and dirty
 * orientations). */
static void fl_curmat_from_euler(const XvtSnapObject* f, float cur[3][3]) {
	const float aA = (float)(int16_t)(0xc000 - f->pitch) * FL_Q16_TO_RAD;
	const float aB = (float)(int16_t)(-(int16_t)f->yaw) * FL_Q16_TO_RAD;
	const float cB = cosf(aB), sB = sinf(aB);
	const float cA = cosf(aA), sA = sinf(aA);
	cur[0][0] = cB;
	cur[0][1] = sB;
	cur[0][2] = 0.0f;
	cur[2][0] = -sB * cA;
	cur[2][1] = cB * cA;
	cur[2][2] = sA;
	cur[1][0] = -sB * sA;
	cur[1][1] = cB * sA;
	cur[1][2] = -cA;
	fl_transformaxes(cur, cur[2], (int16_t)f->roll);
}

/* Model->world basis rows: the curMat rows in the engine's consumption
 * order (R0, R2, R1) — FVIEW_ComputeObjectViewMatrix's row read, with
 * the camera factor moved into the scene's view matrix. */
static void fl_object_world(const float cur[3][3], float out[9]) {
	const float* src[3] = { cur[0], cur[2], cur[1] };
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			out[i * 3 + j] = src[i][j];
		}
	}
}

/* Model matrix from a model->space basis (rows) + translation, the
 * transpose consumption the model preview validated (Math3D_RotateVec3
 * is column-major over the stored rows). */
static void fl_model_matrix(const float basis[9], const float delta[3], float m[16]) {
	const float scale = AERON_OPT_UNITS_PER_METER;
	m[0] = scale * basis[0];
	m[1] = scale * basis[3];
	m[2] = scale * basis[6];
	m[3] = delta[0];
	m[4] = scale * basis[1];
	m[5] = scale * basis[4];
	m[6] = scale * basis[7];
	m[7] = delta[1];
	m[8] = scale * basis[2];
	m[9] = scale * basis[5];
	m[10] = scale * basis[8];
	m[11] = delta[2];
	m[12] = 0.0f;
	m[13] = 0.0f;
	m[14] = 0.0f;
	m[15] = 1.0f;
}

int XvtRenderMath_BuildView(const XvtSnapCamera* cam, const int32_t origin[3], int width, int height,
							XvtRenderView* out) {
	if (!cam || !cam->valid || !origin || !out || width <= 0 || height <= 0 || cam->viewport.width <= 0 ||
		cam->viewport.height <= 0)
		return 0;
	memset(out, 0, sizeof *out);
	float rows[9];
	memcpy(rows, cam->rows, sizeof rows);
	if (!Normalize(rows))
		return 0;
	float projection = Dot(rows, rows + 3);
	for (int i = 0; i < 3; ++i)
		rows[3 + i] -= rows[i] * projection;
	if (!Normalize(rows + 3))
		return 0;
	rows[6] = rows[1] * rows[5] - rows[2] * rows[4];
	rows[7] = rows[2] * rows[3] - rows[0] * rows[5];
	rows[8] = rows[0] * rows[4] - rows[1] * rows[3];
	AeronSceneCamera* c = &out->camera;
	fl_rows_to_quat(rows, c->ori);
	float norm =
		sqrtf(c->ori[0] * c->ori[0] + c->ori[1] * c->ori[1] + c->ori[2] * c->ori[2] + c->ori[3] * c->ori[3]);
	for (int i = 0; i < 4; ++i)
		c->ori[i] /= norm;
	memcpy(out->origin_world, origin, sizeof out->origin_world);
	AeronWorld_LocalI32(origin, cam->world_pos, c->pos);
	float focal = ldexpf(1.0f, cam->perspective_shift & 31);
	/* MATH2_longfraction treats 0xFFFF as exactly one. Zero skips scaling. */
	float aspect_y =
		!cam->aspect_y_q16 || cam->aspect_y_q16 == UINT16_MAX ? 1.0f : (float)cam->aspect_y_q16 / 65536.0f;
	float half_w = cam->viewport.width * 0.5f, half_h = cam->viewport.height * 0.5f;
	c->v_half_rad = atanf(half_h / (focal * aspect_y));
	c->h_half_rad = atanf(tanf(c->v_half_rad) * (float)width / (float)height);
	c->near_z = 1.0f;
	c->proj_x_offset = (cam->center_x - half_w) / half_w;
	c->proj_y_offset = (half_h - cam->center_y - cam->projection_offset_y) / half_h;
	c->viewport = (AeronRectI) { 0, 0, width, height };
	out->classic_pixel_scale = (float)height / cam->viewport.height;
	AeronScene_ComputeViewProj(c, out->view_proj);
	return 1;
}

int XvtRenderMath_ProjectWorld(const XvtRenderView* view, const int32_t world[3], float* x, float* y,
							   float* depth) {
	if (!view || !world || !x || !y)
		return 0;
	float p[3];
	AeronWorld_LocalI32(view->origin_world, world, p);
	const float* m = view->view_proj;
	float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
	if (depth)
		*depth = w;
	if (w <= 0 || !isfinite(w))
		return 0;
	float nx = (m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3]) / w;
	float ny = (m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7]) / w;
	*x = (nx + 1) * 0.5f * view->camera.viewport.width + view->camera.viewport.x;
	*y = (1 - ny) * 0.5f * view->camera.viewport.height + view->camera.viewport.y;
	return isfinite(*x) && isfinite(*y);
}

int XvtRenderMath_BuildMainView(const XvtSnapCamera* cam, const int32_t origin[3], int width, int height,
								XvtRenderView* out) {
	if (!XvtRenderMath_BuildView(cam, origin, width, height, out) || cam->screen_width <= 0 ||
		cam->screen_height <= 0)
		return 0;
	float scale = fminf((float)width / cam->screen_width, (float)height / cam->screen_height);
	float focal = ldexpf(1, cam->perspective_shift & 31) * scale;
	float aspect =
		!cam->aspect_y_q16 || cam->aspect_y_q16 == UINT16_MAX ? 1 : (float)cam->aspect_y_q16 / 65536;
	AeronSceneCamera* c = &out->camera;
	c->h_half_rad = atanf(width / (2 * focal));
	c->v_half_rad = atanf(height / (2 * focal * aspect));
	/* Map the original viewport's projection center into the fitted cockpit frame. */
	float cx = (width - cam->screen_width * scale) / 2 + (cam->viewport.x + cam->center_x) * scale;
	float cy = (height - cam->screen_height * scale) / 2 +
			   (cam->viewport.y + cam->center_y + cam->projection_offset_y) * scale;
	c->proj_x_offset = 2 * cx / width - 1;
	c->proj_y_offset = 1 - 2 * cy / height;
	out->classic_pixel_scale = scale;
	AeronScene_ComputeViewProj(c, out->view_proj);
	return 1;
}

void XvtRenderMath_ObjectMatrix(const XvtSnapObject* object, const int32_t origin[3], float out[16]) {
	float cur[3][3], basis[9], local[3];
	if (object->has_mobile && !object->orient_dirty)
		fl_curmat_from_cached(object->cached_rows_q15, cur);
	else
		fl_curmat_from_euler(object, cur);
	fl_object_world(cur, basis);
	AeronWorld_LocalI32(origin, object->world_pos, local);
	fl_model_matrix(basis, local, out);
}

int XvtRenderMath_PoseChanged(const XvtRenderSnapshot* current, const XvtRenderSnapshot* previous) {
	if (!current || !previous || current->flight_valid != previous->flight_valid)
		return 1;
	if (!current->flight_valid)
		return 0;
	if ((current->flight_unlocked && current->view_time_ticks != previous->view_time_ticks &&
		 XvtComponentAnimation_Changed()) ||
		memcmp(&current->camera, &previous->camera, sizeof current->camera) ||
		current->object_count != previous->object_count)
		return 1;
	for (unsigned i = 0; i < current->object_count; ++i) {
		const XvtSnapObject *a = &current->objects[i], *b = &previous->objects[i];
		if (a->id.slot != b->id.slot || a->id.signature != b->id.signature ||
			a->object_type != b->object_type || memcmp(a->world_pos, b->world_pos, sizeof a->world_pos) ||
			a->pitch != b->pitch || a->yaw != b->yaw || a->roll != b->roll ||
			a->has_mobile != b->has_mobile || a->orient_dirty != b->orient_dirty ||
			a->has_craft != b->has_craft || a->source_type != b->source_type ||
			a->node_switch != b->node_switch || a->state != b->state || a->genus != b->genus ||
			a->slot_class != b->slot_class || a->source_slot != b->source_slot ||
			a->light_scale != b->light_scale || a->throttle != b->throttle ||
			a->engine_output != b->engine_output || a->working_subsystems != b->working_subsystems ||
			a->object_kind != b->object_kind || a->speed != b->speed || a->max_speed != b->max_speed ||
			a->laser_redirect != b->laser_redirect || a->shield_redirect != b->shield_redirect ||
			a->beam_level != b->beam_level ||
			memcmp(a->type_specific, b->type_specific, sizeof a->type_specific) ||
			memcmp(a->cached_rows_q15, b->cached_rows_q15, sizeof a->cached_rows_q15) ||
			memcmp(a->mesh_rotation, b->mesh_rotation, sizeof a->mesh_rotation) ||
			memcmp(a->component_state, b->component_state, sizeof a->component_state))
			return 1;
	}
	return 0;
}

int XvtRenderMath_Layout(float sw, float sh, float tw, float th, XvtLayoutTransform* out) {
	if (!out || !isfinite(sw + sh + tw + th) || sw <= 0 || sh <= 0 || tw <= 0 || th <= 0)
		return 0;
	*out = (XvtLayoutTransform) { fminf(tw / sw, th / sh), sw, sh, tw, th };
	return 1;
}

void XvtRenderMath_LayoutPoint(const XvtLayoutTransform* t, float ax, float ay, float x, float y, float* ox,
							   float* oy) {
	*ox = x * t->scale + ax * (t->target_width - t->source_width * t->scale);
	*oy = y * t->scale + ay * (t->target_height - t->source_height * t->scale);
}

void XvtRenderMath_LayoutInverse(const XvtLayoutTransform* t, float ax, float ay, float x, float y, float* ox,
								 float* oy) {
	*ox = (x - ax * (t->target_width - t->source_width * t->scale)) / t->scale;
	*oy = (y - ay * (t->target_height - t->source_height * t->scale)) / t->scale;
}
