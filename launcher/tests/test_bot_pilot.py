"""The bot's pilot steps: made at the first link, kept or renamed once, shown in /status.

Purpose:
    Prove a link that succeeds makes the account's pilot from its Discord
    names and offers Keep name and Change name; that the Pilot command makes
    one for a linked account that has none; that Change name opens a window
    starting at the name, a refused name changes nothing and keeps the
    buttons, and the one change is used once; that every step is for the
    linked account in a direct message only; and that the door carries the
    buttons and the window to Discord and back.

Flow:
    A ``Core`` over the made-up install with a ``PilotStore`` in a temporary
    folder; replies read as plain data; the door's steps through the
    stand-in for Discord in ``botdata``.

Invariants:
    - No Discord connection; no game data.

Call:
    ``pytest tests/test_bot_pilot.py``
"""

from __future__ import annotations

import json
import logging

import pytest
from botdata import HOST
from botdata import make_bot
from botdata import STRANGER
from botdata import Talk

from jedimaster.bot.core import DM_ONLY
from jedimaster.bot.core import HOST_ONLY
from jedimaster.bot.discord_door import Door
from jedimaster.bot.discord_door import TextWindow
from jedimaster.bot.pilot_walk import BAD_FILE
from jedimaster.bot.pilot_walk import BAD_NAME
from jedimaster.bot.pilot_walk import NO_CHANGE_LEFT
from jedimaster.bot.pilot_walk import NO_PILOT
from jedimaster.bot.pilot_walk import SAME_NAME
from jedimaster.bot.reply import Button
from jedimaster.launch.pilot_store import Pilot

logger = logging.getLogger(__name__)

NAMES = ("Mr. Webbin", "mr.webbin")


@pytest.fixture
def bot(tmp_path):
    return make_bot(tmp_path)


def labels(reply):
    return [b.label for b in reply.buttons]


def link_with_names(bot, user_id=HOST, names=NAMES):
    code = bot.linker.start()
    return bot.core.link(user_id, code, True, names)


# ---- the first link -------------------------------------------------------


def test_the_first_link_makes_the_pilot_and_offers_the_name(bot):
    reply = link_with_names(bot)
    assert reply.text == (
        "Linked. This launcher answers this account only.\n"
        "Your pilot is Mr. Webbin. Keep the name, or change it once."
    )
    assert labels(reply) == ["Keep name", "Change name"]
    assert bot.pilots.get(HOST) == Pilot(HOST, "Mr. Webbin", False)


def test_the_pilot_is_named_from_the_account_name_when_the_display_name_fits_nothing(
    bot,
):
    link_with_names(bot, names=("🔥🔥", "mr.webbin"))
    assert bot.pilots.get(HOST).name == "mr.webbin"


def test_a_link_that_fails_makes_no_pilot(bot):
    bot.linker.start()
    reply = bot.core.link(HOST, "WRONG-CODE", True, NAMES)
    assert reply.text.startswith("That code is not right")
    assert reply.buttons == ()
    assert bot.pilots.get(HOST) is None


def test_a_link_off_a_direct_message_makes_no_pilot(bot):
    code = bot.linker.start()
    assert bot.core.link(HOST, code, False, NAMES).text == DM_ONLY
    assert bot.pilots.get(HOST) is None


def test_linking_again_keeps_the_pilot_there_is(bot):
    bot.pilots.make(HOST, "Red Five")
    reply = link_with_names(bot)
    assert reply.text.endswith(
        "Your pilot is Red Five. Keep the name, or change it once."
    )
    assert bot.pilots.get(HOST).name == "Red Five"


def test_a_renamed_pilot_is_shown_without_buttons(bot):
    bot.pilots.make(HOST, "Red Five")
    bot.pilots.rename(HOST, "Red Six")
    reply = link_with_names(bot)
    assert reply.text.endswith("Your pilot is Red Six.")
    assert reply.buttons == ()


def test_a_bad_pilot_file_is_said_and_left_alone(bot):
    path = bot.pilots.path(HOST)
    path.parent.mkdir(parents=True)
    path.write_text("not json", encoding="utf-8")
    reply = link_with_names(bot)
    assert reply.text.endswith(BAD_FILE)
    assert reply.buttons == ()
    assert path.read_text(encoding="utf-8") == "not json"


# ---- the Pilot command ----------------------------------------------------


def test_the_pilot_command_makes_one_for_a_linked_account_without(bot):
    bot.link_account(HOST)
    assert bot.pilots.get(HOST) is None
    reply = bot.core.pilot(HOST, True, NAMES)
    assert reply.text == "Your pilot is Mr. Webbin. Keep the name, or change it once."
    assert bot.pilots.get(HOST).name == "Mr. Webbin"


def test_the_pilot_command_is_for_the_linked_account_in_a_direct_message(bot):
    link_with_names(bot)
    assert bot.core.pilot(STRANGER, True, NAMES).text == HOST_ONLY
    assert bot.core.pilot(HOST, False, NAMES).text == DM_ONLY
    assert bot.pilots.get(STRANGER) is None


# ---- keep, change, rename -------------------------------------------------


def test_keep_name_closes_the_offer_and_keeps_the_change(bot):
    link_with_names(bot)
    reply = bot.core.keep_name(HOST, True)
    assert reply.text == (
        "Your pilot is Mr. Webbin. The Pilot command can still change its name once."
    )
    assert reply.edit and reply.buttons == ()
    assert bot.pilots.get(HOST).renamed is False


def test_change_name_opens_the_window_at_the_name(bot):
    link_with_names(bot)
    reply = bot.core.change_name(HOST, True)
    window = reply.window
    assert (window.kind, window.title, window.value, window.max_length) == (
        "pilot_name",
        "Pilot name",
        "Mr. Webbin",
        12,
    )


def test_a_typed_name_is_the_one_change(bot):
    link_with_names(bot)
    reply = bot.core.rename_pilot(HOST, "  Red Five ", True)
    assert reply.text == "Your pilot is now Red Five. The name cannot change again."
    assert reply.edit and reply.buttons == ()
    assert bot.pilots.get(HOST) == Pilot(HOST, "Red Five", True)


def test_after_the_change_no_other_is_allowed(bot):
    link_with_names(bot)
    bot.core.rename_pilot(HOST, "Red Five", True)
    assert bot.core.change_name(HOST, True).text == NO_CHANGE_LEFT
    assert bot.core.change_name(HOST, True).window is None
    assert bot.core.rename_pilot(HOST, "Red Six", True).text == NO_CHANGE_LEFT
    assert bot.pilots.get(HOST).name == "Red Five"


@pytest.mark.parametrize("typed", ["Music Man", "Wedge Antilles", "!Ace", "   ", "Ünï"])
def test_a_name_off_the_rule_changes_nothing_and_keeps_the_buttons(bot, typed):
    link_with_names(bot)
    reply = bot.core.rename_pilot(HOST, typed, True)
    assert reply.text == BAD_NAME
    assert labels(reply) == ["Keep name", "Change name"]
    assert bot.pilots.get(HOST) == Pilot(HOST, "Mr. Webbin", False)


def test_the_same_name_changes_nothing(bot):
    link_with_names(bot)
    reply = bot.core.rename_pilot(HOST, "Mr. Webbin", True)
    assert reply.text == SAME_NAME
    assert labels(reply) == ["Keep name", "Change name"]
    assert bot.pilots.get(HOST).renamed is False


def test_the_steps_without_a_pilot_say_so(bot):
    bot.link_account(HOST)
    for reply in (
        bot.core.keep_name(HOST, True),
        bot.core.change_name(HOST, True),
        bot.core.rename_pilot(HOST, "Red Five", True),
    ):
        assert reply.text == NO_PILOT
    assert bot.pilots.get(HOST) is None


def test_the_steps_are_for_the_linked_account_in_a_direct_message(bot):
    link_with_names(bot)
    for user, dm, words in ((STRANGER, True, HOST_ONLY), (HOST, False, DM_ONLY)):
        assert bot.core.keep_name(user, dm).text == words
        assert bot.core.change_name(user, dm).text == words
        assert bot.core.rename_pilot(user, "Red Five", dm).text == words
    assert bot.pilots.get(HOST) == Pilot(HOST, "Mr. Webbin", False)


# ---- /status --------------------------------------------------------------


def test_status_names_the_pilot(bot):
    link_with_names(bot)
    assert bot.core.status(HOST).text.splitlines()[-1] == "Pilot: Mr. Webbin."


def test_status_says_when_there_is_no_pilot(bot):
    bot.link_account(HOST)
    assert bot.core.status(HOST).text.splitlines()[-1] == (
        "Pilot: none yet; the Pilot command makes one."
    )


# ---- through the door -----------------------------------------------------


@pytest.fixture
def door(bot):
    async def deliver(pushes):
        return 0

    return Door(bot.core, bot.store, deliver)


async def test_the_slash_link_makes_the_pilot_from_the_asker_s_names(bot, door):
    code = bot.linker.start()
    talk = Talk(HOST, display="José Álvarez", account="jose")
    await door.tree.get_command("link").callback(talk, code)
    assert bot.pilots.get(HOST).name == "Jose Alvarez"
    view = talk.said()[0][1]["view"]
    assert [b.label for b in view.children] == ["Keep name", "Change name"]


async def test_change_name_opens_the_window_as_the_first_answer(bot, door):
    link_with_names(bot)
    talk = Talk(HOST)
    await door.pressed(talk, Button("Change name", "pilot_change", ""))
    ((name, (window,), _),) = talk.response.calls
    assert name == "send_modal" and isinstance(window, TextWindow)
    assert window.text.default == "Mr. Webbin"
    assert window.text.max_length == 12


async def test_the_typed_name_renames_and_edits_the_message(bot, door):
    link_with_names(bot)
    opened = Talk(HOST)
    await door.pressed(opened, Button("Change name", "pilot_change", ""))
    window = opened.response.calls[0][1][0]
    window.text._value = "Red Five"
    submit = Talk(HOST)
    await window.on_submit(submit)
    ((name, _, kwargs),) = submit.response.calls
    assert name == "edit_message"
    assert (
        kwargs["content"] == "Your pilot is now Red Five. The name cannot change again."
    )
    assert bot.pilots.get(HOST).name == "Red Five"


async def test_keep_name_edits_the_message(bot, door):
    link_with_names(bot)
    talk = Talk(HOST)
    await door.pressed(talk, Button("Keep name", "pilot_keep", ""))
    ((name, _, kwargs),) = talk.response.calls
    assert name == "edit_message" and kwargs["view"] is None
    assert kwargs["content"].startswith("Your pilot is Mr. Webbin.")


async def test_a_button_of_another_kind_is_left_unanswered(bot, door, caplog):
    link_with_names(bot)
    talk = Talk(HOST)
    with caplog.at_level(logging.WARNING):
        await door.pressed(talk, Button("Odd", "odd_kind", ""))
    assert talk.response.calls == []
    assert "unknown kind 'odd_kind'" in caplog.text


async def test_the_pilot_file_is_readable_json_after_the_walk(bot, door):
    link_with_names(bot)
    data = json.loads(bot.pilots.path(HOST).read_text(encoding="utf-8"))
    assert data == {
        "format": 1,
        "account": str(HOST),
        "name": "Mr. Webbin",
        "renamed": False,
    }
