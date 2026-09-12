#include "xvt_runtime/assets/opt_native.h"
#include "xvt_runtime/storage/storage.h"
#include <stdlib.h>

typedef struct XvtOptRelocation {
	void** visited;
	size_t count, capacity;
	intptr_t delta;
} XvtOptRelocation;

static void* XvtOpt_Move(const void* pointer, intptr_t delta) {
	return pointer ? (void*)((uintptr_t)pointer + (uintptr_t)delta) : NULL;
}

static int XvtOpt_Seen(XvtOptRelocation* state, void* pointer) {
	for (size_t i = 0; i < state->count; ++i)
		if (state->visited[i] == pointer)
			return 1;
	if (state->count == state->capacity) {
		size_t capacity = state->capacity ? state->capacity * 2 : 64;
		void** grown = realloc(state->visited, capacity * sizeof(*grown));
		if (!grown) {
			XvtStorage_Fatal("Cannot relocate OPT graph", 1);
			return 1;
		}
		state->visited = grown;
		state->capacity = capacity;
	}
	state->visited[state->count++] = pointer;
	return 0;
}

static void XvtOpt_MoveNode(XvtOptRelocation* state, OptNode* node, unsigned depth) {
	if (!node || XvtOpt_Seen(state, node))
		return;
	if (depth >= 256) {
		XvtStorage_Fatal("OPT graph exceeds relocation depth", 1);
		return;
	}
	node->pName = XvtOpt_Move(node->pName, state->delta);
	node->param2 = XvtOpt_Move(node->param2, state->delta);
	if (node->nodeType == OPT_TEXTURE && node->param2 && !XvtOpt_Seen(state, node->param2)) {
		OptTextureData* texture = node->param2;
		if (!texture->paletteType)
			texture->palette = XvtOpt_Move(texture->palette, state->delta);
	}
	if (node->nodeType == OPT_NODEREF)
		node->param1 = 0;
	node->pChildren = XvtOpt_Move(node->pChildren, state->delta);
	if (node->pChildren && !XvtOpt_Seen(state, node->pChildren)) {
		for (int i = 0; i < node->childCount; ++i) {
			node->pChildren[i] = XvtOpt_Move(node->pChildren[i], state->delta);
			XvtOpt_MoveNode(state, node->pChildren[i], depth + 1);
		}
	}
}

void XvtOpt_RelocateNode(OptNode* node, intptr_t delta) {
	XvtOptRelocation state = { .delta = delta };
	XvtOpt_MoveNode(&state, node, 0);
	free(state.visited);
}

void XvtOpt_Relocate(OptimizedPolyObject* model) {
	if (!model || model->selfMarker == model)
		return;
	XvtOptRelocation state = { .delta = (intptr_t)((uintptr_t)model - (uintptr_t)model->selfMarker) };
	model->selfMarker = model;
	model->rootNodes = XvtOpt_Move(model->rootNodes, state.delta);
	for (int i = 0; i < model->rootNodeCount; ++i) {
		model->rootNodes[i] = XvtOpt_Move(model->rootNodes[i], state.delta);
		XvtOpt_MoveNode(&state, model->rootNodes[i], 0);
	}
	free(state.visited);
}

OptNode* XvtOpt_ResolveCached(const OptimizedPolyObject* model, OptNode* node) {
	if (!node->param1)
		node->param1 = (intptr_t)OptModel_ResolveNodeRef(model, node->param2);
	return (OptNode*)node->param1;
}
