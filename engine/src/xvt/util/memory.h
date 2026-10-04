#ifndef XVT_UTIL_MEMORY_H
#define XVT_UTIL_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct memory_handle_table_state {
	/* Bytes in each handle's block; 0 when free. */
	size_t size_table[32768];
	void *ptr_table[32768]; /* Each handle's block; NULL when free. */
};

extern uint8_t g_handle_allocator_initialized;
extern unsigned int g_handle_allocation_attempt_count;
extern struct memory_handle_table_state g_handle_tables;

int memory_set_region_execute_read_write(void *address, size_t size);
uint16_t memory_alloc_handle_zeroed(size_t size, int legacy_tag);
uint16_t memory_alloc_handle(size_t size, int legacy_tag);
uint16_t memory_alloc_handle_internal(size_t size, int legacy_tag,
				      int clear_flag);
void memory_free_handle(unsigned int handle);
void *memory_get_handle_block(uint16_t handle);
void memory_handle_block_done_stub(uint16_t handle);

#ifdef __cplusplus
}
#endif

#endif
