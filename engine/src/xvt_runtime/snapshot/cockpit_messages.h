#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_MESSAGES_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_MESSAGES_H
#include "xvt_runtime/snapshot/cockpit_state.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Cockpit overlay text: the three message panes, the alert box, and the loading progress bar
 * and text. The recovered HUD code brackets its drawing with Begin/End calls, and RecordGlyph
 * captures each glyph into whichever capture is open: loading text first, then a message, then
 * an alert line. Messages are captured into working panes and shown only once latched into a
 * placed pane. All their generations are drawn from one shared counter. */

/* Reset clears all panes, the alert and the progress bar, and abandons open captures; the
 * loading text survives, since it is drawn before mission start resets flight state.
 * ResetWorking clears only the working panes and the open message capture. */
void xvt_cockpit_messages_reset(void);
void xvt_cockpit_messages_reset_working(void);
/* Hides the progress bar and the loading text for this frame. */
void xvt_cockpit_messages_begin_flight_frame(void);
/* Opens a message capture from the live pane record: pane_type 8 is the flight-group pane, 3, 4
 * and 7 the system pane, any other the ready pane. The clip rectangle's corner is the origin. */
void xvt_cockpit_messages_begin_message(int pane_type);
/* Records how many characters of the open message are revealed. */
void xvt_cockpit_messages_record_reveal(unsigned characters);
/* Closes the message capture into its working pane, unless a glyph overflowed, which discards
 * it. The generation rises only when visibility, origin or glyphs changed. */
void xvt_cockpit_messages_end_message(void);
/* Clears a visible working pane and raises the generation; others are left alone. */
void xvt_cockpit_messages_clear(xvt_cockpit_message_id pane);
/* Hides every placed pane until it is latched again. */
void xvt_cockpit_messages_begin_placement(void);
/* Places working pane at the destination rectangle, moving its glyphs by its origin minus the
 * source point, with the live pane timer, and the live age while the live record still holds
 * that message. Out-of-range panes are ignored. */
void xvt_cockpit_messages_latch(xvt_cockpit_message_id pane, int source_x,
				int source_y, int x, int y, int width,
				int height);
/* Captures a glyph at the live text cursor into the open capture; with none open it does
 * nothing, as for a glyph outside the clip. A full message or alert line logs an error and
 * discards that capture; full loading text requests a fatal error. */
void xvt_cockpit_messages_record_glyph(unsigned character, unsigned advance,
				       unsigned height, int narrow);
/* Clears the alert, its generation included, and abandons an open alert line. */
void xvt_cockpit_messages_begin_alert(void);
/* Opens a capture of alert line max(mode - 1, 0), building on the active alert
 * or on an empty one, and sets the alert's placement to the rectangle. It
 * clears that line and those after it; mode 1 also sets the border color. Modes
 * outside 0 to 3 are ignored. */
void xvt_cockpit_messages_begin_alert_line(int mode, int x, int y, int width,
					   int height);
/* Closes the alert line: fills the rows from it down with the text background color and commits
 * the alert, unless a glyph overflowed, which discards the line. The generation rises on any
 * change. */
void xvt_cockpit_messages_end_alert_line(void);
/* Deactivates the alert, raising the generation if it was active. */
void xvt_cockpit_messages_end_alert(void);
/* Records the loading progress bar; the generation rises on any change. */
void xvt_cockpit_messages_record_progress(unsigned step, int x, int y,
					  int width, int height,
					  int filled_width);
/* Hides the progress bar and clears the loading text. */
void xvt_cockpit_messages_clear_progress(void);
/* Bracket the loading text. Begin starts an empty capture relative to the screen; End makes the
 * text visible when any glyph was captured and always raises the generation. */
void xvt_cockpit_messages_begin_loading_text(void);
void xvt_cockpit_messages_end_loading_text(void);
/* Writes the placed panes, the alert and the loading state into state, packing their glyphs into
 * the overlay store in that order; the store's size is checked at compile time to fit them all.
 * Also sets the view's screen size while the alert, the progress bar or the loading text shows. */
void xvt_cockpit_messages_export(struct xvt_cockpit_state *state);
#ifdef __cplusplus
}
#endif
#endif
