#include "xvt/frontend/mission_setup.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/campaign_task.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/frontend_cleanup.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/mission_dialogs.h"
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/assets/model_preview.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

#ifdef XVT_MODERN
#include <strings.h>
#endif

/* Folder of each mission type, by mission_directory_id: training, melee,
 * tournament, combat engagement, battle, campaign. Mission lists and files are
 * read as "<folder>\<file>". Constant. */
// GLOBAL: XVT 0x52C1E8
const char *g_mission_directory_names[6] = {"train",  "melee",	"tourn",
					    "combat", "battle", "campaign"};
/* Offset from FRONTSTR_273_NONE of the name of each flight group warhead code,
 * 0 to 10: code 10 gives 2, and codes 2 to 9 give 3 to 10. Constant; only
 * mission_setup_get_warhead_type reads it. */
// GLOBAL: XVT 0x52C6A8
const int g_warhead_type_map[11] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 2};
/* Craft species of each preset craft choice: index 0 holds 0, preset option 0
 * being the flight group's own craft; 1 to 5 are the Rebel craft (Z-95, X-wing,
 * Y-wing, A-wing, B-wing) and 6 to 10 the Imperial ones (TIE fighter,
 * interceptor, bomber, advanced, assault gunboat). Flight groups of preset
 * category 3 index it at their option plus 5. Constant. */
// GLOBAL: XVT 0x52C564
const int g_preset_craft_types[11] = {0, 14, 1, 2, 3, 4, 5, 6, 7, 8, 16};
/* For each craft species 0 to 19, the craft of the other side that
 * mission_setup_init_craft_loadout, its only reader, switches to when a melee or
 * tournament sequence past its first mission gives the pilot a craft of the
 * wrong side; 0 for none. Constant. */
// GLOBAL: XVT 0x52C550
const uint8_t g_craft_iff_counterpart[20] = {0, 6, 16, 8, 16, 14, 1, 14, 3, 3,
					     3, 3, 3,  3, 5,  5,  2, 0,	 0, 0};
/* World position (x, y, z) of the craft model in the briefing craft screen's
 * preview, by craft species 0 to 16; only mission_setup_draw_craft_loadout reads
 * it, without checking that the species is under 17. Constant. */
// GLOBAL: XVT 0x52C5D8
static const struct model_preview_craft_position
	g_model_preview_craft_positions[17] = {
		{0, 0, 0},	 {25, -30, 20}, {0, -120, 20}, {10, -55, 20},
		{0, 0, 0},	 {0, 400, 30},	{40, 390, 45}, {-30, 240, 10},
		{-10, 200, 10},	 {0, 0, 0},	{0, 0, 0},     {0, 0, 0},
		{0, 0, 0},	 {0, 0, 0},	{40, 10, 40},  {0, 0, 0},
		{-10, -200, 10},
};
/* Flight group given to each team slot: entry team * 8 + slot, beside
 * g_mission_setup_player_assignments.team_player_ids[team][slot]; -1 for none. Many
 * functions write it, chiefly mission_setup_flight_assignment_update, which sets
 * all 80 entries to -1 on its frame 0 unless g_frontend_skip_screen_entry_setup is
 * set, the other flight assignment functions, and
 * frontend_net_process_network_packets from the flight assignment packets. */
// GLOBAL: XVT 0xAA5AC0
int g_mission_setup_player_flight_group_indices[80] = {0};
/* Five ramps of eight colors, dark to bright, from 0x48 to 0xFC in each lit
 * channel: green, red, yellow, blue and magenta. The briefing map's label and
 * craft icon highlights read it. Only mission_setup_flight_assignment_update
 * writes it, on its frame 0; until then every entry is 0. */
// GLOBAL: XVT 0xAA5C10
int g_text_shade_ramps[5][8] = {{0}};
/* The team assignment: team_player_ids holds the player id in each of the 8 slots
 * of the 10 teams, slot 0 being the team's captain, 0 for an empty slot, and
 * assigned_player_ids the ids of the players who have a team slot. Many functions
 * write it, chiefly the team assignment screen and the functions it calls, the
 * prune functions, and frontend_net_process_network_packets from the team
 * assignment packets. */
// GLOBAL: XVT 0xAA5CB0
struct mission_setup_player_assignments g_mission_setup_player_assignments = {
	{0}, {0}};
/* Player flight groups of each team in the loaded mission, which is the team's
 * number of player slots. Only mission_setup_update_team_counts writes it. */
// GLOBAL: XVT 0xAA5E10
int g_team_player_flight_group_count[10] = {0};
/* Id of the player being dragged on the team or flight assignment screen; 0 for
 * none. 5 functions write it: mission_setup_team_assignment_update,
 * mission_setup_draw_unassigned_players, mission_setup_draw_team_assignments,
 * mission_setup_flight_assignment_update and
 * mission_setup_draw_flight_assignments. */
// GLOBAL: XVT 0xAA5E38
int g_mission_setup_dragged_player_id = 0;
/* Ids of the players someone has started to drag on the team or flight
 * assignment screen, which no one else may drag; the first
 * g_mission_setup_reserved_player_count entries count. Written by the team and
 * flight assignment screens and their draw functions when a drag starts or a
 * reservation packet arrives, and by mission_briefing_craft_selection_update from
 * the reservation packets; cleared when those screens start. */
// GLOBAL: XVT 0xAA5E40
int g_mission_setup_reserved_player_ids[8] = {0};
/* Entries in use in g_mission_setup_reserved_player_ids, 0 to 8; written beside
 * it. */
// GLOBAL: XVT 0xAA5E60
int g_mission_setup_reserved_player_count = 0;
/* 1 when the team assignment screen went on without showing its teams (one team
 * with one player, single-slot melee teams, a solo Quick Start, a single 8-slot
 * training team, a solo combat engagement or battle, or a debriefing return); 0
 * once it shows them. The flight assignment and briefing screens read it to
 * offer their way back as a return to mission selection. Only
 * mission_setup_team_assignment_update writes it. */
// GLOBAL: XVT 0xAA5E64
int g_mission_setup_team_assignment_skipped = 0;
/* Teams with at least one player flight group in the loaded mission, 0 to 10;
 * the team loops take teams 0 to g_team_count - 1. Only
 * mission_setup_update_team_counts writes it. */
// GLOBAL: XVT 0xAA5E68
int g_team_count = 0;
/* Heap array of 100 ship list entries that ship_list_load fills from
 * frontres\frntspec.lst for the tech library and the briefing's craft screen;
 * NULL until loaded. ship_list_load allocates it;
 * mission_briefing_craft_selection_exit, tech_library_update,
 * frontend_handle_common_screen_controls and, in the modern build,
 * xvt_frontend_task_shutdown free it and set it to NULL. */
// GLOBAL: XVT 0xAA60F4
struct ship_list_entry *g_ship_list = NULL;
/* Index in g_ship_list of each craft species' model. It starts with species 2 to
 * 8 at 1 to 7, 14 at 8, 16 at 9 and the rest at 0; ship_list_load, its only
 * writer, sets the entry of each species under 17 that the list names. */
// GLOBAL: XVT 0x52C590
int g_ship_type_to_ship_list_index[18] = {0, 0, 1, 2, 3, 4, 5, 6, 7,
					  0, 0, 0, 0, 0, 8, 0, 9, 0};
/* Entries ship_list_load kept in g_ship_list; the tech library steps through that
 * many. Written by ship_list_load when the list opens and, set to 0, by
 * xvt_frontend_task_shutdown in the modern build. */
// GLOBAL: XVT 0xAA60FC
int g_ship_count = 0;
/* Index in g_frontend_mission.flight_groups of the local player's flight group on
 * the briefing's craft screen. Only mission_setup_init_craft_loadout writes it. */
// GLOBAL: XVT 0xAA6104
int g_mission_setup_selected_flight_group_index = 0;
/* The local player's craft choice among its flight group's own craft: 0 the
 * group's craft, n its optional craft n - 1. Written by
 * mission_setup_init_craft_loadout (0, then any stepping to the right side),
 * mission_setup_update_craft_loadout, and frontend_net_process_network_packets from
 * the host's CRAFT_LOADOUT when craft selection is host only. */
// GLOBAL: XVT 0xAA60F8
int g_mission_setup_selected_flight_group_craft_option_index = 0;
/* The local player's choice among the preset craft, an index into
 * g_preset_craft_types (the option plus 5 for category 3); 0 for the flight
 * group's own craft. Written as
 * g_mission_setup_selected_flight_group_craft_option_index is. */
// GLOBAL: XVT 0xAA6100
int g_mission_setup_selected_preset_craft_option_index = 0;
/* Preset craft choices the local player's flight group offers: 11 for preset
 * category 1, 6 for categories 2 and 3, 0 for categories 0 and 4. Only
 * mission_setup_init_craft_loadout writes it. */
// GLOBAL: XVT 0xAA6108
int g_mission_setup_preset_craft_option_count = 0;
/* The local player's warhead choice: 0 the flight group's default, n its
 * optional warhead n - 1. Written by mission_setup_init_craft_loadout (0),
 * mission_setup_update_craft_loadout, and frontend_net_process_network_packets from
 * the host's CRAFT_LOADOUT when craft selection is host only. */
// GLOBAL: XVT 0xAA60D8
int g_mission_setup_selected_warhead_option_index = 0;
/* The local player's beam weapon choice: 0 the flight group's default, n its
 * optional beam n - 1. Written as g_mission_setup_selected_warhead_option_index
 * is. */
// GLOBAL: XVT 0xAA60E4
int g_mission_setup_selected_beam_option_index = 0;
/* The local player's countermeasure choice: 0 the flight group's default, n its
 * optional countermeasure n - 1. Written as
 * g_mission_setup_selected_warhead_option_index is. */
// GLOBAL: XVT 0xAA610C
int g_mission_setup_selected_countermeasure_option_index = 0;
/* Waves of the local player's flight group, or of its chosen optional craft,
 * less one as the mission stores them; the craft screen shows it plus 1 when
 * craft waves are on their default. Written by mission_setup_init_craft_loadout,
 * mission_setup_update_craft_loadout, and frontend_net_process_network_packets from
 * the host's CRAFT_LOADOUT when craft selection is host only. */
// GLOBAL: XVT 0xAA60E8
int g_mission_setup_selected_wave_count_minus_one = 0;
/* Craft in the local player's flight group, or of its chosen optional craft,
 * from the mission. Written as g_mission_setup_selected_wave_count_minus_one is. */
// GLOBAL: XVT 0xAA60EC
int g_mission_setup_selected_craft_count = 0;
/* Craft choices the local player's flight group offers from its own list: 1 for
 * preset category 0, 1 plus its optional craft for category 4, 0 for the preset
 * categories 1 to 3. Only mission_setup_init_craft_loadout writes it. */
// GLOBAL: XVT 0xAA60F0
int g_mission_setup_flight_group_craft_option_count = 0;
/* Warhead choices the local player's flight group offers: its nonzero optional
 * warheads, plus 1 when it has any or a default warhead. Only
 * mission_setup_init_craft_loadout writes it. */
// GLOBAL: XVT 0xAA60D4
int g_mission_setup_warhead_option_count = 0;
/* Beam weapon choices the local player's flight group offers: its nonzero
 * optional beams, plus 1 when it has any or a default beam. Only
 * mission_setup_init_craft_loadout writes it. */
// GLOBAL: XVT 0xAA60DC
int g_mission_setup_beam_option_count = 0;
/* Countermeasure choices the local player's flight group offers: its nonzero
 * optional countermeasures, plus 1 when it has any or a default one. Only
 * mission_setup_init_craft_loadout writes it. */
// GLOBAL: XVT 0xAA60E0
int g_mission_setup_countermeasure_option_count = 0;
/* Heap array of the current mission type's list entries, g_mission_count of
 * them; NULL when none is loaded. mission_setup_load_mission_list frees and
 * reloads it, and many screens' exit functions free it and set it to NULL.
 * mission_setup_battle_choice_build_list swaps it out for a moment, and the pilot
 * record takes a loaded list over as g_battle_mission_list. */
// GLOBAL: XVT 0xAA6114
struct mission_list_entry *g_mission_list = NULL;
/* Index in g_mission_list of the selected mission, the entry whose mission_idx is
 * the current type's selected description id, or g_mission_count when none is.
 * Many functions write it, chiefly frontend_mission_load_current and the mission
 * pickers of the mission setup screens. */
// GLOBAL: XVT 0xAA6134
int g_selected_mission_list_index = 0;
/* 1 while a game is under way that leaving would abort: the mission setup and
 * common screen controls then ask before leaving. mission_setup_draw_player_roster
 * sets it to 1 when more than one player is ready and to 0 otherwise, and
 * mission_setup_flight_assignment_update to 1 in a solo game; screens such as the
 * concourse, debriefing, join and host screens set it to 0. */
// GLOBAL: XVT 0xAA613C
int g_frontend_game_session_in_progress = 0;
/* 1 on the host of a network game and in a solo game, 0 on a client. Many
 * functions write it, chiefly the host and join screens, the concourse, the
 * flight loading screen, and mission_setup_enter_next_mission and
 * mission_setup_enter_current_mission, which set 1 in a solo game. */
// GLOBAL: XVT 0xAA6144
int g_mission_setup_is_host = 0;
/* 1 once a mission is starting: while it is set the lobby packets send the
 * eight g_mp_roster entries as they are, after clearing departed players,
 * instead of the network roster's ready players. mission_setup_update sets it to
 * 1 on the host's Begin and when the host's mission start arrives, and
 * mission_setup_enter_next_mission and mission_setup_enter_current_mission outside a
 * solo game. mission_setup_update's frame 0 and many other screens set it to
 * 0. */
// GLOBAL: XVT 0xB6A2A8
int g_mission_setup_roster_authoritative = 0;
/* Entries in g_mission_list. Only mission_setup_load_mission_list, which sets it to
 * 0 before loading, and mission_setup_battle_choice_build_list, which puts it back
 * after loading the battle list, write it. */
// GLOBAL: XVT 0xAA6138
unsigned int g_mission_count = 0;
/* The game's players as the mission setup screens show them: up to 8 entries,
 * each a name, an id (0 for an empty entry), a rating and the loadout choices.
 * Many functions write it, chiefly frontend_net_process_network_packets from the
 * host's lobby and loadout packets, the lobby senders, which clear departed
 * players, and in a solo game mission_setup_update and the Enter functions,
 * which put the pilot in entry 0. */
// GLOBAL: XVT 0xAA6150
struct mp_roster_entry g_mp_roster[8] = {{0}};
/* Entries in g_battle_mission_list. Set by mission_setup_battle_choice_build_list
 * and pilot_record_draw_mission_achievements_page, and to 0 by
 * mission_setup_battle_choice_exit. */
// GLOBAL: XVT 0xB6A244
int g_battle_mission_list_count = 0;
/* Index in g_pilot_data.network_players of the local player's entry. Written by
 * frontend_mission_init_player_state and mission_setup_prune_disconnected_players,
 * which leaves it as it was when no entry holds the local id. */
// GLOBAL: XVT 0xB6A24C
int g_local_pilot_network_player_index = 0;
/* Heap array of mission list entries: on the battle choice screen, the missions
 * of the current battle (mission_setup_battle_choice_build_list); on the pilot
 * record's achievements page, the battle list. NULL when none.
 * mission_setup_battle_choice_exit, concourse_exit and
 * pilot_record_draw_mission_achievements_page free it and set it to NULL. */
// GLOBAL: XVT 0xB6A2B4
struct mission_list_entry *g_battle_mission_list = NULL;
/* GetTickCount() at the battle choice countdown's latest frame, in ms. Only
 * mission_setup_battle_choice_update writes it. */
// GLOBAL: XVT 0x66D880
int g_battle_choice_clock_ms = 0;
/* First row the battle choice screen's mission list shows. Only
 * mission_setup_battle_choice_draw_list writes it: on its frame 0, and from its
 * scrollbar. */
// GLOBAL: XVT 0x66D884
int g_battle_choice_scroll_offset = 0;
/* Milliseconds left to choose the next battle mission: 120000 on the battle
 * choice screen's frame 0, lowered each frame by the time elapsed and to a
 * host's BRIEFING_COUNTDOWN value when that is lower, and held at 0 once under
 * 0. Only mission_setup_battle_choice_update writes it. */
// GLOBAL: XVT 0x66D888
int g_battle_choice_remaining_ms = 0;
/* GetTickCount() at the battle choice countdown's previous frame, in ms; the
 * difference to g_battle_choice_clock_ms is the time elapsed. Only
 * mission_setup_battle_choice_update writes it. */
// GLOBAL: XVT 0x66D88C
int g_battle_choice_previous_clock_ms = 0;
/* Rows of the battle choice screen's mission list: available entries plus
 * section headings. Only mission_setup_battle_choice_draw_list writes it, on its
 * frame 0. */
// GLOBAL: XVT 0x66D890
int g_battle_choice_row_count = 0;
/* Whole seconds left when the host last sent BRIEFING_COUNTDOWN; 120 on the
 * battle choice screen's frame 0. Only mission_setup_battle_choice_update writes
 * it. */
// GLOBAL: XVT 0x66D894
int g_battle_choice_last_sent_second = 0;
/* 1 once the battle choice countdown has run out and been handled, so the
 * captain's choice is sent once; 0 on the screen's frame 0. Only
 * mission_setup_battle_choice_update writes it. */
// GLOBAL: XVT 0x66D898
int g_battle_choice_timeout_handled = 0;
/* 1 while the mission setup screens use g_pilot_data.faction_statistics[2], the
 * mission state of games outside solo play, rather than the current faction's
 * entry. mission_setup_update, its only writer, sets it on frame 0: 1 outside a
 * solo game, 0 in one. mission_setup_exit saves the state back to the entry it
 * names. */
// GLOBAL: XVT 0x665D24
int g_mission_setup_use_combat_sim_pilot_state = 0;
/* Panel the network mission setup screen shows below the description: the
 * player roster or the game settings. mission_setup_update sets the roster on
 * frame 0, and mission_setup_draw_mission_type_controls's Players and Settings
 * buttons switch it. */
// GLOBAL: XVT 0x665D10
mission_setup_active_panel g_mission_setup_active_panel =
	MISSION_SETUP_PANEL_PLAYERS;
/* GetTickCount() when the host last sent the ready roster and lobby selection,
 * in ms. mission_setup_update, its only writer, sets it on frame 0 and after
 * each send. */
// GLOBAL: XVT 0x665D14
int g_mission_setup_last_host_broadcast_ms = 0;
/* Rows of the mission setup screen's mission list: available missions plus
 * section headings. Only mission_setup_draw_mission_list writes it, on its frame
 * 0. */
// GLOBAL: XVT 0x665D1C
int g_mission_setup_mission_list_row_count = 0;
/* First row the mission setup screen's mission list shows. Only
 * mission_setup_draw_mission_list writes it: on its frame 0, and from its
 * scrollbar. */
// GLOBAL: XVT 0x665D20
int g_mission_setup_mission_list_scroll_offset = 0;
/* Index in g_mp_roster of the player the host selected in the roster, whom the
 * boot button removes; -1 for none. mission_setup_update sets -1 on frame 0 and
 * on a lobby state, mission_setup_draw_mission_list on a lobby state, and
 * mission_setup_draw_player_roster sets or clears it on a click. */
// GLOBAL: XVT 0x665D18
int g_mission_setup_selected_player_roster_index = 0;
/* On a network client, whether the host's selected battle has an active saved
 * continuation, from the host's BATTLE_PROGRESS packet
 * (frontend_net_process_network_packets). Set to 0 by mission_setup_update on frame
 * 0, and when a sequence starts without continuing by
 * mission_setup_team_assignment_update in the original build and
 * xvt_campaign_task_enter_teams in the modern one. */
// GLOBAL: XVT 0xAA6120
int g_remote_battle_continuation_active = 0;
/* On a network client, the host's continue choice from BATTLE_PROGRESS
 * (SEQUENCE_RESTART or SEQUENCE_CONTINUE); written and cleared as
 * g_remote_battle_continuation_active is. */
// GLOBAL: XVT 0xAA6124
int g_remote_battle_sequence_continuation_choice = 0;
/* On a network client, the current mission index of the host's saved battle,
 * from BATTLE_PROGRESS; written and cleared as g_remote_battle_continuation_active
 * is. Nothing reads it. */
// GLOBAL: XVT 0xAA6128
int g_remote_battle_last_completed_mission_index = 0;
/* On a network client, the Rebel victories of the host's saved battle, from
 * BATTLE_PROGRESS; written and cleared as g_remote_battle_continuation_active
 * is. */
// GLOBAL: XVT 0xAA612C
int g_remote_battle_rebel_victory_count = 0;
/* On a network client, the Imperial victories of the host's saved battle, from
 * BATTLE_PROGRESS; written and cleared as g_remote_battle_continuation_active
 * is. */
// GLOBAL: XVT 0xAA6130
int g_remote_battle_imperial_victory_count = 0;
/* 1 when a screen hands back to an earlier one that should keep its state
 * rather than start over: the team assignment screen then keeps and prunes its
 * assignments, the flight assignment screen keeps its flight groups, and the
 * join screen keeps its game list or, for a transport other than IPX, returns
 * to the concourse. Many functions write it, chiefly the mission setup,
 * briefing and debriefing screens when they go back (1), the team assignment
 * and join screens once they have read it (0), and the flight assignment
 * screen, which sets 1 after its own setup. */
// GLOBAL: XVT 0x52C184
int g_frontend_skip_screen_entry_setup = 0;
/* Frames before the host's Begin button on the mission setup screen shows
 * again: 240 after a Begin press, 24 when frontend_net_process_network_packets
 * admits a joining player. mission_setup_update sets it to 0 on frame 0 and
 * lowers it by 1 each frame. */
// GLOBAL: XVT 0xAA6140
int g_mission_setup_begin_button_lockout_frames = 0;
/* 1 while the team assignment screen shows the mission description in place of
 * the team slots, a choice offered in combat engagement sequences. Set by
 * mission_setup_update_team_controls's buttons, and to 0 on the screen's frame 0
 * by mission_setup_team_assignment_update in the original build and
 * xvt_campaign_task_enter_teams in the modern one. */
// GLOBAL: XVT 0x6691D0
int g_mission_setup_show_description_panel = 0;
/* GetTickCount() at the flight assignment countdown's latest frame, in ms. Only
 * mission_setup_flight_assignment_update writes it. */
// GLOBAL: XVT 0x6691D8
int g_mission_setup_countdown_clock_ms = 0;
/* Milliseconds left before the flight assignments are final: 120000 on the
 * flight assignment screen's frame 0, lowered each frame by the time elapsed
 * and to a countdown packet's value when that is lower, and held at 0 once
 * under 0. Only mission_setup_flight_assignment_update writes it. */
// GLOBAL: XVT 0x669250
int g_mission_setup_launch_countdown_ms = 0;
/* GetTickCount() at the flight assignment countdown's previous frame, in ms;
 * the difference to g_mission_setup_countdown_clock_ms is the time elapsed. Only
 * mission_setup_flight_assignment_update writes it. */
// GLOBAL: XVT 0x669254
int g_mission_setup_countdown_previous_clock_ms = 0;
/* 1 once this player, as its team's captain, sent FLIGHT_ASSIGNMENTS_READY when
 * the flight assignment countdown ran out; 0 on the screen's frame 0. Only
 * mission_setup_flight_assignment_update writes it. */
// GLOBAL: XVT 0x669788
int g_mission_setup_launch_signal_sent = 0;
/* Whole seconds left when the host last sent the flight assignment countdown;
 * 120 on the screen's frame 0. Only mission_setup_flight_assignment_update writes
 * it. */
// GLOBAL: XVT 0x66978C
int g_mission_setup_last_broadcast_countdown_second = 0;
/* 1 while the flight assignment screen shows the mission description and the
 * full flight slot list in place of the briefing map, for a team with more than
 * 4 player flight groups. Written by mission_setup_flight_assignment_update on
 * frame 0 and by mission_setup_draw_assignment_controls's buttons. */
// GLOBAL: XVT 0x669790
static int g_mission_setup_use_expanded_assignment_layout = 0;
/* How the debriefing hands back to the mission setup screens: replaying the
 * current mission or going on to the next. mission_setup_team_assignment_update,
 * finding it set, clears it and goes straight to flight assignment; while it is
 * ENTER_CURRENT_MISSION no saved campaign or battle is offered on entry.
 * Written by mission_debrief_update and mission_setup_team_assignment_update. */
// GLOBAL: XVT 0xA91B8C
mission_setup_debrief_transition g_mission_setup_debrief_transition =
	MISSION_SETUP_DEBRIEF_TRANSITION_NONE;

/* The mission setup screen's exit callback. Frees g_mission_list and
 * g_mission_text and sets both to NULL, frees the "background" image, and saves
 * the pilot's team, mission type, selected mission per type and sequence fields
 * back into g_pilot_data.faction_statistics: entry 2 while
 * g_mission_setup_use_combat_sim_pilot_state is set, else the current faction's
 * entry. Then resets the scrollable controls and clears the mouse input gate.
 * Ignores frame_counter and returns 0. */
// FUNCTION: XVT 0x4E1470
int mission_setup_exit(int frame_counter)
{
	(void)frame_counter;

	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	front_image_free_resource_by_name("background");
	if (g_mission_setup_use_combat_sim_pilot_state) {
		g_pilot_data.faction_statistics[2].team = g_pilot_data.team;
		g_pilot_data.faction_statistics[2].mission_directory_id =
			g_pilot_data.mission_directory_id;
		memcpy(g_pilot_data.faction_statistics[2]
			       .mission_description_ids,
		       g_pilot_data.mission_description_ids,
		       sizeof(g_pilot_data.faction_statistics[2]
				      .mission_description_ids));
		g_pilot_data.faction_statistics[2].mission_sequence_active =
			g_pilot_data.mission_sequence_active;
		g_pilot_data.faction_statistics[2]
			.saved_mission_description_id =
			g_pilot_data.saved_mission_description_id;
	} else {
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.team = g_pilot_data.team;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_directory_id =
			g_pilot_data.mission_directory_id;
		memcpy(g_pilot_data
			       .faction_statistics[g_pilot_data
							   .current_faction_id]
			       .mission_description_ids,
		       g_pilot_data.mission_description_ids,
		       sizeof(g_pilot_data
				      .faction_statistics
					      [g_pilot_data.current_faction_id]
				      .mission_description_ids));
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_sequence_active =
			g_pilot_data.mission_sequence_active;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.saved_mission_description_id =
			g_pilot_data.saved_mission_description_id;
	}
	frontend_reset_scrollable_controls();
	frontend_mouse_clear_input_gate();
	return 0;
}

/* The mission setup screen, run once per frame: the player picks a mission type
 * and a mission, a network game's players gather, and Begin moves on to
 * mission_setup_team_assignment_update. Below, "outside a solo game" means
 * g_frontend_mission_session_mode is not FRONTEND_MISSION_SESSION_SINGLEPLAYER. On
 * frame 0 it resets the screen: it compacts g_mp_roster; clears
 * g_frontend_briefing_entered_count, g_mission_setup_begin_button_lockout_frames and
 * the five g_remoteBattle globals; loads the pilot's mission state from
 * g_pilot_data.faction_statistics, entry 2 outside a solo game (setting
 * g_mission_setup_use_combat_sim_pilot_state to 1), else the current faction's entry
 * (setting it to 0); allocates the 4096-byte g_mission_text; and clears the
 * three sequence states. A sequence left active (mission_sequence_active 1) puts
 * saved_mission_description_id back as the current type's selected mission and
 * moves the mission type from melee to tournament, from combat engagement to
 * battle, and from any other type to campaign; mission_sequence_active is then
 * cleared. It loads the mission, its text and team counts, clamps the
 * g_game_config settings the mission type does not allow, takes a saved battle's
 * length and setup choice or a saved campaign's setup choice, and clears the
 * eight network player slots and g_mp_roster_ready_flags; a solo game fills roster
 * entry 0 from the pilot. Every frame outside a solo game, the host sends the
 * ready roster and lobby selection when more than 5000 ms have passed since
 * g_mission_setup_last_host_broadcast_ms, and the packet type
 * frontend_net_process_network_packets returns is handled: a lobby state loads the
 * host's mission; a host cancel shuts the session down and leaves for the
 * concourse or the join screen; a mission start sets
 * g_mission_setup_roster_authoritative and moves to team assignment; a kick sends
 * a leave packet, shuts the session down and leaves; a pilot rating updates its
 * sender's roster entry. Its branch for a PLAYER_UNAVAILABLE packet never runs:
 * frontend_net_process_network_packets returns NET_PACKET_NONE for one. Then it
 * draws the game name (outside a solo game), title, mission name, description,
 * chat panel, version and pilot, and the settings panel (always in a solo game)
 * or the player roster, and handles the Quick Start or Previous button, the
 * Begin button, the mission list button, the mission type controls and, for a
 * network host on the players panel, the button that boots the selected player.
 * In a solo game Begin and Quick Start move to team assignment, first picking a
 * tournament, battle or campaign's first mission and staying when
 * mission_setup_select_first_sequence_mission returns 0; Begin sets
 * g_game_config.random_seed before that pick, and Quick Start sets
 * g_frontend_quick_start_launch_flag. A host's Begin is refused when the first
 * character of the mission's file name, read as a digit, is under the ready
 * player count; otherwise it picks a sequence's first mission likewise and
 * sends the lobby state and a mission start packet; either way it locks the
 * button for 240 frames. In the modern build each dialog returns 0 at once and
 * the action after it runs when the dialog closes. Returns 1 when
 * frontend_handle_common_screen_controls returns 1, the player having quit the
 * game; else 0. */
// FUNCTION: XVT 0x4E15D0
int mission_setup_update(int frame_counter)
{
	enum {
		NETWORK_PLAYER_COUNT = 8,
		COMBAT_SIM_FACTION = 2,
		BRIEFING_TEXT_SIZE = 4096,
		HOST_BROADCAST_INTERVAL_MS = 5000,
		BUTTON_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TEXT_COLOR_WHITE = 0xFFFF,
		HOVER_BEGIN = 7,
		HOVER_PREVIOUS = 8,
		HOVER_MISSION_LIST = 9,
		HOVER_BOOT_PLAYER = 10,
		BEGIN_BUTTON_LOCKOUT_FRAMES = 240,
		PACKET_SIZE_ONE_WORD = sizeof(int),
		PACKET_SIZE_MISSION_START = 5 * sizeof(int),
		RANDOM_SETUP_SEQUENTIAL = 0,
		RANDOM_SETUP_PLAYER_CHOICE = 2,
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
		MISSION_PLAYER_COUNT_CHARACTER_OFFSET = '0',
		ANIMATION_CYCLE_FRAMES = 32,
	};

	int player_index;

	if (frame_counter == 0) {
		frontend_cursor_set_pos(37, 445);
		mp_roster_compact_active_entries();
		g_remote_battle_continuation_active = 0;
		g_frontend_briefing_entered_count = 0;
		g_mission_setup_begin_button_lockout_frames = 0;
		g_remote_battle_sequence_continuation_choice = 0;
		g_remote_battle_last_completed_mission_index = 0;
		g_remote_battle_rebel_victory_count = 0;
		g_remote_battle_imperial_victory_count = 0;

		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_pilot_data.team =
				g_pilot_data
					.faction_statistics[COMBAT_SIM_FACTION]
					.team;
			g_pilot_data.mission_directory_id =
				g_pilot_data
					.faction_statistics[COMBAT_SIM_FACTION]
					.mission_directory_id;
			g_mission_setup_use_combat_sim_pilot_state = 1;
			memcpy(g_pilot_data.mission_description_ids,
			       g_pilot_data
				       .faction_statistics[COMBAT_SIM_FACTION]
				       .mission_description_ids,
			       sizeof(g_pilot_data.mission_description_ids));
			g_pilot_data.mission_sequence_active =
				g_pilot_data
					.faction_statistics[COMBAT_SIM_FACTION]
					.mission_sequence_active;
			g_pilot_data.faction_statistics[COMBAT_SIM_FACTION]
				.mission_sequence_active = 0;
			g_pilot_data.saved_mission_description_id =
				g_pilot_data
					.faction_statistics[COMBAT_SIM_FACTION]
					.saved_mission_description_id;
		} else {
			g_mission_setup_use_combat_sim_pilot_state = 0;
			g_pilot_data.team =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.team;
			g_pilot_data.mission_directory_id =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_directory_id;
			memcpy(g_pilot_data.mission_description_ids,
			       g_pilot_data
				       .faction_statistics
					       [g_pilot_data.current_faction_id]
				       .mission_description_ids,
			       sizeof(g_pilot_data.mission_description_ids));
			g_pilot_data.mission_sequence_active =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_sequence_active;
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_sequence_active = 0;
			g_pilot_data.saved_mission_description_id =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.saved_mission_description_id;
		}

		g_config_connection_type_editable = 0;
		g_mission_text = (char *)malloc(BRIEFING_TEXT_SIZE);
		g_frontend_first_visible_line = 0;
		g_mission_setup_active_panel = MISSION_SETUP_PANEL_PLAYERS;
		g_frontend_quick_start_launch_flag = 0;
		g_mission_setup_roster_authoritative = 0;
		g_mission_setup_selected_player_roster_index = -1;
		g_game_config.continue_battle_or_campaign = SEQUENCE_CONTINUE;
		g_mission_setup_last_host_broadcast_ms = GetTickCount();
		g_selected_mission_list_index = 0;
		if (g_mission_list != NULL) {
			for (; (unsigned int)g_selected_mission_list_index <
			       g_mission_count;
			     ++g_selected_mission_list_index) {
				if (g_mission_list
					    [g_selected_mission_list_index]
						    .mission_idx ==
				    g_pilot_data.mission_description_ids
					    [g_pilot_data
						     .mission_directory_id]) {
					break;
				}
			}
		}

		if (g_pilot_data.mission_sequence_active == 1) {
			g_pilot_data.mission_description_ids
				[g_pilot_data.mission_directory_id] =
				g_pilot_data.saved_mission_description_id;
		}
		memset(&g_pilot_data.melee_tournament_sequence_state, 0,
		       sizeof(g_pilot_data.melee_tournament_sequence_state));
		memset(&g_pilot_data.battle_sequence_state, 0,
		       sizeof(g_pilot_data.battle_sequence_state));
		memset(&g_pilot_data.campaign_sequence_state, 0,
		       sizeof(g_pilot_data.campaign_sequence_state));
		if (g_pilot_data.mission_sequence_active == 1) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_MELEES) {
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_TOURNAMENTS;
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_BATTLES;
			} else {
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_CAMPAIGNS;
			}
		}
		g_pilot_data.mission_sequence_active = 0;
		frontend_mission_load_current();
		mission_setup_load_mission_desc_text(g_mission_text);
		mission_setup_update_team_counts();

		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    (g_pilot_data.mission_directory_id ==
			     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
		     g_pilot_data.mission_directory_id ==
			     MISSION_DIRECTORY_BATTLES) &&
		    g_game_config.craft_waves == CRAFT_WAVES_UNLIMITED) {
			g_game_config.craft_waves = CRAFT_WAVES_DEFAULT;
		}
		if (g_game_config.craft_selection ==
			    CRAFT_SELECTION_HOST_ONLY &&
		    (g_frontend_mission_session_mode ==
			     FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		     (g_pilot_data.mission_directory_id !=
			      MISSION_DIRECTORY_MELEES &&
		      g_pilot_data.mission_directory_id !=
			      MISSION_DIRECTORY_TOURNAMENTS))) {
			g_game_config.craft_selection = CRAFT_SELECTION_ON;
		}
		if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_CAMPAIGNS &&
		    g_game_config.difficulty == GAME_DIFFICULTY_EASY_CHEAT) {
			g_game_config.difficulty = GAME_DIFFICULTY_EASY;
		}
		if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_BATTLES &&
		    g_game_config.random_setup == RANDOM_SETUP_PLAYER_CHOICE) {
			g_game_config.random_setup = RANDOM_SETUP_SEQUENTIAL;
		}
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_BATTLES) {
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilot_data
					    .sp_battle_continuations
						    [g_mission_list
							     [g_selected_mission_list_index]
								     .mission_idx]
					    .is_active != 0) {
					g_game_config.battle_length_index =
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.battle_length_index;
					g_game_config.random_setup =
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup;
				}
			} else if (
				g_pilot_data
					.mp_battle_continuations
						[g_mission_list
							 [g_selected_mission_list_index]
								 .mission_idx]
					.is_active != 0) {
				g_game_config.battle_length_index =
					g_pilot_data
						.mp_battle_continuations
							[g_mission_list
								 [g_selected_mission_list_index]
									 .mission_idx]
						.battle_length_index;
				g_game_config.random_setup =
					g_pilot_data
						.mp_battle_continuations
							[g_mission_list
								 [g_selected_mission_list_index]
									 .mission_idx]
						.random_setup;
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_CAMPAIGNS) {
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilot_data
					    .sp_campaign_continuations
						    [g_mission_list
							     [g_selected_mission_list_index]
								     .mission_idx]
					    .is_active != 0) {
					g_game_config.random_setup =
						g_pilot_data
							.sp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup;
				}
			} else if (net_is_host() != 0) {
				if (g_pilot_data
					    .mp_campaign_continuations
						    [g_mission_list
							     [g_selected_mission_list_index]
								     .mission_idx]
					    .is_active != 0) {
					g_game_config.random_setup =
						g_pilot_data
							.mp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup;
				}
			} else if (
				g_pilot_data
					.mp_campaign_continuations
						[g_mission_list
							 [g_selected_mission_list_index]
								 .mission_idx +
						 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
					.is_active != 0) {
				g_game_config.random_setup =
					g_pilot_data
						.mp_campaign_continuations
							[g_mission_list[g_selected_mission_list_index]
								 .mission_idx +
							 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
						.random_setup;
			}
		}

		g_frontend_skip_screen_entry_setup = 0;
		for (player_index = 0; player_index < NETWORK_PLAYER_COUNT;
		     ++player_index) {
			memset(&g_pilot_data.network_players[player_index], 0,
			       sizeof(g_pilot_data
					      .network_players[player_index]));
			g_pilot_data.network_players[player_index].craft_id = 0;
			g_pilot_data.network_players[player_index]
				.craft_option = -1;
			g_pilot_data.network_players[player_index]
				.warhead_option = -1;
			g_pilot_data.network_players[player_index].beam_option =
				-1;
			g_pilot_data.network_players[player_index]
				.countermeasure_option = -1;
		}
		memset(g_mp_roster_ready_flags, 0,
		       sizeof(g_mp_roster_ready_flags));
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			strcpy(g_mp_roster[0].name, g_pilot_data.name);
			g_mp_roster[0].player_id = 1;
			g_mp_roster[0].pilot_rating = g_pilot_data.rating;
		}
		mission_setup_draw_background();
		frontend_text_start_text_fade_in(20);
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (net_is_host() != 0) {
			unsigned int now_ms = GetTickCount();
			if (now_ms -
				    (unsigned int)
					    g_mission_setup_last_host_broadcast_ms >
			    HOST_BROADCAST_INTERVAL_MS) {
				mission_setup_broadcast_ready_roster(0);
				mission_setup_broadcast_lobby_selection();
				g_mission_setup_last_host_broadcast_ms = now_ms;
			}
		}

		int packet_type = frontend_net_process_network_packets();
		if (packet_type == NET_PACKET_STATE) {
			if (g_pilot_data.mission_directory_id !=
				    g_frontend_net_received_mission_directory_id ||
			    g_pilot_data.mission_description_ids
					    [g_frontend_net_received_mission_directory_id] !=
				    g_frontend_net_received_mission_description_id) {
				g_game_config.continue_battle_or_campaign =
					SEQUENCE_CONTINUE;
			}
			g_pilot_data.mission_directory_id =
				g_frontend_net_received_mission_directory_id;
			g_pilot_data.mission_description_ids
				[g_frontend_net_received_mission_directory_id] =
				g_frontend_net_received_mission_description_id;
			frontend_mission_load_current();
			front_image_free_resource_by_name("background");
			mission_setup_draw_background();
			mission_setup_load_mission_desc_text(g_mission_text);
			mission_setup_update_team_counts();
			if (g_mission_list != NULL) {
				for (g_selected_mission_list_index = 0;
				     (unsigned int)
					     g_selected_mission_list_index <
				     g_mission_count;
				     ++g_selected_mission_list_index) {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    g_pilot_data.mission_description_ids
						    [g_pilot_data
							     .mission_directory_id]) {
						break;
					}
				}
			}
			g_mission_setup_selected_player_roster_index = -1;
			g_frontend_first_visible_line = 0;
			if (g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_BATTLES &&
			    g_game_config.random_setup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
				g_game_config.random_setup =
					RANDOM_SETUP_SEQUENTIAL;
			}
			if (g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_CAMPAIGNS &&
			    g_game_config.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
				g_game_config.difficulty = GAME_DIFFICULTY_EASY;
			}
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_CAMPAIGNS) {
				g_game_config.random_setup =
					RANDOM_SETUP_SEQUENTIAL;
			}
			if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
			    (g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_BATTLES) &&
			    g_game_config.craft_waves ==
				    CRAFT_WAVES_UNLIMITED) {
				g_game_config.craft_waves = CRAFT_WAVES_DEFAULT;
			}
			if (g_game_config.craft_selection ==
			    CRAFT_SELECTION_HOST_ONLY) {
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_game_config.craft_selection =
						CRAFT_SELECTION_ON;
				} else if (
					g_pilot_data.mission_directory_id !=
						MISSION_DIRECTORY_MELEES &&
					g_pilot_data.mission_directory_id !=
						MISSION_DIRECTORY_TOURNAMENTS) {
					g_game_config.craft_selection =
						CRAFT_SELECTION_ON;
				}
			}
		} else if (packet_type == NET_PACKET_HOST_CANCELLED) {
			net_shutdown_direct_play_session();
			if (net_is_host() == 0) {
				frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					frontend_string_get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					frontend_string_get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return xvt_dialog_continue_with(
					xvt_mission_dialogs_resume,
					XVT_MISSION_SETUP_CANCELLED);
#endif
			}
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			if (g_game_config.network_type != 0) {
				frontend_screen_set_callbacks(concourse_update,
							      concourse_exit);
			} else {
				frontend_screen_set_callbacks(
					frontend_net_join_game_screen,
					(frontend_screen_exit_fn)
						frontend_mission_list_free_screen_resources);
			}
		} else if (packet_type == NET_PACKET_PLAYER_UNAVAILABLE) {
			net_shutdown_direct_play_session();
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_675_THERE_WAS_A_ERROR_WHILE_ATTEMPTING),
				frontend_string_get(
					FRONTSTR_676_TO_CONNECT_PLEASE_TRY_AGAIN_OR),
				frontend_string_get(
					FRONTSTR_677_SELECT_ANOTHER_GAME),
				NULL, NULL);
#ifdef XVT_MODERN
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_SETUP_CANCELLED);
#endif
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			if (g_game_config.network_type != 0) {
				frontend_screen_set_callbacks(concourse_update,
							      concourse_exit);
			} else {
				frontend_screen_set_callbacks(
					frontend_net_join_game_screen,
					(frontend_screen_exit_fn)
						frontend_mission_list_free_screen_resources);
			}
		} else if (packet_type == NET_PACKET_FRONTEND_MISSION_START) {
			if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
			    (g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_BATTLES) &&
			    g_game_config.craft_waves ==
				    CRAFT_WAVES_UNLIMITED) {
				g_game_config.craft_waves = CRAFT_WAVES_DEFAULT;
			}
			if (g_game_config.craft_selection ==
				    CRAFT_SELECTION_HOST_ONLY &&
			    g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_MELEES &&
			    g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_TOURNAMENTS) {
				g_game_config.craft_selection =
					CRAFT_SELECTION_ON;
			}
			if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
			    g_pilot_data.mission_sequence_active == 1) {
				g_pilot_data.battle_sequence_state
					.mission_list_indices[0] =
					g_selected_mission_list_index;
				g_pilot_data.battle_sequence_state
					.mission_ordinals[0] =
					g_selected_mission_list_index;
				if (g_mission_list != NULL) {
					g_pilot_data.battle_sequence_state
						.current_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
				}
			}
			g_mission_setup_roster_authoritative = 1;
			frontend_screen_set_callbacks(
				mission_setup_team_assignment_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_mission_resources
#else
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
			);
			return 0;
		} else if (packet_type == NET_PACKET_PLAYER_KICKED) {
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_LEFT;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch,
				PACKET_SIZE_ONE_WORD);
			net_shutdown_direct_play_session();
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_562_YOU_HAVE_BEEN_BOOTED_BY_THE_HOST),
				frontend_string_get(
					FRONTSTR_563_PLEASE_CHOOSE_ANOTHER),
				frontend_string_get(FRONTSTR_564_GAME_TO_JOIN),
				NULL, NULL);
#ifdef XVT_MODERN
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_SETUP_BOOTED);
#endif
			if (g_game_config.network_type != 0) {
				frontend_screen_set_callbacks(concourse_update,
							      concourse_exit);
			} else {
				frontend_screen_set_callbacks(
					frontend_net_join_game_screen,
					(frontend_screen_exit_fn)
						frontend_mission_list_free_screen_resources);
			}
			return 0;
		} else if (packet_type == NET_PACKET_PILOT_RATING) {
			for (player_index = 0;
			     player_index < NETWORK_PLAYER_COUNT;
			     ++player_index) {
				if (g_mp_roster[player_index].player_id ==
				    g_frontend_net_packet_sender_player_id) {
					g_mp_roster[player_index].pilot_rating =
						g_frontend_net_packet_arg0;
					break;
				}
			}
		}
	}
	struct RECT rect;
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
		frontend_text_draw_centered(BUTTON_FONT_SIZE,
					    g_pilot_data.multiplayer_game_name,
					    &rect, TEXT_COLOR_WHITE);
	}

	sprintf(g_frontend_scratch_buffer, "%s %c%s",
		frontend_string_get(FRONTSTR_193_SELECT_MISSION), 4,
		frontend_string_get(
			(frontend_string_id)(g_pilot_data.mission_directory_id +
					     FRONTSTR_194_TRAINING_EXERCISES)));
	frontend_draw_rect_assign(&rect, 84, 90, 434, 108);
	frontend_text_draw_centered(TITLE_FONT_SIZE, g_frontend_scratch_buffer,
				    &rect, TEXT_COLOR_WHITE);
	if (net_is_host() != 0 ||
	    g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 84, 112, 416, 130);
	} else {
		frontend_draw_rect_assign(&rect, 84, 112, 434, 130);
	}
	if ((unsigned int)g_selected_mission_list_index < g_mission_count) {
		frontend_draw_rect_inset_xy(&rect, 4, 0);
		frontend_text_draw_aligned_in_rect(
			TITLE_FONT_SIZE,
			g_mission_list[g_selected_mission_list_index]
				.description,
			&rect, 0, 1, g_color_yellow);
	}
	mission_setup_draw_mission_description();
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_net_update_and_draw_chat_panel(frame_counter);
	}
	frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
	sprintf(g_frontend_scratch_buffer, "v. %d.%d", 2, 0);
	frontend_text_draw_centered(BUTTON_FONT_SIZE, g_frontend_scratch_buffer,
				    &rect, TEXT_COLOR_WHITE);
	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(BUTTON_FONT_SIZE,
					    g_frontend_scratch_buffer, &rect,
					    g_color_yellow);
		if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_MELEES &&
		    g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_TOURNAMENTS) {
			if (g_pilot_data.current_faction_id == 0) {
				sprintf(g_frontend_scratch_buffer, "rebtiny%d",
					(frame_counter %
					 ANIMATION_CYCLE_FRAMES) >>
						1);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 204, 453);
			} else {
				sprintf(g_frontend_scratch_buffer, "imptiny%d",
					(frame_counter %
					 ANIMATION_CYCLE_FRAMES) >>
						1);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 204, 453);
			}
		} else {
			sprintf(g_frontend_scratch_buffer, "rebtiny%d",
				(frame_counter % ANIMATION_CYCLE_FRAMES) >> 1);
			front_image_draw_sprite(g_frontend_scratch_buffer, 204,
						453);
			sprintf(g_frontend_scratch_buffer, "imptiny%d",
				(frame_counter % ANIMATION_CYCLE_FRAMES) >> 1);
		}
		front_image_draw_sprite(g_frontend_scratch_buffer, 420, 453);
	}

	if (g_mission_setup_active_panel == MISSION_SETUP_PANEL_SETTINGS ||
	    g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		mission_setup_draw_game_settings();
	} else {
		mission_setup_draw_player_roster(frame_counter);
	}

	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_659_QUICK_START));
		if (frontend_button_handle_sprite_button(
			    &rect, "quickflyup", "quickflydown",
			    frontend_string_get(FRONTSTR_659_QUICK_START),
			    BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS,
			    "buttonsound") != 0) {
			if ((g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_TOURNAMENTS ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_BATTLES ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_CAMPAIGNS) &&
			    mission_setup_select_first_sequence_mission() ==
				    0) {
				frontend_button_disable_overlay_text();
				return 0;
			}
			g_frontend_quick_start_launch_flag = 1;
			frontend_screen_set_callbacks(
				mission_setup_team_assignment_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_mission_resources
#else
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
			);
			frontend_button_disable_overlay_text();
			return 0;
		}
	} else if (net_is_host() != 0) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_569_PREVIOUS));
		int button_pressed = frontend_button_handle_sprite_button(
			&rect, "leaveup", "leavedown",
			frontend_string_get(
				FRONTSTR_258_RETURN_TO_PILOT_RECORDS),
			BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS, "buttonsound");
		if (button_pressed != 0) {
			if (g_frontend_game_session_in_progress != 0 &&
			    g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				button_pressed = frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
					frontend_string_get(
						FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
					frontend_string_get(
						FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
					frontend_string_get(FRONTSTR_523_OKAY),
					frontend_string_get(
						FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
				return xvt_dialog_continue_with(
					xvt_mission_dialogs_resume,
					XVT_MISSION_SETUP_HOST_LEAVE);
#endif
			}
			if (button_pressed != 0) {
				g_frontend_skip_screen_entry_setup = 1;
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_HOST_CANCELLED;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					PACKET_SIZE_ONE_WORD);
				net_shutdown_direct_play_session();
				g_frontend_mission_session_mode =
					FRONTEND_MISSION_SESSION_NONE;
				frontend_screen_set_callbacks(concourse_update,
							      concourse_exit);
			}
		}
	} else {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_569_PREVIOUS));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_259_RETURN_TO_JOIN_GAME),
			    BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS,
			    "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				frontend_string_get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				frontend_string_get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_259_RETURN_TO_JOIN_GAME),
			    BUTTON_FONT_SIZE, 0, HOVER_PREVIOUS,
			    "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    frontend_string_get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    frontend_string_get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_LEFT;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch,
				PACKET_SIZE_ONE_WORD);
			net_shutdown_direct_play_session();
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
		}
#endif
	}

	if ((unsigned int)g_mission_setup_begin_button_lockout_frames > 0) {
		--g_mission_setup_begin_button_lockout_frames;
	}
	frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_705_BEGIN));
		if (frontend_button_handle_sprite_button(
			    &rect, "nextup", "nextdown",
			    frontend_string_get(FRONTSTR_705_BEGIN),
			    BUTTON_FONT_SIZE, 0, HOVER_BEGIN,
			    "flysound") != 0) {
			if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_TOURNAMENTS ||
			    g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_BATTLES ||
			    g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_CAMPAIGNS) {
				g_game_config.random_seed = GetTickCount();
				if (mission_setup_select_first_sequence_mission() ==
				    0) {
					frontend_button_disable_overlay_text();
					return 0;
				}
			}
			frontend_screen_set_callbacks(
				mission_setup_team_assignment_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_mission_resources
#else
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
			);
			frontend_button_disable_overlay_text();
			return 0;
		}
	} else if (net_is_host() != 0 &&
		   g_mission_setup_begin_button_lockout_frames == 0) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_705_BEGIN));
		if (frontend_button_handle_sprite_button(
			    &rect, "nextup", "nextdown",
			    frontend_string_get(FRONTSTR_705_BEGIN),
			    BUTTON_FONT_SIZE, 0, HOVER_BEGIN,
			    "flysound") != 0) {
			g_mission_setup_begin_button_lockout_frames =
				BEGIN_BUTTON_LOCKOUT_FRAMES;
			if ((int)(uint8_t)g_mission_list
					    [g_selected_mission_list_index]
						    .file_name[0] -
				    MISSION_PLAYER_COUNT_CHARACTER_OFFSET <
			    net_count_ready_players()) {
				if (g_mission_list
					    [g_selected_mission_list_index]
						    .file_name[0] == '1') {
					frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_637_YOU_HAVE_SELECTED_A_SINGLE_PLAYER_MISSION),
						frontend_string_get(
							FRONTSTR_638_FOR_A_MULTIPLAYER_GAME),
						frontend_string_get(
							FRONTSTR_639_PLEASE_SELECT_A_MULTIPLAYER_MISSION),
						NULL, NULL);
#ifdef XVT_MODERN
					return xvt_dialog_continue_with(
						xvt_mission_dialogs_resume,
						XVT_MISSION_NOTICE);
#endif
				} else {
					sprintf(g_frontend_scratch_buffer,
						frontend_string_get(
							FRONTSTR_532_THIS_MISSION_ONLY_SUPPORTS_PERCENT_D_PLAYERS),
						(int)(uint8_t)g_mission_list
								[g_selected_mission_list_index]
									.file_name
										[0] -
							MISSION_PLAYER_COUNT_CHARACTER_OFFSET);
					frontend_dialog_show_confirm_dialog(
						g_frontend_scratch_buffer,
						frontend_string_get(
							FRONTSTR_533_REMOVE_SOME_PLAYERS_BEFORE_CONTINUING),
						NULL, NULL, NULL);
#ifdef XVT_MODERN
					return xvt_dialog_continue_with(
						xvt_mission_dialogs_resume,
						XVT_MISSION_NOTICE);
#endif
				}
			} else {
				if ((g_pilot_data.mission_directory_id ==
					     MISSION_DIRECTORY_TOURNAMENTS ||
				     g_pilot_data.mission_directory_id ==
					     MISSION_DIRECTORY_BATTLES ||
				     g_pilot_data.mission_directory_id ==
					     MISSION_DIRECTORY_CAMPAIGNS) &&
				    mission_setup_select_first_sequence_mission() ==
					    0) {
					frontend_button_disable_overlay_text();
					return 0;
				}
				mission_setup_send_lobby_state(0);
				*(int *)&g_frontend_net_packet_scratch
					 .payload[4] =
					g_pilot_data.mission_directory_id;
				/* mission_count first carries the selected
				 * mission's description id (payload word 0);
				 * below it carries the sequence length, which
				 * for combat engagements is the number of
				 * victories needed. */
				int mission_count =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				*(int *)&g_frontend_net_packet_scratch
					 .payload[0] = mission_count;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[8] =
					g_pilot_data.mission_sequence_active;
				g_mission_setup_roster_authoritative = 1;
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_FRONTEND_MISSION_START;
				if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_MELEES) {
					mission_count =
						g_pilot_data
							.melee_tournament_sequence_state
							.mission_count;
				} else if (
					g_pilot_data.mission_directory_id ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					mission_count =
						g_pilot_data
							.battle_sequence_state
							.victories_needed;
				} else {
					mission_count =
						g_pilot_data
							.campaign_sequence_state
							.mission_count;
				}
				*(int *)&g_frontend_net_packet_scratch
					 .payload[12] = mission_count;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					PACKET_SIZE_MISSION_START);
			}
		}
	}

	frontend_button_disable_overlay_text();
	if (net_is_host() != 0 ||
	    g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 417, 112, 434, 130);
		if (frontend_button_handle_sprite_button(
			    &rect, "dropbtnup", "dropbtndown",
			    frontend_string_get(FRONTSTR_684_MISSION_LIST),
			    TITLE_FONT_SIZE, TEXT_COLOR_WHITE,
			    HOVER_MISSION_LIST, "buttonsound") != 0) {
			frontend_draw_rect_assign(&rect, 0, 0, 639, 479);
			frontend_screen_queue_push(
				mission_setup_draw_mission_list, &rect);
		}
	}

	int mission_type_controls_changed =
		mission_setup_draw_mission_type_controls();
	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    net_is_host() != 0 && mission_type_controls_changed != 0) {
		mission_setup_send_lobby_state(0);
	}
	if (frontend_handle_common_screen_controls(1) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (xvt_dialog_is_active()) {
		return 0;
	}
#endif
	if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_NET_HOST &&
	    net_is_host() != 0 &&
	    g_mission_setup_active_panel == MISSION_SETUP_PANEL_PLAYERS) {
		frontend_draw_rect_assign(&rect, 417, 309, 432, 336);
		if (frontend_button_handle_sprite_button(
			    &rect, "bootu", "bootd",
			    frontend_string_get(
				    FRONTSTR_531_REMOVE_PLAYER_FROM_GAME),
			    BUTTON_FONT_SIZE, 0, HOVER_BOOT_PLAYER,
			    "buttonsound") != 0 &&
		    g_mission_setup_selected_player_roster_index != -1) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_KICKED;
			net_send_packet_and_flush(
				g_mp_roster
					[g_mission_setup_selected_player_roster_index]
						.player_id,
				&g_frontend_net_packet_scratch,
				PACKET_SIZE_ONE_WORD);
			net_clear_player_ready_flag_with_lock_guard(
				g_mp_roster
					[g_mission_setup_selected_player_roster_index]
						.player_id);
			mission_setup_send_lobby_state(0);
		}
	}
	return 0;
}

/* Draws the mission type buttons on the mission setup screen and handles clicks
 * on them; called each frame by mission_setup_update. The eight navigation
 * lights show the selected mission type and, in a network game, the Players or
 * Settings panel, or in a solo game the pilot's faction. In a solo game the
 * Rebel and Imperial pilot buttons save the current faction's mission state
 * into g_pilot_data.faction_statistics and load the other faction's, keeping the
 * mission type and, when the new list holds it, the selected mission. In a
 * network game the Players and Settings buttons set g_mission_setup_active_panel.
 * For the host or a solo player, a click on an unselected mission type button
 * sets g_pilot_data.mission_directory_id, loads that type's mission and its text,
 * and selects the first available mission when the stored one is marked
 * unavailable (for campaigns, also when it is missing); a right click on the
 * selected button steps g_selected_mission_list_index back to the previous
 * available mission and a left click forward to the next, wrapping, and a
 * network host then calls mission_setup_send_lobby_state. A new type also sets
 * g_game_config.continue_battle_or_campaign to SEQUENCE_CONTINUE and clears the
 * settings it does not allow: easy cheat difficulty (all types but campaign),
 * player choice setup (all but battle; campaign sets sequential setup outright)
 * and, outside a solo game, unlimited waves (battle and combat engagement).
 * Outside a solo game it also sets g_game_config.mission_time_limit to 255
 * (written as -1), which the settings show as Default, when it moves to
 * training, and when it moves between groups (melee and tournament; combat
 * engagement and battle; campaign), except from training to campaign. A battle
 * then takes its saved length and setup choice, a campaign its setup choice.
 * Returns 1 when the faction, the panel or the mission type changed, or when a
 * step through the training missions changed the IFF of the mission's first
 * flight group with a player; else 0. Does not check that any mission is
 * available: a step loops forever when every entry is unavailable. */
// FUNCTION: XVT 0x4E2970
int mission_setup_draw_mission_type_controls(void)
{
	enum {
		NAVIGATION_SLOT_COUNT = 8,
		PLAYERS_OR_REBEL_NAVIGATION_SLOT = 5,
		SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT = 6,
		CAMPAIGN_NAVIGATION_SLOT = 7,
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
		PILOT_FACTION_REBEL = 0,
		PILOT_FACTION_IMPERIAL = 1,
		RANDOM_SETUP_SEQUENTIAL = 0,
		RANDOM_SETUP_PLAYER_CHOICE = 2,
		HOVER_TRAINING = 11,
		HOVER_MELEE = 12,
		HOVER_TOURNAMENT = 13,
		HOVER_COMBAT_ENGAGEMENT = 14,
		HOVER_BATTLE = 15,
		HOVER_PLAYERS_OR_REBEL = 16,
		HOVER_SETTINGS_OR_IMPERIAL = 17,
		HOVER_CAMPAIGN = 18,
		BUTTON_LEFT = 22,
		BUTTON_RIGHT = 42,
		BUTTON_TOP = 334,
		BUTTON_BOTTOM = 358,
		CAMPAIGN_BUTTON_TOP = 254,
		CAMPAIGN_BUTTON_BOTTOM = 278,
		BATTLE_BUTTON_TOP = 226,
		BATTLE_BUTTON_BOTTOM = 250,
		BUTTON_SPACING = 28,
		BUTTON_FONT_SIZE = 12,
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
	};

	int cursor_y;
	int cursor_x;

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	frontend_navigation_slot_state
		navigation_slot_states[NAVIGATION_SLOT_COUNT];
	navigation_slot_states[MISSION_DIRECTORY_TRAINING_EXERCISES] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigation_slot_states[MISSION_DIRECTORY_MELEES] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigation_slot_states[MISSION_DIRECTORY_TOURNAMENTS] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigation_slot_states[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	navigation_slot_states[MISSION_DIRECTORY_BATTLES] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	if (g_frontend_mission_session_mode == FRONTEND_MISSION_SESSION_NONE) {
		navigation_slot_states[PLAYERS_OR_REBEL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
		navigation_slot_states[SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
	} else {
		navigation_slot_states[PLAYERS_OR_REBEL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		navigation_slot_states[SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		int navigation_index;
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			navigation_index = g_mission_setup_active_panel;
		} else {
			navigation_index = g_pilot_data.current_faction_id;
		}
		navigation_slot_states[PLAYERS_OR_REBEL_NAVIGATION_SLOT +
				       navigation_index] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	navigation_slot_states[CAMPAIGN_NAVIGATION_SLOT] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_CAMPAIGNS) {
		navigation_slot_states[CAMPAIGN_NAVIGATION_SLOT] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else {
		navigation_slot_states[g_pilot_data.mission_directory_id] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	frontend_button_draw_eight_slot_navigation_state(
		navigation_slot_states);

	int is_host = net_is_host();
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		is_host = 1;
	}
	struct RECT rect;
	frontend_draw_rect_assign(&rect, BUTTON_LEFT, BUTTON_TOP, BUTTON_RIGHT,
				  BUTTON_BOTTOM);
	int changed = 0;
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		int previous_mission_directory_id;
		int previous_mission_description_id;
		if (g_pilot_data.current_faction_id != PILOT_FACTION_IMPERIAL) {
			if (frontend_button_handle_sprite_button(
				    &rect, "reg7u", "reg7u",
				    frontend_string_get(
					    FRONTSTR_643_IMPERIAL_PILOT),
				    BUTTON_FONT_SIZE, 0,
				    HOVER_SETTINGS_OR_IMPERIAL,
				    "jewelsound") != 0) {
				changed = 1;
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.team = g_pilot_data.team;
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_directory_id =
					g_pilot_data.mission_directory_id;
				memcpy(g_pilot_data
					       .faction_statistics
						       [g_pilot_data
								.current_faction_id]
					       .mission_description_ids,
				       g_pilot_data.mission_description_ids,
				       sizeof(g_pilot_data
						      .faction_statistics
							      [g_pilot_data
								       .current_faction_id]
						      .mission_description_ids));
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_sequence_active =
					g_pilot_data.mission_sequence_active;
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.saved_mission_description_id =
					g_pilot_data
						.saved_mission_description_id;
				previous_mission_directory_id =
					g_pilot_data.mission_directory_id;
				previous_mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				g_pilot_data.current_faction_id =
					PILOT_FACTION_IMPERIAL;
				g_pilot_data.team =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.team;
				g_pilot_data.mission_directory_id =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_directory_id;
				memcpy(g_pilot_data.mission_description_ids,
				       g_pilot_data
					       .faction_statistics
						       [g_pilot_data
								.current_faction_id]
					       .mission_description_ids,
				       sizeof(g_pilot_data
						      .mission_description_ids));
				g_pilot_data.mission_sequence_active =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_sequence_active;
				g_pilot_data.saved_mission_description_id =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.saved_mission_description_id;
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "reg7d",
				frontend_string_get(
					FRONTSTR_643_IMPERIAL_PILOT),
				BUTTON_FONT_SIZE, 0);
		}
		frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
		if (g_pilot_data.current_faction_id != PILOT_FACTION_REBEL) {
			if (frontend_button_handle_sprite_button(
				    &rect, "reg6u", "reg6u",
				    frontend_string_get(
					    FRONTSTR_642_REBEL_PILOT),
				    BUTTON_FONT_SIZE, 0, HOVER_PLAYERS_OR_REBEL,
				    "jewelsound") != 0) {
				changed = 1;
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.team = g_pilot_data.team;
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_directory_id =
					g_pilot_data.mission_directory_id;
				memcpy(g_pilot_data
					       .faction_statistics
						       [g_pilot_data
								.current_faction_id]
					       .mission_description_ids,
				       g_pilot_data.mission_description_ids,
				       sizeof(g_pilot_data
						      .faction_statistics
							      [g_pilot_data
								       .current_faction_id]
						      .mission_description_ids));
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_sequence_active =
					g_pilot_data.mission_sequence_active;
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.saved_mission_description_id =
					g_pilot_data
						.saved_mission_description_id;
				previous_mission_directory_id =
					g_pilot_data.mission_directory_id;
				previous_mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				g_pilot_data.current_faction_id =
					PILOT_FACTION_REBEL;
				g_pilot_data.team =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.team;
				g_pilot_data.mission_directory_id =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_directory_id;
				memcpy(g_pilot_data.mission_description_ids,
				       g_pilot_data
					       .faction_statistics
						       [g_pilot_data
								.current_faction_id]
					       .mission_description_ids,
				       sizeof(g_pilot_data
						      .mission_description_ids));
				g_pilot_data.mission_sequence_active =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_sequence_active;
				g_pilot_data.saved_mission_description_id =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.saved_mission_description_id;
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "reg6d",
				frontend_string_get(FRONTSTR_642_REBEL_PILOT),
				BUTTON_FONT_SIZE, 0);
		}
		if (changed != 0) {
			if (g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_CAMPAIGNS &&
			    g_game_config.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
				g_game_config.difficulty = GAME_DIFFICULTY_EASY;
			}
			if (g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_BATTLES &&
			    g_game_config.random_setup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
				g_game_config.random_setup =
					RANDOM_SETUP_SEQUENTIAL;
			}
			g_pilot_data.mission_directory_id =
				previous_mission_directory_id;
			mission_setup_load_mission_list(
				previous_mission_directory_id);
			if (g_mission_list != NULL) {
				for (g_selected_mission_list_index = 0;
				     (unsigned int)
					     g_selected_mission_list_index <
				     g_mission_count;
				     ++g_selected_mission_list_index) {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    previous_mission_description_id) {
						g_pilot_data.mission_description_ids
							[g_pilot_data
								 .mission_directory_id] =
							previous_mission_description_id;
						break;
					}
				}
				if ((unsigned int)
					    g_selected_mission_list_index >=
				    g_mission_count) {
					for (g_selected_mission_list_index = 0;
					     (unsigned int)
						     g_selected_mission_list_index <
					     g_mission_count;
					     ++g_selected_mission_list_index) {
						if (g_mission_list
							    [g_selected_mission_list_index]
								    .mission_idx ==
						    g_pilot_data.mission_description_ids
							    [g_pilot_data
								     .mission_directory_id]) {
							break;
						}
					}
					if ((unsigned int)
						    g_selected_mission_list_index >=
					    g_mission_count) {
						g_pilot_data.mission_description_ids
							[g_pilot_data
								 .mission_directory_id] =
							g_mission_list[0]
								.mission_idx;
					}
				}
			}
			frontend_mission_load_current();
			mission_setup_update_team_counts();
			mission_setup_load_mission_desc_text(g_mission_text);
			g_frontend_first_visible_line = 0;
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_BATTLES) {
				if (g_pilot_data
					    .sp_battle_continuations
						    [g_mission_list
							     [g_selected_mission_list_index]
								     .mission_idx]
					    .is_active != 0) {
					g_game_config.battle_length_index =
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.battle_length_index;
					g_game_config.random_setup =
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup;
				}
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_CAMPAIGNS) {
				if (g_pilot_data
					    .sp_campaign_continuations
						    [g_mission_list
							     [g_selected_mission_list_index]
								     .mission_idx]
					    .is_active != 0) {
					g_game_config.random_setup =
						g_pilot_data
							.sp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup;
				}
			}
		}
	} else {
		if (navigation_slot_states
			    [SETTINGS_OR_IMPERIAL_NAVIGATION_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE) {
			if (g_mission_setup_active_panel !=
			    MISSION_SETUP_PANEL_SETTINGS) {
				if (frontend_button_handle_sprite_button(
					    &rect, "game7u", "game7u",
					    frontend_string_get(
						    FRONTSTR_203_SETTINGS),
					    BUTTON_FONT_SIZE, 0,
					    HOVER_SETTINGS_OR_IMPERIAL,
					    "jewelsound") != 0) {
					changed = 1;
					g_mission_setup_active_panel =
						MISSION_SETUP_PANEL_SETTINGS;
				}
			} else {
				frontend_button_draw_sprite_and_tooltip(
					&rect, "game7d",
					frontend_string_get(
						FRONTSTR_203_SETTINGS),
					BUTTON_FONT_SIZE, 0);
			}
		}
		frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
		if (navigation_slot_states[PLAYERS_OR_REBEL_NAVIGATION_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE) {
			if (g_mission_setup_active_panel !=
			    MISSION_SETUP_PANEL_PLAYERS) {
				if (frontend_button_handle_sprite_button(
					    &rect, "game6u", "game6u",
					    frontend_string_get(
						    FRONTSTR_202_PLAYERS),
					    BUTTON_FONT_SIZE, 0,
					    HOVER_PLAYERS_OR_REBEL,
					    "jewelsound") != 0) {
					g_mission_setup_active_panel =
						MISSION_SETUP_PANEL_PLAYERS;
					changed = 1;
				}
			} else {
				frontend_button_draw_sprite_and_tooltip(
					&rect, "game6d",
					frontend_string_get(
						FRONTSTR_202_PLAYERS),
					BUTTON_FONT_SIZE, 0);
			}
		}
	}

	frontend_draw_rect_assign(&rect, BUTTON_LEFT, CAMPAIGN_BUTTON_TOP,
				  BUTTON_RIGHT, CAMPAIGN_BUTTON_BOTTOM);
	int index;
	int selected_mission_id;
	if (g_pilot_data.mission_directory_id != MISSION_DIRECTORY_CAMPAIGNS) {
		if (is_host != 0) {
			if (frontend_button_handle_sprite_button(
				    &rect, "game8u", "game8u",
				    frontend_string_get(FRONTSTR_199_CAMPAIGNS),
				    BUTTON_FONT_SIZE, 0, HOVER_CAMPAIGN,
				    "jewelsound") != 0) {
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_TRAINING_EXERCISES &&
				    g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_CAMPAIGNS) {
					g_game_config.mission_time_limit = -1;
				}
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_CAMPAIGNS;
				g_game_config.random_setup =
					RANDOM_SETUP_SEQUENTIAL;
				changed = 1;
				g_game_config.continue_battle_or_campaign =
					SEQUENCE_CONTINUE;
				frontend_mission_load_current();
				mission_setup_update_team_counts();
				mission_setup_load_mission_desc_text(
					g_mission_text);
				g_frontend_first_visible_line = 0;
				if (g_mission_list != NULL) {
					g_selected_mission_list_index = 0;
					while ((unsigned int)g_selected_mission_list_index <
						       g_mission_count &&
					       g_mission_list[g_selected_mission_list_index]
							       .mission_idx !=
						       g_pilot_data.mission_description_ids
							       [g_pilot_data
									.mission_directory_id]) {
						++g_selected_mission_list_index;
					}
					if (g_selected_mission_list_index ==
						    (int)g_mission_count ||
					    g_mission_list[g_selected_mission_list_index]
							    .is_unavailable !=
						    0) {
						for (index = 0;
						     index <
						     (int)g_mission_count;
						     ++index) {
							if (g_mission_list[index]
								    .is_unavailable ==
							    0) {
								g_pilot_data.mission_description_ids
									[g_pilot_data
										 .mission_directory_id] =
									g_mission_list[index]
										.mission_idx;
								g_selected_mission_list_index =
									index;
								break;
							}
						}
						frontend_mission_load_current();
						mission_setup_update_team_counts();
						mission_setup_load_mission_desc_text(
							g_mission_text);
						g_frontend_first_visible_line =
							0;
					}
				}
				if (g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_BATTLES &&
				    g_game_config.random_setup ==
					    RANDOM_SETUP_PLAYER_CHOICE) {
					g_game_config.random_setup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					int mission_index =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;

					if (g_pilot_data
						    .sp_campaign_continuations
							    [mission_index]
						    .is_active != 0) {
						g_game_config.random_setup =
							g_pilot_data
								.sp_campaign_continuations
									[mission_index]
								.random_setup;
					}
				} else if (net_is_host() != 0) {
					int mission_index =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;

					if (g_pilot_data
						    .mp_campaign_continuations
							    [mission_index]
						    .is_active != 0) {
						g_game_config.random_setup =
							g_pilot_data
								.mp_campaign_continuations
									[mission_index]
								.random_setup;
					}
				} else {
					int mission_index =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;

					if (g_pilot_data
						    .mp_campaign_continuations
							    [mission_index +
							     CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
						    .is_active != 0) {
						g_game_config.random_setup =
							g_pilot_data
								.mp_campaign_continuations
									[mission_index +
									 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
								.random_setup;
					}
				}
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "game8u",
				frontend_string_get(FRONTSTR_199_CAMPAIGNS),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "game8d",
			frontend_string_get(FRONTSTR_199_CAMPAIGNS),
			BUTTON_FONT_SIZE, 0);
		if (is_host != 0 && frontend_draw_point_in_rect(
					    &rect, cursor_x, cursor_y) != 0) {
			if (frontend_mouse_get_right_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selected_mission_list_index ==
						    0) {
							g_selected_mission_list_index =
								g_mission_count -
								1;
						} else {
							--g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					if (g_mission_list != NULL) {
						g_frontend_first_visible_line =
							0;
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						int mission_index =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;

						if (g_pilot_data
							    .sp_campaign_continuations
								    [mission_index]
							    .is_active != 0) {
							g_game_config
								.random_setup =
								g_pilot_data
									.sp_campaign_continuations
										[mission_index]
									.random_setup;
						}
					} else if (net_is_host() != 0) {
						int mission_index =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;

						if (g_pilot_data
							    .mp_campaign_continuations
								    [mission_index]
							    .is_active != 0) {
							g_game_config
								.random_setup =
								g_pilot_data
									.mp_campaign_continuations
										[mission_index]
									.random_setup;
						}
					} else {
						int mission_index =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;

						if (g_pilot_data
							    .mp_campaign_continuations
								    [mission_index +
								     CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
							    .is_active != 0) {
							g_game_config
								.random_setup =
								g_pilot_data
									.mp_campaign_continuations
										[mission_index +
										 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
									.random_setup;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			} else if (frontend_mouse_get_left_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_mission_count -
							    g_selected_mission_list_index ==
						    1) {
							g_selected_mission_list_index =
								0;
						} else {
							++g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					if (g_mission_list != NULL) {
						g_frontend_first_visible_line =
							0;
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						int mission_index =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;

						if (g_pilot_data
							    .sp_campaign_continuations
								    [mission_index]
							    .is_active != 0) {
							g_game_config
								.random_setup =
								g_pilot_data
									.sp_campaign_continuations
										[mission_index]
									.random_setup;
						}
					} else if (net_is_host() != 0) {
						int mission_index =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;

						if (g_pilot_data
							    .mp_campaign_continuations
								    [mission_index]
							    .is_active != 0) {
							g_game_config
								.random_setup =
								g_pilot_data
									.mp_campaign_continuations
										[mission_index]
									.random_setup;
						}
					} else {
						int mission_index =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;

						if (g_pilot_data
							    .mp_campaign_continuations
								    [mission_index +
								     CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
							    .is_active != 0) {
							g_game_config
								.random_setup =
								g_pilot_data
									.mp_campaign_continuations
										[mission_index +
										 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
									.random_setup;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			}
		}
	}

	frontend_draw_rect_assign(&rect, BUTTON_LEFT, BATTLE_BUTTON_TOP,
				  BUTTON_RIGHT, BATTLE_BUTTON_BOTTOM);
	if (g_pilot_data.mission_directory_id != MISSION_DIRECTORY_BATTLES) {
		if (is_host != 0) {
			if (frontend_button_handle_sprite_button(
				    &rect, "game5u", "game5u",
				    frontend_string_get(FRONTSTR_198_BATTLES),
				    BUTTON_FONT_SIZE, 0, HOVER_BATTLE,
				    "jewelsound") != 0) {
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
					    g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_BATTLES) {
						g_game_config
							.mission_time_limit =
							-1;
					}
					if (g_game_config.craft_waves ==
					    CRAFT_WAVES_UNLIMITED) {
						g_game_config.craft_waves =
							CRAFT_WAVES_DEFAULT;
					}
				}
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_BATTLES;
				changed = 1;
				g_game_config.continue_battle_or_campaign =
					SEQUENCE_CONTINUE;
				if (g_game_config.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_game_config.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				frontend_mission_load_current();
				mission_setup_update_team_counts();
				mission_setup_load_mission_desc_text(
					g_mission_text);
				g_frontend_first_visible_line = 0;
				if (g_mission_list != NULL) {
					for (g_selected_mission_list_index = 0;
					     (unsigned int)
						     g_selected_mission_list_index <
					     g_mission_count;
					     ++g_selected_mission_list_index) {
						if (g_mission_list
							    [g_selected_mission_list_index]
								    .mission_idx ==
						    g_pilot_data.mission_description_ids
							    [g_pilot_data
								     .mission_directory_id]) {
							if (g_mission_list[g_selected_mission_list_index]
								    .is_unavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_mission_count;
								     ++index) {
									if (g_mission_list[index]
										    .is_unavailable ==
									    0) {
										g_pilot_data
											.mission_description_ids
												[g_pilot_data
													 .mission_directory_id] =
											g_mission_list[index]
												.mission_idx;
										g_selected_mission_list_index =
											index;
										break;
									}
								}
								frontend_mission_load_current();
								mission_setup_update_team_counts();
								mission_setup_load_mission_desc_text(
									g_mission_text);
							}
							break;
						}
					}
				}
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilot_data
						    .sp_battle_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
						    .is_active != 0) {
						g_game_config
							.battle_length_index =
							g_pilot_data
								.sp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.battle_length_index;
						g_game_config.random_setup =
							g_pilot_data
								.sp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.random_setup;
					}
				} else {
					if (g_pilot_data
						    .mp_battle_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
						    .is_active != 0) {
						g_game_config
							.battle_length_index =
							g_pilot_data
								.mp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.battle_length_index;
						g_game_config.random_setup =
							g_pilot_data
								.mp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.random_setup;
					}
				}
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "game5u",
				frontend_string_get(FRONTSTR_198_BATTLES),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "game5d",
			frontend_string_get(FRONTSTR_198_BATTLES),
			BUTTON_FONT_SIZE, 0);
		if (is_host != 0 && frontend_draw_point_in_rect(
					    &rect, cursor_x, cursor_y) != 0) {
			if (frontend_mouse_get_right_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selected_mission_list_index ==
						    0) {
							g_selected_mission_list_index =
								g_mission_count -
								1;
						} else {
							--g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_pilot_data
							    .sp_battle_continuations
								    [g_mission_list[g_selected_mission_list_index]
									     .mission_idx]
							    .is_active != 0) {
							g_game_config
								.battle_length_index =
								g_pilot_data
									.sp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.battle_length_index;
							g_game_config
								.random_setup =
								g_pilot_data
									.sp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.random_setup;
						}
					} else {
						if (g_pilot_data
							    .mp_battle_continuations
								    [g_mission_list[g_selected_mission_list_index]
									     .mission_idx]
							    .is_active != 0) {
							g_game_config
								.battle_length_index =
								g_pilot_data
									.mp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.battle_length_index;
							g_game_config
								.random_setup =
								g_pilot_data
									.mp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.random_setup;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			} else if (frontend_mouse_get_left_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_mission_count -
							    g_selected_mission_list_index ==
						    1) {
							g_selected_mission_list_index =
								0;
						} else {
							++g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_pilot_data
							    .sp_battle_continuations
								    [g_mission_list[g_selected_mission_list_index]
									     .mission_idx]
							    .is_active != 0) {
							g_game_config
								.battle_length_index =
								g_pilot_data
									.sp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.battle_length_index;
							g_game_config
								.random_setup =
								g_pilot_data
									.sp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.random_setup;
						}
					} else {
						if (g_pilot_data
							    .mp_battle_continuations
								    [g_mission_list[g_selected_mission_list_index]
									     .mission_idx]
							    .is_active != 0) {
							g_game_config
								.battle_length_index =
								g_pilot_data
									.mp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.battle_length_index;
							g_game_config
								.random_setup =
								g_pilot_data
									.mp_battle_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx]
									.random_setup;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			}
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (g_pilot_data.mission_directory_id !=
	    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		if (is_host != 0) {
			if (frontend_button_handle_sprite_button(
				    &rect, "game4u", "game4u",
				    frontend_string_get(
					    FRONTSTR_191_COMBAT_ENGAGEMENTS),
				    BUTTON_FONT_SIZE, 0,
				    HOVER_COMBAT_ENGAGEMENT,
				    "jewelsound") != 0) {
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
					    g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_BATTLES) {
						g_game_config
							.mission_time_limit =
							-1;
					}
					if (g_game_config.craft_waves ==
					    CRAFT_WAVES_UNLIMITED) {
						g_game_config.craft_waves =
							CRAFT_WAVES_DEFAULT;
					}
				}
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
				changed = 1;
				g_game_config.continue_battle_or_campaign =
					SEQUENCE_CONTINUE;
				if (g_game_config.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_game_config.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				if (g_game_config.random_setup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
					g_game_config.random_setup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				frontend_mission_load_current();
				mission_setup_update_team_counts();
				mission_setup_load_mission_desc_text(
					g_mission_text);
				g_frontend_first_visible_line = 0;
				g_selected_mission_list_index = 0;
				if (g_mission_list != NULL) {
					for (;
					     (unsigned int)
						     g_selected_mission_list_index <
					     g_mission_count;
					     ++g_selected_mission_list_index) {
						if (g_mission_list
							    [g_selected_mission_list_index]
								    .mission_idx ==
						    g_pilot_data.mission_description_ids
							    [g_pilot_data
								     .mission_directory_id]) {
							if (g_mission_list[g_selected_mission_list_index]
								    .is_unavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_mission_count;
								     ++index) {
									if (g_mission_list[index]
										    .is_unavailable ==
									    0) {
										g_pilot_data
											.mission_description_ids
												[g_pilot_data
													 .mission_directory_id] =
											g_mission_list[index]
												.mission_idx;
										g_selected_mission_list_index =
											index;
										break;
									}
								}
								frontend_mission_load_current();
								mission_setup_update_team_counts();
								mission_setup_load_mission_desc_text(
									g_mission_text);
							}
							break;
						}
					}
				}
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "game4u",
				frontend_string_get(
					FRONTSTR_191_COMBAT_ENGAGEMENTS),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "game4d",
			frontend_string_get(FRONTSTR_191_COMBAT_ENGAGEMENTS),
			BUTTON_FONT_SIZE, 0);
		if (is_host != 0 && frontend_draw_point_in_rect(
					    &rect, cursor_x, cursor_y) != 0) {
			if (frontend_mouse_get_right_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selected_mission_list_index ==
						    0) {
							g_selected_mission_list_index =
								g_mission_count -
								1;
						} else {
							--g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			} else if (frontend_mouse_get_left_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_mission_count -
							    g_selected_mission_list_index ==
						    1) {
							g_selected_mission_list_index =
								0;
						} else {
							++g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			}
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (navigation_slot_states[MISSION_DIRECTORY_TOURNAMENTS] !=
	    FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_pilot_data.mission_directory_id !=
		    MISSION_DIRECTORY_TOURNAMENTS) {
			if (is_host != 0) {
				if (frontend_button_handle_sprite_button(
					    &rect, "game3u", "game3u",
					    frontend_string_get(
						    FRONTSTR_196_TOURNAMENTS),
					    BUTTON_FONT_SIZE, 0,
					    HOVER_TOURNAMENT,
					    "jewelsound") != 0) {
					if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
					    g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_MELEES &&
					    g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_TOURNAMENTS) {
						g_game_config
							.mission_time_limit =
							-1;
					}
					g_pilot_data.mission_directory_id =
						MISSION_DIRECTORY_TOURNAMENTS;
					changed = 1;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					if (g_game_config.difficulty ==
					    GAME_DIFFICULTY_EASY_CHEAT) {
						g_game_config.difficulty =
							GAME_DIFFICULTY_EASY;
					}
					if (g_game_config.random_setup ==
					    RANDOM_SETUP_PLAYER_CHOICE) {
						g_game_config.random_setup =
							RANDOM_SETUP_SEQUENTIAL;
					}
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						for (g_selected_mission_list_index =
							     0;
						     (unsigned int)
							     g_selected_mission_list_index <
						     g_mission_count;
						     ++g_selected_mission_list_index) {
							if (g_mission_list[g_selected_mission_list_index]
								    .mission_idx ==
							    g_pilot_data.mission_description_ids
								    [g_pilot_data
									     .mission_directory_id]) {
								if (g_mission_list[g_selected_mission_list_index]
									    .is_unavailable !=
								    0) {
									for (index = 0;
									     index <
									     (int)g_mission_count;
									     ++index) {
										if (g_mission_list[index]
											    .is_unavailable ==
										    0) {
											g_pilot_data
												.mission_description_ids
													[g_pilot_data
														 .mission_directory_id] =
												g_mission_list[index]
													.mission_idx;
											g_selected_mission_list_index =
												index;
											break;
										}
									}
									frontend_mission_load_current();
									mission_setup_update_team_counts();
									mission_setup_load_mission_desc_text(
										g_mission_text);
								}
								break;
							}
						}
					}
				}
			} else {
				frontend_button_draw_sprite_and_tooltip(
					&rect, "game3u",
					frontend_string_get(
						FRONTSTR_196_TOURNAMENTS),
					BUTTON_FONT_SIZE, 0);
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "game3d",
				frontend_string_get(FRONTSTR_196_TOURNAMENTS),
				BUTTON_FONT_SIZE, 0);
			if (is_host != 0 &&
			    frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y) != 0) {
				if (frontend_mouse_get_right_click() != 0) {
					if (g_mission_list != NULL) {
						if (g_game_config
							    .sfx_datapad_enabled !=
						    0) {
							frontend_sound_play_ui_sound(
								"jewelsound", 1,
								0, 255,
								SOUND_VOLUME_SCALE *
									g_game_config
										.sfx_datapad_volume,
								SOUND_CENTER_PAN);
						}
						do {
							if (g_selected_mission_list_index ==
							    0) {
								g_selected_mission_list_index =
									g_mission_count -
									1;
							} else {
								--g_selected_mission_list_index;
							}
						} while (
							g_mission_list[g_selected_mission_list_index]
								.is_unavailable !=
							0);
						selected_mission_id =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;
						g_game_config
							.continue_battle_or_campaign =
							SEQUENCE_CONTINUE;
						g_pilot_data.mission_description_ids
							[g_pilot_data
								 .mission_directory_id] =
							selected_mission_id;
						frontend_mission_load_current();
						mission_setup_update_team_counts();
						mission_setup_load_mission_desc_text(
							g_mission_text);
						g_frontend_first_visible_line =
							0;
						if (g_mission_list != NULL) {
							g_selected_mission_list_index =
								0;
							while ((unsigned int)g_selected_mission_list_index <
								       g_mission_count &&
							       g_mission_list[g_selected_mission_list_index]
									       .mission_idx !=
								       g_pilot_data
									       .mission_description_ids
										       [g_pilot_data
												.mission_directory_id]) {
								++g_selected_mission_list_index;
							}
						}
					}
					if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
					    net_is_host() != 0) {
						mission_setup_send_lobby_state(
							0);
					}
				} else if (frontend_mouse_get_left_click() !=
					   0) {
					if (g_mission_list != NULL) {
						if (g_game_config
							    .sfx_datapad_enabled !=
						    0) {
							frontend_sound_play_ui_sound(
								"jewelsound", 1,
								0, 255,
								SOUND_VOLUME_SCALE *
									g_game_config
										.sfx_datapad_volume,
								SOUND_CENTER_PAN);
						}
						do {
							if (g_mission_count -
								    g_selected_mission_list_index ==
							    1) {
								g_selected_mission_list_index =
									0;
							} else {
								++g_selected_mission_list_index;
							}
						} while (
							g_mission_list[g_selected_mission_list_index]
								.is_unavailable !=
							0);
						selected_mission_id =
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx;
						g_game_config
							.continue_battle_or_campaign =
							SEQUENCE_CONTINUE;
						g_pilot_data.mission_description_ids
							[g_pilot_data
								 .mission_directory_id] =
							selected_mission_id;
						frontend_mission_load_current();
						mission_setup_update_team_counts();
						mission_setup_load_mission_desc_text(
							g_mission_text);
						g_frontend_first_visible_line =
							0;
						if (g_mission_list != NULL) {
							g_selected_mission_list_index =
								0;
							while ((unsigned int)g_selected_mission_list_index <
								       g_mission_count &&
							       g_mission_list[g_selected_mission_list_index]
									       .mission_idx !=
								       g_pilot_data
									       .mission_description_ids
										       [g_pilot_data
												.mission_directory_id]) {
								++g_selected_mission_list_index;
							}
						}
					}
					if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
					    net_is_host() != 0) {
						mission_setup_send_lobby_state(
							0);
					}
				}
			}
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (g_pilot_data.mission_directory_id != MISSION_DIRECTORY_MELEES) {
		if (is_host != 0) {
			if (frontend_button_handle_sprite_button(
				    &rect, "game2u", "game2u",
				    frontend_string_get(FRONTSTR_190_MELEES),
				    BUTTON_FONT_SIZE, 0, HOVER_MELEE,
				    "jewelsound") != 0) {
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_MELEES &&
				    g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_TOURNAMENTS) {
					g_game_config.mission_time_limit = -1;
				}
				changed = 1;
				g_game_config.continue_battle_or_campaign =
					SEQUENCE_CONTINUE;
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_MELEES;
				if (g_game_config.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_game_config.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				if (g_game_config.random_setup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
					g_game_config.random_setup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				frontend_mission_load_current();
				mission_setup_update_team_counts();
				mission_setup_load_mission_desc_text(
					g_mission_text);
				g_frontend_first_visible_line = 0;
				if (g_mission_list != NULL) {
					for (g_selected_mission_list_index = 0;
					     (unsigned int)
						     g_selected_mission_list_index <
					     g_mission_count;
					     ++g_selected_mission_list_index) {
						if (g_mission_list
							    [g_selected_mission_list_index]
								    .mission_idx ==
						    g_pilot_data.mission_description_ids
							    [g_pilot_data
								     .mission_directory_id]) {
							if (g_mission_list[g_selected_mission_list_index]
								    .is_unavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_mission_count;
								     ++index) {
									if (g_mission_list[index]
										    .is_unavailable ==
									    0) {
										g_pilot_data
											.mission_description_ids
												[g_pilot_data
													 .mission_directory_id] =
											g_mission_list[index]
												.mission_idx;
										g_selected_mission_list_index =
											index;
										break;
									}
								}
								frontend_mission_load_current();
								mission_setup_update_team_counts();
								mission_setup_load_mission_desc_text(
									g_mission_text);
							}
							break;
						}
					}
				}
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "game2u",
				frontend_string_get(FRONTSTR_190_MELEES),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "game2d",
			frontend_string_get(FRONTSTR_190_MELEES),
			BUTTON_FONT_SIZE, 0);
		if (is_host != 0 && frontend_draw_point_in_rect(
					    &rect, cursor_x, cursor_y) != 0) {
			if (frontend_mouse_get_right_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selected_mission_list_index ==
						    0) {
							g_selected_mission_list_index =
								g_mission_count -
								1;
						} else {
							--g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			} else if (frontend_mouse_get_left_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_mission_count -
							    g_selected_mission_list_index ==
						    1) {
							g_selected_mission_list_index =
								0;
						} else {
							++g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					selected_mission_id =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						selected_mission_id;
					frontend_mission_load_current();
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			}
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (g_pilot_data.mission_directory_id !=
	    MISSION_DIRECTORY_TRAINING_EXERCISES) {
		if (is_host != 0) {
			if (frontend_button_handle_sprite_button(
				    &rect, "game1u", "game1u",
				    frontend_string_get(FRONTSTR_189_EXERCISE),
				    BUTTON_FONT_SIZE, 0, HOVER_TRAINING,
				    "jewelsound") != 0) {
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_game_config.mission_time_limit = -1;
				}
				changed = 1;
				g_game_config.continue_battle_or_campaign =
					SEQUENCE_CONTINUE;
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_TRAINING_EXERCISES;
				if (g_game_config.difficulty ==
				    GAME_DIFFICULTY_EASY_CHEAT) {
					g_game_config.difficulty =
						GAME_DIFFICULTY_EASY;
				}
				if (g_game_config.random_setup ==
				    RANDOM_SETUP_PLAYER_CHOICE) {
					g_game_config.random_setup =
						RANDOM_SETUP_SEQUENTIAL;
				}
				frontend_mission_load_current();
				mission_setup_update_team_counts();
				mission_setup_load_mission_desc_text(
					g_mission_text);
				g_frontend_first_visible_line = 0;
				if (g_mission_list != NULL) {
					for (g_selected_mission_list_index = 0;
					     (unsigned int)
						     g_selected_mission_list_index <
					     g_mission_count;
					     ++g_selected_mission_list_index) {
						if (g_mission_list
							    [g_selected_mission_list_index]
								    .mission_idx ==
						    g_pilot_data.mission_description_ids
							    [g_pilot_data
								     .mission_directory_id]) {
							if (g_mission_list[g_selected_mission_list_index]
								    .is_unavailable !=
							    0) {
								for (index = 0;
								     index <
								     (int)g_mission_count;
								     ++index) {
									if (g_mission_list[index]
										    .is_unavailable ==
									    0) {
										g_pilot_data
											.mission_description_ids
												[g_pilot_data
													 .mission_directory_id] =
											g_mission_list[index]
												.mission_idx;
										g_selected_mission_list_index =
											index;
										break;
									}
								}
								frontend_mission_load_current();
								mission_setup_update_team_counts();
								mission_setup_load_mission_desc_text(
									g_mission_text);
							}
							break;
						}
					}
				}
			}
		} else {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "game1u",
				frontend_string_get(FRONTSTR_189_EXERCISE),
				BUTTON_FONT_SIZE, 0);
		}
	} else {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "game1d",
			frontend_string_get(FRONTSTR_189_EXERCISE),
			BUTTON_FONT_SIZE, 0);
		if (is_host != 0 && frontend_draw_point_in_rect(
					    &rect, cursor_x, cursor_y) != 0) {
			if (frontend_mouse_get_right_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_selected_mission_list_index ==
						    0) {
							g_selected_mission_list_index =
								g_mission_count -
								1;
						} else {
							--g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					int player_flight_group_index = 0;
					while (player_flight_group_index <
						       (int)g_frontend_mission
							       .flight_group_count &&
					       g_frontend_mission
							       .flight_groups
								       [player_flight_group_index]
							       .player_number ==
						       0) {
						++player_flight_group_index;
					}
					int previous_player_iff =
						g_frontend_mission
							.flight_groups
								[player_flight_group_index]
							.iff;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					frontend_mission_load_current();
					player_flight_group_index = 0;
					while (player_flight_group_index <
						       (int)g_frontend_mission
							       .flight_group_count &&
					       g_frontend_mission
							       .flight_groups
								       [player_flight_group_index]
							       .player_number ==
						       0) {
						++player_flight_group_index;
					}
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group_index]
						    .iff !=
					    previous_player_iff) {
						changed = 1;
					}
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			} else if (frontend_mouse_get_left_click() != 0) {
				if (g_mission_list != NULL) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					do {
						if (g_mission_count -
							    g_selected_mission_list_index ==
						    1) {
							g_selected_mission_list_index =
								0;
						} else {
							++g_selected_mission_list_index;
						}
					} while (
						g_mission_list
							[g_selected_mission_list_index]
								.is_unavailable !=
						0);
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id] =
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx;
					int player_flight_group_index = 0;
					while (player_flight_group_index <
						       (int)g_frontend_mission
							       .flight_group_count &&
					       g_frontend_mission
							       .flight_groups
								       [player_flight_group_index]
							       .player_number ==
						       0) {
						++player_flight_group_index;
					}
					int previous_player_iff =
						g_frontend_mission
							.flight_groups
								[player_flight_group_index]
							.iff;
					g_game_config
						.continue_battle_or_campaign =
						SEQUENCE_CONTINUE;
					frontend_mission_load_current();
					player_flight_group_index = 0;
					while (player_flight_group_index <
						       (int)g_frontend_mission
							       .flight_group_count &&
					       g_frontend_mission
							       .flight_groups
								       [player_flight_group_index]
							       .player_number ==
						       0) {
						++player_flight_group_index;
					}
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group_index]
						    .iff !=
					    previous_player_iff) {
						changed = 1;
					}
					mission_setup_update_team_counts();
					mission_setup_load_mission_desc_text(
						g_mission_text);
					g_frontend_first_visible_line = 0;
					if (g_mission_list != NULL) {
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}
					}
				}
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_is_host() != 0) {
					mission_setup_send_lobby_state(0);
				}
			}
		}
	}

	if (changed != 0) {
		front_image_free_resource_by_name("background");
		mission_setup_draw_background();
	}
	return changed;
}

/* Loads the mission list of a mission type into g_mission_list and
 * g_mission_count, freeing the old list first. The file is "<type>\mission.lst"
 * in a network game; in a solo game or with no session, training, combat
 * engagements and campaigns read rebel.lst or imperial.lst by the pilot's
 * faction, and melees, tournaments and battles read mission.lst. In the modern
 * build a list that cannot be opened ends the program; the original build asks
 * for the game's CD until it opens, and Cancel returns with an empty list.
 * Lines starting with "//" are skipped and a "[name]" line names the section of
 * the entries after it. Each entry is three lines: the mission id, the file
 * name (lowercased) and the description. A file name starting with "&" marks
 * the entry unavailable; one starting with "*" carries two numbers, from its
 * third character, that are read and dropped, and marks the entry unavailable
 * while the campaign mission with the entry's id has never been flown: by the
 * current faction in a solo game, else by either faction. A list that ends
 * early keeps the entries completed. Does not check the type id: outside 0 to 5
 * a solo game opens whatever name g_frontend_scratch_buffer held and a network
 * game reads past g_mission_directory_names. A failed allocation returns with
 * g_mission_list NULL and g_mission_count still holding the count. */
// FUNCTION: XVT 0x4E5000
void mission_setup_load_mission_list(int mission_directory_id)
{
	enum {
		MISSION_LINE_CAPACITY = 256,
		SECTION_NAME_CAPACITY = 128,
		MISSION_PREFIX_FIRST_NUMBER = 2,
	};

	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	g_mission_count = 0;
	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_frontend_mission_session_mode != FRONTEND_MISSION_SESSION_NONE) {
		sprintf(g_frontend_scratch_buffer, "%s\\mission.lst",
			g_mission_directory_names[mission_directory_id]);
	} else {
		switch (mission_directory_id) {
		case MISSION_DIRECTORY_TRAINING_EXERCISES:
		case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
		case MISSION_DIRECTORY_CAMPAIGNS:
			if (g_pilot_data.current_faction_id == 0) {
				sprintf(g_frontend_scratch_buffer,
					"%s\\rebel.lst",
					g_mission_directory_names
						[mission_directory_id]);
			} else {
				sprintf(g_frontend_scratch_buffer,
					"%s\\imperial.lst",
					g_mission_directory_names
						[mission_directory_id]);
			}
			break;
		case MISSION_DIRECTORY_MELEES:
		case MISSION_DIRECTORY_TOURNAMENTS:
		case MISSION_DIRECTORY_BATTLES:
			sprintf(g_frontend_scratch_buffer, "%s\\mission.lst",
				g_mission_directory_names
					[mission_directory_id]);
			break;
		default:
			break;
		}
	}

	xvt_file *stream;
#ifdef XVT_MODERN
	stream = file_open(g_frontend_scratch_buffer, "r");
	if (stream == NULL) {
		xvt_storage_fatal("Cannot load mission list", 1);
		return;
	}
#else
	while ((stream = file_open(g_frontend_scratch_buffer, "r")) == NULL) {
		if (frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
			    frontend_string_get(
				    FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
			    frontend_string_get(
				    FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) == 0) {
			return;
		}
	}

#endif
	g_mission_count = mission_setup_count_mission_list_entries(stream);
	file_seek(stream, 0, 0);
	if (g_mission_count == 0) {
		file_close(stream);
		return;
	}
	g_mission_list = (struct mission_list_entry *)malloc(
		sizeof(*g_mission_list) * g_mission_count);
	if (g_mission_list == NULL) {
		file_close(stream);
		return;
	}

	char current_section[SECTION_NAME_CAPACITY];
	memset(current_section, 0, sizeof(current_section));
	unsigned int character_index;
	for (unsigned int entry_index = 0; entry_index < g_mission_count;
	     ++entry_index) {
		for (;;) {
			do {
				if (FILE_GETS(g_frontend_scratch_buffer,
					      MISSION_LINE_CAPACITY,
					      stream) == NULL) {
					g_mission_count = entry_index;
					file_close(stream);
					return;
				}
			} while (g_frontend_scratch_buffer[0] == '/' &&
				 g_frontend_scratch_buffer[1] == '/');
			if (g_frontend_scratch_buffer
				    [strlen(g_frontend_scratch_buffer) - 1] ==
			    '\n') {
				g_frontend_scratch_buffer
					[strlen(g_frontend_scratch_buffer) -
					 1] = '\0';
			}
			if (g_frontend_scratch_buffer[0] != '[') {
				break;
			}
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
			strcpy(current_section, &g_frontend_scratch_buffer[1]);
		}

		strcpy(g_mission_list[entry_index].section_name,
		       current_section);
		g_mission_list[entry_index].is_unavailable = 0;
		g_mission_list[entry_index].mission_idx =
			atoi(g_frontend_scratch_buffer);
		if (FILE_GETS(g_frontend_scratch_buffer, MISSION_LINE_CAPACITY,
			      stream) == NULL) {
			g_mission_count = entry_index;
			file_close(stream);
			return;
		}
		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		for (character_index = 0;
		     character_index < strlen(g_frontend_scratch_buffer);
		     ++character_index) {
			g_frontend_scratch_buffer[character_index] =
				(char)tolower(
					(unsigned char)g_frontend_scratch_buffer
						[character_index]);
		}

		if (g_frontend_scratch_buffer[0] == '*') {
			for (character_index = MISSION_PREFIX_FIRST_NUMBER;
			     g_frontend_scratch_buffer[character_index] != ' ';
			     ++character_index) {
			}
			g_frontend_scratch_buffer[character_index] = '\0';
			(void)atoi(&g_frontend_scratch_buffer
					   [MISSION_PREFIX_FIRST_NUMBER]);
			++character_index;
			char *second_number =
				&g_frontend_scratch_buffer[character_index];
			while (g_frontend_scratch_buffer[character_index] !=
			       ' ') {
				++character_index;
			}
			g_frontend_scratch_buffer[character_index] = '\0';
			(void)atoi(second_number);
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .sp_campaign_missions
						    [g_mission_list[entry_index]
							     .mission_idx -
						     1]
					    .number_times_flown == 0) {
					g_mission_list[entry_index]
						.is_unavailable = 1;
				}
			} else if (
				g_pilot_data.faction_statistics[0]
						.mp_campaign_missions
							[g_mission_list[entry_index]
								 .mission_idx -
							 1]
						.number_times_flown == 0 &&
				g_pilot_data.faction_statistics[1]
						.mp_campaign_missions
							[g_mission_list[entry_index]
								 .mission_idx -
							 1]
						.number_times_flown == 0) {
				g_mission_list[entry_index].is_unavailable = 1;
			}
			strcpy(g_mission_list[entry_index].file_name,
			       &g_frontend_scratch_buffer[character_index + 1]);
		} else if (g_frontend_scratch_buffer[0] == '&') {
			g_mission_list[entry_index].is_unavailable = 1;
			strcpy(g_mission_list[entry_index].file_name,
			       &g_frontend_scratch_buffer[2]);
		} else {
			strcpy(g_mission_list[entry_index].file_name,
			       g_frontend_scratch_buffer);
		}

		if (FILE_GETS(g_frontend_scratch_buffer, MISSION_LINE_CAPACITY,
			      stream) == NULL) {
			g_mission_count = entry_index;
			file_close(stream);
			return;
		}
		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		strcpy(g_mission_list[entry_index].description,
		       g_frontend_scratch_buffer);
	}
	file_close(stream);
}

/* Fills the 4096-byte out_text4096 with the description of the selected mission
 * (the entry of g_mission_list matching g_pilot_data's description id for the
 * current mission type), zeroing it first. For a tournament, battle or campaign
 * it reads the sequence file as text: the first line gives a count of lines to
 * skip, and the printable characters and newlines of the rest are copied, at
 * most 4095 of them. Any other mission file is read by its format word: 14 or
 * 13 takes the file's last 4096 bytes, 12 its last 1024, each ended with a NUL
 * in its last byte; another version leaves the text empty. Returns at once, the
 * text empty, when out_text4096 is NULL, no entry matches or the file does not
 * open. */
// FUNCTION: XVT 0x4E5590
void mission_setup_load_mission_desc_text(char *out_text4096)
{
	if (out_text4096 == NULL) {
		return;
	}

	memset(out_text4096, 0, 4096);
	unsigned int mission_list_index = 0;
	while (mission_list_index < g_mission_count) {
		if (g_mission_list[mission_list_index].mission_idx ==
		    g_pilot_data.mission_description_ids
			    [g_pilot_data.mission_directory_id]) {
			break;
		}
		++mission_list_index;
	}
	if (mission_list_index >= g_mission_count) {
		return;
	}

	sprintf(g_frontend_scratch_buffer, "%s\\%s",
		g_mission_directory_names[g_pilot_data.mission_directory_id],
		g_mission_list[mission_list_index].file_name);
	xvt_file *stream = file_open(g_frontend_scratch_buffer, "rb");
	if (stream == NULL) {
		return;
	}

	if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TOURNAMENTS ||
	    g_pilot_data.mission_directory_id == MISSION_DIRECTORY_BATTLES ||
	    g_pilot_data.mission_directory_id == MISSION_DIRECTORY_CAMPAIGNS) {
		FILE_GETS(g_frontend_scratch_buffer, 256, stream);
		int skipped_line_count = atoi(g_frontend_scratch_buffer);
		while (skipped_line_count != 0) {
			FILE_GETS(g_frontend_scratch_buffer, 256, stream);
			--skipped_line_count;
		}

		int output_length = 0;
		char *output_cursor = out_text4096;
		while (FILE_GETS(g_frontend_scratch_buffer, 256, stream) !=
		       NULL) {
			unsigned int line_length =
				strlen(g_frontend_scratch_buffer);
			unsigned int character_index = 0;
			if (line_length != 0) {
				do {
					int character = (unsigned char)
						g_frontend_scratch_buffer
							[character_index];
					if (isprint(character) ||
					    character == '\n') {
						*output_cursor = character;
						++output_cursor;
						++output_length;
						if ((unsigned int)
							    output_length >=
						    4095) {
							break;
						}
					}
					++character_index;
				} while (line_length > character_index);
			}
			if ((unsigned int)output_length >= 4095) {
				break;
			}
		}
		out_text4096[output_length] = '\0';
	} else {
		uint16_t mission_version;
		file_read_word(stream, &mission_version);
		if (mission_version == 14 || mission_version == 13) {
			file_seek(stream, -4096, SEEK_END);
			file_read_bytes(stream, out_text4096, 4096);
			out_text4096[4095] = '\0';
		} else if (mission_version == 12) {
			file_seek(stream, -1024, SEEK_END);
			file_read_bytes(stream, out_text4096, 1024);
			out_text4096[1023] = '\0';
		}
	}
	file_close(stream);
}

/* Draws the mission description box of the mission setup screen: a heading for
 * a tournament, battle, campaign or other mission, then g_mission_text wrapped,
 * with a scrollbar that sets g_frontend_first_visible_line when the text's wrapped
 * line count (from a draw starting at line 4096) plus one is over 9. For a
 * battle whose saved continuation is in use the heading is followed by the
 * Imperial and Rebel victory counts: in a solo game or for a network host,
 * counted over the saved results up to the current mission index, at most 10;
 * for a network client, g_remote_battle_imperial_victory_count and
 * g_remote_battle_rebel_victory_count. In use means the continuation is active and
 * g_game_config.continue_battle_or_campaign is not SEQUENCE_RESTART, or for a
 * client, g_remote_battle_continuation_active and
 * g_remote_battle_sequence_continuation_choice both nonzero. Returns 1. Does not
 * check g_mission_list for a battle. */
// FUNCTION: XVT 0x4E5810
int mission_setup_draw_mission_description(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 138, 430, 153);
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_TOURNAMENTS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(
				FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_BATTLES) {
		int continuation_active = 0;
		int mission_index;
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx;
			if (g_pilot_data.sp_battle_continuations[mission_index]
					    .is_active != 0 &&
			    g_game_config.continue_battle_or_campaign !=
				    SEQUENCE_RESTART) {
				continuation_active = 1;
			}
		} else if (g_frontend_mission_session_mode ==
			   FRONTEND_MISSION_SESSION_NET_HOST) {
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx;
			if (g_pilot_data.mp_battle_continuations[mission_index]
					    .is_active != 0 &&
			    g_game_config.continue_battle_or_campaign !=
				    SEQUENCE_RESTART) {
				continuation_active = 1;
			}
		} else if (g_remote_battle_continuation_active != 0 &&
			   g_remote_battle_sequence_continuation_choice != 0) {
			continuation_active = 1;
		}

		if (continuation_active) {
			frontend_text_draw_aligned_in_rect(
				15,
				frontend_string_get(
					FRONTSTR_472_BATTLE_DESCRIPTION),
				&rect, 0, 1, 0xFFFF);
			rect.left += frontend_text_measure_width(
				frontend_string_get(
					FRONTSTR_472_BATTLE_DESCRIPTION),
				15);
			int imperial_victories;
			int rebel_victories;
			int result_index;
			int max_result_index;
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				imperial_victories = 0;
				rebel_victories = 0;
				result_index = 0;
				mission_index =
					g_mission_list
						[g_selected_mission_list_index]
							.mission_idx;
				max_result_index =
					g_pilot_data
						.sp_battle_continuations
							[mission_index]
						.sequence_state
						.current_mission_index;
				while (result_index <= max_result_index &&
				       result_index < 10) {
					switch (g_pilot_data
							.sp_battle_continuations
								[mission_index]
							.sequence_state
							.mission_results
								[result_index]) {
					case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
						++imperial_victories;
						break;
					case BATTLE_MISSION_RESULT_REBEL_VICTORY:
						++rebel_victories;
						break;
					default:
						break;
					}
					++result_index;
				}
			} else if (g_frontend_mission_session_mode ==
				   FRONTEND_MISSION_SESSION_NET_HOST) {
				imperial_victories = 0;
				rebel_victories = 0;
				result_index = 0;
				mission_index =
					g_mission_list
						[g_selected_mission_list_index]
							.mission_idx;
				max_result_index =
					g_pilot_data
						.mp_battle_continuations
							[mission_index]
						.sequence_state
						.current_mission_index;
				while (result_index <= max_result_index &&
				       result_index < 10) {
					switch (g_pilot_data
							.mp_battle_continuations
								[mission_index]
							.sequence_state
							.mission_results
								[result_index]) {
					case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
						++imperial_victories;
						break;
					case BATTLE_MISSION_RESULT_REBEL_VICTORY:
						++rebel_victories;
						break;
					default:
						break;
					}
					++result_index;
				}
			} else {
				rebel_victories =
					g_remote_battle_rebel_victory_count;
				imperial_victories =
					g_remote_battle_imperial_victory_count;
			}
			sprintf(g_frontend_scratch_buffer,
				"%c%s %c%d   %c%s %c%d", 5,
				frontend_string_get(
					FRONTSTR_342_IMPERIAL_VICTORIES),
				1, imperial_victories, 3,
				frontend_string_get(
					FRONTSTR_343_REBEL_VICTORIES),
				1, rebel_victories);
			frontend_text_draw_centered(
				12, g_frontend_scratch_buffer, &rect, 0xFFFF);
		} else {
			frontend_text_draw_aligned_in_rect(
				15,
				frontend_string_get(
					FRONTSTR_472_BATTLE_DESCRIPTION),
				&rect, 0, 1, 0xFFFF);
		}
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}

	frontend_draw_rect_assign(&rect, 88, 157, 420, 301);
	int line_count = frontend_text_draw_wrapped(12, g_mission_text, &rect,
						    0xFFFF, 4, 4096) +
			 1;
	if (line_count > 9) {
		frontend_draw_rect_assign(&rect, 421, 157, 430, 301);
		g_frontend_first_visible_line = frontend_scrollbar_draw(
			&rect, g_frontend_first_visible_line, line_count, 0, 5,
			(unsigned int)g_color_navy, 9);
		frontend_draw_rect_assign(&rect, 88, 157, 420, 301);
	} else {
		frontend_draw_rect_assign(&rect, 88, 157, 430, 301);
	}
	frontend_text_draw_wrapped(12, g_mission_text, &rect, 0xFFFF, 4,
				   g_frontend_first_visible_line);
	return 1;
}

/* Draws the Players in Game panel of the mission setup screen: each g_mp_roster
 * entry with a nonzero playerId, four to a column, as its rating and name, or
 * the join in progress text when it has neither; the local player's entry
 * pulses, as does every entry in a solo game. Sets
 * g_frontend_game_session_in_progress to 1 when net_count_ready_players returns more
 * than 1, else 0. Outside a solo game, over TCP/IP with internet play or over a
 * modem, each player but the host gets a latency light, light1 to light6 for
 * the average latency (capped at 749 ms) divided by 125, plus 1, and a
 * dropped-packet light, drop1 to drop6; a second pass gives each light a
 * tooltip with the latency, the dropped-packet percent and a quality rating,
 * 100 - latency_penalty - 10 * drop_penalty / 100, where latency_penalty is the
 * latency less 100, divided by 25, and drop_penalty is the drop rate in
 * hundredths of a percent less 100, each penalty kept from going under 0 and
 * the rating too. A network host that clicks another player's entry, left or
 * right, stores its index in g_mission_setup_selected_player_roster_index, or -1
 * when it was already selected; the selected entry is drawn on navy. Returns
 * 1. */
// FUNCTION: XVT 0x4E5B90
int mission_setup_draw_player_roster(int frame_counter)
{
	int mouse_x;
	int mouse_y;

	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 88, 311, 430, 326);
	frontend_text_draw_aligned_in_rect(
		15, frontend_string_get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0,
		1, 0xFFFF);
	if (net_count_ready_players() > 1) {
		g_frontend_game_session_in_progress = 1;
	} else {
		g_frontend_game_session_in_progress = 0;
	}

	int displayed_count = 0;
	frontend_draw_rect_assign(&rect, 88, 331, 258, 345);
	struct RECT light_rect;
	front_image_get_resource_rect("light1", &light_rect);

	struct RECT previous_clip_rect;
	int roster_index;
	int average_latency;
	int drop_rate;
	int *player_id;
	for (roster_index = 0; roster_index < 8; ++roster_index) {
		struct mp_roster_entry *roster_entry =
			&g_mp_roster[roster_index];
		player_id = &roster_entry->player_id;
		if (*player_id == 0) {
			continue;
		}

		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    net_is_host() != 0 &&
		    net_get_local_player_id() != *player_id &&
		    roster_index ==
			    g_mission_setup_selected_player_roster_index) {
			frontend_draw_rect(&rect, 0, 0, g_color_navy, 1);
		}

		frontend_display_get_screen_clip_rect(&previous_clip_rect);
		frontend_display_set_screen_clip_rect640x480(&rect);
		if (roster_entry->pilot_rating != 0 ||
		    roster_entry->name[0] != '\0') {
			sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
				frontend_string_get(FRONTSTR_154_DRONE +
						    roster_entry->pilot_rating),
				1, roster_entry->name);
		} else {
			sprintf(g_frontend_scratch_buffer, "%s",
				frontend_string_get(
					FRONTSTR_720_JOIN_IN_PROGRESS));
		}
		if (net_get_local_player_id() == *player_id ||
		    g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			frontend_text_draw_aligned_in_rect(
				12, g_frontend_scratch_buffer, &rect, 0, 1,
				g_pulse_color_ramp
					[((frame_counter % 24) & ~1) >> 1]);
		} else {
			frontend_text_draw_aligned_in_rect(
				12, g_frontend_scratch_buffer, &rect, 0, 1,
				g_color_yellow);
		}
		frontend_display_set_screen_clip_rect640x480(
			&previous_clip_rect);

		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    ((g_game_config.network_type == NET_TRANSPORT_TCPIP &&
		      g_game_config.internet_play != 0) ||
		     g_game_config.network_type == NET_TRANSPORT_MODEM) &&
		    net_get_host_player_id() != *player_id) {
			average_latency =
				net_get_average_latency_ms(*player_id);
			if (average_latency > 749) {
				average_latency = 749;
			}
			sprintf(g_frontend_scratch_buffer, "light%d",
				average_latency / 125 + 1);
			front_image_draw_sprite(g_frontend_scratch_buffer,
						rect.right - 33, rect.top + 2);
			/* drop_rate here is the number of the drop%d sprite:
			 * the dropped-packet percent plus one, at most 6. */
			drop_rate = net_get_packet_drop_rate_basis_points(
					    *player_id) /
					    100 +
				    1;
			if (drop_rate > 6) {
				drop_rate = 6;
			}
			sprintf(g_frontend_scratch_buffer, "drop%d", drop_rate);
			front_image_draw_sprite(g_frontend_scratch_buffer,
						rect.right - 33, rect.top + 9);
		}

		++displayed_count;
		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    net_is_host() != 0 &&
		    net_get_local_player_id() != *player_id &&
		    frontend_draw_point_in_rect(&rect, mouse_x, mouse_y)) {
			frontend_draw_rect_outline(&rect, 0, 0, g_color_green);
			if (frontend_mouse_get_left_click() != 0 ||
			    frontend_mouse_get_right_click() != 0) {
				if (g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"jewelsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				if (roster_index ==
				    g_mission_setup_selected_player_roster_index) {
					g_mission_setup_selected_player_roster_index =
						-1;
				} else {
					g_mission_setup_selected_player_roster_index =
						roster_index;
				}
			}
		}
		if (displayed_count == 4) {
			frontend_draw_rect_assign(&rect, 260, 331, 430, 345);
		} else {
			frontend_draw_rect_offset_xy(&rect, 0, 15);
		}
	}

	displayed_count = 0;
	frontend_draw_rect_assign(&rect, 88, 331, 258, 345);
	front_image_get_resource_rect("light1", &light_rect);
	struct RECT quality_rect;
	for (roster_index = 0; roster_index < 8; ++roster_index) {
		struct mp_roster_entry *roster_entry =
			&g_mp_roster[roster_index];
		player_id = &roster_entry->player_id;
		if (*player_id == 0) {
			continue;
		}
		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    ((g_game_config.network_type == NET_TRANSPORT_TCPIP &&
		      g_game_config.internet_play != 0) ||
		     g_game_config.network_type == NET_TRANSPORT_MODEM) &&
		    net_get_host_player_id() != *player_id) {
			int light_width =
				light_rect.right - light_rect.left + 1;
			average_latency =
				net_get_average_latency_ms(*player_id);
			if (average_latency > 749) {
				average_latency = 749;
			}
			/* drop_rate here is the drop rate in hundredths of a percent. */
			drop_rate = net_get_packet_drop_rate_basis_points(
				*player_id);
			int latency_penalty = average_latency - 100;
			if (latency_penalty < 0) {
				latency_penalty = 0;
			}
			latency_penalty /= 25;
			int drop_penalty = drop_rate - 100;
			if (drop_penalty < 0) {
				drop_penalty = 0;
			}
			int connection_quality =
				100 - latency_penalty - 10 * drop_penalty / 100;
			if (connection_quality < 0) {
				connection_quality = 0;
			}
			sprintf(g_frontend_scratch_buffer,
				"%s: %d %s  |  %s: %d%%  |  %s: %d%%",
				frontend_string_get(
					FRONTSTR_755_AVERAGE_LATENCY),
				average_latency,
				frontend_string_get(FRONTSTR_756_MS),
				frontend_string_get(
					FRONTSTR_758_PACKETS_DROPPED),
				drop_rate / 100,
				frontend_string_get(
					FRONTSTR_757_CONNECTION_QUALITY_RATING),
				connection_quality);
			frontend_draw_rect_assign(
				&quality_rect, rect.right - 33, rect.top + 2,
				rect.right - 33 + light_width,
				rect.top + 2 + light_rect.bottom -
					light_rect.top);
			frontend_button_draw_sprite_and_tooltip(
				&quality_rect, NULL, g_frontend_scratch_buffer,
				12, 0xFFFF);
		}
		++displayed_count;
		if (displayed_count == 4) {
			frontend_draw_rect_assign(&rect, 260, 331, 430, 345);
		} else {
			frontend_draw_rect_offset_xy(&rect, 0, 15);
		}
	}
	return 1;
}

/* Sends a lobby selection packet to every player; mission_setup_update calls it
 * on the host when more than 5000 ms have passed since the last. Counting the
 * packet type as word 0, words 1 to 8 carry the game name, 10 and 11 the
 * mission type and its selected description id, 12 the value 8, and from word
 * 14 six words per player: id, pilot rating, average latency in ms, and its
 * packet, dropped packet and retry counts. While
 * g_mission_setup_roster_authoritative is set it first clears the playerId of each
 * g_mp_roster entry with no ready network player of that id, then sends all
 * eight entries and 8 in word 13; otherwise it sends each ready network player,
 * its rating being the first byte of its player info minus 1, and the ready
 * count in word 13. Word 9 is not set. Returns 1. */
// FUNCTION: XVT 0x4E6120
int mission_setup_broadcast_lobby_selection(void)
{
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_LOBBY_SELECTION;
	memcpy(g_frontend_net_packet_scratch.payload,
	       g_pilot_data.multiplayer_game_name,
	       sizeof(g_pilot_data.multiplayer_game_name));
	int mission_directory_id = g_pilot_data.mission_directory_id;
	int *packet_words = &g_frontend_net_packet_scratch.packet_type;
	packet_words[10] = mission_directory_id;
	int mission_description_id =
		g_pilot_data.mission_description_ids[mission_directory_id];
	packet_words[11] = mission_description_id;
	const int roster_capacity = 8;
	const int header_word_count = 14;
	int packet_word_count;
	int roster_count;
	int roster_index;
	struct net_player_info *player_roster;
	if (g_mission_setup_roster_authoritative != 0) {
		packet_word_count = header_word_count;
		packet_words[12] = roster_capacity;
		packet_words[13] = roster_capacity;
		player_roster = net_get_player_roster(&roster_count);
		/* Before sending, clear each roster entry with no ready player
		 * in the network roster. */
		for (roster_index = 0; roster_index < roster_capacity;
		     ++roster_index) {
			struct mp_roster_entry *roster_entry =
				&g_mp_roster[roster_index];

			int player_roster_index = 0;
			if (roster_count > 0) {
				struct net_player_info *player = player_roster;

				do {
					if (player->player_id != 0 &&
					    player->ready_flag != 0 &&
					    roster_entry->player_id ==
						    (int)player->player_id) {
						break;
					}
					++player;
					++player_roster_index;
				} while (player_roster_index < roster_count);
			}
			if (player_roster_index == roster_count) {
				roster_entry->player_id = 0;
			}
		}

		for (roster_index = 0; roster_index < roster_capacity;
		     ++roster_index) {
			const struct mp_roster_entry *roster_entry =
				&g_mp_roster[roster_index];
			int player_id = roster_entry->player_id;

			packet_words[packet_word_count++] = player_id;
			packet_words[packet_word_count++] =
				roster_entry->pilot_rating;
			packet_words[packet_word_count++] =
				net_get_average_latency_ms(player_id);
			packet_words[packet_word_count++] =
				net_get_player_packet_count(player_id);
			packet_words[packet_word_count++] =
				net_get_player_packet_drop_count(player_id);
			packet_words[packet_word_count++] =
				net_get_player_packet_retry_count(player_id);
		}
	} else {
		packet_words[12] = roster_capacity;
		packet_word_count = header_word_count;
		packet_words[13] = net_count_ready_players();
		player_roster = net_get_player_roster(&roster_count);
		roster_index = 0;
		if (roster_count > 0) {
			struct net_player_info *player = player_roster;

			do {
				if (player->ready_flag == 1) {
					packet_words[packet_word_count++] =
						player->player_id;
					packet_words[packet_word_count++] =
						(int)(uint8_t)
							player->long_name[0] -
						1;
					packet_words[packet_word_count++] =
						net_get_average_latency_ms(
							player->player_id);
					packet_words[packet_word_count++] =
						net_get_player_packet_count(
							player->player_id);
					packet_words[packet_word_count++] =
						net_get_player_packet_drop_count(
							player->player_id);
					packet_words[packet_word_count++] =
						net_get_player_packet_retry_count(
							player->player_id);
				}
				++player;
				++roster_index;
			} while (roster_index < roster_count);
		}
	}
	net_send_packet_and_flush(
		0, &g_frontend_net_packet_scratch,
		(unsigned int)(packet_word_count * sizeof(int)));
	return 1;
}

/* Sends the host's lobby state to to_player_id, 0 meaning every player. First a
 * STATE packet: counting the type as word 0, words 1 to 8 carry the game name,
 * 10 and 11 the mission type and its selected description id, 12 the value 8,
 * and from word 14 three words per player: id, pilot rating and average latency
 * in ms. While g_mission_setup_roster_authoritative is set it first refreshes each
 * g_mp_roster entry's rating from its ready network player, or clears its
 * playerId when there is none, sends all eight entries with 8 in word 13, and
 * then a LOADOUT_ROSTER packet with each entry's five loadout fields. Otherwise
 * it sends each ready network player, its rating being the first byte of its
 * player info minus 1, with net_count_ready_players() in word 13, and for a
 * battle a BATTLE_PROGRESS packet: whether the selected mission's saved
 * continuation is active (the solo one in a solo game, else the network one),
 * the continue choice, its current mission index, and the Rebel and Imperial
 * victories over its results up to that index, at most 10. Last a GAME_OPTIONS
 * packet of 19 words: the type, g_game_config settings, and in word 13 a rand()
 * value. Word 9 of the STATE packet is not set. Returns what
 * net_send_packet_and_flush returns for the options packet. */
// FUNCTION: XVT 0x4E6310
int mission_setup_send_lobby_state(int to_player_id)
{
	enum {
		PACKET_HEADER_WORD_COUNT = 14,
		ROSTER_RECORD_WORD_COUNT = 3,
		LOADOUT_RECORD_WORD_COUNT = 5,
	};

	int *packet_words = &g_frontend_net_packet_scratch.packet_type;
	int roster_capacity =
		(int)(sizeof(g_mp_roster) / sizeof(g_mp_roster[0]));
	packet_words[0] = NET_PACKET_STATE;
	memcpy(g_frontend_net_packet_scratch.payload,
	       g_pilot_data.multiplayer_game_name,
	       sizeof(g_pilot_data.multiplayer_game_name));
	int mission_directory_id = g_pilot_data.mission_directory_id;
	packet_words[10] = mission_directory_id;
	packet_words[11] =
		g_pilot_data.mission_description_ids[mission_directory_id];
	int roster_count;
	struct net_player_info *player_roster;
	int player_roster_index;
	int roster_index;
	if (g_mission_setup_roster_authoritative != 0) {
		int packet_word_count = PACKET_HEADER_WORD_COUNT;
		packet_words[12] = roster_capacity;
		packet_words[13] = roster_capacity;
		player_roster = net_get_player_roster(&roster_count);
		/* Before sending, refresh each roster entry's pilot rating from
		 * the network roster, and clear entries with no ready player
		 * there. */
		for (roster_index = 0; roster_index < roster_capacity;
		     ++roster_index) {
			struct mp_roster_entry *roster_entry =
				&g_mp_roster[roster_index];

			for (player_roster_index = 0;
			     player_roster_index < roster_count;
			     ++player_roster_index) {
				if (player_roster[player_roster_index]
						    .player_id != 0 &&
				    player_roster[player_roster_index]
						    .ready_flag != 0 &&
				    player_roster[player_roster_index]
						    .player_id ==
					    (DPID)roster_entry->player_id) {
					roster_entry->pilot_rating =
						(pilot_rating)(uint8_t)player_roster
							[player_roster_index]
								.long_name[0] -
						1;
					break;
				}
			}
			if (player_roster_index == roster_count) {
				roster_entry->player_id = 0;
			}
		}
		for (roster_index = 0; roster_index < roster_capacity;
		     ++roster_index) {
			const struct mp_roster_entry *roster_entry =
				&g_mp_roster[roster_index];
			int player_id = roster_entry->player_id;

			packet_words[packet_word_count++] = player_id;
			packet_words[packet_word_count++] =
				roster_entry->pilot_rating;
			packet_words[packet_word_count++] =
				net_get_average_latency_ms(player_id);
		}
		net_send_packet_and_flush(
			to_player_id, &g_frontend_net_packet_scratch,
			(unsigned int)(packet_word_count *
				       sizeof(packet_words[0])));

		packet_word_count = 1;
		packet_words[0] = NET_PACKET_LOADOUT_ROSTER;
		for (roster_index = 0; roster_index < roster_capacity;
		     ++roster_index) {
			const struct mp_roster_entry *roster_entry =
				&g_mp_roster[roster_index];

			packet_words[packet_word_count++] =
				roster_entry->craft_type_override;
			packet_words[packet_word_count++] =
				roster_entry->craft_option_index;
			packet_words[packet_word_count++] =
				roster_entry->warhead_option_index;
			packet_words[packet_word_count++] =
				roster_entry->beam_option_index;
			packet_words[packet_word_count++] =
				roster_entry->countermeasure_option_index;
		}
		net_send_packet_and_flush(
			to_player_id, &g_frontend_net_packet_scratch,
			(unsigned int)(packet_word_count *
				       sizeof(packet_words[0])));
	} else {
		int ready_packet_word_count = PACKET_HEADER_WORD_COUNT;
		packet_words[12] = roster_capacity;
		packet_words[13] = net_count_ready_players();
		player_roster = net_get_player_roster(&roster_count);
		for (roster_index = 0; roster_index < roster_count;
		     ++roster_index) {
			const struct net_player_info *player =
				&player_roster[roster_index];

			if (player->ready_flag != 1) {
				continue;
			}
			packet_words[ready_packet_word_count++] =
				player->player_id;
			packet_words[ready_packet_word_count++] =
				(int)(uint8_t)player->long_name[0] - 1;
			packet_words[ready_packet_word_count++] =
				net_get_average_latency_ms(player->player_id);
		}
		net_send_packet_and_flush(
			to_player_id, &g_frontend_net_packet_scratch,
			(unsigned int)(ready_packet_word_count *
				       sizeof(packet_words[0])));

		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_BATTLES) {
			packet_words[0] = NET_PACKET_BATTLE_PROGRESS;
			const int *mission_index;
			int imperial_victories = 0;
			int rebel_victories = 0;
			int result_index;
			int current_mission_index;
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				mission_index =
					&g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx;
				packet_words[1] =
					g_pilot_data
						.sp_battle_continuations
							[*mission_index]
						.is_active;
				packet_words[2] =
					(uint8_t)g_game_config
						.continue_battle_or_campaign;
				packet_words[3] =
					g_pilot_data
						.sp_battle_continuations
							[*mission_index]
						.sequence_state
						.current_mission_index;
				current_mission_index =
					g_pilot_data
						.sp_battle_continuations
							[*mission_index]
						.sequence_state
						.current_mission_index;
				result_index = 0;
				if (current_mission_index >= 0) {
					do {
						if (result_index >=
						    (int)(sizeof(g_pilot_data
									 .sp_battle_continuations
										 [*mission_index]
									 .sequence_state
									 .mission_results) /
							  sizeof(g_pilot_data
									 .sp_battle_continuations
										 [*mission_index]
									 .sequence_state
									 .mission_results
										 [0]))) {
							break;
						}
						switch (g_pilot_data
								.sp_battle_continuations
									[*mission_index]
								.sequence_state
								.mission_results
									[result_index]) {
						case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
							++imperial_victories;
							break;
						case BATTLE_MISSION_RESULT_REBEL_VICTORY:
							++rebel_victories;
							break;
						default:
							break;
						}
						++result_index;
					} while (result_index <=
						 current_mission_index);
				}
			} else {
				mission_index =
					&g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx;
				packet_words[1] =
					g_pilot_data
						.mp_battle_continuations
							[*mission_index]
						.is_active;
				packet_words[2] =
					(uint8_t)g_game_config
						.continue_battle_or_campaign;
				packet_words[3] =
					g_pilot_data
						.mp_battle_continuations
							[*mission_index]
						.sequence_state
						.current_mission_index;
				current_mission_index =
					g_pilot_data
						.mp_battle_continuations
							[*mission_index]
						.sequence_state
						.current_mission_index;
				result_index = 0;
				if (current_mission_index >= 0) {
					do {
						if (result_index >=
						    (int)(sizeof(g_pilot_data
									 .mp_battle_continuations
										 [*mission_index]
									 .sequence_state
									 .mission_results) /
							  sizeof(g_pilot_data
									 .mp_battle_continuations
										 [*mission_index]
									 .sequence_state
									 .mission_results
										 [0]))) {
							break;
						}
						switch (g_pilot_data
								.mp_battle_continuations
									[*mission_index]
								.sequence_state
								.mission_results
									[result_index]) {
						case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
							++imperial_victories;
							break;
						case BATTLE_MISSION_RESULT_REBEL_VICTORY:
							++rebel_victories;
							break;
						default:
							break;
						}
						++result_index;
					} while (result_index <=
						 current_mission_index);
				}
			}
			packet_words[4] = rebel_victories;
			packet_words[5] = imperial_victories;
			net_send_packet_and_flush(
				to_player_id, &g_frontend_net_packet_scratch,
				6 * sizeof(packet_words[0]));
		}
	}

	packet_words[0] = NET_PACKET_GAME_OPTIONS;
	packet_words[1] = (uint8_t)g_game_config.difficulty;
	packet_words[2] = g_game_config.collisions;
	packet_words[3] = g_game_config.craft_jumping;
	packet_words[4] = g_game_config.random_setup;
	packet_words[5] = (uint8_t)g_game_config.battle_length_index;
	packet_words[6] = g_game_config.require_password;
	packet_words[7] = g_game_config.in_progress_join;
	packet_words[8] = (uint8_t)g_game_config.craft_selection;
	packet_words[9] = g_game_config.locate_players;
	packet_words[10] = (uint8_t)g_game_config.craft_waves;
	packet_words[11] = g_game_config.mission_time_limit;
	packet_words[12] = g_game_config.last_team_time_limit_minutes;
	packet_words[13] = rand();
	packet_words[14] = g_game_config.internet_play;
	packet_words[15] = g_game_config.ai_opponents;
	packet_words[16] = g_game_config.server_update_rate;
	packet_words[17] = (uint8_t)g_game_config.combat_balance;
	packet_words[18] = (uint8_t)g_game_config.continue_battle_or_campaign;
	return net_send_packet_and_flush(to_player_id,
					 &g_frontend_net_packet_scratch,
					 19 * sizeof(packet_words[0]));
}

/* Sends a READY_ROSTER packet to to_player_id, 0 meaning every player (its one
 * caller, mission_setup_update, passes 0): 8, the ready player count, then for
 * each ready network player its id, its rating (the first byte of its player
 * info minus 1) and its average latency in ms. Returns what
 * net_send_packet_and_flush returns. */
// FUNCTION: XVT 0x4E6750
int mission_setup_broadcast_ready_roster(int to_player_id)
{
	enum { ROSTER_CAPACITY = 8, PACKET_HEADER_WORD_COUNT = 3 };

	int *packet_words = (int *)&g_frontend_net_packet_scratch;
	packet_words[0] = NET_PACKET_READY_ROSTER;
	packet_words[1] = ROSTER_CAPACITY;
	int packet_word_count = PACKET_HEADER_WORD_COUNT;
	packet_words[2] = net_count_ready_players();
	int roster_count;
	struct net_player_info *roster = net_get_player_roster(&roster_count);
	for (int roster_index = 0; roster_index < roster_count;
	     ++roster_index) {
		if (roster[roster_index].ready_flag == 1) {
			packet_words[packet_word_count++] =
				roster[roster_index].player_id;
			packet_words[packet_word_count++] =
				(int)(uint8_t)roster[roster_index]
					.long_name[0] -
				1;
			packet_words[packet_word_count++] =
				net_get_average_latency_ms(
					roster[roster_index].player_id);
		}
	}
	return net_send_packet_and_flush(
		to_player_id, &g_frontend_net_packet_scratch,
		(unsigned int)(packet_word_count * sizeof(int)));
}

/* The mission list that drops down on the mission setup screen, a pushed screen
 * run each frame. On frame 0 it counts the rows in
 * g_mission_setup_mission_list_row_count, one per available mission and one per
 * section heading, and sets g_mission_setup_mission_list_scroll_offset to the
 * selected mission's index in g_mission_list, or 0 when the count is under 20.
 * Outside a solo game a lobby state from the host loads the host's mission, as
 * mission_setup_update does. It draws up to 19 rows, section headings red and
 * missions white, the selected one yellow and, outside a solo game, a
 * single-player mission (file name starting with "1") gray, with a scrollbar
 * when there are more rows; then each mission's award beside it: a medal
 * (medlvl) or, for training and combat engagements, a citation (citlvl for the
 * Imperial faction, rcitlvl for the Rebel), with a tooltip naming it. A solo
 * game shows the current faction's award; otherwise the lower nonzero award
 * level of the two factions, the tooltip naming each faction's. A click on a
 * mission selects it (outside a solo game only when its file name does not
 * start with "1"), takes a saved battle's length and setup or a saved
 * campaign's setup, loads the mission and its text and, outside a solo game,
 * sends the lobby state; a click on a mission row, or a click outside the list,
 * closes the list. Returns 0. When g_mission_list is NULL it pops the screen and
 * goes on drawing. In a solo game a campaign picked here takes its setup from
 * sp_campaign_continuations at the mission id plus 12 unless net_is_host returns
 * nonzero. */
// FUNCTION: XVT 0x4E67F0
int mission_setup_draw_mission_list(int frame_counter)
{
	enum {
		VISIBLE_ROW_COUNT = 19,
		ROW_HEIGHT = 15,
		LIST_LEFT = 84,
		LIST_TOP = 131,
		LIST_RIGHT = 434,
		LIST_SCROLLBAR_RIGHT = 423,
		LIST_BOTTOM_PADDING = 139,
		MISSION_TEXT_INDENT = 37,
		TEXT_FONT_SIZE = 12,
		SCROLLBAR_CONTROL_ID = 8,
		SINGLE_PLAYER_FILE_PREFIX = '1',
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
		AWARD_TOOLTIP_MEDAL_BASE = FRONTSTR_381_BATTLE_MEDALLION,
		AWARD_TOOLTIP_CITATION_BASE = FRONTSTR_659_QUICK_START,
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
	};

	if (g_mission_list == NULL) {
		keyboard_flush_char_buffer();
		frontend_screen_pop_state();
	}

	int mission_list_index;
	char last_section_name[sizeof(g_mission_list[0].section_name)];
	if (frame_counter == 0) {
		g_mission_setup_mission_list_row_count = g_mission_count;
		memset(last_section_name, 0, sizeof(last_section_name));
		for (mission_list_index = 0;
		     (unsigned int)mission_list_index < g_mission_count;
		     ++mission_list_index) {
			if (g_mission_list[mission_list_index].is_unavailable !=
			    0) {
				--g_mission_setup_mission_list_row_count;
			} else {
				if (g_pilot_data.mission_description_ids
					    [g_pilot_data
						     .mission_directory_id] ==
				    g_mission_list[mission_list_index]
					    .mission_idx) {
					g_mission_setup_mission_list_scroll_offset =
						mission_list_index;
				}
				if (strcmp(g_mission_list[mission_list_index]
						   .section_name,
					   last_section_name) != 0) {
					++g_mission_setup_mission_list_row_count;
					strcpy(last_section_name,
					       g_mission_list
						       [mission_list_index]
							       .section_name);
				}
			}
		}
		if (g_mission_setup_mission_list_row_count <
		    VISIBLE_ROW_COUNT + 1) {
			g_mission_setup_mission_list_scroll_offset = 0;
		}
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    frontend_net_process_network_packets() == NET_PACKET_STATE) {
		if (g_pilot_data.mission_directory_id !=
			    g_frontend_net_received_mission_directory_id ||
		    g_pilot_data.mission_description_ids
				    [g_frontend_net_received_mission_directory_id] !=
			    g_frontend_net_received_mission_description_id) {
			g_game_config.continue_battle_or_campaign =
				SEQUENCE_CONTINUE;
		}
		g_pilot_data.mission_directory_id =
			g_frontend_net_received_mission_directory_id;
		g_pilot_data.mission_description_ids
			[g_frontend_net_received_mission_directory_id] =
			g_frontend_net_received_mission_description_id;
		frontend_mission_load_current();
		front_image_free_resource_by_name("background");
		mission_setup_draw_background();
		mission_setup_load_mission_desc_text(g_mission_text);
		mission_setup_update_team_counts();
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			while ((unsigned int)g_selected_mission_list_index <
				       g_mission_count &&
			       g_mission_list[g_selected_mission_list_index]
					       .mission_idx !=
				       g_pilot_data.mission_description_ids
					       [g_pilot_data
							.mission_directory_id]) {
				++g_selected_mission_list_index;
			}
		}
		g_frontend_first_visible_line = 0;
		g_mission_setup_selected_player_roster_index = -1;
		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    (g_pilot_data.mission_directory_id ==
			     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
		     g_pilot_data.mission_directory_id ==
			     MISSION_DIRECTORY_BATTLES) &&
		    g_game_config.craft_waves == CRAFT_WAVES_UNLIMITED) {
			g_game_config.craft_waves = CRAFT_WAVES_DEFAULT;
		}
		if (g_game_config.craft_selection ==
			    CRAFT_SELECTION_HOST_ONLY &&
		    (g_frontend_mission_session_mode ==
			     FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		     (g_pilot_data.mission_directory_id !=
			      MISSION_DIRECTORY_MELEES &&
		      g_pilot_data.mission_directory_id !=
			      MISSION_DIRECTORY_TOURNAMENTS))) {
			g_game_config.craft_selection = CRAFT_SELECTION_ON;
		}
		if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_BATTLES &&
		    g_game_config.random_setup == 2) {
			g_game_config.random_setup = 0;
		}
		if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_CAMPAIGNS &&
		    g_game_config.difficulty == GAME_DIFFICULTY_EASY_CHEAT) {
			g_game_config.difficulty = GAME_DIFFICULTY_EASY;
		}
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_CAMPAIGNS) {
			g_game_config.random_setup = 0;
		}
	}

	memset(last_section_name, 0, sizeof(last_section_name));
	struct RECT rect;
	if (g_mission_setup_mission_list_row_count > VISIBLE_ROW_COUNT) {
		frontend_draw_rect_assign(&rect, 424, LIST_TOP, LIST_RIGHT,
					  424);
		g_mission_setup_mission_list_scroll_offset =
			frontend_scrollbar_draw(
				&rect,
				g_mission_setup_mission_list_scroll_offset,
				g_mission_setup_mission_list_row_count, 0, 5,
				(unsigned int)g_color_navy,
				SCROLLBAR_CONTROL_ID);
	}

	int cursor_x;
	int cursor_y;
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	/* display_row first holds the number of rows the list box shows, to
	 * size the box; from the list loop on it is the running row index,
	 * headers included. */
	int display_row = VISIBLE_ROW_COUNT;
	if (g_mission_setup_mission_list_row_count <= VISIBLE_ROW_COUNT) {
		display_row = g_mission_setup_mission_list_row_count;
	}
	frontend_draw_rect_assign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				  ROW_HEIGHT * display_row +
					  LIST_BOTTOM_PADDING);
	if (!frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		keyboard_flush_char_buffer();
		frontend_screen_pop_state();
		frontend_unregister_scrollable_control(SCROLLBAR_CONTROL_ID);
		front_image_free_resource_by_name("background");
		mission_setup_draw_background();
	}

	frontend_draw_rect_assign(
		&rect, LIST_LEFT, LIST_TOP,
		g_mission_setup_mission_list_row_count <= VISIBLE_ROW_COUNT
			? LIST_RIGHT
			: LIST_SCROLLBAR_RIGHT,
		ROW_HEIGHT * display_row + LIST_BOTTOM_PADDING);
	frontend_draw_rect(&rect, 0, 0, frontend_display_pack_rgb(0, 0, 64), 1);
	frontend_draw_rect(&rect, 0, 0, 0xFFFF, 0);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	frontend_draw_rect(&rect, 0, 0, 0, 0);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	rect.left += MISSION_TEXT_INDENT;
	rect.bottom = rect.top + 14;

	display_row = 0;
	for (mission_list_index = 0;
	     (unsigned int)mission_list_index < g_mission_count;
	     ++mission_list_index) {
		if (g_mission_list[mission_list_index].is_unavailable != 0) {
			continue;
		}
		if (strcmp(g_mission_list[mission_list_index].section_name,
			   last_section_name) != 0) {
			if (display_row >=
				    g_mission_setup_mission_list_scroll_offset &&
			    display_row - g_mission_setup_mission_list_scroll_offset <
				    VISIBLE_ROW_COUNT) {
				rect.left -= MISSION_TEXT_INDENT;
				frontend_text_draw_aligned_in_rect(
					TEXT_FONT_SIZE,
					g_mission_list[mission_list_index]
						.section_name,
					&rect, 0, 1, g_color_red);
				rect.left += MISSION_TEXT_INDENT;
				frontend_draw_rect_offset_xy(&rect, 0,
							     ROW_HEIGHT);
			}
			strcpy(last_section_name,
			       g_mission_list[mission_list_index].section_name);
			++display_row;
		}
		if (display_row - g_mission_setup_mission_list_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (display_row < g_mission_setup_mission_list_scroll_offset ||
		    display_row - g_mission_setup_mission_list_scroll_offset >=
			    VISIBLE_ROW_COUNT) {
			++display_row;
			if (display_row -
				    g_mission_setup_mission_list_scroll_offset >=
			    VISIBLE_ROW_COUNT) {
				break;
			}
			continue;
		} else {
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y)) {
				frontend_draw_rect(&rect, 0, 0, g_color_green,
						   0);
				if (frontend_mouse_get_left_click() != 0 ||
				    frontend_mouse_get_right_click() != 0) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}

					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_mission_list
							    [mission_list_index]
								    .file_name
									    [0] !=
						    SINGLE_PLAYER_FILE_PREFIX) {
							g_pilot_data.mission_description_ids
								[g_pilot_data
									 .mission_directory_id] =
								g_mission_list[mission_list_index]
									.mission_idx;
							g_selected_mission_list_index =
								0;
							while ((unsigned int)g_selected_mission_list_index <
								       g_mission_count &&
							       g_mission_list[g_selected_mission_list_index]
									       .mission_idx !=
								       g_pilot_data
									       .mission_description_ids
										       [g_pilot_data
												.mission_directory_id]) {
								++g_selected_mission_list_index;
							}

							if (g_pilot_data
								    .mission_directory_id ==
							    MISSION_DIRECTORY_BATTLES) {
								if (g_frontend_mission_session_mode ==
								    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
									if (g_pilot_data
										    .sp_battle_continuations
											    [g_mission_list[g_selected_mission_list_index]
												     .mission_idx]
										    .is_active !=
									    0) {
										g_game_config
											.battle_length_index =
											g_pilot_data
												.sp_battle_continuations
													[g_mission_list[g_selected_mission_list_index]
														 .mission_idx]
												.battle_length_index;
										g_game_config
											.random_setup =
											g_pilot_data
												.sp_battle_continuations
													[g_mission_list[g_selected_mission_list_index]
														 .mission_idx]
												.random_setup;
									}
								} else if (
									g_pilot_data
										.mp_battle_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.is_active !=
									0) {
									g_game_config
										.battle_length_index =
										g_pilot_data
											.mp_battle_continuations
												[g_mission_list[g_selected_mission_list_index]
													 .mission_idx]
											.battle_length_index;
									g_game_config
										.random_setup =
										g_pilot_data
											.mp_battle_continuations
												[g_mission_list[g_selected_mission_list_index]
													 .mission_idx]
											.random_setup;
								}
							} else if (
								g_pilot_data
									.mission_directory_id ==
								MISSION_DIRECTORY_CAMPAIGNS) {
								if (g_frontend_mission_session_mode ==
								    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
									if (g_pilot_data
										    .sp_campaign_continuations
											    [g_mission_list[g_selected_mission_list_index]
												     .mission_idx]
										    .is_active !=
									    0) {
										g_game_config
											.random_setup =
											g_pilot_data
												.sp_campaign_continuations
													[g_mission_list[g_selected_mission_list_index]
														 .mission_idx]
												.random_setup;
									}
								} else {
									if (net_is_host() !=
									    0) {
										if (g_pilot_data
											    .mp_campaign_continuations
												    [g_mission_list[g_selected_mission_list_index]
													     .mission_idx]
											    .is_active !=
										    0) {
											g_game_config
												.random_setup =
												g_pilot_data
													.mp_campaign_continuations
														[g_mission_list[g_selected_mission_list_index]
															 .mission_idx]
													.random_setup;
										}
									} else if (
										g_pilot_data
											.mp_campaign_continuations
												[g_mission_list[g_selected_mission_list_index]
													 .mission_idx +
												 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
											.is_active !=
										0) {
										g_game_config
											.random_setup =
											g_pilot_data
												.mp_campaign_continuations
													[g_mission_list[g_selected_mission_list_index]
														 .mission_idx +
													 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
												.random_setup;
									}
								}
							}
							frontend_mission_load_current();
							front_image_free_resource_by_name(
								"background");
							mission_setup_draw_background();
							mission_setup_update_team_counts();
							mission_setup_load_mission_desc_text(
								g_mission_text);
							g_frontend_first_visible_line =
								0;
							if (g_frontend_mission_session_mode !=
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								mission_setup_send_lobby_state(
									0);
							}
						}
					} else {
						g_pilot_data.mission_description_ids
							[g_pilot_data
								 .mission_directory_id] =
							g_mission_list
								[mission_list_index]
									.mission_idx;
						g_selected_mission_list_index =
							0;
						while ((unsigned int)g_selected_mission_list_index <
							       g_mission_count &&
						       g_mission_list[g_selected_mission_list_index]
								       .mission_idx !=
							       g_pilot_data.mission_description_ids
								       [g_pilot_data
										.mission_directory_id]) {
							++g_selected_mission_list_index;
						}

						if (g_pilot_data
							    .mission_directory_id ==
						    MISSION_DIRECTORY_BATTLES) {
							if (g_pilot_data
								    .sp_battle_continuations
									    [g_mission_list[g_selected_mission_list_index]
										     .mission_idx]
								    .is_active !=
							    0) {
								g_game_config
									.battle_length_index =
									g_pilot_data
										.sp_battle_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.battle_length_index;
								g_game_config
									.random_setup =
									g_pilot_data
										.sp_battle_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.random_setup;
							}
						} else if (
							g_pilot_data
								.mission_directory_id ==
							MISSION_DIRECTORY_CAMPAIGNS) {
							if (net_is_host() !=
							    0) {
								if (g_pilot_data
									    .sp_campaign_continuations
										    [g_mission_list[g_selected_mission_list_index]
											     .mission_idx]
									    .is_active !=
								    0) {
									g_game_config
										.random_setup =
										g_pilot_data
											.sp_campaign_continuations
												[g_mission_list[g_selected_mission_list_index]
													 .mission_idx]
											.random_setup;
								}
							} else if (
								g_pilot_data
									.sp_campaign_continuations
										[g_mission_list[g_selected_mission_list_index]
											 .mission_idx +
										 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
									.is_active !=
								0) {
								g_game_config
									.random_setup =
									g_pilot_data
										.sp_campaign_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx +
											 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
										.random_setup;
							}
						}
						frontend_mission_load_current();
						front_image_free_resource_by_name(
							"background");
						mission_setup_draw_background();
						mission_setup_update_team_counts();
						mission_setup_load_mission_desc_text(
							g_mission_text);
						g_frontend_first_visible_line =
							0;
					}

					keyboard_flush_char_buffer();
					frontend_screen_pop_state();
					frontend_unregister_scrollable_control(
						SCROLLBAR_CONTROL_ID);
					front_image_free_resource_by_name(
						"background");
					mission_setup_draw_background();
				}
			}

			unsigned short text_color = 0xFFFF;
			if (g_pilot_data.mission_description_ids
				    [g_pilot_data.mission_directory_id] ==
			    g_mission_list[mission_list_index].mission_idx) {
				text_color = g_color_yellow;
			}
			if (g_mission_list[mission_list_index].file_name[0] ==
				    SINGLE_PLAYER_FILE_PREFIX &&
			    g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				text_color = g_color_gray;
			}
			frontend_text_draw_aligned_in_rect(
				TEXT_FONT_SIZE,
				g_mission_list[mission_list_index].description,
				&rect, 0, 1, text_color);
			frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
		}
		++display_row;
		if (display_row - g_mission_setup_mission_list_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}

	memset(last_section_name, 0, sizeof(last_section_name));
	frontend_draw_rect_assign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				  ROW_HEIGHT * mission_list_index +
					  LIST_BOTTOM_PADDING);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	display_row = 0;
	rect.left += MISSION_TEXT_INDENT;
	rect.bottom = rect.top + 14;

	struct RECT award_rect;
	unsigned int award_id;
	int award_faction_id;
	unsigned int imperial_award_id;
	unsigned int rebel_award_id;
	for (mission_list_index = 0;
	     (unsigned int)mission_list_index < g_mission_count;
	     ++mission_list_index) {
		if (g_mission_list[mission_list_index].is_unavailable != 0) {
			continue;
		}
		if (strcmp(g_mission_list[mission_list_index].section_name,
			   last_section_name) != 0) {
			if (display_row >=
				    g_mission_setup_mission_list_scroll_offset &&
			    display_row - g_mission_setup_mission_list_scroll_offset <
				    VISIBLE_ROW_COUNT) {
				frontend_draw_rect_offset_xy(&rect, 0,
							     ROW_HEIGHT);
			}
			strcpy(last_section_name,
			       g_mission_list[mission_list_index].section_name);
			++display_row;
		}
		if (display_row - g_mission_setup_mission_list_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (display_row < g_mission_setup_mission_list_scroll_offset ||
		    display_row - g_mission_setup_mission_list_scroll_offset >=
			    VISIBLE_ROW_COUNT) {
			++display_row;
			if (display_row -
				    g_mission_setup_mission_list_scroll_offset >=
			    VISIBLE_ROW_COUNT) {
				break;
			}
			continue;
		} else {
			award_id = 0;
			rebel_award_id = 0;
			imperial_award_id = 0;
			award_faction_id = 0;
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				award_faction_id =
					g_pilot_data.current_faction_id;
				switch (g_pilot_data.mission_directory_id) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_training_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_MELEES:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_melee_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_tournaments
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_combat_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_BATTLES:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_battles
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					break;
				default:
					break;
				}
			} else {
				switch (g_pilot_data.mission_directory_id) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					rebel_award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_training_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_training_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					award_id = rebel_award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_MELEES:
					rebel_award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_melee_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_melee_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					award_id = rebel_award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					rebel_award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_tournaments
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_tournaments
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					award_id = rebel_award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					rebel_award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_combat_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_combat_missions
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					award_id = rebel_award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_BATTLES:
					rebel_award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_battles
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_battles
								[g_mission_list[mission_list_index]
									 .mission_idx]
							.award_level;
					award_id = rebel_award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				default:
					break;
				}
			}

			if (award_id != 0) {
				frontend_draw_rect_copy(&award_rect, &rect);
				award_rect.left -= MISSION_TEXT_INDENT;
				award_rect.right =
					award_rect.left + MISSION_TEXT_INDENT;
				if (g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				    g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_TRAINING_EXERCISES) {
					sprintf(g_frontend_scratch_buffer,
						"medlvl%d", award_id);
					front_image_draw_sprite(
						g_frontend_scratch_buffer,
						award_rect.left,
						award_rect.top);
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (rebel_award_id == 0) {
							if (imperial_award_id !=
							    0) {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s",
									frontend_string_get(
										FRONTSTR_759_IMPERIAL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
												     imperial_award_id)));
							} else {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s, %s: %s",
									frontend_string_get(
										FRONTSTR_760_REBEL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
												     rebel_award_id)),
									frontend_string_get(
										FRONTSTR_759_IMPERIAL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
												     imperial_award_id)));
							}
						} else if (imperial_award_id !=
							   0) {
							sprintf(g_frontend_scratch_buffer,
								"%s: %s, %s: %s",
								frontend_string_get(
									FRONTSTR_760_REBEL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											    rebel_award_id)),
								frontend_string_get(
									FRONTSTR_759_IMPERIAL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											    imperial_award_id)));
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%s: %s",
								frontend_string_get(
									FRONTSTR_760_REBEL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											    rebel_award_id)));
						}
					} else {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get((
							       frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
										   award_id)));
					}
				} else {
					sprintf(g_frontend_scratch_buffer,
						award_faction_id != 0
							? "citlvl%d"
							: "rcitlvl%d",
						award_id);
					front_image_draw_sprite(
						g_frontend_scratch_buffer,
						award_rect.left,
						award_rect.top);
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (rebel_award_id != 0) {
							if (imperial_award_id ==
							    0) {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s",
									frontend_string_get(
										FRONTSTR_760_REBEL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
												     rebel_award_id)));
							} else {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s, %s: %s",
									frontend_string_get(
										FRONTSTR_760_REBEL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
												     rebel_award_id)),
									frontend_string_get(
										FRONTSTR_759_IMPERIAL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
												     imperial_award_id)));
							}
						} else if (imperial_award_id !=
							   0) {
							sprintf(g_frontend_scratch_buffer,
								"%s: %s",
								frontend_string_get(
									FRONTSTR_759_IMPERIAL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
											    imperial_award_id)));
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%s: %s, %s: %s",
								frontend_string_get(
									FRONTSTR_760_REBEL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
											    rebel_award_id)),
								frontend_string_get(
									FRONTSTR_759_IMPERIAL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
											    imperial_award_id)));
						}
					} else {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get((
							       frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
										   award_id)));
					}
				}
				frontend_button_draw_sprite_and_tooltip(
					&award_rect, NULL,
					g_frontend_scratch_buffer,
					TEXT_FONT_SIZE, 0xFFFF);
			}
			frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
		}
		++display_row;
		if (display_row - g_mission_setup_mission_list_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}
	return 0;
}

/* Starts the tournament, battle or campaign selected on the mission setup
 * screen at its first mission. Opens the sequence file of the g_mission_list
 * entry matching the selected description id; its first line is the mission
 * count, stored in melee_tournament_sequence_state.mission_count for a tournament
 * and campaign_sequence_state.mission_count for a campaign. For a battle it sets
 * battle_sequence_state.victories_needed to g_game_config.battle_length_index + 2,
 * seeds rand with GetTickCount, and stores the first mission's ordinal in
 * mission_ordinals[0]: rand() % count when g_game_config.random_setup is nonzero,
 * else 0. The mission file is named on line ordinal + 1 after the count line.
 * It then sets mission_sequence_active to 1, moves the mission type to the one
 * the sequence plays (tournament to melee, battle to combat engagement,
 * campaign to training), saves that type's selected mission in
 * saved_mission_description_id, loads its list, selects the mission whose file
 * name matches the line lowercased (for a combat engagement also storing its id
 * in battle_sequence_state.current_mission_id), and stores its list index in
 * battle_sequence_state.mission_list_indices[0] whatever the type. Returns 1.
 * Returns 0 when the file does not open or is empty, or the mission's line is
 * empty; by then the count, and for a battle the victories and first ordinal,
 * may already be stored. Does not check a count of 0, which divides by zero
 * with random setup, or a selected mission missing from the list. */
// FUNCTION: XVT 0x4E79A0
int mission_setup_select_first_sequence_mission(void)
{
	enum {
		SEQUENCE_DESCRIPTOR_PATH_CAPACITY = 128,
		SEQUENCE_DESCRIPTOR_LINE_CAPACITY = 255,
		FIRST_SEQUENCE_MISSION_INDEX = 0,
	};

	unsigned int descriptor_mission_index = 0;
	if (g_mission_count > descriptor_mission_index) {
		while (1) {
			if (g_mission_list[descriptor_mission_index]
				    .mission_idx ==
			    g_pilot_data.mission_description_ids
				    [g_pilot_data.mission_directory_id]) {
				break;
			}
			++descriptor_mission_index;
			if (g_mission_count <= descriptor_mission_index) {
				break;
			}
		}
	}
	char descriptor_path[SEQUENCE_DESCRIPTOR_PATH_CAPACITY];
	sprintf(descriptor_path, "%s\\%s",
		g_mission_directory_names[g_pilot_data.mission_directory_id],
		g_mission_list[descriptor_mission_index].file_name);
	xvt_file *stream = file_open(descriptor_path, "r");
	if (stream == NULL) {
		return 0;
	}
	if (FILE_GETS(g_frontend_scratch_buffer,
		      SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream) == NULL) {
		file_close(stream);
		return 0;
	}

	int mission_count = atoi(g_frontend_scratch_buffer);
	int first_mission_ordinal;
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_TOURNAMENTS) {
		first_mission_ordinal = 0;
		g_pilot_data.melee_tournament_sequence_state.mission_count =
			mission_count;
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		first_mission_ordinal = 0;
		g_pilot_data.campaign_sequence_state.mission_count =
			mission_count;
	} else {
		srand(GetTickCount());
		g_pilot_data.battle_sequence_state.victories_needed =
			g_game_config.battle_length_index + 2;
		first_mission_ordinal = 0;
		if (0 != g_game_config.random_setup) {
			first_mission_ordinal = rand() % mission_count;
		}
		g_pilot_data.battle_sequence_state
			.mission_ordinals[FIRST_SEQUENCE_MISSION_INDEX] =
			first_mission_ordinal;
	}

	int lines_to_read = first_mission_ordinal + 1;
	do {
		FILE_GETS(g_frontend_scratch_buffer,
			  SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream);
		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		--lines_to_read;
	} while (lines_to_read != 0);
	file_close(stream);
	if (g_frontend_scratch_buffer[0] == '\0') {
		return 0;
	}

	g_pilot_data.mission_sequence_active = 1;
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_CAMPAIGNS) {
		g_pilot_data.mission_directory_id =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
	} else {
		--g_pilot_data.mission_directory_id;
	}
	g_pilot_data.saved_mission_description_id =
		g_pilot_data.mission_description_ids
			[g_pilot_data.mission_directory_id];
	/* descriptor_path is reused here for the first mission's file name,
	 * lowercased to match the mission list. */
	strcpy(descriptor_path, g_frontend_scratch_buffer);
	for (unsigned int character_index = 0;
	     character_index < strlen(descriptor_path); ++character_index) {
		descriptor_path[character_index] = (char)tolower(
			(unsigned char)descriptor_path[character_index]);
	}

	mission_setup_load_mission_list(g_pilot_data.mission_directory_id);
	unsigned int mission_list_index = 0;
	if (g_mission_count > mission_list_index) {
		while (1) {
			if (strcmp(descriptor_path,
				   g_mission_list[mission_list_index]
					   .file_name) == 0) {
				g_pilot_data.mission_description_ids
					[g_pilot_data.mission_directory_id] =
					g_mission_list[mission_list_index]
						.mission_idx;
				if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					g_pilot_data.battle_sequence_state
						.current_mission_id =
						g_mission_list
							[mission_list_index]
								.mission_idx;
				}
				break;
			}
			++mission_list_index;
			if (g_mission_count <= mission_list_index) {
				break;
			}
		}
	}

	if (g_mission_list != NULL) {
		g_selected_mission_list_index = 0;
		while ((unsigned int)g_selected_mission_list_index <
		       g_mission_count) {
			if (g_mission_list[g_selected_mission_list_index]
				    .mission_idx ==
			    g_pilot_data.mission_description_ids
				    [g_pilot_data.mission_directory_id]) {
				g_pilot_data.battle_sequence_state
					.mission_list_indices
						[FIRST_SEQUENCE_MISSION_INDEX] =
					g_selected_mission_list_index;
				break;
			}
			++g_selected_mission_list_index;
		}
	}
	return 1;
}

/* Draws the Mission Settings panel of the mission setup screen in two columns
 * of 6 rows. The host or a solo player clicks a value to change it, a right
 * click stepping the time limits back; other players see each value in yellow.
 * The rows: for a battle or campaign with a saved continuation, its status,
 * Restart or Continue, which toggles g_game_config.continue_battle_or_campaign and
 * on Continue takes back the saved length and setup choice (a client sees
 * g_remote_battle_sequence_continuation_choice while
 * g_remote_battle_continuation_active is set); for the host outside a solo game,
 * whether joining needs a password; outside a solo game, combat balance for a
 * combat engagement or battle, else difficulty (Easy to Hard, and for a
 * campaign also the easy cheat level); outside a solo game the mission time
 * limit (none, default, 1 to 20 minutes) and the last team's time limit (none,
 * 1 to 10 minutes); for a battle its length (2 to 4 wins); but for a campaign
 * the random setup (off or on, and for a battle also the player's choice);
 * outside a solo game AI opponents for a melee or tournament, craft selection
 * but for a campaign (off, on, host only; host only becomes off outside a melee
 * or tournament, for every player, each frame), and locate players; and for
 * everyone craft waves (none, default, unlimited; unlimited is skipped outside
 * a solo game for a combat engagement or battle) and starfighter collisions. A
 * battle's length and random setup stay fixed while its saved continuation is
 * active and Continue is chosen; the random setup row also tests a campaign's
 * continuation, but campaigns never show that row. When a value changed outside
 * a solo game it sends the 19-word GAME_OPTIONS packet to every player, laid
 * out as mission_setup_send_lobby_state lays it out. Returns 1. */
// FUNCTION: XVT 0x4E7CE0
int mission_setup_draw_game_settings(void)
{
	enum {
		SETTINGS_LEFT_X = 88,
		SETTINGS_RIGHT_X = 262,
		SETTINGS_TOP_Y = 330,
		SETTINGS_TITLE_Y = 311,
		SETTINGS_COLUMN_WIDTH = 165,
		SETTINGS_ROW_HEIGHT = 14,
		SETTINGS_VALUE_GAP = 2,
		SETTINGS_ROWS_PER_COLUMN = 6,
		SETTINGS_FONT_SIZE = 10,
		SETTINGS_TITLE_FONT_SIZE = 15,
		SETTINGS_TEXT_COLOR = 0xFFFF,
		HOVER_DIFFICULTY_OR_BALANCE = 20,
		HOVER_JOINING_GAME = 21,
		HOVER_MISSION_TIME_LIMIT = 22,
		HOVER_LAST_TEAM_TIME_LIMIT = 23,
		HOVER_RANDOM_SETUP = 24,
		HOVER_AI_OPPONENTS = 25,
		HOVER_BATTLE_LENGTH = 26,
		HOVER_CRAFT_SELECTION = 27,
		HOVER_LOCATE_PLAYERS = 28,
		HOVER_CRAFT_WAVES = 29,
		HOVER_COLLISIONS = 31,
		HOVER_SEQUENCE_STATUS = 33,
		MAX_MISSION_TIME_MINUTES = 20,
		MAX_LAST_TEAM_TIME_MINUTES = 10,
		GAME_OPTIONS_PACKET_WORD_COUNT = 19,
	};

	frontend_text_draw(SETTINGS_TITLE_FONT_SIZE,
			   frontend_string_get(FRONTSTR_658_MISSION_SETTINGS),
			   SETTINGS_LEFT_X, SETTINGS_TITLE_Y,
			   SETTINGS_TEXT_COLOR);
	int x = SETTINGS_LEFT_X;
	int y = SETTINGS_TOP_Y;
	int row = 0;
	int settings_changed = 0;
	int can_edit = net_is_host() != 0 ||
		       g_frontend_mission_session_mode ==
			       FRONTEND_MISSION_SESSION_SINGLEPLAYER;

	int text_width;
	struct RECT rect;
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_BATTLES ||
	    g_pilot_data.mission_directory_id == MISSION_DIRECTORY_CAMPAIGNS) {
		if (can_edit) {
			int continuation_active = 0;
			const char *label;
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_BATTLES) {
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilot_data
						    .sp_battle_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
						    .is_active == 1) {
						continuation_active = 1;
					}
				} else {
					if (g_pilot_data
						    .mp_battle_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
						    .is_active == 1) {
						continuation_active = 1;
					}
				}
				label = frontend_string_get(
					FRONTSTR_773_BATTLE_STATUS);
			} else {
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_pilot_data
						    .sp_campaign_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
						    .is_active == 1) {
						continuation_active = 1;
					}
				} else if (net_is_host() != 0) {
					if (g_pilot_data
						    .mp_campaign_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
						    .is_active == 1) {
						continuation_active = 1;
					}
				} else {
					if (g_pilot_data
						    .mp_campaign_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx +
							     12]
						    .is_active == 1) {
						continuation_active = 1;
					}
				}
				label = frontend_string_get(
					FRONTSTR_778_CAMPAIGN_STATUS);
			}
			strcpy(g_frontend_scratch_buffer, label);
			if (continuation_active) {
				frontend_text_draw(SETTINGS_FONT_SIZE,
						   g_frontend_scratch_buffer, x,
						   y, SETTINGS_TEXT_COLOR);
				text_width = frontend_text_measure_width(
					g_frontend_scratch_buffer,
					SETTINGS_FONT_SIZE);
				frontend_draw_rect_assign(
					&rect,
					x + text_width + SETTINGS_VALUE_GAP, y,
					x + SETTINGS_COLUMN_WIDTH,
					y + SETTINGS_ROW_HEIGHT - 1);
				if (frontend_button_handle_text_button(
					    &rect,
					    frontend_string_get((
						    frontend_string_id)((uint8_t)g_game_config
										.continue_battle_or_campaign +
									FRONTSTR_774_RESTART)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_SEQUENCE_STATUS,
					    "settingsound") != 0) {
					settings_changed = 1;
					g_game_config
						.continue_battle_or_campaign ^=
						1;
					if (g_game_config
						    .continue_battle_or_campaign !=
					    SEQUENCE_RESTART) {
						if (g_pilot_data
							    .mission_directory_id ==
						    MISSION_DIRECTORY_BATTLES) {
							if (g_frontend_mission_session_mode ==
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								g_game_config
									.battle_length_index =
									(battle_length)g_pilot_data
										.sp_battle_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.battle_length_index;
								g_game_config
									.random_setup =
									(uint8_t)g_pilot_data
										.sp_battle_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.random_setup;
							} else {
								g_game_config
									.battle_length_index =
									(battle_length)g_pilot_data
										.mp_battle_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.battle_length_index;
								g_game_config
									.random_setup =
									(uint8_t)g_pilot_data
										.mp_battle_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.random_setup;
							}
						} else if (
							g_pilot_data
								.mission_directory_id ==
							MISSION_DIRECTORY_CAMPAIGNS) {
							if (g_frontend_mission_session_mode ==
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								g_game_config
									.random_setup =
									(uint8_t)g_pilot_data
										.sp_campaign_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.random_setup;
							} else if (
								net_is_host() !=
								0) {
								g_game_config
									.random_setup =
									(uint8_t)g_pilot_data
										.mp_campaign_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx]
										.random_setup;
							} else {
								g_game_config
									.random_setup =
									(uint8_t)g_pilot_data
										.mp_campaign_continuations
											[g_mission_list[g_selected_mission_list_index]
												 .mission_idx +
											 12]
										.random_setup;
							}
						}
					}
				}
				++row;
				y += SETTINGS_ROW_HEIGHT;
			}
		} else if (g_remote_battle_continuation_active != 0) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_BATTLES) {
				strcpy(g_frontend_scratch_buffer,
				       frontend_string_get(
					       FRONTSTR_773_BATTLE_STATUS));
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_CAMPAIGNS) {
				strcpy(g_frontend_scratch_buffer,
				       frontend_string_get(
					       FRONTSTR_778_CAMPAIGN_STATUS));
			}
			frontend_text_draw(SETTINGS_FONT_SIZE,
					   g_frontend_scratch_buffer, x, y,
					   SETTINGS_TEXT_COLOR);
			text_width = frontend_text_measure_width(
				g_frontend_scratch_buffer, SETTINGS_FONT_SIZE);
			frontend_draw_rect_assign(
				&rect, x + text_width + SETTINGS_VALUE_GAP, y,
				x + SETTINGS_COLUMN_WIDTH,
				y + SETTINGS_ROW_HEIGHT - 1);
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(g_remote_battle_sequence_continuation_choice +
							    FRONTSTR_774_RESTART)),
				&rect, 0, 1, g_color_yellow);
			++row;
			y += SETTINGS_ROW_HEIGHT;
		}
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    can_edit) {
		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_426_JOINING_GAME), x, y,
			SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_426_JOINING_GAME),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (frontend_button_handle_text_button(
			    &rect,
			    frontend_string_get((
				    frontend_string_id)(g_game_config
								.require_password +
							FRONTSTR_439_OPEN)),
			    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
			    HOVER_JOINING_GAME, "settingsound") != 0) {
			settings_changed = 1;
			g_game_config.require_password ^= 1;
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    (g_pilot_data.mission_directory_id ==
		     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
	     g_pilot_data.mission_directory_id == MISSION_DIRECTORY_BATTLES)) {
		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_768_COMBAT_BALANCE), x, y,
			SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_768_COMBAT_BALANCE),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (can_edit) {
			if (frontend_button_handle_text_button(
				    &rect,
				    frontend_string_get((
					    frontend_string_id)((uint8_t)g_game_config
									.combat_balance +
								FRONTSTR_769_AUTOBALANCE)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_DIFFICULTY_OR_BALANCE,
				    "settingsound") != 0) {
				settings_changed = 1;
				if (++g_game_config.combat_balance >
				    COMBAT_BALANCE_FAVOR_REBEL) {
					g_game_config.combat_balance =
						COMBAT_BALANCE_AUTOBALANCE;
				}
			}
		} else {
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE,
				frontend_string_get((
					frontend_string_id)((uint8_t)g_game_config
								    .combat_balance +
							    FRONTSTR_769_AUTOBALANCE)),
				&rect, 0, 1, g_color_yellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	} else {
		frontend_text_draw(SETTINGS_FONT_SIZE,
				   frontend_string_get(FRONTSTR_421_DIFFICULTY),
				   x, y, SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_421_DIFFICULTY),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_CAMPAIGNS) {
			if (can_edit) {
				if (frontend_button_handle_text_button(
					    &rect,
					    frontend_string_get((
						    frontend_string_id)((uint8_t)g_game_config
										.difficulty +
									FRONTSTR_808_EASY)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_DIFFICULTY_OR_BALANCE,
					    "settingsound") != 0) {
					settings_changed = 1;
					if (++g_game_config.difficulty >
					    GAME_DIFFICULTY_EASY_CHEAT) {
						g_game_config.difficulty =
							GAME_DIFFICULTY_EASY;
					}
				}
			} else {
				frontend_text_draw_aligned_in_rect(
					SETTINGS_FONT_SIZE,
					frontend_string_get((
						frontend_string_id)((uint8_t)g_game_config
									    .difficulty +
								    FRONTSTR_808_EASY)),
					&rect, 0, 1, g_color_yellow);
			}
		} else {
			if (can_edit) {
				if (frontend_button_handle_text_button(
					    &rect,
					    frontend_string_get((
						    frontend_string_id)((uint8_t)g_game_config
										.difficulty +
									FRONTSTR_433_EASY)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_DIFFICULTY_OR_BALANCE,
					    "settingsound") != 0) {
					settings_changed = 1;
					if (++g_game_config.difficulty >
					    GAME_DIFFICULTY_HARD) {
						g_game_config.difficulty =
							GAME_DIFFICULTY_EASY;
					}
				}
			} else {
				frontend_text_draw_aligned_in_rect(
					SETTINGS_FONT_SIZE,
					frontend_string_get((
						frontend_string_id)((uint8_t)g_game_config
									    .difficulty +
								    FRONTSTR_433_EASY)),
					&rect, 0, 1, g_color_yellow);
			}
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_431_MISSION_TIME_LIMIT), x,
			y, SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_431_MISSION_TIME_LIMIT),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (g_game_config.mission_time_limit == 0) {
			strcpy(g_frontend_scratch_buffer,
			       frontend_string_get(FRONTSTR_445_NONE));
		} else if (g_game_config.mission_time_limit == UINT8_MAX) {
			strcpy(g_frontend_scratch_buffer,
			       frontend_string_get(FRONTSTR_446_DEFAULT));
		} else {
			sprintf(g_frontend_scratch_buffer, "%d %s",
				g_game_config.mission_time_limit,
				frontend_string_get(FRONTSTR_448_MIN));
		}
		int button_result;
		if (can_edit) {
			button_result = frontend_button_handle_text_button(
				&rect, g_frontend_scratch_buffer,
				SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				HOVER_MISSION_TIME_LIMIT, "settingsound");
			if (button_result == 1) {
				settings_changed = 1;
				if (g_game_config.mission_time_limit == 0) {
					g_game_config.mission_time_limit =
						UINT8_MAX;
				} else if (g_game_config.mission_time_limit ==
					   UINT8_MAX) {
					g_game_config.mission_time_limit = 1;
				} else if (++g_game_config.mission_time_limit >
					   MAX_MISSION_TIME_MINUTES) {
					g_game_config.mission_time_limit = 0;
				}
			}
			if (button_result == 2) {
				settings_changed = 1;
				if (g_game_config.mission_time_limit ==
				    UINT8_MAX) {
					g_game_config.mission_time_limit = 0;
				} else if (g_game_config.mission_time_limit ==
					   0) {
					g_game_config.mission_time_limit =
						MAX_MISSION_TIME_MINUTES;
				} else if (--g_game_config.mission_time_limit ==
					   0) {
					g_game_config.mission_time_limit =
						UINT8_MAX;
				}
			}
		} else {
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE, g_frontend_scratch_buffer,
				&rect, 0, 1, g_color_yellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}

		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_432_LAST_TEAM_TIME_LIMIT),
			x, y, SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_432_LAST_TEAM_TIME_LIMIT),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (g_game_config.last_team_time_limit_minutes == 0) {
			strcpy(g_frontend_scratch_buffer,
			       frontend_string_get(FRONTSTR_445_NONE));
		} else {
			sprintf(g_frontend_scratch_buffer, "%d %s",
				g_game_config.last_team_time_limit_minutes,
				frontend_string_get(FRONTSTR_448_MIN));
		}
		if (can_edit) {
			button_result = frontend_button_handle_text_button(
				&rect, g_frontend_scratch_buffer,
				SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				HOVER_LAST_TEAM_TIME_LIMIT, "settingsound");
			if (button_result == 1) {
				settings_changed = 1;
				if (++g_game_config
					      .last_team_time_limit_minutes >
				    MAX_LAST_TEAM_TIME_MINUTES) {
					g_game_config
						.last_team_time_limit_minutes =
						0;
				}
			}
			if (button_result == 2) {
				settings_changed = 1;
				if (g_game_config
					    .last_team_time_limit_minutes ==
				    0) {
					g_game_config
						.last_team_time_limit_minutes =
						MAX_LAST_TEAM_TIME_MINUTES;
				} else {
					--g_game_config
						  .last_team_time_limit_minutes;
				}
			}
		} else {
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE, g_frontend_scratch_buffer,
				&rect, 0, 1, g_color_yellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	int option_enabled;
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_BATTLES) {
		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_425_BATTLE_LENGTH), x, y,
			SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_425_BATTLE_LENGTH),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		option_enabled = 0;
		if (can_edit) {
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_game_config.continue_battle_or_campaign !=
					    SEQUENCE_CONTINUE ||
				    g_pilot_data.sp_battle_continuations
						    [g_mission_list
							     [g_selected_mission_list_index]
								     .mission_idx]
							    .is_active != 1) {
					option_enabled = 1;
				}
			} else if (
				g_game_config.continue_battle_or_campaign !=
					SEQUENCE_CONTINUE ||
				g_pilot_data.mp_battle_continuations
						[g_mission_list
							 [g_selected_mission_list_index]
								 .mission_idx]
							.is_active != 1) {
				option_enabled = 1;
			}
		}
		if (option_enabled) {
			if (frontend_button_handle_text_button(
				    &rect,
				    frontend_string_get((
					    frontend_string_id)((uint8_t)g_game_config
									.battle_length_index +
								FRONTSTR_436_2_WINS)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_BATTLE_LENGTH, "settingsound") != 0) {
				settings_changed = 1;
				if (++g_game_config.battle_length_index >
				    BATTLE_LENGTH_FOUR_WINS) {
					g_game_config.battle_length_index =
						BATTLE_LENGTH_TWO_WINS;
				}
			}
		} else {
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE,
				frontend_string_get((
					frontend_string_id)((uint8_t)g_game_config
								    .battle_length_index +
							    FRONTSTR_436_2_WINS)),
				&rect, 0, 1, g_color_yellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_pilot_data.mission_directory_id != MISSION_DIRECTORY_CAMPAIGNS) {
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_BATTLES) {
			frontend_text_draw(SETTINGS_FONT_SIZE,
					   frontend_string_get(
						   FRONTSTR_736_RANDOM_MISSION),
					   x, y, SETTINGS_TEXT_COLOR);
			text_width = frontend_text_measure_width(
				frontend_string_get(
					FRONTSTR_736_RANDOM_MISSION),
				SETTINGS_FONT_SIZE);
		} else {
			frontend_text_draw(
				SETTINGS_FONT_SIZE,
				frontend_string_get(FRONTSTR_424_RANDOMIZE), x,
				y, SETTINGS_TEXT_COLOR);
			text_width = frontend_text_measure_width(
				frontend_string_get(FRONTSTR_424_RANDOMIZE),
				SETTINGS_FONT_SIZE);
		}
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		option_enabled = 0;
		if (can_edit) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_BATTLES) {
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_game_config.continue_battle_or_campaign !=
						    SEQUENCE_CONTINUE ||
					    g_pilot_data.sp_battle_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
								    .is_active !=
						    1) {
						option_enabled = 1;
					}
				} else if (
					g_game_config.continue_battle_or_campaign !=
						SEQUENCE_CONTINUE ||
					g_pilot_data.mp_battle_continuations
							[g_mission_list
								 [g_selected_mission_list_index]
									 .mission_idx]
								.is_active !=
						1) {
					option_enabled = 1;
				}
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_CAMPAIGNS) {
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (g_game_config.continue_battle_or_campaign !=
						    SEQUENCE_CONTINUE ||
					    g_pilot_data.sp_campaign_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
								    .is_active !=
						    1) {
						option_enabled = 1;
					}
				} else if (net_is_host() != 0) {
					if (g_game_config.continue_battle_or_campaign !=
						    SEQUENCE_CONTINUE ||
					    g_pilot_data.mp_campaign_continuations
							    [g_mission_list[g_selected_mission_list_index]
								     .mission_idx]
								    .is_active !=
						    1) {
						option_enabled = 1;
					}
				} else if (
					g_game_config.continue_battle_or_campaign !=
						SEQUENCE_CONTINUE ||
					g_pilot_data.mp_campaign_continuations
							[g_mission_list[g_selected_mission_list_index]
								 .mission_idx +
							 12]
								.is_active !=
						1) {
					option_enabled = 1;
				}
			} else {
				option_enabled = 1;
			}
		}
		if (option_enabled) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_BATTLES) {
				if (frontend_button_handle_text_button(
					    &rect,
					    frontend_string_get((
						    frontend_string_id)(g_game_config
										.random_setup +
									FRONTSTR_819_OFF)),
					    SETTINGS_FONT_SIZE,
					    SETTINGS_TEXT_COLOR,
					    HOVER_RANDOM_SETUP,
					    "settingsound") != 0) {
					settings_changed = 1;
					if (++g_game_config.random_setup > 2) {
						g_game_config.random_setup = 0;
					}
				}
			} else if (
				frontend_button_handle_text_button(
					&rect,
					frontend_string_get((
						frontend_string_id)(g_game_config
									    .random_setup +
								    FRONTSTR_236_OFF)),
					SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
					HOVER_RANDOM_SETUP,
					"settingsound") != 0) {
				settings_changed = 1;
				g_game_config.random_setup ^= 1;
			}
		} else {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_BATTLES) {
				frontend_text_draw_aligned_in_rect(
					SETTINGS_FONT_SIZE,
					frontend_string_get((
						frontend_string_id)(g_game_config
									    .random_setup +
								    FRONTSTR_819_OFF)),
					&rect, 0, 1, g_color_yellow);
			} else {
				frontend_text_draw_aligned_in_rect(
					SETTINGS_FONT_SIZE,
					frontend_string_get((
						frontend_string_id)(g_game_config
									    .random_setup +
								    FRONTSTR_236_OFF)),
					&rect, 0, 1, g_color_yellow);
			}
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	     g_pilot_data.mission_directory_id ==
		     MISSION_DIRECTORY_TOURNAMENTS)) {
		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_572_AI_OPPONENTS), x, y,
			SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_572_AI_OPPONENTS),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (can_edit) {
			if (frontend_button_handle_text_button(
				    &rect,
				    frontend_string_get((
					    frontend_string_id)(g_game_config
									.ai_opponents +
								FRONTSTR_236_OFF)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_AI_OPPONENTS, "settingsound") != 0) {
				settings_changed = 1;
				g_game_config.ai_opponents ^= 1;
			}
		} else {
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(g_game_config
								    .ai_opponents +
							    FRONTSTR_236_OFF)),
				&rect, 0, 1, g_color_yellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_pilot_data.mission_directory_id != MISSION_DIRECTORY_CAMPAIGNS) {
		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_428_CRAFT_SELECTION), x, y,
			SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_428_CRAFT_SELECTION),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (can_edit) {
			if (frontend_button_handle_text_button(
				    &rect,
				    frontend_string_get((
					    frontend_string_id)((uint8_t)g_game_config
									.craft_selection +
								FRONTSTR_536_OFF)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_CRAFT_SELECTION,
				    "settingsound") != 0) {
				settings_changed = 1;
				if (++g_game_config.craft_selection >
				    CRAFT_SELECTION_HOST_ONLY) {
					g_game_config.craft_selection =
						CRAFT_SELECTION_OFF;
				}
			}
		} else {
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE,
				frontend_string_get((
					frontend_string_id)((uint8_t)g_game_config
								    .craft_selection +
							    FRONTSTR_536_OFF)),
				&rect, 0, 1, g_color_yellow);
		}
		if (g_game_config.craft_selection ==
			    CRAFT_SELECTION_HOST_ONLY &&
		    g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_MELEES &&
		    g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_TOURNAMENTS) {
			g_game_config.craft_selection = CRAFT_SELECTION_OFF;
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_text_draw(
			SETTINGS_FONT_SIZE,
			frontend_string_get(FRONTSTR_429_LOCATE_PLAYERS), x, y,
			SETTINGS_TEXT_COLOR);
		text_width = frontend_text_measure_width(
			frontend_string_get(FRONTSTR_429_LOCATE_PLAYERS),
			SETTINGS_FONT_SIZE);
		frontend_draw_rect_assign(
			&rect, x + text_width + SETTINGS_VALUE_GAP, y,
			x + SETTINGS_COLUMN_WIDTH, y + SETTINGS_ROW_HEIGHT - 1);
		if (can_edit) {
			if (frontend_button_handle_text_button(
				    &rect,
				    frontend_string_get((
					    frontend_string_id)(g_game_config
									.locate_players +
								FRONTSTR_443_OFF)),
				    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
				    HOVER_LOCATE_PLAYERS,
				    "settingsound") != 0) {
				settings_changed = 1;
				g_game_config.locate_players ^= 1;
			}
		} else {
			frontend_text_draw_aligned_in_rect(
				SETTINGS_FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(g_game_config
								    .locate_players +
							    FRONTSTR_443_OFF)),
				&rect, 0, 1, g_color_yellow);
		}
		++row;
		y += SETTINGS_ROW_HEIGHT;
		if (row == SETTINGS_ROWS_PER_COLUMN) {
			x = SETTINGS_RIGHT_X;
			y = SETTINGS_TOP_Y;
		}
	}

	frontend_text_draw(SETTINGS_FONT_SIZE,
			   frontend_string_get(FRONTSTR_430_CRAFT_WAVES), x, y,
			   SETTINGS_TEXT_COLOR);
	text_width = frontend_text_measure_width(
		frontend_string_get(FRONTSTR_430_CRAFT_WAVES),
		SETTINGS_FONT_SIZE);
	frontend_draw_rect_assign(&rect, x + text_width + SETTINGS_VALUE_GAP, y,
				  x + SETTINGS_COLUMN_WIDTH,
				  y + SETTINGS_ROW_HEIGHT - 1);
	if (can_edit) {
		if (frontend_button_handle_text_button(
			    &rect,
			    frontend_string_get(
				    (frontend_string_id)((uint8_t)g_game_config
								 .craft_waves +
							 FRONTSTR_445_NONE)),
			    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
			    HOVER_CRAFT_WAVES, "settingsound") != 0) {
			settings_changed = 1;
			if (++g_game_config.craft_waves >
			    CRAFT_WAVES_UNLIMITED) {
				g_game_config.craft_waves = CRAFT_WAVES_NONE;
			}
			if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
			    (g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_BATTLES) &&
			    g_game_config.craft_waves ==
				    CRAFT_WAVES_UNLIMITED) {
				g_game_config.craft_waves = CRAFT_WAVES_NONE;
			}
		}
	} else {
		frontend_text_draw_aligned_in_rect(
			SETTINGS_FONT_SIZE,
			frontend_string_get(
				(frontend_string_id)((uint8_t)g_game_config
							     .craft_waves +
						     FRONTSTR_445_NONE)),
			&rect, 0, 1, g_color_yellow);
	}
	++row;
	y += SETTINGS_ROW_HEIGHT;
	if (row == SETTINGS_ROWS_PER_COLUMN) {
		x = SETTINGS_RIGHT_X;
		y = SETTINGS_TOP_Y;
	}

	frontend_text_draw(
		SETTINGS_FONT_SIZE,
		frontend_string_get(FRONTSTR_422_STARFIGHTER_COLLISIONS), x, y,
		SETTINGS_TEXT_COLOR);
	text_width = frontend_text_measure_width(
		frontend_string_get(FRONTSTR_422_STARFIGHTER_COLLISIONS),
		SETTINGS_FONT_SIZE);
	frontend_draw_rect_assign(&rect, x + text_width + SETTINGS_VALUE_GAP, y,
				  x + SETTINGS_COLUMN_WIDTH,
				  y + SETTINGS_ROW_HEIGHT - 1);
	if (can_edit) {
		if (frontend_button_handle_text_button(
			    &rect,
			    frontend_string_get(
				    (frontend_string_id)(g_game_config
								 .collisions +
							 FRONTSTR_236_OFF)),
			    SETTINGS_FONT_SIZE, SETTINGS_TEXT_COLOR,
			    HOVER_COLLISIONS, "settingsound") != 0) {
			settings_changed = 1;
			g_game_config.collisions ^= 1;
		}
	} else {
		frontend_text_draw_aligned_in_rect(
			SETTINGS_FONT_SIZE,
			frontend_string_get(
				(frontend_string_id)(g_game_config.collisions +
						     FRONTSTR_236_OFF)),
			&rect, 0, 1, g_color_yellow);
	}

	++row;
	y += SETTINGS_ROW_HEIGHT;
	if (row == SETTINGS_ROWS_PER_COLUMN) {
		x = SETTINGS_RIGHT_X;
		y = SETTINGS_TOP_Y;
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    settings_changed == 1) {
		int *packet_words = &g_frontend_net_packet_scratch.packet_type;
		packet_words[0] = NET_PACKET_GAME_OPTIONS;
		packet_words[1] = (uint8_t)g_game_config.difficulty;
		packet_words[2] = g_game_config.collisions;
		packet_words[3] = g_game_config.craft_jumping;
		packet_words[4] = g_game_config.random_setup;
		packet_words[5] = (uint8_t)g_game_config.battle_length_index;
		packet_words[6] = g_game_config.require_password;
		packet_words[7] = g_game_config.in_progress_join;
		packet_words[8] = (uint8_t)g_game_config.craft_selection;
		packet_words[9] = g_game_config.locate_players;
		packet_words[10] = (uint8_t)g_game_config.craft_waves;
		packet_words[11] = g_game_config.mission_time_limit;
		packet_words[12] = g_game_config.last_team_time_limit_minutes;
		packet_words[13] = rand();
		packet_words[14] = g_game_config.internet_play;
		packet_words[15] = g_game_config.ai_opponents;
		packet_words[16] = g_game_config.server_update_rate;
		packet_words[17] = (uint8_t)g_game_config.combat_balance;
		packet_words[18] =
			(uint8_t)g_game_config.continue_battle_or_campaign;
		net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
					  GAME_OPTIONS_PACKET_WORD_COUNT *
						  sizeof(packet_words[0]));
	}
	return 1;
}

/* Counts the entries of a mission list file from the stream's position, as
 * mission_setup_load_mission_list reads them: lines starting with "//" and
 * "[section]" lines are skipped, and each entry is three lines; an entry the
 * file ends inside is not counted. Returns the count and leaves the stream at
 * its end. The section name it copies into a local is never used. */
// FUNCTION: XVT 0x4E9230
int mission_setup_count_mission_list_entries(xvt_file *stream)
{
	int entry_count = 0;
	char section_name[128];
	while (1) {
		do {
			if (FILE_GETS(g_frontend_scratch_buffer,
				      sizeof(g_frontend_scratch_buffer),
				      stream) == NULL) {
				return entry_count;
			}
		} while (g_frontend_scratch_buffer[0] == '/' &&
			 g_frontend_scratch_buffer[1] == '/');
		if (g_frontend_scratch_buffer[0] == '[') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
#ifdef XVT_MODERN
			strncpy(section_name, &g_frontend_scratch_buffer[1],
				sizeof(section_name) - 1);
			section_name[sizeof(section_name) - 1] = '\0';
#else
			strcpy(section_name, &g_frontend_scratch_buffer[1]);
#endif
			continue;
		}
		if (FILE_GETS(g_frontend_scratch_buffer,
			      sizeof(g_frontend_scratch_buffer),
			      stream) == NULL) {
			return entry_count;
		}
		if (FILE_GETS(g_frontend_scratch_buffer,
			      sizeof(g_frontend_scratch_buffer),
			      stream) == NULL) {
			return entry_count;
		}
		++entry_count;
	}
}

/* Loads the background for the current mission type into the "background" image
 * and draws the mission setup screen's base into the offscreen surface:
 * background, frame, "allactive" while g_host_cd_available is set, else
 * "clientactive", the chat box outside a solo game, and the overlay. In a solo
 * game the background is chosen by mission type and, for training, combat
 * engagements, battles and campaigns, by the pilot's faction (0 for Rebel);
 * outside a solo game a training mission's comes from the IFF of its first
 * flight group with a player (0 for Rebel), a campaign's from
 * mission_setup_use_rebel_background, and the rest by type alone. Tournaments, and
 * any type id not listed, take gametrn. Returns 1. Does not check that a
 * network training mission has a flight group with a player. */
// FUNCTION: XVT 0x4E9330
int mission_setup_draw_background(void)
{
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			if (g_pilot_data.current_faction_id == 0) {
				front_image_register_resource_default(
					"frontres\\gametr.bmp", "background");
			} else {
				front_image_register_resource_default(
					"frontres\\gameti.bmp", "background");
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			if (g_pilot_data.current_faction_id == 0) {
				front_image_register_resource_default(
					"frontres\\gamecr.bmp", "background");
			} else {
				front_image_register_resource_default(
					"frontres\\gameci.bmp", "background");
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_BATTLES) {
			if (g_pilot_data.current_faction_id == 0) {
				front_image_register_resource_default(
					"frontres\\gamebr.bmp", "background");
			} else {
				front_image_register_resource_default(
					"frontres\\gamebi.bmp", "background");
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_MELEES) {
			front_image_register_resource_default(
				"frontres\\gamemn.bmp", "background");
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_CAMPAIGNS) {
			if (g_pilot_data.current_faction_id == 0) {
				front_image_register_resource_default(
					"frontres\\gamecar.bmp", "background");
			} else {
				front_image_register_resource_default(
					"frontres\\gamecai.bmp", "background");
			}
		} else {
			front_image_register_resource_default(
				"frontres\\gametrn.bmp", "background");
		}
	} else {
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			int flight_group_index = 0;
			if (*(int16_t *)&g_frontend_mission.flight_group_count >
			    flight_group_index) {
				uint8_t *player_number_ptr =
					&g_frontend_mission.flight_groups[0]
						 .player_number;
				do {
					if (*player_number_ptr != 0) {
						break;
					}
					player_number_ptr +=
						sizeof(struct xvt_flight_group);
					++flight_group_index;
				} while (*(int16_t *)&g_frontend_mission
						  .flight_group_count >
					 flight_group_index);
			}
			if (g_frontend_mission.flight_groups[flight_group_index]
				    .iff == 0) {
				front_image_register_resource_default(
					"frontres\\gametr.bmp", "background");
			} else {
				front_image_register_resource_default(
					"frontres\\gameti.bmp", "background");
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			front_image_register_resource_default(
				"frontres\\gamecn.bmp", "background");
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_BATTLES) {
			front_image_register_resource_default(
				"frontres\\gamebn.bmp", "background");
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_MELEES) {
			front_image_register_resource_default(
				"frontres\\gamemn.bmp", "background");
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_CAMPAIGNS) {
			if (mission_setup_use_rebel_background() != 0) {
				front_image_register_resource_default(
					"frontres\\gamecar.bmp", "background");
			} else {
				front_image_register_resource_default(
					"frontres\\gamecai.bmp", "background");
			}
		} else {
			front_image_register_resource_default(
				"frontres\\gametrn.bmp", "background");
		}
	}

	frontend_display_lock_offscreen_surface();
	front_image_draw_sprite_opaque("background", 0, 0);
	front_image_draw_sprite("frame", 0, 0);
	if (g_host_cd_available != 0) {
		front_image_draw_sprite("allactive", 0, 0);
	} else {
		front_image_draw_sprite("clientactive", 0, 0);
	}
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		front_image_draw_sprite_translucent("chatbox", 0, 0);
	}
	front_image_draw_sprite_translucent("gameoverlay", 0, 0);
	frontend_display_unlock_offscreen_surface(1);
	return 1;
}

/* Tells which background a network campaign gets: opens the selected campaign's
 * sequence file, takes its second line as the first mission's file name in the
 * training directory, loads that mission into a local copy, and returns 1 when
 * the IFF of its first flight group with a player is 0 (Rebel), else 0. Returns
 * 1 when the campaign file does not open or has no second line. Does not check
 * that the mission file opened: when it did not, the result comes from an unset
 * local copy. */
// FUNCTION: XVT 0x4E9560
int mission_setup_use_rebel_background(void)
{
	char file_path[256];

	sprintf(file_path, "%s\\%s",
		g_mission_directory_names[MISSION_DIRECTORY_CAMPAIGNS],
		g_mission_list[g_selected_mission_list_index].file_name);
	xvt_file *stream = file_open(file_path, "r");
	if (stream != NULL) {
		FILE_GETS(g_frontend_scratch_buffer,
			  sizeof(g_frontend_scratch_buffer), stream);
		char *read_result =
			FILE_GETS(g_frontend_scratch_buffer,
				  sizeof(g_frontend_scratch_buffer), stream);
		file_close(stream);
		if (read_result != NULL) {
			if (g_frontend_scratch_buffer
				    [strlen(g_frontend_scratch_buffer) - 1] ==
			    '\n') {
				g_frontend_scratch_buffer
					[strlen(g_frontend_scratch_buffer) -
					 1] = '\0';
			}
			sprintf(file_path, "%s\\%s",
				g_mission_directory_names
					[MISSION_DIRECTORY_TRAINING_EXERCISES],
				g_frontend_scratch_buffer);
			struct frontend_mission mission;
			frontend_mission_load_file(file_path, &mission);
			{
				int flight_group_index = 0;
				if ((int16_t)mission.flight_group_count > 0) {
					uint8_t *player_number_ptr =
						&mission.flight_groups[0]
							 .player_number;
					do {
						if (*player_number_ptr != 0) {
							break;
						}
						player_number_ptr += sizeof(
							struct
							xvt_flight_group);
						++flight_group_index;
					} while ((int16_t)mission
							 .flight_group_count >
						 flight_group_index);
				}

				return mission.flight_groups[flight_group_index]
					       .iff == 0;
			}
		}
	}

	return 1;
}

/* Draws the local player's craft on the mission briefing's craft screen; called
 * by mission_briefing.c. First the model preview of the chosen craft, in a
 * larger box in a solo game, turned to fixed angles and placed at
 * g_model_preview_craft_positions for its type; in a melee or tournament it shows
 * the flight group's markings, plus 1 (past 3 back to 0) for craft other than
 * types 1 to 4, 14 and 16. Then the craft type, craft count and wave count on
 * the left (a wave count of none shows 1, unlimited shows its name, otherwise
 * g_mission_setup_selected_wave_count_minus_one + 1) and the warhead, beam weapon and
 * countermeasure on the right. Once the local player is marked ready in
 * g_mp_roster_ready_flags outside a solo game, everything is gray. Otherwise the
 * craft values are drawn in color code 1 when there is more than one craft
 * choice and craft selection is allowed (in a training sequence only at the
 * easy cheat difficulty, outside a solo game as g_game_config.craft_selection
 * says), else in code 4; the warhead and countermeasure values in code 1 when
 * there is more than one choice and the loadout is not locked, which happens
 * only in a training sequence below the easy cheat difficulty, else in code 4;
 * and the beam value in yellow when there is one choice or the loadout is
 * locked. The beam line is left out for craft types 1 to 5 and 14. Does not
 * check that the local player has a g_mp_roster entry: without one it reads
 * g_mp_roster_ready_flags[8], past the array's end. */
// FUNCTION: XVT 0x4EC0B0
void mission_setup_draw_craft_loadout(void)
{
	enum {
		FONT_SIZE = 12,
		ROW_HEIGHT = 15,
		LABEL_LEFT = 88,
		LABEL_TOP = 111,
		LABEL_RIGHT = 234,
		LABEL_BOTTOM = 125,
		VALUE_LEFT = 275,
		VALUE_RIGHT = 431,
		COLOR_ESCAPE_SELECTABLE = 1,
		COLOR_ESCAPE_FIXED = 4,
		CRAFT_STRING_BASE = 21,
		WARHEAD_STRING_BASE = 273,
		BEAM_STRING_BASE = 283,
		COUNTERMEASURE_STRING_BASE = 288,
	};

	struct RECT rect;

	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 84, 107, 430, 407);
	} else {
		frontend_draw_rect_assign(&rect, 144, 107, 370, 333);
	}
	model_preview_set_object_euler_degrees(110.0f, -135.0f, 20.0f);
	int craft_type = mission_setup_get_craft_type(-1);
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	    g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TOURNAMENTS) {
		int markings;

		switch (craft_type) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 14:
		case 16:
			markings =
				g_frontend_mission
					.flight_groups
						[g_mission_setup_selected_flight_group_index]
					.markings;
			break;
		default:
			markings =
				g_frontend_mission
					.flight_groups
						[g_mission_setup_selected_flight_group_index]
					.markings +
				1;
			if (markings > 3) {
				markings = 0;
			}
			break;
		}
		model_preview_set_node_switch_index(markings);
	}
	model_preview_set_object_world_position(
		g_model_preview_craft_positions[craft_type].x,
		g_model_preview_craft_positions[craft_type].y,
		g_model_preview_craft_positions[craft_type].z);
	model_preview_render_viewport(rect.left, rect.top,
				      rect.right - rect.left + 1,
				      rect.bottom - rect.top + 1, 0);

	int roster_index;
	for (roster_index = 0;
	     roster_index < (int)(sizeof(g_mp_roster) / sizeof(g_mp_roster[0]));
	     ++roster_index) {
		if (net_get_local_player_id() ==
		    g_mp_roster[roster_index].player_id) {
			break;
		}
	}

	int draw_beam;
	if (g_mp_roster_ready_flags[roster_index] != 0 &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, LABEL_LEFT, LABEL_TOP,
					  LABEL_RIGHT, LABEL_BOTTOM);
		craft_type = mission_setup_get_craft_type(-1);
		sprintf(g_frontend_scratch_buffer, "%s: %s",
			frontend_string_get(FRONTSTR_605_CRAFT_TYPE),
			frontend_string_get(
				(frontend_string_id)(craft_type +
						     CRAFT_STRING_BASE)));
		frontend_text_draw_aligned_in_rect(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   &rect, 0, 1, g_color_gray);
		frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);

		sprintf(g_frontend_scratch_buffer, "%s %u",
			frontend_string_get(FRONTSTR_573_OF_CRAFT),
			g_mission_setup_selected_craft_count);
		frontend_text_draw_aligned_in_rect(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   &rect, 0, 1, g_color_gray);
		frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);

		if (g_game_config.craft_waves == CRAFT_WAVES_UNLIMITED) {
			sprintf(g_frontend_scratch_buffer, "%s %s",
				frontend_string_get(FRONTSTR_267_OF_WAVES),
				frontend_string_get(FRONTSTR_447_UNLIMITED));
		} else if (g_game_config.craft_waves == CRAFT_WAVES_NONE) {
			sprintf(g_frontend_scratch_buffer, "%s 1",
				frontend_string_get(FRONTSTR_267_OF_WAVES));
		} else {
			sprintf(g_frontend_scratch_buffer, "%s %u",
				frontend_string_get(FRONTSTR_267_OF_WAVES),
				g_mission_setup_selected_wave_count_minus_one +
					1);
		}
		frontend_text_draw_aligned_in_rect(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   &rect, 0, 1, g_color_gray);

		frontend_draw_rect_assign(&rect, 270, LABEL_TOP, 426,
					  LABEL_BOTTOM);
		frontend_draw_rect_assign(&rect, VALUE_LEFT, LABEL_TOP,
					  VALUE_RIGHT, LABEL_BOTTOM);
		frontend_string_id value_string_id;
		if (g_mission_setup_warhead_option_count != 0) {
			value_string_id =
				(frontend_string_id)(mission_setup_get_warhead_type(
							     -1) +
						     WARHEAD_STRING_BASE);
		} else {
			value_string_id = FRONTSTR_273_NONE;
		}
		const char *value_text = frontend_string_get(value_string_id);
		sprintf(g_frontend_scratch_buffer, "%s %s",
			frontend_string_get(FRONTSTR_264_WARHEADS), value_text);
		rect.left = rect.right -
			    frontend_text_measure_width(
				    g_frontend_scratch_buffer, FONT_SIZE);
		frontend_text_draw_aligned_in_rect(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   &rect, 0, 1, g_color_gray);
		frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);

		draw_beam = 1;
		if (g_mission_setup_beam_option_count == 0) {
			sprintf(g_frontend_scratch_buffer, "%s %s",
				frontend_string_get(FRONTSTR_266_BEAM_WEAPON),
				frontend_string_get(FRONTSTR_273_NONE));
			craft_type = mission_setup_get_craft_type(-1);
			if (craft_type >= 1 &&
			    (craft_type <= 5 || craft_type == 14)) {
				draw_beam = 0;
			}
		} else {
			craft_type = mission_setup_get_craft_type(-1);
			if (craft_type < 1 ||
			    (craft_type > 5 && craft_type != 14)) {
				value_text = frontend_string_get((
					frontend_string_id)(mission_setup_get_beam_type(
								    -1) +
							    BEAM_STRING_BASE));
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get(
						FRONTSTR_266_BEAM_WEAPON),
					value_text);
			} else {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get(
						FRONTSTR_266_BEAM_WEAPON),
					frontend_string_get(FRONTSTR_273_NONE));
				draw_beam = 0;
			}
		}
		if (draw_beam != 0) {
			rect.left =
				rect.right -
				frontend_text_measure_width(
					g_frontend_scratch_buffer, FONT_SIZE);
			frontend_text_draw_aligned_in_rect(
				FONT_SIZE, g_frontend_scratch_buffer, &rect, 0,
				1, g_color_gray);
			frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
		}

		if (g_mission_setup_countermeasure_option_count != 0) {
			value_string_id =
				(frontend_string_id)(mission_setup_get_countermeasure_type(
							     -1) +
						     COUNTERMEASURE_STRING_BASE);
		} else {
			value_string_id = FRONTSTR_273_NONE;
		}
		value_text = frontend_string_get(value_string_id);
		sprintf(g_frontend_scratch_buffer, "%s %s",
			frontend_string_get(FRONTSTR_265_COUNTERMEASURES),
			value_text);
		rect.left = rect.right -
			    frontend_text_measure_width(
				    g_frontend_scratch_buffer, FONT_SIZE);
		frontend_text_draw_aligned_in_rect(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   &rect, 0, 1, g_color_gray);
		return;
	}

	frontend_draw_rect_assign(&rect, LABEL_LEFT, LABEL_TOP, LABEL_RIGHT,
				  LABEL_BOTTOM);
	craft_type = mission_setup_get_craft_type(-1);
	int craft_selection_allowed;
	int loadout_selection_locked;
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			if (g_game_config.difficulty ==
			    GAME_DIFFICULTY_EASY_CHEAT) {
				loadout_selection_locked = 0;
				craft_selection_allowed = 1;
			} else {
				loadout_selection_locked = 1;
				craft_selection_allowed = 0;
			}
		} else {
			loadout_selection_locked = 0;
			craft_selection_allowed = 1;
		}
	} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_TRAINING_EXERCISES &&
		   g_pilot_data.mission_sequence_active == 1) {
		if (g_game_config.difficulty == GAME_DIFFICULTY_EASY_CHEAT) {
			craft_selection_allowed = 1;
			loadout_selection_locked = 0;
		} else {
			craft_selection_allowed = 0;
			loadout_selection_locked = 1;
		}
	} else {
		craft_selection_allowed = 0;
		loadout_selection_locked = 0;
		switch (g_game_config.craft_selection) {
		case CRAFT_SELECTION_OFF:
			craft_selection_allowed = 0;
			break;
		case CRAFT_SELECTION_ON:
			craft_selection_allowed = 1;
			break;
		case CRAFT_SELECTION_HOST_ONLY:
			if (net_is_host() != 0 ||
			    g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				craft_selection_allowed = 1;
			}
			break;
		default:
			break;
		}
	}

	char value_escape;
	if ((g_mission_setup_flight_group_craft_option_count > 1 ||
	     g_mission_setup_preset_craft_option_count > 1) &&
	    craft_selection_allowed != 0) {
		value_escape = COLOR_ESCAPE_SELECTABLE;
	} else {
		value_escape = COLOR_ESCAPE_FIXED;
	}
	sprintf(g_frontend_scratch_buffer, "%c%s: %c%s", COLOR_ESCAPE_FIXED,
		frontend_string_get(FRONTSTR_605_CRAFT_TYPE), value_escape,
		frontend_string_get(
			(frontend_string_id)(craft_type + CRAFT_STRING_BASE)));
	frontend_text_draw_aligned_in_rect(FONT_SIZE, g_frontend_scratch_buffer,
					   &rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);

	sprintf(g_frontend_scratch_buffer, "%c%s %c%u", COLOR_ESCAPE_FIXED,
		frontend_string_get(FRONTSTR_573_OF_CRAFT), value_escape,
		g_mission_setup_selected_craft_count);
	frontend_text_draw_aligned_in_rect(FONT_SIZE, g_frontend_scratch_buffer,
					   &rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);

	if (g_game_config.craft_waves == CRAFT_WAVES_UNLIMITED) {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_267_OF_WAVES),
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_447_UNLIMITED));
	} else if (g_game_config.craft_waves == CRAFT_WAVES_NONE) {
		sprintf(g_frontend_scratch_buffer, "%c%s %c1",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_267_OF_WAVES),
			COLOR_ESCAPE_FIXED);
	} else {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%u",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_267_OF_WAVES),
			value_escape,
			g_mission_setup_selected_wave_count_minus_one + 1);
	}
	frontend_text_draw_aligned_in_rect(FONT_SIZE, g_frontend_scratch_buffer,
					   &rect, 0, 1, 0xFFFF);

	frontend_draw_rect_assign(&rect, VALUE_LEFT, LABEL_TOP, VALUE_RIGHT,
				  LABEL_BOTTOM);
	if (g_mission_setup_warhead_option_count == 0) {
		sprintf(g_frontend_scratch_buffer, "%c%s %s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_264_WARHEADS),
			frontend_string_get(FRONTSTR_273_NONE));
	} else if (g_mission_setup_warhead_option_count != 1 &&
		   loadout_selection_locked == 0) {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_264_WARHEADS),
			COLOR_ESCAPE_SELECTABLE,
			frontend_string_get((
				frontend_string_id)(mission_setup_get_warhead_type(
							    -1) +
						    WARHEAD_STRING_BASE)));
	} else {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_264_WARHEADS),
			COLOR_ESCAPE_FIXED,
			frontend_string_get((
				frontend_string_id)(mission_setup_get_warhead_type(
							    -1) +
						    WARHEAD_STRING_BASE)));
	}
	rect.left = rect.right - frontend_text_measure_width(
					 g_frontend_scratch_buffer, FONT_SIZE);
	frontend_text_draw_aligned_in_rect(FONT_SIZE, g_frontend_scratch_buffer,
					   &rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);

	draw_beam = 1;
	if (g_mission_setup_beam_option_count <= 0) {
		sprintf(g_frontend_scratch_buffer, "%c%s %s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_266_BEAM_WEAPON),
			frontend_string_get(FRONTSTR_273_NONE));
		craft_type = mission_setup_get_craft_type(-1);
		if (craft_type >= 1 && (craft_type <= 5 || craft_type == 14)) {
			draw_beam = 0;
		}
	} else {
		craft_type = mission_setup_get_craft_type(-1);
		if (craft_type < 1 || (craft_type > 5 && craft_type != 14)) {
			sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
				COLOR_ESCAPE_FIXED,
				frontend_string_get(FRONTSTR_266_BEAM_WEAPON),
				COLOR_ESCAPE_SELECTABLE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_beam_type(
								    -1) +
							    BEAM_STRING_BASE)));
		} else {
			sprintf(g_frontend_scratch_buffer, "%c%s %s",
				COLOR_ESCAPE_FIXED,
				frontend_string_get(FRONTSTR_266_BEAM_WEAPON),
				frontend_string_get(FRONTSTR_273_NONE));
			draw_beam = 0;
		}
	}
	if (draw_beam != 0) {
		rect.left = rect.right -
			    frontend_text_measure_width(
				    g_frontend_scratch_buffer, FONT_SIZE);
		if (g_mission_setup_beam_option_count == 1 ||
		    loadout_selection_locked != 0) {
			frontend_text_draw_aligned_in_rect(
				FONT_SIZE, g_frontend_scratch_buffer, &rect, 0,
				1, g_color_yellow);
		} else {
			frontend_text_draw_aligned_in_rect(
				FONT_SIZE, g_frontend_scratch_buffer, &rect, 0,
				1, 0xFFFF);
		}
		frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
	}

	if (g_mission_setup_countermeasure_option_count == 0) {
		sprintf(g_frontend_scratch_buffer, "%c%s %s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_265_COUNTERMEASURES),
			frontend_string_get(FRONTSTR_273_NONE));
	} else if (g_mission_setup_countermeasure_option_count != 1 &&
		   loadout_selection_locked == 0) {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_265_COUNTERMEASURES),
			COLOR_ESCAPE_SELECTABLE,
			frontend_string_get((
				frontend_string_id)(mission_setup_get_countermeasure_type(
							    -1) +
						    COUNTERMEASURE_STRING_BASE)));
	} else {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
			COLOR_ESCAPE_FIXED,
			frontend_string_get(FRONTSTR_265_COUNTERMEASURES),
			COLOR_ESCAPE_FIXED,
			frontend_string_get((
				frontend_string_id)(mission_setup_get_countermeasure_type(
							    -1) +
						    COUNTERMEASURE_STRING_BASE)));
	}
	rect.left = rect.right - frontend_text_measure_width(
					 g_frontend_scratch_buffer, FONT_SIZE);
	frontend_text_draw_aligned_in_rect(FONT_SIZE, g_frontend_scratch_buffer,
					   &rect, 0, 1, 0xFFFF);
}

/* Draws the table of the players' craft and loadouts on the mission briefing's
 * craft screen; called by mission_briefing.c. Its headings sit at y
 * 341 - (15 * ready_count >> 1), ready_count being net_count_ready_players(). The
 * local player's row comes first, then the others: rating and name, craft,
 * warhead, beam and countermeasure, gray once the player is marked ready in
 * g_mp_roster_ready_flags, else yellow, the local name (every name in a solo game)
 * pulsing until then. For training, combat engagements and battles the other
 * players on the pilot's team (g_pilot_data.team) follow, then the rest with the
 * not on your team text in place of a loadout. For any other mission type every
 * other player follows, with "----" in place of the loadout when the flight
 * group assigned to the player's slot, found among the first g_team_count teams,
 * is -1. */
// FUNCTION: XVT 0x4ECB90
void mission_setup_draw_player_loadouts(int frame_counter)
{
	enum {
		PLAYER_SLOTS_PER_TEAM = 8,
		ROSTER_CAPACITY = sizeof(g_mp_roster) / sizeof(g_mp_roster[0]),
		PLAYER_X = 88,
		CRAFT_X = 228,
		WARHEAD_X = 338,
		BEAM_X = 378,
		COUNTERMEASURE_X = 403,
		ROW_HEIGHT = 15,
		FONT_SIZE = 12,
		PULSE_PERIOD = 24,
		PULSE_PAIR_MASK = ~1
	};

	int row_y = 341 - (ROW_HEIGHT * net_count_ready_players() >> 1);
	frontend_text_draw(FONT_SIZE, frontend_string_get(FRONTSTR_186_PLAYERS),
			   PLAYER_X, row_y, g_color_yellow);
	frontend_text_draw(FONT_SIZE, frontend_string_get(FRONTSTR_576_CRAFT),
			   CRAFT_X, row_y, g_color_yellow);
	frontend_text_draw(FONT_SIZE, frontend_string_get(FRONTSTR_577_WHD),
			   WARHEAD_X, row_y, g_color_yellow);
	frontend_text_draw(FONT_SIZE, frontend_string_get(FRONTSTR_578_BM),
			   BEAM_X, row_y, g_color_yellow);
	frontend_text_draw(FONT_SIZE, frontend_string_get(FRONTSTR_579_CM),
			   COUNTERMEASURE_X, row_y, g_color_yellow);
	row_y += ROW_HEIGHT;
	(void)net_count_ready_players();

	struct RECT row_rect;
	struct RECT old_clip_rect;
	struct RECT other_team_rect;
	int roster_index;
	int team_player_index;
	int player_id;
	uint16_t color;
	if (g_pilot_data.mission_directory_id !=
		    MISSION_DIRECTORY_TRAINING_EXERCISES &&
	    g_pilot_data.mission_directory_id !=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilot_data.mission_directory_id != MISSION_DIRECTORY_BATTLES) {
		for (roster_index = 0; roster_index < ROSTER_CAPACITY;
		     ++roster_index) {
			player_id = g_mp_roster[roster_index].player_id;
			if (player_id == 0 ||
			    net_get_local_player_id() != player_id) {
				continue;
			}

			color = g_mp_roster_ready_flags[roster_index] != 0
					? g_color_gray
					: g_color_yellow;
			frontend_draw_rect_assign(&row_rect, PLAYER_X, row_y,
						  CRAFT_X - 2,
						  row_y + ROW_HEIGHT);
			frontend_display_get_screen_clip_rect(&old_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&row_rect);
			if (g_mp_roster_ready_flags[roster_index] != 0) {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					g_mp_roster[roster_index].name);
				frontend_text_draw(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   PLAYER_X, row_y, color);
			} else {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				frontend_text_draw(
					FONT_SIZE, g_frontend_scratch_buffer,
					PLAYER_X, row_y,
					net_get_local_player_id() ==
								g_mp_roster[roster_index]
									.player_id ||
							g_frontend_mission_session_mode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulse_color_ramp
							  [((frame_counter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			frontend_display_set_screen_clip_rect640x480(
				&old_clip_rect);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_craft_type(
								    roster_index) +
							    21)),
				CRAFT_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_warhead_type(
								    roster_index) +
							    580)),
				WARHEAD_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_beam_type(
								    roster_index) +
							    590)),
				BEAM_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_countermeasure_type(
								    roster_index) +
							    595)),
				COUNTERMEASURE_X, row_y, color);
			row_y += ROW_HEIGHT;
		}

		for (roster_index = 0; roster_index < ROSTER_CAPACITY;
		     ++roster_index) {
			player_id = g_mp_roster[roster_index].player_id;
			if (player_id == 0 ||
			    net_get_local_player_id() == player_id) {
				continue;
			}

			color = g_mp_roster_ready_flags[roster_index] != 0
					? g_color_gray
					: g_color_yellow;
			int assignment_missing = 0;
			for (int team_index = 0; team_index < g_team_count;
			     ++team_index) {
				for (team_player_index = 0;
				     team_player_index <
				     g_team_player_flight_group_count
					     [team_index];
				     ++team_player_index) {
					if (g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [team_player_index] ==
					    player_id) {
						break;
					}
				}
				if (team_player_index <
				    g_team_player_flight_group_count
					    [team_index]) {
					if (g_mission_setup_player_flight_group_indices
						    [team_index *
							     PLAYER_SLOTS_PER_TEAM +
						     team_player_index] == -1) {
						assignment_missing = 1;
					}
					break;
				}
			}

			frontend_draw_rect_assign(&row_rect, PLAYER_X, row_y,
						  CRAFT_X - 2,
						  row_y + ROW_HEIGHT);
			frontend_display_get_screen_clip_rect(&old_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&row_rect);
			if (g_mp_roster_ready_flags[roster_index] != 0) {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					g_mp_roster[roster_index].name);
				frontend_text_draw(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   PLAYER_X, row_y, color);
			} else {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				frontend_text_draw(
					FONT_SIZE, g_frontend_scratch_buffer,
					PLAYER_X, row_y,
					net_get_local_player_id() ==
								player_id ||
							g_frontend_mission_session_mode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulse_color_ramp
							  [((frame_counter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			frontend_display_set_screen_clip_rect640x480(
				&old_clip_rect);
			if (assignment_missing) {
				frontend_text_draw(FONT_SIZE, "----", CRAFT_X,
						   row_y, color);
				frontend_text_draw(FONT_SIZE, "----", WARHEAD_X,
						   row_y, color);
				frontend_text_draw(FONT_SIZE, "----", BEAM_X,
						   row_y, color);
				frontend_text_draw(FONT_SIZE, "----",
						   COUNTERMEASURE_X, row_y,
						   color);
			} else {
				frontend_text_draw(
					FONT_SIZE,
					frontend_string_get((
						frontend_string_id)(mission_setup_get_craft_type(
									    roster_index) +
								    21)),
					CRAFT_X, row_y, color);
				frontend_text_draw(
					FONT_SIZE,
					frontend_string_get((
						frontend_string_id)(mission_setup_get_warhead_type(
									    roster_index) +
								    580)),
					WARHEAD_X, row_y, color);
				frontend_text_draw(
					FONT_SIZE,
					frontend_string_get((
						frontend_string_id)(mission_setup_get_beam_type(
									    roster_index) +
								    590)),
					BEAM_X, row_y, color);
				frontend_text_draw(
					FONT_SIZE,
					frontend_string_get((
						frontend_string_id)(mission_setup_get_countermeasure_type(
									    roster_index) +
								    595)),
					COUNTERMEASURE_X, row_y, color);
			}
			row_y += ROW_HEIGHT;
		}
	} else {
		for (roster_index = 0; roster_index < ROSTER_CAPACITY;
		     ++roster_index) {
			player_id = g_mp_roster[roster_index].player_id;
			if (player_id == 0 ||
			    net_get_local_player_id() != player_id) {
				continue;
			}

			color = g_mp_roster_ready_flags[roster_index] != 0
					? g_color_gray
					: g_color_yellow;
			frontend_draw_rect_assign(&row_rect, PLAYER_X, row_y,
						  CRAFT_X - 2,
						  row_y + ROW_HEIGHT);
			frontend_display_get_screen_clip_rect(&old_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&row_rect);
			if (g_mp_roster_ready_flags[roster_index] != 0) {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					g_mp_roster[roster_index].name);
				frontend_text_draw(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   PLAYER_X, row_y, color);
			} else {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				frontend_text_draw(
					FONT_SIZE, g_frontend_scratch_buffer,
					PLAYER_X, row_y,
					net_get_local_player_id() ==
								g_mp_roster[roster_index]
									.player_id ||
							g_frontend_mission_session_mode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulse_color_ramp
							  [((frame_counter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			frontend_display_set_screen_clip_rect640x480(
				&old_clip_rect);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_craft_type(
								    roster_index) +
							    21)),
				CRAFT_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_warhead_type(
								    roster_index) +
							    580)),
				WARHEAD_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_beam_type(
								    roster_index) +
							    590)),
				BEAM_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_countermeasure_type(
								    roster_index) +
							    595)),
				COUNTERMEASURE_X, row_y, color);
			row_y += ROW_HEIGHT;
		}

		for (roster_index = 0; roster_index < ROSTER_CAPACITY;
		     ++roster_index) {
			player_id = g_mp_roster[roster_index].player_id;
			if (player_id == 0 ||
			    net_get_local_player_id() == player_id) {
				continue;
			}

			for (team_player_index = 0;
			     team_player_index <
			     g_team_player_flight_group_count[g_pilot_data
								      .team];
			     ++team_player_index) {
				if (g_mission_setup_player_assignments
					    .team_player_ids
						    [g_pilot_data.team]
						    [team_player_index] ==
				    player_id) {
					break;
				}
			}
			if (team_player_index ==
			    g_team_player_flight_group_count[g_pilot_data
								     .team]) {
				continue;
			}

			color = g_mp_roster_ready_flags[roster_index] != 0
					? g_color_gray
					: g_color_yellow;
			frontend_draw_rect_assign(&row_rect, PLAYER_X, row_y,
						  CRAFT_X - 2,
						  row_y + ROW_HEIGHT);
			frontend_display_get_screen_clip_rect(&old_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&row_rect);
			if (g_mp_roster_ready_flags[roster_index] != 0) {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					g_mp_roster[roster_index].name);
				frontend_text_draw(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   PLAYER_X, row_y, color);
			} else {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				frontend_text_draw(
					FONT_SIZE, g_frontend_scratch_buffer,
					PLAYER_X, row_y,
					net_get_local_player_id() ==
								player_id ||
							g_frontend_mission_session_mode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulse_color_ramp
							  [((frame_counter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			frontend_display_set_screen_clip_rect640x480(
				&old_clip_rect);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_craft_type(
								    roster_index) +
							    21)),
				CRAFT_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_warhead_type(
								    roster_index) +
							    580)),
				WARHEAD_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_beam_type(
								    roster_index) +
							    590)),
				BEAM_X, row_y, color);
			frontend_text_draw(
				FONT_SIZE,
				frontend_string_get((
					frontend_string_id)(mission_setup_get_countermeasure_type(
								    roster_index) +
							    595)),
				COUNTERMEASURE_X, row_y, color);
			row_y += ROW_HEIGHT;
		}

		for (roster_index = 0; roster_index < ROSTER_CAPACITY;
		     ++roster_index) {
			player_id = g_mp_roster[roster_index].player_id;
			if (player_id == 0 ||
			    net_get_local_player_id() == player_id) {
				continue;
			}

			for (team_player_index = 0;
			     team_player_index <
			     g_team_player_flight_group_count[g_pilot_data
								      .team];
			     ++team_player_index) {
				if (g_mission_setup_player_assignments
					    .team_player_ids
						    [g_pilot_data.team]
						    [team_player_index] ==
				    player_id) {
					break;
				}
			}
			if (team_player_index !=
			    g_team_player_flight_group_count[g_pilot_data
								     .team]) {
				continue;
			}

			color = g_mp_roster_ready_flags[roster_index] != 0
					? g_color_gray
					: g_color_yellow;
			frontend_draw_rect_assign(&row_rect, PLAYER_X, row_y,
						  CRAFT_X - 2,
						  row_y + ROW_HEIGHT);
			frontend_display_get_screen_clip_rect(&old_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&row_rect);
			if (g_mp_roster_ready_flags[roster_index] != 0) {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					g_mp_roster[roster_index].name);
				frontend_text_draw(FONT_SIZE,
						   g_frontend_scratch_buffer,
						   PLAYER_X, row_y, color);
			} else {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				frontend_text_draw(
					FONT_SIZE, g_frontend_scratch_buffer,
					PLAYER_X, row_y,
					net_get_local_player_id() ==
								player_id ||
							g_frontend_mission_session_mode ==
								FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? g_pulse_color_ramp
							  [((frame_counter %
							     PULSE_PERIOD) &
							    PULSE_PAIR_MASK) >>
							   1]
						: color);
			}
			frontend_display_set_screen_clip_rect640x480(
				&old_clip_rect);
			frontend_draw_rect_assign(&other_team_rect, CRAFT_X,
						  row_y, COUNTERMEASURE_X,
						  row_y + ROW_HEIGHT - 1);
			frontend_text_draw_aligned_in_rect(
				FONT_SIZE,
				frontend_string_get(
					FRONTSTR_599_NOT_ON_YOUR_TEAM),
				&other_team_rect, 1, 0, color);
			row_y += ROW_HEIGHT;
		}
	}
}

/* Handles the loadout buttons of the mission briefing's craft screen; called by
 * mission_briefing.c each frame. Next and previous craft, warhead, beam weapon
 * and countermeasure are each lit when there is more than one choice and the
 * player may change the loadout: a solo player may; in a training sequence
 * (training type with mission_sequence_active 1), in either kind of game, only at
 * the easy cheat difficulty; outside a solo game otherwise as
 * g_game_config.craft_selection says: on for everyone, host only for the host,
 * off for no one. The beam button is also unlit for craft types 1 to 5 and 14.
 * The warhead, beam and countermeasure buttons step their
 * g_missionSetupSelected option forward on a left click and back otherwise,
 * wrapping. The craft buttons step
 * g_mission_setup_selected_preset_craft_option_index, skipping the preset that is the
 * flight group's own craft, or else
 * g_mission_setup_selected_flight_group_craft_option_index, which also sets
 * g_mission_setup_selected_craft_count and g_mission_setup_selected_wave_count_minus_one
 * from the option; in a melee or tournament sequence past its first mission
 * they also step past craft of the other faction (types 1 to 4 and 14 are
 * Rebel). A new craft loads its model into the preview and, in a melee or
 * tournament, swaps the background between the Rebel and Imperial craft screens
 * by the craft's faction, setting g_mission_briefing_craft_screen_faction and
 * redrawing the screen's base. When a choice changed outside a solo game it
 * sends a 9-word CRAFT_LOADOUT packet to every player: counting the type as
 * word 0, the flight group's optional craft category, the preset and flight
 * group craft options, the warhead, beam and countermeasure options, the wave
 * count minus one and the craft count. Returns 0. Does not check that some
 * craft option fits the faction: the stepping then never ends. */
// FUNCTION: XVT 0x4ED700
int mission_setup_update_craft_loadout(void)
{
	enum {
		NAVIGATION_SLOT_COUNT = 8,
		ACTIVE_LOADOUT_SLOT_COUNT = 5,
		NEXT_CRAFT_SLOT = 0,
		PREVIOUS_CRAFT_SLOT = 1,
		WARHEAD_SLOT = 2,
		BEAM_SLOT = 3,
		COUNTERMEASURE_SLOT = 4,
		BUTTON_SPACING = 28,
		BUTTON_FONT_SIZE = 12,
		NEXT_CRAFT_HELD_SLOT = 11,
		PREVIOUS_CRAFT_HELD_SLOT = 12,
		WARHEAD_HELD_SLOT = 13,
		BEAM_HELD_SLOT = 14,
		COUNTERMEASURE_HELD_SLOT = 15,
		PILOT_FACTION_REBEL = 0,
		PILOT_FACTION_IMPERIAL = 1,
		PACKET_PRESET_CRAFT_OFFSET = 4,
		PACKET_FLIGHT_GROUP_CRAFT_OFFSET = 8,
		PACKET_WARHEAD_OFFSET = 12,
		PACKET_BEAM_OFFSET = 16,
		PACKET_COUNTERMEASURE_OFFSET = 20,
		PACKET_WAVE_COUNT_OFFSET = 24,
		PACKET_CRAFT_COUNT_OFFSET = 28,
		CRAFT_LOADOUT_PACKET_SIZE = 9 * sizeof(int),
	};

	int is_host = 0;
	if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
	    net_is_host() != 0) {
		is_host = 1;
	}
	int loadout_changed = 0;
	int can_change_loadout;
	int selected_option_index;
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			can_change_loadout = g_game_config.difficulty ==
					     GAME_DIFFICULTY_EASY_CHEAT;
		} else {
			can_change_loadout = 1;
		}
	} else {
		can_change_loadout = 0;
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			can_change_loadout = g_game_config.difficulty ==
					     GAME_DIFFICULTY_EASY_CHEAT;
		} else {
			/* selected_option_index holds the craft selection
			 * setting here; from the loadout buttons on it holds
			 * the countermeasure, beam or warhead option being
			 * cycled. */
			selected_option_index = g_game_config.craft_selection;
			if (selected_option_index != CRAFT_SELECTION_OFF) {
				if (selected_option_index ==
				    CRAFT_SELECTION_ON) {
					can_change_loadout = 1;
				} else if (selected_option_index ==
						   CRAFT_SELECTION_HOST_ONLY &&
					   is_host) {
					can_change_loadout = 1;
				}
			}
		}
	}

	int craft_buttons_enabled = 0;
	if ((g_mission_setup_flight_group_craft_option_count > 1 ||
	     g_mission_setup_preset_craft_option_count > 1) &&
	    can_change_loadout) {
		craft_buttons_enabled = 1;
	}
	frontend_navigation_slot_state slot_states[NAVIGATION_SLOT_COUNT];
	slot_states[NEXT_CRAFT_SLOT] = craft_buttons_enabled;
	slot_states[PREVIOUS_CRAFT_SLOT] = craft_buttons_enabled;
	slot_states[WARHEAD_SLOT] =
		g_mission_setup_warhead_option_count > 1 && can_change_loadout;
	int craft_type;
	if (g_mission_setup_beam_option_count <= 1 || !can_change_loadout) {
		slot_states[BEAM_SLOT] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	} else {
		slot_states[BEAM_SLOT] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		craft_type = mission_setup_get_craft_type(-1);
		if (craft_type >= CRAFT_SPECIES_X_WING &&
		    (craft_type <= CRAFT_SPECIES_TIE_FIGHTER ||
		     craft_type == CRAFT_SPECIES_Z_95_HEADHUNTER)) {
			slot_states[BEAM_SLOT] =
				FRONTEND_NAVIGATION_SLOT_INACTIVE;
		}
	}
	slot_states[COUNTERMEASURE_SLOT] =
		g_mission_setup_countermeasure_option_count > 1 &&
		can_change_loadout;
	slot_states[ACTIVE_LOADOUT_SLOT_COUNT] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[ACTIVE_LOADOUT_SLOT_COUNT + 1] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[ACTIVE_LOADOUT_SLOT_COUNT + 2] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;

	int mouse_y;
	int mouse_x;
	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 22, 114, 42, 138);
	for (int slot_index = 0; slot_index < ACTIVE_LOADOUT_SLOT_COUNT;
	     ++slot_index) {
		if (slot_states[slot_index] !=
			    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
		    frontend_draw_point_in_rect(&rect, mouse_x, mouse_y) != 0 &&
		    (frontend_mouse_get_left_down() != 0 ||
		     frontend_mouse_get_right_down() != 0 ||
		     frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			slot_states[slot_index] =
				FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		frontend_draw_rect_offset_xy(&rect, 0, BUTTON_SPACING);
	}
	frontend_button_draw_eight_slot_navigation_state(slot_states);

	frontend_draw_rect_assign(&rect, 22, 226, 42, 250);
	int left_click;
	if (slot_states[COUNTERMEASURE_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    frontend_button_handle_sprite_button(
		    &rect, "craft5u", "craft5d",
		    frontend_string_get(
			    FRONTSTR_271_NEXT_COUNTERMEASURE_CHOICE),
		    BUTTON_FONT_SIZE, 0, COUNTERMEASURE_HELD_SLOT,
		    "jewelsound") != 0) {
		left_click = frontend_mouse_get_left_click();
		selected_option_index =
			g_mission_setup_selected_countermeasure_option_index;
		if (left_click != 0) {
			++selected_option_index;
			g_mission_setup_selected_countermeasure_option_index =
				selected_option_index;
			if (g_mission_setup_countermeasure_option_count <=
			    selected_option_index) {
				g_mission_setup_selected_countermeasure_option_index =
					0;
			}
		} else {
			if (selected_option_index != 0) {
				--selected_option_index;
			} else {
				selected_option_index =
					g_mission_setup_countermeasure_option_count -
					1;
			}
			g_mission_setup_selected_countermeasure_option_index =
				selected_option_index;
		}
		loadout_changed = 1;
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (slot_states[BEAM_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    frontend_button_handle_sprite_button(
		    &rect, "craft4u", "craft4d",
		    frontend_string_get(FRONTSTR_272_NEXT_BEAM_WEAPON_CHOICE),
		    BUTTON_FONT_SIZE, 0, BEAM_HELD_SLOT, "jewelsound") != 0) {
		left_click = frontend_mouse_get_left_click();
		selected_option_index =
			g_mission_setup_selected_beam_option_index;
		if (left_click != 0) {
			++selected_option_index;
			g_mission_setup_selected_beam_option_index =
				selected_option_index;
			if (g_mission_setup_beam_option_count <=
			    selected_option_index) {
				g_mission_setup_selected_beam_option_index = 0;
			}
		} else {
			if (selected_option_index != 0) {
				--selected_option_index;
			} else {
				selected_option_index =
					g_mission_setup_beam_option_count - 1;
			}
			g_mission_setup_selected_beam_option_index =
				selected_option_index;
		}
		loadout_changed = 1;
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (slot_states[WARHEAD_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    frontend_button_handle_sprite_button(
		    &rect, "craft3u", "craft3d",
		    frontend_string_get(FRONTSTR_270_NEXT_WARHEAD_CHOICE),
		    BUTTON_FONT_SIZE, 0, WARHEAD_HELD_SLOT,
		    "jewelsound") != 0) {
		left_click = frontend_mouse_get_left_click();
		selected_option_index =
			g_mission_setup_selected_warhead_option_index;
		if (left_click != 0) {
			++selected_option_index;
			g_mission_setup_selected_warhead_option_index =
				selected_option_index;
			if (g_mission_setup_warhead_option_count <=
			    selected_option_index) {
				g_mission_setup_selected_warhead_option_index =
					0;
			}
		} else {
			if (selected_option_index != 0) {
				--selected_option_index;
			} else {
				selected_option_index =
					g_mission_setup_warhead_option_count -
					1;
			}
			g_mission_setup_selected_warhead_option_index =
				selected_option_index;
		}
		loadout_changed = 1;
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	int selected_preset_craft_option_index;
	int selected_flight_group_craft_option_index;
	int selected_flight_group_index;
	int reject_craft_choice;
	if (slot_states[PREVIOUS_CRAFT_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    frontend_button_handle_sprite_button(
		    &rect, "craft2u", "craft2d",
		    frontend_string_get(FRONTSTR_269_PREVIOUS_CRAFT_CHOICE),
		    BUTTON_FONT_SIZE, 0, PREVIOUS_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		loadout_changed = 1;
		selected_preset_craft_option_index =
			g_mission_setup_selected_preset_craft_option_index;
		selected_flight_group_craft_option_index =
			g_mission_setup_selected_flight_group_craft_option_index;
		selected_flight_group_index =
			g_mission_setup_selected_flight_group_index;
		for (;;) {
			reject_craft_choice = 0;
			if (g_mission_setup_preset_craft_option_count != 0) {
				--g_mission_setup_selected_preset_craft_option_index;
				selected_preset_craft_option_index =
					g_mission_setup_selected_preset_craft_option_index;
				if (selected_preset_craft_option_index < 0) {
					selected_preset_craft_option_index =
						g_mission_setup_preset_craft_option_count -
						1;
				}
				/* craft_type holds the flight group's preset
				 * craft category here (1 to 3), not a craft
				 * species. */
				craft_type =
					g_frontend_mission
						.flight_groups
							[selected_flight_group_index]
						.optional_craft_category;
				if (selected_preset_craft_option_index != 0 &&
				    craft_type >= 1) {
					if (craft_type <= 2) {
						if ((uint8_t)g_frontend_mission
							    .flight_groups
								    [selected_flight_group_index]
							    .craft_type ==
						    g_preset_craft_types
							    [selected_preset_craft_option_index]) {
							--selected_preset_craft_option_index;
						}
					} else if (
						craft_type == 3 &&
						(uint8_t)g_frontend_mission
								.flight_groups
									[selected_flight_group_index]
								.craft_type ==
							g_preset_craft_types
								[selected_preset_craft_option_index +
								 5]) {
						--selected_preset_craft_option_index;
					}
				}
			} else {
				--g_mission_setup_selected_flight_group_craft_option_index;
				selected_flight_group_craft_option_index =
					g_mission_setup_selected_flight_group_craft_option_index;
				if (selected_flight_group_craft_option_index <
				    0) {
					selected_flight_group_craft_option_index =
						g_mission_setup_flight_group_craft_option_count -
						1;
				}
				if (selected_flight_group_craft_option_index !=
				    0) {
					g_mission_setup_selected_craft_count =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_optional_craft
								[selected_flight_group_craft_option_index -
								 1];
					g_mission_setup_selected_wave_count_minus_one =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_optional_craft_waves
								[selected_flight_group_craft_option_index -
								 1];
				} else {
					g_mission_setup_selected_craft_count =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_craft;
					g_mission_setup_selected_wave_count_minus_one =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_waves;
				}
			}

			if (g_pilot_data.mission_sequence_active == 1 &&
			    (g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_MELEES ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_TOURNAMENTS)) {
				g_mission_setup_selected_preset_craft_option_index =
					selected_preset_craft_option_index;
				g_mission_setup_selected_flight_group_craft_option_index =
					selected_flight_group_craft_option_index;
				if ((unsigned int)g_pilot_data
					    .melee_tournament_sequence_state
					    .current_mission_index > 0) {
					craft_type =
						mission_setup_get_craft_type(
							-1);
					selected_preset_craft_option_index =
						g_mission_setup_selected_preset_craft_option_index;
					selected_flight_group_craft_option_index =
						g_mission_setup_selected_flight_group_craft_option_index;
					selected_flight_group_index =
						g_mission_setup_selected_flight_group_index;
					if (craft_type >=
						    CRAFT_SPECIES_X_WING &&
					    (craft_type <=
						     CRAFT_SPECIES_B_WING ||
					     craft_type ==
						     CRAFT_SPECIES_Z_95_HEADHUNTER)) {
						if (g_pilot_data
							    .current_faction_id ==
						    PILOT_FACTION_IMPERIAL) {
							reject_craft_choice = 1;
						}
					} else if (
						g_pilot_data
							.current_faction_id ==
						PILOT_FACTION_REBEL) {
						reject_craft_choice = 1;
					}
				}
			}
			g_mission_setup_selected_preset_craft_option_index =
				selected_preset_craft_option_index;
			g_mission_setup_selected_flight_group_craft_option_index =
				selected_flight_group_craft_option_index;
			if (reject_craft_choice == 0) {
				craft_type = mission_setup_get_craft_type(-1);
				model_preview_load_model(
					g_ship_list
						[g_ship_type_to_ship_list_index
							 [craft_type]]
							.model_file_name);
				model_preview_set_light_direction(-1, 0, 1);
				if (g_pilot_data.mission_directory_id ==
					    MISSION_DIRECTORY_MELEES ||
				    g_pilot_data.mission_directory_id ==
					    MISSION_DIRECTORY_TOURNAMENTS) {
					craft_type =
						mission_setup_get_craft_type(
							-1);
					if (craft_type >=
						    CRAFT_SPECIES_X_WING &&
					    (craft_type <=
						     CRAFT_SPECIES_B_WING ||
					     craft_type ==
						     CRAFT_SPECIES_Z_95_HEADHUNTER)) {
						if (g_mission_briefing_craft_screen_faction ==
						    MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL) {
							front_image_free_resource_by_name(
								"background");
							g_mission_briefing_craft_screen_faction =
								MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
							if (g_frontend_mission_session_mode ==
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								front_image_register_resource_default(
									"frontres\\craftsr.bmp",
									"background");
							} else {
								front_image_register_resource_default(
									"frontres\\craftmr.bmp",
									"background");
							}
						}
					} else if (
						g_mission_briefing_craft_screen_faction ==
						MISSION_BRIEFING_CRAFT_SCREEN_REBEL) {
						front_image_free_resource_by_name(
							"background");
						g_mission_briefing_craft_screen_faction =
							MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
						if (g_frontend_mission_session_mode ==
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							front_image_register_resource_default(
								"frontres\\craftsi.bmp",
								"background");
						} else {
							front_image_register_resource_default(
								"frontres\\craftmi.bmp",
								"background");
						}
					}
					frontend_display_lock_offscreen_surface();
					front_image_draw_sprite_opaque(
						"background", 0, 0);
					front_image_draw_sprite("frame", 0, 0);
					front_image_draw_sprite("allactive", 0,
								0);
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						front_image_draw_sprite_translucent(
							"chatbox", 0, 0);
					}
					front_image_draw_sprite_translucent(
						"regoverlay", 0, 0);
					frontend_display_unlock_offscreen_surface(
						1);
				}
				break;
			}
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (slot_states[NEXT_CRAFT_SLOT] != FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    frontend_button_handle_sprite_button(
		    &rect, "craft1u", "craft1d",
		    frontend_string_get(FRONTSTR_268_NEXT_CRAFT_CHOICE),
		    BUTTON_FONT_SIZE, 0, NEXT_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		loadout_changed = 1;
		selected_preset_craft_option_index =
			g_mission_setup_selected_preset_craft_option_index;
		selected_flight_group_craft_option_index =
			g_mission_setup_selected_flight_group_craft_option_index;
		selected_flight_group_index =
			g_mission_setup_selected_flight_group_index;
		do {
			reject_craft_choice = 0;
			if (g_mission_setup_preset_craft_option_count != 0) {
				++g_mission_setup_selected_preset_craft_option_index;
				selected_preset_craft_option_index =
					g_mission_setup_selected_preset_craft_option_index;
				if (g_mission_setup_preset_craft_option_count <=
				    selected_preset_craft_option_index) {
					selected_preset_craft_option_index = 0;
				}
				/* craft_type holds the flight group's preset
				 * craft category here (1 to 3), not a craft
				 * species. */
				craft_type =
					g_frontend_mission
						.flight_groups
							[selected_flight_group_index]
						.optional_craft_category;
				if (selected_preset_craft_option_index != 0 &&
				    craft_type >= 1) {
					if (craft_type <= 2) {
						if ((uint8_t)g_frontend_mission
							    .flight_groups
								    [selected_flight_group_index]
							    .craft_type ==
						    g_preset_craft_types
							    [selected_preset_craft_option_index]) {
							++selected_preset_craft_option_index;
						}
					} else if (
						craft_type == 3 &&
						(uint8_t)g_frontend_mission
								.flight_groups
									[selected_flight_group_index]
								.craft_type ==
							g_preset_craft_types
								[selected_preset_craft_option_index +
								 5]) {
						++selected_preset_craft_option_index;
					}
				}
				g_mission_setup_selected_preset_craft_option_index =
					selected_preset_craft_option_index;
				if (g_mission_setup_preset_craft_option_count <=
				    selected_preset_craft_option_index) {
					selected_preset_craft_option_index = 0;
				}
			} else {
				++g_mission_setup_selected_flight_group_craft_option_index;
				selected_flight_group_craft_option_index =
					g_mission_setup_selected_flight_group_craft_option_index;
				if (g_mission_setup_flight_group_craft_option_count <=
				    selected_flight_group_craft_option_index) {
					selected_flight_group_craft_option_index =
						0;
				}
				if (selected_flight_group_craft_option_index !=
				    0) {
					g_mission_setup_selected_craft_count =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_optional_craft
								[selected_flight_group_craft_option_index -
								 1];
					g_mission_setup_selected_wave_count_minus_one =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_optional_craft_waves
								[selected_flight_group_craft_option_index -
								 1];
				} else {
					g_mission_setup_selected_craft_count =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_craft;
					g_mission_setup_selected_wave_count_minus_one =
						g_frontend_mission
							.flight_groups
								[selected_flight_group_index]
							.number_of_waves;
				}
			}

			if (g_pilot_data.mission_sequence_active == 1 &&
			    (g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_MELEES ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_TOURNAMENTS)) {
				g_mission_setup_selected_preset_craft_option_index =
					selected_preset_craft_option_index;
				g_mission_setup_selected_flight_group_craft_option_index =
					selected_flight_group_craft_option_index;
				if ((unsigned int)g_pilot_data
					    .melee_tournament_sequence_state
					    .current_mission_index > 0) {
					craft_type =
						mission_setup_get_craft_type(
							-1);
					selected_preset_craft_option_index =
						g_mission_setup_selected_preset_craft_option_index;
					selected_flight_group_craft_option_index =
						g_mission_setup_selected_flight_group_craft_option_index;
					selected_flight_group_index =
						g_mission_setup_selected_flight_group_index;
					if (craft_type >=
						    CRAFT_SPECIES_X_WING &&
					    (craft_type <=
						     CRAFT_SPECIES_B_WING ||
					     craft_type ==
						     CRAFT_SPECIES_Z_95_HEADHUNTER)) {
						if (g_pilot_data
							    .current_faction_id ==
						    PILOT_FACTION_IMPERIAL) {
							reject_craft_choice = 1;
						}
					} else if (
						g_pilot_data
							.current_faction_id ==
						PILOT_FACTION_REBEL) {
						reject_craft_choice = 1;
					}
				}
			}
			g_mission_setup_selected_preset_craft_option_index =
				selected_preset_craft_option_index;
			g_mission_setup_selected_flight_group_craft_option_index =
				selected_flight_group_craft_option_index;
		} while (reject_craft_choice != 0);

		craft_type = mission_setup_get_craft_type(-1);
		model_preview_load_model(
			g_ship_list[g_ship_type_to_ship_list_index[craft_type]]
				.model_file_name);
		model_preview_set_light_direction(-1, 0, 1);
		selected_preset_craft_option_index =
			g_mission_setup_selected_preset_craft_option_index;
		selected_flight_group_craft_option_index =
			g_mission_setup_selected_flight_group_craft_option_index;
		selected_flight_group_index =
			g_mission_setup_selected_flight_group_index;
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_MELEES ||
		    g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TOURNAMENTS) {
			craft_type = mission_setup_get_craft_type(-1);
			if (craft_type >= CRAFT_SPECIES_X_WING &&
			    (craft_type <= CRAFT_SPECIES_B_WING ||
			     craft_type == CRAFT_SPECIES_Z_95_HEADHUNTER)) {
				if (g_mission_briefing_craft_screen_faction ==
				    MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL) {
					front_image_free_resource_by_name(
						"background");
					g_mission_briefing_craft_screen_faction =
						MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						front_image_register_resource_default(
							"frontres\\craftsr.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\craftmr.bmp",
							"background");
					}
				}
			} else if (g_mission_briefing_craft_screen_faction ==
				   MISSION_BRIEFING_CRAFT_SCREEN_REBEL) {
				front_image_free_resource_by_name("background");
				g_mission_briefing_craft_screen_faction =
					MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					front_image_register_resource_default(
						"frontres\\craftsi.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\craftmi.bmp",
						"background");
				}
			}
			frontend_display_lock_offscreen_surface();
			front_image_draw_sprite_opaque("background", 0, 0);
			front_image_draw_sprite("frame", 0, 0);
			front_image_draw_sprite("allactive", 0, 0);
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				front_image_draw_sprite_translucent("chatbox",
								    0, 0);
			}
			front_image_draw_sprite_translucent("regoverlay", 0, 0);
			frontend_display_unlock_offscreen_surface(1);
		}
	}

	selected_preset_craft_option_index =
		g_mission_setup_selected_preset_craft_option_index;
	selected_flight_group_craft_option_index =
		g_mission_setup_selected_flight_group_craft_option_index;
	selected_flight_group_index =
		g_mission_setup_selected_flight_group_index;
	if (loadout_changed != 0 &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		*(int *)&g_frontend_net_packet_scratch
			 .payload[PACKET_PRESET_CRAFT_OFFSET] =
			selected_preset_craft_option_index;
		*(int *)&g_frontend_net_packet_scratch
			 .payload[PACKET_FLIGHT_GROUP_CRAFT_OFFSET] =
			selected_flight_group_craft_option_index;
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_CRAFT_LOADOUT;
		memcpy(&g_frontend_net_packet_scratch
				.payload[PACKET_BEAM_OFFSET],
		       &g_mission_setup_selected_beam_option_index,
		       sizeof(g_mission_setup_selected_beam_option_index));
		*(int *)&g_frontend_net_packet_scratch.payload[0] =
			g_frontend_mission
				.flight_groups[selected_flight_group_index]
				.optional_craft_category;
		memcpy(&g_frontend_net_packet_scratch
				.payload[PACKET_WARHEAD_OFFSET],
		       &g_mission_setup_selected_warhead_option_index,
		       sizeof(g_mission_setup_selected_warhead_option_index));
		memcpy(&g_frontend_net_packet_scratch
				.payload[PACKET_COUNTERMEASURE_OFFSET],
		       &g_mission_setup_selected_countermeasure_option_index,
		       sizeof(g_mission_setup_selected_countermeasure_option_index));
		memcpy(&g_frontend_net_packet_scratch
				.payload[PACKET_WAVE_COUNT_OFFSET],
		       &g_mission_setup_selected_wave_count_minus_one,
		       sizeof(g_mission_setup_selected_wave_count_minus_one));
		memcpy(&g_frontend_net_packet_scratch
				.payload[PACKET_CRAFT_COUNT_OFFSET],
		       &g_mission_setup_selected_craft_count,
		       sizeof(g_mission_setup_selected_craft_count));
		net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
					  CRAFT_LOADOUT_PACKET_SIZE);
	}
	return 0;
}

/* Sets up the local player's craft and loadout choices for the mission
 * briefing's craft screen; called by mission_briefing.c. Picks the flight
 * group: in a solo game
 * g_mission_setup_player_flight_group_indices[g_pilot_data.team * 8]; otherwise the
 * one assigned to the slot of g_pilot_data.team holding the local player's id,
 * or g_mission_setup_selected_flight_group_index as it was when no slot does. Stores
 * it in g_mission_setup_selected_flight_group_index, sets the five option indices to
 * 0 and the wave and craft counts from the flight group, and sets the option
 * counts: by the group's optional craft category, 0 gives one flight group
 * choice, 1 the 11 presets, 2 and 3 six presets, and 4 the group's own optional
 * craft plus 1 (unchanged for another category); warheads, beams and
 * countermeasures each count the group's nonzero optional entries, plus 1 when
 * there are any or the group has a default. In a melee or tournament sequence
 * past its first mission, when the craft does not suit the pilot's faction (a
 * Rebel craft, types 1 to 4 and 14, for an Imperial pilot, or any other craft
 * for a Rebel pilot), it steps through the craft options until it reaches
 * g_craft_iff_counterpart of that craft; nothing changes when the counterpart is
 * 0. Does not check that an option gives the counterpart: the stepping then
 * never ends. */
// FUNCTION: XVT 0x4EE1A0
void mission_setup_init_craft_loadout(void)
{
	int selected_flight_group_index;

	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		selected_flight_group_index =
			g_mission_setup_player_flight_group_indices
				[g_pilot_data.team * 8];
	} else {
		for (int player_index = 0; player_index < 8; ++player_index) {
			int team_player_id =
				g_mission_setup_player_assignments
					.team_player_ids[g_pilot_data.team]
							[player_index];
			int local_player_id = net_get_local_player_id();
			selected_flight_group_index =
				g_mission_setup_selected_flight_group_index;
			if (team_player_id == local_player_id) {
				selected_flight_group_index =
					g_mission_setup_player_flight_group_indices
						[g_pilot_data.team * 8 +
						 player_index];
			}
			g_mission_setup_selected_flight_group_index =
				selected_flight_group_index;
		}
	}

	g_mission_setup_selected_flight_group_index =
		selected_flight_group_index;
	g_mission_setup_selected_flight_group_craft_option_index = 0;
	g_mission_setup_selected_preset_craft_option_index = 0;
	g_mission_setup_selected_warhead_option_index = 0;
	g_mission_setup_selected_beam_option_index = 0;
	g_mission_setup_selected_countermeasure_option_index = 0;
	g_mission_setup_selected_wave_count_minus_one =
		g_frontend_mission.flight_groups[selected_flight_group_index]
			.number_of_waves;
	g_mission_setup_selected_craft_count =
		g_frontend_mission.flight_groups[selected_flight_group_index]
			.number_of_craft;

	int option_index;
	int option_count;
	switch (g_frontend_mission.flight_groups[selected_flight_group_index]
			.optional_craft_category) {
	case 0:
		g_mission_setup_preset_craft_option_count = 0;
		g_mission_setup_flight_group_craft_option_count = 1;
		break;
	case 1:
		g_mission_setup_preset_craft_option_count = 11;
		g_mission_setup_flight_group_craft_option_count = 0;
		break;
	case 2:
		g_mission_setup_preset_craft_option_count = 6;
		g_mission_setup_flight_group_craft_option_count = 0;
		break;
	case 3:
		g_mission_setup_preset_craft_option_count = 6;
		g_mission_setup_flight_group_craft_option_count = 0;
		break;
	case 4:
		option_count = 1;
		g_mission_setup_preset_craft_option_count = 0;
		for (option_index = 0; option_index < 10; ++option_index) {
			if (g_frontend_mission
				    .flight_groups[selected_flight_group_index]
				    .optional_craft[option_index] !=
			    CRAFT_SPECIES_UNKNOWN) {
				++option_count;
			}
			g_mission_setup_flight_group_craft_option_count =
				option_count;
		}
		break;
	default:
		break;
	}

	option_count = 0;
	for (option_index = 0; option_index < 8; ++option_index) {
		if (g_frontend_mission
			    .flight_groups[selected_flight_group_index]
			    .optional_warheads[option_index] != 0) {
			++option_count;
		}
	}
	if (g_frontend_mission.flight_groups[selected_flight_group_index]
		    .warhead != 0) {
		g_mission_setup_warhead_option_count = option_count + 1;
	} else {
		g_mission_setup_warhead_option_count = option_count;
		if (option_count != 0) {
			g_mission_setup_warhead_option_count = option_count + 1;
		}
	}

	option_count = 0;
	for (option_index = 0; option_index < 6; ++option_index) {
		if (g_frontend_mission
			    .flight_groups[selected_flight_group_index]
			    .optional_beams[option_index] != 0) {
			++option_count;
		}
	}
	if (g_frontend_mission.flight_groups[selected_flight_group_index]
		    .beam != 0) {
		g_mission_setup_beam_option_count = option_count + 1;
	} else {
		g_mission_setup_beam_option_count = option_count;
		if (option_count != 0) {
			g_mission_setup_beam_option_count = option_count + 1;
		}
	}

	option_count = 0;
	for (option_index = 0; option_index < 4; ++option_index) {
		if (g_frontend_mission
			    .flight_groups[selected_flight_group_index]
			    .optional_countermeasures[option_index] != 0) {
			++option_count;
		}
	}
	if (g_frontend_mission.flight_groups[selected_flight_group_index]
		    .countermeasures != 0) {
		g_mission_setup_countermeasure_option_count = option_count + 1;
	} else {
		g_mission_setup_countermeasure_option_count = option_count;
		if (option_count != 0) {
			g_mission_setup_countermeasure_option_count =
				option_count + 1;
		}
	}

	int preset_craft_option_index;
	if (g_pilot_data.mission_sequence_active == 1 &&
	    (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	     g_pilot_data.mission_directory_id ==
		     MISSION_DIRECTORY_TOURNAMENTS) &&
	    (unsigned int)g_pilot_data.melee_tournament_sequence_state
			    .current_mission_index > 0) {
		int counterpart_craft_type = 0;
		int craft_type = mission_setup_get_craft_type(-1);
		if (((craft_type >= 1 && craft_type <= 4) ||
		     craft_type == 14)) {
			if (g_pilot_data.current_faction_id == 1) {
				counterpart_craft_type =
					g_craft_iff_counterpart[craft_type];
			}
		} else if (g_pilot_data.current_faction_id == 0) {
			counterpart_craft_type =
				g_craft_iff_counterpart[craft_type];
		}
		if (counterpart_craft_type == 0) {
			return;
		}

		int craft_type_mismatch;
		do {
			craft_type_mismatch = 0;
			if (counterpart_craft_type !=
			    mission_setup_get_craft_type(-1)) {
				craft_type_mismatch = 1;
			}
			if (craft_type_mismatch) {
				if (g_mission_setup_preset_craft_option_count !=
				    0) {
					++g_mission_setup_selected_preset_craft_option_index;
					preset_craft_option_index =
						g_mission_setup_selected_preset_craft_option_index;
					if (preset_craft_option_index >=
					    g_mission_setup_preset_craft_option_count) {
						preset_craft_option_index = 0;
					}
					if (preset_craft_option_index != 0) {
						switch (g_frontend_mission
								.flight_groups
									[g_mission_setup_selected_flight_group_index]
								.optional_craft_category) {
						case 1:
						case 2:
							if (g_frontend_mission
								    .flight_groups
									    [g_mission_setup_selected_flight_group_index]
								    .craft_type ==
							    g_preset_craft_types
								    [preset_craft_option_index]) {
								++preset_craft_option_index;
							}
							break;
						case 3:
							if (g_frontend_mission
								    .flight_groups
									    [g_mission_setup_selected_flight_group_index]
								    .craft_type ==
							    g_preset_craft_types
								    [preset_craft_option_index +
								     5]) {
								++preset_craft_option_index;
							}
							break;
						default:
							break;
						}
					}
					g_mission_setup_selected_preset_craft_option_index =
						preset_craft_option_index;
					if (preset_craft_option_index >=
					    g_mission_setup_preset_craft_option_count) {
						g_mission_setup_selected_preset_craft_option_index =
							0;
					}
				} else {
					++g_mission_setup_selected_flight_group_craft_option_index;
					int flight_group_craft_option_index =
						g_mission_setup_selected_flight_group_craft_option_index;
					if (flight_group_craft_option_index >=
					    g_mission_setup_flight_group_craft_option_count) {
						flight_group_craft_option_index =
							0;
					}
					g_mission_setup_selected_flight_group_craft_option_index =
						flight_group_craft_option_index;
					if (flight_group_craft_option_index !=
					    0) {
						g_mission_setup_selected_craft_count =
							g_frontend_mission
								.flight_groups
									[g_mission_setup_selected_flight_group_index]
								.number_of_optional_craft
									[flight_group_craft_option_index -
									 1];
						g_mission_setup_selected_wave_count_minus_one =
							g_frontend_mission
								.flight_groups
									[g_mission_setup_selected_flight_group_index]
								.number_of_optional_craft_waves
									[flight_group_craft_option_index -
									 1];
					} else {
						g_mission_setup_selected_craft_count =
							g_frontend_mission
								.flight_groups
									[g_mission_setup_selected_flight_group_index]
								.number_of_craft;
						g_mission_setup_selected_wave_count_minus_one =
							g_frontend_mission
								.flight_groups
									[g_mission_setup_selected_flight_group_index]
								.number_of_waves;
					}
				}
			}
		} while (craft_type_mismatch);
	}
}

/* Returns a player's warhead as the offset of its name from FRONTSTR_273_NONE
 * (0 for none), through g_warhead_type_map. With player_roster_index -1 it is the
 * local choice: the selected flight group's default warhead when
 * g_mission_setup_selected_warhead_option_index is 0 or below, else its optional
 * warhead at that index minus 1. For a g_mp_roster index it is that player's
 * assigned flight group, searched over every team's first
 * g_team_player_flight_group_count slots (a later match replacing an earlier one),
 * and the entry's warhead_option_index the same way. Does not check that the
 * player has a slot: the flight group index is then unset. */
// FUNCTION: XVT 0x4EE510
int mission_setup_get_warhead_type(int player_roster_index)
{
	int flight_group_index;
	int warhead_option_index;
	uint8_t warhead_type;

	if (player_roster_index == -1) {
		warhead_option_index =
			g_mission_setup_selected_warhead_option_index - 1;
		flight_group_index =
			g_mission_setup_selected_flight_group_index;
		if (warhead_option_index < 0) {
			warhead_type =
				g_frontend_mission
					.flight_groups[flight_group_index]
					.warhead;
			if (warhead_type == 0) {
				return 0;
			}
			return g_warhead_type_map[warhead_type];
		}
		warhead_type =
			g_frontend_mission.flight_groups[flight_group_index]
				.optional_warheads[warhead_option_index];
		return g_warhead_type_map[warhead_type];
	}

	int team_flight_group_offset = 0;
	for (int team_index = 0; team_index < 10; ++team_index) {
		int team_player_index = 0;
		while (team_player_index <
		       g_team_player_flight_group_count[team_index]) {
			if (g_mission_setup_player_assignments
				    .team_player_ids[team_index]
						    [team_player_index] ==
			    g_mp_roster[player_roster_index].player_id) {
				flight_group_index =
					g_mission_setup_player_flight_group_indices
						[team_flight_group_offset +
						 team_player_index];
				break;
			}
			++team_player_index;
		}
		team_flight_group_offset += 8;
	}

	warhead_option_index =
		g_mp_roster[player_roster_index].warhead_option_index;
	if (warhead_option_index == 0) {
		warhead_type =
			g_frontend_mission.flight_groups[flight_group_index]
				.warhead;
		if (warhead_type == 0) {
			return 0;
		}
		return g_warhead_type_map[warhead_type];
	}
	warhead_type = g_frontend_mission.flight_groups[flight_group_index]
			       .optional_warheads[warhead_option_index - 1];
	return g_warhead_type_map[warhead_type];
}

/* Returns a player's beam weapon as the mission's beam code, 0 for none: always
 * 0 for craft types 1 to 4 and 14, else the flight group's default beam when
 * the option index is 0, or its optional beam at the index minus 1. With
 * player_roster_index -1 it uses the local choice
 * (g_mission_setup_selected_beam_option_index and the selected flight group); for a
 * g_mp_roster index, that entry's beam_option_index and the player's assigned
 * flight group, found as mission_setup_get_warhead_type finds it, unset when the
 * player has no slot. */
// FUNCTION: XVT 0x4EE650
int mission_setup_get_beam_type(int player_roster_index)
{
	int craft_type;
	uint8_t beam_type;

	if (player_roster_index == -1) {
		craft_type = mission_setup_get_craft_type(-1);
		if ((craft_type >= 1 && craft_type <= 4) || craft_type == 14) {
			return 0;
		} else {
			unsigned int selected_option_index =
				g_mission_setup_selected_beam_option_index;
			int selected_flight_group_index =
				g_mission_setup_selected_flight_group_index;
			if (selected_option_index == 0) {
				beam_type =
					g_frontend_mission
						.flight_groups
							[selected_flight_group_index]
						.beam;
				if (beam_type == 0) {
					return 0;
				}
				return beam_type;
			} else {
				return g_frontend_mission
					.flight_groups
						[selected_flight_group_index]
					.optional_beams[selected_option_index -
							1];
			}
		}
	}

	int team_flight_group_offset = 0;
	int flight_group_index;
	for (int team_index = 0; team_index < 10; ++team_index) {
		for (int team_player_index = 0;
		     team_player_index <
		     g_team_player_flight_group_count[team_index];
		     ++team_player_index) {
			if (g_mission_setup_player_assignments
				    .team_player_ids[team_index]
						    [team_player_index] ==
			    g_mp_roster[player_roster_index].player_id) {
				flight_group_index =
					g_mission_setup_player_flight_group_indices
						[team_flight_group_offset +
						 team_player_index];
				break;
			}
		}
		team_flight_group_offset += 8;
	}

	craft_type = mission_setup_get_craft_type(player_roster_index);
	if ((craft_type >= 1 && craft_type <= 4) || craft_type == 14) {
		return 0;
	} else {
		int beam_option_index =
			g_mp_roster[player_roster_index].beam_option_index;
		if (beam_option_index == 0) {
			beam_type = g_frontend_mission
					    .flight_groups[flight_group_index]
					    .beam;
			if (beam_type == 0) {
				return 0;
			}
			return beam_type;
		} else {
			return g_frontend_mission
				.flight_groups[flight_group_index]
				.optional_beams[beam_option_index - 1];
		}
	}
}

/* Returns a player's countermeasure as the mission's countermeasure code, 0 for
 * none: the flight group's default when the option index is 0, else its
 * optional countermeasure at the index minus 1. With player_roster_index -1 it
 * uses the local choice (g_mission_setup_selected_countermeasure_option_index and
 * the selected flight group); for a g_mp_roster index, that entry's
 * countermeasure_option_index and the player's assigned flight group, found as
 * mission_setup_get_warhead_type finds it, unset when the player has no slot. */
// FUNCTION: XVT 0x4EE7C0
int mission_setup_get_countermeasure_type(int player_roster_index)
{
	if (player_roster_index == -1) {
		unsigned int selected_option_index =
			g_mission_setup_selected_countermeasure_option_index;
		int selected_flight_group_index =
			g_mission_setup_selected_flight_group_index;
		if (selected_option_index == 0) {
			uint8_t selected_type =
				g_frontend_mission
					.flight_groups
						[selected_flight_group_index]
					.countermeasures;
			if (selected_type == 0) {
				return 0;
			}
			return selected_type;
		}
		return g_frontend_mission
			.flight_groups[selected_flight_group_index]
			.optional_countermeasures[selected_option_index - 1];
	}

	int team_flight_group_offset = 0;
	int flight_group_index;
	for (int team_index = 0; team_index < 10; ++team_index) {
		int team_player_index = 0;
		while (team_player_index <
		       g_team_player_flight_group_count[team_index]) {
			if (g_mission_setup_player_assignments
				    .team_player_ids[team_index]
						    [team_player_index] ==
			    g_mp_roster[player_roster_index].player_id) {
				flight_group_index =
					g_mission_setup_player_flight_group_indices
						[team_flight_group_offset +
						 team_player_index];
				break;
			}
			++team_player_index;
		}
		team_flight_group_offset += 8;
	}

	int countermeasure_option_index =
		g_mp_roster[player_roster_index].countermeasure_option_index;
	if (countermeasure_option_index == 0) {
		uint8_t countermeasure_type =
			g_frontend_mission.flight_groups[flight_group_index]
				.countermeasures;
		if (countermeasure_type == 0) {
			return 0;
		}
		return countermeasure_type;
	}
	return g_frontend_mission.flight_groups[flight_group_index]
		.optional_countermeasures[countermeasure_option_index - 1];
}

/* Returns the craft species a player flies. With player_roster_index -1, the
 * local choice: when the selected flight group offers presets, its own craft
 * for option 0, else g_preset_craft_types at the option for categories 1 and 2
 * and at the option plus 5 for category 3, and the option index itself for any
 * other category; otherwise its own craft for flight group option 0, else its
 * optional craft at the option minus 1. For a g_mp_roster index: the entry's
 * craft_type_override when nonzero, else the assigned flight group's optional
 * craft at craft_option_index itself when that, read as unsigned, is under 10 (so
 * not for -1), else the group's own craft. The flight group is found as
 * mission_setup_get_warhead_type finds it, unset when the player has no slot. */
// FUNCTION: XVT 0x4EE8E0
int mission_setup_get_craft_type(int player_roster_index)
{
	int result;

	if (player_roster_index == -1) {
		if (g_mission_setup_preset_craft_option_count != 0) {
			/* result holds the selected preset craft option here,
			 * an index into g_preset_craft_types. */
			result =
				g_mission_setup_selected_preset_craft_option_index;
			if (result == 0) {
				return g_frontend_mission
					.flight_groups
						[g_mission_setup_selected_flight_group_index]
					.craft_type;
			}
			int optional_craft_category =
				g_frontend_mission
					.flight_groups
						[g_mission_setup_selected_flight_group_index]
					.optional_craft_category;
			switch (optional_craft_category) {
			case 1:
			case 2:
				return g_preset_craft_types[result];
			case 3:
				return g_preset_craft_types[result + 5];
			}
			return result;
		}
		if (g_mission_setup_selected_flight_group_craft_option_index ==
		    0) {
			return g_frontend_mission
				.flight_groups
					[g_mission_setup_selected_flight_group_index]
				.craft_type;
		}
		return g_frontend_mission
			.flight_groups
				[g_mission_setup_selected_flight_group_index]
			.optional_craft
				[g_mission_setup_selected_flight_group_craft_option_index -
				 1];
	}

	int team_flight_group_offset = 0;
	int flight_group_index;
	for (int team_index = 0; team_index < 10; ++team_index) {
		int team_player_index = 0;
		while (team_player_index <
		       g_team_player_flight_group_count[team_index]) {
			if (g_mission_setup_player_assignments
				    .team_player_ids[team_index]
						    [team_player_index] ==
			    g_mp_roster[player_roster_index].player_id) {
				flight_group_index =
					g_mission_setup_player_flight_group_indices
						[team_flight_group_offset +
						 team_player_index];
				break;
			}
			++team_player_index;
		}
		team_flight_group_offset += 8;
	}

	/* result now holds the roster entry's craft type override, a craft species. */
	result = g_mp_roster[player_roster_index].craft_type_override;
	if (result != 0) {
		return result;
	}

	unsigned int craft_option_index =
		g_mp_roster[player_roster_index].craft_option_index;
	if (craft_option_index != UINT32_MAX && craft_option_index < 10) {
		return g_frontend_mission.flight_groups[flight_group_index]
			.optional_craft[craft_option_index];
	}
	return g_frontend_mission.flight_groups[flight_group_index].craft_type;
}

/* Loads the ship list that the tech library and the briefing's craft screen
 * draw models from, once: it does nothing while g_ship_list is set. Allocates
 * 100 entries in g_ship_list, then reads frontres\frntspec.lst, a file name and
 * a craft type per line, keeping only names whose last three characters,
 * lowercased, are "opt"; sets g_ship_type_to_ship_list_index[type] to the entry's
 * index for types under 17 and g_ship_count to the count kept. When the file
 * does not open it returns with g_ship_list allocated and g_ship_count unchanged.
 * Does not check for more than 100 entries, or for names under 3 characters. */
// FUNCTION: XVT 0x4EEA80
void ship_list_load(void)
{
	if (g_ship_list != NULL) {
		return;
	}
	g_ship_list =
		(struct ship_list_entry *)malloc(sizeof(*g_ship_list) * 100);
	if (g_ship_list == NULL) {
		return;
	}

	/* ship_list_index and ship_count always hold the same value: both start
	 * at 0 here and step together for each .opt entry kept from the
	 * list. */
	int ship_list_index = 0;
	int ship_count = 0;
	xvt_file *stream = file_open("frontres\\frntspec.lst", "r");
	if (stream == NULL) {
		return;
	}

	int craft_type;
	for (;;) {
		if (FILE_SCANF(stream, "%s %d\n", g_frontend_scratch_buffer,
			       &craft_type) != 2) {
			break;
		}
		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		for (unsigned int extension_index =
			     strlen(g_frontend_scratch_buffer) - 3;
		     extension_index < strlen(g_frontend_scratch_buffer);
		     ++extension_index) {
			g_frontend_scratch_buffer[extension_index] =
				(char)tolower(
					(unsigned char)g_frontend_scratch_buffer
						[extension_index]);
		}
		if (strcmp(&g_frontend_scratch_buffer
				   [strlen(g_frontend_scratch_buffer) - 3],
			   "opt") != 0) {
			continue;
		}
		strncpy(g_ship_list[ship_list_index].model_file_name,
			g_frontend_scratch_buffer,
			sizeof(g_ship_list[ship_list_index].model_file_name) -
				1);
		g_ship_list[ship_list_index].model_file_name
			[sizeof(g_ship_list[ship_list_index].model_file_name) -
			 1] = '\0';
		g_ship_list[ship_list_index].craft_type = craft_type;
		if (craft_type < 17) {
			g_ship_type_to_ship_list_index[craft_type] = ship_count;
		}
		++ship_list_index;
		++ship_count;
	}
	file_close(stream);
	g_ship_count = ship_count;
}

/* Exit callback of the mission_setup_enter_next_mission screen: frees
 * g_mission_list and g_mission_text, setting both to NULL, and the "background"
 * image. Returns 0. */
// FUNCTION: XVT 0x4F1190
int mission_setup_exit_next_mission(void)
{
	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	front_image_free_resource_by_name("background");
	return 0;
}

/* A one-frame screen the mission debriefing sets to go on to a tournament,
 * battle or campaign's next mission. Clears g_frontend_skip_screen_entry_setup,
 * puts saved_mission_description_id back as the played mission type's selected
 * mission, moves the mission type back to the sequence's own (training to
 * campaign, else up by one), loads its list and selects its entry, resets the
 * eight g_pilot_data.network_players results and choices, and calls
 * mission_setup_select_next_sequence_mission, ignoring its result. Then sends a
 * PILOT_RATING packet with the pilot's rating to every player and moves on. In
 * a solo game it sets g_mission_setup_is_host and refills g_mp_roster with the
 * pilot alone; outside one it sets g_mission_setup_roster_authoritative and prunes
 * the departed players and the team assignments. A training sequence (a
 * campaign) goes to team assignment. A combat engagement sequence with the
 * player's choice setup (g_game_config.random_setup 2) past its first mission
 * goes to mission_setup_battle_choice_update when the previous mission's result
 * is not equal to g_pilot_data.team, or, outside a solo game, when the ready
 * player count is not 1. Anything else goes to flight assignment. Ignores
 * frame_counter and returns 0. */
// FUNCTION: XVT 0x4F11E0
int mission_setup_enter_next_mission(int frame_counter)
{
	enum { NETWORK_PLAYER_COUNT = 8 };

	(void)frame_counter;
	g_frontend_skip_screen_entry_setup = 0;
	g_pilot_data
		.mission_description_ids[g_pilot_data.mission_directory_id] =
		g_pilot_data.saved_mission_description_id;
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_TRAINING_EXERCISES) {
		g_pilot_data.mission_directory_id = MISSION_DIRECTORY_CAMPAIGNS;
	} else {
		++g_pilot_data.mission_directory_id;
	}

	mission_setup_load_mission_list(g_pilot_data.mission_directory_id);
	if (g_mission_list != NULL) {
		g_selected_mission_list_index = 0;
		while ((unsigned int)g_selected_mission_list_index <
			       g_mission_count &&
		       g_mission_list[g_selected_mission_list_index]
				       .mission_idx !=
			       g_pilot_data.mission_description_ids
				       [g_pilot_data.mission_directory_id]) {
			++g_selected_mission_list_index;
		}
	}

	for (int player_index = 0; player_index < NETWORK_PLAYER_COUNT;
	     ++player_index) {
		int *craft_id_ptr =
			&g_pilot_data.network_players[player_index].craft_id;
		*craft_id_ptr = 0;
		g_pilot_data.network_players[player_index].craft_option = -1;
		g_pilot_data.network_players[player_index].warhead_option = -1;
		g_pilot_data.network_players[player_index].beam_option = -1;
		g_pilot_data.network_players[player_index]
			.countermeasure_option = -1;
		g_pilot_data.network_players[player_index].total_score = 0;
		g_pilot_data.network_players[player_index].kills = 0;
		g_pilot_data.network_players[player_index].kills_shared = 0;
		g_pilot_data.network_players[player_index].craft_inspected = 0;
		g_pilot_data.network_players[player_index].kills_assist = 0;
		g_pilot_data.network_players[player_index].total_losses = 0;
		g_pilot_data.network_players[player_index].has_left = 0;
	}

	mission_setup_select_next_sequence_mission();
	*(int *)g_frontend_net_packet_scratch.payload = g_pilot_data.rating;
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_PILOT_RATING;
	net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
				  2 * sizeof(int));
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_mission_setup_is_host = 1;
		memset(g_mp_roster, 0, sizeof(g_mp_roster));
		strcpy(g_mp_roster[0].name, g_pilot_data.name);
		g_mp_roster[0].player_id = 1;
		g_mp_roster[0].pilot_rating = g_pilot_data.rating;
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			frontend_screen_set_callbacks(
				mission_setup_team_assignment_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_mission_resources
#else
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
			);
			return 0;
		}
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
		    g_pilot_data.mission_sequence_active == 1 &&
		    g_game_config.random_setup == 2 &&
		    g_pilot_data.battle_sequence_state.current_mission_index >
			    0 &&
		    (int)g_pilot_data.battle_sequence_state.mission_results
				    [g_pilot_data.battle_sequence_state
					     .current_mission_index -
				     1] != g_pilot_data.team) {
			frontend_screen_set_callbacks(
				mission_setup_battle_choice_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_battle_choice
#else
				(frontend_screen_exit_fn)
					mission_setup_battle_choice_exit
#endif
			);
			return 0;
		}
		frontend_screen_set_callbacks(
			mission_setup_flight_assignment_update,
			mission_setup_free_screen_resources);
		return 0;
	}

	g_mission_setup_roster_authoritative = 1;
	mission_setup_prune_disconnected_players();
	mission_setup_prune_team_assignments();
	if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES &&
	    g_pilot_data.mission_sequence_active == 1) {
		frontend_screen_set_callbacks(
			mission_setup_team_assignment_update,

#ifdef XVT_MODERN
			xvt_frontend_cleanup_mission_resources
#else
			(frontend_screen_exit_fn)
				frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
		);
		return 0;
	}
	if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilot_data.mission_sequence_active == 1 &&
	    g_game_config.random_setup == 2 &&
	    g_pilot_data.battle_sequence_state.current_mission_index > 0) {
		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    net_count_ready_players() != 1) {
			frontend_screen_set_callbacks(
				mission_setup_battle_choice_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_battle_choice
#else
				(frontend_screen_exit_fn)
					mission_setup_battle_choice_exit
#endif
			);
			return 0;
		}
		if ((int)g_pilot_data.battle_sequence_state
			    .mission_results[g_pilot_data.battle_sequence_state
						     .current_mission_index -
					     1] != g_pilot_data.team) {
			frontend_screen_set_callbacks(
				mission_setup_battle_choice_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_battle_choice
#else
				(frontend_screen_exit_fn)
					mission_setup_battle_choice_exit
#endif
			);
			return 0;
		}
	}
	frontend_screen_set_callbacks(mission_setup_flight_assignment_update,
				      mission_setup_free_screen_resources);
	return 0;
}

/* Drops the players who left the lobby from the mission's assignments, using
 * the lobby roster from net_get_player_roster. Clears each
 * g_mission_setup_player_assignments.team_player_ids slot and each
 * g_pilot_data.network_players direct_play_id whose player is not a ready roster
 * member; sets g_local_pilot_network_player_index to the network_players entry
 * holding the local player's id, when one does; and for each assigned_player_ids
 * slot holding 0, shifts the players of each of the first g_team_count teams,
 * with their g_mission_setup_player_flight_group_indices, down over the team's slots
 * holding 0, setting slot 7 to 0 and its flight group to -1. The shifting uses
 * a player count of 1 in a solo game, else net_count_ready_players(), and does
 * nothing when that count is 0. It clears an assigned_player_ids slot that has no
 * ready roster member, with the g_mp_roster entry of the same index, only when
 * its search stops at index 8; with fewer than 8 players in the lobby roster no
 * slot is cleared. Returns 1. */
// FUNCTION: XVT 0x4F14A0
int mission_setup_prune_disconnected_players(void)
{
	int player_count;

	struct net_player_info *player_roster =
		net_get_player_roster(&player_count);
	int team_index;
	int team_player_index;
	int roster_index;
	for (team_index = 0; team_index < 10; team_index++) {
		for (team_player_index = 0; team_player_index < 8;
		     team_player_index++) {
			roster_index = 0;
			if (player_count > 0) {
				do {
					if (player_roster[roster_index]
							    .ready_flag != 0 &&
					    player_roster[roster_index]
							    .player_id ==
						    (DPID)g_mission_setup_player_assignments
							    .team_player_ids
								    [team_index]
								    [team_player_index]) {
						break;
					}
					roster_index++;
				} while (player_count > roster_index);
			}
			if (player_count == roster_index) {
				g_mission_setup_player_assignments
					.team_player_ids[team_index]
							[team_player_index] = 0;
			}
		}
	}

	int active_player_index;
	for (active_player_index = 0; active_player_index < 8;
	     active_player_index++) {
		roster_index = 0;
		if (player_count > 0) {
			do {
				if (player_roster[roster_index].ready_flag !=
					    0 &&
				    player_roster[roster_index].player_id ==
					    (DPID)g_mission_setup_player_assignments
						    .assigned_player_ids
							    [active_player_index]) {
					break;
				}
				roster_index++;
			} while (player_count > roster_index);
		}
		if (roster_index == 8) {
			g_mission_setup_player_assignments
				.assigned_player_ids[active_player_index] = 0;
			g_mp_roster[active_player_index].player_id = 0;
		}
	}

	int pilot_player_index;
	for (pilot_player_index = 0; pilot_player_index < 8;
	     pilot_player_index++) {
		roster_index = 0;
		if (player_count > 0) {
			do {
				if (player_roster[roster_index].ready_flag !=
					    0 &&
				    player_roster[roster_index].player_id ==
					    (DPID)g_pilot_data
						    .network_players
							    [pilot_player_index]
						    .direct_play_id) {
					break;
				}
				roster_index++;
			} while (player_count > roster_index);
		}
		if (player_count == roster_index) {
			g_pilot_data.network_players[pilot_player_index]
				.direct_play_id = 0;
		}
	}

	for (pilot_player_index = 0; pilot_player_index < 8;
	     pilot_player_index++) {
		if (g_pilot_data.network_players[pilot_player_index]
			    .direct_play_id == net_get_local_player_id()) {
			g_local_pilot_network_player_index = pilot_player_index;
			break;
		}
	}

	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		player_count = 1;
	} else {
		player_count = net_count_ready_players();
	}
	for (active_player_index = 0; active_player_index < 8;
	     active_player_index++) {
		roster_index = 0;
		if (player_count > 0) {
			do {
				if (g_mission_setup_player_assignments
					    .assigned_player_ids
						    [active_player_index] ==
				    0) {
					break;
				}
				roster_index++;
			} while (player_count > roster_index);
		}
		if (player_count != roster_index && g_team_count > 0) {
			/* The loop above stops early only when this assigned
			 * slot is empty, so the id taken here is always 0, and
			 * the pass below shifts each team's players down over
			 * its empty slots. */
			int empty_player_id =
				g_mission_setup_player_assignments
					.assigned_player_ids
						[active_player_index];
			for (team_index = 0; team_index < g_team_count;
			     team_index++) {
				for (team_player_index = 0;
				     team_player_index < 8;
				     team_player_index++) {
					if (g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [team_player_index] ==
					    empty_player_id) {
						if (team_player_index < 7) {
							for (int shift_index =
								     team_player_index;
							     shift_index < 7;
							     shift_index++) {
								g_mission_setup_player_assignments
									.team_player_ids
										[team_index]
										[shift_index] =
									g_mission_setup_player_assignments
										.team_player_ids
											[team_index]
											[shift_index +
											 1];
								g_mission_setup_player_flight_group_indices
									[team_index *
										 8 +
									 shift_index] = g_mission_setup_player_flight_group_indices
										[team_index *
											 8 +
										 shift_index +
										 1];
							}
						}
						g_mission_setup_player_assignments
							.team_player_ids
								[team_index]
								[7] = 0;
						g_mission_setup_player_flight_group_indices
							[team_index * 8 + 7] =
								-1;
					}
				}
			}
		}
	}
	return 1;
}

/* Moves the g_mp_roster entries with a nonzero playerId to the front, keeping
 * their order, and zeroes the entries they leave. Returns 1. */
// FUNCTION: XVT 0x4F1680
int mp_roster_compact_active_entries(void)
{
	for (unsigned int empty_index = 0; empty_index < 8; ++empty_index) {
		if (g_mp_roster[empty_index].player_id == 0) {
			for (unsigned int active_index = empty_index + 1;
			     active_index < 8; ++active_index) {
				if (g_mp_roster[active_index].player_id != 0) {
					g_mp_roster[empty_index] =
						g_mp_roster[active_index];
					memset(&g_mp_roster[active_index], 0,
					       sizeof(g_mp_roster
							      [active_index]));
					break;
				}
			}
		}
	}

	return 1;
}

/* Moves an active tournament, battle or campaign on to its next mission; the
 * mission type must be the sequence's own, as mission_setup_enter_next_mission
 * leaves it. Opens the sequence file of the g_mission_list entry matching the
 * selected description id and picks the next mission's ordinal: a tournament's
 * melee_tournament_sequence_state.current_mission_index; a campaign's
 * campaign_sequence_state.current_mission_index, first lowered by 1 when
 * last_mission_completed is 0, so the mission is flown again; for a battle, when
 * the previous mission was a draw, battle_sequence_state.current_mission_index is
 * lowered by 1 and its stored ordinal reused; otherwise, with
 * g_game_config.random_setup nonzero, a rand() % count ordinal not yet used in
 * this battle, rand being seeded with g_game_config.random_seed; otherwise the
 * current index. A battle stores the ordinal in
 * mission_ordinals[current_mission_index]. The mission file is named on line
 * ordinal + 1 after the count line; an ordinal below 0 reads no line, so the
 * count line itself is taken as the name. Then, as
 * mission_setup_select_first_sequence_mission does, it sets mission_sequence_active
 * to 1, moves to the played mission type, saves that type's selected mission in
 * saved_mission_description_id, loads its list, selects the named mission (for
 * a combat engagement also storing its id in
 * battle_sequence_state.current_mission_id) and stores its list index in
 * battle_sequence_state.mission_list_indices[current_mission_index] whatever the
 * type; last it loads the mission and its team counts. Returns 1, or 0 when the
 * file does not open or is empty, or the mission's line is empty. Does not
 * check that a random draw can find an unused ordinal (it then never ends), or
 * that a battle's current_mission_index is above 0 before it reads the previous
 * result. */
// FUNCTION: XVT 0x4F1700
int mission_setup_select_next_sequence_mission(void)
{
	enum {
		SEQUENCE_DESCRIPTOR_PATH_CAPACITY = 128,
		SEQUENCE_DESCRIPTOR_LINE_CAPACITY = 255,
	};

	unsigned int descriptor_mission_index = 0;
	unsigned int random_seed = g_game_config.random_seed;
	while (descriptor_mission_index < g_mission_count &&
	       g_mission_list[descriptor_mission_index].mission_idx !=
		       g_pilot_data.mission_description_ids
			       [g_pilot_data.mission_directory_id]) {
		++descriptor_mission_index;
	}
	char descriptor_path[SEQUENCE_DESCRIPTOR_PATH_CAPACITY];
	sprintf(descriptor_path, "%s\\%s",
		g_mission_directory_names[g_pilot_data.mission_directory_id],
		g_mission_list[descriptor_mission_index].file_name);
	xvt_file *stream = file_open(descriptor_path, "r");
	if (stream == NULL) {
		return 0;
	}
	if (FILE_GETS(g_frontend_scratch_buffer,
		      SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream) == NULL) {
		file_close(stream);
		return 0;
	}

	unsigned int mission_count = atoi(g_frontend_scratch_buffer);
	int current_mission_ordinal;
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_TOURNAMENTS) {
		current_mission_ordinal =
			g_pilot_data.melee_tournament_sequence_state
				.current_mission_index;
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		if (g_pilot_data.campaign_sequence_state
			    .last_mission_completed == 0) {
			--g_pilot_data.campaign_sequence_state
				  .current_mission_index;
		}
		current_mission_ordinal = g_pilot_data.campaign_sequence_state
						  .current_mission_index;
	} else {
		if (g_pilot_data.battle_sequence_state
			    .mission_results[g_pilot_data.battle_sequence_state
						     .current_mission_index -
					     1] == BATTLE_MISSION_RESULT_DRAW) {
			--g_pilot_data.battle_sequence_state
				  .current_mission_index;
			current_mission_ordinal =
				g_pilot_data.battle_sequence_state
					.mission_ordinals
						[g_pilot_data
							 .battle_sequence_state
							 .current_mission_index];
		} else if (g_game_config.random_setup != 0) {
			srand(random_seed);
			int duplicate_mission;
			do {
				duplicate_mission = 0;
				current_mission_ordinal =
					rand() % mission_count;
				for (int previous_mission_index =
					     (int)g_pilot_data
						     .battle_sequence_state
						     .current_mission_index -
					     1;
				     previous_mission_index >= 0;
				     --previous_mission_index) {
					if (g_pilot_data.battle_sequence_state
						    .mission_ordinals
							    [previous_mission_index] ==
					    current_mission_ordinal) {
						duplicate_mission = 1;
						break;
					}
				}
			} while (duplicate_mission != 0);
		} else {
			current_mission_ordinal =
				(int)g_pilot_data.battle_sequence_state
					.current_mission_index;
		}
		g_pilot_data.battle_sequence_state
			.mission_ordinals[g_pilot_data.battle_sequence_state
						  .current_mission_index] =
			current_mission_ordinal;
	}

	if (current_mission_ordinal >= 0) {
		int lines_to_read = current_mission_ordinal + 1;
		do {
			FILE_GETS(g_frontend_scratch_buffer,
				  SEQUENCE_DESCRIPTOR_LINE_CAPACITY, stream);
			if (g_frontend_scratch_buffer
				    [strlen(g_frontend_scratch_buffer) - 1] ==
			    '\n') {
				g_frontend_scratch_buffer
					[strlen(g_frontend_scratch_buffer) -
					 1] = '\0';
			}
			--lines_to_read;
		} while (lines_to_read != 0);
	}
	file_close(stream);
	if (g_frontend_scratch_buffer[0] == '\0') {
		return 0;
	}

	g_pilot_data.mission_sequence_active = 1;
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_CAMPAIGNS) {
		g_pilot_data.mission_directory_id =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
	} else {
		--g_pilot_data.mission_directory_id;
	}
	g_pilot_data.saved_mission_description_id =
		g_pilot_data.mission_description_ids
			[g_pilot_data.mission_directory_id];
	/* descriptor_path is reused here for the chosen mission's file name,
	 * lowercased to match the mission list. */
	strcpy(descriptor_path, g_frontend_scratch_buffer);
	for (int character_index = 0;
	     character_index < (int)strlen(descriptor_path);
	     ++character_index) {
		descriptor_path[character_index] = (char)tolower(
			(unsigned char)descriptor_path[character_index]);
	}

	mission_setup_load_mission_list(g_pilot_data.mission_directory_id);
	int mission_list_index = 0;
	while (mission_list_index < (int)g_mission_count) {
		if (strcmp(descriptor_path,
			   g_mission_list[mission_list_index].file_name) == 0) {
			g_pilot_data.mission_description_ids
				[g_pilot_data.mission_directory_id] =
				g_mission_list[mission_list_index].mission_idx;
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				g_pilot_data.battle_sequence_state
					.current_mission_id =
					g_mission_list[mission_list_index]
						.mission_idx;
			}
			break;
		}
		++mission_list_index;
	}

	if (g_mission_list != NULL) {
		g_selected_mission_list_index = 0;
		while ((unsigned int)g_selected_mission_list_index <
		       g_mission_count) {
			if (g_mission_list[g_selected_mission_list_index]
				    .mission_idx ==
			    g_pilot_data.mission_description_ids
				    [g_pilot_data.mission_directory_id]) {
				g_pilot_data.battle_sequence_state
					.mission_list_indices
						[g_pilot_data
							 .battle_sequence_state
							 .current_mission_index] =
					g_selected_mission_list_index;
				break;
			}
			++g_selected_mission_list_index;
		}
	}
	frontend_mission_load_current();
	mission_setup_update_team_counts();
	return 1;
}

/* Exit callback of the mission_setup_enter_current_mission screen: frees
 * g_mission_list and g_mission_text, setting both to NULL, and the "background"
 * image. Returns 0. */
// FUNCTION: XVT 0x4F1AB0
int mission_setup_exit_current_mission(void)
{
	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	front_image_free_resource_by_name("background");
	return 0;
}

/* A one-frame screen the mission debriefing sets to fly the current mission
 * again. Clears g_frontend_skip_screen_entry_setup, resets the eight
 * g_pilot_data.network_players results and choices (craft_id and the counts 0, the
 * four options -1, has_left 0), and sends a PILOT_RATING packet with the pilot's
 * rating to every player. In a solo game it sets g_mission_setup_is_host and
 * refills g_mp_roster with the pilot alone; outside one it sets
 * g_mission_setup_roster_authoritative and prunes the departed players and the
 * team assignments. Then a training sequence (a campaign) goes to team
 * assignment and anything else to flight assignment. Ignores frame_counter and
 * returns 0. */
// FUNCTION: XVT 0x4F1B00
int mission_setup_enter_current_mission(int frame_counter)
{
	(void)frame_counter;
	struct pilot_network_player *player = &g_pilot_data.network_players[0];
	int *craft_id_ptr = &player->craft_id;
	g_frontend_skip_screen_entry_setup = 0;
	do {
		*craft_id_ptr = 0;
		craft_id_ptr = (int *)((char *)craft_id_ptr +
				       sizeof(struct pilot_network_player));
		player->craft_option = -1;
		player->warhead_option = -1;
		player->beam_option = -1;
		player->countermeasure_option = -1;
		player->total_score = 0;
		player->kills = 0;
		player->kills_shared = 0;
		player->craft_inspected = 0;
		player->kills_assist = 0;
		player->total_losses = 0;
		player->has_left = 0;
		++player;
	} while (craft_id_ptr < &g_pilot_data.teams[2].unknown08);

	*(int *)g_frontend_net_packet_scratch.payload = g_pilot_data.rating;
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_PILOT_RATING;
	net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
				  2 * sizeof(int));
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_mission_setup_is_host = 1;
		memset(g_mp_roster, 0, sizeof(g_mp_roster));
		strcpy(g_mp_roster[0].name, g_pilot_data.name);
		g_mp_roster[0].player_id = 1;
		g_mp_roster[0].pilot_rating = g_pilot_data.rating;
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			frontend_screen_set_callbacks(
				mission_setup_team_assignment_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_mission_resources
#else
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
			);
			return 0;
		}
		frontend_screen_set_callbacks(
			mission_setup_flight_assignment_update,
			mission_setup_free_screen_resources);
		return 0;
	} else {
		g_mission_setup_roster_authoritative = 1;
		mission_setup_prune_disconnected_players();
		mission_setup_prune_team_assignments();
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			frontend_screen_set_callbacks(
				mission_setup_team_assignment_update,

#ifdef XVT_MODERN
				xvt_frontend_cleanup_mission_resources
#else
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
			);
			return 0;
		}
		frontend_screen_set_callbacks(
			mission_setup_flight_assignment_update,
			mission_setup_free_screen_resources);
		return 0;
	}
}

/* The team assignment screen, run once per frame: players are dragged into the
 * mission's team slots, and Next moves on to flight assignment. On frame 0 the
 * original build clears g_frontend_chat_team_only,
 * g_mission_setup_show_description_panel and g_frontend_first_visible_line; asks for
 * the game CD until it is found, Cancel returning 1 after, outside a solo game,
 * telling the host or the players and shutting the session down; and leaves for
 * the concourse when a client CD tries to host or fly solo. Then, entering a
 * campaign or combat engagement sequence (g_frontend_skip_screen_entry_setup 0, not
 * replaying the current mission), it continues a saved campaign or battle when
 * there is one (mission_setup_try_continue_campaign or
 * mission_setup_try_continue_battle), clearing the five g_remoteBattle globals
 * when it does not, and plays a campaign's cutscenes through
 * cutscene_play_for_current_mission_phase(0); a network player whose cutscene
 * result is 0 leaves for the join screen. The modern build does all this in
 * xvt_campaign_task_enter_teams, which ends the program in place of the CD
 * dialogs, and returns 0 until that returns 1. A pending debriefing transition
 * then is cleared and goes straight to flight assignment. Otherwise it clears
 * the drag and the reservations, loads the mission, and either clears the
 * assignments and recounts the teams or, with g_frontend_skip_screen_entry_setup
 * set, keeps them and prunes them. One team with one ready player puts roster
 * entry 0 alone on team 0 and goes to flight assignment (back to
 * mission_setup_update instead when skipping the entry setup). A solo combat
 * engagement or battle puts the pilot on team current_faction_id ^ 1 and goes to
 * the battle choice or flight assignment; a solo game with more teams,
 * g_frontend_skip_screen_entry_setup and g_mission_setup_team_assignment_skipped set
 * goes back to mission_setup_update. In other cases the ready players are dealt
 * into the teams' free slots, one per team per round, in g_mp_roster order. A
 * melee whose teams have one slot each, a solo Quick Start, and a training
 * mission with one team of 8 slots also go straight to flight assignment. Every
 * one of these skips past the teams, the debriefing one included, sets
 * g_mission_setup_team_assignment_skipped. Else it draws the screen's base with a
 * captain and slot overlay per team slot and allocates and loads g_mission_text.
 * Every frame it draws the mission name, title, help text, unassigned players,
 * the team slots or the mission description, the chat panel and the pilot
 * banner. Outside a solo game it handles one packet: a host cancel leaves for
 * the join screen; a lobby state compacts g_mp_roster and prunes the teams; the
 * final team assignments set g_pilot_data.team and go to the battle choice or
 * flight assignment; a return to setup goes back to mission_setup_update; the
 * reservation packets update g_mission_setup_reserved_player_ids; and new or
 * cleared assignments clear the reservations and end any drag. Previous, for
 * the host or a solo player, returns a solo game to mission_setup_update and has
 * a host send every player RETURN_TO_SETUP; for a campaign it first shows a
 * confirm dialog whose answer neither build reads. A client's Leave asks, then
 * tells the host and returns to the join screen. When the teams are valid, Next
 * sets the solo pilot's team and goes to the battle choice or flight
 * assignment, or for a host sends the final team assignments. Last it handles
 * the team controls, draws a dragged name at the cursor and, when a click ends
 * the drag, outside a solo game sends a RELEASE_TEAM_RESERVATION. Returns 1
 * when frontend_handle_common_screen_controls returns 1 or the original build's CD
 * dialog is cancelled; else 0. Does not check that the teams have a free slot
 * for each ready player: the dealing then never ends. */
// FUNCTION: XVT 0x4F1CC0
int mission_setup_team_assignment_update(int frame_counter)
{
	enum {
		MAX_PLAYERS = 8,
		MAX_TEAMS = 10,
		BRIEFING_TEXT_CAPACITY = 4096,
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
		DRAG_INPUT_GATE = 2,
	};

#ifndef XVT_MODERN
	int cutscene_result;
#endif
	struct RECT rect;

	int ready_player_count;
	int team_index;
	int slot_index;
	if (frame_counter == 0) {
#ifdef XVT_MODERN
		if (xvt_campaign_task_enter_teams() != 1) {
			return 0;
		}
#else
		g_frontend_chat_team_only = 0;
		g_mission_setup_show_description_panel = 0;
		g_frontend_first_visible_line = 0;
		while (file_check_game_cd_present(g_skip_movie_checks) == 0) {
			cd_audio_initialize();
			if (frontend_dialog_show_confirm_dialog(
				    frontend_string_get(
					    FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
				    frontend_string_get(
					    FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
				    frontend_string_get(
					    FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
				    frontend_string_get(FRONTSTR_523_OKAY),
				    frontend_string_get(FRONTSTR_019_CANCEL)) ==
			    0) {
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (net_is_host() != 0) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_HOST_CANCELLED;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type));
					} else {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_PLAYER_LEFT;
						net_send_packet_and_flush(
							net_get_host_player_id(),
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type));
					}
					net_shutdown_direct_play_session_for_quit();
				}
				return 1;
			}
		}
		frontend_check_host_cd_present();
		if (g_host_cd_available == 0 &&
		    g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_NET_CLIENT) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_749_YOU_SWITCHED_TO_A_CLIENT_CD),
				frontend_string_get(
					FRONTSTR_750_YOU_CANNOT_HOST_A_NETWORK_GAME),
				frontend_string_get(
					FRONTSTR_751_OR_FLY_SOLO_WITH_THE_CLIENT_CD),
				NULL, NULL);
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_HOST_CANCELLED;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					sizeof(g_frontend_net_packet_scratch
						       .packet_type));
				net_shutdown_direct_play_session();
			}
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
			return 0;
		}

		if (g_frontend_skip_screen_entry_setup == 0 &&
		    g_mission_setup_debrief_transition !=
			    MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION &&
		    g_pilot_data.mission_sequence_active == 1) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				if (mission_setup_try_continue_campaign() ==
				    0) {
					g_remote_battle_continuation_active = 0;
					g_remote_battle_sequence_continuation_choice =
						0;
					g_remote_battle_last_completed_mission_index =
						0;
					g_remote_battle_rebel_victory_count = 0;
					g_remote_battle_imperial_victory_count =
						0;
				}
				cutscene_result =
					cutscene_play_for_current_mission_phase(
						0);
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    cutscene_result == 0) {
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_PLAYER_LEFT;
					net_send_packet_and_flush(
						net_get_host_player_id(),
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type));
					net_shutdown_direct_play_session();
					frontend_screen_set_callbacks(
						frontend_net_join_game_screen,
						(frontend_screen_exit_fn)
							frontend_mission_list_free_screen_resources);
					return 0;
				}
			} else if (
				g_pilot_data.mission_directory_id ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				mission_setup_try_continue_battle() == 0) {
				g_remote_battle_continuation_active = 0;
				g_remote_battle_sequence_continuation_choice =
					0;
				g_remote_battle_last_completed_mission_index =
					0;
				g_remote_battle_rebel_victory_count = 0;
				g_remote_battle_imperial_victory_count = 0;
			}
		}
#endif
		if (g_mission_setup_debrief_transition !=
		    MISSION_SETUP_DEBRIEF_TRANSITION_NONE) {
			g_mission_setup_debrief_transition =
				MISSION_SETUP_DEBRIEF_TRANSITION_NONE;
			g_mission_setup_team_assignment_skipped = 1;
			frontend_screen_set_callbacks(
				mission_setup_flight_assignment_update,
				mission_setup_free_screen_resources);
			return 0;
		}

		frontend_cursor_set_pos(37, 445);
		g_mission_setup_dragged_player_id = 0;
		g_mission_setup_debrief_transition =
			MISSION_SETUP_DEBRIEF_TRANSITION_NONE;
		g_mission_setup_reserved_player_count = 0;
		memset(g_mission_setup_reserved_player_ids, 0,
		       sizeof(g_mission_setup_reserved_player_ids));
		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			for (g_selected_mission_list_index = 0;
			     (unsigned int)g_selected_mission_list_index <
			     g_mission_count;
			     ++g_selected_mission_list_index) {
				if (g_mission_list
					    [g_selected_mission_list_index]
						    .mission_idx ==
				    g_pilot_data.mission_description_ids
					    [g_pilot_data
						     .mission_directory_id]) {
					break;
				}
			}
		}
		frontend_mission_load_current();
		if (g_frontend_skip_screen_entry_setup == 0) {
			memset(g_mission_setup_player_assignments
				       .team_player_ids,
			       0,
			       sizeof(g_mission_setup_player_assignments
					      .team_player_ids));
			memset(g_mission_setup_player_assignments
				       .assigned_player_ids,
			       0,
			       sizeof(g_mission_setup_player_assignments
					      .assigned_player_ids));
			mission_setup_update_team_counts();
		} else {
			mission_setup_prune_team_assignments();
		}

		int assigned_player_count;
		if (g_team_count == 1) {
			ready_player_count = 1;
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				ready_player_count = net_count_ready_players();
			}
			if (ready_player_count == 1) {
				if (g_frontend_skip_screen_entry_setup == 1) {
					frontend_screen_set_callbacks(
						mission_setup_update,
						(frontend_screen_exit_fn)
							mission_setup_exit);
					g_frontend_skip_screen_entry_setup = 0;
					return 0;
				}
				g_mission_setup_player_assignments
					.team_player_ids[0][0] =
					g_mp_roster[0].player_id;
				g_mission_setup_player_assignments
					.assigned_player_ids[0] =
					g_mp_roster[0].player_id;
				g_pilot_data.team = 0;
				frontend_screen_set_callbacks(
					mission_setup_flight_assignment_update,
					mission_setup_free_screen_resources);
				g_mission_setup_team_assignment_skipped = 1;
				g_frontend_skip_screen_entry_setup = 0;
				return 0;
			}
			if (g_frontend_skip_screen_entry_setup == 0 &&
			    g_team_count != 0) {
				team_index = 0;
				assigned_player_count = 0;
				do {
					for (slot_index = 0;
					     slot_index <
					     g_team_player_flight_group_count
						     [team_index];
					     ++slot_index) {
						if (g_mission_setup_player_assignments
							    .team_player_ids
								    [team_index]
								    [slot_index] ==
						    0) {
							g_mission_setup_player_assignments
								.assigned_player_ids
									[assigned_player_count] =
								g_mp_roster[assigned_player_count]
									.player_id;
							g_mission_setup_player_assignments
								.team_player_ids
									[team_index]
									[slot_index] =
								g_mp_roster[assigned_player_count]
									.player_id;
							++assigned_player_count;
							break;
						}
					}
					if (++team_index >= g_team_count) {
						team_index = 0;
					}
				} while (ready_player_count >
					 assigned_player_count);
			}
		} else {
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_frontend_skip_screen_entry_setup == 0) {
					if (g_pilot_data.mission_directory_id ==
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
					    g_pilot_data.mission_directory_id ==
						    MISSION_DIRECTORY_BATTLES) {
						team_index =
							g_pilot_data
								.current_faction_id ^
							1;
						g_mission_setup_player_assignments
							.assigned_player_ids
								[0] =
							g_mp_roster[0]
								.player_id;
						g_pilot_data.team = team_index;
						g_mission_setup_player_assignments
							.team_player_ids
								[team_index]
								[0] =
							g_mp_roster[0]
								.player_id;
						int use_battle_choice = 0;
						if (g_pilot_data.mission_directory_id ==
							    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
						    g_pilot_data.mission_sequence_active ==
							    1 &&
						    g_game_config.random_setup ==
							    2 &&
						    g_pilot_data.battle_sequence_state
								    .current_mission_index >
							    0 &&
						    g_pilot_data.battle_sequence_state
								    .mission_results
									    [g_pilot_data
										     .battle_sequence_state
										     .current_mission_index -
									     1] !=
							    (battle_mission_result)
								    team_index) {
							use_battle_choice = 1;
						}
						if (use_battle_choice != 0) {
							frontend_screen_set_callbacks(
								mission_setup_battle_choice_update,

#ifdef XVT_MODERN
								xvt_frontend_cleanup_battle_choice
#else
								(frontend_screen_exit_fn)
									mission_setup_battle_choice_exit
#endif
							);
						} else {
							frontend_screen_set_callbacks(
								mission_setup_flight_assignment_update,
								mission_setup_free_screen_resources);
						}
						g_mission_setup_team_assignment_skipped =
							1;
						g_frontend_skip_screen_entry_setup =
							0;
						return 0;
					}
				} else if (
					g_mission_setup_team_assignment_skipped !=
					0) {
					frontend_screen_set_callbacks(
						mission_setup_update,
						(frontend_screen_exit_fn)
							mission_setup_exit);
					g_frontend_skip_screen_entry_setup = 0;
					return 0;
				}
			}
			if (g_frontend_skip_screen_entry_setup == 0) {
				ready_player_count = 1;
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					ready_player_count =
						net_count_ready_players();
				}
				if (g_team_count != 0) {
					team_index = 0;
					assigned_player_count = 0;
					do {
						for (slot_index = 0;
						     slot_index <
						     g_team_player_flight_group_count
							     [team_index];
						     ++slot_index) {
							if (g_mission_setup_player_assignments
								    .team_player_ids
									    [team_index]
									    [slot_index] ==
							    0) {
								g_mission_setup_player_assignments
									.assigned_player_ids
										[assigned_player_count] =
									g_mp_roster[assigned_player_count]
										.player_id;
								g_mission_setup_player_assignments
									.team_player_ids
										[team_index]
										[slot_index] =
									g_mp_roster[assigned_player_count]
										.player_id;
								++assigned_player_count;
								break;
							}
						}
						if (++team_index >=
						    g_team_count) {
							team_index = 0;
						}
					} while (ready_player_count >
						 assigned_player_count);
				}
			}
		}

		g_frontend_skip_screen_entry_setup = 0;
		g_mission_setup_team_assignment_skipped = 0;
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (team_index = 0; team_index < g_team_count;
			     ++team_index) {
				if (g_mission_setup_player_assignments
					    .team_player_ids[team_index][0] ==
				    1) {
					g_pilot_data.team = team_index;
					break;
				}
			}
		}
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_MELEES) {
			for (team_index = 0; team_index < g_team_count;
			     ++team_index) {
				if (g_team_player_flight_group_count
					    [team_index] > 1) {
					break;
				}
			}
			if (team_index == g_team_count) {
				g_pilot_data.team = 0;
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					for (team_index = 0;
					     team_index < g_team_count;
					     ++team_index) {
						for (slot_index = 0;
						     slot_index <
						     g_team_player_flight_group_count
							     [team_index];
						     ++slot_index) {
							if (net_get_local_player_id() ==
							    g_mission_setup_player_assignments
								    .team_player_ids
									    [team_index]
									    [slot_index]) {
								g_pilot_data
									.team =
									team_index;
							}
						}
					}
				}
				g_frontend_skip_screen_entry_setup = 0;
				frontend_screen_set_callbacks(
					mission_setup_flight_assignment_update,
					mission_setup_free_screen_resources);
				g_mission_setup_team_assignment_skipped = 1;
				return 0;
			}
		}
		if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    g_frontend_quick_start_launch_flag == 1) {
			frontend_screen_set_callbacks(
				mission_setup_flight_assignment_update,
				mission_setup_free_screen_resources);
			g_mission_setup_team_assignment_skipped = 1;
			g_frontend_skip_screen_entry_setup = 0;
			return 0;
		}
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_team_count == 1 &&
		    g_team_player_flight_group_count[0] == MAX_PLAYERS) {
			for (team_index = 0; team_index < g_team_count;
			     ++team_index) {
				for (slot_index = 0; slot_index < MAX_PLAYERS;
				     ++slot_index) {
					if (net_get_local_player_id() ==
					    g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [slot_index]) {
						g_pilot_data.team = team_index;
						break;
					}
				}
			}
			frontend_screen_set_callbacks(
				mission_setup_flight_assignment_update,
				mission_setup_free_screen_resources);
			g_mission_setup_team_assignment_skipped = 1;
			g_frontend_skip_screen_entry_setup = 0;
			return 0;
		}

		front_image_register_resource_default("frontres\\soloteam.bmp",
						      "background");
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		if (g_host_cd_available != 0) {
			front_image_draw_sprite("allactive", 0, 0);
		} else {
			front_image_draw_sprite("clientactive", 0, 0);
		}
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			front_image_draw_sprite_translucent("chatbox", 0, 0);
		}
		front_image_draw_sprite_translucent("teamoverlay", 0, 0);
		frontend_draw_rect_assign(&rect, 88, 207, 256, 221);
		for (team_index = 0; team_index < g_team_count; ++team_index) {
			/* MAX_PLAYERS stands for team counts here: with 7 or 8
			 * teams, teams 4 and up move to a second column. */
			if ((g_team_count == MAX_PLAYERS ||
			     g_team_count == MAX_PLAYERS - 1) &&
			    team_index == 4) {
				frontend_draw_rect_assign(&rect, 260, 207, 428,
							  221);
			}
			frontend_draw_rect_offset_xy(&rect, 0, 15);
			for (slot_index = 0;
			     slot_index <
			     g_team_player_flight_group_count[team_index];
			     ++slot_index) {
				if (slot_index == 0) {
					front_image_draw_sprite_translucent(
						"captoverlay", rect.left,
						rect.top);
				} else {
					front_image_draw_sprite_translucent(
						"slotoverlay", rect.left,
						rect.top);
				}
				frontend_draw_rect_offset_xy(&rect, 0, 15);
			}
		}
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
		g_mission_text = malloc(BRIEFING_TEXT_CAPACITY);
		mission_setup_load_mission_desc_text(g_mission_text);
	}

	frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
	struct RECT saved_clip_rect;
	frontend_display_get_screen_clip_rect(&saved_clip_rect);
	frontend_display_set_screen_clip_rect640x480(&rect);
	sprintf(g_frontend_scratch_buffer, "%c%s", 4,
		g_mission_list[g_selected_mission_list_index].description);
	for (int text_index = (int)strlen(g_frontend_scratch_buffer) - 1;
	     text_index > 0; --text_index) {
		if (g_frontend_scratch_buffer[text_index] == '(') {
			g_frontend_scratch_buffer[text_index] = '\0';
			break;
		}
	}
	frontend_text_draw_centered(12, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	frontend_display_set_screen_clip_rect640x480(&saved_clip_rect);
	frontend_draw_rect_assign(&rect, 84, 90, 434, 108);
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		frontend_text_draw_centered(
			15, frontend_string_get(FRONTSTR_466_CHOOSE_SIDES),
			&rect, 0xFFFF);
	} else {
		frontend_text_draw_centered(
			15, frontend_string_get(FRONTSTR_465_CHOOSE_TEAMS),
			&rect, 0xFFFF);
	}
	if (g_mission_setup_show_description_panel == 0) {
		frontend_draw_rect_assign(&rect, 84, 400, 434, 414);
		if (net_is_host() == 0) {
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				for (team_index = 0; team_index < g_team_count;
				     ++team_index) {
					if (net_get_local_player_id() ==
					    g_mission_setup_player_assignments
						    .team_player_ids[team_index]
								    [0]) {
						frontend_text_draw_centered(
							12,
							frontend_string_get(
								FRONTSTR_741_YOU_ARE_A_TEAM_CAPTAIN),
							&rect, g_color_red);
					}
				}
				frontend_draw_rect_offset_xy(&rect, 0, 15);
				frontend_text_draw_centered(
					12,
					frontend_string_get(
						FRONTSTR_469_PLEASE_WAIT_WHILE_THE_HOST_PICKS_TEAMS),
					&rect, g_color_red);
			} else {
				frontend_draw_rect_offset_xy(&rect, 0, 15);
				frontend_text_draw_centered(
					12,
					frontend_string_get(
						FRONTSTR_467_ASSIGN_TEAMS_BY_DRAGGING_PLAYERS_NAMES_INTO_TEAM_SLOTS),
					&rect, g_color_green);
			}
		} else if (g_frontend_mission_session_mode !=
			   FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			frontend_text_draw_centered(
				12,
				frontend_string_get(
					FRONTSTR_468_YOU_ARE_THE_HOST),
				&rect, g_color_green);
		} else {
			frontend_draw_rect_offset_xy(&rect, 0, 15);
			frontend_text_draw_centered(
				12,
				frontend_string_get(
					FRONTSTR_467_ASSIGN_TEAMS_BY_DRAGGING_PLAYERS_NAMES_INTO_TEAM_SLOTS),
				&rect, g_color_green);
		}
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		int packet_type = frontend_net_process_network_packets();
		if (packet_type == NET_PACKET_HOST_CANCELLED) {
			if (net_is_host() == 0) {
				frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					frontend_string_get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					frontend_string_get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return xvt_dialog_continue_with(
					xvt_mission_dialogs_resume,
					XVT_MISSION_TEAM_CANCELLED);
#endif
			}
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
			net_shutdown_direct_play_session();
		} else if (packet_type == NET_PACKET_STATE) {
			mp_roster_compact_active_entries();
			mission_setup_prune_team_assignments();
		} else if (packet_type == NET_PACKET_TEAM_ASSIGNMENTS_READY) {
			for (team_index = 0; team_index < g_team_count;
			     ++team_index) {
				for (slot_index = 0; slot_index < MAX_PLAYERS;
				     ++slot_index) {
					if (net_get_local_player_id() ==
					    g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [slot_index]) {
						g_pilot_data.team = team_index;
						break;
					}
				}
			}
			if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
			    g_pilot_data.mission_sequence_active == 1 &&
			    g_game_config.random_setup == 2 &&
			    g_pilot_data.battle_sequence_state
					    .current_mission_index > 0) {
				if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
				    net_count_ready_players() != 1) {
					frontend_screen_set_callbacks(
						mission_setup_battle_choice_update,

#ifdef XVT_MODERN
						xvt_frontend_cleanup_battle_choice
#else
						(frontend_screen_exit_fn)
							mission_setup_battle_choice_exit
#endif
					);
					return 0;
				}
				if (g_pilot_data.battle_sequence_state.mission_results
					    [g_pilot_data.battle_sequence_state
						     .current_mission_index -
					     1] !=
				    (battle_mission_result)g_pilot_data.team) {
					frontend_screen_set_callbacks(
						mission_setup_battle_choice_update,

#ifdef XVT_MODERN
						xvt_frontend_cleanup_battle_choice
#else
						(frontend_screen_exit_fn)
							mission_setup_battle_choice_exit
#endif
					);
					return 0;
				}
			}
			frontend_screen_set_callbacks(
				mission_setup_flight_assignment_update,
				mission_setup_free_screen_resources);
			return 0;
		} else if (packet_type == NET_PACKET_RETURN_TO_SETUP) {
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				mission_setup_update,
				(frontend_screen_exit_fn)mission_setup_exit);
			return 0;
		} else if (packet_type == NET_PACKET_RELEASE_TEAM_RESERVATION) {
			for (slot_index = 0;
			     slot_index < g_mission_setup_reserved_player_count;
			     ++slot_index) {
				if (g_mission_setup_reserved_player_ids
					    [slot_index] ==
				    g_frontend_net_packet_arg0) {
					--g_mission_setup_reserved_player_count;
					break;
				}
			}
			for (; slot_index < MAX_PLAYERS - 1; ++slot_index) {
				g_mission_setup_reserved_player_ids[slot_index] =
					g_mission_setup_reserved_player_ids
						[slot_index + 1];
			}
			g_mission_setup_reserved_player_ids[MAX_PLAYERS - 1] =
				0;
		} else if (packet_type == NET_PACKET_TEAM_RESERVATION) {
			for (slot_index = 0;
			     slot_index < g_mission_setup_reserved_player_count;
			     ++slot_index) {
				if (g_mission_setup_reserved_player_ids
					    [slot_index] ==
				    g_frontend_net_packet_arg0) {
					break;
				}
			}
			if (slot_index ==
			    g_mission_setup_reserved_player_count) {
				g_mission_setup_reserved_player_ids
					[g_mission_setup_reserved_player_count++] =
						g_frontend_net_packet_arg0;
			}
		} else if (packet_type == NET_PACKET_TEAM_ASSIGNMENTS ||
			   packet_type == NET_PACKET_CLEAR_TEAM_ASSIGNMENTS) {
			g_mission_setup_reserved_player_count = 0;
			memset(g_mission_setup_reserved_player_ids, 0,
			       sizeof(g_mission_setup_reserved_player_ids));
			g_mission_setup_dragged_player_id = 0;
			if (frontend_mouse_is_gate_owner(DRAG_INPUT_GATE)) {
				frontend_mouse_clear_input_gate();
			}
		}
	}

	mission_setup_draw_unassigned_players(frame_counter);
	if (g_mission_setup_show_description_panel == 0) {
		mission_setup_draw_team_assignments(frame_counter);
	} else {
		mission_setup_draw_team_mission_description();
	}
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_net_update_and_draw_chat_panel(frame_counter);
	}
	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
		int animation_frame;
		if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_MELEES &&
		    g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_TOURNAMENTS) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				for (int flight_group_index = 0;
				     flight_group_index <
				     (int16_t)g_frontend_mission
					     .flight_group_count;
				     ++flight_group_index) {
					if (g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .player_number != 0) {
						if (g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .iff == 0) {
							animation_frame =
								(frame_counter %
								 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
								1;
							sprintf(g_frontend_scratch_buffer,
								"rebtiny%d",
								animation_frame);
							front_image_draw_sprite(
								g_frontend_scratch_buffer,
								204, 453);
							front_image_draw_sprite(
								g_frontend_scratch_buffer,
								420, 453);
						} else if (
							g_frontend_mission
								.flight_groups
									[flight_group_index]
								.iff == 1) {
							animation_frame =
								(frame_counter %
								 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
								1;
							sprintf(g_frontend_scratch_buffer,
								"imptiny%d",
								animation_frame);
							front_image_draw_sprite(
								g_frontend_scratch_buffer,
								204, 453);
							front_image_draw_sprite(
								g_frontend_scratch_buffer,
								420, 453);
						} else {
							animation_frame =
								(frame_counter %
								 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
								1;
							sprintf(g_frontend_scratch_buffer,
								"rebtiny%d",
								animation_frame);
							front_image_draw_sprite(
								g_frontend_scratch_buffer,
								204, 453);
							sprintf(g_frontend_scratch_buffer,
								"imptiny%d",
								animation_frame);
							front_image_draw_sprite(
								g_frontend_scratch_buffer,
								420, 453);
						}
						break;
					}
				}
			} else {
				int local_player_id = net_get_local_player_id();
				for (team_index = 0; team_index < g_team_count;
				     ++team_index) {
					for (slot_index = 0;
					     slot_index <
					     g_team_player_flight_group_count
						     [team_index];
					     ++slot_index) {
						if (g_mission_setup_player_assignments
							    .team_player_ids
								    [team_index]
								    [slot_index] ==
						    local_player_id) {
							break;
						}
					}
					if (slot_index <
					    g_team_player_flight_group_count
						    [team_index]) {
						break;
					}
				}
				if (team_index != g_team_count) {
					if (team_index == 0) {
						animation_frame =
							(frame_counter %
							 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1;
						sprintf(g_frontend_scratch_buffer,
							"imptiny%d",
							animation_frame);
					} else {
						animation_frame =
							(frame_counter %
							 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1;
						sprintf(g_frontend_scratch_buffer,
							"rebtiny%d",
							animation_frame);
					}
					front_image_draw_sprite(
						g_frontend_scratch_buffer, 204,
						453);
					front_image_draw_sprite(
						g_frontend_scratch_buffer, 420,
						453);
				}
			}
		} else {
			animation_frame =
				(frame_counter %
				 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
				1;
			sprintf(g_frontend_scratch_buffer, "rebtiny%d",
				animation_frame);
			front_image_draw_sprite(g_frontend_scratch_buffer, 204,
						453);
			sprintf(g_frontend_scratch_buffer, "imptiny%d",
				animation_frame);
			front_image_draw_sprite(g_frontend_scratch_buffer, 420,
						453);
		}
	}

	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	if (net_is_host() != 0 ||
	    g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_569_PREVIOUS));
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			if (g_pilot_data.mission_sequence_active == 1 &&
			    g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
					frontend_string_get(
						FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
					frontend_string_get(
						FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
					frontend_string_get(FRONTSTR_523_OKAY),
					frontend_string_get(
						FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
				return xvt_dialog_continue_with(
					xvt_mission_dialogs_resume,
					XVT_MISSION_TEAM_PREVIOUS);
#endif
			}
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_RETURN_TO_SETUP;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					sizeof(g_frontend_net_packet_scratch
						       .packet_type));
			}
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				frontend_screen_set_callbacks(
					mission_setup_update,
					(frontend_screen_exit_fn)
						mission_setup_exit);
			}
		}
	} else {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_569_PREVIOUS));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				frontend_string_get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				frontend_string_get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_TEAM_CLIENT_LEAVE);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    frontend_string_get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    frontend_string_get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_LEFT;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type));
			net_shutdown_direct_play_session();
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
		}
#endif
	}

	frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (mission_setup_is_team_assignment_valid()) {
			frontend_button_set_overlay_text(
				frontend_string_get(FRONTSTR_212_NEXT));
			if (frontend_button_handle_sprite_button(
				    &rect, "nextup", "nextdown",
				    frontend_string_get(
					    FRONTSTR_475_GO_TO_BRIEFING),
				    12, 0, 7, "flysound") != 0) {
				for (team_index = 0; team_index < g_team_count;
				     ++team_index) {
					for (slot_index = 0;
					     slot_index < MAX_PLAYERS;
					     ++slot_index) {
						if (g_mission_setup_player_assignments
							    .team_player_ids
								    [team_index]
								    [slot_index] ==
						    1) {
							g_pilot_data.team =
								team_index;
							break;
						}
					}
				}
				if (g_pilot_data.mission_directory_id ==
					    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				    g_pilot_data.mission_sequence_active == 1 &&
				    g_game_config.random_setup == 2 &&
				    g_pilot_data.battle_sequence_state
						    .current_mission_index >
					    0 &&
				    g_pilot_data.battle_sequence_state.mission_results
						    [g_pilot_data
							     .battle_sequence_state
							     .current_mission_index -
						     1] !=
					    (battle_mission_result)
						    g_pilot_data.team) {
					frontend_screen_set_callbacks(
						mission_setup_battle_choice_update,

#ifdef XVT_MODERN
						xvt_frontend_cleanup_battle_choice
#else
						(frontend_screen_exit_fn)
							mission_setup_battle_choice_exit
#endif
					);
					return 0;
				}
				frontend_screen_set_callbacks(
					mission_setup_flight_assignment_update,
					mission_setup_free_screen_resources);
				frontend_button_disable_overlay_text();
				return 0;
			}
		}
	} else if (net_is_host() != 0 &&
		   mission_setup_is_team_assignment_valid()) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_212_NEXT));
		if (frontend_button_handle_sprite_button(
			    &rect, "nextup", "nextdown",
			    frontend_string_get(FRONTSTR_475_GO_TO_BRIEFING),
			    12, 0, 7, "flysound") != 0) {
			mission_setup_broadcast_team_assignments();
		}
	}
	frontend_button_disable_overlay_text();
	mission_setup_update_team_controls();
	if (frontend_handle_common_screen_controls(1) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (xvt_dialog_is_active()) {
		return 0;
	}
#endif

	int cursor_x;
	int cursor_y;
	if (frontend_mouse_is_gate_owner(DRAG_INPUT_GATE)) {
		if (frontend_mouse_get_left_click_for(DRAG_INPUT_GATE) == 0 &&
		    frontend_mouse_get_right_click_for(DRAG_INPUT_GATE) == 0) {
			/* ready_player_count holds the number of roster slots
			 * to search here (1, or all eight), not a count of
			 * ready players. */
			ready_player_count =
				g_frontend_mission_session_mode ==
						FRONTEND_MISSION_SESSION_SINGLEPLAYER
					? 1
					: MAX_PLAYERS;
			for (int roster_index = 0;
			     roster_index < ready_player_count;
			     ++roster_index) {
				if (g_mp_roster[roster_index].player_id !=
				    g_mission_setup_dragged_player_id) {
					continue;
				}
				frontend_cursor_get_pos(&cursor_x, &cursor_y);
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				if (net_get_local_player_id() ==
					    g_mp_roster[roster_index]
						    .player_id ||
				    g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					frontend_text_draw(
						12, g_frontend_scratch_buffer,
						cursor_x - 7, cursor_y - 7,
						g_pulse_color_ramp
							[((frame_counter % 24) &
							  ~1) >>
							 1]);
				} else {
					frontend_text_draw(
						12, g_frontend_scratch_buffer,
						cursor_x - 7, cursor_y - 7,
						g_color_yellow);
				}
				return 0;
			}
			return 0;
		}
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			*(int *)g_frontend_net_packet_scratch.payload =
				g_mission_setup_dragged_player_id;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_RELEASE_TEAM_RESERVATION;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				2 * sizeof(g_frontend_net_packet_scratch
						   .packet_type));
		}
		frontend_mouse_clear_input_gate();
		g_mission_setup_dragged_player_id = 0;
	}
	return 0;
}

/* Draws the Unassigned Players list of the team assignment screen: each of the
 * first ready_count g_mp_roster entries (1 in a solo game, else
 * net_count_ready_players()) whose player is in no assigned_player_ids slot and is
 * not being dragged, four to a column. A player in
 * g_mission_setup_reserved_player_ids is gray, the local player (every player in a
 * solo game) pulses, others are yellow. The host or a solo player starts a drag
 * by pressing a mouse button on a name while the drag gate (2) is free: a solo
 * game sets g_mission_setup_dragged_player_id and takes the gate; a host does so
 * only for a player not reserved, also adding it to
 * g_mission_setup_reserved_player_ids, sending every player a TEAM_RESERVATION
 * (player and host ids) and a TEAM_ASSIGNMENT to team 10, and taking the player
 * out of assigned_player_ids and out of every team, shifting the later slots
 * down. Returns 1. */
// FUNCTION: XVT 0x4F32B0
int mission_setup_draw_unassigned_players(int frame_counter)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 114, 430, 128);
	frontend_text_draw_aligned_in_rect(
		15, frontend_string_get(FRONTSTR_474_UNASSIGNED_PLAYERS), &rect,
		0, 1, 0xFFFF);
	int ready_player_count = 1;
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		ready_player_count = net_count_ready_players();
	}
	int displayed_player_count = 0;
	frontend_draw_rect_assign(&rect, 88, 134, 258, 148);
	struct RECT previous_clip_rect;
	int index;
	int cursor_x;
	int cursor_y;
	if (ready_player_count > 0) {
		int reserved_player_count =
			g_mission_setup_reserved_player_count;
		for (int roster_index = 0; roster_index < ready_player_count;
		     ++roster_index) {
			int player_id = g_mp_roster[roster_index].player_id;
			for (index = 0;
			     index <
			     (int)(sizeof(g_mission_setup_player_assignments
						  .assigned_player_ids) /
				   sizeof(g_mission_setup_player_assignments
						  .assigned_player_ids[0]));
			     ++index) {
				if (g_mission_setup_player_assignments
					    .assigned_player_ids[index] ==
				    player_id) {
					break;
				}
			}
			if (index !=
				    (int)(sizeof(g_mission_setup_player_assignments
							 .assigned_player_ids) /
					  sizeof(g_mission_setup_player_assignments
							 .assigned_player_ids
								 [0])) ||
			    g_mission_setup_dragged_player_id == player_id) {
				continue;
			}
			for (index = 0; index < reserved_player_count;
			     ++index) {
				if (g_mission_setup_reserved_player_ids
					    [index] == player_id) {
					break;
				}
			}
			frontend_display_get_screen_clip_rect(
				&previous_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&rect);
			if (index == g_mission_setup_reserved_player_count) {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				int local_player_id = net_get_local_player_id();
				int text_color = g_color_yellow;
				int session_mode =
					g_frontend_mission_session_mode;
				if (player_id == local_player_id ||
				    session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					frontend_text_draw_aligned_in_rect(
						12, g_frontend_scratch_buffer,
						&rect, 0, 1,
						g_pulse_color_ramp
							[((frame_counter % 24) &
							  ~1) >>
							 1]);
				} else {
					frontend_text_draw_aligned_in_rect(
						12, g_frontend_scratch_buffer,
						&rect, 0, 1, text_color);
				}
			} else {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					g_mp_roster[roster_index].name);
				frontend_text_draw_aligned_in_rect(
					12, g_frontend_scratch_buffer, &rect, 0,
					1, g_color_gray);
			}
			++displayed_player_count;
			frontend_display_set_screen_clip_rect640x480(
				&previous_clip_rect);
			if ((net_is_host() != 0 ||
			     g_frontend_mission_session_mode ==
				     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
			    !frontend_mouse_is_gate_owner(2)) {
				frontend_cursor_get_pos(&cursor_x, &cursor_y);
				if ((frontend_mouse_get_left_down() != 0 ||
				     frontend_mouse_get_right_down() != 0) &&
				    frontend_draw_point_in_rect(&rect, cursor_x,
								cursor_y)) {
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_mission_setup_dragged_player_id =
							player_id;
						frontend_mouse_set_input_gate(
							2);
					} else {
						for (index = 0;
						     index <
						     g_mission_setup_reserved_player_count;
						     ++index) {
							if (g_mission_setup_reserved_player_ids
								    [index] ==
							    player_id) {
								break;
							}
						}
						if (index ==
						    g_mission_setup_reserved_player_count) {
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_TEAM_RESERVATION;
							++g_mission_setup_reserved_player_count;
							g_mission_setup_reserved_player_ids
								[index] =
									player_id;
							*(int *)g_frontend_net_packet_scratch
								 .payload =
								player_id;
							*(int *)(g_frontend_net_packet_scratch
									 .payload +
								 sizeof(int)) =
								net_get_host_player_id();
							net_send_packet_and_flush(
								0,
								&g_frontend_net_packet_scratch,
								3 * sizeof(int));
							g_mission_setup_dragged_player_id =
								player_id;
							frontend_mouse_set_input_gate(
								2);
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_TEAM_ASSIGNMENT;
							*(int *)g_frontend_net_packet_scratch
								 .payload =
								g_mission_setup_dragged_player_id;
							*(int *)(g_frontend_net_packet_scratch
									 .payload +
								 sizeof(int)) =
								10;
							net_send_packet_and_flush(
								0,
								&g_frontend_net_packet_scratch,
								3 * sizeof(int));
							for (index = 0;
							     index <
							     (int)(sizeof(g_mission_setup_player_assignments
										  .assigned_player_ids) /
								   sizeof(g_mission_setup_player_assignments
										  .assigned_player_ids
											  [0]));
							     ++index) {
								if (g_mission_setup_player_assignments
									    .assigned_player_ids
										    [index] ==
								    g_mission_setup_dragged_player_id) {
									g_mission_setup_player_assignments
										.assigned_player_ids
											[index] =
										0;
								}
							}
							for (int team = 0;
							     team <
							     g_team_count;
							     ++team) {
								int team_slot_count = g_team_player_flight_group_count
									[team];
								for (int slot =
									     0;
								     slot <
								     team_slot_count;
								     ++slot) {
									if (g_mission_setup_player_assignments
										    .team_player_ids
											    [team]
											    [slot] ==
									    g_mission_setup_dragged_player_id) {
										for (int shift =
											     slot +
											     1;
										     shift <
										     (int)(sizeof(g_mission_setup_player_assignments
													  .team_player_ids
														  [team]) /
											   sizeof(g_mission_setup_player_assignments
													  .team_player_ids
														  [team]
														  [0]));
										     ++shift) {
											g_mission_setup_player_assignments
												.team_player_ids
													[team]
													[shift -
													 1] =
												g_mission_setup_player_assignments
													.team_player_ids
														[team]
														[shift];
										}
										g_mission_setup_player_assignments
											.team_player_ids
												[team]
												[sizeof(g_mission_setup_player_assignments
														.team_player_ids
															[team]) /
													 sizeof(g_mission_setup_player_assignments
															.team_player_ids
																[team]
																[0]) -
												 1] =
											0;
									}
								}
							}
						}
					}
				}
			}
			if (displayed_player_count == 4) {
				frontend_draw_rect_assign(&rect, 259, 134, 430,
							  148);
			} else {
				frontend_draw_rect_offset_xy(&rect, 0, 15);
			}
			reserved_player_count =
				g_mission_setup_reserved_player_count;
		}
	}
	return 1;
}

/* Counts the player flight groups of the loaded mission (g_frontend_mission) by
 * team into g_team_player_flight_group_count, a flight group counting when its
 * player_number is nonzero, and sets g_team_count to the number of teams with at
 * least one. Does not check that a flight group's team is under 10. */
// FUNCTION: XVT 0x4F36D0
void mission_setup_update_team_counts(void)
{
	memset(g_team_player_flight_group_count, 0,
	       sizeof(g_team_player_flight_group_count));
	int active_team_count = 0;
	int flight_group_index;
	for (flight_group_index = 0;
	     *(int16_t *)&g_frontend_mission.flight_group_count >
	     flight_group_index;
	     ++flight_group_index) {
		if (g_frontend_mission.flight_groups[flight_group_index]
			    .player_number != 0) {
			++g_team_player_flight_group_count
				[g_frontend_mission
					 .flight_groups[flight_group_index]
					 .team];
		}
	}

	/* flight_group_index walks the ten teams here. */
	for (flight_group_index = 0; flight_group_index < 10;
	     ++flight_group_index) {
		if (g_team_player_flight_group_count[flight_group_index] != 0) {
			++active_team_count;
		}
		g_team_count = active_team_count;
	}
}

/* Draws the team slots of the team assignment screen and handles drops and
 * drags on them, in two columns when there are 7 or 8 teams. Each heading is
 * "Team n:" and the team's name, or for a combat engagement the name alone,
 * with the side's wins while a continued battle sequence is shown
 * (g_remote_battle_continuation_active, g_remote_battle_sequence_continuation_choice
 * and mission_sequence_active 1): g_remote_battle_imperial_victory_count for team 0,
 * g_remote_battle_rebel_victory_count for the others. A click on a slot while
 * dragging (gate 2) drops g_mission_setup_dragged_player_id: a player already in
 * that slot loses its assigned_player_ids slot (outside a solo game a
 * TEAM_ASSIGNMENT to team 10 is sent for it) and is replaced; on an empty slot
 * the player goes to the team's first empty slot up to it. The dropped player
 * gets an assigned_player_ids slot when it has none; outside a solo game a
 * TEAM_ASSIGNMENT (player, team, slot) and a RELEASE_TEAM_RESERVATION go to
 * every player; the drag ends. Each filled slot whose player is among the first
 * ready_count g_mp_roster entries shows its rating and name, pulsing for the
 * local player and in a solo game, else yellow. The host or a solo player
 * pressing a mouse button on a filled slot starts a drag of its player: a host
 * only for a player not reserved, reserving it, sending a TEAM_RESERVATION and
 * a TEAM_ASSIGNMENT to team 10, and taking it out of assigned_player_ids and its
 * team, shifting the later slots down; a solo game takes it out of its team
 * only when it holds an assigned_player_ids slot. */
// FUNCTION: XVT 0x4F3740
void mission_setup_draw_team_assignments(int frame_counter)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 90, 207, 256, 221);
	struct RECT saved_clip;
	int cursor_x;
	int cursor_y;
	int active_index;
	int shift;
	for (int team_index = 0; team_index < g_team_count; ++team_index) {
		if ((g_team_count == 8 || g_team_count == 7) &&
		    team_index == 4) {
			frontend_draw_rect_assign(&rect, 262, 207, 428, 221);
		}
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			if (g_remote_battle_continuation_active != 0 &&
			    g_remote_battle_sequence_continuation_choice != 0 &&
			    g_pilot_data.mission_sequence_active == 1) {
				if (team_index == 0) {
					sprintf(g_frontend_scratch_buffer,
						"%s - %c%s: %d",
						g_frontend_mission
							.teams[team_index]
							.name,
						4,
						frontend_string_get(
							FRONTSTR_776_WINS),
						g_remote_battle_imperial_victory_count);
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%s - %c%s: %d",
						g_frontend_mission
							.teams[team_index]
							.name,
						4,
						frontend_string_get(
							FRONTSTR_776_WINS),
						g_remote_battle_rebel_victory_count);
				}
			} else {
				sprintf(g_frontend_scratch_buffer, "%s",
					g_frontend_mission.teams[team_index]
						.name);
			}
		} else {
			sprintf(g_frontend_scratch_buffer, "%s %u: %s",
				frontend_string_get(FRONTSTR_217_TEAM),
				team_index + 1,
				g_frontend_mission.teams[team_index].name);
		}
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		for (int slot_index = 0;
		     slot_index < g_team_player_flight_group_count[team_index];
		     ++slot_index) {
			if (frontend_mouse_is_gate_owner(2) &&
			    (frontend_mouse_get_left_click_for(2) != 0 ||
			     frontend_mouse_get_right_click_for(2) != 0)) {
				frontend_cursor_get_pos(&cursor_x, &cursor_y);
				if (frontend_draw_point_in_rect(&rect, cursor_x,
								cursor_y)) {
					int replaced_player_id =
						g_mission_setup_player_assignments
							.team_player_ids
								[team_index]
								[slot_index];

					if (replaced_player_id != 0) {
						for (active_index = 0;
						     active_index < 8;
						     ++active_index) {
							if (g_mission_setup_player_assignments
								    .assigned_player_ids
									    [active_index] ==
							    replaced_player_id) {
								g_mission_setup_player_assignments
									.assigned_player_ids
										[active_index] =
									0;
							}
						}
						if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_TEAM_ASSIGNMENT;
							*(int *)&g_frontend_net_packet_scratch
								 .payload[0] =
								replaced_player_id;
							*(int *)&g_frontend_net_packet_scratch
								 .payload[4] =
								10;
							net_send_packet_and_flush(
								0,
								&g_frontend_net_packet_scratch,
								12);
						}
					}
					int target_slot;
					if (replaced_player_id == 0) {
						for (target_slot = 0;
						     target_slot < slot_index;
						     ++target_slot) {
							if (g_mission_setup_player_assignments
								    .team_player_ids
									    [team_index]
									    [target_slot] ==
							    0) {
								break;
							}
						}
					} else {
						target_slot = slot_index;
					}
					g_mission_setup_player_assignments
						.team_player_ids[team_index]
								[target_slot] =
						g_mission_setup_dragged_player_id;
					for (active_index = 0; active_index < 8;
					     ++active_index) {
						if (g_mission_setup_player_assignments
							    .assigned_player_ids
								    [active_index] ==
						    g_mission_setup_dragged_player_id) {
							break;
						}
					}
					if (active_index == 8) {
						for (active_index = 0;
						     active_index < 8;
						     ++active_index) {
							if (g_mission_setup_player_assignments
								    .assigned_player_ids
									    [active_index] ==
							    0) {
								g_mission_setup_player_assignments
									.assigned_player_ids
										[active_index] =
									g_mission_setup_dragged_player_id;
								break;
							}
						}
					}
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"slotsound", 1, 0, 255,
							12 * g_game_config
									.sfx_datapad_volume,
							63);
					}
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_TEAM_ASSIGNMENT;
						*(int *)&g_frontend_net_packet_scratch
							 .payload[0] =
							g_mission_setup_dragged_player_id;
						*(int *)&g_frontend_net_packet_scratch
							 .payload[4] =
							team_index;
						*(int *)&g_frontend_net_packet_scratch
							 .payload[8] =
							target_slot;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							16);
					}
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_RELEASE_TEAM_RESERVATION;
						*(int *)&g_frontend_net_packet_scratch
							 .payload[0] =
							g_mission_setup_dragged_player_id;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							8);
					}
					g_mission_setup_dragged_player_id = 0;
					frontend_mouse_clear_input_gate();
				}
			}
			if (g_mission_setup_player_assignments
				    .team_player_ids[team_index][slot_index] !=
			    0) {
				int ready_count =
					g_frontend_mission_session_mode ==
							FRONTEND_MISSION_SESSION_SINGLEPLAYER
						? 1
						: net_count_ready_players();
				int roster_index;
				for (roster_index = 0;
				     roster_index < ready_count;
				     ++roster_index) {
					if (g_mp_roster[roster_index]
						    .player_id ==
					    g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [slot_index]) {
						break;
					}
				}
				if (roster_index < ready_count) {
					sprintf(g_frontend_scratch_buffer,
						"%c%s %c%s", 6,
						frontend_string_get(
							FRONTSTR_154_DRONE +
							g_mp_roster[roster_index]
								.pilot_rating),
						1,
						g_mp_roster[roster_index].name);
					frontend_display_get_screen_clip_rect(
						&saved_clip);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					if (net_get_local_player_id() ==
						    g_mp_roster[roster_index]
							    .player_id ||
					    g_frontend_mission_session_mode ==
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						frontend_text_draw_aligned_in_rect(
							12,
							g_frontend_scratch_buffer,
							&rect, 0, 1,
							g_pulse_color_ramp
								[((frame_counter %
								   24) &
								  ~1) >>
								 1]);
					} else {
						frontend_text_draw_aligned_in_rect(
							12,
							g_frontend_scratch_buffer,
							&rect, 0, 1,
							g_color_yellow);
					}
					frontend_display_set_screen_clip_rect640x480(
						&saved_clip);
					if (!frontend_mouse_is_gate_owner(2) &&
					    (net_is_host() != 0 ||
					     g_frontend_mission_session_mode ==
						     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
					    (frontend_mouse_get_left_down() !=
						     0 ||
					     frontend_mouse_get_right_down() !=
						     0)) {
						frontend_cursor_get_pos(
							&cursor_x, &cursor_y);
						if (frontend_draw_point_in_rect(
							    &rect, cursor_x,
							    cursor_y)) {
							if (g_frontend_mission_session_mode !=
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								int reserved_index;

								for (reserved_index =
									     0;
								     reserved_index <
								     g_mission_setup_reserved_player_count;
								     ++reserved_index) {
									if (g_mission_setup_reserved_player_ids
										    [reserved_index] ==
									    g_mp_roster[roster_index]
										    .player_id) {
										break;
									}
								}
								if (reserved_index ==
								    g_mission_setup_reserved_player_count) {
									g_mission_setup_reserved_player_ids
										[reserved_index] =
											g_mp_roster[roster_index]
												.player_id;
									++g_mission_setup_reserved_player_count;
									g_frontend_net_packet_scratch
										.packet_type =
										NET_PACKET_TEAM_RESERVATION;
									*(int *)&g_frontend_net_packet_scratch
										 .payload[0] =
										g_mp_roster[roster_index]
											.player_id;
									*(int *)&g_frontend_net_packet_scratch
										 .payload[4] =
										net_get_host_player_id();
									net_send_packet_and_flush(
										0,
										&g_frontend_net_packet_scratch,
										12);
									g_mission_setup_dragged_player_id =
										g_mp_roster[roster_index]
											.player_id;
									frontend_mouse_set_input_gate(
										2);
									g_frontend_net_packet_scratch
										.packet_type =
										NET_PACKET_TEAM_ASSIGNMENT;
									*(int *)&g_frontend_net_packet_scratch
										 .payload[0] =
										g_mission_setup_dragged_player_id;
									*(int *)&g_frontend_net_packet_scratch
										 .payload[4] =
										10;
									net_send_packet_and_flush(
										0,
										&g_frontend_net_packet_scratch,
										12);
									for (active_index =
										     0;
									     active_index <
									     8;
									     ++active_index) {
										if (g_mission_setup_player_assignments
											    .assigned_player_ids
												    [active_index] ==
										    g_mission_setup_dragged_player_id) {
											g_mission_setup_player_assignments
												.assigned_player_ids
													[active_index] =
												0;
											break;
										}
									}
									for (int remove_team =
										     0;
									     remove_team <
									     g_team_count;
									     ++remove_team) {
										for (int remove_slot =
											     0;
										     remove_slot <
										     g_team_player_flight_group_count
											     [remove_team];
										     ++remove_slot) {
											if (g_mission_setup_player_assignments
												    .team_player_ids
													    [remove_team]
													    [remove_slot] ==
											    g_mission_setup_dragged_player_id) {
												for (shift = remove_slot +
													     1;
												     shift <
												     8;
												     ++shift) {
													g_mission_setup_player_assignments
														.team_player_ids
															[remove_team]
															[shift -
															 1] =
														g_mission_setup_player_assignments
															.team_player_ids
																[remove_team]
																[shift];
												}
												g_mission_setup_player_assignments
													.team_player_ids
														[remove_team]
														[7] =
													0;
												break;
											}
										}
									}
								}
							} else {
								g_mission_setup_dragged_player_id =
									g_mp_roster[roster_index]
										.player_id;
								frontend_mouse_set_input_gate(
									2);
								for (active_index =
									     0;
								     active_index <
								     8;
								     ++active_index) {
									if (g_mission_setup_player_assignments
										    .assigned_player_ids
											    [active_index] ==
									    g_mission_setup_dragged_player_id) {
										g_mission_setup_player_assignments
											.assigned_player_ids
												[active_index] =
											0;
										for (shift = slot_index +
											     1;
										     shift <
										     8;
										     ++shift) {
											g_mission_setup_player_assignments
												.team_player_ids
													[team_index]
													[shift -
													 1] =
												g_mission_setup_player_assignments
													.team_player_ids
														[team_index]
														[shift];
										}
										g_mission_setup_player_assignments
											.team_player_ids
												[team_index]
												[7] =
											0;
										break;
									}
								}
							}
						}
					}
				}
			}
			frontend_draw_rect_offset_xy(&rect, 0, 15);
		}
	}
}

/* Takes out of the team assignments every assigned_player_ids entry that is not
 * the nonzero playerId of a g_mp_roster entry, a 0 entry included: the id is
 * removed from each slot of the first g_team_count teams that holds it, the
 * later slots shifting down and slot 7 becoming 0, and the entry is set to 0.
 * g_mission_setup_player_flight_group_indices is not shifted with them. Outside a
 * solo game it also calls net_count_ready_players and drops the result. */
// FUNCTION: XVT 0x4F3EA0
void mission_setup_prune_team_assignments(void)
{
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		net_count_ready_players();
	}

	int active_player_index = 0;
	do {
		int *active_player_id_ptr =
			&g_mission_setup_player_assignments
				 .assigned_player_ids[active_player_index];
		unsigned int roster_index = 0;
		int *roster_player_id_ptr = &g_mp_roster[0].player_id;
		do {
			int roster_player_value = *roster_player_id_ptr;
			if (roster_player_value != 0 &&
			    *active_player_id_ptr == roster_player_value) {
				break;
			}
			roster_player_id_ptr =
				(int *)((char *)roster_player_id_ptr +
					sizeof(struct mp_roster_entry));
		} while (++roster_index < 8);

		if (roster_index == 8) {
			if (g_team_count > 0) {
				int removed_player_id = *active_player_id_ptr;
				int *team_last_player_id_ptr =
					&g_mission_setup_player_assignments
						 .team_player_ids[0][7];
				unsigned int team_offset = 0;
				int teams_remaining = g_team_count;
				do {
					for (int team_player_index = 0;
					     team_player_index < 8;
					     ++team_player_index) {
						if (removed_player_id ==
						    ((int *)g_mission_setup_player_assignments
							     .team_player_ids)
							    [team_offset +
							     team_player_index]) {
							if (team_player_index <
							    7) {
								int shift_count =
									7 -
									team_player_index;
								int *shift_destination =
									(int *)((char *)&g_mission_setup_player_assignments +
										4 * team_offset +
										4 * team_player_index);
								do {
									int shifted_player_id = shift_destination
										[1];
									*shift_destination =
										shifted_player_id;
									++shift_destination;
									--shift_count;
								} while (
									shift_count !=
									0);
							}
							*team_last_player_id_ptr =
								0;
						}
					}
					team_last_player_id_ptr += 8;
					team_offset += 8;
					--teams_remaining;
				} while (teams_remaining != 0);
			}
			*active_player_id_ptr = 0;
		}
		++active_player_index;
	} while (active_player_index < 8);
}

/* Tells whether the teams are ready for flight assignment. Returns 0 when one
 * of the first ready_count g_mp_roster entries (1 in a solo game, else
 * net_count_ready_players()) has a playerId of 0 or one in no assigned_player_ids
 * slot, or when one of the first g_team_count teams has a player in slots 1 to 7
 * but none in slot 0, its captain's slot; else 1. */
// FUNCTION: XVT 0x4F3F70
int mission_setup_is_team_assignment_valid(void)
{
	int ready_player_count = 1;
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		ready_player_count = net_count_ready_players();
	}

	int ready_player_index = 0;
	if (ready_player_count > 0) {
		do {
			int assignment_index = 0;
			int player_id =
				g_mp_roster[ready_player_index].player_id;
			do {
				if (player_id != 0 &&
				    g_mission_setup_player_assignments
						    .assigned_player_ids
							    [assignment_index] ==
					    player_id) {
					break;
				}
				++assignment_index;
			} while (assignment_index < 8);
			if (assignment_index == 8) {
				return 0;
			}
			++ready_player_index;
		} while (ready_player_index < ready_player_count);
	}

	int team_index = 0;
	if (g_team_count > 0) {
		do {
			int team_player_index = 1;
			do {
				if (g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [team_player_index] !=
					    0 &&
				    g_mission_setup_player_assignments
						    .team_player_ids[team_index]
								    [0] == 0) {
					return 0;
				}
				++team_player_index;
			} while (team_player_index < 8);
			++team_index;
		} while (team_index < g_team_count);
	}

	return 1;
}

/* Draws the side buttons of the team assignment screen and handles them. For a
 * combat engagement sequence (mission_sequence_active 1) the Assign Teams and
 * Mission Description buttons set g_mission_setup_show_description_panel to 0 or 1
 * and redraw the screen's base, with the team slot overlays when the teams come
 * back; Mission Description also resets g_frontend_first_visible_line. For the
 * host or a solo player while the teams show, a left or right click inside
 * Clear List calls mission_setup_clear_team_assignments and inside Auto Assign
 * mission_setup_randomize_team_assignments. Returns 1. */
// FUNCTION: XVT 0x4F4010
int mission_setup_update_team_controls(void)
{
	frontend_navigation_slot_state slot_states[8];
	int host_controls = net_is_host() != 0 ||
			    g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER;

	if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilot_data.mission_sequence_active == 1) {
		slot_states[0] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[1] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[g_mission_setup_show_description_panel] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else {
		slot_states[0] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slot_states[1] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	}
	slot_states[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[3] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[4] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	if (host_controls && g_mission_setup_show_description_panel == 0) {
		slot_states[5] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[6] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	} else {
		slot_states[5] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slot_states[6] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	}
	slot_states[7] = FRONTEND_NAVIGATION_SLOT_INACTIVE;

	int cursor_x;
	int cursor_y;
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	int slot;
	struct RECT rect;
	if (host_controls) {
		frontend_draw_rect_assign(&rect, 22, 306, 42, 330);
		for (slot = 5; slot < 7; ++slot) {
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_down() != 0 ||
			     frontend_mouse_get_right_down() != 0 ||
			     frontend_mouse_get_left_click() != 0 ||
			     frontend_mouse_get_right_click() != 0)) {
				slot_states[slot] =
					FRONTEND_NAVIGATION_SLOT_SELECTED;
			}
			frontend_draw_rect_offset_xy(&rect, 0, 28);
		}
	}
	frontend_button_draw_eight_slot_navigation_state(slot_states);

	if (g_mission_setup_show_description_panel == 0 && host_controls) {
		frontend_draw_rect_assign(&rect, 22, 334, 42, 358);
		frontend_button_handle_sprite_button(
			&rect, "clearu", "cleard",
			frontend_string_get(FRONTSTR_215_CLEAR_LIST), 12, 0, 16,
			"jewelsound");
		if (frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
		    (frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			mission_setup_clear_team_assignments();
		}
		frontend_draw_rect_offset_xy(&rect, 0, -28);
		frontend_button_handle_sprite_button(
			&rect, "assignu", "assignd",
			frontend_string_get(FRONTSTR_214_AUTO_ASSIGN), 12, 0,
			17, "jewelsound");
		if (frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
		    (frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			mission_setup_randomize_team_assignments();
		}
	}

	frontend_draw_rect_assign(&rect, 22, 142, 42, 166);
	if (slot_states[1] != FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_mission_setup_show_description_panel == 1) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "team2d",
				frontend_string_get(
					FRONTSTR_798_MISSION_DESCRIPTION),
				12, 0);
		} else if (frontend_button_handle_sprite_button(
				   &rect, "team2u", "team2u",
				   frontend_string_get(
					   FRONTSTR_798_MISSION_DESCRIPTION),
				   12, 0, 12, "jewelsound") != 0) {
			g_mission_setup_show_description_panel = 1;
			g_frontend_first_visible_line = 0;
			front_image_register_resource_default(
				"frontres\\soloteam.bmp", "background");
			frontend_display_lock_offscreen_surface();
			front_image_draw_sprite_opaque("background", 0, 0);
			front_image_draw_sprite("frame", 0, 0);
			if (g_host_cd_available != 0) {
				front_image_draw_sprite("allactive", 0, 0);
			} else {
				front_image_draw_sprite("clientactive", 0, 0);
			}
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				front_image_draw_sprite_translucent("chatbox",
								    0, 0);
			}
			front_image_draw_sprite_translucent("teamoverlay", 0,
							    0);
			frontend_display_unlock_offscreen_surface(1);
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (slot_states[0] != FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_mission_setup_show_description_panel == 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "team1d",
				frontend_string_get(FRONTSTR_797_ASSIGN_TEAMS),
				12, 0);
		} else {
			if (frontend_button_handle_sprite_button(
				    &rect, "team1u", "team1u",
				    frontend_string_get(
					    FRONTSTR_797_ASSIGN_TEAMS),
				    12, 0, 11, "jewelsound") != 0) {
				g_mission_setup_show_description_panel = 0;
				front_image_register_resource_default(
					"frontres\\soloteam.bmp", "background");
				frontend_display_lock_offscreen_surface();
				front_image_draw_sprite_opaque("background", 0,
							       0);
				front_image_draw_sprite("frame", 0, 0);
				if (g_host_cd_available != 0) {
					front_image_draw_sprite("allactive", 0,
								0);
				} else {
					front_image_draw_sprite("clientactive",
								0, 0);
				}
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					front_image_draw_sprite_translucent(
						"chatbox", 0, 0);
				}
				front_image_draw_sprite_translucent(
					"teamoverlay", 0, 0);
				{
					frontend_draw_rect_assign(
						&rect, 88, 207, 256, 221);
					int overlay_team = 0;
					for (overlay_team = 0;
					     overlay_team < g_team_count;
					     ++overlay_team) {
						if ((g_team_count == 8 ||
						     g_team_count == 7) &&
						    overlay_team == 4) {
							frontend_draw_rect_assign(
								&rect, 260, 207,
								428, 221);
						}
						frontend_draw_rect_offset_xy(
							&rect, 0, 15);
						for (slot = 0;
						     slot <
						     g_team_player_flight_group_count
							     [overlay_team];
						     ++slot) {
							if (slot == 0) {
								front_image_draw_sprite_translucent(
									"captoverlay",
									rect.left,
									rect.top);
							} else {
								front_image_draw_sprite_translucent(
									"slotoverlay",
									rect.left,
									rect.top);
							}
							frontend_draw_rect_offset_xy(
								&rect, 0, 15);
						}
					}
				}
				frontend_display_unlock_offscreen_surface(1);
			}
		}
	}
	return 1;
}

/* Clears the team assignments and places each of the ready_count players (1 in a
 * solo game, else net_count_ready_players()) at random: a g_mp_roster entry drawn
 * by rand() % 8 among those with a nonzero playerId not yet placed, on a team
 * drawn by rand() % g_team_count among those with fewer placed players than
 * their g_team_player_flight_group_count, in that team's first empty slot; the
 * player's id also goes in assigned_player_ids at the roster entry's index.
 * Outside a solo game it then sends every player a TEAM_ASSIGNMENTS packet with
 * team_player_ids and assigned_player_ids. Returns 1. Does not check that there are
 * enough roster entries and team slots for the players, or that g_team_count is
 * nonzero: the draws then never end, or divide by zero. */
// FUNCTION: XVT 0x4F4580
int mission_setup_randomize_team_assignments(void)
{
	memset(g_mission_setup_player_assignments.team_player_ids, 0,
	       sizeof(g_mission_setup_player_assignments.team_player_ids));
	memset(g_mission_setup_player_assignments.assigned_player_ids, 0,
	       sizeof(g_mission_setup_player_assignments.assigned_player_ids));
	int assigned_team_counts[10];
	memset(assigned_team_counts, 0, sizeof(assigned_team_counts));
	int used_roster[8];
	memset(used_roster, 0, sizeof(used_roster));
	int ready_player_count = 1;
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		ready_player_count = net_count_ready_players();
	}
	int roster_index;
	int team;
	while (ready_player_count > 0) {
		do {
			do {
				roster_index = rand() % 8;
			} while (g_mp_roster[roster_index].player_id == 0);
		} while (used_roster[roster_index] != 0);

		do {
			team = rand() % g_team_count;
		} while (assigned_team_counts[team] >=
			 g_team_player_flight_group_count[team]);

		int assignment_slot = 0;
		++assigned_team_counts[team];
		int *team_assignment = g_mission_setup_player_assignments
					       .team_player_ids[team];
		for (;;) {
			if (*team_assignment == 0) {
				--ready_player_count;
				used_roster[roster_index] = 1;
				int player_id =
					g_mp_roster[roster_index].player_id;
				g_mission_setup_player_assignments
					.team_player_ids[team]
							[assignment_slot] =
					player_id;
				g_mission_setup_player_assignments
					.assigned_player_ids[roster_index] =
					player_id;
				break;
			}
			++team_assignment;
			++assignment_slot;
			if (assignment_slot >= 8) {
				break;
			}
		}
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_TEAM_ASSIGNMENTS;
		memcpy(g_frontend_net_packet_scratch.payload,
		       g_mission_setup_player_assignments.team_player_ids,
		       sizeof(g_mission_setup_player_assignments
				      .team_player_ids));
		memcpy(g_frontend_net_packet_scratch.payload +
			       sizeof(g_mission_setup_player_assignments
					      .team_player_ids),
		       g_mission_setup_player_assignments.assigned_player_ids,
		       sizeof(g_mission_setup_player_assignments
				      .assigned_player_ids));
		net_send_packet_and_flush(
			0, &g_frontend_net_packet_scratch,
			sizeof(g_mission_setup_player_assignments) +
				sizeof(g_frontend_net_packet_scratch
					       .packet_type));
	}
	return 1;
}

/* Clears team_player_ids and assigned_player_ids and, outside a solo game, sends
 * every player a CLEAR_TEAM_ASSIGNMENTS packet. Returns 1. */
// FUNCTION: XVT 0x4F46C0
int mission_setup_clear_team_assignments(void)
{
	memset(g_mission_setup_player_assignments.team_player_ids, 0,
	       sizeof(g_mission_setup_player_assignments.team_player_ids));
	memset(g_mission_setup_player_assignments.assigned_player_ids, 0,
	       sizeof(g_mission_setup_player_assignments.assigned_player_ids));
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_CLEAR_TEAM_ASSIGNMENTS;
		net_send_packet_and_flush(
			0, &g_frontend_net_packet_scratch,
			sizeof(g_frontend_net_packet_scratch.packet_type));
	}
	return 1;
}

/* Sends every player the host's final team assignments, a
 * TEAM_ASSIGNMENTS_READY packet carrying team_player_ids (not assigned_player_ids).
 * Returns 1. */
// FUNCTION: XVT 0x4F4710
int mission_setup_broadcast_team_assignments(void)
{
	g_frontend_net_packet_scratch.packet_type =
		NET_PACKET_TEAM_ASSIGNMENTS_READY;
	memcpy(g_frontend_net_packet_scratch.payload,
	       g_mission_setup_player_assignments.team_player_ids,
	       sizeof(g_mission_setup_player_assignments.team_player_ids));
	net_send_packet_and_flush(
		0, &g_frontend_net_packet_scratch,
		sizeof(g_mission_setup_player_assignments.team_player_ids) +
			sizeof(g_frontend_net_packet_scratch.packet_type));
	return 1;
}

/* Continues a saved battle when a combat engagement sequence starts, the
 * mission type being combat engagement; mission_setup_team_assignment_update calls
 * it on frame 0, the modern build through its campaign task. It moves the
 * mission type up to battle, loads the list and selects the stored battle. A
 * solo game continues when g_game_config.continue_battle_or_campaign is not
 * SEQUENCE_RESTART and the battle's sp_battle_continuations entry is active,
 * copying the saved sequence state into g_pilot_data.battle_sequence_state. A
 * network host does the same with mp_battle_continuations, first sending every
 * player a BATTLE_CONTINUATION packet: 1, the saved random_seed and the saved
 * state, or 0 alone when it does not continue. A client waits for that packet
 * until more than 30000 ms have passed; the original build drops any other
 * packet it reads meanwhile. On 1 it takes the host's state, keeping its own
 * saved cumulative_score when the host's seed equals its own saved seed and
 * setting it to 0 when not. Continuing sets launch_session_marker to 1, raises
 * current_mission_index by 1, calls mission_setup_select_next_sequence_mission and
 * returns 1. Otherwise it lowers the mission type by 1, clears the entry's
 * is_active and returns 0. On a client's timeout it returns 0 having lowered the
 * mission type by 1 without raising it first, which leaves it at tournament. In
 * the modern build a client returns XVT_CAMPAIGN_PENDING while it waits. */
// FUNCTION: XVT 0x4F4750
int mission_setup_try_continue_battle(void)
{
	enum { BATTLE_CONTINUATION_WAIT_TIMEOUT_MS = 30000 };

#ifndef XVT_MODERN
	uint32_t wait_start_ms;
	DPID sender_id;
	uint32_t packet_size;
#endif
	int mission_index;
	int mission_description_id;
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		++g_pilot_data.mission_directory_id;
		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			if ((unsigned int)g_mission_count > 0) {
				mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				do {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    mission_description_id) {
						break;
					}
					++g_selected_mission_list_index;
				} while ((unsigned int)g_mission_count >
					 (unsigned int)
						 g_selected_mission_list_index);
			}
		}
		if (g_game_config.continue_battle_or_campaign !=
			    SEQUENCE_RESTART &&
		    g_pilot_data.sp_battle_continuations
				    [g_mission_list
					     [g_selected_mission_list_index]
						     .mission_idx]
					    .is_active != 0) {
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx;
			memcpy(&g_pilot_data.battle_sequence_state,
			       &g_pilot_data
					.sp_battle_continuations[mission_index]
					.sequence_state,
			       sizeof(g_pilot_data.battle_sequence_state));
			g_pilot_data.launch_session_marker = 1;
			++g_pilot_data.battle_sequence_state
				  .current_mission_index;
			mission_setup_select_next_sequence_mission();
			return 1;
		}

		--g_pilot_data.mission_directory_id;
		mission_index = g_mission_list[g_selected_mission_list_index]
					.mission_idx;
		g_pilot_data.sp_battle_continuations[mission_index].is_active =
			0;
		return 0;
	}

	uint32_t continuation_seed;
	int *received_packet;
	if (net_is_host() != 0) {
		++g_pilot_data.mission_directory_id;
		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			if ((unsigned int)g_mission_count > 0) {
				mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				do {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    mission_description_id) {
						break;
					}
					++g_selected_mission_list_index;
				} while ((unsigned int)g_mission_count >
					 (unsigned int)
						 g_selected_mission_list_index);
			}
		}
		if (g_game_config.continue_battle_or_campaign ==
			    SEQUENCE_RESTART ||
		    g_pilot_data.mp_battle_continuations
				    [g_mission_list
					     [g_selected_mission_list_index]
						     .mission_idx]
					    .is_active == 0) {
			--g_pilot_data.mission_directory_id;
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx;
			*(int *)g_frontend_net_packet_scratch.payload = 0;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_BATTLE_CONTINUATION;
			g_pilot_data.mp_battle_continuations[mission_index]
				.is_active = 0;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type) +
					sizeof(int));
			return 0;
		}

		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_BATTLE_CONTINUATION;
		*(int *)g_frontend_net_packet_scratch.payload = 1;
		memcpy(g_frontend_net_packet_scratch.payload + sizeof(int),
		       &g_pilot_data
				.mp_battle_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.random_seed,
		       sizeof(g_pilot_data.mp_battle_continuations[0]
				      .random_seed));
		memcpy(g_frontend_net_packet_scratch.payload + sizeof(int) +
			       sizeof(continuation_seed),
		       &g_pilot_data
				.mp_battle_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.sequence_state,
		       sizeof(g_pilot_data.mp_battle_continuations[0]
				      .sequence_state));
		net_send_packet_and_flush(
			0, &g_frontend_net_packet_scratch,
			sizeof(g_frontend_net_packet_scratch.packet_type) +
				sizeof(int) + sizeof(continuation_seed) +
				sizeof(struct battle_sequence_state));
		memcpy(&g_pilot_data.battle_sequence_state,
		       &g_pilot_data
				.mp_battle_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.sequence_state,
		       sizeof(g_pilot_data.battle_sequence_state));
	} else {
#ifdef XVT_MODERN
		int wait_result = xvt_campaign_task_wait_packet(
			NET_PACKET_BATTLE_CONTINUATION, &received_packet);
		if (wait_result == XVT_CAMPAIGN_PENDING) {
			return XVT_CAMPAIGN_PENDING;
		}
		if (wait_result == 0) {
			--g_pilot_data.mission_directory_id;
			return 0;
		}
#else
		wait_start_ms = GetTickCount();
		do {
			received_packet = net_get_next_app_packet(&sender_id,
								  &packet_size);
			if (received_packet != NULL &&
			    received_packet[0] ==
				    NET_PACKET_BATTLE_CONTINUATION) {
				break;
			}
			if (GetTickCount() - wait_start_ms >
			    BATTLE_CONTINUATION_WAIT_TIMEOUT_MS) {
				--g_pilot_data.mission_directory_id;
				return 0;
			}
		} while (1);
#endif

		++g_pilot_data.mission_directory_id;
		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			if ((unsigned int)g_mission_count > 0) {
				mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				do {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    mission_description_id) {
						break;
					}
					++g_selected_mission_list_index;
				} while ((unsigned int)g_mission_count >
					 (unsigned int)
						 g_selected_mission_list_index);
			}
		}
		if (received_packet[1] == 0) {
			--g_pilot_data.mission_directory_id;
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx;
			g_pilot_data.mp_battle_continuations[mission_index]
				.is_active = 0;
			return 0;
		}

		struct battle_sequence_state local_sequence_state;
		memcpy(&local_sequence_state,
		       &g_pilot_data
				.mp_battle_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.sequence_state,
		       sizeof(local_sequence_state));
		continuation_seed =
			g_pilot_data
				.mp_battle_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.random_seed;
		memcpy(&g_pilot_data.battle_sequence_state, &received_packet[3],
		       sizeof(g_pilot_data.battle_sequence_state));
		if ((uint32_t)received_packet[2] == continuation_seed) {
			g_pilot_data.battle_sequence_state.cumulative_score =
				local_sequence_state.cumulative_score;
		} else {
			g_pilot_data.battle_sequence_state.cumulative_score = 0;
		}
	}

	g_pilot_data.launch_session_marker = 1;
	++g_pilot_data.battle_sequence_state.current_mission_index;
	mission_setup_select_next_sequence_mission();
	return 1;
}

/* Continues a saved campaign when a campaign sequence starts, the mission type
 * being training; mission_setup_team_assignment_update calls it on frame 0, the
 * modern build through its campaign task. It sets the mission type to campaign,
 * loads the list and selects the stored campaign. A solo game continues when
 * g_game_config.continue_battle_or_campaign is not SEQUENCE_RESTART and the
 * campaign's sp_campaign_continuations entry is active, copying the saved
 * sequence state into g_pilot_data.campaign_sequence_state. A network host does
 * the same with mp_campaign_continuations, first sending every player a
 * CAMPAIGN_CONTINUATION packet: 1, the saved random_seed and the saved state, or
 * 0 alone when it does not continue. A client waits for that packet until more
 * than 30000 ms have passed; the original build drops any other packet it reads
 * meanwhile. On 1 it takes the host's state, keeping the cumulative_score of its
 * own saved entry (mp_campaign_continuations at the campaign id plus 12) when the
 * host's seed equals that entry's seed and setting it to 0 when not. Continuing
 * sets launch_session_marker to 1, raises current_mission_index by 1, calls
 * mission_setup_select_next_sequence_mission and returns 1. Otherwise it sets the
 * mission type back to training, clears the entry's is_active (a client's at the
 * id plus 12) unless the wait timed out, and returns 0. In the modern build a
 * client returns XVT_CAMPAIGN_PENDING while it waits. */
// FUNCTION: XVT 0x4F4B80
int mission_setup_try_continue_campaign(void)
{
	enum {
		CAMPAIGN_CLIENT_CONTINUATION_OFFSET = 12,
		CAMPAIGN_CONTINUATION_WAIT_TIMEOUT_MS = 30000
	};

#ifndef XVT_MODERN
	uint32_t wait_start_ms;
	DPID sender_id;
	uint32_t packet_size;
#endif
	int mission_index;
	int mission_description_id;
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_pilot_data.mission_directory_id = MISSION_DIRECTORY_CAMPAIGNS;
		mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			if ((unsigned int)g_mission_count > 0) {
				mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				do {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    mission_description_id) {
						break;
					}
					++g_selected_mission_list_index;
				} while ((unsigned int)g_mission_count >
					 (unsigned int)
						 g_selected_mission_list_index);
			}
		}
		if (g_game_config.continue_battle_or_campaign !=
			    SEQUENCE_RESTART &&
		    g_pilot_data.sp_campaign_continuations
				    [g_mission_list
					     [g_selected_mission_list_index]
						     .mission_idx]
					    .is_active != 0) {
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx;
			memcpy(&g_pilot_data.campaign_sequence_state,
			       &g_pilot_data
					.sp_campaign_continuations
						[mission_index]
					.sequence_state,
			       sizeof(g_pilot_data.campaign_sequence_state));
			g_pilot_data.launch_session_marker = 1;
			++g_pilot_data.campaign_sequence_state
				  .current_mission_index;
			mission_setup_select_next_sequence_mission();
			return 1;
		}

		g_pilot_data.mission_directory_id =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
		mission_index = g_mission_list[g_selected_mission_list_index]
					.mission_idx;
		g_pilot_data.sp_campaign_continuations[mission_index]
			.is_active = 0;
		return 0;
	}

	uint32_t continuation_seed;
	int *received_packet;
	if (net_is_host() != 0) {
		g_pilot_data.mission_directory_id = MISSION_DIRECTORY_CAMPAIGNS;
		mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			if ((unsigned int)g_mission_count > 0) {
				mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				do {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    mission_description_id) {
						break;
					}
					++g_selected_mission_list_index;
				} while ((unsigned int)g_mission_count >
					 (unsigned int)
						 g_selected_mission_list_index);
			}
		}
		if (g_game_config.continue_battle_or_campaign ==
			    SEQUENCE_RESTART ||
		    g_pilot_data.mp_campaign_continuations
				    [g_mission_list
					     [g_selected_mission_list_index]
						     .mission_idx]
					    .is_active == 0) {
			g_pilot_data.mission_directory_id =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx;
			*(int *)g_frontend_net_packet_scratch.payload = 0;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_CAMPAIGN_CONTINUATION;
			g_pilot_data.mp_campaign_continuations[mission_index]
				.is_active = 0;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type) +
					sizeof(int));
			return 0;
		}

		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_CAMPAIGN_CONTINUATION;
		*(int *)g_frontend_net_packet_scratch.payload = 1;
		memcpy(g_frontend_net_packet_scratch.payload + sizeof(int),
		       &g_pilot_data
				.mp_campaign_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.random_seed,
		       sizeof(g_pilot_data.mp_campaign_continuations[0]
				      .random_seed));
		memcpy(g_frontend_net_packet_scratch.payload + sizeof(int) +
			       sizeof(continuation_seed),
		       &g_pilot_data
				.mp_campaign_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.sequence_state,
		       sizeof(g_pilot_data.mp_campaign_continuations[0]
				      .sequence_state));
		net_send_packet_and_flush(
			0, &g_frontend_net_packet_scratch,
			sizeof(g_frontend_net_packet_scratch.packet_type) +
				sizeof(int) + sizeof(continuation_seed) +
				sizeof(struct campaign_sequence_state));
		memcpy(&g_pilot_data.campaign_sequence_state,
		       &g_pilot_data
				.mp_campaign_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx]
				.sequence_state,
		       sizeof(g_pilot_data.campaign_sequence_state));
	} else {
#ifdef XVT_MODERN
		int wait_result = xvt_campaign_task_wait_packet(
			NET_PACKET_CAMPAIGN_CONTINUATION, &received_packet);
		if (wait_result == XVT_CAMPAIGN_PENDING) {
			return XVT_CAMPAIGN_PENDING;
		}
		if (wait_result == 0) {
			g_pilot_data.mission_directory_id =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			return 0;
		}
#else
		wait_start_ms = GetTickCount();
		do {
			received_packet = net_get_next_app_packet(&sender_id,
								  &packet_size);
			if (received_packet != NULL &&
			    received_packet[0] ==
				    NET_PACKET_CAMPAIGN_CONTINUATION) {
				break;
			}
			if (GetTickCount() - wait_start_ms >
			    CAMPAIGN_CONTINUATION_WAIT_TIMEOUT_MS) {
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_TRAINING_EXERCISES;
				return 0;
			}
		} while (1);
#endif

		g_pilot_data.mission_directory_id = MISSION_DIRECTORY_CAMPAIGNS;
		mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			if ((unsigned int)g_mission_count > 0) {
				mission_description_id =
					g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id];
				do {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    mission_description_id) {
						break;
					}
					++g_selected_mission_list_index;
				} while ((unsigned int)g_mission_count >
					 (unsigned int)
						 g_selected_mission_list_index);
			}
		}
		if (received_packet[1] == 0) {
			g_pilot_data.mission_directory_id =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			mission_index =
				g_mission_list[g_selected_mission_list_index]
					.mission_idx +
				CAMPAIGN_CLIENT_CONTINUATION_OFFSET;
			g_pilot_data.mp_campaign_continuations[mission_index]
				.is_active = 0;
			return 0;
		}

		struct campaign_sequence_state local_sequence_state;
		memcpy(&local_sequence_state,
		       &g_pilot_data
				.mp_campaign_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx +
					 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
				.sequence_state,
		       sizeof(local_sequence_state));
		continuation_seed =
			g_pilot_data
				.mp_campaign_continuations
					[g_mission_list
						 [g_selected_mission_list_index]
							 .mission_idx +
					 CAMPAIGN_CLIENT_CONTINUATION_OFFSET]
				.random_seed;
		memcpy(&g_pilot_data.campaign_sequence_state,
		       &received_packet[3],
		       sizeof(g_pilot_data.campaign_sequence_state));
		if ((uint32_t)received_packet[2] == continuation_seed) {
			g_pilot_data.campaign_sequence_state.cumulative_score =
				local_sequence_state.cumulative_score;
		} else {
			g_pilot_data.campaign_sequence_state.cumulative_score =
				0;
		}
	}

	g_pilot_data.launch_session_marker = 1;
	++g_pilot_data.campaign_sequence_state.current_mission_index;
	mission_setup_select_next_sequence_mission();
	return 1;
}

/* Draws the mission description on the team assignment screen in place of the
 * team slots: a heading for a tournament, battle, campaign or other mission,
 * then g_mission_text wrapped, with a scrollbar that sets
 * g_frontend_first_visible_line when the text's wrapped line count (from a draw
 * starting at line 4096) plus one is over 13. Returns 1. */
// FUNCTION: XVT 0x4F4F80
int mission_setup_draw_team_mission_description(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 207, 430, 224);
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_TOURNAMENTS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(
				FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_BATTLES) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_472_BATTLE_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}
	frontend_draw_rect_assign(&rect, 88, 225, 420, 433);
	int line_count = frontend_text_draw_wrapped(12, g_mission_text, &rect,
						    0xFFFF, 4, 4096) +
			 1;
	if (line_count > 13) {
		frontend_draw_rect_assign(&rect, 421, 225, 430, 433);
		g_frontend_first_visible_line = frontend_scrollbar_draw(
			&rect, g_frontend_first_visible_line, line_count, 0, 5,
			(unsigned int)g_color_navy, 9);
		frontend_draw_rect_assign(&rect, 88, 225, 420, 433);
	} else {
		frontend_draw_rect_assign(&rect, 88, 225, 430, 433);
	}
	frontend_text_draw_wrapped(12, g_mission_text, &rect, 0xFFFF, 4,
				   g_frontend_first_visible_line);
	return 1;
}

/* Exit callback of the flight assignment screen: frees the briefing text
 * buffers, unloads the frontres\mapicons.lst images, frees g_mission_list and
 * g_mission_text, setting both to NULL, frees the "background" image, resets the
 * scrollable controls and clears the mouse input gate. Ignores frame_counter and
 * returns 0. */
// FUNCTION: XVT 0x4F5100
int mission_setup_free_screen_resources(int frame_counter)
{
	(void)frame_counter;

	briefing_text_free_allocated_buffers_exit();
	front_image_unload_resource_list("frontres\\mapicons.lst");
	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	front_image_free_resource_by_name("background");
	frontend_reset_scrollable_controls();
	frontend_mouse_clear_input_gate();
	return 0;
}

/* The flight assignment screen, run once per frame: each team's players are
 * placed in the team's player flight groups beside the briefing map, and Next
 * moves on to mission_briefing_craft_selection_update. On frame 0 it clears
 * g_mission_setup_launch_signal_sent, the reservations,
 * g_frontend_briefing_entered_count and g_mission_setup_use_expanded_assignment_layout;
 * sets g_frontend_chat_team_only when the pilot's team has more than one player
 * flight group; sets g_frontend_game_session_in_progress in a solo game; loads the
 * mission for the briefing and the map icons; fills g_text_shade_ramps with five
 * ramps of eight shades from 0x48 to 0xFC (green, red, yellow, blue, magenta);
 * sets every g_mission_setup_player_flight_group_indices entry to -1 unless
 * g_frontend_skip_screen_entry_setup is set; prunes the assignments; for each team
 * gives its player flight groups, in mission order, each to the next slot that
 * holds a player and has no flight group; and sets
 * g_frontend_skip_screen_entry_setup. A melee whose teams have one slot each, or a
 * Quick Start, goes straight to the briefing. Otherwise it picks the background
 * by mission type and side (the pilot's team, or for other types the IFF of the
 * first player flight group), draws the screen's base with a slot overlay per
 * player flight group of the pilot's team (the expanded layout, setting
 * g_mission_setup_use_expanded_assignment_layout, when there are more than 4, else
 * at most 4 under the map), starts the 120000 ms launch countdown, and
 * allocates and loads g_mission_text. Every frame it draws the mission name and,
 * outside a solo game, handles one packet: a host cancel leaves for the join
 * screen; a lobby state prunes the assignments; FLIGHT_ASSIGNMENTS_READY fills
 * the slots still empty and goes to the briefing; a return to setup goes back
 * to mission_setup_update; a countdown packet (type 'e') lowers
 * g_mission_setup_launch_countdown_ms to its value; reservations update
 * g_mission_setup_reserved_player_ids; a pilot rating updates its sender's roster
 * entry. With more than 4 player flight groups it shows, by
 * g_mission_setup_use_expanded_assignment_layout, the mission description and the
 * flight slots (1) or the briefing map and the assigned players (0); with 4 or
 * fewer, the briefing map and the flight slots. Then the pilot banner and,
 * outside a solo game with more than one team, the countdown: the ms elapsed
 * since the last frame come off g_mission_setup_launch_countdown_ms, and the host
 * sends every player a countdown packet whenever the whole seconds change; once
 * it is under 0 it stays at 0, the captain of the pilot's team (slot 0) sends
 * FLIGHT_ASSIGNMENTS_READY to each player of the team once, and the function
 * returns 0 each frame before its buttons. Previous returns a solo game to team
 * assignment, asking first in a tournament or battle sequence (mission type
 * melee or combat engagement); a host's Restart asks, then sends every player
 * RETURN_TO_SETUP; a client's Leave asks, then leaves for the join screen.
 * Next, once mission_setup_are_flight_assignments_complete returns nonzero, goes to
 * the briefing in a solo game, or for the team's captain sends
 * FLIGHT_ASSIGNMENTS_READY to each player of the team. Last it handles the
 * assignment controls and the drag: a left click ends it, outside a solo game
 * sending RELEASE_FLIGHT_RESERVATION (the code tests the left click twice and
 * never the right); else the dragged name is drawn at the cursor. Returns 1
 * when frontend_handle_common_screen_controls returns 1, else 0. */
// FUNCTION: XVT 0x4F5170
int mission_setup_flight_assignment_update(int frame_counter)
{
	enum {
		MAX_PLAYERS = 8,
		MAX_TEAMS = 10,
		MAX_COMPACT_FLIGHT_GROUPS = 4,
		BRIEFING_TEXT_CAPACITY = 4096,
		LAUNCH_COUNTDOWN_MS = 120000,
		DRAG_INPUT_GATE = 3,
		PACKET_COUNTDOWN = 'e',
	};

	int flight_group_index;
	int cursor_x;
	int cursor_y;
	struct RECT rect;

	if (frame_counter == 0) {
		g_mission_setup_launch_signal_sent = 0;
		if (g_team_player_flight_group_count[g_pilot_data.team] > 1) {
			g_frontend_chat_team_only = 1;
		}
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_frontend_game_session_in_progress = 1;
		}
		frontend_cursor_set_pos(37, 445);
		g_frontend_first_visible_line = 0;
		g_mission_setup_reserved_player_count = 0;
		memset(g_mission_setup_reserved_player_ids, 0,
		       sizeof(g_mission_setup_reserved_player_ids));
		g_frontend_briefing_entered_count = 0;
		g_mission_setup_use_expanded_assignment_layout = 0;
		frontend_mission_load_for_briefing();
		front_image_load_resource_list("frontres\\mapicons.lst");

		g_text_shade_ramps[0][0] =
			frontend_display_pack_rgb(0, 0x48, 0);
		g_text_shade_ramps[0][1] =
			frontend_display_pack_rgb(0, 0x60, 0);
		g_text_shade_ramps[0][2] =
			frontend_display_pack_rgb(0, 0x78, 0);
		g_text_shade_ramps[0][3] =
			frontend_display_pack_rgb(0, 0x94, 0);
		g_text_shade_ramps[0][4] =
			frontend_display_pack_rgb(0, 0xAC, 0);
		g_text_shade_ramps[0][5] =
			frontend_display_pack_rgb(0, 0xC8, 0);
		g_text_shade_ramps[0][6] =
			frontend_display_pack_rgb(0, 0xE0, 0);
		g_text_shade_ramps[0][7] =
			frontend_display_pack_rgb(0, 0xFC, 0);
		g_text_shade_ramps[1][0] =
			frontend_display_pack_rgb(0x48, 0, 0);
		g_text_shade_ramps[1][1] =
			frontend_display_pack_rgb(0x60, 0, 0);
		g_text_shade_ramps[1][2] =
			frontend_display_pack_rgb(0x78, 0, 0);
		g_text_shade_ramps[1][3] =
			frontend_display_pack_rgb(0x94, 0, 0);
		g_text_shade_ramps[1][4] =
			frontend_display_pack_rgb(0xAC, 0, 0);
		g_text_shade_ramps[1][5] =
			frontend_display_pack_rgb(0xC8, 0, 0);
		g_text_shade_ramps[1][6] =
			frontend_display_pack_rgb(0xE0, 0, 0);
		g_text_shade_ramps[1][7] =
			frontend_display_pack_rgb(0xFC, 0, 0);
		g_text_shade_ramps[2][0] =
			frontend_display_pack_rgb(0x48, 0x48, 0);
		g_text_shade_ramps[2][1] =
			frontend_display_pack_rgb(0x60, 0x60, 0);
		g_text_shade_ramps[2][2] =
			frontend_display_pack_rgb(0x78, 0x78, 0);
		g_text_shade_ramps[2][3] =
			frontend_display_pack_rgb(0x94, 0x94, 0);
		g_text_shade_ramps[2][4] =
			frontend_display_pack_rgb(0xAC, 0xAC, 0);
		g_text_shade_ramps[2][5] =
			frontend_display_pack_rgb(0xC8, 0xC8, 0);
		g_text_shade_ramps[2][6] =
			frontend_display_pack_rgb(0xE0, 0xE0, 0);
		g_text_shade_ramps[2][7] =
			frontend_display_pack_rgb(0xFC, 0xFC, 0);
		g_text_shade_ramps[3][0] =
			frontend_display_pack_rgb(0, 0, 0x48);
		g_text_shade_ramps[3][1] =
			frontend_display_pack_rgb(0, 0, 0x60);
		g_text_shade_ramps[3][2] =
			frontend_display_pack_rgb(0, 0, 0x78);
		g_text_shade_ramps[3][3] =
			frontend_display_pack_rgb(0, 0, 0x94);
		g_text_shade_ramps[3][4] =
			frontend_display_pack_rgb(0, 0, 0xAC);
		g_text_shade_ramps[3][5] =
			frontend_display_pack_rgb(0, 0, 0xC8);
		g_text_shade_ramps[3][6] =
			frontend_display_pack_rgb(0, 0, 0xE0);
		g_text_shade_ramps[3][7] =
			frontend_display_pack_rgb(0, 0, 0xFC);
		g_text_shade_ramps[4][0] =
			frontend_display_pack_rgb(0x48, 0, 0x48);
		g_text_shade_ramps[4][1] =
			frontend_display_pack_rgb(0x60, 0, 0x60);
		g_text_shade_ramps[4][2] =
			frontend_display_pack_rgb(0x78, 0, 0x78);
		g_text_shade_ramps[4][3] =
			frontend_display_pack_rgb(0x94, 0, 0x94);
		g_text_shade_ramps[4][4] =
			frontend_display_pack_rgb(0xAC, 0, 0xAC);
		g_text_shade_ramps[4][5] =
			frontend_display_pack_rgb(0xC8, 0, 0xC8);
		g_text_shade_ramps[4][6] =
			frontend_display_pack_rgb(0xE0, 0, 0xE0);
		g_text_shade_ramps[4][7] =
			frontend_display_pack_rgb(0xFC, 0, 0xFC);

		int team_index;
		if (g_frontend_skip_screen_entry_setup == 0) {
			for (team_index = 0; team_index < MAX_TEAMS;
			     ++team_index) {
				memset(&g_mission_setup_player_flight_group_indices
					       [team_index * MAX_PLAYERS],
				       0xFF,
				       MAX_PLAYERS *
					       sizeof(g_mission_setup_player_flight_group_indices
							      [0]));
			}
		}
		mission_setup_prune_flight_assignments();
		g_mission_setup_dragged_player_id = 0;
		for (team_index = 0; team_index < g_team_count; ++team_index) {
			int flight_group_slot = 0;
			for (flight_group_index = 0;
			     flight_group_index <
			     (int16_t)g_frontend_mission.flight_group_count;
			     ++flight_group_index) {
				if (g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .player_number != 0 &&
				    g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .team == team_index &&
				    flight_group_slot < MAX_PLAYERS) {
					for (; flight_group_slot < MAX_PLAYERS;
					     ++flight_group_slot) {
						if (g_mission_setup_player_assignments
								    .team_player_ids
									    [team_index]
									    [flight_group_slot] !=
							    0 &&
						    g_mission_setup_player_flight_group_indices
								    [team_index *
									     MAX_PLAYERS +
								     flight_group_slot] ==
							    -1) {
							g_mission_setup_player_flight_group_indices
								[team_index *
									 MAX_PLAYERS +
								 flight_group_slot] =
									flight_group_index;
							break;
						}
					}
				}
			}
		}
		g_frontend_skip_screen_entry_setup = 1;

		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_MELEES) {
			for (team_index = 0; team_index < g_team_count;
			     ++team_index) {
				if (g_team_player_flight_group_count
					    [team_index] > 1) {
					break;
				}
			}
			if (team_index == g_team_count) {
				frontend_screen_set_callbacks(
					mission_briefing_craft_selection_update,
					mission_briefing_craft_selection_exit);
				return 0;
			}
		}
		if (g_frontend_quick_start_launch_flag == 1) {
			frontend_screen_set_callbacks(
				mission_briefing_craft_selection_update,
				mission_briefing_craft_selection_exit);
			return 0;
		}

		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_MELEES ||
		    g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TOURNAMENTS) {
			front_image_register_resource_default(
				"frontres\\player.bmp", "background");
		} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			   g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_BATTLES) {
			if (g_pilot_data.team == 0) {
				front_image_register_resource_default(
					"frontres\\playeri.bmp", "background");
			} else {
				front_image_register_resource_default(
					"frontres\\playerr.bmp", "background");
			}
		} else {
			for (flight_group_index = 0;
			     flight_group_index <
			     (int16_t)g_frontend_mission.flight_group_count;
			     ++flight_group_index) {
				if (g_frontend_mission
					    .flight_groups[flight_group_index]
					    .player_number != 0) {
					if (g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .iff == 0) {
						front_image_register_resource_default(
							"frontres\\playerr.bmp",
							"background");
					} else if (
						g_frontend_mission
							.flight_groups
								[flight_group_index]
							.iff == 1) {
						front_image_register_resource_default(
							"frontres\\playeri.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\player.bmp",
							"background");
					}
					break;
				}
			}
		}

		if (g_team_player_flight_group_count[g_pilot_data.team] >
		    MAX_COMPACT_FLIGHT_GROUPS) {
			g_mission_setup_use_expanded_assignment_layout = 1;
			frontend_cursor_set_pos(33, 319);
			frontend_display_lock_offscreen_surface();
			front_image_draw_sprite_opaque("background", 0, 0);
			front_image_draw_sprite("frame", 0, 0);
			if (g_host_cd_available != 0) {
				front_image_draw_sprite("allactive", 0, 0);
			} else {
				front_image_draw_sprite("clientactive", 0, 0);
			}
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				front_image_draw_sprite_translucent("chatbox",
								    0, 0);
			}
			front_image_draw_sprite_translucent("mapassignoverlay",
							    0, 0);
			/* cursor_x and cursor_y hold each flight group slot
			 * overlay's screen position here, not the mouse. */
			cursor_x = 230;
			cursor_y = 284;
			for (flight_group_index = 0;
			     flight_group_index <
			     (int16_t)g_frontend_mission.flight_group_count;
			     ++flight_group_index) {
				if (g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .player_number != 0 &&
				    g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .team ==
					    g_pilot_data.team) {
					front_image_draw_sprite_translucent(
						"fgslotoverlay", cursor_x,
						cursor_y);
					cursor_y += 17;
				}
			}
		} else {
			frontend_display_lock_offscreen_surface();
			front_image_draw_sprite_opaque("background", 0, 0);
			front_image_draw_sprite("frame", 0, 0);
			if (g_host_cd_available != 0) {
				front_image_draw_sprite("allactive", 0, 0);
			} else {
				front_image_draw_sprite("clientactive", 0, 0);
			}
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				front_image_draw_sprite_translucent("chatbox",
								    0, 0);
			}
			front_image_draw_sprite_translucent("mapoverlay", 0, 0);
			frontend_draw_rect_copy(&rect,
						&g_briefing_map_panel_rect);
			frontend_draw_rect_offset_xy(&rect, 84, 96);
			rect.top = rect.bottom - 27;
			frontend_draw_fill_rect_translucent(&rect, 0, 0,
							    g_color_blue);
			/* cursor_x and cursor_y hold each flight group slot
			 * overlay's screen position here, not the mouse. */
			cursor_x = 230;
			cursor_y = 352;
			int drawn_slot_count = 0;
			for (flight_group_index = 0;
			     flight_group_index <
			     (int16_t)g_frontend_mission.flight_group_count;
			     ++flight_group_index) {
				if (g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .player_number != 0 &&
				    g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .team ==
					    g_pilot_data.team) {
					++drawn_slot_count;
					front_image_draw_sprite_translucent(
						"fgslotoverlay", cursor_x,
						cursor_y);
					if (drawn_slot_count >=
					    MAX_COMPACT_FLIGHT_GROUPS) {
						break;
					}
					cursor_y += 17;
				}
			}
		}
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
		g_mission_setup_launch_countdown_ms = LAUNCH_COUNTDOWN_MS;
		g_mission_setup_countdown_clock_ms = GetTickCount();
		g_mission_setup_last_broadcast_countdown_second =
			LAUNCH_COUNTDOWN_MS / 1000;
		g_mission_setup_countdown_previous_clock_ms =
			g_mission_setup_countdown_clock_ms;
		g_mission_text = malloc(BRIEFING_TEXT_CAPACITY);
		mission_setup_load_mission_desc_text(g_mission_text);
	}

	frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
	struct RECT saved_clip_rect;
	frontend_display_get_screen_clip_rect(&saved_clip_rect);
	frontend_display_set_screen_clip_rect640x480(&rect);
	sprintf(g_frontend_scratch_buffer, "%c%s", 4,
		g_mission_list[g_selected_mission_list_index].description);
	for (int text_index = (int)strlen(g_frontend_scratch_buffer) - 1;
	     text_index > 0; --text_index) {
		if (g_frontend_scratch_buffer[text_index] == '(') {
			g_frontend_scratch_buffer[text_index] = '\0';
			break;
		}
	}
	frontend_text_draw_centered(12, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	frontend_display_set_screen_clip_rect640x480(&saved_clip_rect);

	int slot_index;
	int roster_index;
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		int packet_type = frontend_net_process_network_packets();
		if (packet_type == NET_PACKET_HOST_CANCELLED) {
			net_shutdown_direct_play_session();
			if (net_is_host() == 0) {
				frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					frontend_string_get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					frontend_string_get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return xvt_dialog_continue_with(
					xvt_mission_dialogs_resume,
					XVT_MISSION_ASSIGNMENT_CANCELLED);
#endif
			}
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
		} else if (packet_type == NET_PACKET_STATE) {
			mission_setup_prune_flight_assignments();
		} else if (packet_type == NET_PACKET_FLIGHT_ASSIGNMENTS_READY) {
			mission_setup_fill_flight_assignments();
			frontend_screen_set_callbacks(
				mission_briefing_craft_selection_update,
				mission_briefing_craft_selection_exit);
			return 0;
		} else if (packet_type == NET_PACKET_RETURN_TO_SETUP) {
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				mission_setup_update,
				(frontend_screen_exit_fn)mission_setup_exit);
			return 0;
		} else if (packet_type == PACKET_COUNTDOWN) {
			if (g_mission_setup_launch_countdown_ms >
			    g_frontend_net_packet_arg0) {
				g_mission_setup_launch_countdown_ms =
					g_frontend_net_packet_arg0;
			}
		} else if (packet_type ==
			   NET_PACKET_RELEASE_FLIGHT_RESERVATION) {
			for (slot_index = 0;
			     slot_index < g_mission_setup_reserved_player_count;
			     ++slot_index) {
				if (g_mission_setup_reserved_player_ids
					    [slot_index] ==
				    g_frontend_net_packet_arg0) {
					--g_mission_setup_reserved_player_count;
					break;
				}
			}
			for (; slot_index < MAX_PLAYERS - 1; ++slot_index) {
				g_mission_setup_reserved_player_ids[slot_index] =
					g_mission_setup_reserved_player_ids
						[slot_index + 1];
			}
			g_mission_setup_reserved_player_ids[MAX_PLAYERS - 1] =
				0;
		} else if (packet_type == NET_PACKET_FLIGHT_RESERVATION) {
			for (slot_index = 0;
			     slot_index < g_mission_setup_reserved_player_count;
			     ++slot_index) {
				if (g_mission_setup_reserved_player_ids
					    [slot_index] ==
				    g_frontend_net_packet_arg0) {
					break;
				}
			}
			if (slot_index ==
			    g_mission_setup_reserved_player_count) {
				g_mission_setup_reserved_player_ids
					[g_mission_setup_reserved_player_count++] =
						g_frontend_net_packet_arg0;
			}
		} else if (packet_type == NET_PACKET_PILOT_RATING) {
			for (roster_index = 0; roster_index < MAX_PLAYERS;
			     ++roster_index) {
				if (g_mp_roster[roster_index].player_id ==
				    g_frontend_net_packet_sender_player_id) {
					g_mp_roster[roster_index].pilot_rating =
						g_frontend_net_packet_arg0;
					break;
				}
			}
		}
	}

	struct RECT map_rect;
	if (g_team_player_flight_group_count[g_pilot_data.team] >
	    MAX_COMPACT_FLIGHT_GROUPS) {
		if (g_mission_setup_use_expanded_assignment_layout == 0) {
			frontend_draw_rect_assign(&rect, 84, 96, 443, 335);
			frontend_draw_rect_assign(&map_rect, 84, 96, 443, 335);
			frontend_display_get_screen_clip_rect(&saved_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&map_rect);
			frontend_cursor_get_pos(&cursor_x, &cursor_y);
			mission_briefing_handle_map_mouse_input(
				&rect, &map_rect, 0,
				frontend_mouse_get_left_down(),
				frontend_mouse_get_right_down(),
				(int16_t)cursor_x, (int16_t)cursor_y);
			briefing_script_advance_or_reset_at_end(frame_counter);
			mission_briefing_draw_map_viewport(&rect, &map_rect, 1);
			frontend_display_set_screen_clip_rect640x480(
				&saved_clip_rect);
			sprintf(g_frontend_scratch_buffer, "%s %c%s",
				frontend_string_get(FRONTSTR_207_BRIEFING), 4,
				g_frontend_mission.teams[g_pilot_data.team]
					.name);
			frontend_text_draw(12, g_frontend_scratch_buffer, 86,
					   92, 0xFFFF);
			mission_setup_draw_assigned_players(frame_counter);
		} else {
			mission_setup_draw_assignment_mission_description();
			mission_setup_draw_flight_assignments(frame_counter);
		}
	} else {
		frontend_draw_rect_assign(&rect, 84, 96, 443, 335);
		frontend_draw_rect_assign(&map_rect, 84, 96, 443, 335);
		frontend_display_get_screen_clip_rect(&saved_clip_rect);
		frontend_display_set_screen_clip_rect640x480(&map_rect);
		frontend_cursor_get_pos(&cursor_x, &cursor_y);
		mission_briefing_handle_map_mouse_input(
			&rect, &map_rect, 0, frontend_mouse_get_left_down(),
			frontend_mouse_get_right_down(), (int16_t)cursor_x,
			(int16_t)cursor_y);
		briefing_script_advance_or_reset_at_end(frame_counter);
		mission_briefing_draw_map_viewport(&rect, &map_rect, 1);
		frontend_display_set_screen_clip_rect640x480(&saved_clip_rect);
		sprintf(g_frontend_scratch_buffer, "%s %c%s",
			frontend_string_get(FRONTSTR_207_BRIEFING), 4,
			g_frontend_mission.teams[g_pilot_data.team].name);
		frontend_text_draw(12, g_frontend_scratch_buffer, 86, 92,
				   0xFFFF);
		mission_setup_draw_flight_assignments(frame_counter);
	}

	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
		int animation_frame;
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_MELEES ||
		    g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TOURNAMENTS) {
			animation_frame = (frame_counter % 32) >> 1;
			sprintf(g_frontend_scratch_buffer, "rebtiny%d",
				animation_frame);
			front_image_draw_sprite(g_frontend_scratch_buffer, 204,
						453);
			sprintf(g_frontend_scratch_buffer, "imptiny%d",
				animation_frame);
			front_image_draw_sprite(g_frontend_scratch_buffer, 420,
						453);
		} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			   g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_BATTLES) {
			if (g_pilot_data.team == 0) {
				animation_frame = (frame_counter % 32) >> 1;
				sprintf(g_frontend_scratch_buffer, "imptiny%d",
					animation_frame);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 204, 453);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 420, 453);
			} else {
				animation_frame = (frame_counter % 32) >> 1;
				sprintf(g_frontend_scratch_buffer, "rebtiny%d",
					animation_frame);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 204, 453);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 420, 453);
			}
		} else {
			for (flight_group_index = 0;
			     flight_group_index <
			     (int16_t)g_frontend_mission.flight_group_count;
			     ++flight_group_index) {
				if (g_frontend_mission
					    .flight_groups[flight_group_index]
					    .player_number != 0) {
					if (g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .iff == 0) {
						animation_frame =
							(frame_counter % 32) >>
							1;
						sprintf(g_frontend_scratch_buffer,
							"rebtiny%d",
							animation_frame);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							204, 453);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							420, 453);
					} else if (
						g_frontend_mission
							.flight_groups
								[flight_group_index]
							.iff == 1) {
						animation_frame =
							(frame_counter % 32) >>
							1;
						sprintf(g_frontend_scratch_buffer,
							"imptiny%d",
							animation_frame);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							204, 453);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							420, 453);
					} else {
						animation_frame =
							(frame_counter % 32) >>
							1;
						sprintf(g_frontend_scratch_buffer,
							"rebtiny%d",
							animation_frame);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							204, 453);
						sprintf(g_frontend_scratch_buffer,
							"imptiny%d",
							animation_frame);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							420, 453);
					}
					break;
				}
			}
		}
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_team_count > 1) {
		frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
		frontend_format_seconds_to_clock_string(
			g_mission_setup_launch_countdown_ms / 1000);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
	}
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_net_update_and_draw_chat_panel(frame_counter);
	}
	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_team_count > 1) {
		g_mission_setup_countdown_clock_ms = GetTickCount();
		g_mission_setup_launch_countdown_ms +=
			g_mission_setup_countdown_previous_clock_ms -
			g_mission_setup_countdown_clock_ms;
		if (net_is_host() != 0 &&
		    g_mission_setup_last_broadcast_countdown_second !=
			    g_mission_setup_launch_countdown_ms / 1000) {
			g_mission_setup_last_broadcast_countdown_second =
				g_mission_setup_launch_countdown_ms / 1000;
			*(int *)g_frontend_net_packet_scratch.payload =
				g_mission_setup_launch_countdown_ms;
			g_frontend_net_packet_scratch.packet_type =
				PACKET_COUNTDOWN;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				2 * sizeof(g_frontend_net_packet_scratch
						   .packet_type));
		}
		if (g_mission_setup_launch_countdown_ms < 0) {
			g_mission_setup_launch_countdown_ms = 0;
			if (g_mission_setup_launch_signal_sent == 0 &&
			    net_get_local_player_id() ==
				    g_mission_setup_player_assignments
					    .team_player_ids[g_pilot_data.team]
							    [0]) {
				g_mission_setup_launch_signal_sent = 1;
				for (slot_index = 0; slot_index < MAX_PLAYERS;
				     ++slot_index) {
					if (g_mission_setup_player_assignments
						    .team_player_ids
							    [g_pilot_data.team]
							    [slot_index] != 0) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_FLIGHT_ASSIGNMENTS_READY;
						net_send_packet_and_flush(
							g_mission_setup_player_assignments
								.team_player_ids
									[g_pilot_data
										 .team]
									[slot_index],
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type));
					}
				}
			}
			return 0;
		}
		g_mission_setup_countdown_previous_clock_ms =
			g_mission_setup_countdown_clock_ms;
	}

	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_569_PREVIOUS));
		int leave_confirmed;
		if (g_mission_setup_team_assignment_skipped != 0) {
			leave_confirmed = frontend_button_handle_sprite_button(
				&rect, "leaveup", "leavedown",
				frontend_string_get(
					FRONTSTR_260_RETURN_TO_SELECT_MISSION),
				12, 0, 8, "buttonsound");
		} else {
			leave_confirmed = frontend_button_handle_sprite_button(
				&rect, "leaveup", "leavedown",
				frontend_string_get(
					FRONTSTR_261_RETURN_TO_SELECT_TEAMS),
				12, 0, 8, "buttonsound");
		}
		if (leave_confirmed != 0) {
			if (g_pilot_data.mission_sequence_active == 1) {
				if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_MELEES) {
					leave_confirmed = frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
						frontend_string_get(
							FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
						frontend_string_get(
							FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
						frontend_string_get(
							FRONTSTR_523_OKAY),
						frontend_string_get(
							FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
					return xvt_dialog_continue_with(
						xvt_mission_dialogs_resume,
						XVT_MISSION_SOLO_BACK_TO_TEAMS);
#endif
				} else if (
					g_pilot_data.mission_directory_id ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					leave_confirmed = frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
						frontend_string_get(
							FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
						frontend_string_get(
							FRONTSTR_683_TERMINATE_THIS_BATTLE),
						frontend_string_get(
							FRONTSTR_523_OKAY),
						frontend_string_get(
							FRONTSTR_019_CANCEL));
#ifdef XVT_MODERN
					return xvt_dialog_continue_with(
						xvt_mission_dialogs_resume,
						XVT_MISSION_SOLO_BACK_TO_TEAMS);
#endif
				}
			}
			if (leave_confirmed != 0) {
				g_frontend_skip_screen_entry_setup = 1;
				frontend_screen_set_callbacks(
					mission_setup_team_assignment_update,

#ifdef XVT_MODERN
					xvt_frontend_cleanup_mission_resources
#else
					(frontend_screen_exit_fn)
						frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
				);
			}
		}
	} else if (net_is_host() != 0) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_668_RESTART));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
				frontend_string_get(
					FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
				frontend_string_get(
					FRONTSTR_754_TO_SELECT_MISSION),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_HOST_RESTART);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
			    frontend_string_get(
				    FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
			    frontend_string_get(FRONTSTR_754_TO_SELECT_MISSION),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_RETURN_TO_SETUP;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type));
		}
#endif

	} else {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_204_LEAVE));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				frontend_string_get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				frontend_string_get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    frontend_string_get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    frontend_string_get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_LEFT;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type));
			net_shutdown_direct_play_session();
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
		}
#endif
	}

	frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (mission_setup_are_flight_assignments_complete() != 0) {
			frontend_button_set_overlay_text(
				frontend_string_get(FRONTSTR_212_NEXT));
			if (frontend_button_handle_sprite_button(
				    &rect, "nextup", "nextdown",
				    frontend_string_get(
					    FRONTSTR_667_GO_TO_CRAFT_SELECTION),
				    12, 0, 7, "flysound") != 0) {
				frontend_screen_set_callbacks(
					mission_briefing_craft_selection_update,
					mission_briefing_craft_selection_exit);
				frontend_button_disable_overlay_text();
				return 0;
			}
		}
	} else if (net_get_local_player_id() ==
			   g_mission_setup_player_assignments
				   .team_player_ids[g_pilot_data.team][0] &&
		   mission_setup_are_flight_assignments_complete() != 0) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_212_NEXT));
		if (frontend_button_handle_sprite_button(
			    &rect, "nextup", "nextdown",
			    frontend_string_get(
				    FRONTSTR_667_GO_TO_CRAFT_SELECTION),
			    12, 0, 7, "flysound") != 0) {
			for (slot_index = 0; slot_index < MAX_PLAYERS;
			     ++slot_index) {
				if (g_mission_setup_player_assignments
					    .team_player_ids[g_pilot_data.team]
							    [slot_index] != 0) {
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_FLIGHT_ASSIGNMENTS_READY;
					net_send_packet_and_flush(
						g_mission_setup_player_assignments
							.team_player_ids
								[g_pilot_data
									 .team]
								[slot_index],
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type));
				}
			}
		}
	}
	frontend_button_disable_overlay_text();
	mission_setup_draw_assignment_controls();
	if (frontend_handle_common_screen_controls(1) == 1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (xvt_dialog_is_active()) {
		return 0;
	}
#endif

	if (frontend_mouse_is_gate_owner(DRAG_INPUT_GATE)) {
		if (frontend_mouse_get_left_click_for(DRAG_INPUT_GATE) != 0 ||
		    frontend_mouse_get_left_click_for(DRAG_INPUT_GATE) != 0) {
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				*(int *)g_frontend_net_packet_scratch.payload =
					g_mission_setup_dragged_player_id;
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_RELEASE_FLIGHT_RESERVATION;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					2 * sizeof(g_frontend_net_packet_scratch
							   .packet_type));
			}
			g_mission_setup_dragged_player_id = 0;
			frontend_mouse_clear_input_gate();
			return 0;
		}
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			if (g_mp_roster[roster_index].player_id != 0 &&
			    g_mp_roster[roster_index].player_id ==
				    g_mission_setup_dragged_player_id) {
				frontend_cursor_get_pos(&cursor_x, &cursor_y);
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				if (net_get_local_player_id() ==
					    g_mp_roster[roster_index]
						    .player_id ||
				    g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					frontend_text_draw(
						12, g_frontend_scratch_buffer,
						cursor_x - 7, cursor_y - 7,
						g_pulse_color_ramp
							[((frame_counter % 24) &
							  ~1) >>
							 1]);
				} else {
					frontend_text_draw(
						12, g_frontend_scratch_buffer,
						cursor_x - 7, cursor_y - 7,
						g_color_yellow);
				}
				return 0;
			}
		}
	}
	return 0;
}

/* Draws the side buttons of the flight assignment screen and handles them. The
 * captain's buttons belong to the local player when it holds slot 0 of
 * g_pilot_data.team, and to every solo player. While the briefing map shows
 * (g_mission_setup_use_expanded_assignment_layout 0) there are the map's buttons:
 * Play sets g_briefing_playback_active and Stop clears it; a click on Forward
 * while playing advances the briefing a line and restarts it when
 * g_briefing_text_slot_block_idx[1] then equals g_briefing_last_narrated_text_block_idx;
 * Rewind restarts it. When the pilot's team has more than 4 player flight
 * groups, Assign Players and View Briefing Map set
 * g_mission_setup_use_expanded_assignment_layout to 1 or 0 and redraw the screen's
 * base (Assign Players also resets g_frontend_first_visible_line, View Briefing
 * Map puts the cursor at 37, 445), and the captain gets Clear List and Auto
 * Assign in the expanded layout. With 2 to 4 player flight groups the captain
 * gets Clear List and Auto Assign under the map. Clear List calls
 * mission_setup_clear_flight_assignments, Auto Assign
 * mission_setup_randomize_flight_assignments. Returns 0. */
// FUNCTION: XVT 0x4F8E40
int mission_setup_draw_assignment_controls(void)
{
	enum {
		NAV_SLOT_PLAY = 0,
		NAV_SLOT_STOP = 1,
		NAV_SLOT_REWIND = 3,
		NAV_SLOT_EXPANDED_AUTO_ASSIGN = 4,
		NAV_SLOT_FIRST_ASSIGNMENT = 5,
		NAV_SLOT_SECOND_ASSIGNMENT = 6,
		NAV_SLOT_EXPANDED_CLEAR_LIST = 7,
		NAV_SLOT_COUNT = 8,
		COMPACT_FLIGHT_GROUP_LIMIT = 4,
		SINGLE_FLIGHT_GROUP_COUNT = 1,
		BUTTON_LEFT = 22,
		BUTTON_RIGHT = 42,
		MAP_BUTTON_TOP = 198,
		MAP_BUTTON_BOTTOM = 222,
		EXPANDED_TOP_BUTTON_TOP = 226,
		EXPANDED_TOP_BUTTON_BOTTOM = 250,
		EXPANDED_SECOND_BUTTON_TOP = 254,
		EXPANDED_SECOND_BUTTON_BOTTOM = 278,
		COMPACT_ASSIGN_BUTTON_TOP = 306,
		COMPACT_ASSIGN_BUTTON_BOTTOM = 330,
		BOTTOM_BUTTON_TOP = 334,
		BOTTOM_BUTTON_BOTTOM = 358,
		BUTTON_ROW_OFFSET = 28,
		REWIND_TO_STOP_OFFSET = -56,
		BUTTON_FONT_SIZE = 12,
		PLAY_OR_AUTO_ASSIGN_HELD_SLOT = 11,
		STOP_OR_CLEAR_LIST_HELD_SLOT = 12,
		REWIND_HELD_SLOT = 14,
		BRIEFING_MAP_OR_AUTO_ASSIGN_HELD_SLOT = 16,
		ASSIGN_PLAYERS_OR_CLEAR_LIST_HELD_SLOT = 17,
		ASSIGNMENT_OVERLAY_X = 230,
		ASSIGNMENT_OVERLAY_Y = 284,
		ASSIGNMENT_OVERLAY_ROW_HEIGHT = 17,
		BRIEFING_MAP_OFFSET_X = 84,
		BRIEFING_MAP_OFFSET_Y = 96,
		BRIEFING_NARRATION_HEIGHT = 27,
		BRIEFING_CURSOR_X = 37,
		BRIEFING_CURSOR_Y = 445,
		UI_SOUND_PRIORITY = 255,
		UI_SOUND_VOLUME_SCALE = 12,
		UI_SOUND_PAN_CENTER = 63
	};

	int captain_controls = net_get_local_player_id() ==
			       g_mission_setup_player_assignments
				       .team_player_ids[g_pilot_data.team][0];
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		captain_controls = 1;
	}
	int expanded_at_start = g_mission_setup_use_expanded_assignment_layout;
	frontend_navigation_slot_state slot_states[NAV_SLOT_COUNT];
	if (expanded_at_start == 0) {
		slot_states[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slot_states[NAV_SLOT_PLAY] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[NAV_SLOT_STOP] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[NAV_SLOT_REWIND] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[NAV_SLOT_EXPANDED_AUTO_ASSIGN] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slot_states[NAV_SLOT_EXPANDED_CLEAR_LIST] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
	} else {
		slot_states[NAV_SLOT_PLAY] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slot_states[NAV_SLOT_STOP] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slot_states[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
		slot_states[NAV_SLOT_REWIND] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
		if (captain_controls != 0) {
			slot_states[NAV_SLOT_EXPANDED_AUTO_ASSIGN] =
				FRONTEND_NAVIGATION_SLOT_ACTIVE;
			slot_states[NAV_SLOT_EXPANDED_CLEAR_LIST] =
				FRONTEND_NAVIGATION_SLOT_ACTIVE;
		} else {
			slot_states[NAV_SLOT_EXPANDED_AUTO_ASSIGN] =
				FRONTEND_NAVIGATION_SLOT_INACTIVE;
			slot_states[NAV_SLOT_EXPANDED_CLEAR_LIST] =
				FRONTEND_NAVIGATION_SLOT_INACTIVE;
		}
	}

	slot_states[NAV_SLOT_FIRST_ASSIGNMENT] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[NAV_SLOT_SECOND_ASSIGNMENT] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	int team_flight_groups =
		g_team_player_flight_group_count[g_pilot_data.team];
	if (team_flight_groups > COMPACT_FLIGHT_GROUP_LIMIT) {
		slot_states[NAV_SLOT_FIRST_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[NAV_SLOT_SECOND_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[NAV_SLOT_FIRST_ASSIGNMENT +
			    g_mission_setup_use_expanded_assignment_layout] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else if (captain_controls != 0 &&
		   team_flight_groups > SINGLE_FLIGHT_GROUP_COUNT) {
		slot_states[NAV_SLOT_FIRST_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
		slot_states[NAV_SLOT_SECOND_ASSIGNMENT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
	}
	if (expanded_at_start == 0) {
		slot_states[g_briefing_playback_active ^ 1] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}

	struct RECT rect;
	frontend_draw_rect_assign(&rect, BUTTON_LEFT, MAP_BUTTON_TOP,
				  BUTTON_RIGHT, MAP_BUTTON_BOTTOM);
	int cursor_x;
	int cursor_y;
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	if (!g_mission_setup_use_expanded_assignment_layout &&
	    frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (frontend_mouse_get_left_down() != 0 ||
	     frontend_mouse_get_right_down() != 0 ||
	     frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		slot_states[NAV_SLOT_REWIND] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}

	int slot_index;
	if (g_team_player_flight_group_count[g_pilot_data.team] >
	    COMPACT_FLIGHT_GROUP_LIMIT) {
		if (g_mission_setup_use_expanded_assignment_layout == 1 &&
		    captain_controls != 0) {
			frontend_draw_rect_assign(
				&rect, BUTTON_LEFT, EXPANDED_TOP_BUTTON_TOP,
				BUTTON_RIGHT, EXPANDED_TOP_BUTTON_BOTTOM);
			for (slot_index = 0; slot_index < 2; ++slot_index) {
				if (frontend_draw_point_in_rect(&rect, cursor_x,
								cursor_y) &&
				    (frontend_mouse_get_left_down() != 0 ||
				     frontend_mouse_get_right_down() != 0 ||
				     frontend_mouse_get_left_click() != 0 ||
				     frontend_mouse_get_right_click() != 0)) {
					if (slot_index == 0) {
						slot_states[NAV_SLOT_FIRST_ASSIGNMENT] =
							FRONTEND_NAVIGATION_SLOT_SELECTED;
					} else {
						slot_states[NAV_SLOT_SECOND_ASSIGNMENT] =
							FRONTEND_NAVIGATION_SLOT_SELECTED;
					}
				}
				frontend_draw_rect_offset_xy(&rect, 0,
							     BUTTON_ROW_OFFSET);
			}
		}
	} else if (captain_controls != 0 &&
		   slot_states[NAV_SLOT_FIRST_ASSIGNMENT] !=
			   FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		frontend_draw_rect_assign(
			&rect, BUTTON_LEFT, COMPACT_ASSIGN_BUTTON_TOP,
			BUTTON_RIGHT, COMPACT_ASSIGN_BUTTON_BOTTOM);
		for (slot_index = NAV_SLOT_FIRST_ASSIGNMENT;
		     slot_index < NAV_SLOT_EXPANDED_CLEAR_LIST; ++slot_index) {
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_down() != 0 ||
			     frontend_mouse_get_right_down() != 0 ||
			     frontend_mouse_get_left_click() != 0 ||
			     frontend_mouse_get_right_click() != 0)) {
				slot_states[slot_index] =
					FRONTEND_NAVIGATION_SLOT_SELECTED;
			}
			frontend_draw_rect_offset_xy(&rect, 0,
						     BUTTON_ROW_OFFSET);
		}
	}
	frontend_button_draw_eight_slot_navigation_state(slot_states);

	if (g_team_player_flight_group_count[g_pilot_data.team] >
	    COMPACT_FLIGHT_GROUP_LIMIT) {
		frontend_draw_rect_assign(&rect, BUTTON_LEFT, BOTTOM_BUTTON_TOP,
					  BUTTON_RIGHT, BOTTOM_BUTTON_BOTTOM);
		if (g_mission_setup_use_expanded_assignment_layout == 1) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "play2d",
				frontend_string_get(
					FRONTSTR_799_ASSIGN_PLAYERS),
				BUTTON_FONT_SIZE, 0);
		} else if (frontend_button_handle_sprite_button(
				   &rect, "play2u", "play2u",
				   frontend_string_get(
					   FRONTSTR_799_ASSIGN_PLAYERS),
				   BUTTON_FONT_SIZE, 0,
				   ASSIGN_PLAYERS_OR_CLEAR_LIST_HELD_SLOT,
				   "jewelsound") != 0) {
			g_mission_setup_use_expanded_assignment_layout = 1;
			g_frontend_first_visible_line = 0;
			frontend_display_lock_offscreen_surface();
			front_image_draw_sprite_opaque("background", 0, 0);
			front_image_draw_sprite("frame", 0, 0);
			if (g_host_cd_available != 0) {
				front_image_draw_sprite("allactive", 0, 0);
			} else {
				front_image_draw_sprite("clientactive", 0, 0);
			}
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				front_image_draw_sprite_translucent("chatbox",
								    0, 0);
			}
			front_image_draw_sprite_translucent("mapassignoverlay",
							    0, 0);
			cursor_x = ASSIGNMENT_OVERLAY_X;
			cursor_y = ASSIGNMENT_OVERLAY_Y;
			for (int flight_group_index = 0;
			     flight_group_index <
			     (int16_t)g_frontend_mission.flight_group_count;
			     ++flight_group_index) {
				const struct xvt_flight_group *flight_group =
					&g_frontend_mission.flight_groups
						 [flight_group_index];
				if (flight_group->player_number != 0 &&
				    flight_group->team == g_pilot_data.team) {
					front_image_draw_sprite_translucent(
						"fgslotoverlay", cursor_x,
						cursor_y);
					cursor_y +=
						ASSIGNMENT_OVERLAY_ROW_HEIGHT;
				}
			}
			frontend_display_unlock_offscreen_surface(1);
		}

		frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_ROW_OFFSET);
		if (g_mission_setup_use_expanded_assignment_layout == 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "play1d",
				frontend_string_get(
					FRONTSTR_800_VIEW_BRIEFING_MAP),
				BUTTON_FONT_SIZE, 0);
		} else {
			if (frontend_button_handle_sprite_button(
				    &rect, "play1u", "play1u",
				    frontend_string_get(
					    FRONTSTR_800_VIEW_BRIEFING_MAP),
				    BUTTON_FONT_SIZE, 0,
				    BRIEFING_MAP_OR_AUTO_ASSIGN_HELD_SLOT,
				    "jewelsound") != 0) {
				g_mission_setup_use_expanded_assignment_layout =
					0;
				frontend_display_lock_offscreen_surface();
				front_image_draw_sprite_opaque("background", 0,
							       0);
				front_image_draw_sprite("frame", 0, 0);
				if (g_host_cd_available != 0) {
					front_image_draw_sprite("allactive", 0,
								0);
				} else {
					front_image_draw_sprite("clientactive",
								0, 0);
				}
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					front_image_draw_sprite_translucent(
						"chatbox", 0, 0);
				}
				front_image_draw_sprite_translucent(
					"mapoverlay", 0, 0);
				frontend_draw_rect_copy(
					&rect, &g_briefing_map_panel_rect);
				frontend_draw_rect_offset_xy(
					&rect, BRIEFING_MAP_OFFSET_X,
					BRIEFING_MAP_OFFSET_Y);
				rect.top =
					rect.bottom - BRIEFING_NARRATION_HEIGHT;
				frontend_draw_fill_rect_translucent(
					&rect, 0, 0,
					(unsigned int)g_color_blue);
				frontend_display_unlock_offscreen_surface(1);
				frontend_cursor_set_pos(BRIEFING_CURSOR_X,
							BRIEFING_CURSOR_Y);
			}
		}
	} else if (captain_controls != 0 &&
		   slot_states[NAV_SLOT_FIRST_ASSIGNMENT] !=
			   FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		frontend_draw_rect_assign(&rect, BUTTON_LEFT, BOTTOM_BUTTON_TOP,
					  BUTTON_RIGHT, BOTTOM_BUTTON_BOTTOM);
		if (frontend_button_handle_sprite_button(
			    &rect, "clearu", "cleard",
			    frontend_string_get(FRONTSTR_215_CLEAR_LIST),
			    BUTTON_FONT_SIZE, 0,
			    ASSIGN_PLAYERS_OR_CLEAR_LIST_HELD_SLOT,
			    "jewelsound") != 0) {
			mission_setup_clear_flight_assignments();
		}
		frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_ROW_OFFSET);
		if (frontend_button_handle_sprite_button(
			    &rect, "assignu", "assignd",
			    frontend_string_get(FRONTSTR_214_AUTO_ASSIGN),
			    BUTTON_FONT_SIZE, 0,
			    BRIEFING_MAP_OR_AUTO_ASSIGN_HELD_SLOT,
			    "jewelsound") != 0) {
			mission_setup_randomize_flight_assignments();
		}
	}

	if (g_mission_setup_use_expanded_assignment_layout == 0) {
		frontend_draw_rect_assign(&rect, BUTTON_LEFT, MAP_BUTTON_TOP,
					  BUTTON_RIGHT, MAP_BUTTON_BOTTOM);
		if (frontend_button_handle_sprite_button(
			    &rect, "map4u", "map4d",
			    frontend_string_get(FRONTSTR_210_REWIND),
			    BUTTON_FONT_SIZE, 0, REWIND_HELD_SLOT,
			    "jewelsound") != 0) {
			g_briefing_last_narrated_text_block_idx = 0;
			g_briefing_text_page_number = 0;
			briefing_script_reset_state();
		}
		frontend_draw_rect_offset_xy(&rect, 0, REWIND_TO_STOP_OFFSET);
		if (g_briefing_playback_active == 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "map2d",
				frontend_string_get(FRONTSTR_208_STOP),
				BUTTON_FONT_SIZE, 0);
		} else {
			if (frontend_button_handle_sprite_button(
				    &rect, "map2u", "map2u",
				    frontend_string_get(FRONTSTR_208_STOP),
				    BUTTON_FONT_SIZE, 0,
				    STOP_OR_CLEAR_LIST_HELD_SLOT,
				    "jewelsound") != 0) {
				g_briefing_playback_active = 0;
			}
		}
		frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_ROW_OFFSET);
		if (g_briefing_playback_active != 0) {
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_click() != 0 ||
			     frontend_mouse_get_right_click() != 0)) {
				if (g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"jewelsound", 1, 0,
						UI_SOUND_PRIORITY,
						UI_SOUND_VOLUME_SCALE *
							g_game_config
								.sfx_datapad_volume,
						UI_SOUND_PAN_CENTER);
				}
				briefing_script_advance_to_next_visible_line();
				if (g_briefing_text_slot_block_idx[1] ==
				    g_briefing_last_narrated_text_block_idx) {
					g_briefing_last_narrated_text_block_idx =
						0;
					g_briefing_text_page_number = 0;
					briefing_script_reset_state();
				}
			}
			frontend_button_draw_sprite_and_tooltip(
				&rect, "map1d",
				frontend_string_get(FRONTSTR_209_FORWARD),
				BUTTON_FONT_SIZE, 0);
		} else if (frontend_button_handle_sprite_button(
				   &rect, "map1u", "map1u",
				   frontend_string_get(FRONTSTR_561_PLAY),
				   BUTTON_FONT_SIZE, 0,
				   PLAY_OR_AUTO_ASSIGN_HELD_SLOT,
				   "jewelsound") != 0) {
			g_briefing_playback_active = 1;
		}
	} else if (captain_controls != 0 &&
		   slot_states[NAV_SLOT_EXPANDED_AUTO_ASSIGN] !=
			   FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		frontend_draw_rect_assign(
			&rect, BUTTON_LEFT, EXPANDED_SECOND_BUTTON_TOP,
			BUTTON_RIGHT, EXPANDED_SECOND_BUTTON_BOTTOM);
		if (frontend_button_handle_sprite_button(
			    &rect, "clear2u", "clear2d",
			    frontend_string_get(FRONTSTR_215_CLEAR_LIST),
			    BUTTON_FONT_SIZE, 0, STOP_OR_CLEAR_LIST_HELD_SLOT,
			    "jewelsound") != 0) {
			mission_setup_clear_flight_assignments();
		}
		frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_ROW_OFFSET);
		if (frontend_button_handle_sprite_button(
			    &rect, "assign2u", "assign2d",
			    frontend_string_get(FRONTSTR_214_AUTO_ASSIGN),
			    BUTTON_FONT_SIZE, 0, PLAY_OR_AUTO_ASSIGN_HELD_SLOT,
			    "jewelsound") != 0) {
			mission_setup_randomize_flight_assignments();
		}
	}
	return 0;
}

/* Draws the flight assignment table of the pilot's team and handles drags and
 * drops on it: the duty roster of the team's player flight groups (number,
 * craft and name in a color by IFF, and the first order's designation or the
 * General text), each with its assigned pilot, and up to 4 of the team's
 * players who have no flight group, gray when reserved. With more than 4 player
 * flight groups the table sits 68 pixels higher. With more than one, a help
 * line tells the team's captain (the local player in slot 0 of
 * g_pilot_data.team) or a solo player to drag names into pilot slots, and others
 * to wait. The captain or a solo player pressing a mouse button on an
 * unassigned player starts a drag (input gate 3); with more than one player
 * flight group, so does a press on a flight group's pilot, and a solo game then
 * takes that pilot's flight group away (-1). A captain drags only a player not
 * reserved, reserving it and sending every player a FLIGHT_RESERVATION (the
 * player and the local id) and a FLIGHT_ASSIGNMENT_NOTIFY clearing each of the
 * team's slots that holds the player with a flight group. With more than one
 * player flight group, a click while dragging on a flight group's pilot cell
 * drops the player there: a solo game sets the dragged player's
 * g_mission_setup_player_flight_group_indices entry to that flight group, clearing
 * the pilot it replaces; a captain instead sends FLIGHT_ASSIGNMENT_NOTIFY
 * packets, clearing the replaced pilot and then assigning the dragged player's
 * slot, and a RELEASE_FLIGHT_RESERVATION. Either way the drag ends. Returns
 * 1. */
// FUNCTION: XVT 0x4F9750
int mission_setup_draw_flight_assignments(int frame_counter)
{
	enum {
		PLAYER_SLOTS_PER_TEAM = 8,
		MAX_UNASSIGNED_ROWS = 4,
		INPUT_GATE = 3,
		HEADER_Y = 335,
		LIST_Y = 352,
		COLUMN_DUTY = 88,
		COLUMN_ASSIGNED = 232,
		COLUMN_UNASSIGNED = 339,
		ROW_HEIGHT = 17,
		FONT_SIZE = 12,
		UI_SOUND_PRIORITY = 255,
		UI_SOUND_VOLUME_SCALE = 12,
		UI_SOUND_PAN_CENTER = 63,
	};

	struct RECT rect;

	int y_offset = g_team_player_flight_group_count[g_pilot_data.team] > 4
			       ? -68
			       : 0;
	frontend_draw_rect_assign(&rect, COLUMN_DUTY, HEADER_Y + y_offset, 229,
				  HEADER_Y + y_offset + 14);
	frontend_text_draw_aligned_in_rect(
		FONT_SIZE, frontend_string_get(FRONTSTR_627_DUTY_ROSTER), &rect,
		0, 1, 0xFFFF);
	frontend_draw_rect_assign(&rect, COLUMN_ASSIGNED, HEADER_Y + y_offset,
				  336, HEADER_Y + y_offset + 14);
	frontend_text_draw_aligned_in_rect(
		FONT_SIZE, frontend_string_get(FRONTSTR_628_ASSIGNED_PILOTS),
		&rect, 0, 1, 0xFFFF);
	frontend_draw_rect_assign(&rect, COLUMN_UNASSIGNED, HEADER_Y + y_offset,
				  443, HEADER_Y + y_offset + 14);
	frontend_text_draw_aligned_in_rect(
		FONT_SIZE, frontend_string_get(FRONTSTR_685_UNASSIGNED_PLAYERS),
		&rect, 0, 1, 0xFFFF);
	int is_team_captain = net_get_local_player_id() ==
			      g_mission_setup_player_assignments
				      .team_player_ids[g_pilot_data.team][0];
	if (g_team_player_flight_group_count[g_pilot_data.team] > 1) {
		frontend_draw_rect_assign(&rect, COLUMN_DUTY, 422, 443, 436);
		const char *instruction_text;
		int instruction_color;
		if (is_team_captain ||
		    g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			instruction_color = g_color_green;
			instruction_text = frontend_string_get(
				FRONTSTR_629_ASSIGN_BY_DRAGGING_PLAYERS_NAMES_INTO_PILOT_SLOTS);
		} else {
			instruction_color = g_color_red;
			instruction_text = frontend_string_get(
				FRONTSTR_630_PLEASE_WAIT_WHILE_THE_TEAM_CAPTAIN_ASSIGNS_FLIGHT_GROUPS);
		}
		frontend_text_draw_centered(FONT_SIZE, instruction_text, &rect,
					    instruction_color);
	}

	struct RECT saved_clip_rect;
	int team_player_index;
	int cursor_x;
	int cursor_y;
	{
		int displayed_player_count = 0;
		frontend_draw_rect_assign(&rect, COLUMN_UNASSIGNED,
					  LIST_Y + y_offset, 443,
					  LIST_Y + y_offset + 16);
		for (team_player_index = 0;
		     team_player_index <
		     g_team_player_flight_group_count[g_pilot_data.team];
		     ++team_player_index) {
			int assignment_index =
				g_pilot_data.team * PLAYER_SLOTS_PER_TEAM +
				team_player_index;
			int player_id =
				g_mission_setup_player_assignments
					.team_player_ids[g_pilot_data.team]
							[team_player_index];
			int roster_index;
			for (roster_index = 0;
			     roster_index < PLAYER_SLOTS_PER_TEAM;
			     ++roster_index) {
				if (g_mp_roster[roster_index].player_id ==
				    player_id) {
					break;
				}
			}
			if (roster_index == PLAYER_SLOTS_PER_TEAM ||
			    g_mission_setup_player_flight_group_indices
					    [assignment_index] != -1 ||
			    g_mission_setup_dragged_player_id == player_id ||
			    player_id == 0) {
				continue;
			}

			int reserved_index;
			for (reserved_index = 0;
			     reserved_index <
			     g_mission_setup_reserved_player_count;
			     ++reserved_index) {
				if (g_mission_setup_reserved_player_ids
					    [reserved_index] == player_id) {
					break;
				}
			}
			frontend_display_get_screen_clip_rect(&saved_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&rect);
			if (reserved_index ==
			    g_mission_setup_reserved_player_count) {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				if (net_get_local_player_id() == player_id ||
				    g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					frontend_text_draw_aligned_in_rect(
						FONT_SIZE,
						g_frontend_scratch_buffer,
						&rect, 0, 1,
						g_pulse_color_ramp
							[((frame_counter % 24) &
							  ~1) >>
							 1]);
				} else {
					frontend_text_draw_aligned_in_rect(
						FONT_SIZE,
						g_frontend_scratch_buffer,
						&rect, 0, 1, g_color_yellow);
				}
			} else {
				sprintf(g_frontend_scratch_buffer, "%s %s",
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					g_mp_roster[roster_index].name);
				frontend_text_draw_aligned_in_rect(
					FONT_SIZE, g_frontend_scratch_buffer,
					&rect, 0, 1, g_color_gray);
			}
			frontend_display_set_screen_clip_rect640x480(
				&saved_clip_rect);
			++displayed_player_count;

			if ((net_get_local_player_id() ==
				     g_mission_setup_player_assignments
					     .team_player_ids[g_pilot_data.team]
							     [0] ||
			     g_frontend_mission_session_mode ==
				     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
			    !frontend_mouse_is_gate_owner(INPUT_GATE)) {
				frontend_cursor_get_pos(&cursor_x, &cursor_y);
				if ((frontend_mouse_get_left_down() != 0 ||
				     frontend_mouse_get_right_down() != 0) &&
				    frontend_draw_point_in_rect(&rect, cursor_x,
								cursor_y)) {
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_mission_setup_dragged_player_id =
							player_id;
						frontend_mouse_set_input_gate(
							INPUT_GATE);
					} else {
						for (reserved_index = 0;
						     reserved_index <
						     g_mission_setup_reserved_player_count;
						     ++reserved_index) {
							if (g_mission_setup_reserved_player_ids
								    [reserved_index] ==
							    player_id) {
								break;
							}
						}
						if (reserved_index ==
						    g_mission_setup_reserved_player_count) {
							g_mission_setup_reserved_player_ids
								[reserved_index] =
									player_id;
							++g_mission_setup_reserved_player_count;
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_FLIGHT_RESERVATION;
							*(int *)g_frontend_net_packet_scratch
								 .payload =
								player_id;
							/* playerId now holds
							 * the local player's
							 * id, sent as the
							 * player making the
							 * reservation; the
							 * dragged player's id
							 * is read back from the
							 * roster below. */
							player_id =
								net_get_local_player_id();
							*(int *)(g_frontend_net_packet_scratch
									 .payload +
								 sizeof(int)) =
								player_id;
							net_send_packet_and_flush(
								0,
								&g_frontend_net_packet_scratch,
								3 * sizeof(int));
							g_mission_setup_dragged_player_id =
								g_mp_roster[roster_index]
									.player_id;
							frontend_mouse_set_input_gate(
								INPUT_GATE);
							/* reserved_index walks
							 * this team's player
							 * slots here, not the
							 * reserved list. */
							for (reserved_index = 0;
							     reserved_index <
							     g_team_player_flight_group_count
								     [g_pilot_data
									      .team];
							     ++reserved_index) {
								assignment_index =
									g_pilot_data.team *
										PLAYER_SLOTS_PER_TEAM +
									reserved_index;
								if (g_mission_setup_player_assignments
										    .team_player_ids
											    [g_pilot_data
												     .team]
											    [reserved_index] ==
									    g_mission_setup_dragged_player_id &&
								    g_mission_setup_player_flight_group_indices
										    [assignment_index] !=
									    -1) {
									g_frontend_net_packet_scratch
										.packet_type =
										NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
									*(int *)g_frontend_net_packet_scratch
										 .payload =
										g_pilot_data
											.team;
									*(int *)(g_frontend_net_packet_scratch
											 .payload +
										 sizeof(int)) =
										reserved_index;
									*(int *)(g_frontend_net_packet_scratch
											 .payload +
										 2 * sizeof(int)) =
										-1;
									net_send_packet_and_flush(
										0,
										&g_frontend_net_packet_scratch,
										5 * sizeof(int));
								}
							}
						}
					}
				}
			}
			frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
			if (displayed_player_count >= MAX_UNASSIGNED_ROWS) {
				break;
			}
		}
	}

	{
		int displayed_flight_group_count = 0;
		frontend_cursor_get_pos(&cursor_x, &cursor_y);
		frontend_draw_rect_assign(&rect, COLUMN_DUTY, LIST_Y + y_offset,
					  229, LIST_Y + y_offset + 16);
		struct RECT player_rect;
		frontend_draw_rect_assign(&player_rect, COLUMN_ASSIGNED,
					  LIST_Y + y_offset, 332,
					  LIST_Y + y_offset + 16);
		for (int flight_group_index = 0;
		     flight_group_index <
		     (int)(int16_t)g_frontend_mission.flight_group_count;
		     ++flight_group_index) {
			struct xvt_flight_group *flight_group =
				&g_frontend_mission
					 .flight_groups[flight_group_index];
			if (flight_group->player_number == 0 ||
			    flight_group->team != g_pilot_data.team) {
				continue;
			}
			uint8_t iff_color_code;
			switch (flight_group->iff) {
			case 0:
				iff_color_code = 2;
				break;
			case 1:
			case 4:
				iff_color_code = 3;
				break;
			case 2:
				iff_color_code = 5;
				break;
			case 3:
				iff_color_code = 4;
				break;
			case 5:
				iff_color_code = 6;
				break;
			default:
				iff_color_code = 1;
				break;
			}
			sprintf(g_frontend_scratch_buffer, "%u. %c%s %s: %c",
				displayed_flight_group_count + 1,
				iff_color_code,
				frontend_string_get((
					frontend_string_id)(flight_group
								    ->craft_type +
							    609)),
				flight_group->name, 1);
			const char *designation =
				flight_group->orders[0].designation[0] != '\0'
					? flight_group->orders[0].designation
					: frontend_string_get(
						  FRONTSTR_626_GENERAL);
			strcat(g_frontend_scratch_buffer, designation);
			frontend_text_draw_aligned_in_rect(
				FONT_SIZE, g_frontend_scratch_buffer, &rect, 0,
				1, 0xFFFF);

			int assigned_team_player_index = -1;
			int roster_index;
			for (team_player_index = 0;
			     team_player_index <
			     g_team_player_flight_group_count[g_pilot_data
								      .team];
			     ++team_player_index) {
				int assignment_index =
					g_pilot_data.team *
						PLAYER_SLOTS_PER_TEAM +
					team_player_index;
				if (g_mission_setup_player_flight_group_indices
					    [assignment_index] !=
				    flight_group_index) {
					continue;
				}
				assigned_team_player_index = team_player_index;
				for (roster_index = 0;
				     roster_index < PLAYER_SLOTS_PER_TEAM;
				     ++roster_index) {
					if (g_mp_roster[roster_index]
						    .player_id ==
					    g_mission_setup_player_assignments
						    .team_player_ids
							    [g_pilot_data.team]
							    [team_player_index]) {
						break;
					}
				}
				if (roster_index == PLAYER_SLOTS_PER_TEAM) {
					continue;
				}

				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get((
						frontend_string_id)(g_mp_roster[roster_index]
									    .pilot_rating +
								    154)),
					1, g_mp_roster[roster_index].name);
				frontend_display_get_screen_clip_rect(
					&saved_clip_rect);
				frontend_display_set_screen_clip_rect640x480(
					&player_rect);
				if (net_get_local_player_id() ==
					    g_mp_roster[roster_index]
						    .player_id ||
				    g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					frontend_text_draw_aligned_in_rect(
						FONT_SIZE,
						g_frontend_scratch_buffer,
						&player_rect, 0, 1,
						g_pulse_color_ramp
							[((frame_counter % 24) &
							  ~1) >>
							 1]);
				} else {
					frontend_text_draw_aligned_in_rect(
						FONT_SIZE,
						g_frontend_scratch_buffer,
						&player_rect, 0, 1,
						g_color_yellow);
				}
				frontend_display_set_screen_clip_rect640x480(
					&saved_clip_rect);
			}

			if (g_team_player_flight_group_count[g_pilot_data
								     .team] >
			    1) {
				if (assigned_team_player_index == -1) {
					if (frontend_mouse_is_gate_owner(
						    INPUT_GATE) &&
					    (frontend_mouse_get_left_click_for(
						     INPUT_GATE) != 0 ||
					     frontend_mouse_get_right_click_for(
						     INPUT_GATE) != 0) &&
					    frontend_draw_point_in_rect(
						    &player_rect, cursor_x,
						    cursor_y)) {
						if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							int dragged_team_player_index;

							for (dragged_team_player_index =
								     0;
							     dragged_team_player_index <
							     g_team_player_flight_group_count
								     [g_pilot_data
									      .team];
							     ++dragged_team_player_index) {
								if (g_mission_setup_player_assignments
									    .team_player_ids
										    [g_pilot_data
											     .team]
										    [dragged_team_player_index] ==
								    g_mission_setup_dragged_player_id) {
									break;
								}
							}
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
							*(int *)g_frontend_net_packet_scratch
								 .payload =
								g_pilot_data
									.team;
							*(int *)(g_frontend_net_packet_scratch
									 .payload +
								 sizeof(int)) =
								dragged_team_player_index;
							*(int *)(g_frontend_net_packet_scratch
									 .payload +
								 2 * sizeof(int)) =
								flight_group_index;
							net_send_packet_and_flush(
								0,
								&g_frontend_net_packet_scratch,
								4 * sizeof(int));
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_RELEASE_FLIGHT_RESERVATION;
							*(int *)g_frontend_net_packet_scratch
								 .payload =
								g_mission_setup_dragged_player_id;
							net_send_packet_and_flush(
								0,
								&g_frontend_net_packet_scratch,
								2 * sizeof(int));
						} else {
							if (g_game_config
								    .sfx_datapad_enabled !=
							    0) {
								frontend_sound_play_ui_sound(
									"slotsound",
									1, 0,
									UI_SOUND_PRIORITY,
									UI_SOUND_VOLUME_SCALE *
										g_game_config
											.sfx_datapad_volume,
									UI_SOUND_PAN_CENTER);
							}
							int dragged_team_player_index;
							for (dragged_team_player_index =
								     0;
							     dragged_team_player_index <
							     g_team_player_flight_group_count
								     [g_pilot_data
									      .team];
							     ++dragged_team_player_index) {
								if (g_mission_setup_player_assignments
									    .team_player_ids
										    [g_pilot_data
											     .team]
										    [dragged_team_player_index] ==
								    g_mission_setup_dragged_player_id) {
									break;
								}
							}
							if (dragged_team_player_index <
							    g_team_player_flight_group_count
								    [g_pilot_data
									     .team]) {
								g_mission_setup_player_flight_group_indices
									[g_pilot_data.team *
										 PLAYER_SLOTS_PER_TEAM +
									 dragged_team_player_index] =
										flight_group_index;
							}
						}
						g_mission_setup_dragged_player_id =
							0;
						frontend_mouse_clear_input_gate();
					}
				} else if (!frontend_mouse_is_gate_owner(
						   INPUT_GATE)) {
					if ((is_team_captain ||
					     g_frontend_mission_session_mode ==
						     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
					    (frontend_mouse_get_left_down() !=
						     0 ||
					     frontend_mouse_get_right_down() !=
						     0) &&
					    frontend_draw_point_in_rect(
						    &player_rect, cursor_x,
						    cursor_y)) {
						int player_id =
							g_mission_setup_player_assignments
								.team_player_ids
									[g_pilot_data
										 .team]
									[assigned_team_player_index];
						if (g_frontend_mission_session_mode ==
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							g_mission_setup_dragged_player_id =
								player_id;
							frontend_mouse_set_input_gate(
								INPUT_GATE);
							g_mission_setup_player_flight_group_indices
								[g_pilot_data.team *
									 PLAYER_SLOTS_PER_TEAM +
								 assigned_team_player_index] =
									-1;
						} else {
							int reserved_index;
							for (reserved_index = 0;
							     reserved_index <
							     g_mission_setup_reserved_player_count;
							     ++reserved_index) {
								if (g_mission_setup_reserved_player_ids
									    [reserved_index] ==
								    player_id) {
									break;
								}
							}
							if (reserved_index ==
							    g_mission_setup_reserved_player_count) {
								g_mission_setup_reserved_player_ids
									[reserved_index] =
										player_id;
								++g_mission_setup_reserved_player_count;
								g_frontend_net_packet_scratch
									.packet_type =
									NET_PACKET_FLIGHT_RESERVATION;
								*(int *)g_frontend_net_packet_scratch
									 .payload =
									player_id;
								/* roster_index
								 * is reused
								 * here for the
								 * local
								 * player's id,
								 * sent as the
								 * player making
								 * the
								 * reservation. */
								roster_index =
									net_get_local_player_id();
								*(int *)(g_frontend_net_packet_scratch
										 .payload +
									 sizeof(int)) =
									roster_index;
								net_send_packet_and_flush(
									0,
									&g_frontend_net_packet_scratch,
									3 * sizeof(int));
								g_mission_setup_dragged_player_id =
									player_id;
								frontend_mouse_set_input_gate(
									INPUT_GATE);
								/* reserved_index
								 * walks this
								 * team's player
								 * slots here,
								 * not the
								 * reserved
								 * list. */
								for (reserved_index =
									     0;
								     reserved_index <
								     g_team_player_flight_group_count
									     [g_pilot_data
										      .team];
								     ++reserved_index) {
									int assignment_index =
										g_pilot_data.team *
											PLAYER_SLOTS_PER_TEAM +
										reserved_index;
									if (g_mission_setup_player_assignments
											    .team_player_ids
												    [g_pilot_data
													     .team]
												    [reserved_index] ==
										    g_mission_setup_dragged_player_id &&
									    g_mission_setup_player_flight_group_indices
											    [assignment_index] !=
										    -1) {
										g_frontend_net_packet_scratch
											.packet_type =
											NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
										*(int *)g_frontend_net_packet_scratch
											 .payload =
											g_pilot_data
												.team;
										*(int *)(g_frontend_net_packet_scratch
												 .payload +
											 sizeof(int)) =
											reserved_index;
										*(int *)(g_frontend_net_packet_scratch
												 .payload +
											 2 * sizeof(int)) =
											-1;
										net_send_packet_and_flush(
											0,
											&g_frontend_net_packet_scratch,
											5 * sizeof(int));
									}
								}
							}
						}
					}
				} else if ((frontend_mouse_get_left_click_for(
						    INPUT_GATE) != 0 ||
					    frontend_mouse_get_right_click_for(
						    INPUT_GATE) != 0) &&
					   frontend_draw_point_in_rect(
						   &player_rect, cursor_x,
						   cursor_y)) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
						*(int *)g_frontend_net_packet_scratch
							 .payload =
							g_pilot_data.team;
						*(int *)(g_frontend_net_packet_scratch
								 .payload +
							 sizeof(int)) =
							assigned_team_player_index;
						*(int *)(g_frontend_net_packet_scratch
								 .payload +
							 2 * sizeof(int)) = -1;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							5 * sizeof(int));
						int dragged_team_player_index;
						for (dragged_team_player_index =
							     0;
						     dragged_team_player_index <
						     g_team_player_flight_group_count
							     [g_pilot_data
								      .team];
						     ++dragged_team_player_index) {
							if (g_mission_setup_player_assignments
								    .team_player_ids
									    [g_pilot_data
										     .team]
									    [dragged_team_player_index] ==
							    g_mission_setup_dragged_player_id) {
								break;
							}
						}
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY;
						*(int *)g_frontend_net_packet_scratch
							 .payload =
							g_pilot_data.team;
						*(int *)(g_frontend_net_packet_scratch
								 .payload +
							 sizeof(int)) =
							dragged_team_player_index;
						*(int *)(g_frontend_net_packet_scratch
								 .payload +
							 2 * sizeof(int)) =
							flight_group_index;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							4 * sizeof(int));
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_RELEASE_FLIGHT_RESERVATION;
						*(int *)g_frontend_net_packet_scratch
							 .payload =
							g_mission_setup_dragged_player_id;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							2 * sizeof(int));
					} else {
						if (g_game_config
							    .sfx_datapad_enabled !=
						    0) {
							frontend_sound_play_ui_sound(
								"slotsound", 1,
								0,
								UI_SOUND_PRIORITY,
								UI_SOUND_VOLUME_SCALE *
									g_game_config
										.sfx_datapad_volume,
								UI_SOUND_PAN_CENTER);
						}
						int dragged_player_id =
							g_mission_setup_dragged_player_id;
						g_mission_setup_player_flight_group_indices
							[g_pilot_data.team *
								 PLAYER_SLOTS_PER_TEAM +
							 assigned_team_player_index] =
								-1;
						int dragged_team_player_index;
						for (dragged_team_player_index =
							     0;
						     dragged_team_player_index <
						     g_team_player_flight_group_count
							     [g_pilot_data
								      .team];
						     ++dragged_team_player_index) {
							if (g_mission_setup_player_assignments
								    .team_player_ids
									    [g_pilot_data
										     .team]
									    [dragged_team_player_index] ==
							    dragged_player_id) {
								break;
							}
						}
						if (dragged_team_player_index <
						    g_team_player_flight_group_count
							    [g_pilot_data
								     .team]) {
							g_mission_setup_player_flight_group_indices
								[g_pilot_data.team *
									 PLAYER_SLOTS_PER_TEAM +
								 dragged_team_player_index] =
									flight_group_index;
						}
					}
					g_mission_setup_dragged_player_id = 0;
					frontend_mouse_clear_input_gate();
				}
			}

			frontend_draw_rect_offset_xy(&player_rect, 0,
						     ROW_HEIGHT);
			frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
			++displayed_flight_group_count;
		}
	}
	return 1;
}

/* Takes out of the team and flight assignments every assigned_player_ids entry
 * that is not the playerId of a g_mp_roster entry (a 0 entry stays while some
 * roster entry is empty): the id is removed from each slot of the first
 * g_team_count teams that holds it, the later slots and their
 * g_mission_setup_player_flight_group_indices entries shifting down and slot 7
 * becoming 0 with flight group -1, and the entry is set to 0. Outside a solo
 * game it also calls net_count_ready_players and drops the result. */
// FUNCTION: XVT 0x4FA420
void mission_setup_prune_flight_assignments(void)
{
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		net_count_ready_players();
	}

	int active_player_index = 0;
	do {
		int *active_player_id_ptr =
			&g_mission_setup_player_assignments
				 .assigned_player_ids[active_player_index];
		unsigned int roster_index = 0;
		int *roster_player_id_ptr = &g_mp_roster[0].player_id;
		int removed_player_id = *active_player_id_ptr;
		do {
			if (*roster_player_id_ptr == removed_player_id) {
				break;
			}
			roster_player_id_ptr =
				(int *)((char *)roster_player_id_ptr +
					sizeof(struct mp_roster_entry));
			++roster_index;
		} while (roster_index < 8);

		if (roster_index == 8) {
			if (g_team_count > 0) {
				unsigned int team_byte_offset = 0;
				int teams_remaining = g_team_count;
				do {
					/* roster_index counts the player slots
					 * within a team here, alongside
					 * player_byte_offset, not roster
					 * entries. */
					roster_index = 0;
					int player_byte_offset =
						team_byte_offset;
					do {
						if (*(int *)((uint8_t *)g_mission_setup_player_assignments
								     .team_player_ids +
							     player_byte_offset) ==
						    removed_player_id) {
							if ((int)roster_index <
							    7) {
								int shift_byte_offset =
									player_byte_offset;
								int shift_remaining =
									7 -
									(int)roster_index;
								do {
									int shifted_player_id = *(
										int *)((uint8_t *)g_mission_setup_player_assignments
											       .team_player_ids +
										       shift_byte_offset +
										       sizeof(int));
									*(int *)((uint8_t *)g_mission_setup_player_assignments
											 .team_player_ids +
										 shift_byte_offset) =
										shifted_player_id;
									*(int *)((uint8_t *)
											 g_mission_setup_player_flight_group_indices +
										 shift_byte_offset) =
										*(int *)((uint8_t *)
												 g_mission_setup_player_flight_group_indices +
											 shift_byte_offset +
											 sizeof(int));
									shift_byte_offset +=
										sizeof(int);
									--shift_remaining;
								} while (
									shift_remaining !=
									0);
							}
							*(int *)((uint8_t *)g_mission_setup_player_assignments
									 .team_player_ids +
								 team_byte_offset +
								 7 * sizeof(int)) =
								0;
							*(int *)((uint8_t *)
									 g_mission_setup_player_flight_group_indices +
								 team_byte_offset +
								 7 * sizeof(int)) =
								-1;
						}
						player_byte_offset +=
							sizeof(int);
						++roster_index;
					} while ((int)roster_index < 8);
					team_byte_offset += 8 * sizeof(int);
					--teams_remaining;
				} while (teams_remaining != 0);
			}
			*active_player_id_ptr = 0;
		}
		++active_player_index;
	} while (active_player_index < 8);
}

/* Tells whether every player of the pilot's team has a flight group: returns 0
 * when one of the team's first g_team_player_flight_group_count slots holds a
 * player whose g_mission_setup_player_flight_group_indices entry is -1, else 1. It
 * runs the same test of the pilot's team once per team, g_team_count times, and
 * checks no other team. */
// FUNCTION: XVT 0x4FA500
int mission_setup_are_flight_assignments_complete(void)
{
	int team_index = 0;
	if (g_team_count > 0) {
		int player_count =
			g_team_player_flight_group_count[g_pilot_data.team];
		do {
			int player_index = 0;
			if (player_count > 0) {
				do {
					int assignment_index =
						8 * g_pilot_data.team +
						player_index;
					if (g_mission_setup_player_assignments.team_player_ids
							    [g_pilot_data.team]
							    [player_index] !=
						    0 &&
					    g_mission_setup_player_flight_group_indices
							    [assignment_index] ==
						    -1) {
						return 0;
					}
				} while (++player_index < player_count);
			}
		} while (++team_index < g_team_count);
	}
	return 1;
}

/* Gives each player of the pilot's team a random player flight group of the
 * team, when slot 0 holds a player. For each slot in turn, until one holds 0,
 * it draws rand() % 9 + 1 and steps that many times through the team's player
 * flight groups not given to an earlier slot, in mission order and cycling, and
 * gives the slot the one it stops on. Outside a solo game it then sends every
 * player a FLIGHT_ASSIGNMENTS packet with the team and its eight
 * g_mission_setup_player_flight_group_indices entries. Returns 1. Does not check
 * that the team has a free flight group for each player (the stepping then
 * never ends), or stop after slot 7 when all 8 slots hold players: it then goes
 * on into the next team's row. */
// FUNCTION: XVT 0x4FA570
int mission_setup_randomize_flight_assignments(void)
{
	enum { PLAYER_SLOTS_PER_TEAM = 8, FLIGHT_GROUP_SELECTION_COUNT = 9 };

	int assigned_players = 0;
	int team = g_pilot_data.team;
	if (g_mission_setup_player_assignments.team_player_ids[team][0] != 0) {
		do {
			int selection =
				rand() % FLIGHT_GROUP_SELECTION_COUNT + 1;
			g_mission_setup_player_flight_group_indices
				[team * PLAYER_SLOTS_PER_TEAM +
				 assigned_players] = -1;
			do {
				int flight_group_index = 0;
				if ((int16_t)g_frontend_mission
					    .flight_group_count > 0) {
					do {
						if (g_frontend_mission
								    .flight_groups
									    [flight_group_index]
								    .player_number !=
							    0 &&
						    g_frontend_mission
								    .flight_groups
									    [flight_group_index]
								    .team ==
							    team) {
							int prior_assignment =
								0;
							if (assigned_players >
							    0) {
								do {
									if (g_mission_setup_player_flight_group_indices
										    [team * PLAYER_SLOTS_PER_TEAM +
										     prior_assignment] ==
									    flight_group_index) {
										break;
									}
									++prior_assignment;
								} while (
									prior_assignment <
									assigned_players);
							}
							if (prior_assignment ==
								    assigned_players &&
							    --selection == 0) {
								g_mission_setup_player_flight_group_indices
									[team * PLAYER_SLOTS_PER_TEAM +
									 assigned_players] =
										flight_group_index;
								break;
							}
						}
						++flight_group_index;
					} while (
						flight_group_index <
						(int)(int16_t)g_frontend_mission
							.flight_group_count);
				}
			} while (g_mission_setup_player_flight_group_indices
					 [team * PLAYER_SLOTS_PER_TEAM +
					  assigned_players] == -1);
			++assigned_players;
		} while (g_mission_setup_player_assignments
				 .team_player_ids[team][assigned_players] != 0);
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		memcpy(g_frontend_net_packet_scratch.payload,
		       &g_pilot_data.team, sizeof(g_pilot_data.team));
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_FLIGHT_ASSIGNMENTS;
		memcpy(g_frontend_net_packet_scratch.payload +
			       sizeof(g_pilot_data.team),
		       &g_mission_setup_player_flight_group_indices
			       [g_pilot_data.team * PLAYER_SLOTS_PER_TEAM],
		       PLAYER_SLOTS_PER_TEAM *
			       sizeof(g_mission_setup_player_flight_group_indices
					      [0]));
		net_send_packet_and_flush(
			0, &g_frontend_net_packet_scratch,
			sizeof(g_frontend_net_packet_scratch.packet_type) +
				sizeof(g_pilot_data.team) +
				PLAYER_SLOTS_PER_TEAM *
					sizeof(g_mission_setup_player_flight_group_indices
						       [0]));
	}
	return 1;
}

/* Sets the pilot's team's eight g_mission_setup_player_flight_group_indices entries
 * to -1 and, outside a solo game, sends every player a CLEAR_FLIGHT_ASSIGNMENTS
 * packet with the team. Returns 1. */
// FUNCTION: XVT 0x4FA690
int mission_setup_clear_flight_assignments(void)
{
	int first_assignment = 8 * g_pilot_data.team;
	int assignment_index = 0;
	do {
		int current_assignment = first_assignment + assignment_index;

		++assignment_index;
		g_mission_setup_player_flight_group_indices
			[current_assignment] = -1;
	} while (assignment_index < 8);
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		memcpy(g_frontend_net_packet_scratch.payload,
		       &g_pilot_data.team, sizeof(g_pilot_data.team));
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS;
		net_send_packet_and_flush(
			0, &g_frontend_net_packet_scratch,
			sizeof(g_frontend_net_packet_scratch.packet_type) +
				sizeof(g_pilot_data.team));
	}
	return 1;
}

/* Gives each player of the pilot's team who has no flight group the first of
 * the team's player flight groups no slot holds, and sends every player a
 * FLIGHT_ASSIGNMENT packet (team, slot, flight group) for it; when every one is
 * taken the slot keeps the team's last player flight group and nothing is sent.
 * Only mission_setup_flight_assignment_update calls it, when the captain's
 * FLIGHT_ASSIGNMENTS_READY arrives. Returns 1. */
// FUNCTION: XVT 0x4FA6F0
int mission_setup_fill_flight_assignments(void)
{
	enum { PLAYER_SLOTS_PER_TEAM = 8, PACKET_VALUE_COUNT = 3 };

	for (int player_index = 0;
	     player_index < g_team_player_flight_group_count[g_pilot_data.team];
	     ++player_index) {
		int team_assignment_offset =
			PLAYER_SLOTS_PER_TEAM * g_pilot_data.team;

		if (g_mission_setup_player_flight_group_indices
				    [team_assignment_offset + player_index] ==
			    -1 &&
		    g_mission_setup_player_assignments
				    .team_player_ids[g_pilot_data.team]
						    [player_index] != 0) {
			for (int flight_group_index = 0;
			     flight_group_index <
			     (int)(int16_t)
				     g_frontend_mission.flight_group_count;
			     ++flight_group_index) {
				if (g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .player_number != 0 &&
				    g_frontend_mission
						    .flight_groups
							    [flight_group_index]
						    .team ==
					    g_pilot_data.team) {
					int prior_assignment;

					for (prior_assignment = 0;
					     prior_assignment <
					     g_team_player_flight_group_count
						     [g_pilot_data.team];
					     ++prior_assignment) {
						if (g_mission_setup_player_flight_group_indices
							    [team_assignment_offset +
							     prior_assignment] ==
						    flight_group_index) {
							break;
						}
					}
					g_mission_setup_player_flight_group_indices
						[team_assignment_offset +
						 player_index] =
							flight_group_index;
					if (prior_assignment ==
					    g_team_player_flight_group_count
						    [g_pilot_data.team]) {
						*(int *)&g_frontend_net_packet_scratch
							 .payload[0] =
							g_pilot_data.team;
						*(int *)&g_frontend_net_packet_scratch
							 .payload[4] =
							player_index;
						*(int *)&g_frontend_net_packet_scratch
							 .payload[8] =
							flight_group_index;
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_FLIGHT_ASSIGNMENT;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type) +
								PACKET_VALUE_COUNT *
									sizeof(int));
						break;
					}
				}
			}
		}
	}
	return 1;
}

/* Draws the duty roster under the briefing map on the flight assignment screen
 * when the pilot's team has more than 4 player flight groups and the map shows:
 * the team's player flight groups, numbered, four to a column, each with its
 * pilot's rating and name (pulsing for the local player and in a solo game,
 * else yellow) or the unassigned text. The team's captain, or a solo player,
 * also sees the team captain line. Returns 1. */
// FUNCTION: XVT 0x4FA810
int mission_setup_draw_assigned_players(int frame_counter)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 335, 443, 349);
	frontend_text_draw_centered(
		12, frontend_string_get(FRONTSTR_627_DUTY_ROSTER), &rect,
		0xFFFF);
	int is_team_captain = net_get_local_player_id() ==
			      g_mission_setup_player_assignments
				      .team_player_ids[g_pilot_data.team][0];
	if (is_team_captain || g_frontend_mission_session_mode ==
				       FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 88, 422, 443, 436);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_741_YOU_ARE_A_TEAM_CAPTAIN),
			&rect, g_color_green);
	}

	int displayed_flight_group_count = 0;
	frontend_draw_rect_assign(&rect, 88, 352, 264, 368);
	struct RECT player_text_rect;
	struct RECT previous_clip_rect;
	int assignment_slot;
	for (int flight_group_index = 0;
	     *(int16_t *)&g_frontend_mission.flight_group_count >
	     flight_group_index;
	     ++flight_group_index) {
		if (g_frontend_mission.flight_groups[flight_group_index]
				    .player_number != 0 &&
		    g_frontend_mission.flight_groups[flight_group_index].team ==
			    g_pilot_data.team) {
			for (assignment_slot = 0;
			     assignment_slot <
			     g_team_player_flight_group_count[g_pilot_data
								      .team];
			     ++assignment_slot) {
				if (g_mission_setup_player_flight_group_indices
					    [g_pilot_data.team * 8 +
					     assignment_slot] ==
				    flight_group_index) {
					for (int roster_index = 0;
					     roster_index < 8; ++roster_index) {
						if (g_mp_roster[roster_index]
							    .player_id ==
						    g_mission_setup_player_assignments
							    .team_player_ids
								    [g_pilot_data
									     .team]
								    [assignment_slot]) {
							sprintf(g_frontend_scratch_buffer,
								"%u.",
								displayed_flight_group_count +
									1);
							frontend_draw_rect_copy(
								&player_text_rect,
								&rect);
							player_text_rect.right =
								player_text_rect
									.left +
								15;
							frontend_text_draw_aligned_in_rect(
								12,
								g_frontend_scratch_buffer,
								&player_text_rect,
								0, 1, 0xFFFF);
							player_text_rect.left =
								rect.left + 15;
							player_text_rect.right =
								rect.right;
							sprintf(g_frontend_scratch_buffer,
								"%c%s %c%s", 6,
								frontend_string_get(
									FRONTSTR_154_DRONE +
									g_mp_roster[roster_index]
										.pilot_rating),
								1,
								g_mp_roster[roster_index]
									.name);
							frontend_display_get_screen_clip_rect(
								&previous_clip_rect);
							frontend_display_set_screen_clip_rect640x480(
								&rect);
							if (net_get_local_player_id() ==
								    g_mp_roster[roster_index]
									    .player_id ||
							    g_frontend_mission_session_mode ==
								    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								frontend_text_draw_aligned_in_rect(
									12,
									g_frontend_scratch_buffer,
									&player_text_rect,
									0, 1,
									g_pulse_color_ramp
										[((frame_counter %
										   24) &
										  ~1) >>
										 1]);
							} else {
								frontend_text_draw_aligned_in_rect(
									12,
									g_frontend_scratch_buffer,
									&player_text_rect,
									0, 1,
									g_color_yellow);
							}
							frontend_display_set_screen_clip_rect640x480(
								&previous_clip_rect);
							break;
						}
					}
					break;
				}
			}

			if (g_team_player_flight_group_count[g_pilot_data
								     .team] ==
			    assignment_slot) {
				sprintf(g_frontend_scratch_buffer, "%u. %s",
					displayed_flight_group_count + 1,
					frontend_string_get(
						FRONTSTR_801_UNASSIGNED));
				frontend_text_draw_aligned_in_rect(
					12, g_frontend_scratch_buffer, &rect, 0,
					1, 0xFFFF);
			}
			++displayed_flight_group_count;
			if (displayed_flight_group_count == 4) {
				frontend_draw_rect_assign(&rect, 267, 352, 443,
							  368);
			} else {
				frontend_draw_rect_offset_xy(&rect, 0, 17);
			}
		}
		if (g_team_player_flight_group_count[g_pilot_data.team] <=
		    displayed_flight_group_count) {
			break;
		}
	}
	return 1;
}

/* Draws the mission description on the flight assignment screen's expanded
 * layout: a heading for a tournament, battle, campaign or other mission, then
 * g_mission_text wrapped, with a scrollbar that sets g_frontend_first_visible_line
 * when the text's wrapped line count (from a draw starting at line 4096) plus
 * one is over 9. Returns 1. */
// FUNCTION: XVT 0x4FAB50
int mission_setup_draw_assignment_mission_description(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 90, 430, 107);
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_TOURNAMENTS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(
				FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_BATTLES) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_472_BATTLE_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}
	frontend_draw_rect_assign(&rect, 88, 108, 420, 252);
	int line_count = frontend_text_draw_wrapped(12, g_mission_text, &rect,
						    0xFFFF, 4, 4096) +
			 1;
	if (line_count > 9) {
		frontend_draw_rect_assign(&rect, 421, 108, 430, 252);
		g_frontend_first_visible_line = frontend_scrollbar_draw(
			&rect, g_frontend_first_visible_line, line_count, 0, 5,
			(unsigned int)g_color_navy, 9);
		frontend_draw_rect_assign(&rect, 88, 108, 420, 252);
	} else {
		frontend_draw_rect_assign(&rect, 88, 108, 430, 252);
	}
	frontend_text_draw_wrapped(12, g_mission_text, &rect, 0xFFFF, 4,
				   g_frontend_first_visible_line);
	return 1;
}

/* Exit callback of the battle choice screen: frees g_mission_list,
 * g_battle_mission_list (setting g_battle_mission_list_count to 0) and
 * g_mission_text, setting each to NULL, frees the "background" image, resets the
 * scrollable controls and clears the mouse input gate. Returns 0. */
// FUNCTION: XVT 0x4FBE10
int mission_setup_battle_choice_exit(void)
{
	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_battle_mission_list != NULL) {
		free(g_battle_mission_list);
		g_battle_mission_list = NULL;
		g_battle_mission_list_count = 0;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	front_image_free_resource_by_name("background");
	frontend_reset_scrollable_controls();
	frontend_mouse_clear_input_gate();
	return 0;
}

/* The battle choice screen, run once per frame: in a combat engagement sequence
 * with the players' choice setup, the team whose index is not the previous
 * mission's result, which the screen's text calls the losing team, picks the
 * next mission from the battle's list. On frame 0 it clears
 * g_battle_choice_timeout_handled, draws the screen's base, reloads the list and
 * selects the stored mission, starts the 120000 ms countdown in
 * g_battle_choice_remaining_ms, allocates and loads g_mission_text and builds the
 * choice list. Outside a solo game it handles one packet: a host cancel leaves
 * for the join screen; a lobby state prunes the flight assignments;
 * MISSION_CHOICE stores the chosen mission id as
 * battle_sequence_state.current_mission_id and in mission_ordinals at
 * current_mission_index, stores its list index in mission_list_indices, loads the
 * mission and moves to flight assignment; a return to setup goes back to
 * mission_setup_update; a BRIEFING_COUNTDOWN lowers g_battle_choice_remaining_ms to
 * its value; a pilot rating updates its sender's roster entry. It draws the
 * battle's name, the title, the selected mission, the description, the chat
 * panel, the pilot banner and, outside a solo game, the countdown: the ms
 * elapsed since the last frame come off g_battle_choice_remaining_ms and the host
 * sends every player a BRIEFING_COUNTDOWN whenever the whole seconds change;
 * once it is under 0 it stays at 0, the losing team's captain (slot 0) sends
 * the host SUBMIT_MISSION_CHOICE once, and the function returns 0 each frame
 * before the rest. Then the help text, the roster, and Previous (solo: ask,
 * then mission setup), Restart (host: ask, then RETURN_TO_SETUP to every
 * player) or Leave (client: ask, then the join screen), and Next: a solo player
 * moves to flight assignment, the losing team's captain sends the host
 * SUBMIT_MISSION_CHOICE. A solo player or that captain also gets the mission
 * list button, which pushes mission_setup_battle_choice_draw_list. Both
 * SUBMIT_MISSION_CHOICE sends give a size of one word, so the description id
 * written into the payload is not sent and the host relays a word that was not
 * received. Returns 1 when frontend_handle_common_screen_controls returns 1, else
 * 0. Does not check that current_mission_index is above 0 before reading the
 * previous result. */
// FUNCTION: XVT 0x4FBE90
int mission_setup_battle_choice_update(int frame_counter)
{
	enum {
		PLAYER_COUNT = 8,
		BATTLE_CHOICE_DURATION_MS = 120000,
		MILLISECONDS_PER_SECOND = 1000,
		BATTLE_CHOICE_DURATION_SECONDS =
			BATTLE_CHOICE_DURATION_MS / MILLISECONDS_PER_SECOND,
		BRIEFING_TEXT_CAPACITY = 4096,
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
		BASIC_PACKET_SIZE = sizeof(int),
		TIMER_PACKET_SIZE = 2 * sizeof(int),
	};

	if (frame_counter == 0) {
		g_battle_choice_timeout_handled = 0;
		frontend_cursor_set_pos(37, 445);
		g_frontend_first_visible_line = 0;
		front_image_register_resource_default("frontres\\soloteam.bmp",
						      "background");
		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			for (g_selected_mission_list_index = 0;
			     (unsigned int)g_selected_mission_list_index <
			     g_mission_count;
			     ++g_selected_mission_list_index) {
				if (g_mission_list
					    [g_selected_mission_list_index]
						    .mission_idx ==
				    g_pilot_data.mission_description_ids
					    [g_pilot_data
						     .mission_directory_id]) {
					break;
				}
			}
		}
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		if (g_host_cd_available != 0) {
			front_image_draw_sprite("allactive", 0, 0);
		} else {
			front_image_draw_sprite("clientactive", 0, 0);
		}
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			front_image_draw_sprite_translucent("chatbox", 0, 0);
		}
		front_image_draw_sprite_translucent("gameoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
		g_battle_choice_remaining_ms = BATTLE_CHOICE_DURATION_MS;
		g_battle_choice_clock_ms = GetTickCount();
		g_battle_choice_last_sent_second =
			BATTLE_CHOICE_DURATION_SECONDS;
		g_battle_choice_previous_clock_ms = g_battle_choice_clock_ms;
		g_mission_text = malloc(BRIEFING_TEXT_CAPACITY);
		mission_setup_load_mission_desc_text(g_mission_text);
		mission_setup_battle_choice_build_list();
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		int packet_type = frontend_net_process_network_packets();
		if (packet_type == NET_PACKET_HOST_CANCELLED) {
			net_shutdown_direct_play_session();
			if (net_is_host() == 0) {
				frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					frontend_string_get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					frontend_string_get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return xvt_dialog_continue_with(
					xvt_mission_dialogs_resume,
					XVT_MISSION_ASSIGNMENT_CANCELLED);
#endif
			}
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
		} else if (packet_type == NET_PACKET_STATE) {
			mission_setup_prune_flight_assignments();
		} else if (packet_type == NET_PACKET_MISSION_CHOICE) {
			g_pilot_data.battle_sequence_state.current_mission_id =
				g_frontend_net_packet_arg0;
			g_pilot_data.battle_sequence_state.mission_ordinals
				[g_pilot_data.battle_sequence_state
					 .current_mission_index] =
				g_frontend_net_packet_arg0;
			if (g_mission_list != NULL) {
				for (g_selected_mission_list_index = 0;
				     (unsigned int)
					     g_selected_mission_list_index <
				     g_mission_count;
				     ++g_selected_mission_list_index) {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    g_frontend_net_packet_arg0) {
						g_pilot_data
							.battle_sequence_state
							.mission_list_indices
								[g_pilot_data
									 .battle_sequence_state
									 .current_mission_index] =
							g_selected_mission_list_index;
						break;
					}
				}
			}
			frontend_mission_load_current();
			frontend_screen_set_callbacks(
				mission_setup_flight_assignment_update,
				mission_setup_free_screen_resources);
			return 0;
		} else if (packet_type == NET_PACKET_RETURN_TO_SETUP) {
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				mission_setup_update,
				(frontend_screen_exit_fn)mission_setup_exit);
			return 0;
		} else if (packet_type == NET_PACKET_BRIEFING_COUNTDOWN) {
			if (g_frontend_net_packet_arg0 <
			    g_battle_choice_remaining_ms) {
				g_battle_choice_remaining_ms =
					g_frontend_net_packet_arg0;
			}
		} else if (packet_type == NET_PACKET_PILOT_RATING) {
			for (int roster_index = 0; roster_index < PLAYER_COUNT;
			     ++roster_index) {
				if (g_mp_roster[roster_index].player_id ==
				    g_frontend_net_packet_sender_player_id) {
					g_mp_roster[roster_index].pilot_rating =
						g_frontend_net_packet_arg0;
					break;
				}
			}
		}
	}

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
	struct RECT saved_clip_rect;
	frontend_display_get_screen_clip_rect(&saved_clip_rect);
	frontend_display_set_screen_clip_rect640x480(&rect);
	frontend_text_draw_centered(12, g_mission_sequence_description, &rect,
				    0xFFFF);
	frontend_display_set_screen_clip_rect640x480(&saved_clip_rect);
	frontend_draw_rect_assign(&rect, 84, 90, 434, 108);
	frontend_text_draw_centered(
		15,
		frontend_string_get(FRONTSTR_812_SELECT_NEXT_BATTLE_MISSION),
		&rect, 0xFFFF);
	if (net_is_host() != 0 ||
	    g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 84, 112, 416, 130);
	} else {
		frontend_draw_rect_assign(&rect, 84, 112, 434, 130);
	}
	if ((unsigned int)g_selected_mission_list_index < g_mission_count) {
		frontend_draw_rect_inset_xy(&rect, 4, 0);
		frontend_text_draw_aligned_in_rect(
			15,
			g_mission_list[g_selected_mission_list_index]
				.description,
			&rect, 0, 1, g_color_yellow);
	}
	mission_setup_battle_choice_draw_description();
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_net_update_and_draw_chat_panel(frame_counter);
	}

	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
		int animation_frame;
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if (g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_MELEES &&
			    g_pilot_data.mission_directory_id !=
				    MISSION_DIRECTORY_TOURNAMENTS) {
				if (g_pilot_data.current_faction_id == 0) {
					sprintf(g_frontend_scratch_buffer,
						"rebtiny%d",
						(frame_counter %
						 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1);
					front_image_draw_sprite(
						g_frontend_scratch_buffer, 204,
						453);
				} else {
					sprintf(g_frontend_scratch_buffer,
						"imptiny%d",
						(frame_counter %
						 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
							1);
					front_image_draw_sprite(
						g_frontend_scratch_buffer, 204,
						453);
				}
			} else {
				animation_frame =
					(frame_counter %
					 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
					1;
				sprintf(g_frontend_scratch_buffer, "rebtiny%d",
					animation_frame);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 204, 453);
				sprintf(g_frontend_scratch_buffer, "imptiny%d",
					animation_frame);
			}
		} else {
			animation_frame =
				(frame_counter %
				 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
				1;
			sprintf(g_frontend_scratch_buffer, "rebtiny%d",
				animation_frame);
			front_image_draw_sprite(g_frontend_scratch_buffer, 204,
						453);
			sprintf(g_frontend_scratch_buffer, "imptiny%d",
				animation_frame);
		}
		front_image_draw_sprite(g_frontend_scratch_buffer, 420, 453);
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
		frontend_format_seconds_to_clock_string(
			g_battle_choice_remaining_ms / MILLISECONDS_PER_SECOND);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_battle_choice_clock_ms = GetTickCount();
			g_battle_choice_remaining_ms +=
				g_battle_choice_previous_clock_ms -
				g_battle_choice_clock_ms;
			if (net_is_host() != 0) {
				int remaining_seconds =
					g_battle_choice_remaining_ms /
					MILLISECONDS_PER_SECOND;
				if (g_battle_choice_last_sent_second !=
				    remaining_seconds) {
					g_battle_choice_last_sent_second =
						remaining_seconds;
					*(int *)g_frontend_net_packet_scratch
						 .payload =
						g_battle_choice_remaining_ms;
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_BRIEFING_COUNTDOWN;
					net_send_packet_and_flush(
						0,
						&g_frontend_net_packet_scratch,
						TIMER_PACKET_SIZE);
				}
			}
			if (g_battle_choice_remaining_ms < 0) {
				g_battle_choice_remaining_ms = 0;
				if (g_battle_choice_timeout_handled == 0) {
					g_battle_choice_timeout_handled = 1;
					if (g_pilot_data.battle_sequence_state.mission_results
							    [g_pilot_data
								     .battle_sequence_state
								     .current_mission_index -
							     1] !=
						    (battle_mission_result)
							    g_pilot_data.team &&
					    net_get_local_player_id() ==
						    g_mission_setup_player_assignments
							    .team_player_ids
								    [g_pilot_data
									     .team]
								    [0]) {
						*(int *)g_frontend_net_packet_scratch
							 .payload =
							g_pilot_data.mission_description_ids
								[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS];
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_SUBMIT_MISSION_CHOICE;
						net_send_packet_and_flush(
							net_get_host_player_id(),
							&g_frontend_net_packet_scratch,
							BASIC_PACKET_SIZE);
					}
				}
				return 0;
			}
			g_battle_choice_previous_clock_ms =
				g_battle_choice_clock_ms;
		}
	}

	frontend_draw_rect_assign(&rect, 84, 400, 434, 414);
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_813_YOU_LOST_THE_LAST_BATTLE_MISSION),
			&rect, g_color_green);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_814_SELECT_THE_NEXT_BATTLE_MISSION),
			&rect, g_color_green);
	} else if (g_pilot_data.battle_sequence_state
			   .mission_results[g_pilot_data.battle_sequence_state
						    .current_mission_index -
					    1] ==
		   (battle_mission_result)g_pilot_data.team) {
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_815_YOUR_TEAM_WON_THE_LAST_BATTLE_MISSION),
			&rect, g_color_red);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_816_THE_LOSING_TEAM_WILL_SELECT_THE_NEXT_BATTLE_MISSION),
			&rect, g_color_red);
	} else if (net_get_local_player_id() ==
		   g_mission_setup_player_assignments
			   .team_player_ids[g_pilot_data.team][0]) {
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_817_YOUR_TEAM_LOST_THE_LAST_BATTLE_MISSION),
			&rect, g_color_green);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_814_SELECT_THE_NEXT_BATTLE_MISSION),
			&rect, g_color_green);
	} else {
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_817_YOUR_TEAM_LOST_THE_LAST_BATTLE_MISSION),
			&rect, g_color_red);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_818_YOUR_TEAM_CAPTAIN_WILL_CHOOSE_THE_NEXT_BATTLE_MISSION),
			&rect, g_color_red);
	}
	mission_setup_battle_choice_draw_roster(frame_counter);

	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_569_PREVIOUS));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
				frontend_string_get(
					FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
				frontend_string_get(
					FRONTSTR_683_TERMINATE_THIS_BATTLE),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_SOLO_BACK_TO_SETUP);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
			    frontend_string_get(
				    FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
			    frontend_string_get(
				    FRONTSTR_683_TERMINATE_THIS_BATTLE),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				mission_setup_update,
				(frontend_screen_exit_fn)mission_setup_exit);
		}
#endif

	} else if (net_is_host() != 0) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_668_RESTART));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
				frontend_string_get(
					FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
				frontend_string_get(
					FRONTSTR_754_TO_SELECT_MISSION),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_HOST_RESTART);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
			    frontend_string_get(
				    FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
			    frontend_string_get(FRONTSTR_754_TO_SELECT_MISSION),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_RETURN_TO_SETUP;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				BASIC_PACKET_SIZE);
		}
#endif

	} else {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_204_LEAVE));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				frontend_string_get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				frontend_string_get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    frontend_string_get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    frontend_string_get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_LEFT;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch,
				BASIC_PACKET_SIZE);
			net_shutdown_direct_play_session();
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
		}
#endif
	}

	frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_212_NEXT));
		if (frontend_button_handle_sprite_button(
			    &rect, "nextup", "nextdown",
			    frontend_string_get(FRONTSTR_475_GO_TO_BRIEFING),
			    12, 0, 7, "flysound") != 0) {
			frontend_screen_set_callbacks(
				mission_setup_flight_assignment_update,
				mission_setup_free_screen_resources);
			frontend_button_disable_overlay_text();
			return 0;
		}
	} else if (g_pilot_data.battle_sequence_state.mission_results
				   [g_pilot_data.battle_sequence_state
					    .current_mission_index -
				    1] !=
			   (battle_mission_result)g_pilot_data.team &&
		   net_get_local_player_id() ==
			   g_mission_setup_player_assignments
				   .team_player_ids[g_pilot_data.team][0]) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_212_NEXT));
		if (frontend_button_handle_sprite_button(
			    &rect, "nextup", "nextdown",
			    frontend_string_get(FRONTSTR_475_GO_TO_BRIEFING),
			    12, 0, 7, "flysound") != 0) {
			*(int *)g_frontend_net_packet_scratch.payload =
				g_pilot_data.mission_description_ids
					[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS];
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_SUBMIT_MISSION_CHOICE;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch,
				BASIC_PACKET_SIZE);
		}
	}

	int can_choose_mission = 0;
	frontend_button_disable_overlay_text();
	if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
	    (g_pilot_data.battle_sequence_state
			     .mission_results[g_pilot_data.battle_sequence_state
						      .current_mission_index -
					      1] !=
		     (battle_mission_result)g_pilot_data.team &&
	     net_get_local_player_id() ==
		     g_mission_setup_player_assignments
			     .team_player_ids[g_pilot_data.team][0])) {
		can_choose_mission = 1;
	}
	if (can_choose_mission == 1) {
		frontend_draw_rect_assign(&rect, 417, 112, 434, 130);
		if (frontend_button_handle_sprite_button(
			    &rect, "dropbtnup", "dropbtndown",
			    frontend_string_get(FRONTSTR_684_MISSION_LIST), 15,
			    0xFFFF, 9, "buttonsound") != 0) {
			frontend_draw_rect_assign(&rect, 0, 0, 639, 479);
			frontend_screen_queue_push(
				mission_setup_battle_choice_draw_list, &rect);
		}
	}
	return frontend_handle_common_screen_controls(1) == 1;
}

/* Draws the description box of the battle choice screen: a heading for a
 * tournament, battle, campaign or other mission, then g_mission_text wrapped,
 * with a scrollbar that sets g_frontend_first_visible_line when the text's wrapped
 * line count (from a draw starting at line 4096) plus one is over 9. Returns
 * 1. */
// FUNCTION: XVT 0x4FCBF0
int mission_setup_battle_choice_draw_description(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 138, 430, 153);
	if (g_pilot_data.mission_directory_id ==
	    MISSION_DIRECTORY_TOURNAMENTS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(
				FRONTSTR_471_TOURNAMENT_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_BATTLES) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_472_BATTLE_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
		   MISSION_DIRECTORY_CAMPAIGNS) {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_777_CAMPAIGN_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			15,
			frontend_string_get(FRONTSTR_192_MISSION_DESCRIPTION),
			&rect, 0, 1, 0xFFFF);
	}
	frontend_draw_rect_assign(&rect, 88, 157, 420, 301);
	int line_count = frontend_text_draw_wrapped(12, g_mission_text, &rect,
						    0xFFFF, 4, 4096) +
			 1;
	if (line_count > 9) {
		frontend_draw_rect_assign(&rect, 421, 157, 430, 301);
		g_frontend_first_visible_line = frontend_scrollbar_draw(
			&rect, g_frontend_first_visible_line, line_count, 0, 5,
			(unsigned int)g_color_navy, 9);
		frontend_draw_rect_assign(&rect, 88, 157, 420, 301);
	} else {
		frontend_draw_rect_assign(&rect, 88, 157, 430, 301);
	}
	frontend_text_draw_wrapped(12, g_mission_text, &rect, 0xFFFF, 4,
				   g_frontend_first_visible_line);
	return 1;
}

/* Draws the Players in Game panel of the battle choice screen: each g_mp_roster
 * entry with a nonzero playerId, four to a column, as its rating and name, or
 * the join in progress text when it has neither; the local player's entry
 * pulses, as does every entry in a solo game, and the others are yellow.
 * Returns 1. */
// FUNCTION: XVT 0x4FCD70
int mission_setup_battle_choice_draw_roster(int frame_counter)
{
	int mouse_x;
	int mouse_y;

	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 88, 311, 430, 326);
	frontend_text_draw_aligned_in_rect(
		15, frontend_string_get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0,
		1, 0xFFFF);
	int displayed_count = 0;
	frontend_draw_rect_assign(&rect, 88, 331, 258, 345);
	struct RECT previous_clip_rect;
	for (int roster_index = 0; roster_index < 8; ++roster_index) {
		if (g_mp_roster[roster_index].player_id != 0) {
			frontend_display_get_screen_clip_rect(
				&previous_clip_rect);
			frontend_display_set_screen_clip_rect640x480(&rect);
			if (g_mp_roster[roster_index].pilot_rating == 0 &&
			    g_mp_roster[roster_index].name[0] == '\0') {
				sprintf(g_frontend_scratch_buffer, "%s",
					frontend_string_get(
						FRONTSTR_720_JOIN_IN_PROGRESS));
			} else {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get(
						FRONTSTR_154_DRONE +
						g_mp_roster[roster_index]
							.pilot_rating),
					1, g_mp_roster[roster_index].name);
			}
			if (net_get_local_player_id() ==
				    g_mp_roster[roster_index].player_id ||
			    g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				frontend_text_draw_aligned_in_rect(
					12, g_frontend_scratch_buffer, &rect, 0,
					1,
					g_pulse_color_ramp
						[((frame_counter % 24) & ~1) >>
						 1]);
			} else {
				frontend_text_draw_aligned_in_rect(
					12, g_frontend_scratch_buffer, &rect, 0,
					1, g_color_yellow);
			}
			++displayed_count;
			if (displayed_count == 4) {
				frontend_draw_rect_assign(&rect, 260, 331, 430,
							  345);
			} else {
				frontend_draw_rect_offset_xy(&rect, 0, 15);
			}
			frontend_display_set_screen_clip_rect640x480(
				&previous_clip_rect);
		}
	}

	return 1;
}

/* Builds g_battle_mission_list, the battle's missions the choosing team may pick,
 * freeing the old one first. It loads the battle list for a moment to find the
 * selected battle's sequence file and copy the battle's description into
 * g_mission_sequence_description, then puts g_mission_list and g_mission_count
 * back. The file's first line gives the count, stored in
 * g_battle_mission_list_count; each line after it names a mission, matched without
 * regard to case against g_mission_list's file names. A match is copied in with
 * its section name cleared, and marked unavailable when its line's index is
 * among the mission_ordinals before current_mission_index. Returns 1, or 0 when
 * the file does not open or is empty, or ends early, the count then cut to the
 * lines read. Does not check the allocation, and leaves an entry unset when its
 * file name is not in the list. */
// FUNCTION: XVT 0x4FCF30
int mission_setup_battle_choice_build_list(void)
{
	enum {
		BATTLE_DESCRIPTOR_PATH_CAPACITY = 128,
		BATTLE_DESCRIPTOR_LINE_CAPACITY = 255
	};

	if (g_battle_mission_list != NULL) {
		free(g_battle_mission_list);
		g_battle_mission_list = NULL;
	}
	struct mission_list_entry *saved_mission_list = g_mission_list;
	unsigned int saved_mission_count = g_mission_count;
	g_mission_list = NULL;
	mission_setup_load_mission_list(MISSION_DIRECTORY_BATTLES);
	unsigned int selected_mission_index = 0;
	while (selected_mission_index < g_mission_count &&
	       g_mission_list[selected_mission_index].mission_idx !=
		       g_pilot_data.mission_description_ids
			       [MISSION_DIRECTORY_BATTLES]) {
		++selected_mission_index;
	}
	char battle_descriptor_path[BATTLE_DESCRIPTOR_PATH_CAPACITY];
	sprintf(battle_descriptor_path, "%s\\%s",
		g_mission_directory_names[MISSION_DIRECTORY_BATTLES],
		g_mission_list[selected_mission_index].file_name);
	strcpy(g_mission_sequence_description,
	       g_mission_list[selected_mission_index].description);
	free(g_mission_list);
	g_mission_list = saved_mission_list;
	g_mission_count = saved_mission_count;

	xvt_file *stream = file_open(battle_descriptor_path, "r");
	if (stream == NULL) {
		return 0;
	}
	if (FILE_GETS(g_frontend_scratch_buffer,
		      BATTLE_DESCRIPTOR_LINE_CAPACITY, stream) == NULL) {
		file_close(stream);
		return 0;
	}

	int parsed_mission_count = atoi(g_frontend_scratch_buffer);
	g_battle_mission_list_count = parsed_mission_count;
	g_battle_mission_list = (struct mission_list_entry *)malloc(
		sizeof(*g_battle_mission_list) * parsed_mission_count);
	int battle_mission_index = 0;
	int previous_mission_index;
	int mission_was_used;
	while (battle_mission_index < g_battle_mission_list_count) {
		if (FILE_GETS(g_frontend_scratch_buffer,
			      BATTLE_DESCRIPTOR_LINE_CAPACITY,
			      stream) == NULL) {
			g_battle_mission_list_count = battle_mission_index;
			file_close(stream);
			return 0;
		}
		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}

		unsigned int source_mission_index = 0;
		while (source_mission_index < g_mission_count) {
#ifdef XVT_MODERN
			if (strcasecmp(g_frontend_scratch_buffer,
				       g_mission_list[source_mission_index]
					       .file_name) == 0)
#else
			if (_strcmpi(g_frontend_scratch_buffer,
				     g_mission_list[source_mission_index]
					     .file_name) == 0)
#endif
			{
				mission_was_used = 0;
				for (previous_mission_index =
					     (int)g_pilot_data
						     .battle_sequence_state
						     .current_mission_index -
					     1;
				     previous_mission_index >= 0;
				     --previous_mission_index) {
					if (g_pilot_data.battle_sequence_state
						    .mission_ordinals
							    [previous_mission_index] ==
					    battle_mission_index) {
						mission_was_used = 1;
						break;
					}
				}
				memcpy(&g_battle_mission_list
					       [battle_mission_index],
				       &g_mission_list[source_mission_index],
				       sizeof(g_battle_mission_list
						      [battle_mission_index]));
				memset(g_battle_mission_list
					       [battle_mission_index]
						       .section_name,
				       0,
				       sizeof(g_battle_mission_list
						      [battle_mission_index]
							      .section_name));
				if (mission_was_used != 0) {
					g_battle_mission_list
						[battle_mission_index]
							.is_unavailable = 1;
				}
				break;
			}
			++source_mission_index;
		}
		++battle_mission_index;
	}
	return 1;
}

/* The battle choice screen's drop-down list of the battle's missions, a pushed
 * screen run each frame. On frame 0 it counts the rows in
 * g_battle_choice_row_count, one per available entry of g_battle_mission_list and
 * one per change of section among them, and sets g_battle_choice_scroll_offset to
 * the index of the entry matching the selected mission, or to 0 when the count
 * is under 20. Outside a solo game a lobby state from the host loads the host's
 * mission. It draws up to 19 rows, skipping unavailable entries, with a
 * scrollbar when there are more, then each entry's award beside it, chosen as
 * mission_setup_draw_mission_list chooses it. A click on a row takes that entry as
 * the selected mission, unless outside a solo game its file name starts with
 * "1": it stores the entry's ordinal and list index at battle_sequence_state's
 * current_mission_index (the ordinal being the entry's index in
 * g_battle_mission_list in a solo game and its mission id outside one), loads the
 * mission and its text, and outside a solo game sends the lobby state. A click
 * on a row, or outside the list, closes the list. Each row's text and color
 * come from g_mission_list at the entry's index, not from the entry. Returns 0.
 * When g_battle_mission_list is NULL it pops the screen and goes on drawing. */
// FUNCTION: XVT 0x4FD1F0
int mission_setup_battle_choice_draw_list(int frame_counter)
{
	enum {
		VISIBLE_ROW_COUNT = 19,
		ROW_HEIGHT = 15,
		LIST_LEFT = 84,
		LIST_TOP = 131,
		LIST_RIGHT = 434,
		LIST_SCROLLBAR_RIGHT = 423,
		LIST_BOTTOM_PADDING = 139,
		MISSION_TEXT_INDENT = 37,
		TEXT_FONT_SIZE = 12,
		SCROLLBAR_CONTROL_ID = 8,
		SINGLE_PLAYER_FILE_PREFIX = '1',
		SOUND_VOLUME_SCALE = 12,
		SOUND_CENTER_PAN = 63,
		AWARD_TOOLTIP_MEDAL_BASE = FRONTSTR_381_BATTLE_MEDALLION,
		AWARD_TOOLTIP_CITATION_BASE = FRONTSTR_659_QUICK_START,
	};

	if (g_battle_mission_list == NULL) {
		keyboard_flush_char_buffer();
		frontend_screen_pop_state();
	}

	int battle_mission_index;
	char last_section_name[sizeof(g_battle_mission_list[0].section_name)];
	if (frame_counter == 0) {
		g_battle_choice_row_count = g_battle_mission_list_count;
		memset(last_section_name, 0, sizeof(last_section_name));
		for (battle_mission_index = 0;
		     (unsigned int)g_battle_mission_list_count >
		     (unsigned int)battle_mission_index;
		     ++battle_mission_index) {
			if (g_pilot_data.mission_description_ids
				    [g_pilot_data.mission_directory_id] ==
			    g_battle_mission_list[battle_mission_index]
				    .mission_idx) {
				g_battle_choice_scroll_offset =
					battle_mission_index;
			}
			if (g_battle_mission_list[battle_mission_index]
				    .is_unavailable != 0) {
				--g_battle_choice_row_count;
			} else if (strcmp(g_battle_mission_list
						  [battle_mission_index]
							  .section_name,
					  last_section_name) != 0) {
				++g_battle_choice_row_count;
				strcpy(last_section_name,
				       g_battle_mission_list
					       [battle_mission_index]
						       .section_name);
			}
		}
		if (g_battle_choice_row_count < VISIBLE_ROW_COUNT + 1) {
			g_battle_choice_scroll_offset = 0;
		}
	}

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    frontend_net_process_network_packets() == NET_PACKET_STATE) {
		g_pilot_data.mission_directory_id =
			g_frontend_net_received_mission_directory_id;
		g_pilot_data.mission_description_ids
			[g_frontend_net_received_mission_directory_id] =
			g_frontend_net_received_mission_description_id;
		frontend_mission_load_current();
		mission_setup_load_mission_desc_text(g_mission_text);
		mission_setup_update_team_counts();
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			while (g_mission_count >
				       (unsigned int)
					       g_selected_mission_list_index &&
			       g_mission_list[g_selected_mission_list_index]
					       .mission_idx !=
				       g_pilot_data.mission_description_ids
					       [g_pilot_data
							.mission_directory_id]) {
				++g_selected_mission_list_index;
			}
		}
		g_frontend_first_visible_line = 0;
	}

	memset(last_section_name, 0, sizeof(last_section_name));
	struct RECT rect;
	if (g_battle_choice_row_count > VISIBLE_ROW_COUNT) {
		frontend_draw_rect_assign(&rect, 424, LIST_TOP, LIST_RIGHT,
					  424);
		g_battle_choice_scroll_offset = frontend_scrollbar_draw(
			&rect, g_battle_choice_scroll_offset,
			g_battle_choice_row_count, 0, 5,
			(unsigned int)g_color_navy, SCROLLBAR_CONTROL_ID);
	}

	int cursor_x;
	int cursor_y;
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	int visible_row_count;
	if (g_battle_choice_row_count > VISIBLE_ROW_COUNT) {
		visible_row_count = VISIBLE_ROW_COUNT;
	} else {
		visible_row_count = g_battle_choice_row_count;
	}
	frontend_draw_rect_assign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				  ROW_HEIGHT * visible_row_count +
					  LIST_BOTTOM_PADDING);
	if (!frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		keyboard_flush_char_buffer();
		frontend_screen_pop_state();
		frontend_unregister_scrollable_control(SCROLLBAR_CONTROL_ID);
	}

	if (g_battle_choice_row_count > VISIBLE_ROW_COUNT) {
		frontend_draw_rect_assign(
			&rect, LIST_LEFT, LIST_TOP, LIST_SCROLLBAR_RIGHT,
			ROW_HEIGHT * visible_row_count + LIST_BOTTOM_PADDING);
	} else {
		frontend_draw_rect_assign(
			&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
			ROW_HEIGHT * visible_row_count + LIST_BOTTOM_PADDING);
	}
	battle_mission_index = 0;
	frontend_draw_rect(&rect, 0, 0, frontend_display_pack_rgb(0, 0, 64), 1);
	frontend_draw_rect(&rect, 0, 0, 0xFFFF, 0);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	frontend_draw_rect(&rect, 0, 0, 0, 0);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	rect.bottom = rect.top + 14;
	rect.left += MISSION_TEXT_INDENT;

	int display_row = 0;
	for (; (unsigned int)g_battle_mission_list_count >
	       (unsigned int)battle_mission_index;
	     ++battle_mission_index) {
		if (g_battle_mission_list[battle_mission_index]
			    .is_unavailable != 0) {
			continue;
		}
		if (strcmp(g_battle_mission_list[battle_mission_index]
				   .section_name,
			   last_section_name) != 0) {
			if (display_row >= g_battle_choice_scroll_offset &&
			    display_row - g_battle_choice_scroll_offset <
				    VISIBLE_ROW_COUNT) {
				rect.left -= MISSION_TEXT_INDENT;
				frontend_text_draw_aligned_in_rect(
					TEXT_FONT_SIZE,
					g_battle_mission_list
						[battle_mission_index]
							.section_name,
					&rect, 0, 1, g_color_red);
				rect.left += MISSION_TEXT_INDENT;
				frontend_draw_rect_offset_xy(&rect, 0,
							     ROW_HEIGHT);
			}
			strcpy(last_section_name,
			       g_battle_mission_list[battle_mission_index]
				       .section_name);
			++display_row;
		}
		if (display_row - g_battle_choice_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (display_row >= g_battle_choice_scroll_offset &&
		    display_row - g_battle_choice_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y)) {
				frontend_draw_rect(&rect, 0, 0, g_color_green,
						   0);
				if (frontend_mouse_get_left_click() != 0 ||
				    frontend_mouse_get_right_click() != 0) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"jewelsound", 1, 0, 255,
							SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							SOUND_CENTER_PAN);
					}
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (g_battle_mission_list
							    [battle_mission_index]
								    .file_name
									    [0] !=
						    SINGLE_PLAYER_FILE_PREFIX) {
							g_pilot_data.mission_description_ids
								[g_pilot_data
									 .mission_directory_id] =
								g_battle_mission_list
									[battle_mission_index]
										.mission_idx;
							for (g_selected_mission_list_index =
								     0;
							     g_mission_count >
							     (unsigned int)
								     g_selected_mission_list_index;
							     ++g_selected_mission_list_index) {
								if (g_mission_list[g_selected_mission_list_index]
									    .mission_idx ==
								    g_pilot_data.mission_description_ids
									    [g_pilot_data
										     .mission_directory_id]) {
									g_pilot_data
										.battle_sequence_state
										.mission_ordinals
											[g_pilot_data
												 .battle_sequence_state
												 .current_mission_index] =
										g_mission_list[g_selected_mission_list_index]
											.mission_idx;
									g_pilot_data
										.battle_sequence_state
										.mission_list_indices
											[g_pilot_data
												 .battle_sequence_state
												 .current_mission_index] =
										g_selected_mission_list_index;
									break;
								}
							}
							frontend_mission_load_current();
							mission_setup_update_team_counts();
							mission_setup_load_mission_desc_text(
								g_mission_text);
							g_frontend_first_visible_line =
								0;
							if (g_frontend_mission_session_mode !=
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
								mission_setup_send_lobby_state(
									0);
							}
						}
					} else {
						g_pilot_data.mission_description_ids
							[g_pilot_data
								 .mission_directory_id] =
							g_battle_mission_list
								[battle_mission_index]
									.mission_idx;
						for (g_selected_mission_list_index =
							     0;
						     g_mission_count >
						     (unsigned int)
							     g_selected_mission_list_index;
						     ++g_selected_mission_list_index) {
							if (g_mission_list[g_selected_mission_list_index]
								    .mission_idx ==
							    g_pilot_data.mission_description_ids
								    [g_pilot_data
									     .mission_directory_id]) {
								g_pilot_data
									.battle_sequence_state
									.mission_ordinals
										[g_pilot_data
											 .battle_sequence_state
											 .current_mission_index] =
									battle_mission_index;
								g_pilot_data
									.battle_sequence_state
									.mission_list_indices
										[g_pilot_data
											 .battle_sequence_state
											 .current_mission_index] =
									g_selected_mission_list_index;
								break;
							}
						}
						frontend_mission_load_current();
						mission_setup_update_team_counts();
						mission_setup_load_mission_desc_text(
							g_mission_text);
						g_frontend_first_visible_line =
							0;
					}
					keyboard_flush_char_buffer();
					frontend_screen_pop_state();
					frontend_unregister_scrollable_control(
						SCROLLBAR_CONTROL_ID);
				}
			}

			unsigned short text_color = 0xFFFF;
			if (g_pilot_data.mission_description_ids
				    [g_pilot_data.mission_directory_id] ==
			    g_mission_list[battle_mission_index].mission_idx) {
				text_color = g_color_yellow;
			}
			if (g_mission_list[battle_mission_index].file_name[0] ==
				    SINGLE_PLAYER_FILE_PREFIX &&
			    g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				text_color = g_color_gray;
			}
			frontend_text_draw_aligned_in_rect(
				TEXT_FONT_SIZE,
				g_mission_list[battle_mission_index]
					.description,
				&rect, 0, 1, text_color);
			frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
		}
		++display_row;
		if (display_row - g_battle_choice_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}

	memset(last_section_name, 0, sizeof(last_section_name));
	frontend_draw_rect_assign(&rect, LIST_LEFT, LIST_TOP, LIST_RIGHT,
				  ROW_HEIGHT * battle_mission_index +
					  LIST_BOTTOM_PADDING);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	rect.left += MISSION_TEXT_INDENT;
	rect.bottom = rect.top + 14;
	display_row = 0;

	struct RECT award_rect;
	unsigned int award_id;
	unsigned int rebel_award_id;
	unsigned int imperial_award_id;
	int award_faction_id;
	for (battle_mission_index = 0;
	     (unsigned int)g_battle_mission_list_count >
	     (unsigned int)battle_mission_index;
	     ++battle_mission_index) {
		if (g_battle_mission_list[battle_mission_index]
			    .is_unavailable != 0) {
			continue;
		}
		if (strcmp(g_battle_mission_list[battle_mission_index]
				   .section_name,
			   last_section_name) != 0) {
			if (display_row >= g_battle_choice_scroll_offset &&
			    display_row - g_battle_choice_scroll_offset <
				    VISIBLE_ROW_COUNT) {
				frontend_draw_rect_offset_xy(&rect, 0,
							     ROW_HEIGHT);
			}
			strcpy(last_section_name,
			       g_battle_mission_list[battle_mission_index]
				       .section_name);
			++display_row;
		}
		if (display_row - g_battle_choice_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
		if (display_row >= g_battle_choice_scroll_offset &&
		    display_row - g_battle_choice_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			award_id = 0;
			award_faction_id = 0;
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				award_faction_id =
					g_pilot_data.current_faction_id;
				switch (g_pilot_data.mission_directory_id) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_training_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_MELEES:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_melee_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_tournaments
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_combat_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					break;
				case MISSION_DIRECTORY_BATTLES:
					award_id =
						g_pilot_data
							.faction_statistics
								[award_faction_id]
							.sp_battles
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					break;
				default:
					break;
				}
			} else {
				switch (g_pilot_data.mission_directory_id) {
				case MISSION_DIRECTORY_TRAINING_EXERCISES:
					award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_training_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_training_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					rebel_award_id = award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_MELEES:
					award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_melee_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_melee_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					rebel_award_id = award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_TOURNAMENTS:
					award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_tournaments
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_tournaments
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					rebel_award_id = award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
					award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_combat_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_combat_missions
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					rebel_award_id = award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				case MISSION_DIRECTORY_BATTLES:
					award_id =
						g_pilot_data
							.faction_statistics[0]
							.mp_battles
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					imperial_award_id =
						g_pilot_data
							.faction_statistics[1]
							.mp_battles
								[g_battle_mission_list
									 [battle_mission_index]
										 .mission_idx]
							.award_level;
					rebel_award_id = award_id;
					if (imperial_award_id != 0 &&
					    (imperial_award_id < award_id ||
					     award_id == 0)) {
						award_id = imperial_award_id;
						award_faction_id = 1;
					}
					break;
				default:
					break;
				}
			}

			if (award_id != 0) {
				frontend_draw_rect_copy(&award_rect, &rect);
				award_rect.left -= MISSION_TEXT_INDENT;
				award_rect.right =
					award_rect.left + MISSION_TEXT_INDENT;
				if (g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
				    g_pilot_data.mission_directory_id !=
					    MISSION_DIRECTORY_TRAINING_EXERCISES) {
					sprintf(g_frontend_scratch_buffer,
						"medlvl%d", award_id);
					front_image_draw_sprite(
						g_frontend_scratch_buffer,
						award_rect.left,
						award_rect.top);
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (rebel_award_id == 0 &&
						    imperial_award_id != 0) {
							sprintf(g_frontend_scratch_buffer,
								"%s: %s",
								frontend_string_get(
									FRONTSTR_759_IMPERIAL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											    imperial_award_id)));
						} else if (rebel_award_id !=
								   0 &&
							   imperial_award_id ==
								   0) {
							sprintf(g_frontend_scratch_buffer,
								"%s: %s",
								frontend_string_get(
									FRONTSTR_760_REBEL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											    rebel_award_id)));
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%s: %s, %s: %s",
								frontend_string_get(
									FRONTSTR_760_REBEL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											    rebel_award_id)),
								frontend_string_get(
									FRONTSTR_759_IMPERIAL),
								frontend_string_get((
									frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											    imperial_award_id)));
						}
					} else {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get((
							       frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
										   award_id)));
					}
				} else {
					if (award_faction_id == 0) {
						sprintf(g_frontend_scratch_buffer,
							"rcitlvl%d", award_id);
					} else {
						sprintf(g_frontend_scratch_buffer,
							"citlvl%d", award_id);
					}
					if (g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
					    g_pilot_data.mission_directory_id !=
						    MISSION_DIRECTORY_TRAINING_EXERCISES) {
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							award_rect.left,
							award_rect.top);
						if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (rebel_award_id ==
								    0 &&
							    imperial_award_id !=
								    0) {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s",
									frontend_string_get(
										FRONTSTR_759_IMPERIAL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
												     imperial_award_id)));
							} else if (
								rebel_award_id !=
									0 &&
								imperial_award_id ==
									0) {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s",
									frontend_string_get(
										FRONTSTR_760_REBEL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
												     rebel_award_id)));
							} else {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s, %s: %s",
									frontend_string_get(
										FRONTSTR_760_REBEL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
												     rebel_award_id)),
									frontend_string_get(
										FRONTSTR_759_IMPERIAL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
												     imperial_award_id)));
							}
						} else {
							strcpy(g_frontend_scratch_buffer,
							       frontend_string_get((
								       frontend_string_id)(AWARD_TOOLTIP_MEDAL_BASE +
											   award_id)));
						}
					} else {
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							award_rect.left,
							award_rect.top);
						if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (rebel_award_id ==
								    0 &&
							    imperial_award_id !=
								    0) {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s",
									frontend_string_get(
										FRONTSTR_759_IMPERIAL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
												     imperial_award_id)));
							} else if (
								rebel_award_id !=
									0 &&
								imperial_award_id ==
									0) {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s",
									frontend_string_get(
										FRONTSTR_760_REBEL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
												     rebel_award_id)));
							} else {
								sprintf(g_frontend_scratch_buffer,
									"%s: %s, %s: %s",
									frontend_string_get(
										FRONTSTR_760_REBEL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
												     rebel_award_id)),
									frontend_string_get(
										FRONTSTR_759_IMPERIAL),
									frontend_string_get(
										(frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
												     imperial_award_id)));
							}
						} else {
							strcpy(g_frontend_scratch_buffer,
							       frontend_string_get((
								       frontend_string_id)(AWARD_TOOLTIP_CITATION_BASE +
											   award_id)));
						}
					}
				}
				frontend_button_draw_sprite_and_tooltip(
					&award_rect, NULL,
					g_frontend_scratch_buffer,
					TEXT_FONT_SIZE, 0xFFFF);
			}
			frontend_draw_rect_offset_xy(&rect, 0, ROW_HEIGHT);
		}
		++display_row;
		if (display_row - g_battle_choice_scroll_offset >=
		    VISIBLE_ROW_COUNT) {
			break;
		}
	}
	return 0;
}
