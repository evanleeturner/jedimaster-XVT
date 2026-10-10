"""The planted faults of the page's control list and settings file.

Purpose:
    Break one rule of ``jedimaster/page/control.py`` or ``settings.py`` per
    plant: the request checks, each command's answer and the settings file.

Flow:
    ``plants_page`` joins this table with the others; ``plant_faults``
    applies each plant alone: write ``new`` over ``old`` in ``path``, run the
    suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``page-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_page_control import CONTROL_FILE_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

CTL = "jedimaster/page/control.py"
SET = "jedimaster/page/settings.py"
PRO = "jedimaster/page/protocol.py"
TC = "tests/test_page_control.py::"
TS = "tests/test_page_settings.py::"

CONTROL_PLANTS: list[Plant] = [
    Plant(
        "page-request-args-dropped",
        CTL,
        "    return ParsedRequest(known, command, args)",
        "    return ParsedRequest(known, command, {})",
        (TC + "test_parse_request_returns_the_request",),
    ),
    Plant(
        "page-request-size-cap-off",
        CTL,
        '    if len(text.encode("utf-8")) > MAX_MESSAGE_BYTES:',
        "    if False:",
        (TC + "test_oversize_message_is_a_bad_message",),
    ),
    Plant(
        "page-request-not-object-passes",
        CTL,
        "    if not isinstance(doc, dict):",
        "    if False:",
        (TC + "test_bad_message[array]", TC + "test_bad_message[string]"),
    ),
    Plant(
        "page-request-bool-is-an-id",
        CTL,
        "    return isinstance(value, int) and not isinstance(value, bool)",
        "    return isinstance(value, int)",
        (TC + "test_bad_message[id-bool]",),
    ),
    Plant(
        "page-request-id-over-max",
        CTL,
        "1 <= ident <= MAX_ID else UNKNOWN_ID",
        "1 <= ident <= MAX_ID + 1 else UNKNOWN_ID",
        (TC + "test_bad_message[id-too-large]",),
    ),
    Plant(
        "page-request-id-below-max",
        CTL,
        "1 <= ident <= MAX_ID else UNKNOWN_ID",
        "1 <= ident < MAX_ID else UNKNOWN_ID",
        (TC + "test_the_largest_id_is_accepted",),
    ),
    Plant(
        "page-request-extra-field-allowed",
        CTL,
        '    if set(doc) != {"id", "command", "args"}:',
        '    if not {"id", "command", "args"} <= set(doc):',
        (TC + "test_bad_message[extra-field]",),
    ),
    Plant(
        "page-request-missing-field-allowed",
        CTL,
        '    if set(doc) != {"id", "command", "args"}:',
        "    if False:",
        (TC + "test_bad_message[id-only]", TC + "test_bad_message[no-args]"),
    ),
    Plant(
        "page-request-args-any-type",
        CTL,
        "    if not isinstance(command, str) or not isinstance(args, dict):",
        "    if not isinstance(command, str):",
        (TC + "test_bad_message[args-list]",),
    ),
    Plant(
        "page-request-command-any-type",
        CTL,
        "    if not isinstance(command, str) or not isinstance(args, dict):",
        "    if not isinstance(args, dict):",
        (TC + "test_bad_message[command-number]",),
    ),
    Plant(
        "page-request-any-command",
        CTL,
        "    if command not in COMMANDS:",
        "    if False:",
        (TC + "test_command_off_the_list_is_refused_and_logged",),
    ),
    Plant(
        "page-request-refusal-not-logged",
        CTL,
        "        if isinstance(parsed, Refusal):\n            logger.warning(",
        "        if isinstance(parsed, Refusal):\n            logger.debug(",
        (TC + "test_command_off_the_list_is_refused_and_logged",),
    ),
    Plant(
        "page-request-refusal-forgets-id",
        CTL,
        '                    "id": parsed.id,\n                    "ok": False,',
        '                    "id": UNKNOWN_ID,\n                    "ok": False,',
        (TC + "test_bad_arguments",),
    ),
    Plant(
        "page-args-hello-unchecked",
        CTL,
        '    if set(args) != {"page_version"}:',
        "    if False:",
        (
            TC + "test_bad_arguments[hello-no-version]",
            TC + "test_bad_arguments[hello-extra]",
        ),
    ),
    Plant(
        "page-args-hello-empty-version",
        CTL,
        "not 1 <= len(version) <= 64",
        "not 0 <= len(version) <= 64",
        (TC + "test_bad_arguments[hello-empty-version]",),
    ),
    Plant(
        "page-args-hello-long-version",
        CTL,
        "not 1 <= len(version) <= 64",
        "not 1 <= len(version) <= 65",
        (TC + "test_bad_arguments[hello-long-version]",),
    ),
    Plant(
        "page-args-hello-version-type",
        CTL,
        "    if not isinstance(version, str) or not 1 <= len(version) <= 64:",
        "    if not 1 <= len(version) <= 64:",
        (TC + "test_bad_arguments[hello-number-version]",),
    ),
    Plant(
        "page-args-none-taken",
        CTL,
        '    return None if not args else "this command takes no arguments"',
        "    return None",
        (
            TC + "test_bad_arguments[status-args]",
            TC + "test_bad_arguments[missions-args]",
            TC + "test_bad_arguments[get-args]",
        ),
    ),
    Plant(
        "page-args-set-name-unchecked",
        CTL,
        "    if not isinstance(name, str) or name not in SETTINGS:",
        "    if False:",
        (
            TC + "test_bad_arguments[set-unknown-name]",
            TC + "test_bad_arguments[set-list-name]",
        ),
    ),
    Plant(
        "page-args-set-value-unchecked",
        CTL,
        "    if not isinstance(value, str) or value not in SETTINGS[name]:",
        "    if False:",
        (
            TC + "test_bad_arguments[set-unknown-value]",
            TC + "test_bad_arguments[set-number-value]",
        ),
    ),
    Plant(
        "page-args-set-extra-allowed",
        CTL,
        '    if set(args) != {"name", "value"}:',
        '    if not {"name", "value"} <= set(args):',
        (TC + "test_bad_arguments[set-extra]",),
    ),
    Plant(
        "page-args-set-missing-allowed",
        CTL,
        '    if set(args) != {"name", "value"}:',
        "    if False:",
        (TC + "test_bad_arguments[set-empty]", TC + "test_bad_arguments[set-no-value]"),
    ),
    Plant(
        "page-hello-revision",
        CTL,
        '            "schema_revision": SCHEMA_REVISION,',
        '            "schema_revision": 0,',
        (TC + "test_hello",),
    ),
    Plant(
        "page-hello-version",
        CTL,
        '            "launcher_version": self.launcher_version,\n            "schema_revision"',
        '            "launcher_version": "0",\n            "schema_revision"',
        (TC + "test_hello",),
    ),
    Plant(
        "page-status-install-flag",
        CTL,
        '                "install_found": self.install is not None,',
        '                "install_found": True,',
        (TC + "test_install_not_found",),
    ),
    Plant(
        "page-status-event-name",
        CTL,
        '            "event": "status",',
        '            "event": "state",',
        (TC + "test_status_push_is_valid",),
    ),
    Plant(
        "page-install-missing-found",
        CTL,
        '            return {"found": False, "path": None, "balance_of_power": False}',
        '            return {"found": True, "path": None, "balance_of_power": False}',
        (TC + "test_install_not_found",),
    ),
    Plant(
        "page-install-bop-always",
        CTL,
        '            "balance_of_power": bop is not None and bop.is_dir(),',
        '            "balance_of_power": True,',
        (TC + "test_install_status_without_balance_of_power",),
    ),
    Plant(
        "page-install-home-spelled-out",
        CTL,
        '    return "~" if not rest.parts else f"~/{rest.as_posix()}"',
        "    return str(path)",
        (TC + "test_install_status_shows_home_as_tilde",),
    ),
    Plant(
        "page-install-outside-home-hidden",
        CTL,
        "    except ValueError:\n        return str(path)",
        '    except ValueError:\n        return "~"',
        (TC + "test_install_status_found",),
    ),
    Plant(
        "page-missions-order-reversed",
        CTL,
        '        return {"menus": [self._menu(name) for name in MISSION_TYPES]}',
        '        return {"menus": [self._menu(n) for n in reversed(list(MISSION_TYPES))]}',
        (TC + "test_missions_list",),
    ),
    Plant(
        "page-missions-unresolved-claims-resolved",
        CTL,
        '            "resolved": False,',
        '            "resolved": True,',
        (TC + "test_menu_that_does_not_resolve_is_reported",),
    ),
    Plant(
        "page-missions-never-resolved",
        CTL,
        '        menu["resolved"] = True',
        "        pass",
        (TC + "test_missions_list",),
    ),
    Plant(
        "page-missions-unreadable-raises",
        CTL,
        '        except (ListFormatError, OSError) as exc:\n            logger.warning("cannot read the menu',
        '        except OSError as exc:\n            logger.warning("cannot read the menu',
        (TC + "test_menu_the_reader_refuses_is_reported_and_logged",),
    ),
    Plant(
        "page-missions-availability-ignored",
        CTL,
        '                "available": e.available,',
        '                "available": True,',
        (TC + "test_missions_list",),
    ),
    Plant(
        "page-missions-section-dropped",
        CTL,
        '                "section": e.section,',
        '                "section": "",',
        (TC + "test_missions_list",),
    ),
    Plant(
        "page-missions-line-leaks",
        CTL,
        '                "title": e.title,\n',
        '                "title": e.title,\n                "line": e.line,\n',
        (TC + "test_missions_list",),
    ),
    Plant(
        "page-set-not-stored",
        CTL,
        '        self.store.set(args["name"], args["value"])',
        "        pass",
        (TC + "test_settings_set_changes_saves_and_announces",),
    ),
    Plant(
        "page-set-not-announced",
        CTL,
        '        self._pushes.append({"event": "settings.changed", "data": result})',
        "        pass",
        (TC + "test_settings_set_changes_saves_and_announces",),
    ),
    Plant(
        "page-set-pushes-linger",
        CTL,
        "        self._pushes = []\n",
        "",
        (TC + "test_settings_set_changes_saves_and_announces",),
    ),
]

SETTINGS_PLANTS: list[Plant] = [
    Plant(
        "page-settings-xdg-ignored",
        SET,
        '        base = Path(env["XDG_CONFIG_HOME"]) if env.get("XDG_CONFIG_HOME") else None',
        "        base = None",
        (TS + "test_path_on_linux_uses_xdg_config_home",),
    ),
    Plant(
        "page-settings-no-dot-config",
        SET,
        '        base = base if base is not None else home / ".config"',
        "        base = base if base is not None else home",
        (TS + "test_path_on_linux_falls_back_to_dot_config",),
    ),
    Plant(
        "page-settings-macos-library",
        SET,
        '        base = home / "Library" / "Application Support"',
        '        base = home / "Library"',
        (TS + "test_path_on_macos",),
    ),
    Plant(
        "page-settings-windows-appdata",
        SET,
        '        base = Path(env["APPDATA"]) if env.get("APPDATA") else home / "AppData/Roaming"',
        '        base = home / "AppData/Roaming"',
        (TS + "test_path_on_windows_uses_appdata",),
    ),
    Plant(
        "page-settings-no-app-folder",
        SET,
        "    return base / APP_FOLDER / FILE_NAME",
        "    return base / FILE_NAME",
        (
            TS + "test_path_on_linux_uses_xdg_config_home",
            TS + "test_path_on_linux_falls_back_to_dot_config",
            TS + "test_path_on_macos",
            TS + "test_path_on_windows_uses_appdata",
        ),
    ),
    Plant(
        "page-settings-missing-file-warns",
        SET,
        "        except FileNotFoundError:",
        "        except IsADirectoryError:",
        (TS + "test_missing_file_gives_defaults_without_a_warning",),
    ),
    Plant(
        "page-settings-not-text-raises",
        SET,
        "        except (OSError, UnicodeDecodeError) as exc:",
        "        except OSError as exc:",
        (TS + "test_a_file_that_is_not_text_gives_defaults_and_warns",),
    ),
    Plant(
        "page-settings-broken-json-raises",
        SET,
        "        except json.JSONDecodeError as exc:",
        "        except KeyError as exc:",
        (TS + "test_broken_file_gives_defaults_warns_and_stays",),
    ),
    Plant(
        "page-settings-not-object-silent",
        SET,
        "        if not isinstance(data, dict):",
        "        if False:",
        (TS + "test_broken_file_gives_defaults_warns_and_stays",),
    ),
    Plant(
        "page-settings-bad-value-taken",
        SET,
        "            if data[name] in allowed:",
        "            if True:",
        (TS + "test_broken_file_gives_defaults_warns_and_stays",),
    ),
    Plant(
        "page-settings-unknown-names-kept",
        SET,
        "                self.values[name] = data[name]",
        "                self.values.update(data)",
        (TS + "test_unknown_names_in_the_file_are_ignored",),
    ),
    Plant(
        "page-settings-get-not-a-copy",
        SET,
        '        """Return a copy of every setting\'s current value."""\n        return dict(self.values)',
        '        """Return a copy of every setting\'s current value."""\n        return self.values',
        (TS + "test_get_returns_a_copy",),
    ),
    Plant(
        "page-settings-set-unchecked",
        SET,
        "        if name not in SETTINGS or value not in SETTINGS[name]:",
        "        if False:",
        (TS + "test_set_off_the_list_raises_and_changes_nothing",),
    ),
    Plant(
        "page-settings-set-value-unchecked",
        SET,
        "        if name not in SETTINGS or value not in SETTINGS[name]:",
        "        if name not in SETTINGS:",
        (TS + "test_set_off_the_list_raises_and_changes_nothing",),
    ),
    Plant(
        "page-settings-set-not-written",
        SET,
        "        return self._write()",
        "        return True",
        (TS + "test_a_change_is_written_and_read_back",),
    ),
    Plant(
        "page-settings-no-folder-made",
        SET,
        "            self.path.parent.mkdir(parents=True, exist_ok=True)",
        "            pass",
        (TS + "test_a_change_is_written_and_read_back",),
    ),
    Plant(
        "page-settings-write-in-place",
        SET,
        '            temp.write_text(json.dumps(self.values, indent=2) + "\\n", encoding="utf-8")\n            os.replace(temp, self.path)',
        '            self.path.write_text(json.dumps(self.values, indent=2) + "\\n", encoding="utf-8")\n            temp.write_text("half", encoding="utf-8")',
        (TS + "test_a_change_is_written_and_read_back",),
    ),
    Plant(
        "page-settings-write-failure-hidden",
        SET,
        '            logger.warning("cannot write the settings file %s: %s", self.path, exc)\n            return False',
        '            logger.warning("cannot write the settings file %s: %s", self.path, exc)\n            return True',
        (TS + "test_unwritable_file_warns_and_keeps_the_value",),
    ),
    Plant(
        "page-settings-failure-not-logged",
        SET,
        '            logger.warning("cannot write the settings file %s: %s", self.path, exc)',
        '            logger.debug("cannot write the settings file %s: %s", self.path, exc)',
        (TS + "test_unwritable_file_warns_and_keeps_the_value",),
    ),
    Plant(
        "page-settings-default-value",
        PRO,
        'DEFAULT_SETTINGS: dict[str, str] = {"art_scaling": "whole_pixels"}',
        'DEFAULT_SETTINGS: dict[str, str] = {"art_scaling": "engine_fit"}',
        (TC + "test_settings_get_defaults", TS + "test_missing_file_gives"),
    ),
]

SHOW_PLANTS: list[Plant] = [
    Plant(
        "page-show-command-unregistered",
        CTL,
        '            "page.show_mission": self._show_mission,\n',
        "",
        (TC + "test_show_mission_finds_the_entry_and_pushes_it",),
    ),
    Plant(
        "page-show-args-unchecked",
        CTL,
        '    "page.show_mission": _show_args,\n',
        '    "page.show_mission": lambda args: None,\n',
        (TC + "test_bad_arguments[show-empty]",),
    ),
    Plant(
        "page-show-type-unchecked",
        CTL,
        "    if not isinstance(kind, str) or kind not in MISSION_TYPE_NAMES:",
        "    if not isinstance(kind, str):",
        (TC + "test_bad_arguments[show-unknown-type]",),
    ),
    Plant(
        "page-show-negative-id-allowed",
        CTL,
        "    if not _is_int(ident) or not 0 <= ident <= MAX_ID:",
        "    if not _is_int(ident) or not -5 <= ident <= MAX_ID:",
        (TC + "test_bad_arguments[show-negative-id]",),
    ),
    Plant(
        "page-show-id-above-max-allowed",
        CTL,
        "    if not _is_int(ident) or not 0 <= ident <= MAX_ID:",
        "    if not _is_int(ident) or not 0 <= ident <= MAX_ID + 1:",
        (TC + "test_bad_arguments[show-id-too-large]",),
    ),
    Plant(
        "page-show-id-zero-refused",
        CTL,
        "    if not _is_int(ident) or not 0 <= ident <= MAX_ID:",
        "    if not _is_int(ident) or not 1 <= ident <= MAX_ID:",
        (TC + "test_a_missing_mission_is_an_ok_reply_with_no_push",),
    ),
    Plant(
        "page-show-extra-argument-allowed",
        CTL,
        '    if set(args) != {"mission_type", "id"}:',
        '    if not {"mission_type", "id"} <= set(args):',
        (TC + "test_bad_arguments[show-extra]",),
    ),
    Plant(
        "page-show-last-entry-wins",
        CTL,
        '        for entry in menu["entries"]:\n            if entry["id"] == args["id"]:',
        '        for entry in reversed(menu["entries"]):\n            if entry["id"] == args["id"]:',
        (TC + "test_when_an_id_repeats_the_first_entry_wins",),
    ),
    Plant(
        "page-show-every-id-matches",
        CTL,
        '            if entry["id"] == args["id"]:',
        "            if True:",
        (TC + "test_a_missing_mission_is_an_ok_reply_with_no_push",),
    ),
    Plant(
        "page-show-type-ignored",
        CTL,
        '        menu = self._menu(args["mission_type"])',
        '        menu = self._menu("training")',
        (TC + "test_show_mission_finds_an_entry_in_another_type",),
    ),
    Plant(
        "page-show-event-renamed",
        CTL,
        '                        "event": "page.show_mission",',
        '                        "event": "page.show",',
        (TC + "test_show_mission_finds_the_entry_and_pushes_it",),
    ),
    Plant(
        "page-show-title-is-the-file",
        CTL,
        '                            "title": entry["title"],',
        '                            "title": entry["file"],',
        (TC + "test_show_mission_finds_the_entry_and_pushes_it",),
    ),
    Plant(
        "page-show-always-shown",
        CTL,
        '        return {"shown": False}',
        '        return {"shown": True}',
        (TC + "test_a_missing_mission_is_an_ok_reply_with_no_push",),
    ),
    Plant(
        "page-show-never-shown",
        CTL,
        '                return {"shown": True}',
        '                return {"shown": False}',
        (TC + "test_show_mission_finds_the_entry_and_pushes_it",),
    ),
    Plant(
        "page-show-push-id-wrong",
        CTL,
        '                            "id": args["id"],',
        '                            "id": entry["id"] + 1,',
        (TC + "test_show_mission_finds_the_entry_and_pushes_it",),
    ),
    Plant(
        "page-show-hello-says-revision-1",
        PRO,
        "SCHEMA_REVISION = 2",
        "SCHEMA_REVISION = 1",
        (TC + "test_hello", TC + "test_the_list_has_six_commands_and_revision_2"),
    ),
    Plant(
        "page-show-command-dropped",
        PRO,
        "    SettingsSetRequest,\n    ShowMissionRequest,\n)\nCOMMANDS",
        "    SettingsSetRequest,\n)\nCOMMANDS",
        (TC + "test_the_list_has_six_commands_and_revision_2",),
        regen_schema=True,
    ),
    Plant(
        "page-show-type-name-dropped",
        PRO,
        '"training", "melee", "tournament", "combat", "battle", "campaign"\n]',
        '"training", "melee", "tournament", "combat", "battle"\n]',
        (TC + "test_the_mission_types_of_the_list_are_the_games_six",),
        regen_schema=True,
    ),
]

CONTROL_FILE_PLANTS: list[Plant] = [*CONTROL_PLANTS, *SETTINGS_PLANTS, *SHOW_PLANTS]
