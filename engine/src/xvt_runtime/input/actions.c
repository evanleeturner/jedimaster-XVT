#include "xvt_runtime/input/actions.h"
#include "xvt/flight/flight_input.h"
#include <string.h>

typedef struct XvtActionDefinition {
	const char *name, *label;
	XvtInputActionCategory category;
	uint16_t key;
} XvtActionDefinition;

static const XvtActionDefinition g_actions[XVT_INPUT_ACTION_COUNT] = {
	{ "none", "None", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_NONE },
	{ "fire_weapon", "Fire Weapon / Warhead", XVT_INPUT_ACTION_CATEGORY_WEAPONS, FLIGHT_KEY_ALT_2 },
	{ "cycle_weapon_group", "Cycle Weapon Group", XVT_INPUT_ACTION_CATEGORY_WEAPONS, FLIGHT_KEY_W },
	{ "cycle_weapon_firing_mode", "Cycle Weapon Firing Mode", XVT_INPUT_ACTION_CATEGORY_WEAPONS,
	  FLIGHT_KEY_X },
	{ "fire_countermeasure_or_center_map_target", "Fire Countermeasure / Center Map Target",
	  XVT_INPUT_ACTION_CATEGORY_WEAPONS, FLIGHT_KEY_C },
	{ "toggle_beam", "Toggle Beam Weapon", XVT_INPUT_ACTION_CATEGORY_WEAPONS, FLIGHT_KEY_B },
	{ "cycle_beam_recharge_rate", "Cycle Beam Recharge Rate", XVT_INPUT_ACTION_CATEGORY_WEAPONS,
	  FLIGHT_KEY_F8 },
	{ "cycle_cannon_recharge_rate", "Cycle Laser / Cannon Recharge Rate", XVT_INPUT_ACTION_CATEGORY_WEAPONS,
	  FLIGHT_KEY_F9 },
	{ "cycle_shield_recharge_rate", "Cycle Shield Recharge Rate", XVT_INPUT_ACTION_CATEGORY_WEAPONS,
	  FLIGHT_KEY_F10 },
	{ "cycle_shield_mode", "Cycle Shield Direction", XVT_INPUT_ACTION_CATEGORY_WEAPONS, FLIGHT_KEY_S },
	{ "xfer_cannon_to_shields", "Cannon Energy to Shields", XVT_INPUT_ACTION_CATEGORY_WEAPONS,
	  FLIGHT_KEY_APOSTROPHE },
	{ "xfer_shields_to_cannon", "Shield Energy to Cannons", XVT_INPUT_ACTION_CATEGORY_WEAPONS,
	  FLIGHT_KEY_SEMICOLON },
	{ "xfer_all_cannon_to_shields", "All Cannon Energy to Shields", XVT_INPUT_ACTION_CATEGORY_WEAPONS,
	  FLIGHT_KEY_QUOTES },
	{ "target_roll_modifier", "Target / Roll Modifier", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_ALT_3 },
	{ "auto_target", "Target Under Crosshair", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_ALT_1 },
	{ "target_next", "Next Target", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_T },
	{ "target_prev", "Previous Target", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_Y },
	{ "target_nearest_fighter_or_mine", "Nearest Enemy Fighter / Mine", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_R },
	{ "target_my_attacker", "My Attacker", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_E },
	{ "target_attacker_chain", "Target's Attacker", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_A },
	{ "target_newest_craft", "Newest Craft", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_U },
	{ "target_objective", "Nearest Objective Craft", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_O },
	{ "target_nearest_hostile_player", "Nearest Enemy Player", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_P },
	{ "target_next_player", "Next Player", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_SHIFT_P },
	{ "target_incoming_warhead", "Nearest Incoming Warhead", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_I },
	{ "target_next_friendly", "Next Friendly Craft", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_F1 },
	{ "target_prev_friendly", "Previous Friendly Craft", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_F2 },
	{ "target_next_enemy", "Next Enemy Craft", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_F3 },
	{ "target_prev_enemy", "Previous Enemy Craft", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_F4 },
	{ "target_component_next", "Next Target Component", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_COMMA },
	{ "target_component_prev", "Previous Target Component", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_LESS_THAN },
	{ "target_clear", "Clear Target", XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_ALT_C },
	{ "toggle_threat_display_or_zoom_map_target", "Toggle Threat Display / Zoom Map to Target",
	  XVT_INPUT_ACTION_CATEGORY_TARGETING, FLIGHT_KEY_Z },
	{ "target_preset_store_1", "Store Target Preset 1", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_SHIFT_F5 },
	{ "target_preset_store_2", "Store Target Preset 2", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_SHIFT_F6 },
	{ "target_preset_store_3", "Store Target Preset 3", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_SHIFT_F7 },
	{ "target_preset_recall_1", "Recall Target Preset 1", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_F5 },
	{ "target_preset_recall_2", "Recall Target Preset 2", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_F6 },
	{ "target_preset_recall_3", "Recall Target Preset 3", XVT_INPUT_ACTION_CATEGORY_TARGETING,
	  FLIGHT_KEY_F7 },
	{ "throttle_up", "Increase Throttle", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_EQUAL },
	{ "throttle_down", "Decrease Throttle", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_MINUS },
	{ "throttle_up_or_track_map_target", "Increase Throttle / Track Map Target",
	  XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_PAD_PLUS },
	{ "throttle_down_or_stop_tracking_map_target", "Decrease Throttle / Stop Tracking Map Target",
	  XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_PAD_MINUS },
	{ "throttle_zero", "Zero Throttle", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_BACKSLASH },
	{ "throttle_one_third", "One-Third Throttle", XVT_INPUT_ACTION_CATEGORY_THROTTLE,
	  FLIGHT_KEY_LEFT_BRACKET },
	{ "throttle_two_thirds", "Two-Thirds Throttle", XVT_INPUT_ACTION_CATEGORY_THROTTLE,
	  FLIGHT_KEY_RIGHT_BRACKET },
	{ "throttle_full", "Full Throttle", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_BACKSPACE },
	{ "match_speed", "Match Target Speed", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_ENTER },
	{ "power_preset_recall_1", "Recall Power Preset 1", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_9 },
	{ "power_preset_recall_2", "Recall Power Preset 2", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_0 },
	{ "power_preset_store_1", "Store Power Preset 1", XVT_INPUT_ACTION_CATEGORY_THROTTLE,
	  FLIGHT_KEY_SHIFT_9 },
	{ "power_preset_store_2", "Store Power Preset 2", XVT_INPUT_ACTION_CATEGORY_THROTTLE,
	  FLIGHT_KEY_SHIFT_0 },
	{ "toggle_slam", "Toggle SLAM / Overdrive", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_N },
	{ "toggle_s_foils", "Toggle S-Foils", XVT_INPUT_ACTION_CATEGORY_THROTTLE, FLIGHT_KEY_V },
	{ "view_toggle_high_angle", "Toggle High-Angle View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_0 },
	{ "view_toggle_cockpit", "Toggle Cockpit", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PERIOD },
	{ "toggle_external_camera_or_follow_map_target", "Toggle External Camera / Follow Map Target",
	  XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_SLASH },
	{ "toggle_camera_control_or_stop_following_map_target",
	  "Toggle External Camera Control / Stop Following Map Target", XVT_INPUT_ACTION_CATEGORY_VIEW,
	  FLIGHT_KEY_PAD_STAR },
	{ "view_left_shoulder", "Left Shoulder View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_1 },
	{ "view_rear", "Rear View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_2 },
	{ "view_right_shoulder", "Right Shoulder View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_3 },
	{ "view_left_wing", "Left Wing View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_4 },
	{ "view_straight_up", "Straight Up View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_5 },
	{ "view_right_wing", "Right Wing View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_6 },
	{ "view_left_forward", "Left Forward View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_7 },
	{ "view_forward", "Forward View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_8 },
	{ "view_right_forward", "Right Forward View", XVT_INPUT_ACTION_CATEGORY_VIEW, FLIGHT_KEY_PAD_9 },
	{ "info_goals", "Mission Goals", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_G },
	{ "info_scoreboard", "Scoreboard", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_K },
	{ "info_messages", "Messages", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_L },
	{ "info_damage", "Damage Report", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_D },
	{ "info_friendly_craft", "Friendly Craft", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_F },
	{ "info_enemy_craft", "Enemy Craft", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_SHIFT_F },
	{ "info_previous", "Previous Info", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_MFD_CYCLE_1 },
	{ "info_next", "Next Info", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_MFD_CYCLE_2 },
	{ "info_scroll_up", "Scroll Info Up", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_MFD_SCROLL_UP },
	{ "info_scroll_down", "Scroll Info Down", XVT_INPUT_ACTION_CATEGORY_INFORMATION,
	  FLIGHT_KEY_MFD_SCROLL_DOWN },
	{ "info_map", "Map", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_M },
	{ "info_map_autopilot", "Map with Autopilot", XVT_INPUT_ACTION_CATEGORY_INFORMATION, FLIGHT_KEY_SHIFT_M },
	{ "comm_assign_target", "Assign Target to Wingman", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_A },
	{ "comm_request_resupply", "Request Resupply", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_B },
	{ "comm_cover_me", "Order Wingman to Cover Me", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_C },
	{ "comm_evade", "Order Target to Evade", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_SHIFT_E },
	{ "comm_continue_mission", "Continue Mission", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_G },
	{ "comm_head_home", "Order Target to Head Home", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_H },
	{ "comm_ignore_target", "Ignore Current Target", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_I },
	{ "comm_report_orders", "Report Current Orders", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_R },
	{ "comm_reinforcements", "Request Reinforcements", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	  FLIGHT_KEY_SHIFT_S },
	{ "comm_stop_and_wait", "Stop and Wait", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_SHIFT_W },
	{ "chat", "Chat / Cycle Recipients", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_TAB },
	{ "chat_send", "Send Message", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_ENTER },
	{ "chat_cancel", "Cancel Message", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_ESCAPE },
	{ "taunt_1", "Send Taunt 1", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_1 },
	{ "taunt_2", "Send Taunt 2", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_2 },
	{ "taunt_3", "Send Taunt 3", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_3 },
	{ "taunt_4", "Send Taunt 4", XVT_INPUT_ACTION_CATEGORY_COMMUNICATIONS, FLIGHT_KEY_4 },
	{ "hyperspace_or_map_help", "Hyperspace / Map Help", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_H },
	{ "jump_craft", "Jump to Another Craft", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_J },
	{ "eject", "Eject", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_ALT_E },
	{ "confirm_order_or_toggle_map_view", "Confirm Order / Toggle Map View", XVT_INPUT_ACTION_CATEGORY_SYSTEM,
	  FLIGHT_KEY_SPACE },
	{ "end_mission", "End Mission", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_Q },
	{ "leave_observation", "Leave Observation Deck", XVT_INPUT_ACTION_CATEGORY_SYSTEM,
	  FLIGHT_KEY_ABORT_MISSION },
	{ "pause", "Pause", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_ALT_P },
	{ "escape", "Options / Back", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_ESCAPE },
	{ "cycle_graphics_detail", "Cycle Classic Graphics Detail", XVT_INPUT_ACTION_CATEGORY_SYSTEM,
	  FLIGHT_KEY_ALT_D },
	{ "cycle_brightness", "Cycle Classic Brightness", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_ALT_B },
	{ "toggle_interlace", "Toggle Classic Interlace", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_ALT_I },
	{ "show_version", "Show Game Version", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_ALT_V },
	{ "toggle_message_logging", "Toggle Message Logging", XVT_INPUT_ACTION_CATEGORY_SYSTEM,
	  FLIGHT_KEY_SHIFT_L },
	{ "toggle_system_messages", "Toggle System Messages", XVT_INPUT_ACTION_CATEGORY_SYSTEM,
	  FLIGHT_KEY_ALT_S },
	{ "screenshot", "Screenshot", XVT_INPUT_ACTION_CATEGORY_SYSTEM, FLIGHT_KEY_SCREENSHOT },
};

XvtInputAction XvtInputActions_FromName(const char* name) {
	if (name)
		for (int i = 0; i < XVT_INPUT_ACTION_COUNT; ++i)
			if (!strcmp(name, g_actions[i].name))
				return (XvtInputAction)i;
	return XVT_INPUT_ACTION_NONE;
}

const char* XvtInputActions_ToName(XvtInputAction action) {
	return (unsigned)action < XVT_INPUT_ACTION_COUNT ? g_actions[action].name : "none";
}

const char* XvtInputActions_DisplayName(XvtInputAction action) {
	return (unsigned)action < XVT_INPUT_ACTION_COUNT ? g_actions[action].label : "None";
}

XvtInputActionCategory XvtInputActions_Category(XvtInputAction action) {
	return (unsigned)action < XVT_INPUT_ACTION_COUNT ? g_actions[action].category
													 : XVT_INPUT_ACTION_CATEGORY_SYSTEM;
}

const char* XvtInputActions_CategoryName(XvtInputActionCategory category) {
	static const char* names[] = { "Weapons",     "Targeting", "Throttle",      "View",
								   "Information", "System",    "Communications" };
	return (unsigned)category < XVT_INPUT_ACTION_CATEGORY_COUNT ? names[category] : "";
}

uint16_t XvtInputActions_Key(XvtInputAction action) {
	return (unsigned)action < XVT_INPUT_ACTION_COUNT ? g_actions[action].key : 0;
}

bool XvtInputActions_KeyboardBindable(XvtInputAction action) {
	return action > XVT_INPUT_ACTION_NONE && action < XVT_INPUT_ACTION_COUNT &&
		   action != XVT_INPUT_ACTION_CHAT_SEND && action != XVT_INPUT_ACTION_CHAT_CANCEL;
}
