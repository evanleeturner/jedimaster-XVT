#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_PAGES_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_PAGES_H
#include "xvt_runtime/snapshot/cockpit_state.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xvt_cockpit_page_section {
	XVT_COCKPIT_PAGE_HEADER,
	XVT_COCKPIT_PAGE_BODY,
	XVT_COCKPIT_PAGE_SECTION_COUNT
} xvt_cockpit_page_section;

/* Cockpit MFD pages. Each page has a header and a body section, captured glyph by glyph between
 * BeginSection and EndSection into a working page. Latch copies a working page to its placed
 * page, and Export writes the placed pages into cockpit state. Section buffers grow on demand up
 * to XVT_HUD_PAGE_GLYPH_CAPACITY glyphs. Every call taking a page ignores one out of range.
 * A capture failure never publishes partial text: the section keeps its old content and is
 * marked failed, and latching a page with a failed section makes Export invalidate the state. */

/* Reset frees and clears the working and placed pages and the capture buffer; ResetWorking frees
 * and clears only the working pages. Both abandon an open capture. */
void xvt_cockpit_pages_reset(void);
void xvt_cockpit_pages_reset_working(void);
/* Clears the latch-failure flag and marks every placed page as not latched this frame. */
void xvt_cockpit_pages_begin_frame(void);
/* Sets the working page's origin; glyphs, rows and bounds are recorded relative to it. */
void xvt_cockpit_pages_set_origin(unsigned page, int x, int y);
/* Empties both sections of a working page, clears its colors and failure marks, and raises its
 * generations. */
void xvt_cockpit_pages_clear(unsigned page);
/* Opens a capture of one section of a page. Opening while another is open logs an error, fails
 * the open capture and opens nothing. Out-of-range arguments are ignored. */
void xvt_cockpit_pages_begin_section(unsigned page,
				     xvt_cockpit_page_section section);
/* Closes the capture. A failed capture marks the section failed and keeps its old content;
 * otherwise the section is replaced only when its glyphs or rows changed, which raises its and
 * the page's generation. */
void xvt_cockpit_pages_end_section(void);
/* Captures a glyph at the live text cursor into the open section. Ignored with no capture, after
 * a failure, or outside the clip; running out of capacity or memory logs an error and fails the
 * capture. */
void xvt_cockpit_pages_record_glyph(unsigned character, unsigned advance,
				    unsigned height, int narrow);
/* Ends the current row and starts one with this key, at the live clip rectangle and text
 * background color. Ignored with no capture or after a failure; more than
 * XVT_HUD_ROWS_PER_SECTION rows logs an error and fails the capture. */
void xvt_cockpit_pages_record_row(uint32_t key, int selected);
/* Record the page's background or border as the live clip rectangle in the text background
 * color, 0 when that is the key color. The page's generation rises on a change. */
void xvt_cockpit_pages_record_background(unsigned page);
void xvt_cockpit_pages_record_border(unsigned page);
/* Records the first visible row, the total rows and the selected row of a working page. */
void xvt_cockpit_pages_record_scroll(unsigned page, int first_visible_row,
				     int total_rows, int selected_row);
/* Records the working page's display mode. */
void xvt_cockpit_pages_record_mode(unsigned page, unsigned mode);
/* Copies a working page to its placed page and marks it latched this frame; the placed page's
 * generation rises when anything changed. A page with a failed section, or a copy that cannot
 * allocate, is not latched and makes this frame's Export invalidate the state. */
void xvt_cockpit_pages_latch(unsigned page);
/* After a latch failure this frame, marks state invalid and writes nothing more. Otherwise writes
 * each page that state already shows and that was latched this frame, packing its header and
 * then its body into the page store; other pages are hidden. Overflowing the store logs an error
 * and marks state invalid, leaving later pages unwritten. */
void xvt_cockpit_pages_export(struct xvt_cockpit_state *state);
#ifdef __cplusplus
}
#endif
#endif
