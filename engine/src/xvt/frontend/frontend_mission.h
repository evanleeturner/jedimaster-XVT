#ifndef XVT_FRONTEND_FRONTEND_MISSION_H
#define XVT_FRONTEND_FRONTEND_MISSION_H

#include <stdint.h>

#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/* A mission file's header as the frontend loads it: the flight engine's
 * mission_header without its two counts. */
struct frontend_mission_header {
	uint8_t time_limit_min; /* Loaded; nothing in the frontend reads it. */
	uint8_t time_limit_sec; /* Loaded; nothing in the frontend reads it. */
	/* Loaded; frontend_mission_init_for_briefing sets 1. Nothing reads it. */
	uint8_t win_type;
	uint8_t backdrop; /* Loaded; nothing in the frontend reads it. */
	uint8_t rescue;	  /* Loaded; nothing in the frontend reads it. */
	/* Loaded; frontend_mission_init_for_briefing sets 0. Nothing reads it. */
	uint8_t all_waypoints_shown;
	uint8_t variables[8];  /* Loaded; nothing in the frontend reads it. */
	char iff_names[4][20]; /* Loaded; nothing in the frontend reads it. */
	/* Loaded; nothing in the frontend reads it. */
	mission_type
		mission_type; ///< One-byte mission mode; XVT uses the shared legacy values through SKIRMISH (0..4).
	/* The debriefing's player statistics add mission result rows only
	 * while this is 0. */
	uint8_t goals_unimportant; ///< Nonzero suppresses normal mission-goal importance/failure handling.
	/* Loaded; nothing in the frontend reads it. */
	uint8_t time_limit_minutes; ///< Mission countdown duration in whole minutes; zero disables the
	///< header-supplied limit.
	uint8_t reserved[61]; /* Loaded; nothing reads it. */
};

#pragma pack(pop)
typedef char xvt_size_frontend_mission_header
	[(sizeof(struct frontend_mission_header) == 158) ? 1 : -1];

#pragma pack(push, 1)

/* A mission file as the frontend loads it, without its briefings. */
struct frontend_mission {
	/* The mission's flight groups; the first flight_group_count are
	 * loaded. */
	struct xvt_flight_group flight_groups[48];
	/* In-flight messages, each in the slot the file gives before it; only
	 * the loaders touch them. */
	struct mission_message messages[64];
	/* Each team's global goals, as many as the file gives for it; only the
	 * loaders touch them. */
	struct global_goal global_goals[10][7];
	struct frontend_mission_header header; /* The file's header. */
	/* Team records; those the file flags absent stay zero. The screens
	 * read their names. */
	struct team teams[10];
	/* Flight groups loaded; most readers take it as a signed 16-bit
	 * count. */
	uint16_t flight_group_count;
	uint16_t message_count; /* Messages loaded; only the loaders read it. */
	/* Never read or written by name; the loaders' memset leaves it 0. */
	uint16_t unused13df0;
	/* The file's first word; the loaders accept 12, 13 and 14 and stop
	 * after it otherwise. Only they read it. */
	uint16_t format_version;
};

#pragma pack(pop)
typedef char xvt_size_frontend_mission
	[(sizeof(struct frontend_mission) == 81396) ? 1 : -1];

typedef enum frontend_mission_session_mode {
	FRONTEND_MISSION_SESSION_NONE = 0x0,
	FRONTEND_MISSION_SESSION_SINGLEPLAYER = 0x2,
	FRONTEND_MISSION_SESSION_NET_CLIENT = 0x3,
	FRONTEND_MISSION_SESSION_NET_HOST = 0x4,
} frontend_mission_session_mode;

extern struct frontend_mission g_frontend_mission;
extern frontend_mission_session_mode g_frontend_mission_session_mode;

int frontend_mission_load_for_briefing(void);
void frontend_mission_init_for_briefing(void);
void frontend_mission_load_current_with_briefing(void);
void frontend_mission_load_file(const char *file_name,
				struct frontend_mission *out_mission);
void frontend_mission_load_current(void);
void frontend_mission_init_player_state(void);

#ifdef __cplusplus
}
#endif

#endif
