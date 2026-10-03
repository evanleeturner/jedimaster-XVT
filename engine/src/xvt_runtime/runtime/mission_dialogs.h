#ifndef XVT_RUNTIME_MISSION_DIALOGS_H
#define XVT_RUNTIME_MISSION_DIALOGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XvtMissionDialogAction {
	XVT_MISSION_NOTICE,
	XVT_MISSION_SETUP_CANCELLED,
	XVT_MISSION_SETUP_BOOTED,
	XVT_MISSION_TEAM_CANCELLED,
	XVT_MISSION_ASSIGNMENT_CANCELLED,
	XVT_MISSION_BRIEFING_CANCELLED,
	XVT_MISSION_SETUP_HOST_LEAVE,
	XVT_MISSION_CLIENT_LEAVE,
	XVT_MISSION_TEAM_CLIENT_LEAVE,
	XVT_MISSION_TEAM_PREVIOUS,
	XVT_MISSION_HOST_RESTART,
	XVT_MISSION_SOLO_BACK_TO_SETUP,
	XVT_MISSION_SOLO_BACK_TO_TEAMS,
	XVT_MISSION_DEBRIEF_CLIENT_LEAVE,
	XVT_MISSION_DEBRIEF_HOST_ABORT,
	XVT_MISSION_DEBRIEF_SOLO_ABORT,
	XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER
} XvtMissionDialogAction;

/* Dialog continuation for the mission screens' dialogs: runs the tail for action, in the suspended
 * screen's callback slot before its exit callback. NOTICE does nothing. The four CANCELLED actions
 * and SETUP_BOOTED ignore result: each returns to the join screen, except BRIEFING_CANCELLED on
 * the host, which returns to the concourse; TEAM_CANCELLED also shuts down the DirectPlay session.
 * TEAM_PREVIOUS also ignores result: a network session sends return-to-setup, a solo one reopens
 * mission setup. Every other tail runs only when result is nonzero. SETUP_HOST_LEAVE tells every
 * player the host cancelled, shuts down the session and opens the concourse. The CLIENT_LEAVE
 * actions tell the host the player left and shut down the session, then open the join screen, or
 * the concourse from the debrief. HOST_RESTART sends return-to-setup and DEBRIEF_HOST_ABORT sends
 * return-to-mission-selection. The SOLO_BACK actions reopen mission setup or team assignment. The
 * DEBRIEF_SOLO_ABORT actions clear the solo launch flags and the roster-authority flag, and the
 * multiplayer roster for CLEAR_ROSTER, then reopen mission setup. Always turns the overlay text
 * off and returns 0. */
int XvtMissionDialogs_Resume(int result, int action);

#ifdef __cplusplus
}
#endif

#endif
