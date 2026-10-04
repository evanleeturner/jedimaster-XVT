#ifndef XVT_RUNTIME_FLIGHT_INTERNAL_H
#define XVT_RUNTIME_FLIGHT_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/assets/file.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/audio/music_cd.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/flight_render.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_alert.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/flight_player.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/dinput.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/flight_frame.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/storage/storage.h"
#include "xvt_runtime/timing/host_clock.h"

/* Flight entry and loading, run by the flight task's phases: Prepare, CreateDevices, then Globals,
 * Palette, MissionSetup and Runtime in that order, and Cleanup at the end. The includes give the
 * runtime files the flight code's globals. */

/* Loads the config and reads the launch options, each found as a substring anywhere in command,
 * quoted names included: traincourse, nopilot, [no]dinput, [no]sfx, [no]music, [no]voice,
 * [no]tickcounter, [no]mipmaps, inprogress, newnet, nolauncher, [no]fullscreen, [no]pageflip.
 * Then splits command in place into 7 arguments at spaces, ~ quoting one, and opens the game
 * session with them. Returns net_session_init_game_session's result, which the flight task reads as
 * XVT_FLIGHT_NETWORK_PENDING, nonzero success or 0 failure; returns 0 for a NULL command, fewer
 * than 7 arguments, or a main window that cannot be focused. */
int xvt_flight_entry_prepare(char *command);
/* Applies the single- or multiplayer display and detail settings, opens the flight display, writes
 * the resolution, color depth and 3D hardware it got back into the config, starts DirectInput
 * (falling back to none) and the sound engine, and takes the mission file from the arguments.
 * Returns 0 when the display or sound fails. */
int xvt_flight_entry_create_devices(void);
/* Shuts down what Prepare and CreateDevices started (sound, DirectInput, the game session with the
 * network flight, 3D hardware), clears and releases the flight surfaces and palette, and hands
 * rendering back to the frontend. */
void xvt_flight_entry_cleanup(void);
/* Forgets the flight's memory handles and pool pointers, without freeing them. */
void xvt_flight_loading_reset(void);
/* Resets the flight's globals, the local player slot and the player records, marking the first
 * session-count players connected. Sets the mission options from the config: difficulty is medium
 * for multiplayer in a directory from combat engagements on, and a configured value above hard
 * becomes easy; the time limits and AI opponents come from the config only in multiplayer. Seeds
 * the game random state from the configured seed in multiplayer and from the clock in single
 * player. Loads the AI plans. */
void xvt_flight_loading_globals(void);
/* Configures the display for the resolution, loads the flight palette with its color order
 * reversed and channels scaled to 6 bits, and allocates the global buffers. In 8-bit color, loads
 * the mission's .pal file into colors 64 through 255, or marks the mission palette for generation
 * when there is none. */
void xvt_flight_loading_palette(void);
/* Sets the proving grounds craft and level (craft 2, level 4 for traincourse, else none), resets
 * the flight input, refills the noise table from rand(), and resets the message log and MFD pages. */
void xvt_flight_loading_mission_setup(void);
/* Initializes the mission runtime state; when music is on and the music CD starts, plays the flight
 * track from one of 4 random start points. In the proving grounds, starts the level, crediting the
 * points of the levels skipped. */
void xvt_flight_loading_runtime(void);

#endif
