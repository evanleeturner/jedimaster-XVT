#ifndef XVT_RUNTIME_CAMPAIGN_TASK_H
#define XVT_RUNTIME_CAMPAIGN_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

enum { XVT_CAMPAIGN_PENDING = -1 };

/* The waiting parts of the team-assignment and debrief screens' first frame: campaign and battle
 * continuation, cutscenes, and the promoted pilot's rename. While a prefix is pending, the frontend
 * task holds the frame counter at 0, so the screen calls the prefix again next frame. */

/* Entry prefixes return pending, cancelled (0), or ready for the screen tail (1). */
/* Team-assignment prefix. Checks the installation (a missing one ends the program). Returns 1 at
 * once unless a mission sequence is active in the training or combat directory, the screen's entry
 * setup is not skipped, and the debrief did not choose to enter the current mission. Otherwise continues
 * the campaign or battle sequence, clearing the remote battle state when that returns 0; training
 * then plays the phase-0 cutscenes. In a network session a cutscene result of 0 (a movie's
 * nonzero result, or no cutscene table) leaves the game: it tells the host, shuts down the
 * session, opens the join screen and returns 0. */
int XvtCampaignTask_EnterTeams(void);
/* Debrief prefix. Shows the cursor and flushes the keyboard; after a completed training mission in
 * an active sequence, plays the phase-1 cutscenes, where a cutscene result of 0 in a network
 * session leaves the game for the concourse and returns 0. Then allocates the briefing text in an
 * active training sequence, clears the ready flags of players who left, resets the network roster
 * to the local player and, after a promotion in a network session, renames the local player with
 * the new rating. Returns 1 when done. */
int XvtCampaignTask_EnterDebrief(void);
/* Takes up to 32 queued application packets, dropping each that is not a complete packet_type
 * packet. Stores the first complete one in packet and returns 1; returns -1 while waiting. The
 * first call of a wait starts a 30-second host-clock limit, checked after each packet read, so a
 * packet read after the limit still wins; past it, logs a warning and returns 0. packet is set to
 * NULL unless 1 is returned. */
int XvtCampaignTask_WaitPacket(int packet_type, int** packet);
/* 1 after a prefix returned pending, until it finishes or Reset. */
int XvtCampaignTask_IsPending(void);
/* 1 while WaitPacket is waiting, so the port keeps running without window focus. */
int XvtCampaignTask_ContinuesWithoutFocus(void);
/* Forgets any prefix or packet wait in progress and resets the cutscene task. */
void XvtCampaignTask_Reset(void);

#ifdef __cplusplus
}
#endif

#endif
