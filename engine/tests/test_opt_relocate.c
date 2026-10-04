/* Checks OPT model relocation and the cached node-reference lookup (xvt_opt_relocate, xvt_opt_relocate_node
 * and xvt_opt_resolve_cached in xvt_runtime/assets/opt_native.h) against the promises in that header. The
 * model is one block this test builds in memory: no file and no game model is read. A copy of the block
 * at another address is the model moved; after relocation every internal pointer must sit at the same
 * offset into its block as in the original.
 *
 * Not run here: a graph more than 256 levels deep and running out of memory, which end the program
 * through xvt_storage_fatal; that path shows a message box. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/assets/opt_native.h"

/* The model block. Root 0 is the Hull group, with a texture whose palette type is 0, the Wing group and a
 * vertex node as children. Root 1 is the Wing group too, so it is reached from two parents. The Wing
 * group holds a texture whose palette type is 1 and a reference node naming "Hull". */
struct block {
	struct optimized_poly_object model;
	struct opt_node *roots[2];
	struct opt_node hull;
	struct opt_node texture0;
	struct opt_node wing;
	struct opt_node texture1;
	struct opt_node reference;
	struct opt_node vertices;
	struct opt_node *hull_children[3];
	struct opt_node *wing_children[2];
	struct opt_texture_data texture_data[2];
	uint16_t palettes[2][4];
	char names[3][8];
	uint8_t payload[24];
};

/* Fills block as described above, with every pointer into block itself and the reference resolved. */
static void build(struct block *block)
{
	memset(block, 0, sizeof *block);
	strcpy(block->names[0], "Hull");
	strcpy(block->names[1], "Wing");
	strcpy(block->names[2], "Hull");
	block->model.self_marker = &block->model;
	block->model.root_node_count = 2;
	block->model.root_nodes = block->roots;
	block->roots[0] = &block->hull;
	block->roots[1] = &block->wing;

	block->hull.p_name = block->names[0];
	block->hull.node_type = OPT_GROUP;
	block->hull.child_count = 3;
	block->hull.p_children = block->hull_children;
	block->hull_children[0] = &block->texture0;
	block->hull_children[1] = &block->wing;
	block->hull_children[2] = &block->vertices;

	block->texture0.node_type = OPT_TEXTURE;
	block->texture0.payload = &block->texture_data[0];
	block->texture_data[0].inline_palette_count = 0;
	block->texture_data[0].palette = block->palettes[0];

	block->wing.p_name = block->names[1];
	block->wing.node_type = OPT_GROUP;
	block->wing.child_count = 2;
	block->wing.p_children = block->wing_children;
	block->wing_children[0] = &block->texture1;
	block->wing_children[1] = &block->reference;

	block->texture1.node_type = OPT_TEXTURE;
	block->texture1.payload = &block->texture_data[1];
	block->texture_data[1].inline_palette_count = 1;
	block->texture_data[1].palette = block->palettes[1];

	block->reference.node_type = OPT_NODEREF;
	block->reference.payload = block->names[2];
	block->reference.payload_count = (xvt_opt_value)&block->hull;

	block->vertices.node_type = OPT_MESHVERTS;
	block->vertices.payload_count = 2;
	block->vertices.payload = block->payload;
}

/* Returns a copy of block at a new address, its pointers still into block. */
static struct block *copy(const struct block *block)
{
	struct block *copy = malloc(sizeof *copy);
	XVT_ASSERT_TRUE(copy != NULL);
	memcpy(copy, block, sizeof *copy);
	return copy;
}

/* How far pointer lies from the start of block, in bytes. */
static intptr_t offset(const void *block, const void *pointer)
{
	return (intptr_t)((uintptr_t)pointer - (uintptr_t)block);
}

#define XVT_ASSERT_SAME_OFFSET(original, moved, field)                         \
	XVT_ASSERT_INT_EQ(offset((moved), (moved)->field),                     \
			  offset((original), (original)->field))

static void check_relocate_moves_every_pointer(void)
{
	struct block *original = malloc(sizeof *original);
	XVT_ASSERT_TRUE(original != NULL);
	build(original);
	struct block *moved = copy(original);
	xvt_opt_relocate(&moved->model);

	XVT_ASSERT_TRUE(moved->model.self_marker == &moved->model);
	XVT_ASSERT_SAME_OFFSET(original, moved, model.root_nodes);
	XVT_ASSERT_SAME_OFFSET(original, moved, roots[0]);
	XVT_ASSERT_SAME_OFFSET(original, moved, roots[1]);
	XVT_ASSERT_SAME_OFFSET(original, moved, hull.p_name);
	XVT_ASSERT_SAME_OFFSET(original, moved, hull.p_children);
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_SAME_OFFSET(original, moved, hull_children[i]);
	}
	XVT_ASSERT_SAME_OFFSET(original, moved, texture0.payload);
	XVT_ASSERT_SAME_OFFSET(original, moved, vertices.payload);
	XVT_ASSERT_SAME_OFFSET(original, moved, reference.payload);

	/* The Wing group is reached from two parents and moves once. */
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.p_name);
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.p_children);
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_SAME_OFFSET(original, moved, wing_children[i]);
	}
	XVT_ASSERT_SAME_OFFSET(original, moved, texture1.payload);

	/* A palette moves for palette type 0 only; for other types the old address stays. */
	XVT_ASSERT_SAME_OFFSET(original, moved, texture_data[0].palette);
	XVT_ASSERT_TRUE(moved->texture_data[1].palette ==
			original->texture_data[1].palette);

	/* Relocation clears the reference cache; a vertex count is not a pointer and stays. */
	XVT_ASSERT_INT_EQ(moved->reference.payload_count, 0);
	XVT_ASSERT_INT_EQ(moved->vertices.payload_count, 2);
	free(moved);
	free(original);
}

static void check_relocate_leaves_unmoved_model(void)
{
	xvt_opt_relocate(NULL);

	struct block *block = malloc(sizeof *block);
	XVT_ASSERT_TRUE(block != NULL);
	build(block);
	struct block *before = copy(block);
	xvt_opt_relocate(&block->model);
	XVT_ASSERT_INT_EQ(memcmp(block, before, sizeof *block), 0);
	free(before);
	free(block);
}

static void check_relocate_node(void)
{
	struct block *original = malloc(sizeof *original);
	XVT_ASSERT_TRUE(original != NULL);
	build(original);
	struct block *moved = copy(original);
	xvt_opt_relocate_node(&moved->wing, (intptr_t)((uintptr_t)moved -
						       (uintptr_t)original));

	/* The node and everything below it move. */
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.p_name);
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.p_children);
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_SAME_OFFSET(original, moved, wing_children[i]);
	}
	XVT_ASSERT_SAME_OFFSET(original, moved, texture1.payload);
	XVT_ASSERT_SAME_OFFSET(original, moved, reference.payload);
	XVT_ASSERT_TRUE(moved->texture_data[1].palette ==
			original->texture_data[1].palette);
	XVT_ASSERT_INT_EQ(moved->reference.payload_count, 0);

	/* Nothing above it or beside it moves, the model header included. */
	XVT_ASSERT_TRUE(moved->model.self_marker == &original->model);
	XVT_ASSERT_TRUE(moved->model.root_nodes == original->model.root_nodes);
	XVT_ASSERT_TRUE(moved->hull.p_children == original->hull.p_children);
	XVT_ASSERT_TRUE(moved->hull_children[0] == original->hull_children[0]);
	XVT_ASSERT_TRUE(moved->texture0.payload == original->texture0.payload);
	XVT_ASSERT_TRUE(moved->vertices.payload == original->vertices.payload);
	free(moved);
	free(original);
}

static void check_resolve_cached(void)
{
	struct block *block = malloc(sizeof *block);
	XVT_ASSERT_TRUE(block != NULL);
	build(block);
	block->reference.payload_count = 0;

	/* The first use looks the name up and caches the node in payload_count. */
	XVT_ASSERT_TRUE(
		xvt_opt_resolve_cached(&block->model, &block->reference) ==
		&block->hull);
	XVT_ASSERT_TRUE(block->reference.payload_count ==
			(xvt_opt_value)&block->hull);

	/* Later uses return the cached node, even once the name no longer leads there. */
	block->hull.p_name = NULL;
	XVT_ASSERT_TRUE(
		xvt_opt_resolve_cached(&block->model, &block->reference) ==
		&block->hull);

	/* A failed lookup returns NULL, caches nothing, and is tried again on the next call. */
	block->reference.payload_count = 0;
	XVT_ASSERT_TRUE(xvt_opt_resolve_cached(&block->model,
					       &block->reference) == NULL);
	XVT_ASSERT_INT_EQ(block->reference.payload_count, 0);
	block->vertices.p_name = block->names[0];
	XVT_ASSERT_TRUE(
		xvt_opt_resolve_cached(&block->model, &block->reference) ==
		&block->vertices);
	free(block);
}

int main(void)
{
	check_relocate_moves_every_pointer();
	check_relocate_leaves_unmoved_model();
	check_relocate_node();
	check_resolve_cached();
	return 0;
}
