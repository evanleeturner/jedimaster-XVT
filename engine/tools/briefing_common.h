/* What the briefing tools share: the folder of links that hands the engine
 * any mission file as one of a mission directory's, the pilot pointed at it,
 * the one-entry mission list in place of the directory's, and the briefing's
 * state line. The tools take mission_setup_load_mission_list with the GNU
 * linker's --wrap; this file holds the wrapper. */
#ifndef XVT_TOOLS_BRIEFING_COMMON_H
#define XVT_TOOLS_BRIEFING_COMMON_H

#include <stddef.h>

/* The name the linked mission takes inside its directory's folder. */
extern const char BRIEFING_LINK_NAME[];

/* Makes a folder under /tmp holding a link to every entry of root but the
 * top-level folder of directory (g_mission_directory_names, any letter
 * case), and in that folder's place a folder holding BRIEFING_LINK_NAME
 * linked to file; writes the folder's path into folder. The engine then
 * opens the mission as "<directory>\<BRIEFING_LINK_NAME>". Returns 0,
 * printing why after program's name, when it cannot. */
int briefing_make_link_folder(const char *program, const char *root,
			      const char *file, int directory, char *folder,
			      size_t size);

/* Points the pilot at the linked mission of directory for team, the first
 * mission of the list; datapad sounds on. */
void briefing_set_pilot(int team, int directory);

/* Prints the briefing's state line for frame: "state F play=P t=T c=X,Y
 * tc=X,Y s=X,Y ts=X,Y page=N last=B slots=A0:B0,A1:B1 markers=M labels=L"
 * (briefing_dump's header comment names each field). */
void briefing_print_state(int frame);

#endif
