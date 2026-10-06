/* Tests for xvt/frontend/briefing_text.c, which frees the briefing's text
 * buffers. The checks put buffers of their own in some slots of each table
 * and ask AddressSanitizer's allocator, which the test program always runs
 * under, whether each one is still allocated after the call. No game data is
 * read.
 *
 * GCC and Clang only, for the allocator query. */
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/briefing_text.h"

/* AddressSanitizer's allocator query, from its allocator interface: nonzero
 * while the block at that address is allocated. Declared here because the
 * header does not ship with every compiler install. */
int __sanitizer_get_ownership(const volatile void *pointer);

static int is_allocated(const void *buffer)
{
	return __sanitizer_get_ownership(buffer) != 0;
}

static char *new_buffer(void)
{
	char *buffer = malloc(40);
	XVT_ASSERT_TRUE(buffer != NULL);
	XVT_ASSERT_TRUE(is_allocated(buffer));
	return buffer;
}

/* Every non-NULL buffer is freed, the first and last slot of each table
 * included, and the empty slots are passed over. The pointers are left as
 * they were. */
static void check_free_buffers(void)
{
	memset(g_briefing_map_label_texts, 0,
	       sizeof g_briefing_map_label_texts);
	memset(g_briefing_text_blocks, 0, sizeof g_briefing_text_blocks);
	memset(g_briefing_unused_buffers, 0, sizeof g_briefing_unused_buffers);
	char *labels[2] = {new_buffer(), new_buffer()};
	char *blocks[2] = {new_buffer(), new_buffer()};
	char *spare[2] = {new_buffer(), new_buffer()};
	g_briefing_map_label_texts[0] = labels[0];
	g_briefing_map_label_texts[31] = labels[1];
	g_briefing_text_blocks[0] = blocks[0];
	g_briefing_text_blocks[31] = blocks[1];
	g_briefing_unused_buffers[0] = spare[0];
	g_briefing_unused_buffers[19] = spare[1];

	briefing_text_free_allocated_buffers();
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_TRUE(!is_allocated(labels[i]));
		XVT_ASSERT_TRUE(!is_allocated(blocks[i]));
		XVT_ASSERT_TRUE(!is_allocated(spare[i]));
	}
	XVT_ASSERT_TRUE(g_briefing_map_label_texts[31] == labels[1]);
	XVT_ASSERT_TRUE(g_briefing_text_blocks[0] == blocks[0]);
	XVT_ASSERT_TRUE(g_briefing_unused_buffers[19] == spare[1]);
	XVT_ASSERT_TRUE(g_briefing_map_label_texts[1] == NULL);
}

/* The exit frees the buffers the same way and returns 1. */
static void check_free_buffers_exit(void)
{
	memset(g_briefing_map_label_texts, 0,
	       sizeof g_briefing_map_label_texts);
	memset(g_briefing_text_blocks, 0, sizeof g_briefing_text_blocks);
	memset(g_briefing_unused_buffers, 0, sizeof g_briefing_unused_buffers);
	char *block = new_buffer();
	g_briefing_text_blocks[5] = block;
	XVT_ASSERT_INT_EQ(briefing_text_free_allocated_buffers_exit(), 1);
	XVT_ASSERT_TRUE(!is_allocated(block));
	g_briefing_text_blocks[5] = NULL;
}

int main(void)
{
	check_free_buffers();
	check_free_buffers_exit();
	return 0;
}
