#ifndef XVT_FRONTEND_PILOT_H
#define XVT_FRONTEND_PILOT_H

#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as int32_t in the binary (IDB enum pilot_rating). */
typedef int32_t pilot_rating;

enum {
	PILOT_RATING_TARGET_DRONE = 0x0,
	PILOT_RATING_GROUND_CREW = 0x1,
	PILOT_RATING_TRAINEE = 0x2,
	PILOT_RATING_FLIGHT_CADET = 0x3,
	PILOT_RATING_OFFICER_4TH_CLASS = 0x4,
	PILOT_RATING_OFFICER_3RD_CLASS = 0x5,
	PILOT_RATING_OFFICER_2ND_CLASS = 0x6,
	PILOT_RATING_OFFICER_1ST_CLASS = 0x7,
	PILOT_RATING_VETERAN_4TH_GRADE = 0x8,
	PILOT_RATING_VETERAN_3RD_GRADE = 0x9,
	PILOT_RATING_VETERAN_2ND_GRADE = 0xA,
	PILOT_RATING_VETERAN_1ST_GRADE = 0xB,
	PILOT_RATING_ACE_4TH_LEVEL = 0xC,
	PILOT_RATING_ACE_3RD_LEVEL = 0xD,
	PILOT_RATING_ACE_2ND_LEVEL = 0xE,
	PILOT_RATING_ACE_1ST_LEVEL = 0xF,
	PILOT_RATING_TOP_ACE_4TH_ORDER = 0x10,
	PILOT_RATING_TOP_ACE_3RD_ORDER = 0x11,
	PILOT_RATING_TOP_ACE_2ND_ORDER = 0x12,
	PILOT_RATING_TOP_ACE_1ST_ORDER = 0x13,
	PILOT_RATING_JEDI_4TH_DEGREE = 0x14,
	PILOT_RATING_JEDI_3RD_DEGREE = 0x15,
	PILOT_RATING_JEDI_2ND_DEGREE = 0x16,
	PILOT_RATING_JEDI_1ST_DEGREE = 0x17,
	PILOT_RATING_JEDI_MASTER = 0x18,
	PILOT_RATING_RESERVED_25 = 0x19,
	PILOT_RATING_RESERVED_26 = 0x1A,
	PILOT_RATING_RESERVED_27 = 0x1B,
	PILOT_RATING_RESERVED_28 = 0x1C,
	PILOT_RATING_RESERVED_29 = 0x1D,
	PILOT_RATING_RESERVED_30 = 0x1E,
	PILOT_RATING_RESERVED_31 = 0x1F,
};

/* Nothing in the engine uses this type. Its fields match the start of
 * pilot_data, except that mission_description_ids has 5 entries where pilot_data
 * has 6. */
struct pilot_data_selection {
	char name[14];	     /* Never read or written by name. */
	int total_score;     /* Never read or written by name. */
	int local_player_id; /* Never read or written by name. */
	/* Never read or written by name. */
	/* Persisted marker set to 1 when launch/debrief session state is
	 * captured; no XVT reader is identified. */
	int launch_session_marker;
	int is_host; /* Never read or written by name. */
	/* Never read or written by name. */
	unsigned int num_human_players_last_mission;
	int session_mode; /* Never read or written by name. */
	/* Never read or written by name. */
	uint8_t xvt_record_payload
		[672]; ///< Opaque 672-byte payload from the XvT-compatible pilot-record prefix.
	int team; /* Never read or written by name. */
	/* Never read or written by name. */
	mission_directory_id mission_directory_id;
	int mission_description_ids[5]; /* Never read or written by name. */
};

/* Stored as int32_t in the binary (IDB enum pilot_promotion_delta). */
typedef int32_t pilot_promotion_delta;

enum {
	PILOT_PROMOTION_DEMOTION = -1,
	PILOT_PROMOTION_NONE = 0x0,
	PILOT_PROMOTION_PROMOTION = 0x1,
};

int pilot_delete_current(void);
int pilot_create_new(const char *pilot_name);
int pilot_save(int use_temporary_file);
int pilot_find_and_load_by_name(const char *pilot_name);
int pilot_parse_command_line(const char *cmd_line);
int pilot_load_xvt_record(xvt_file *stream);
int pilot_load_from_path(const char *base_pilot_path);
int pilot_write_xvt_record(const char *file_name, xvt_file *stream);

#ifdef __cplusplus
}
#endif

#endif
