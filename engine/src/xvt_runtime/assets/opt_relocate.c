#include <stdlib.h>

#include "xvt_runtime/assets/opt_native.h"
#include "xvt_runtime/storage/storage.h"

struct xvt_opt_relocation {
	void **visited;
	size_t count, capacity;
	intptr_t delta;
};

static void *xvt_opt_move(const void *pointer, intptr_t delta)
{
	return pointer ? (void *)((uintptr_t)pointer + (uintptr_t)delta) : NULL;
}

static int xvt_opt_test_and_mark_seen(struct xvt_opt_relocation *state,
				      void *pointer)
{
	for (size_t i = 0; i < state->count; ++i) {
		if (state->visited[i] == pointer) {
			return 1;
		}
	}
	if (state->count == state->capacity) {
		size_t capacity = state->capacity ? state->capacity * 2 : 64;
		void **grown =
			realloc(state->visited, capacity * sizeof(*grown));
		if (!grown) {
			xvt_storage_fatal("Cannot relocate OPT graph", 1);
			return 1;
		}
		state->visited = grown;
		state->capacity = capacity;
	}
	state->visited[state->count++] = pointer;
	return 0;
}

static void xvt_opt_move_node(struct xvt_opt_relocation *state,
			      struct opt_node *node, unsigned depth)
{
	if (!node || xvt_opt_test_and_mark_seen(state, node)) {
		return;
	}
	if (depth >= 256) {
		xvt_storage_fatal("OPT graph exceeds relocation depth", 1);
		return;
	}
	node->p_name = xvt_opt_move(node->p_name, state->delta);
	node->payload = xvt_opt_move(node->payload, state->delta);
	if (node->node_type == OPT_TEXTURE && node->payload &&
	    !xvt_opt_test_and_mark_seen(state, node->payload)) {
		struct opt_texture_data *texture = node->payload;
		if (!texture->inline_palette_count) {
			texture->palette =
				xvt_opt_move(texture->palette, state->delta);
		}
	}
	if (node->node_type == OPT_NODEREF) {
		node->payload_count = 0;
	}
	node->p_children = xvt_opt_move(node->p_children, state->delta);
	if (node->p_children &&
	    !xvt_opt_test_and_mark_seen(state, node->p_children)) {
		for (int i = 0; i < node->child_count; ++i) {
			node->p_children[i] =
				xvt_opt_move(node->p_children[i], state->delta);
			xvt_opt_move_node(state, node->p_children[i],
					  depth + 1);
		}
	}
}

void xvt_opt_relocate_node(struct opt_node *node, intptr_t delta)
{
	struct xvt_opt_relocation state = {.delta = delta};
	xvt_opt_move_node(&state, node, 0);
	free(state.visited);
}

void xvt_opt_relocate(struct optimized_poly_object *model)
{
	if (!model || model->self_marker == model) {
		return;
	}
	struct xvt_opt_relocation state = {
		.delta = (intptr_t)((uintptr_t)model -
				    (uintptr_t)model->self_marker)};
	model->self_marker = model;
	model->root_nodes = xvt_opt_move(model->root_nodes, state.delta);
	for (int i = 0; i < model->root_node_count; ++i) {
		model->root_nodes[i] =
			xvt_opt_move(model->root_nodes[i], state.delta);
		xvt_opt_move_node(&state, model->root_nodes[i], 0);
	}
	free(state.visited);
}

struct opt_node *
xvt_opt_resolve_cached(const struct optimized_poly_object *model,
		       struct opt_node *node)
{
	if (!node->payload_count) {
		node->payload_count = (intptr_t)opt_model_resolve_node_ref(
			model, node->payload);
	}
	return (struct opt_node *)node->payload_count;
}
