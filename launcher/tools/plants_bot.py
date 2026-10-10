"""The planted faults of the bot: link, token, command line, and the whole table.

Purpose:
    Break one rule of ``jedimaster/bot/link.py``, ``credentials.py`` or
    ``cli.py`` (and the two seams the bot uses in ``page/``) per plant: the
    link code's limits, the token's file and its hiding, and how ``page --bot``
    starts. ``BOT_PLANTS`` joins the decisions and the door
    (``plants_bot_core``).

Flow:
    ``plant_faults`` joins this table with the others and applies each plant
    alone: write ``new`` over ``old`` in ``path``, run the suite, restore,
    and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``bot-``.
    - Each ``old`` text occurs exactly once in its file.
    - No plant sends a write to a path outside the test's own folder.

Call:
    ``from plants_bot import BOT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_bot_core import CORE_PLANTS
from plants_bot_core import DOOR_PLANTS
from plants_common import Plant

logger = logging.getLogger(__name__)

LNK = "jedimaster/bot/link.py"
CRD = "jedimaster/bot/credentials.py"
BCL = "jedimaster/bot/cli.py"
PCL = "jedimaster/page/cli.py"
SRV = "jedimaster/page/server.py"
TL = "tests/test_bot_link.py::"
TR = "tests/test_bot_credentials.py::"
TC = "tests/test_bot_cli.py::"
TT = "tests/test_bot_token.py::"
TP = "tests/test_bot_page.py::"
TS = "tests/test_page_server.py::"

LINK_PLANTS: list[Plant] = [
    Plant(
        "bot-link-codes-constant",
        LNK,
        '        self._code = "".join(self.draw(ALPHABET) for _ in range(CODE_LENGTH))',
        "        self._code = ALPHABET[:CODE_LENGTH]",
        (
            TL + "test_two_codes_differ",
            TL + "test_a_code_is_drawn_from_the_secrets_module",
        ),
    ),
    Plant(
        "bot-link-any-code-links",
        LNK,
        '        if not hmac.compare_digest(given, self._code.encode("utf-8")):',
        "        if False:",
        (TL + "test_a_wrong_code_is_refused_and_nothing_is_linked",),
    ),
    Plant(
        "bot-link-unlink-always-saves",
        LNK,
        "        was = self.store.linked_user_id is not None\n        if was:\n",
        "        was = self.store.linked_user_id is not None\n        self.store.save()\n        if was:\n",
        (TL + "test_unlink_with_nothing_linked_writes_no_file",),
    ),
    Plant(
        "bot-link-json-error-uncaught",
        LNK,
        "        except (OSError, ValueError) as exc:",
        "        except OSError as exc:",
        (TL + "test_a_broken_bot_file_links_nobody[text]",),
    ),
    Plant(
        "bot-link-list-file-read",
        LNK,
        "        if not isinstance(doc, dict):",
        "        if False:",
        (TL + "test_a_broken_bot_file_links_nobody[list]",),
    ),
    Plant(
        "bot-link-any-account-id",
        LNK,
        "isinstance(user, int) and not isinstance(user, bool) and user > 0",
        "bool(user)",
        (TL + "test_a_broken_bot_file_links_nobody[string-id]",),
    ),
    Plant(
        "bot-link-alphabet-ambiguous",
        LNK,
        'ALPHABET = "23456789ABCDEFGHJKMNPQRSTVWXYZ"',
        'ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"',
        (TL + "test_the_alphabet_has_thirty_unambiguous_characters",),
    ),
    Plant(
        "bot-link-code-length",
        LNK,
        "CODE_LENGTH = 8",
        "CODE_LENGTH = 6",
        (TL + "test_a_code_is_eight_characters_shown_as_two_groups_of_four",),
    ),
    Plant(
        "bot-link-lifetime-long",
        LNK,
        "LIFETIME_SECONDS = 600.0",
        "LIFETIME_SECONDS = 6000.0",
        (
            TL + "test_a_code_expires_at_ten_minutes",
            TL + "test_the_lifetime_is_ten_minutes",
        ),
    ),
    Plant(
        "bot-link-expiry-late",
        LNK,
        "if self.clock() - self._made_at >= LIFETIME_SECONDS:",
        "if self.clock() - self._made_at > LIFETIME_SECONDS:",
        (TL + "test_a_code_expires_at_ten_minutes",),
    ),
    Plant(
        "bot-link-expiry-early",
        LNK,
        "if self.clock() - self._made_at >= LIFETIME_SECONDS:",
        "if self.clock() - self._made_at >= LIFETIME_SECONDS - 2:",
        (TL + "test_a_code_is_good_for_just_under_ten_minutes",),
    ),
    Plant(
        "bot-link-expiry-never",
        LNK,
        "if self.clock() - self._made_at >= LIFETIME_SECONDS:",
        "if False:",
        (
            TL + "test_a_code_expires_at_ten_minutes",
            TL + "test_expiry_and_void_are_logged",
        ),
    ),
    Plant(
        "bot-link-six-wrong-tries",
        LNK,
        "MAX_WRONG = 5",
        "MAX_WRONG = 6",
        (TL + "test_the_fifth_wrong_try_voids_the_code",),
    ),
    Plant(
        "bot-link-four-wrong-tries",
        LNK,
        "MAX_WRONG = 5",
        "MAX_WRONG = 4",
        (TL + "test_four_wrong_tries_leave_the_right_code_good",),
    ),
    Plant(
        "bot-link-void-forgotten",
        LNK,
        "                self._voided = True\n",
        "                self._voided = False\n",
        (TL + "test_the_fifth_wrong_try_voids_the_code",),
    ),
    Plant(
        "bot-link-wrong-tries-per-account",
        LNK,
        "            self._wrong += 1\n",
        "            self._wrong = 1\n",
        (TL + "test_wrong_tries_count_across_accounts",),
    ),
    Plant(
        "bot-link-code-reusable",
        LNK,
        "        self._code = None\n        self.store.linked_user_id = user_id\n",
        "        self.store.linked_user_id = user_id\n",
        (TL + "test_a_used_code_does_not_link_again_after_an_unlink",),
    ),
    Plant(
        "bot-link-compare-plain",
        LNK,
        'if not hmac.compare_digest(given, self._code.encode("utf-8")):',
        'if given != self._code.encode("utf-8"):',
        (TL + "test_the_code_is_compared_in_constant_time",),
    ),
    Plant(
        "bot-link-case-matters",
        LNK,
        '    return "".join(text.replace("-", "").split()).upper()',
        '    return "".join(text.replace("-", "").split())',
        (TL + "test_lower_case_padding_and_a_missing_dash_are_accepted",),
    ),
    Plant(
        "bot-link-dash-matters",
        LNK,
        '    return "".join(text.replace("-", "").split()).upper()',
        '    return "".join(text.split()).upper()',
        (TL + "test_the_right_code_links_and_is_saved",),
    ),
    Plant(
        "bot-link-space-matters",
        LNK,
        '    return "".join(text.replace("-", "").split()).upper()',
        '    return text.replace("-", "").upper()',
        (TL + "test_lower_case_padding_and_a_missing_dash_are_accepted",),
    ),
    Plant(
        "bot-link-second-account-links",
        LNK,
        '            logger.warning("refused a link attempt: already linked")\n            return ALREADY_LINKED',
        '            logger.warning("refused a link attempt: already linked")\n            return LINKED',
        (TL + "test_a_code_works_once",),
    ),
    Plant(
        "bot-link-code-logged",
        LNK,
        '"a link code was made; it is good for %d minutes", LIFETIME_SECONDS // 60',
        '"a link code %s was made", self._code',
        (TL + "test_the_code_is_never_logged",),
    ),
    Plant(
        "bot-link-not-saved",
        LNK,
        "        self.store.linked_user_id = user_id\n        self.store.save()\n",
        "        self.store.linked_user_id = user_id\n",
        (TL + "test_the_right_code_links_and_is_saved",),
    ),
    Plant(
        "bot-link-unlink-keeps-the-account",
        LNK,
        "            self.store.linked_user_id = None\n            self.store.save()\n",
        "            self.store.save()\n",
        (TL + "test_unlink_removes_the_link_and_reports_whether_there_was_one",),
    ),
    Plant(
        "bot-link-start-when-linked",
        LNK,
        "        if self.store.linked_user_id is not None:\n            return None\n",
        "",
        (TL + "test_start_makes_nothing_once_linked",),
    ),
    Plant(
        "bot-link-new-code-keeps-void",
        LNK,
        "        self._voided = False\n        logger.info(",
        "        logger.info(",
        (TL + "test_a_new_code_clears_the_void",),
    ),
    Plant(
        "bot-link-new-code-keeps-count",
        LNK,
        "        self._wrong = 0\n        self._voided = False\n        logger.info(",
        "        self._voided = False\n        logger.info(",
        (TL + "test_a_new_code_clears_the_void",),
    ),
    Plant(
        "bot-link-no-code-links",
        LNK,
        '            logger.warning("refused a link attempt: no code in play")\n            return NO_CODE',
        '            logger.warning("refused a link attempt: no code in play")\n            return LINKED',
        (TL + "test_an_attempt_without_a_code_says_so",),
    ),
    Plant(
        "bot-link-file-elsewhere",
        LNK,
        "    return settings_path.parent / FILE_NAME",
        "    return settings_path.parent.parent / FILE_NAME",
        (TL + "test_bot_json_sits_beside_the_settings_file",),
    ),
    Plant(
        "bot-link-digest-not-read",
        LNK,
        "            self.commands_digest = digest\n",
        "            self.commands_digest = None\n",
        (TL + "test_the_digest_is_kept_beside_the_account",),
    ),
    Plant(
        "bot-link-bool-is-an-account",
        LNK,
        "isinstance(user, int) and not isinstance(user, bool) and user > 0",
        "isinstance(user, int) and user > 0",
        (TL + "test_a_broken_bot_file_links_nobody[bool-id]",),
    ),
    Plant(
        "bot-link-negative-account",
        LNK,
        "isinstance(user, int) and not isinstance(user, bool) and user > 0",
        "isinstance(user, int) and not isinstance(user, bool)",
        (TL + "test_a_broken_bot_file_links_nobody[negative-id]",),
    ),
    Plant(
        "bot-link-write-failure-raises",
        LNK,
        '        except OSError as exc:\n            logger.warning("cannot write the bot file %s: %s", self.path, exc)\n            return False',
        '        except OSError as exc:\n            logger.warning("cannot write the bot file %s: %s", self.path, exc)\n            return True',
        (TL + "test_a_bot_file_that_cannot_be_written_is_a_warning",),
    ),
]

TOKEN_PLANTS: list[Plant] = [
    Plant(
        "bot-token-good-one-refused",
        CRD,
        '        return "the token has spaces in it"\n    return None',
        '        return "the token has spaces in it"\n    return "no good"',
        (TR + "test_a_good_token_passes_the_check",),
    ),
    Plant(
        "bot-token-missing-file-reads",
        CRD,
        '        logger.debug("no token file at %s", path)\n        return None',
        '        logger.debug("no token file at %s", path)\n        return Token("")',
        (TR + "test_reading_a_missing_file_gives_none",),
    ),
    Plant(
        "bot-token-file-loose",
        CRD,
        "FILE_MODE = 0o600",
        "FILE_MODE = 0o644",
        (
            TR + "test_the_token_file_is_mode_0600",
            TC + "test_setup_writes_a_private_file",
        ),
    ),
    Plant(
        "bot-token-chmod-after",
        CRD,
        "    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, FILE_MODE)",
        "    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)\n    os.chmod(temp, FILE_MODE)",
        (TR + "test_the_file_is_created_with_the_mode_not_changed_afterwards",),
    ),
    Plant(
        "bot-token-copied-not-moved",
        CRD,
        "        os.replace(temp, path)\n",
        '        path.write_text(temp.read_text(encoding="utf-8"), encoding="utf-8")\n',
        (
            TR + "test_no_temporary_file_is_left_behind",
            TR + "test_an_old_loose_file_is_replaced_by_a_private_one",
        ),
    ),
    Plant(
        "bot-token-empty-allowed",
        CRD,
        '    if text == "":\n        return "the token is empty"',
        '    if False:\n        return "the token is empty"',
        (TR + "test_an_empty_token_or_one_with_spaces_is_refused",),
    ),
    Plant(
        "bot-token-spaces-allowed",
        CRD,
        "    if any(ch.isspace() for ch in text):",
        "    if False:",
        (TR + "test_an_empty_token_or_one_with_spaces_is_refused",),
    ),
    Plant(
        "bot-token-repr-shows-it",
        CRD,
        "    def __repr__(self) -> str:\n        return MASK",
        "    def __repr__(self) -> str:\n        return self._text",
        (TR + "test_a_token_never_shows_when_printed",),
    ),
    Plant(
        "bot-token-str-shows-it",
        CRD,
        "    def __str__(self) -> str:\n        return MASK",
        "    def __str__(self) -> str:\n        return self._text",
        (TR + "test_a_token_never_shows_when_printed",),
    ),
    Plant(
        "bot-token-format-shows-it",
        CRD,
        "    def __format__(self, spec: str) -> str:\n        return MASK",
        "    def __format__(self, spec: str) -> str:\n        return self._text",
        (TR + "test_a_token_never_shows_when_printed",),
    ),
    Plant(
        "bot-token-read-keeps-newline",
        CRD,
        '        text = path.read_text(encoding="utf-8").strip()',
        '        text = path.read_text(encoding="utf-8")',
        (TR + "test_reading_drops_the_trailing_newline",),
    ),
    Plant(
        "bot-token-file-elsewhere",
        CRD,
        "    return settings_path.parent / FILE_NAME",
        "    return settings_path.parent / (FILE_NAME + '.txt')",
        (TR + "test_the_token_file_sits_beside_the_settings_file",),
    ),
    Plant(
        "bot-token-filter-leaves-message",
        CRD,
        "            record.msg = message.replace(self._secret, MASK)",
        "            record.msg = message",
        (
            TR + "test_the_filter_hides_the_token_in_a_message_from_any_logger",
            TT + "test_the_token_is_in_no_log_record_through_a_whole_run",
        ),
    ),
    Plant(
        "bot-token-filter-keeps-arguments",
        CRD,
        "            record.args = None\n",
        "",
        (TR + "test_the_filter_hides_the_token_in_arguments_of_every_kind",),
    ),
    Plant(
        "bot-token-filter-leaves-traceback",
        CRD,
        "                record.exc_text = text.replace(self._secret, MASK)",
        "                record.exc_text = text",
        (TR + "test_the_filter_hides_the_token_in_a_traceback",),
    ),
    Plant(
        "bot-token-filter-drops-records",
        CRD,
        "            record.stack_info = record.stack_info.replace(self._secret, MASK)\n        return True",
        "            record.stack_info = record.stack_info.replace(self._secret, MASK)\n        return False",
        (TR + "test_the_filter_leaves_other_records_alone",),
    ),
    Plant(
        "bot-token-filter-on-no-handler",
        CRD,
        "    for handler in logging.getLogger().handlers if handlers is None else handlers:",
        "    for handler in logging.getLogger().handlers if handlers is None else []:",
        (TR + "test_the_filter_hides_the_token_in_a_message_from_any_logger",),
    ),
    Plant(
        "bot-token-filter-not-on-root",
        CRD,
        "    for handler in logging.getLogger().handlers if handlers is None else handlers:",
        "    for handler in [] if handlers is None else handlers:",
        (TR + "test_the_filter_goes_on_the_root_handlers_by_default",),
    ),
    Plant(
        "bot-token-filter-breaks-on-bad-format",
        CRD,
        "        except (TypeError, ValueError):\n            message = str(record.msg)",
        "        except ValueError:\n            message = str(record.msg)",
        (TR + "test_the_filter_survives_a_message_that_does_not_format",),
    ),
]

CLI_PLANTS: list[Plant] = [
    Plant(
        "bot-cli-token-echoed",
        BCL,
        "    text = (read or getpass.getpass)(",
        "    text = (read or input)(",
        (TC + "test_the_token_is_read_with_getpass_by_default",),
    ),
    Plant(
        "bot-cli-bad-token-saved",
        BCL,
        '    if problem is not None:\n        logger.error("%s; nothing was saved", problem)\n        return 2',
        "    if False:\n        return 2",
        (TC + "test_setup_refuses_an_empty_token_or_one_with_spaces",),
    ),
    Plant(
        "bot-cli-bad-token-exit-0",
        BCL,
        '        logger.error("%s; nothing was saved", problem)\n        return 2',
        '        logger.error("%s; nothing was saved", problem)\n        return 0',
        (TC + "test_setup_refuses_an_empty_token_or_one_with_spaces",),
    ),
    Plant(
        "bot-cli-token-printed",
        BCL,
        '    print(f"Saved the bot token in {path}")',
        '    print(f"Saved the bot token {text} in {path}")',
        (TC + "test_setup_saves_the_token_beside_the_settings_file",),
    ),
    Plant(
        "bot-cli-unlink-does-nothing",
        BCL,
        "    if linker.unlink():",
        "    if linker.linked_user_id is not None:",
        (TC + "test_unlink_removes_the_link_and_says_so",),
    ),
    Plant(
        "bot-cli-unlink-eats-the-token",
        BCL,
        "    linker = Linker(BotFile(bot_file_path(settings_path)))",
        "    token_path(settings_path).unlink(missing_ok=True)\n    linker = Linker(BotFile(bot_file_path(settings_path)))",
        (TC + "test_unlink_leaves_the_token_alone",),
    ),
    Plant(
        "bot-cli-no-token-goes-on",
        BCL,
        '            "(it saves the token beside the settings file)"\n        )\n        return 2',
        '            "(it saves the token beside the settings file)"\n        )\n        return 0',
        (TC + "test_page_bot_without_the_token_file_exits_2_and_names_bot_setup",),
    ),
    Plant(
        "bot-cli-no-token-unnamed",
        BCL,
        "no bot token: run 'python -m jedimaster bot setup' first ",
        "no bot token: run the setup first ",
        (TC + "test_page_bot_without_the_token_file_exits_2_and_names_bot_setup",),
    ),
    Plant(
        "bot-cli-discord-unchecked",
        BCL,
        '    if importlib.util.find_spec("discord") is None:',
        "    if False:",
        (TC + "test_page_bot_without_discord_py_exits_2_naming_the_extra",),
    ),
    Plant(
        "bot-cli-extra-unnamed",
        BCL,
        "pip install 'jedimaster[bot]'",
        "pip install jedimaster",
        (TC + "test_page_bot_without_discord_py_exits_2_naming_the_extra",),
    ),
    Plant(
        "bot-cli-filter-not-installed",
        BCL,
        "    install_log_filter(token)\n    return token",
        "    return token",
        (
            TC + "test_check_hides_the_token_from_the_handlers",
            TT + "test_the_token_is_in_no_log_record_through_a_whole_run",
        ),
    ),
    Plant(
        "bot-cli-code-always-the-same",
        BCL,
        "    code = linker.start()\n",
        '    code = "AAAA-AAAA"\n',
        (
            TC + "test_each_start_makes_a_new_code",
            TC + "test_page_bot_prints_no_code_once_linked",
        ),
    ),
    Plant(
        "bot-cli-code-minutes-wrong",
        BCL,
        "        minutes = int(LIFETIME_SECONDS // 60)",
        "        minutes = int(LIFETIME_SECONDS // 6)",
        (TC + "test_page_bot_prints_a_link_code_and_one_instruction",),
    ),
    Plant(
        "bot-cli-code-not-printed",
        BCL,
        '        print(f"Link code: {code}")\n',
        "",
        (TC + "test_page_bot_prints_a_link_code_and_one_instruction",),
    ),
    Plant(
        "bot-cli-bot-never-starts",
        BCL,
        "        task = asyncio.get_running_loop().create_task(door.run(token))",
        "        task = None",
        (TC + "test_the_bot_starts_in_the_pages_loop_and_shares_its_control",),
    ),
    Plant(
        "bot-cli-control-not-shared",
        BCL,
        "        door = Door(Core(control, linker), store, deliver)",
        "        door = Door(Core(Control(None, control.store, '0'), linker), store, deliver)",
        (TC + "test_the_bot_starts_in_the_pages_loop_and_shares_its_control",),
    ),
    Plant(
        "bot-cli-token-not-passed",
        BCL,
        "create_task(door.run(token))",
        "create_task(door.run(Token('x')))",
        (TC + "test_the_bot_starts_in_the_pages_loop_and_shares_its_control",),
    ),
    Plant(
        "bot-cli-linked-start-makes-a-code",
        BCL,
        "    linker = Linker(store)\n    code = linker.start()",
        "    linker = Linker(BotFile(store.path.with_name('none.json')))\n    code = linker.start()",
        (TC + "test_page_bot_prints_no_code_once_linked",),
    ),
    Plant(
        "bot-page-bot-flag-ignored",
        PCL,
        "    if args.bot:\n        from ..bot import cli as bot_cli",
        "    if False:\n        from ..bot import cli as bot_cli",
        (TC + "test_page_bot_without_the_token_file_exits_2_and_names_bot_setup",),
    ),
    Plant(
        "bot-page-bot-failure-goes-on",
        PCL,
        "        if isinstance(token, int):\n            return token",
        "        if isinstance(token, int):\n            token = None",
        (TC + "test_page_bot_without_the_token_file_exits_2_and_names_bot_setup",),
    ),
    Plant(
        "bot-page-bot-not-started",
        PCL,
        '        extra["on_start"] = bot_cli.starter(control, settings_path, token)',
        "        pass",
        (TC + "test_page_bot_prints_a_link_code_and_one_instruction",),
    ),
    Plant(
        "bot-page-plain-start-loads-the-bot",
        PCL,
        "    if args.bot:\n        from ..bot import cli as bot_cli",
        "    if True:\n        from ..bot import cli as bot_cli",
        (TC + "test_page_without_the_flag_never_looks_for_the_bot",),
    ),
    Plant(
        "bot-main-command-unlisted",
        "jedimaster/__main__.py",
        '    "bot": bot_cli,\n',
        "",
        (TC + "test_unlink_removes_the_link_and_says_so",),
    ),
    Plant(
        "bot-server-broadcast-count",
        SRV,
        "    return len(pages)",
        "    return 0",
        (TP + "test_the_page_count_is_the_number_of_open_sockets",),
    ),
    Plant(
        "bot-server-broadcast-first-push-only",
        SRV,
        "    for push in pushes:\n        for other in pages:",
        "    for push in pushes[:1]:\n        for other in pages:",
        (TS + "test_the_broadcast_counts_the_open_pages_and_sends_in_order",),
    ),
    Plant(
        "bot-server-on-start-never",
        SRV,
        "            on_start(runner.app)",
        "            pass",
        (TS + "test_serve_calls_on_start_once_with_the_bound_application",),
    ),
    Plant(
        "bot-server-on-start-too-early",
        SRV,
        "        print(url, flush=True)\n        if on_start is not None:\n            on_start(runner.app)\n",
        "        if on_start is not None:\n            on_start(runner.app)\n        print(url, flush=True)\n",
        (TS + "test_serve_calls_on_start_once_with_the_bound_application",),
    ),
]

BOT_PLANTS: list[Plant] = [
    *LINK_PLANTS,
    *TOKEN_PLANTS,
    *CORE_PLANTS,
    *DOOR_PLANTS,
    *CLI_PLANTS,
]
