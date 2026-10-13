"""The planted faults of the bot's solo walk, options menus, right-click items and door.

Purpose:
    Break one rule of the walks (``walk``), the shared reply shapes
    (``reply``), the registry, and the door's right-click, walk, window and
    direct-message code per plant.

Flow:
    ``plants_bot`` joins this table with the others; ``plant_faults``
    applies each plant alone: write ``new`` over ``old`` in ``path``, run the
    suite, restore, and check that every test in ``expect`` failed. An
    ``expect`` entry matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``solo-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_solo_bot import SOLO_BOT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

WLK = "jedimaster/bot/walk.py"
RPL = "jedimaster/bot/reply.py"
REG = "jedimaster/bot/registry.py"
COR = "jedimaster/bot/core.py"
DOR = "jedimaster/bot/discord_door.py"
TW = "tests/test_bot_walk.py::"
TM = "tests/test_bot_menus.py::"
TD = "tests/test_bot_door.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``solo-<pid>``."""
    return Plant(f"solo-{pid}", path, old, new, expect)


GATES = ("solo", "pick_solo", "launch_solo", "options", "pick_option")

WALK_PLANTS: list[Plant] = [
    *[
        plant(
            f"walk-{name}-open",
            WLK,
            f'self._gate("{name}", user_id, in_dm, dm_only=False)',
            "None",
            TW + "test_another_account_is_refused_and_nothing_is_sent",
        )
        for name in GATES
    ],
    plant(
        "walk-open-to-all-missions",
        COR,
        'self._gate("missions", user_id, in_dm, dm_only=False)',
        "None",
        TW + "test_another_account_is_refused_and_nothing_is_sent",
    ),
    plant(
        "walk-star-words",
        WLK,
        '", opens once flown"',
        '""',
        TW + "test_the_missions_leave_out_ampersand",
    ),
    plant(
        "walk-sided-always-asks",
        WLK,
        '        if side_problem(value, "none") is None:',
        "        if False:",
        TW + "test_a_kind_without_a_side_goes_straight",
        TW + "test_a_sequence_is_picked_as_its_file",
    ),
    plant(
        "walk-one-side",
        WLK,
        'SIDES = ("rebel", "imperial")',
        'SIDES = ("rebel",)',
        TW + "test_a_kind_with_a_side_asks_for_the_side",
    ),
    plant(
        "walk-side-kind-unchecked",
        WLK,
        "        if len(parts) != 1 or parts[0] not in MISSION_TYPE_NAMES:",
        "        if False:",
        TW + "test_a_pick_that_does_not_fit_its_step",
    ),
    plant(
        "walk-side-fit-unchecked",
        WLK,
        "        if side_problem(parts[0], value) is not None:",
        "        if False:",
        TW + "test_a_pick_that_does_not_fit_its_step",
    ),
    plant(
        "walk-kind-unchecked",
        WLK,
        "        if parts or value not in MISSION_TYPE_NAMES:",
        "        if False:",
        TW + "test_a_pick_that_does_not_fit_its_step",
    ),
    plant(
        "walk-mission-unchecked",
        WLK,
        "        if entry is None:\n            return None\n",
        "        if False:\n            return None\n",
        TW + "test_a_mission_not_on_the_list_is_not_followed",
        TW + "test_a_mission_not_on_the_list_is_not_followed",
    ),
    plant(
        "walk-mission-key-fit-unchecked",
        WLK,
        "        if len(parts) != 2 or parts[0] not in MISSION_TYPE_NAMES:",
        "        if False:",
        TW + "test_a_pick_that_does_not_fit_its_step",
    ),
    plant(
        "walk-pilot-folder-unchecked",
        WLK,
        '        if not got["found"]:',
        "        if False:",
        TW + "test_an_unreadable_pilot_folder_says_so",
    ),
    plant(
        "walk-no-engine-unchecked",
        WLK,
        '        if not got["engine_given"]:',
        "        if False:",
        TW + "test_no_game_program_is_named_before_the_pilot_folder",
    ),
    plant(
        "walk-folder-before-engine",
        WLK,
        '        if not got["engine_given"]:\n            return Reply(NO_ENGINE)\n'
        '        if not got["found"]:\n            return Reply(NO_PILOT_FOLDER)\n',
        '        if not got["found"]:\n            return Reply(NO_PILOT_FOLDER)\n'
        '        if not got["engine_given"]:\n            return Reply(NO_ENGINE)\n',
        TW + "test_no_game_program_is_named_before_the_pilot_folder",
    ),
    plant(
        "walk-no-pilots-unchecked",
        WLK,
        '        if not got["pilots"]:',
        "        if False:",
        TW + "test_no_pilots_says_to_make_one",
    ),
    plant(
        "walk-pilot-key-short",
        WLK,
        'f"{kind}|{side}|{value}",\n            "Pilots",',
        'f"{kind}|{side}|",\n            "Pilots",',
        TW + "test_the_pilots_come_from_the_pilot_folder",
    ),
    plant(
        "walk-pilot-empty-ok",
        WLK,
        "or not value:",
        ":",
        TW + "test_a_pick_that_does_not_fit_its_step",
    ),
    plant(
        "walk-button-forgets-pilot",
        WLK,
        'f"{kind}|{side}|{mission}|{value}"',
        'f"{kind}|{side}|{mission}|Ace"',
        TW + "test_a_pilot_pick_offers_launch",
    ),
    plant(
        "walk-launch-player",
        WLK,
        '            "player": str(user_id),\n            "mission_type": kind,',
        '            "player": "1",\n            "mission_type": kind,',
        TW + "test_launch_asks_the_launcher",
    ),
    plant(
        "walk-launch-always-edits",
        WLK,
        'edit=bool(result["started"])',
        "edit=True",
        TW + "test_a_refused_launch_says_why",
    ),
    plant(
        "walk-launch-key-unchecked",
        WLK,
        "        if len(parts) != 4 or parts[0] not in MISSION_TYPE_NAMES:",
        "        if False:",
        TW + "test_a_launch_key_that_is_not_four_parts",
    ),
    plant(
        "walk-launch-reply-text",
        WLK,
        'result["message"], pushes=outcome.pushes',
        '"Started.", pushes=outcome.pushes',
        TW + "test_launch_asks_the_launcher",
        TW + "test_the_launcher_checks_what_the_bot_sent",
    ),
    plant(
        "walk-launch-pushes-lost",
        WLK,
        'result["message"], pushes=outcome.pushes',
        'result["message"], pushes=[]',
        TW + "test_launch_asks_the_launcher",
        TM + "test_the_whole_walk_edits_one_message",
    ),
    plant(
        "walk-options-first-not-marked",
        WLK,
        "Option(AS_GAME_WORDS, AS_GAME, spec.help, now is None)",
        "Option(AS_GAME_WORDS, AS_GAME, spec.help, False)",
        TW + "test_options_is_seven_menus",
    ),
    plant(
        "walk-options-current-unmarked",
        WLK,
        "Option(c.words, c.text, default=c.text == now)",
        "Option(c.words, c.text, default=False)",
        TW + "test_the_current_choice_is_marked",
    ),
    plant(
        "walk-options-four-then-three",
        WLK,
        "OPTIONS_PER_MESSAGE = 5",
        "OPTIONS_PER_MESSAGE = 4",
        TW + "test_options_is_seven_menus",
    ),
    plant(
        "walk-options-lines",
        WLK,
        'lines.append(f"{spec.words}: {shown}.")',
        'lines.append(f"{spec.words}.")',
        TW + "test_the_current_choice_is_marked",
    ),
    plant(
        "walk-options-as-game-sent",
        WLK,
        "sent = None if value == AS_GAME else value",
        "sent = value",
        TW + "test_as_the_game_has_it_removes_the_option",
    ),
    plant(
        "walk-options-name-unchecked",
        WLK,
        "        if name not in OPTIONS:\n            self._end",
        "        if False:\n            self._end",
        TW + "test_an_unknown_option_or_value_is_not_set",
    ),
    plant(
        "walk-options-left-words",
        WLK,
        'text = f"{spec.words} is left as the game has it."',
        'text = f"{spec.words} set."',
        TW + "test_as_the_game_has_it_removes_the_option",
    ),
    plant(
        "walk-options-set-words",
        WLK,
        'text = f"{spec.words} set to {words}."',
        'text = f"{spec.words} set."',
        TW + "test_a_pick_sets_the_option_for_that_player",
        TM + "test_options_arrive_as_two_messages",
    ),
    plant(
        "walk-options-push-lost",
        WLK,
        "return Reply(text, pushes=outcome.pushes)",
        "return Reply(text)",
        TW + "test_a_pick_sets_the_option_for_that_player",
        TM + "test_options_arrive_as_two_messages",
    ),
    plant(
        "walk-options-player",
        WLK,
        '"options.set", {"player": str(user_id), "name": name, "value": sent}',
        '"options.set", {"player": "1", "name": name, "value": sent}',
        TW + "test_a_pick_sets_the_option_for_that_player",
    ),
    plant(
        "walk-options-get-player",
        WLK,
        'self.send("options.get", {"player": str(user_id)})',
        'self.send("options.get", {"player": "1"})',
        TW + "test_the_current_choice_is_marked",
    ),
    plant(
        "walk-edit-not-set",
        WLK,
        "        reply.edit = True\n",
        "",
        TW + "test_a_kind_with_a_side_asks_for_the_side",
        TM + "test_the_whole_walk_edits_one_message",
    ),
    plant(
        "walk-too-long-list-silent",
        WLK,
        '        if left_out:\n            text += f" Showing the first {len(options) - left_out} of {len(options)}."\n        return Reply(text, pickers)',
        "        return Reply(text, pickers)",
        TW + "test_a_long_list_of_missions_says_how_many_are_shown",
    ),
    plant(
        "walk-list-missing-words",
        WLK,
        'return Reply(f"The game\'s list of {words} missions was not found.")',
        'return Reply("No list.")',
        TW + "test_a_list_the_install_lacks_says_so",
    ),
    plant(
        "walk-empty-list-silent",
        WLK,
        '        if not options:\n            return Reply(f"The list of {words} missions is empty.")\n',
        "",
        "tests/test_bot_walk.py::test_an_empty_list_says_so",
    ),
]

REPLY_PLANTS: list[Plant] = [
    plant(
        "reply-menu-24",
        RPL,
        "MENU_SIZE = 25",
        "MENU_SIZE = 24",
        TW + "test_menus_are_cut_at_25",
    ),
    plant(
        "reply-six-menus",
        RPL,
        "MENUS_PER_REPLY = 5",
        "MENUS_PER_REPLY = 6",
        TW + "test_menus_are_cut_at_25",
    ),
    plant(
        "reply-left-out-hidden",
        RPL,
        "max(0, len(options) - room)",
        "0",
        TW + "test_menus_are_cut_at_25",
        TW + "test_a_long_list_of_missions_says_how_many_are_shown",
    ),
    plant(
        "reply-placeholder",
        RPL,
        'f"{title} {start + 1} to {start + len(chunk)}"',
        'f"{title}"',
        TW + "test_menus_are_cut_at_25",
    ),
    plant(
        "reply-label-uncut",
        RPL,
        "LABEL_LIMIT = 100",
        "LABEL_LIMIT = 200",
        TW + "test_a_long_title_is_cut",
    ),
    plant(
        "reply-clip-no-dots",
        RPL,
        'text[: LABEL_LIMIT - 3] + "..."',
        "text[:LABEL_LIMIT]",
        TW + "test_a_long_title_is_cut",
    ),
]

CORE_PLANTS: list[Plant] = [
    plant(
        "core-kinds-skipped",
        COR,
        "        if mission_type is None:",
        "        if False:",
        TW + "test_show_mission_without_a_type",
        TM + "test_show_mission_by_right_click",
    ),
    plant(
        "core-notice-any-push",
        COR,
        '        if push.get("event") != "launch.ended":',
        "        if False:",
        TW + "test_a_battle_end_goes_to_the_linked_account_only",
    ),
    plant(
        "core-notice-unlinked",
        COR,
        '        if user is None:\n            logger.debug("a battle ended',
        '        if False:\n            logger.debug("a battle ended',
        TW + "test_a_battle_end_goes_to_the_linked_account_only",
    ),
    plant(
        "core-host-player-text",
        COR,
        "return None if user is None else str(user)",
        "return str(user)",
        TW + "test_the_launcher_asks_the_bot_for_the_linked_account",
    ),
    plant(
        "core-host-hook-missing",
        COR,
        "        control.host_player = self._host_player\n",
        "",
        TW + "test_the_launcher_asks_the_bot_for_the_linked_account",
    ),
]

REGISTRY_PLANTS: list[Plant] = [
    plant(
        "registry-options-dm-only",
        REG,
        '"Game options", "options", ANYWHERE,',
        '"Game options", "options", DM,',
        TM + "test_the_table_has_the_seven_rows",
    ),
    plant(
        "registry-args-order",
        REG,
        "return (user_id, value, in_dm)",
        "return (user_id, in_dm, value)",
        TM + "test_link_by_slash_and_by_the_window",
        TD + "test_missions_defers_first",
    ),
    plant(
        "registry-missions-no-defer",
        REG,
        "        defers=True,\n",
        "",
        TD + "test_missions_defers_first_then_follows_up_with_menus",
    ),
    plant(
        "registry-type-required",
        REG,
        "MISSION_TYPE_NAMES, required=False",
        "MISSION_TYPE_NAMES, required=True",
        TM + "test_missions_slash_takes_an_optional_choice",
        TM + "test_a_right_click_and_its_slash_twin",
    ),
    plant(
        "registry-title",
        REG,
        '"Show mission",\n        "missions"',
        '"Show missions",\n        "missions"',
        TM + "test_the_table_has_the_seven_rows",
    ),
    plant(
        "registry-limit-five",
        REG,
        "MAX_USER_COMMANDS = 15",
        "MAX_USER_COMMANDS = 5",
        TM + "test_the_table_has_the_seven_rows",
        TM + "test_the_tree_holds_more",
    ),
    plant(
        "registry-solo-method",
        REG,
        'Item("Single player", "solo", ANYWHERE, "solo"',
        'Item("Single player", "options", ANYWHERE, "solo"',
        TM + "test_the_table_has_the_seven_rows",
    ),
]

DOOR_PLANTS: list[Plant] = [
    plant(
        "door-click-logged-loud",
        DOR,
        'logger.debug("%s used on account %d: ignored", item.title, person.id)',
        'logger.info("%s used on account %d: ignored", item.title, person.id)',
        TM + "test_the_person_clicked_is_logged_at_debug_only",
    ),
    plant(
        "door-link-no-window",
        DOR,
        '            if item.method == "link":',
        "            if False:",
        TM + "test_link_by_slash_and_by_the_window",
        TM + "test_the_window_does_not_answer",
    ),
    plant(
        "door-contexts-guilds",
        DOR,
        "guild=item.where != DM, dm_channel=True, private_channel=False",
        "guild=True, dm_channel=True, private_channel=False",
        TM + "test_where_a_row_may_be_used",
    ),
    plant(
        "door-contexts-private",
        DOR,
        "dm_channel=True, private_channel=False",
        "dm_channel=True, private_channel=True",
        TM + "test_where_a_row_may_be_used",
    ),
    plant(
        "door-tree-limit-open",
        DOR,
        "        if total + 1 > MAX_USER_COMMANDS:",
        "        if total + 1 > 100:",
        TM + "test_the_sixteenth_user_command_is_refused",
    ),
    plant(
        "door-tree-limit-five",
        DOR,
        "        if total + 1 > MAX_USER_COMMANDS:",
        "        if total + 1 > 5:",
        TM + "test_the_tree_holds_more",
    ),
    plant(
        "door-no-edit",
        DOR,
        "        if reply.edit and done:\n            await interaction.edit_original_response(content=text, view=view)",
        "        if False:\n            await interaction.edit_original_response(content=text, view=view)",
        TM + "test_the_whole_walk_edits_one_message",
    ),
    plant(
        "door-then-dropped",
        DOR,
        "        for more in reply.then:",
        "        for more in ():",
        TM + "test_options_arrive_as_two_messages",
    ),
    plant(
        "door-solo-pick-no-defer",
        DOR,
        "            await interaction.response.defer()\n            reply = core.pick_solo",
        "            reply = core.pick_solo",
        TM + "test_the_whole_walk_edits_one_message",
    ),
    plant(
        "door-press-wrong-key",
        DOR,
        "reply = self.core.launch_solo(user, spec.key, dm)",
        "reply = self.core.launch_solo(user, spec.key[::-1], dm)",
        TM + "test_the_whole_walk_edits_one_message",
        TM + "test_launch_refused_leaves_the_button",
    ),
    plant(
        "door-press-no-defer",
        DOR,
        "        await interaction.response.defer()\n        user, dm = interaction.user.id, in_dm(interaction)\n        reply = self.core.launch_solo",
        "        user, dm = interaction.user.id, in_dm(interaction)\n        reply = self.core.launch_solo",
        TM + "test_the_whole_walk_edits_one_message",
    ),
    plant(
        "door-dm-wrong-account",
        DOR,
        "person = await self.client.fetch_user(user_id)",
        "person = await self.client.fetch_user(user_id + 1)",
        TM + "test_the_end_goes_to_the_linked_account",
    ),
    plant(
        "door-dm-wrong-text",
        DOR,
        "await person.send(line)",
        'await person.send("done")',
        TM + "test_the_end_goes_to_the_linked_account",
        TM + "test_a_launch_from_a_server_channel",
    ),
    plant(
        "door-dm-crash",
        DOR,
        "        except Exception as exc:  # whatever Discord answers, the bot keeps running",
        "        except ZeroDivisionError as exc:",
        TM + "test_a_refused_message_is_a_warning",
    ),
    plant(
        "door-dm-not-subscribed",
        DOR,
        "        core.control.subscribe(self.on_push)",
        "        pass",
        TM + "test_the_end_reaches_the_door",
        TM + "test_a_launch_from_a_server_channel",
    ),
    plant(
        "door-dm-leaks-reason",
        DOR,
        "                type(exc).__name__,\n            )\n            return",
        "                exc,\n            )\n            return",
        TM + "test_a_refused_message_is_a_warning",
    ),
    plant(
        "door-window-code-lost",
        DOR,
        "await self.door.act(self.item, interaction, self.code.value)",
        "await self.door.act(self.item, interaction, None)",
        TM + "test_link_by_slash_and_by_the_window",
    ),
    plant(
        "door-option-pick-as-setting",
        DOR,
        "            reply = core.pick_option(user, picker.key, value, dm)",
        "            reply = core.pick_setting(user, picker.key, value, dm)",
        TM + "test_options_arrive_as_two_messages",
    ),
    plant(
        "door-kind-pick-as-mission",
        DOR,
        "                reply = core.missions(user, value, dm)",
        "                reply = core.pick_mission(user, picker.key, value, dm)",
        TM + "test_show_mission_by_right_click",
    ),
    plant(
        "door-slash-choices-upper",
        DOR,
        "app_commands.Choice(name=c, value=c) for c in param.choices",
        "app_commands.Choice(name=c, value=c.upper()) for c in param.choices",
        TD + "test_missions_takes_a_type_chosen_from_the_six",
    ),
    plant(
        "door-install-user",
        DOR,
        "HOST_INSTALL = app_commands.AppInstallationType(guild=True, user=False)",
        "HOST_INSTALL = app_commands.AppInstallationType(guild=True, user=True)",
        TM + "test_both_kinds_are_global",
    ),
    plant(
        "door-right-click-is-slash",
        DOR,
        "            type=discord.AppCommandType.user,",
        "            type=discord.AppCommandType.message,",
        TM + "test_the_tree_holds_more",
        TM + "test_a_right_click_item_takes_no_options",
    ),
    plant(
        "door-click-acts-for-person",
        DOR,
        "            await door.act(item, interaction, None)\n\n        return app_commands.ContextMenu",
        "            interaction.user = person\n            await door.act(item, interaction, None)\n\n        return app_commands.ContextMenu",
        TM + "test_a_right_click_and_its_slash_twin",
    ),
]

SOLO_BOT_PLANTS: list[Plant] = [
    *WALK_PLANTS,
    *REPLY_PLANTS,
    *CORE_PLANTS,
    *REGISTRY_PLANTS,
    *DOOR_PLANTS,
]
