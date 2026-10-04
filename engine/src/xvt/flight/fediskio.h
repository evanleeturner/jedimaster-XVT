#ifndef XVT_FLIGHT_FEDISKIO_H
#define XVT_FLIGHT_FEDISKIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/assets/object_type.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum disk_io_string_id {
	DISK_IO_STR_ENTERING_COMBAT = 0x0,
	DISK_IO_STR_ENTERING_MELEE = 0x1,
	DISK_IO_STR_ENTERING_TRAINING = 0x2,
	DISK_IO_STR_ENTERING_CAMPAIGN = 0x3,
	DISK_IO_STR_UNUSED = 0x4,
	DISK_IO_STR_RES_320_NOT_SUPPORTED = 0x5,
	DISK_IO_STR_RES_512_NOT_SUPPORTED = 0x6,
	DISK_IO_STR_RES_640_NOT_SUPPORTED = 0x7,
	DISK_IO_STR_RES_NOT_SUPPORTED = 0x8,
	DISK_IO_STR_RES_320_USED_INSTEAD = 0x9,
	DISK_IO_STR_RES_512_USED_INSTEAD = 0xA,
	DISK_IO_STR_RES_640_USED_INSTEAD = 0xB,
	DISK_IO_STR_NEXT_RES_USED_INSTEAD = 0xC,
	DISK_IO_STR_USING_8BPP = 0xD,
	DISK_IO_STR_USING_16BPP = 0xE,
	DISK_IO_STR_HARDWARE_3D_NOT_SUPPORTED = 0xF,
	DISK_IO_STR_COM_FAILURE_RECEIVING = 0x10,
	DISK_IO_STR_COM_FAILURE_SENDING = 0x11,
	DISK_IO_STR_COM_FAILURE_WAITING = 0x12,
	DISK_IO_STR_RECOVERING = 0x13,
	DISK_IO_STR_RECOVERING_WAIT = 0x14,
	DISK_IO_STR_RESENDING_PACKET = 0x15,
	DISK_IO_STR_RESENDING_PACKET_WAIT = 0x16,
	DISK_IO_STR_ESC_BOOT_PLAYER = 0x17,
	DISK_IO_STR_ESC_DISCONNECT = 0x18,
	DISK_IO_STR_DISCONNECT_COUNTDOWN = 0x19,
	DISK_IO_STR_WAITING_FOR_OTHER_PLAYERS = 0x1A,
	DISK_IO_STR_OTHER_PLAYERS_STILL_LOADING = 0x1B,
	DISK_IO_STR_PLAYER_STILL_LOADING_MINUS = 0x1C,
	DISK_IO_STR_PLAYER_STILL_LOADING_PLUS = 0x1D,
	DISK_IO_STR_NO_CDROM = 0x1E,
	DISK_IO_STR_RETRY_FAIL = 0x1F,
} disk_io_string_id;

extern char g_file_name[256];
extern char *g_str_disk_io_messages[32];
extern uint8_t *g_flight_scratch_screen_buffer;
extern uint8_t *g_flight_aux_buffer_mirror;
extern uint16_t g_file_read_abort_flag;
extern xvt_file *g_stream;
extern unsigned int g_palette_generation_enabled;
extern char g_flight_palette_resource_file_name[12];
extern uint8_t g_rgb565_to_palette_index_lut[UINT16_MAX + 1u];
extern uint16_t g_warhead_guidance_pool_handle;
extern uint16_t g_craft_data_pool_handle;
extern uint16_t g_mobile_object_pool_handle;
extern uint16_t g_flight_aux_buffer_handle;
extern uint16_t g_mobile_object_char_data_handle;
extern uint16_t g_object_table_handle;
extern uint16_t g_string_data_handle;
extern uint16_t g_render_object_list_handle;
extern uint16_t g_flight_scratch_screen_buffer_handle;
extern uint16_t g_flight_small_font_handle;
extern uint16_t g_flight_offscreen_buffer_handle;
extern uint16_t g_flight_micro_font_handle;
extern uint16_t g_flight_medium_font_handle;
extern const int g_flight_group_rating_base_by_ai_level[7];
extern const int g_pilot_rating_promotion_point_thresholds[25];
extern const uint8_t g_placement_award_levels[24];
extern const int g_mission_award_win_thresholds[5];
extern const int g_mission_award_score_thresholds[3];

int16_t fe_disk_io_commit_flight_results(int unused1, int unused2);
uint16_t fe_disk_io_read_all_bytes_or_fatal(const char *file_name, void *dst);
void fe_disk_io_init_global_buffers(void);
void fe_disk_io_unlock_global_buffers(void);
void fe_disk_io_lock_global_buffers(void);
void fe_disk_io_free_flight_resources(void);
void fe_disk_io_load_resources(void);
unsigned int fe_disk_io_init_resources(void);
void fe_disk_io_build_model_def(uint8_t model_def_index,
				object_type_id object_type);
#ifndef XVT_MODERN
char fe_disk_io_show_retry_fail_prompt(void);
int fe_disk_io_show_fatal_error_message_and_wait_key(const char *message);
#endif
int fe_disk_io_open_global_stream(const char *file_name, const char *mode,
				  int prompt_on_fail, int location_mode);
int16_t fe_disk_io_close_global_stream(int16_t remove_file_on_error);
size_t fe_disk_io_read_with_retry_prompt(void *dst, size_t elem_size,
					 size_t elem_count, xvt_file *stream);
void fe_disk_io_fatal_error(file_error_string_id error_code);
void file_print_fatal_message_and_exit(const char *message, int exit_code);

#ifdef __cplusplus
}
#endif

#endif
