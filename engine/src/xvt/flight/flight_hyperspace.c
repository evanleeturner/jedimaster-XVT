#include "xvt/flight/flight_hyperspace.h"

#include <stdlib.h>

#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/trig2.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_capture.h"

enum {
	HYPERSPACE_TRANSITION_OBJECT_TYPE = 137,
};

/* The face data block of the built-in streak model, laid out as the renderer
 * reads an OPT_FACEDATA payload: the edge count, then per face its record, its
 * normal and its texture axes. */
struct hyperspace_face_payload {
	int edge_count; /* Edge count the renderer takes for the mesh: 4. */
	struct opt_packed_face_record face; /* The one face: its index lists. */
	struct opt_vector face_normal;	    /* The face's normal, (0, 0, 1). */
	/* Texture axes, (1, 0, 0) and (0, 1, 0). */
	struct opt_vector texture_gradients[2];
};

/* The built-in model a hyperspace streak is drawn with: one textured quad as a
 * group of four OPT nodes, all in one block. */
struct hyperspace_streak_embedded_model_data {
	/* Vertex node: the 4 corners in g_hyperspace_streak_quad_vertices. */
	struct opt_node vertices_node;
	struct opt_tex_coord
		tex_coords[4]; /* The corners' texture coordinates. */
	/* Texture coordinate node: the 4 in tex_coords. */
	struct opt_node tex_coords_node;
	struct opt_vector normal;     /* The one vertex normal, (0, 0, 1). */
	int normal_node_padding;      /* 0; not used by name. */
	struct opt_node normals_node; /* Vertex normal node: the 1 in normal. */
	/* The face data the face node points at. */
	struct hyperspace_face_payload face_payload;
	struct opt_node face_node; /* Face data node: 1 face, face_payload. */
	struct opt_node *child_nodes[4]; /* The four nodes above, in order. */
	struct opt_node root_node;	 /* Group node holding child_nodes. */
	struct opt_node *
		root_nodes[1]; /* Points at root_node: the model's root list. */
	int trailing_padding;  /* 0; not used by name. */
};

/* The streak quad's four corners: x is plus or minus the streak's half width, y
 * runs from 0 to the stretched length. Starts with x at 64 or -64 and y at 0 or
 * -256; flight_hyperspace_render_transition_effect, its only writer, sets the x of
 * all four and the y of corners 1 and 2 before each streak is drawn. */
// GLOBAL: XVT 0x51C340
static struct opt_vector g_hyperspace_streak_quad_vertices[4] = {
	{64.0f, 0.0f, 0.0f},
	{64.0f, -256.0f, 0.0f},
	{-64.0f, -256.0f, 0.0f},
	{-64.0f, 0.0f, 0.0f},
};
/* The built-in streak model: one quad over g_hyperspace_streak_quad_vertices, set
 * up in its initializer; nothing writes it by name. */
// GLOBAL: XVT 0x51C370
static struct hyperspace_streak_embedded_model_data
	g_hyperspace_streak_embedded_model_data = {
		{NULL, OPT_MESHVERTS, 0, NULL, 4,
		 g_hyperspace_streak_quad_vertices},
		{{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}},
		{NULL, OPT_TEXCOORDS, 0, NULL, 4,
		 g_hyperspace_streak_embedded_model_data.tex_coords},
		{0.0f, 0.0f, 1.0f},
		0,
		{NULL, OPT_VERTNORMALS, 0, NULL, 1,
		 &g_hyperspace_streak_embedded_model_data.normal},
		{
			4,
			{{0, 1, 2, 3},
			 {0, 1, 2, 3},
			 {0, 0, 0, 0},
			 {0, 1, 2, 3}},
			{0.0f, 0.0f, 1.0f},
			{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
		},
		{NULL, OPT_FACEDATA, 0, NULL, 1,
		 &g_hyperspace_streak_embedded_model_data.face_payload},
		{
			&g_hyperspace_streak_embedded_model_data.vertices_node,
			&g_hyperspace_streak_embedded_model_data
				 .tex_coords_node,
			&g_hyperspace_streak_embedded_model_data.normals_node,
			&g_hyperspace_streak_embedded_model_data.face_node,
		},
		{NULL, OPT_GROUP, 4,
		 g_hyperspace_streak_embedded_model_data.child_nodes, 4,
		 g_hyperspace_streak_embedded_model_data.child_nodes},
		{&g_hyperspace_streak_embedded_model_data.root_node},
		0,
};
/* Model header that flight_hyperspace_draw_transition_effect_object lays over the
 * loaded model of object type 137 while it draws a streak, so the renderer
 * draws the built-in streak model (one root,
 * g_hyperspace_streak_embedded_model_data.root_nodes); that function, its only
 * writer, sets self_marker to the model it covers. */
// GLOBAL: XVT 0x51C498
static struct optimized_poly_object g_hyperspace_model_header_patch = {
	&g_hyperspace_model_header_patch, 0, 1,
	g_hyperspace_streak_embedded_model_data.root_nodes};

/* 1 when flight_hyperspace_render_transition_effect is to place new streaks and
 * play the entry sound. Starts at 1;
 * flight_hyperspace_request_transition_effect_initialization sets it and
 * flight_hyperspace_render_transition_effect clears it. */
// GLOBAL: XVT 0x51C4A8
static int g_hyperspace_transition_effect_init_pending = 1;
/* 1 while the exit sound is still to play:
 * flight_hyperspace_render_transition_effect, its only writer, sets it while the
 * transition is under 472 ticks (HYPERSPACE_STRETCH_PHASE_TICKS) and clears it
 * when it plays the exit sound after that. Starts at 1. */
// GLOBAL: XVT 0x51C4AC
static int g_hyperspace_transition_effect_sound_pending = 1;

/* Per streak, its offset from the camera in world Y: always 0x4000
 * (HYPERSPACE_FORWARD_OFFSET). Written only by
 * flight_hyperspace_render_transition_effect, when it places the streaks. */
// GLOBAL: XVT 0x550C80
static int g_hyperspace_streak_offset_y[1024] = {0};
/* Per streak, its offset from the camera in world Z: random, up to 10,239
 * either way. Written only by flight_hyperspace_render_transition_effect, when it
 * places the streaks. */
// GLOBAL: XVT 0x551C80
static int g_hyperspace_streak_offset_z[1024] = {0};
/* Per streak, half its width: 1 plus the average of its X and Z offset sizes
 * shifted right 7. Written only by flight_hyperspace_render_transition_effect,
 * when it places the streaks. */
// GLOBAL: XVT 0x552C80
static int g_hyperspace_streak_half_width[1024] = {0};
/* Per streak, its offset from the camera in world X: random, up to 10,239
 * either way. Written only by flight_hyperspace_render_transition_effect, when it
 * places the streaks. */
// GLOBAL: XVT 0x553C80
static int g_hyperspace_streak_offset_x[1024] = {0};
/* Per streak, its roll: the angle of its (X, Z) offset (trig2_arctan) plus a
 * quarter turn, 0x4000 (a full circle is 65,536). Written only by
 * flight_hyperspace_render_transition_effect, when it places the streaks. */
// GLOBAL: XVT 0x554C80
static int g_hyperspace_streak_roll_angle[1024] = {0};

/* Draws object 0 (g_object_table[0]) as one hyperspace streak: lays
 * g_hyperspace_model_header_patch over the header of the loaded model of object
 * type 137, so the built-in streak model is drawn, sets
 * g_billboard_object_or_type_index to 0, marks the object's orientation for
 * recomputing, sets its transform (fview_set_object_transform), draws it
 * (render_scene_draw_object_model) and puts the model's header back. Does not
 * check that the model's handle locked. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x424410
void flight_hyperspace_draw_transition_effect_object(void)
{
	struct optimized_poly_object *model =
		(struct optimized_poly_object *)memory_get_handle_block(
			g_loaded_models[HYPERSPACE_TRANSITION_OBJECT_TYPE]);
	struct optimized_poly_object saved_header;
	memcpy(&saved_header, model, sizeof(saved_header));
	g_hyperspace_model_header_patch.self_marker = model;
	*model = g_hyperspace_model_header_patch;
	g_billboard_object_or_type_index = 0;
	struct object_record *object = g_object_table;
	object->mobj->orient_matrix_dirty = 1;
	fview_set_object_transform(object->roll, object->pitch, object->yaw, 0,
				   object);
	render_scene_draw_object_model(object);
	memcpy(model, &saved_header, sizeof(saved_header));
}

/* Sets g_hyperspace_transition_effect_init_pending to 1; flight_view_render calls it
 * while the local player's hyperspace_phase is 1. */
// FUNCTION: XVT 0x4244D0
void flight_hyperspace_request_transition_effect_initialization(void)
{
	g_hyperspace_transition_effect_init_pending = 1;
}

/* Draws the hyperspace streaks around the local player's camera: 1,024 with
 * hardware 3D, 512 in software. When g_hyperspace_transition_effect_init_pending is
 * set it first plays the entry sound for the player's side (the imperial sound
 * for IFF 1, else the other one) and places all 1,024 streaks at random (the
 * g_hyperspaceStreak arrays). Each streak is drawn as object 0, type 137, at
 * the camera plus its offset, pitched a quarter turn and rolled by its angle
 * (flight_hyperspace_draw_transition_effect_object). Until the local player's
 * hyperspace_runtime.phase_elapsed_ticks reach 472, a streak's length is the
 * square of a quarter of them and its world Y is lowered by 16 per tick; after
 * that its length is 16,000, its Y is lowered by 7,552 plus the square of twice
 * the ticks less 944, and the exit sound plays once. Object 0, its mobj and
 * g_bilinear_enabled, cleared while drawing, are put back after. The modern
 * build also hands the streaks to xvt_render_capture_hyperspace. */
// FUNCTION: XVT 0x4244E0
void flight_hyperspace_render_transition_effect(void)
{
	enum {
		HYPERSPACE_STREAK_COUNT = 1024,
		HYPERSPACE_SOFTWARE_STREAK_COUNT = 512,
		HYPERSPACE_RANDOM_COORD_MASK = 0x3FFF,
		HYPERSPACE_RANDOM_SCALE_MASK = 3,
		HYPERSPACE_RANDOM_SCALE_BASE = 2,
		HYPERSPACE_RANDOM_COORD_SHIFT = 3,
		HYPERSPACE_RANDOM_SIGN_MASK = 0x1000,
		HYPERSPACE_MINIMUM_RADIUS = 8,
		HYPERSPACE_LENGTH_SHIFT = 8,
		HYPERSPACE_FORWARD_OFFSET = 0x4000,
		HYPERSPACE_ROLL_OFFSET = 0x4000,
		HYPERSPACE_STRETCH_PHASE_TICKS = 0x1D8,
		HYPERSPACE_STRETCH_WORLD_OFFSET = 0x1D80,
		HYPERSPACE_STRETCH_TIME_OFFSET = 944,
		HYPERSPACE_IMPERIAL_IFF = 1,
	};

	int streak_count = HYPERSPACE_STREAK_COUNT;
	if (g_use_hardware3d == 0) {
		streak_count = HYPERSPACE_SOFTWARE_STREAK_COUNT;
	}
	int saved_bilinear_enabled = g_bilinear_enabled;
	g_bilinear_enabled = 0;
	struct object_record saved_object = *g_object_table;
	struct mobile_object saved_mobile_object = *g_object_table->mobj;

	int streak_index;
	if (g_hyperspace_transition_effect_init_pending != 0) {
		if (g_players[g_local_player].iff == HYPERSPACE_IMPERIAL_IFF) {
			fsfx_play_sound(FLIGHT_SOUND_HYPERSPACE_ENTER_IMPERIAL,
					-1, g_local_player);
		} else {
			fsfx_play_sound(
				FLIGHT_SOUND_HYPERSPACE_ENTER_NON_IMPERIAL, -1,
				g_local_player);
		}
		for (streak_index = 0; streak_index < HYPERSPACE_STREAK_COUNT;
		     ++streak_index) {
			int offset_x;
			int offset_z;
			int average_radius;

			do {
				int random_x =
					rand() & HYPERSPACE_RANDOM_COORD_MASK;
				int random_z =
					rand() & HYPERSPACE_RANDOM_COORD_MASK;
				int random_scale =
					(rand() &
					 HYPERSPACE_RANDOM_SCALE_MASK) +
					HYPERSPACE_RANDOM_SCALE_BASE;
				offset_x = (random_x * random_scale) >>
					   HYPERSPACE_RANDOM_COORD_SHIFT;
				offset_z = (random_z * random_scale) >>
					   HYPERSPACE_RANDOM_COORD_SHIFT;
				average_radius = (offset_x + offset_z) >> 1;
			} while (average_radius < HYPERSPACE_MINIMUM_RADIUS);

			g_hyperspace_streak_half_width[streak_index] =
				(average_radius >>
				 (HYPERSPACE_LENGTH_SHIFT - 1)) +
				1;
			if ((rand() & HYPERSPACE_RANDOM_SIGN_MASK) != 0) {
				g_hyperspace_streak_offset_x[streak_index] =
					offset_x;
			} else {
				g_hyperspace_streak_offset_x[streak_index] =
					-offset_x;
			}
			if ((rand() & HYPERSPACE_RANDOM_SIGN_MASK) != 0) {
				g_hyperspace_streak_offset_z[streak_index] =
					offset_z;
			} else {
				g_hyperspace_streak_offset_z[streak_index] =
					-offset_z;
			}
			g_hyperspace_streak_offset_y[streak_index] =
				HYPERSPACE_FORWARD_OFFSET;
			g_hyperspace_streak_roll_angle[streak_index] =
				(uint16_t)trig2_arctan(
					g_hyperspace_streak_offset_z
						[streak_index],
					g_hyperspace_streak_offset_x
						[streak_index]) +
				HYPERSPACE_ROLL_OFFSET;
		}
		g_hyperspace_transition_effect_init_pending = 0;
		XVT_LOG_DEBUG(
			"flight.hyperspace_effect_started slot=%d iff=%d count=%d ticks=%u",
			g_local_player, (int)g_players[g_local_player].iff,
			streak_count,
			(unsigned)g_players[g_local_player]
				.hyperspace_runtime.phase_elapsed_ticks);
	}

	xvt_render_capture_hyperspace(
		(unsigned)streak_count, g_hyperspace_streak_offset_x,
		g_hyperspace_streak_offset_y, g_hyperspace_streak_offset_z,
		g_hyperspace_streak_half_width, g_hyperspace_streak_roll_angle);
	const float fully_stretched_length = 16000.0f;
	for (streak_index = 0; streak_index < streak_count; ++streak_index) {
		g_object_table->world_x =
			g_players[g_local_player].view_state.camera_world_x +
			g_hyperspace_streak_offset_x[streak_index];
		g_object_table->world_y =
			g_players[g_local_player].view_state.camera_world_y +
			g_hyperspace_streak_offset_y[streak_index];
		g_object_table->world_z =
			g_players[g_local_player].view_state.camera_world_z +
			g_hyperspace_streak_offset_z[streak_index];
		g_object_table->object_type = HYPERSPACE_TRANSITION_OBJECT_TYPE;
		g_object_table->genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
		g_object_table->roll =
			(int16_t)g_hyperspace_streak_roll_angle[streak_index];
		g_object_table->yaw = 0;
		/* HYPERSPACE_FORWARD_OFFSET is reused here as an angle: a quarter turn of pitch. */
		g_object_table->pitch = HYPERSPACE_FORWARD_OFFSET;

		int streak_length =
			g_hyperspace_streak_half_width[streak_index];
		g_hyperspace_streak_quad_vertices[0].x = (float)streak_length;
		g_hyperspace_streak_quad_vertices[1].x =
			g_hyperspace_streak_quad_vertices[0].x;
		g_hyperspace_streak_quad_vertices[2].x = (float)-streak_length;
		g_hyperspace_streak_quad_vertices[3].x =
			g_hyperspace_streak_quad_vertices[2].x;

		unsigned int phase_elapsed_ticks =
			g_players[g_local_player]
				.hyperspace_runtime.phase_elapsed_ticks;
		if (phase_elapsed_ticks < HYPERSPACE_STRETCH_PHASE_TICKS) {
			double stretched_length =
				(double)(int64_t)(uint32_t)(phase_elapsed_ticks >>
							    2);
			stretched_length *= stretched_length;
			g_hyperspace_streak_quad_vertices[1].y =
				(float)stretched_length;
			g_hyperspace_streak_quad_vertices[2].y =
				g_hyperspace_streak_quad_vertices[1].y;
			g_object_table->world_y -=
				(int)(phase_elapsed_ticks << 4);
			g_hyperspace_transition_effect_sound_pending = 1;
		} else {
			if (g_hyperspace_transition_effect_sound_pending != 0) {
				if (g_players[g_local_player].iff ==
				    HYPERSPACE_IMPERIAL_IFF) {
					fsfx_play_sound(
						FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL,
						-1, g_local_player);
				} else {
					fsfx_play_sound(
						FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL,
						-1, g_local_player);
				}
				g_hyperspace_transition_effect_sound_pending =
					0;
				XVT_LOG_DEBUG(
					"flight.hyperspace_effect_stretched slot=%d iff=%d ticks=%u",
					g_local_player,
					(int)g_players[g_local_player].iff,
					(unsigned)phase_elapsed_ticks);
			}
			g_hyperspace_streak_quad_vertices[1].y =
				fully_stretched_length;
			g_hyperspace_streak_quad_vertices[2].y =
				fully_stretched_length;
			g_object_table->world_y -=
				HYPERSPACE_STRETCH_WORLD_OFFSET;
			int64_t stretched_phase_ticks =
				(int64_t)(uint32_t)(g_players[g_local_player]
								    .hyperspace_runtime
								    .phase_elapsed_ticks *
							    2 -
						    HYPERSPACE_STRETCH_TIME_OFFSET);
			double stretch_offset = (double)stretched_phase_ticks *
						(double)stretched_phase_ticks;
			g_object_table->world_y -= (int)stretch_offset;
		}
		flight_hyperspace_draw_transition_effect_object();
	}

	*g_object_table = saved_object;
	*g_object_table->mobj = saved_mobile_object;
	g_bilinear_enabled = saved_bilinear_enabled;
}
