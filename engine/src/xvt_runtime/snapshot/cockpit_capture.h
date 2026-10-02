#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_CAPTURE_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_CAPTURE_H

#include "xvt_runtime/snapshot/cockpit_state.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Cockpit capture: three copies of the cockpit state.
 *   working    refreshed from the live game by RefreshInstruments
 *   pending    the frame being composed: LatchComposition copies working in, the Latch* calls
 *              place pages, launchers and messages, and Seal adds the CRT and page text
 *   completed  the last presented frame: Presented stamps generations and copies pending here
 * Export hands out completed. All calls run on the host thread. */

/* Resets text fields, readouts, pages and messages, and clears all three states and the
 * prepared-resources mark. */
void XvtCockpit_Reset(void);
/* Starts a frame: hides the progress bar and loading text, clears the page latches, invalidates
 * the pending CRT, and unseals. A composition selected earlier is kept. */
void XvtCockpit_BeginFrame(void);
/* For the local player only: refreshes working from the live view, cockpit definition,
 * instruments, readouts, text fields and palette. Working is valid when the cockpit layout is. */
void XvtCockpit_RefreshInstruments(int player);
/* Copies a valid working state to pending, hides the placed message panes, marks the
 * composition selected and unseals. Does nothing while working is invalid. */
void XvtCockpit_LatchComposition(void);
/* Places every open MFD page except the message log in pending and latches the visible ones;
 * the damage page is skipped on the map and the map help page off it. */
void XvtCockpit_LatchPages(void);
/* Places launcher count 0 to 3 in pending at this rectangle, drawn after the CRT and keyed on the
 * bypass color, and shows the launcher. Other launchers are ignored. */
void XvtCockpit_LatchLauncher(unsigned launcher, int x, int y, int width, int height);
/* Places the message log page in pending and latches it when open; it is widened when
 * g_flightPlayerCount is 1 or more. */
void XvtCockpit_LatchMessages(void);
/* Hides every placed message pane until latched again. */
void XvtCockpit_BeginMessagePlacement(void);
/* Latches a message pane, except the ready pane while the message log is open. For the ready
 * pane it also places the network ping and lag fields by the same offset. */
void XvtCockpit_LatchMessage(XvtCockpitMessageId pane, int source_x, int source_y, int x, int y, int width,
							 int height);
/* Starts pending from the last presented frame, for an overlay drawn over it, and unseals. */
void XvtCockpit_RetainPresentedFrame(void);
/* With a composition selected, stores crt in pending, exports the pages into it and seals it;
 * otherwise does nothing. */
void XvtCockpit_Seal(const XvtSnapPreview* crt);
/* Publishes pending as the presented frame when it is sealed or standalone_overlay is set;
 * otherwise does nothing. A standalone overlay without a valid layout first captures the cockpit
 * definition. Exports the messages into it, marks it valid when no composition was
 * selected but an alert or loading display shows, raises each part's generation that differs
 * from the last presented frame, and stamps a new presentation serial. */
void XvtCockpit_Presented(int standalone_overlay);
/* Copies the last presented frame into destination. */
void XvtCockpit_Export(XvtCockpitState* destination);
/* Fills destination with the cockpit definition, screen size, features and palettes, including
 * each loaded view's 64-color palette. Leaves it cleared and invalid while working is invalid or
 * the cockpit resources are not loaded; when panel 0 has no asset, the definition is captured and the
 * rest left cleared and invalid. */
void XvtCockpit_ExportResources(XvtCockpitResources* destination);
/* Records the resource generation the renderer has prepared. */
void XvtCockpit_ResourcesPrepared(uint64_t generation);
/* Returns 1 when working is valid and the renderer has prepared its resource generation. */
int XvtCockpit_LoadingAssetsReady(void);
/* Copies source into destination, but only the used rows and glyphs of the page and overlay
 * stores; the rest of destination's stores keeps its old contents. */
void XvtCockpit_CopyState(XvtCockpitState* destination, const XvtCockpitState* source);
#ifdef __cplusplus
}
#endif
#endif
