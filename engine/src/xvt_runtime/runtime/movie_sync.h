#ifndef XVT_RUNTIME_MOVIE_SYNC_H
#define XVT_RUNTIME_MOVIE_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Multiplayer movie sync: every player watches the movie, reports when done, and waits for the
 * rest. A player counts as waiting once it has finished; the frontend packet handler marks remote
 * players waiting, or drops them, from their movie-sync packets. */

/* Lists the first net_count_ready_players() entries of the multiplayer roster, up to 8, none
 * waiting, and clears the completion state and deadline. */
void xvt_movie_sync_begin(void);
/* The first call after Begin marks the local player waiting, sends a movie-sync packet and sets a
 * deadline 5 seconds out for the host and 20 for a client; later calls do nothing. */
void xvt_movie_sync_report_finished(void);
/* Processes frontend network packets, and marks the deadline passed once ReportFinished's deadline expires.
 * Returns 1 when every listed player is waiting, otherwise 0. */
int xvt_movie_sync_update(void);
/* After ReportFinished, draws each listed player's name with watching or waiting, four per row in the top
 * margin; a listed player missing from the roster draws an empty label. Once the deadline has
 * passed and bottom_margin is positive, draws the still-waiting prompt in the bottom margin: the
 * host's offers C to continue, a client's offers E to exit. */
void xvt_movie_sync_draw(int top_margin, int bottom_margin);

#ifdef __cplusplus
}
#endif

#endif
