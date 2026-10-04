#ifndef XVT_RUNTIME_NETWORK_BROWSER_H
#define XVT_RUNTIME_NETWORK_BROWSER_H
#ifdef __cplusplus
extern "C" {
#endif
/* The join-game screen over the multiplayer directory: the modern bodies of
 * frontend_net_join_game_screen and of its list, roster and mission-briefing
 * draws. Rooms, selection, scroll offset and mission preview are held by the
 * network task. */

/* Frontend screen update; frame_counter is the screen's frame counter. When it
 * is 0, sets up: clears the screen-entry setup skip, the
 * connection-type-editable flag and the game-session-in-progress flag, draws
 * the static background and opens the directory browser; an unconfigured
 * directory shows a confirm dialog and returns 0. Otherwise, each frame draws
 * the selected room's name, the list (a clicked row becomes the selection), the
 * roster, the briefing, a status line, the pilot line when a pilot is loaded
 * and the sidebars. Returns 1 when the shared frontend controls act, otherwise
 * 0; with a dialog open, Leave and Join are not drawn. Leave returns to the
 * concourse; Join, drawn only when the network task allows it, begins a
 * connect. */
int xvt_network_browser_screen(int frame_counter);
/* Draws the rooms six rows at a time under a heading, with a scrollbar past six rooms, and keeps
 * the task's scroll offset within range. Row color: gray when incompatible, red when not
 * joinable, yellow when full, green otherwise; the selected row is shaded and a password room
 * shows a key. Each row shows name, free slots and the age of the whole snapshot, the same on
 * every row. Returns the index of the row clicked with either button this frame, or -1. */
int xvt_network_browser_draw_list(void);
/* Draws the players heading and, for the selected room, each player's rating and name, four per
 * column; a rating above Jedi Master shows as Target Drone. Trusts the room's player count to be
 * at most the roster's 8 entries, as the directory header promises. Returns 1. */
int xvt_network_browser_draw_roster(void);
/* Draws the mission heading, with the preview title when there is one, and, for a selected room,
 * the wrapped preview text, with a scrollbar when it wraps to more than six lines; the scroll
 * position is kept in the preview and reset to 0 when no scrollbar is needed. Returns 1. */
int xvt_network_browser_draw_mission(void);
#ifdef __cplusplus
}
#endif
#endif
