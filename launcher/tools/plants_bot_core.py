"""The planted faults of the bot's decisions and its Discord door.

Purpose:
    Break one rule of ``jedimaster/bot/core.py`` or ``discord_door.py`` per
    plant: who is answered, what each command sends and says, how menus are
    cut, and how the door answers, defers, syncs and logs in.

Flow:
    ``plants_bot`` joins this table with the others; ``plant_faults`` applies
    each plant alone: write ``new`` over ``old`` in ``path``, run the suite,
    restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``bot-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_bot_core import CORE_PLANTS, DOOR_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

LNK = "jedimaster/bot/link.py"
COR = "jedimaster/bot/core.py"
DOR = "jedimaster/bot/discord_door.py"
TK = "tests/test_bot_core.py::"
TD = "tests/test_bot_door.py::"
TC = "tests/test_bot_cli.py::"
TT = "tests/test_bot_token.py::"
TP = "tests/test_bot_page.py::"

CORE_PLANTS: list[Plant] = [
    Plant(
        "bot-core-gate-open",
        COR,
        "        if self.linker.linked_user_id != user_id:",
        "        if False:",
        (TK + "test_an_unlinked_account_is_refused_on_each_command",),
    ),
    Plant(
        "bot-core-gate-any-linked-account",
        COR,
        "        if self.linker.linked_user_id != user_id:",
        "        if self.linker.linked_user_id is None:",
        (TK + "test_an_unlinked_account_is_refused_on_each_command",),
    ),
    Plant(
        "bot-core-gate-open-before-linking",
        COR,
        "        if self.linker.linked_user_id != user_id:",
        "        if self.linker.linked_user_id not in (None, user_id):",
        (TK + "test_nothing_is_answered_before_anyone_is_linked",),
    ),
    Plant(
        "bot-core-gate-wrong-text",
        COR,
        'HOST_ONLY = "This launcher answers its host only."',
        'HOST_ONLY = "Access denied.\\nTry again."',
        (TK + "test_the_refusal_is_one_plain_line",),
    ),
    Plant(
        "bot-core-dm-unchecked",
        COR,
        "        if dm_only and not in_dm:",
        "        if False:",
        (TK + "test_direct_message_commands_refuse_a_server",),
    ),
    Plant(
        "bot-core-link-in-a-server",
        COR,
        '        if not in_dm:\n            return self._refused("link", user_id, "not a direct message", DM_ONLY)',
        '        if False:\n            return self._refused("link", user_id, "not a direct message", DM_ONLY)',
        (TK + "test_a_code_sent_in_a_server_is_not_spent",),
    ),
    Plant(
        "bot-core-refusal-silent",
        COR,
        '        logger.warning("refused %s from account %d: %s", command, user_id, reason)',
        '        logger.debug("refused %s from account %d: %s", command, user_id, reason)',
        (TK + "test_an_unlinked_account_is_refused_on_each_command",),
    ),
    Plant(
        "bot-core-status-no-install",
        COR,
        '        install = self._result(self.send("install.status", {}))',
        '        install = {"found": False}',
        (TK + "test_status_sends_hello_and_install_status",),
    ),
    Plant(
        "bot-core-status-no-hello",
        COR,
        '        hello = self._result(self.send("hello", {"page_version": BOT_VERSION_NAME}))',
        '        hello = {"launcher_version": "0"}',
        (TK + "test_status_sends_hello_and_install_status",),
    ),
    Plant(
        "bot-core-balance-always-found",
        COR,
        '            word = "found" if install["balance_of_power"] else "not found"',
        '            word = "found"',
        (TK + "test_status_without_balance_of_power",),
    ),
    Plant(
        "bot-core-unknown-type-runs",
        COR,
        '        if mission_type not in MISSION_TYPE_NAMES:\n            self._end("missions"',
        '        if False:\n            self._end("missions"',
        (TK + "test_missions_for_an_unknown_type_says_which_exist",),
    ),
    Plant(
        "bot-core-menu-of-thirty",
        COR,
        "MENU_SIZE = 25",
        "MENU_SIZE = 30",
        (TK + "test_a_menu_is_25_entries_and_at_most_5_menus",),
    ),
    Plant(
        "bot-core-six-menus",
        COR,
        "MENUS_PER_REPLY = 5",
        "MENUS_PER_REPLY = 6",
        (TK + "test_a_menu_is_25_entries_and_at_most_5_menus",),
    ),
    Plant(
        "bot-core-cut-not-said",
        COR,
        "        if len(options) > room:",
        "        if False:",
        (TK + "test_a_menu_is_25_entries_and_at_most_5_menus",),
    ),
    Plant(
        "bot-core-cut-always-said",
        COR,
        "        if len(options) > room:",
        "        if len(options) >= room:",
        (TK + "test_exactly_125_entries_say_nothing_about_cutting",),
    ),
    Plant(
        "bot-core-repeated-ids-offered",
        COR,
        '            if entry["id"] in seen:\n                continue',
        "            if False:\n                continue",
        (TK + "test_a_repeated_id_is_offered_once_and_the_first_entry_wins",),
    ),
    Plant(
        "bot-core-labels-uncut",
        COR,
        "LABEL_LIMIT = 100",
        "LABEL_LIMIT = 400",
        (TK + "test_long_titles_fit_discords_limits",),
    ),
    Plant(
        "bot-core-section-dropped",
        COR,
        '            place = f"{entry[\'section\']}: " if entry["section"] else ""',
        '            place = ""',
        (TK + "test_missions_sends_missions_list_and_offers_that_menu",),
    ),
    Plant(
        "bot-core-unavailable-unsaid",
        COR,
        '            word = "" if entry["available"] else ", not available"',
        '            word = ""',
        (TK + "test_missions_sends_missions_list_and_offers_that_menu",),
    ),
    Plant(
        "bot-core-empty-menu-unsaid",
        COR,
        "        if not options:\n",
        "        if False:\n",
        (TK + "test_an_empty_menu_says_so",),
    ),
    Plant(
        "bot-core-unresolved-menu-offered",
        COR,
        '        if not menu["resolved"]:',
        "        if False:",
        (TK + "test_missions_for_a_type_the_install_lacks_says_so",),
    ),
    Plant(
        "bot-core-pick-sends-the-wrong-command",
        COR,
        '        outcome = self.send("page.show_mission", args)',
        '        outcome = self.send("missions.list", {})',
        (TK + "test_picking_a_mission_sends_page_show_mission",),
    ),
    Plant(
        "bot-core-pick-id-any-digit",
        COR,
        "mission_id.isascii() and mission_id.isdigit()",
        "mission_id.isdigit()",
        (TK + "test_a_pick_that_is_not_a_mission_sends_nothing",),
    ),
    Plant(
        "bot-core-pick-type-unchecked",
        COR,
        "        if mission_type not in MISSION_TYPE_NAMES or not (",
        "        if not (",
        (TK + "test_a_pick_that_is_not_a_mission_sends_nothing",),
    ),
    Plant(
        "bot-core-pick-always-shown",
        COR,
        '        if not result["shown"]:',
        "        if False:",
        (TK + "test_picking_a_mission_that_is_not_listed_pushes_nothing",),
    ),
    Plant(
        "bot-core-pick-drops-the-push",
        COR,
        '        return Reply(f"Showing {title} on the page.", pushes=outcome.pushes)',
        '        return Reply(f"Showing {title} on the page.")',
        (
            TK + "test_picking_a_mission_sends_page_show_mission",
            TP + "test_a_mission_picked_in_discord_reaches_an_open_page",
        ),
    ),
    Plant(
        "bot-core-no-page-note-missing",
        COR,
        "        if reply.pushes and pages == 0:",
        "        if False:",
        (TK + "test_when_no_page_is_open_the_reply_says_so",),
    ),
    Plant(
        "bot-core-no-page-note-always",
        COR,
        "        if reply.pushes and pages == 0:",
        "        if pages == 0:",
        (TK + "test_when_no_page_is_open_the_reply_says_so",),
    ),
    Plant(
        "bot-core-current-setting-unmarked",
        COR,
        "                Option(label, value, sentence, default=value == now)",
        "                Option(label, value, sentence, default=False)",
        (TK + "test_settings_sends_settings_get_and_offers_each_setting",),
    ),
    Plant(
        "bot-core-setting-words-swapped",
        COR,
        '            "engine_fit": (\n                "Engine fit",',
        '            "engine_fit": (\n                "Fit to engine",',
        (TK + "test_settings_sends_settings_get_and_offers_each_setting",),
    ),
    Plant(
        "bot-core-setting-pick-drops-the-push",
        COR,
        '        return Reply(f"{words} set to {values[value][0]}.", pushes=outcome.pushes)',
        '        return Reply(f"{words} set to {values[value][0]}.")',
        (
            TK + "test_picking_a_setting_sends_settings_set_and_pushes_the_change",
            TP + "test_a_setting_changed_by_the_bot_reaches_an_open_page",
        ),
    ),
    Plant(
        "bot-core-setting-pick-ignores-errors",
        COR,
        '        if self._result(outcome) is None:\n            self._end("pick_setting"',
        '        if False:\n            self._end("pick_setting"',
        (TK + "test_picking_a_setting_off_the_list_changes_nothing",),
    ),
    Plant(
        "bot-core-unlink-does-nothing",
        COR,
        "        self.linker.unlink()\n",
        "",
        (TK + "test_unlink_removes_the_link_and_sends_no_request",),
    ),
    Plant(
        "bot-core-link-reply-holds-the-code",
        COR,
        "        return Reply(LINK_WORDS[outcome])",
        '        return Reply(LINK_WORDS[outcome] + " " + code)',
        (TK + "test_link_answers_each_outcome",),
    ),
    Plant(
        "bot-core-link-expired-reads-void",
        COR,
        '    linking.EXPIRED: (\n        "That code has expired',
        '    linking.EXPIRED: (\n        "That code is void',
        (TK + "test_link_answers_linked_expired_and_void",),
    ),
    Plant(
        "bot-core-ids-stuck",
        COR,
        "        self._next_id = 1 if ident >= MAX_ID else ident + 1",
        "        self._next_id = ident",
        (TK + "test_requests_are_json_text_with_ids_from_a_counter",),
    ),
    Plant(
        "bot-core-ids-never-wrap",
        COR,
        "        self._next_id = 1 if ident >= MAX_ID else ident + 1",
        "        self._next_id = ident + 1",
        (TK + "test_the_counter_starts_again_after_the_largest_id",),
    ),
    Plant(
        "bot-core-request-command-fixed",
        COR,
        '        text = json.dumps({"id": ident, "command": command, "args": args})',
        '        text = json.dumps({"id": ident, "command": "settings.get", "args": args})',
        (TK + "test_a_command_off_the_list_is_refused_by_the_request_path",),
    ),
    Plant(
        "bot-core-request-skips-control",
        COR,
        "        return self.control.handle(text)",
        '        return Outcome({"id": ident, "ok": True, "result": {}})',
        (TK + "test_a_command_off_the_list_is_refused_by_the_request_path",),
    ),
    Plant(
        "bot-core-ids-not-logged",
        COR,
        "            self._ids,\n            outcome,",
        "            [],\n            outcome,",
        (TK + "test_each_command_is_logged_at_debug_with_asker_requests_and_outcome",),
    ),
    Plant(
        "bot-core-asker-not-logged",
        COR,
        '            "command %s from account %d: requests %s, %s",\n            command,\n            user_id,',
        '            "command %s from account %s: requests %s, %s",\n            command,\n            "?",',
        (TK + "test_each_command_is_logged_at_debug_with_asker_requests_and_outcome",),
    ),
    Plant(
        "bot-core-imports-the-door",
        COR,
        "import json\nimport logging\n",
        "import json\nimport logging\n\nimport discord  # noqa: F401\n",
        (TK + "test_core_imports_with_discord_blocked_and_without_the_web_server",),
    ),
    Plant(
        "bot-core-imports-the-server",
        COR,
        "from ..page.control import Control\n",
        "from ..page.control import Control\nfrom ..page import server  # noqa: F401\n",
        (TK + "test_core_imports_with_discord_blocked_and_without_the_web_server",),
    ),
    Plant(
        "bot-link-imports-discord",
        LNK,
        "import hmac\n",
        "import discord  # noqa: F401\nimport hmac\n",
        (TK + "test_only_the_door_names_discord",),
    ),
]

DOOR_PLANTS: list[Plant] = [
    Plant(
        "bot-door-all-intents",
        DOR,
        "super().__init__(intents=discord.Intents(guilds=True))",
        "super().__init__(intents=discord.Intents.all())",
        (TD + "test_the_door_asks_for_the_guilds_intent_only",),
    ),
    Plant(
        "bot-door-public-answers",
        DOR,
        'options: dict[str, Any] = {"ephemeral": True}',
        "options: dict[str, Any] = {}",
        (TD + "test_status_answers_at_once_and_privately",),
    ),
    Plant(
        "bot-door-follow-up-ignored",
        DOR,
        "        if interaction.response.is_done():",
        "        if False:",
        (TD + "test_missions_defers_first_then_follows_up_with_menus",),
    ),
    Plant(
        "bot-door-missions-no-defer",
        DOR,
        "            await interaction.response.defer(ephemeral=True)\n            reply = core.missions(",
        "            reply = core.missions(",
        (TD + "test_missions_defers_first_then_follows_up_with_menus",),
    ),
    Plant(
        "bot-door-pick-no-defer",
        DOR,
        "            await interaction.response.defer(ephemeral=True)\n            reply = self.core.pick_mission(",
        "            reply = self.core.pick_mission(",
        (TD + "test_a_picked_mission_defers_shows_it_and_reaches_the_pages",),
    ),
    Plant(
        "bot-door-status-defers",
        DOR,
        "        async def status(interaction: discord.Interaction) -> None:\n",
        "        async def status(interaction: discord.Interaction) -> None:\n            await interaction.response.defer(ephemeral=True)\n",
        (TD + "test_status_answers_at_once_and_privately",),
    ),
    Plant(
        "bot-door-dm-commands-in-servers",
        DOR,
        "        dm_only = app_commands.allowed_contexts(\n            guilds=False, dms=True, private_channels=False\n        )",
        "        dm_only = app_commands.allowed_contexts(\n            guilds=True, dms=True, private_channels=False\n        )",
        (TD + "test_direct_message_commands_are_dm_only_and_the_others_anywhere",),
    ),
    Plant(
        "bot-door-status-dm-only",
        DOR,
        "        anywhere = app_commands.allowed_contexts(\n            guilds=True, dms=True, private_channels=False\n        )",
        "        anywhere = app_commands.allowed_contexts(\n            guilds=False, dms=True, private_channels=False\n        )",
        (TD + "test_direct_message_commands_are_dm_only_and_the_others_anywhere",),
    ),
    Plant(
        "bot-door-user-install",
        DOR,
        "allowed_installs(guilds=True, users=False)",
        "allowed_installs(guilds=True, users=True)",
        (TD + "test_the_commands_are_global_and_for_the_host_s_server_install",),
    ),
    Plant(
        "bot-door-missions-type-free",
        DOR,
        "                app_commands.Choice(name=name, value=name)",
        "                app_commands.Choice(name=name, value=name.upper())",
        (TD + "test_missions_takes_a_type_chosen_from_the_six",),
    ),
    Plant(
        "bot-door-always-syncs",
        DOR,
        "        if digest == self.store.commands_digest:",
        "        if False:",
        (TD + "test_the_list_syncs_when_new_and_not_again_while_unchanged",),
    ),
    Plant(
        "bot-door-never-syncs",
        DOR,
        "        if digest == self.store.commands_digest:",
        "        if True:",
        (TD + "test_the_list_syncs_when_new_and_not_again_while_unchanged",),
    ),
    Plant(
        "bot-door-digest-not-kept",
        DOR,
        "        self.store.commands_digest = digest\n        self.store.save()\n",
        "        self.store.commands_digest = digest\n",
        (TD + "test_the_digest_is_kept_between_runs",),
    ),
    Plant(
        "bot-door-digest-ignores-the-list",
        DOR,
        "    text = json.dumps(payload, sort_keys=True)",
        "    text = json.dumps(len(payload), sort_keys=True)",
        (TD + "test_the_digest_follows_the_payload",),
    ),
    Plant(
        "bot-door-refused-token-unhandled",
        DOR,
        "        except discord.LoginFailure:",
        "        except KeyError:",
        (TD + "test_a_refused_token_logs_an_error_without_it_and_returns",),
    ),
    Plant(
        "bot-door-refused-token-logged",
        DOR,
        '            logger.error("Discord refused the bot\'s token; the bot is stopped")',
        '            logger.error("Discord refused the token %s", token.reveal())',
        (
            TD + "test_a_refused_token_logs_an_error_without_it_and_returns",
            TT + "test_the_token_is_in_no_log_record_through_a_whole_run",
        ),
    ),
    Plant(
        "bot-door-error-message-logged",
        DOR,
        '            logger.error("the bot stopped: %s", type(exc).__name__)',
        '            logger.error("the bot stopped: %s", exc)',
        (TD + "test_any_other_discord_error_stops_the_bot_and_names_only_its_type",),
    ),
    Plant(
        "bot-door-unreachable-unhandled",
        DOR,
        "        except (aiohttp.ClientError, OSError) as exc:",
        "        except KeyError as exc:",
        (TD + "test_an_unreachable_discord_stops_the_bot_with_an_error",),
    ),
    Plant(
        "bot-door-other-errors-unhandled",
        DOR,
        "        except discord.DiscordException as exc:",
        "        except KeyError as exc:",
        (TD + "test_any_other_discord_error_stops_the_bot_and_names_only_its_type",),
    ),
    Plant(
        "bot-door-empty-pick-runs",
        DOR,
        "        if not values:\n            return\n",
        "",
        (TD + "test_an_empty_pick_does_nothing",),
    ),
    Plant(
        "bot-door-pushes-not-delivered",
        DOR,
        "        pages = await self.deliver(reply.pushes) if reply.pushes else 0",
        "        pages = 1",
        (
            TD + "test_a_picked_setting_changes_it_and_reaches_the_pages",
            TP + "test_a_setting_changed_by_the_bot_reaches_an_open_page",
        ),
    ),
    Plant(
        "bot-door-delivered-without-pushes",
        DOR,
        "        pages = await self.deliver(reply.pushes) if reply.pushes else 0",
        "        pages = await self.deliver(reply.pushes)",
        (TD + "test_status_answers_at_once_and_privately",),
    ),
    Plant(
        "bot-door-server-counts-as-dm",
        DOR,
        "    return interaction.guild is None",
        "    return True",
        (TD + "test_settings_in_a_server_is_refused",),
    ),
    Plant(
        "bot-door-menu-lifetime",
        DOR,
        "MENU_LIFETIME_SECONDS = 300",
        "MENU_LIFETIME_SECONDS = 1",
        (TD + "test_a_menu_is_built_with_a_limited_life",),
    ),
    Plant(
        "bot-door-default-dropped",
        DOR,
        "                        default=o.default,",
        "                        default=False,",
        (TD + "test_settings_answers_at_once_with_a_menu_per_setting",),
    ),
    Plant(
        "bot-door-description-dropped",
        DOR,
        "                        description=o.description or None,",
        "                        description=None,",
        (TD + "test_missions_defers_first_then_follows_up_with_menus",),
    ),
    Plant(
        "bot-door-menus-not-shown",
        DOR,
        '            options["view"] = self._view(reply.pickers)',
        "            pass",
        (TD + "test_missions_defers_first_then_follows_up_with_menus",),
    ),
    Plant(
        "bot-door-pick-glue-loses-the-choice",
        DOR,
        "            await self.picked(interaction, picker, list(select.values))",
        "            await self.picked(interaction, picker, [])",
        (TD + "test_a_picked_setting_changes_it_and_reaches_the_pages",),
    ),
    Plant(
        "bot-door-pick-glue-reads-the-first-menu",
        DOR,
        "            select.callback = self._pick_callback(select, picker)",
        "            select.callback = self._pick_callback(view.children[0] if view.children else select, picker)",
        (TD + "test_a_pick_from_the_second_menu_uses_that_menus_choice",),
    ),
    Plant(
        "bot-door-missing-command",
        DOR,
        '        @tree.command(name="unlink", description="Unlink this launcher")',
        '        @tree.command(name="unlinked", description="Unlink this launcher")',
        (TD + "test_the_commands_are_the_five_in_the_table",),
    ),
    Plant(
        "bot-door-ready-unsaid",
        DOR,
        '        logger.info("bot started as %s", self.user)',
        "        pass",
        (TD + "test_the_bot_says_it_started_with_its_name",),
    ),
    Plant(
        "bot-door-sync-unsaid",
        DOR,
        '        logger.info("synced %d commands", len(sent))',
        "        pass",
        (TD + "test_the_list_syncs_when_new_and_not_again_while_unchanged",),
    ),
]
