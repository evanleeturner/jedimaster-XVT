"""The Discord door: its commands, its menus and its login, against a stand-in.

Purpose:
    Prove that each slash command's real callback answers privately, calls
    ``core`` with the asker's id, defers when it reads files, shows menus,
    sends pushes to the open pages, that the command list syncs only when
    it changed, and that Discord refusing the token stops the bot cleanly.

Flow:
    A ``Door`` over a made-up install; the callbacks of its command tree
    are called with the stand-in of ``botdata``; discord.py's own network
    calls are replaced by recording functions.

Invariants:
    - No connection to Discord: ``Client.start`` and ``CommandTree.sync``
      are replaced wherever they would be called.
    - The one undocumented name used is ``discord.ui.select.selected_values``
      (a public module variable) to stand in for a person's pick.

Call:
    ``pytest tests/test_bot_door.py``
"""

from __future__ import annotations

import logging

import aiohttp
import discord
import pytest
from botdata import HOST
from botdata import make_bot
from botdata import STRANGER
from botdata import Talk
from discord.ui.select import selected_values
from listdata import crlf

from jedimaster.bot.core import HOST_ONLY
from jedimaster.bot.credentials import Token
from jedimaster.bot.discord_door import commands_digest
from jedimaster.bot.discord_door import Door
from jedimaster.bot.link import BotFile

logger = logging.getLogger(__name__)


@pytest.fixture
async def rig(tmp_path):
    """Return a linked bot, its door, and the list of pushes the door delivered."""
    bot = make_bot(tmp_path)
    bot.link_account(HOST)
    delivered: list[list[dict]] = []
    open_pages = [1]

    async def deliver(pushes):
        delivered.append(pushes)
        return open_pages[0]

    bot.door = Door(bot.core, bot.store, deliver)
    bot.delivered = delivered
    bot.open_pages = open_pages
    return bot


async def call(rig, name, talk, *args):
    command = rig.door.tree.get_command(name)
    assert command is not None, name
    await command.callback(talk, *args)


def test_the_door_asks_for_the_guilds_intent_only(rig):
    assert rig.door.client.intents.value == discord.Intents(guilds=True).value


def test_the_commands_are_the_five_in_the_table(rig):
    names = sorted(c.name for c in rig.door.tree.get_commands())
    assert names == ["link", "missions", "settings", "status", "unlink"]


def test_direct_message_commands_are_dm_only_and_the_others_anywhere(rig):
    for command in rig.door.tree.get_commands():
        contexts = command.allowed_contexts
        if command.name in ("link", "settings", "unlink"):
            assert (contexts.guild, contexts.dm_channel, contexts.private_channel) == (
                False,
                True,
                False,
            )
        else:
            assert (contexts.guild, contexts.dm_channel) == (True, True)


def test_the_commands_are_global_and_for_the_host_s_server_install(rig):
    payloads = [c.to_dict(rig.door.tree) for c in rig.door.tree.get_commands()]
    assert all("guild_id" not in p for p in payloads)
    assert all(p["integration_types"] == [0] for p in payloads)


def test_missions_takes_a_type_chosen_from_the_six(rig):
    payload = rig.door.tree.get_command("missions").to_dict(rig.door.tree)
    (option,) = payload["options"]
    assert option["name"] == "type"
    assert [c["value"] for c in option["choices"]] == [
        "training",
        "melee",
        "tournament",
        "combat",
        "battle",
        "campaign",
    ]


async def test_status_answers_at_once_and_privately(rig):
    talk = Talk(HOST)
    await call(rig, "status", talk)
    ((text, options),) = talk.said()
    assert text.startswith("Launcher version 0.1.0.")
    assert options == {"ephemeral": True}
    assert [name for name, _, _ in talk.response.calls] == ["send_message"]
    assert talk.followup.calls == []
    assert rig.delivered == []


async def test_status_works_in_a_server_too(rig):
    talk = Talk(HOST, in_server=True)
    await call(rig, "status", talk)
    assert talk.texts()[0].startswith("Launcher version")


@pytest.mark.parametrize(
    "name, args",
    [("status", ()), ("missions", ("training",)), ("settings", ()), ("unlink", ())],
)
async def test_a_stranger_gets_the_host_only_line_privately(rig, name, args):
    talk = Talk(STRANGER)
    await call(rig, name, talk, *args)
    assert talk.texts() == [HOST_ONLY]
    assert all(options.get("ephemeral") is True for _, options in talk.said())
    assert all("view" not in options for _, options in talk.said())
    assert (
        rig.bot_linked() == HOST
        if hasattr(rig, "bot_linked")
        else rig.linker.linked_user_id == HOST
    )


async def test_link_from_a_stranger_in_a_direct_message_with_the_code_links(tmp_path):
    bot = make_bot(tmp_path)

    async def deliver(pushes):
        return 0

    door = Door(bot.core, bot.store, deliver)
    code = bot.linker.start()
    talk = Talk(STRANGER)
    await door.tree.get_command("link").callback(talk, code.lower())
    assert talk.texts()[0].startswith("Linked")
    assert talk.said()[0][1] == {"ephemeral": True}
    assert bot.linker.linked_user_id == STRANGER


async def test_link_in_a_server_is_refused(tmp_path):
    bot = make_bot(tmp_path)

    async def deliver(pushes):
        return 0

    door = Door(bot.core, bot.store, deliver)
    code = bot.linker.start()
    talk = Talk(STRANGER, in_server=True)
    await door.tree.get_command("link").callback(talk, code)
    assert "direct message" in talk.texts()[0]
    assert bot.linker.linked_user_id is None


async def test_missions_defers_first_then_follows_up_with_menus(rig):
    talk = Talk(HOST)
    await call(rig, "missions", talk, "training")
    assert [name for name, _, _ in talk.response.calls] == ["defer"]
    assert talk.response.calls[0][2] == {"ephemeral": True}
    ((args, options),) = talk.followup.calls
    assert options["ephemeral"] is True
    (select,) = options["view"].children
    assert [o.value for o in select.options] == ["1", "2", "3"]
    assert select.placeholder == "Training missions 1 to 3"
    assert [o.description for o in select.options] == [
        "alpha.tie",
        "bravo.tie, not available",
        "charlie.tie",
    ]
    assert "pick one" in args[0]


async def test_settings_answers_at_once_with_a_menu_per_setting(rig):
    talk = Talk(HOST)
    await call(rig, "settings", talk)
    assert [name for name, _, _ in talk.response.calls] == ["send_message"]
    ((_, options),) = talk.said()
    (select,) = options["view"].children
    assert [(o.label, o.default) for o in select.options] == [
        ("Whole pixels", True),
        ("Engine fit", False),
        ("Sharp bilinear", False),
    ]


async def test_settings_in_a_server_is_refused(rig):
    talk = Talk(HOST, in_server=True)
    await call(rig, "settings", talk)
    assert "direct message" in talk.texts()[0]


async def test_unlink_in_a_direct_message_unlinks(rig):
    talk = Talk(HOST)
    await call(rig, "unlink", talk)
    assert talk.texts()[0].startswith("Unlinked")
    assert rig.linker.linked_user_id is None


async def test_a_picked_setting_changes_it_and_reaches_the_pages(rig):
    shown = Talk(HOST)
    await call(rig, "settings", shown)
    (select,) = shown.said()[0][1]["view"].children
    talk = Talk(HOST)
    marker = selected_values.set({select.custom_id: ["engine_fit"]})
    try:
        await select.callback(talk)
    finally:
        selected_values.reset(marker)
    assert talk.texts() == ["Art scaling set to Engine fit."]
    assert rig.control.store.get() == {"art_scaling": "engine_fit"}
    assert rig.delivered == [
        [
            {
                "event": "settings.changed",
                "data": {"settings": {"art_scaling": "engine_fit"}},
            }
        ]
    ]
    assert [name for name, _, _ in talk.response.calls] == ["send_message"]


async def test_a_picked_mission_defers_shows_it_and_reaches_the_pages(rig):
    shown = Talk(HOST)
    await call(rig, "missions", shown, "training")
    (select,) = shown.followup.calls[0][1]["view"].children
    talk = Talk(HOST)
    marker = selected_values.set({select.custom_id: ["2"]})
    try:
        await select.callback(talk)
    finally:
        selected_values.reset(marker)
    assert [name for name, _, _ in talk.response.calls] == ["defer"]
    assert talk.texts() == ["Showing Second Flight on the page."]
    ((push,),) = rig.delivered
    assert push["data"] == {
        "mission_type": "training",
        "id": 2,
        "title": "Second Flight",
    }


async def test_a_pick_from_the_second_menu_uses_that_menus_choice(rig):
    lines = ["[Open]"]
    for n in range(30):
        lines += [str(n + 1), f"M{n}.TIE", f"Mission {n}"]
    (rig.folder / "XvT" / "Melee" / "MISSION.LST").write_bytes(crlf(*lines))
    shown = Talk(HOST)
    await call(rig, "missions", shown, "melee")
    first, second = shown.followup.calls[0][1]["view"].children
    assert (len(first.options), len(second.options)) == (25, 5)
    talk = Talk(HOST)
    marker = selected_values.set({second.custom_id: ["28"]})
    try:
        await second.callback(talk)
    finally:
        selected_values.reset(marker)
    assert talk.texts()[0].startswith("Showing Mission 27 on the page.")


async def test_with_no_page_open_the_answer_says_so(rig):
    rig.open_pages[0] = 0
    shown = Talk(HOST)
    await call(rig, "settings", shown)
    (select,) = shown.said()[0][1]["view"].children
    talk = Talk(HOST)
    marker = selected_values.set({select.custom_id: ["sharp_bilinear"]})
    try:
        await select.callback(talk)
    finally:
        selected_values.reset(marker)
    assert talk.texts() == [
        "Art scaling set to Sharp bilinear. No page is open, so nothing changed on screen."
    ]
    assert rig.control.store.get() == {"art_scaling": "sharp_bilinear"}


async def test_a_pick_from_a_stranger_changes_nothing(rig):
    picker = rig.core.settings(HOST, True).pickers[0]
    talk = Talk(STRANGER)
    await rig.door.picked(talk, picker, ["engine_fit"])
    assert talk.texts() == [HOST_ONLY]
    assert rig.control.store.get() == {"art_scaling": "whole_pixels"}
    assert rig.delivered == []


async def test_an_empty_pick_does_nothing(rig):
    picker = rig.core.settings(HOST, True).pickers[0]
    talk = Talk(HOST)
    await rig.door.picked(talk, picker, [])
    assert talk.said() == [] and talk.response.calls == []


async def test_a_menu_is_built_with_a_limited_life(rig):
    talk = Talk(HOST)
    await call(rig, "settings", talk)
    view = talk.said()[0][1]["view"]
    assert view.timeout == 300


# ---- the command list -----------------------------------------------------


async def test_the_list_syncs_when_new_and_not_again_while_unchanged(
    rig, monkeypatch, caplog
):
    syncs: list[int] = []

    async def fake_sync(*args, **kwargs):
        syncs.append(1)
        return list(rig.door.tree.get_commands())

    monkeypatch.setattr(rig.door.tree, "sync", fake_sync)
    with caplog.at_level(logging.INFO):
        assert await rig.door.sync_if_changed() is True
        assert await rig.door.sync_if_changed() is False
    assert syncs == [1]
    assert "synced 5 commands" in caplog.text
    assert rig.store.commands_digest == commands_digest(rig.door.tree)


async def test_a_changed_list_syncs_again(rig, monkeypatch):
    syncs: list[int] = []

    async def fake_sync(*args, **kwargs):
        syncs.append(1)
        return []

    monkeypatch.setattr(rig.door.tree, "sync", fake_sync)
    rig.store.commands_digest = "an older list"
    assert await rig.door.sync_if_changed() is True
    assert syncs == [1]


async def test_the_digest_is_kept_between_runs(rig, monkeypatch, tmp_path):
    async def fake_sync(*args, **kwargs):
        return []

    monkeypatch.setattr(rig.door.tree, "sync", fake_sync)
    await rig.door.sync_if_changed()
    assert BotFile(rig.store.path).commands_digest == commands_digest(rig.door.tree)


def test_the_digest_follows_the_payload(rig):
    before = commands_digest(rig.door.tree)
    assert before == commands_digest(rig.door.tree)
    rig.door.tree.get_command("status").description = "Another description"
    changed = commands_digest(rig.door.tree)
    assert changed != before
    rig.door.tree.get_command("status").description = "Show the launcher's status"
    assert commands_digest(rig.door.tree) == before
    rig.door.tree.remove_command("status")
    assert commands_digest(rig.door.tree) != before


async def test_the_setup_hook_syncs_the_commands(rig, monkeypatch):
    syncs: list[int] = []

    async def fake_sync(*args, **kwargs):
        syncs.append(1)
        return []

    monkeypatch.setattr(rig.door.tree, "sync", fake_sync)
    await rig.door.client.setup_hook()
    assert syncs == [1]


async def test_the_bot_says_it_started_with_its_name(rig, caplog):
    with caplog.at_level(logging.INFO):
        await rig.door.client.on_ready()
    assert "bot started as" in caplog.text


# ---- the login ------------------------------------------------------------


async def test_a_refused_token_logs_an_error_without_it_and_returns(
    rig, monkeypatch, caplog
):
    secret = "made-up.TOKEN_value-12345"

    async def refuse(self, token, *, reconnect=True):
        assert token == secret
        raise discord.LoginFailure("Improper token has been passed.")

    monkeypatch.setattr(discord.Client, "start", refuse)
    with caplog.at_level(logging.DEBUG):
        await rig.door.run(Token(secret))
    errors = [r for r in caplog.records if r.levelno == logging.ERROR]
    assert len(errors) == 1 and "refused the bot's token" in errors[0].getMessage()
    assert secret not in caplog.text


async def test_any_other_discord_error_stops_the_bot_and_names_only_its_type(
    rig, monkeypatch, caplog
):
    async def fail(self, token, *, reconnect=True):
        raise discord.HTTPException(response=_Response(), message=f"bad {token}")

    monkeypatch.setattr(discord.Client, "start", fail)
    with caplog.at_level(logging.ERROR):
        await rig.door.run(Token("made-up.TOKEN_value-12345"))
    assert "HTTPException" in caplog.text and "made-up" not in caplog.text


@pytest.mark.parametrize(
    "error",
    [aiohttp.ClientConnectionError("cannot connect"), ConnectionRefusedError(111, "x")],
)
async def test_an_unreachable_discord_stops_the_bot_with_an_error(
    rig, monkeypatch, caplog, error
):
    async def unreachable(self, token, *, reconnect=True):
        raise error

    monkeypatch.setattr(discord.Client, "start", unreachable)
    with caplog.at_level(logging.DEBUG):
        await rig.door.run(Token("made-up.TOKEN_value-12345"))
    errors = [r for r in caplog.records if r.levelno == logging.ERROR]
    assert len(errors) == 1 and "cannot reach Discord" in errors[0].getMessage()
    assert type(error).__name__ in errors[0].getMessage()
    assert "made-up" not in caplog.text


class _Response:
    status = 500
    reason = "x"
