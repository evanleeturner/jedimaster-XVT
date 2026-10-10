"""The bot's decisions: who is answered, what each command sends, what it says.

Purpose:
    Prove that only the linked account is answered, that every refusal is
    logged, that each command sends exactly the requests the fixed list
    names, that a name off the list is refused, that menus are cut to the
    size Discord allows, and that no reply holds the code.

Flow:
    A ``Core`` over a made-up install; ``Control.handle`` wrapped to record
    the text of every request it is given.

Invariants:
    - No Discord, no network, no game data.
    - Every reply of ``Control`` that the bot sees validates against the
      published schema.

Call:
    ``pytest tests/test_bot_core.py``
"""

from __future__ import annotations

import json
import logging
import re
import subprocess
import sys
from pathlib import Path

import pytest
from botdata import HOST
from botdata import make_bot
from botdata import STRANGER
from listdata import crlf
from pagedata import check_valid

from jedimaster.bot import core as core_module
from jedimaster.bot.core import HOST_ONLY
from jedimaster.bot.core import SETTING_WORDS
from jedimaster.page.protocol import MAX_ID
from jedimaster.page.protocol import MISSION_TYPE_NAMES
from jedimaster.page.protocol import SETTINGS

logger = logging.getLogger(__name__)

LAUNCHER = Path(__file__).resolve().parents[1]


@pytest.fixture
def bot(tmp_path):
    made = make_bot(tmp_path)
    made.link_account(HOST)
    made.sent = []
    real = made.control.handle

    def recording(text):
        outcome = real(text)
        made.sent.append((json.loads(text), outcome))
        check_valid(outcome.reply)
        for push in outcome.pushes:
            check_valid(push)
        return outcome

    made.control.handle = recording
    return made


def commands_sent(bot):
    return [request["command"] for request, _ in bot.sent]


# ---- who is answered ------------------------------------------------------


@pytest.mark.parametrize(
    "ask",
    [
        lambda c, u: c.status(u),
        lambda c, u: c.missions(u, "training"),
        lambda c, u: c.pick_mission(u, "training", "1"),
        lambda c, u: c.settings(u, True),
        lambda c, u: c.pick_setting(u, "art_scaling", "engine_fit", True),
        lambda c, u: c.unlink(u, True),
    ],
    ids=["status", "missions", "pick_mission", "settings", "pick_setting", "unlink"],
)
def test_an_unlinked_account_is_refused_on_each_command(bot, ask, caplog):
    with caplog.at_level(logging.WARNING):
        reply = ask(bot.core, STRANGER)
    assert reply.text == HOST_ONLY
    assert reply.pickers == () and reply.pushes == []
    assert bot.sent == []
    assert "not the linked account" in caplog.text
    assert bot.linker.linked_user_id == HOST
    assert bot.control.store.get() == {"art_scaling": "whole_pixels"}


def test_nothing_is_answered_before_anyone_is_linked(tmp_path):
    fresh = make_bot(tmp_path)
    assert fresh.core.status(HOST).text == HOST_ONLY
    assert fresh.core.settings(HOST, True).text == HOST_ONLY


def test_the_refusal_is_one_plain_line():
    assert "\n" not in HOST_ONLY and "host" in HOST_ONLY


@pytest.mark.parametrize("word", ["settings", "unlink", "link"])
def test_direct_message_commands_refuse_a_server(bot, word):
    ask = {
        "settings": lambda: bot.core.settings(HOST, False),
        "unlink": lambda: bot.core.unlink(HOST, False),
        "link": lambda: bot.core.link(STRANGER, "AAAA-AAAA", False),
    }[word]
    assert "direct message" in ask().text
    assert bot.linker.linked_user_id == HOST


def test_a_code_sent_in_a_server_is_not_spent(tmp_path):
    fresh = make_bot(tmp_path)
    code = fresh.linker.start()
    assert "direct message" in fresh.core.link(HOST, code, False).text
    assert fresh.core.link(HOST, code, True).text.startswith("Linked")


# ---- /link and /unlink ----------------------------------------------------


@pytest.mark.parametrize(
    "outcome, start",
    [
        ("wrong", "That code is not right"),
        ("already_linked", "This launcher is already linked"),
    ],
)
def test_link_answers_each_outcome(tmp_path, outcome, start):
    fresh = make_bot(tmp_path)
    code = fresh.linker.start()
    if outcome == "already_linked":
        fresh.linker.attempt(HOST, code)
    reply = fresh.core.link(STRANGER, "AAAA-AAAA", True)
    assert reply.text.startswith(start)
    assert code not in reply.text and "AAAA" not in reply.text


def test_link_answers_linked_expired_and_void(tmp_path):
    fresh = make_bot(tmp_path)
    code = fresh.linker.start()
    fresh.clock.now += 601
    assert "expired" in fresh.core.link(HOST, code, True).text
    code = fresh.linker.start()
    for _ in range(5):
        reply = fresh.core.link(STRANGER, "BBBB-BBBB", True)
    assert "void" in reply.text and "Restart the launcher" in reply.text
    assert "void" in fresh.core.link(HOST, code, True).text
    code = fresh.linker.start()
    reply = fresh.core.link(HOST, code, True)
    assert reply.text.startswith("Linked") and code not in reply.text


def test_link_sends_no_request(bot):
    bot.core.link(STRANGER, "AAAA-AAAA", True)
    assert bot.sent == []


def test_link_once_linked_says_already_linked_to_any_account(bot):
    for who in (HOST, STRANGER):
        assert "already linked" in bot.core.link(who, "AAAA-AAAA", True).text


def test_unlink_removes_the_link_and_sends_no_request(bot):
    reply = bot.core.unlink(HOST, True)
    assert reply.text.startswith("Unlinked")
    assert bot.linker.linked_user_id is None
    assert bot.sent == []
    assert bot.core.status(HOST).text == HOST_ONLY


# ---- /status --------------------------------------------------------------


def test_status_sends_hello_and_install_status(bot):
    reply = bot.core.status(HOST)
    assert commands_sent(bot) == ["hello", "install.status"]
    lines = reply.text.splitlines()
    assert lines[0] == "Launcher version 0.1.0."
    assert lines[1].startswith("Game install: ") and lines[1].endswith("XvT.")
    assert lines[2] == "Balance of Power: found."


def test_status_without_balance_of_power(tmp_path):
    made = make_bot(tmp_path)
    made.link_account(HOST)
    (tmp_path / "XvT" / "BalanceOfPower" / "TRAIN" / "notes.txt").unlink()
    (tmp_path / "XvT" / "BalanceOfPower" / "TRAIN").rmdir()
    (tmp_path / "XvT" / "BalanceOfPower").rmdir()
    assert made.core.status(HOST).text.endswith("Balance of Power: not found.")


def test_status_without_an_install(tmp_path):
    made = make_bot(tmp_path)
    made.link_account(HOST)
    made.control.install = None
    assert made.core.status(HOST).text.splitlines()[1] == "No game install was found."


# ---- /missions ------------------------------------------------------------


def test_missions_sends_missions_list_and_offers_that_menu(bot):
    reply = bot.core.missions(HOST, "training")
    assert commands_sent(bot) == ["missions.list"]
    (picker,) = reply.pickers
    assert picker.kind == "mission" and picker.key == "training"
    labels = [(o.label, o.value) for o in picker.options]
    assert labels == [
        ("Basic: First Flight", "1"),
        ("Basic: Second Flight", "2"),
        ("Advanced: Third Flight", "3"),
    ]
    assert picker.options[1].description == "bravo.tie, not available"
    assert picker.options[0].description == "alpha.tie"


def test_missions_for_a_type_the_install_lacks_says_so(bot):
    reply = bot.core.missions(HOST, "tournament")
    assert reply.pickers == ()
    assert "was not found" in reply.text


def test_missions_for_an_unknown_type_says_which_exist(bot):
    reply = bot.core.missions(HOST, "skirmish")
    assert bot.sent == []
    for name in MISSION_TYPE_NAMES:
        assert name in reply.text


def write_menu(bot, count, repeat_first=False):
    lines = ["[Open]"]
    for n in range(count):
        lines += [
            str(1 if repeat_first and n == 1 else n + 1),
            f"M{n}.TIE",
            f"Mission {n}",
        ]
    (bot.folder / "XvT" / "Melee" / "MISSION.LST").write_bytes(crlf(*lines))


def test_a_menu_is_25_entries_and_at_most_5_menus(bot):
    write_menu(bot, 130)
    reply = bot.core.missions(HOST, "melee")
    sizes = [len(p.options) for p in reply.pickers]
    assert sizes == [25, 25, 25, 25, 25]
    assert "first 125 of 130" in reply.text
    assert reply.pickers[1].placeholder == "Melee missions 26 to 50"


def test_a_short_last_menu_is_not_padded(bot):
    write_menu(bot, 30)
    sizes = [len(p.options) for p in bot.core.missions(HOST, "melee").pickers]
    assert sizes == [25, 5]


def test_exactly_125_entries_say_nothing_about_cutting(bot):
    write_menu(bot, 125)
    reply = bot.core.missions(HOST, "melee")
    assert len(reply.pickers) == 5 and "first" not in reply.text


def test_a_repeated_id_is_offered_once_and_the_first_entry_wins(bot):
    write_menu(bot, 3, repeat_first=True)
    (picker,) = bot.core.missions(HOST, "melee").pickers
    assert [(o.value, o.label) for o in picker.options] == [
        ("1", "Open: Mission 0"),
        ("3", "Open: Mission 2"),
    ]


def test_long_titles_fit_discords_limits(bot):
    (bot.folder / "XvT" / "Melee" / "MISSION.LST").write_bytes(
        crlf("[Open]", "1", "A.TIE", "T" * 300)
    )
    (picker,) = bot.core.missions(HOST, "melee").pickers
    assert len(picker.options[0].label) == 100


def test_an_empty_menu_says_so(bot):
    (bot.folder / "XvT" / "Melee" / "MISSION.LST").write_bytes(crlf("[Open]"))
    reply = bot.core.missions(HOST, "melee")
    assert reply.pickers == () and "empty" in reply.text


# ---- picking a mission ----------------------------------------------------


def test_picking_a_mission_sends_page_show_mission(bot):
    reply = bot.core.pick_mission(HOST, "training", "2")
    (request, outcome) = bot.sent[-1]
    assert request["command"] == "page.show_mission"
    assert request["args"] == {"mission_type": "training", "id": 2}
    assert reply.text == "Showing Second Flight on the page."
    assert reply.pushes == [
        {
            "event": "page.show_mission",
            "data": {"mission_type": "training", "id": 2, "title": "Second Flight"},
        }
    ]


def test_picking_a_mission_that_is_not_listed_pushes_nothing(bot):
    reply = bot.core.pick_mission(HOST, "training", "99")
    assert reply.pushes == [] and "not in the game's list" in reply.text


@pytest.mark.parametrize(
    "pick",
    [
        ("skirmish", "1"),
        ("training", "x"),
        ("training", "-1"),
        ("training", "²"),
        ("training", ""),
    ],
)
def test_a_pick_that_is_not_a_mission_sends_nothing(bot, pick):
    reply = bot.core.pick_mission(HOST, *pick)
    assert bot.sent == [] and "not a mission" in reply.text


def test_a_pick_with_an_id_too_large_is_refused_by_the_list(bot):
    reply = bot.core.pick_mission(HOST, "training", str(2**31))
    assert reply.text == core_module.REFUSED and reply.pushes == []


def test_when_no_page_is_open_the_reply_says_so(bot):
    reply = bot.core.pick_mission(HOST, "training", "1")
    assert bot.core.delivered_text(reply, 0).endswith(core_module.NO_PAGE)
    assert bot.core.delivered_text(reply, 1) == reply.text
    plain = bot.core.status(HOST)
    assert bot.core.delivered_text(plain, 0) == plain.text


# ---- /settings ------------------------------------------------------------


def test_settings_sends_settings_get_and_offers_each_setting(bot):
    reply = bot.core.settings(HOST, True)
    assert commands_sent(bot) == ["settings.get"]
    (picker,) = reply.pickers
    assert picker.kind == "setting" and picker.key == "art_scaling"
    assert [(o.label, o.value, o.default) for o in picker.options] == [
        ("Whole pixels", "whole_pixels", True),
        ("Engine fit", "engine_fit", False),
        ("Sharp bilinear", "sharp_bilinear", False),
    ]
    assert all(o.description.endswith(".") for o in picker.options)
    assert reply.text == "Art scaling is now Whole pixels."


def test_every_setting_and_value_has_words():
    assert set(SETTING_WORDS) == set(SETTINGS)
    for name, values in SETTINGS.items():
        assert set(SETTING_WORDS[name][1]) == set(values)


def test_picking_a_setting_sends_settings_set_and_pushes_the_change(bot):
    reply = bot.core.pick_setting(HOST, "art_scaling", "engine_fit", True)
    (request, _) = bot.sent[-1]
    assert request["command"] == "settings.set"
    assert request["args"] == {"name": "art_scaling", "value": "engine_fit"}
    assert reply.text == "Art scaling set to Engine fit."
    assert reply.pushes == [
        {
            "event": "settings.changed",
            "data": {"settings": {"art_scaling": "engine_fit"}},
        }
    ]
    assert bot.control.store.get() == {"art_scaling": "engine_fit"}


@pytest.mark.parametrize("pick", [("volume", "high"), ("art_scaling", "loud")])
def test_picking_a_setting_off_the_list_changes_nothing(bot, pick):
    reply = bot.core.pick_setting(HOST, *pick, True)
    assert reply.text == core_module.REFUSED and reply.pushes == []
    assert bot.control.store.get() == {"art_scaling": "whole_pixels"}


# ---- the request path -----------------------------------------------------


def test_a_command_off_the_list_is_refused_by_the_request_path(bot, caplog):
    with caplog.at_level(logging.WARNING):
        outcome = bot.core.send("shutdown", {})
    assert outcome.reply["ok"] is False
    assert outcome.reply["error"]["code"] == "unknown_command"
    assert outcome.pushes == []
    assert "unknown_command" in caplog.text
    assert bot.control.store.get() == {"art_scaling": "whole_pixels"}


def test_wrong_arguments_are_refused_by_the_request_path(bot):
    outcome = bot.core.send("settings.set", {"name": "art_scaling", "value": "x"})
    assert outcome.reply["error"]["code"] == "bad_arguments"


def test_requests_are_json_text_with_ids_from_a_counter(bot):
    bot.core.status(HOST)
    bot.core.status(HOST)
    assert [r["id"] for r, _ in bot.sent] == [1, 2, 3, 4]
    assert set(bot.sent[0][0]) == {"id", "command", "args"}


def test_the_counter_starts_again_after_the_largest_id(bot):
    bot.core = core_module.Core(bot.control, bot.linker, first_id=MAX_ID)
    bot.core.send("settings.get", {})
    bot.core.send("settings.get", {})
    assert [r["id"] for r, _ in bot.sent] == [MAX_ID, 1]


def test_the_page_checks_apply_unchanged(bot):
    (outcome,) = [bot.core.send("hello", {})]
    assert outcome.reply["error"]["code"] == "bad_arguments"


# ---- logging --------------------------------------------------------------


def test_each_command_is_logged_at_debug_with_asker_requests_and_outcome(bot, caplog):
    with caplog.at_level(logging.DEBUG, logger="jedimaster.bot.core"):
        bot.core.status(HOST)
    ended = [r.getMessage() for r in caplog.records if "requests" in r.getMessage()]
    assert ended == [f"command status from account {HOST}: requests [1, 2], ok"]


# ---- imports --------------------------------------------------------------


def test_core_imports_with_discord_blocked_and_without_the_web_server():
    code = (
        "import sys\n"
        "sys.modules['discord'] = None\n"
        "import jedimaster.bot.core\n"
        "import jedimaster.bot.link, jedimaster.bot.credentials, jedimaster.bot.cli\n"
        "assert 'jedimaster.page.server' not in sys.modules\n"
        "assert 'aiohttp' not in sys.modules\n"
        "assert 'jedimaster.bot.discord_door' not in sys.modules\n"
    )
    done = subprocess.run(
        [sys.executable, "-c", code], cwd=LAUNCHER, capture_output=True, text=True
    )
    assert done.returncode == 0, done.stderr


def test_only_the_door_names_discord():
    for path in (LAUNCHER / "jedimaster" / "bot").glob("*.py"):
        text = path.read_text(encoding="utf-8")
        names = re.search(r"^(import|from) discord\b", text, re.MULTILINE) is not None
        assert names == (path.name == "discord_door.py"), path.name
