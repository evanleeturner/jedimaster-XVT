"""The right-click items, their slash twins, the walk and the battle's end, at the door.

Purpose:
    Prove the registry table is the one source of both kinds of command; that
    for every row the right-click item and its slash twin make the same
    ``Core`` call with the same arguments and the same reply; that the person
    right-clicked is never an input; that the Single player walk edits one
    message and Launch starts the battle; that the options menus arrive as two
    messages; that the Link item asks for the code in a window; and that a
    battle's end goes to the linked account by direct message, a refused
    message being a warning only.

Flow:
    A ``Door`` over a linked bot with the stand-in engine; callbacks of the
    command tree are called with the stand-in of ``botdata``; ``Core`` methods
    are wrapped to record each call; discord.py's network calls are replaced.

Invariants:
    - No connection to Discord.
    - Private names touched: ``TextInput._value`` (to type into the Link
      window) and ``discord.ui.select.selected_values`` (a public variable).

Call:
    ``pytest tests/test_bot_menus.py``
"""

from __future__ import annotations

import asyncio
import logging

import discord
import pytest
from botdata import HOST
from botdata import make_bot
from botdata import STRANGER
from botdata import Talk
from discord.ui.select import selected_values

from jedimaster.bot.discord_door import Door
from jedimaster.bot.registry import ITEMS
from jedimaster.bot.registry import MAX_USER_COMMANDS

logger = logging.getLogger(__name__)

OTHER = 424242


@pytest.fixture
def rig(tmp_path):
    return build(tmp_path)


def build(tmp_path):
    """Return a linked bot with a door, a record of every ``Core`` call and pushes."""
    bot = make_bot(tmp_path, "done")
    bot.link_account(HOST)
    bot.delivered = []

    async def deliver(pushes):
        bot.delivered.append(pushes)
        return 1

    bot.door = Door(bot.core, bot.store, deliver)
    bot.calls = []
    for item in ITEMS:
        real = getattr(bot.core, item.method)

        def spy(*args, _name=item.method, _real=real):
            bot.calls.append((_name, args))
            return _real(*args)

        setattr(bot.core, item.method, spy)
    return bot


def person(user_id):
    return discord.Object(id=user_id)


async def slash(rig, item, talk, *args):
    command = rig.door.tree.get_command(item.slash)
    await command.callback(talk, *args)


async def right_click(rig, item, talk, who):
    command = rig.door.tree.get_command(item.title, type=discord.AppCommandType.user)
    await command.callback(talk, who)


def shape(talk):
    """Return what a talk said, with views reduced to their menus and buttons."""
    out = []
    for text, options in talk.said():
        view = options.get("view")
        items = []
        for child in view.children if view else []:
            if isinstance(child, discord.ui.Select):
                items.append((child.placeholder, [o.value for o in child.options]))
            else:
                items.append((child.label,))
        out.append((text, options.get("ephemeral"), tuple(items)))
    return out


def typed_into(modal, text):
    modal.code._value = text


# ---- the table ------------------------------------------------------------


def test_the_table_has_the_eight_rows_in_order():
    assert [(i.title, i.method, i.where, i.slash) for i in ITEMS] == [
        ("Single player", "solo", "anywhere", "solo"),
        ("Game options", "options", "anywhere", "options"),
        ("Launcher settings", "settings", "dm", "settings"),
        ("Show mission", "missions", "anywhere", "missions"),
        ("Status", "status", "anywhere", "status"),
        ("Link", "link", "dm", "link"),
        ("Unlink", "unlink", "dm", "unlink"),
        ("Pilot", "pilot", "dm", "pilot"),
    ]
    assert len(ITEMS) <= MAX_USER_COMMANDS == 15


def test_where_a_row_may_be_used_follows_the_table(rig):
    for item in ITEMS:
        for command in (
            rig.door.tree.get_command(item.slash),
            rig.door.tree.get_command(item.title, type=discord.AppCommandType.user),
        ):
            contexts = command.allowed_contexts
            assert contexts.dm_channel is True
            assert contexts.guild is (item.where != "dm"), item.title
            assert contexts.private_channel is False


def test_both_kinds_are_global_and_for_the_host_s_server_install(rig):
    for command in rig.door.tree.get_commands():
        payload = command.to_dict(rig.door.tree)
        assert "guild_id" not in payload
        assert payload["integration_types"] == [0]


def test_a_right_click_item_takes_no_options_and_has_no_description(rig):
    for item in ITEMS:
        command = rig.door.tree.get_command(
            item.title, type=discord.AppCommandType.user
        )
        payload = command.to_dict(rig.door.tree)
        assert payload["type"] == 2 and "options" not in payload
        assert payload.get("description", "") == ""


def test_the_tree_holds_more_than_discord_py_s_own_five_user_commands(rig):
    users = rig.door.tree.get_commands(type=discord.AppCommandType.user)
    assert len(users) == 8


def test_the_sixteenth_user_command_is_refused(rig):
    from discord import app_commands

    async def nothing(interaction, who: discord.User):
        return None

    for n in range(MAX_USER_COMMANDS - 8):
        rig.door.tree.add_command(
            app_commands.ContextMenu(
                name=f"Extra {n}", callback=nothing, type=discord.AppCommandType.user
            )
        )
    with pytest.raises(app_commands.CommandLimitReached):
        rig.door.tree.add_command(
            app_commands.ContextMenu(
                name="One more", callback=nothing, type=discord.AppCommandType.user
            )
        )


# ---- the twins --------------------------------------------------------------


@pytest.mark.parametrize(
    "item",
    [i for i in ITEMS if i.method not in ("link", "unlink")],
    ids=lambda i: i.method,
)
@pytest.mark.parametrize("who", [HOST, OTHER], ids=["on oneself", "on another"])
async def test_a_right_click_and_its_slash_twin_make_the_same_call_and_reply(
    rig, item, who
):
    in_server = item.where != "dm"
    twin, click = Talk(HOST, in_server=in_server), Talk(HOST, in_server=in_server)
    await slash(rig, item, twin)
    await right_click(rig, item, click, person(who))
    (first,), (second,) = [[c] for c in rig.calls]
    assert first == second
    assert shape(twin) == shape(click)
    assert [n for n, *_ in twin.response.calls] == [n for n, *_ in click.response.calls]


async def test_unlink_by_right_click_and_by_slash_make_the_same_call_and_reply(
    tmp_path,
):
    (tmp_path / "a").mkdir()
    (tmp_path / "b").mkdir()
    one, two = build(tmp_path / "a"), build(tmp_path / "b")
    unlink = next(i for i in ITEMS if i.method == "unlink")
    twin, click = Talk(HOST), Talk(HOST)
    await slash(one, unlink, twin)
    await right_click(two, unlink, click, person(OTHER))
    assert one.calls == two.calls == [("unlink", (HOST, True))]
    assert shape(twin) == shape(click)
    assert one.linker.linked_user_id is None and two.linker.linked_user_id is None


async def test_the_person_clicked_is_not_in_any_call(rig):
    for item in ITEMS:
        if item.method == "link":
            continue
        await right_click(
            rig, item, Talk(HOST, in_server=item.where != "dm"), person(OTHER)
        )
    assert rig.calls and all(OTHER not in args for _, args in rig.calls)
    assert all(args[0] == HOST for _, args in rig.calls)


async def test_the_person_clicked_is_logged_at_debug_only(rig, caplog):
    with caplog.at_level(logging.DEBUG, logger="jedimaster.bot.discord_door"):
        await right_click(rig, ITEMS[4], Talk(HOST), person(OTHER))
    assert any(
        str(OTHER) in r.message and r.levelno == logging.DEBUG for r in caplog.records
    )
    assert not any(
        str(OTHER) in r.message for r in caplog.records if r.levelno > logging.DEBUG
    )


async def test_a_stranger_is_refused_through_a_right_click_as_through_a_slash(rig):
    for item in ITEMS:
        if item.method == "link":
            continue
        twin, click = (
            Talk(STRANGER, in_server=item.where != "dm"),
            Talk(STRANGER, in_server=item.where != "dm"),
        )
        await slash(rig, item, twin)
        await right_click(rig, item, click, person(HOST))
        assert shape(twin) == shape(click)
        assert shape(click)[0][0].startswith("This launcher answers its host only")


async def test_link_by_slash_and_by_the_window_make_the_same_call_and_reply(rig):
    link = next(i for i in ITEMS if i.method == "link")
    twin = Talk(STRANGER)
    await slash(rig, link, twin, "ABCD-EFGH")
    click = Talk(STRANGER)
    await right_click(rig, link, click, person(HOST))
    ((name, (modal,), _),) = click.response.calls
    assert name == "send_modal" and modal.children == [modal.code]
    typed_into(modal, "ABCD-EFGH")
    submit = Talk(STRANGER)
    await modal.on_submit(submit)
    first, second = rig.calls
    assert (
        first
        == second
        == (
            "link",
            (STRANGER, "ABCD-EFGH", True, ("Mr. Webbin", "mr.webbin")),
        )
    )
    assert shape(twin) == shape(submit)


async def test_the_window_does_not_answer_until_the_code_is_sent(rig):
    link = next(i for i in ITEMS if i.method == "link")
    click = Talk(STRANGER)
    await right_click(rig, link, click, person(HOST))
    assert rig.calls == [] and click.texts() == []


async def test_the_link_code_is_in_no_log_line(rig, caplog):
    link = next(i for i in ITEMS if i.method == "link")
    with caplog.at_level(logging.DEBUG):
        await slash(rig, link, Talk(STRANGER), "SECRET-CODE")
        click = Talk(STRANGER)
        await right_click(rig, link, click, person(HOST))
        modal = click.response.calls[0][1][0]
        typed_into(modal, "SECRET-CODE")
        await modal.on_submit(Talk(STRANGER))
    assert "SECRET-CODE" not in caplog.text


async def test_show_mission_by_right_click_offers_the_kinds_then_the_missions(rig):
    missions = next(i for i in ITEMS if i.method == "missions")
    talk = Talk(HOST)
    await right_click(rig, missions, talk, person(OTHER))
    assert [n for n, *_ in talk.response.calls] == ["defer"]
    ((_, options),) = talk.said()
    (select,) = options["view"].children
    assert [o.label for o in select.options][:2] == ["Training", "Melee"]
    pick = Talk(HOST)
    marker = selected_values.set({select.custom_id: ["training"]})
    try:
        await select.callback(pick)
    finally:
        selected_values.reset(marker)
    assert rig.calls[-1] == ("missions", (HOST, "training", True)) or rig.calls[-1][1][
        :2
    ] == (HOST, "training")
    assert "Training missions: pick one" in pick.texts()[0]


def test_missions_slash_takes_an_optional_choice_of_the_six(rig):
    payload = rig.door.tree.get_command("missions").to_dict(rig.door.tree)
    (option,) = payload["options"]
    assert option["name"] == "type" and option.get("required", False) is False


# ---- the walk ---------------------------------------------------------------


async def choose(select, value, talk):
    marker = selected_values.set({select.custom_id: [value]})
    try:
        await select.callback(talk)
    finally:
        selected_values.reset(marker)


async def test_the_whole_walk_edits_one_message_and_launch_starts_the_battle(rig):
    start = Talk(HOST)
    await right_click(rig, ITEMS[0], start, person(OTHER))
    ((text, options),) = start.said()
    assert text == "Single player: pick a kind of battle."
    (kind,) = options["view"].children
    step = Talk(HOST)
    await choose(kind, "training", step)
    assert [n for n, *_ in step.response.calls] == ["defer"]
    (side,) = step.edits[-1]["view"].children
    step = Talk(HOST)
    await choose(side, "rebel", step)
    (mission,) = step.edits[-1]["view"].children
    step = Talk(HOST)
    await choose(mission, "alpha.tie", step)
    (pilot,) = step.edits[-1]["view"].children
    step = Talk(HOST)
    await choose(pilot, "Ace", step)
    (launch,) = step.edits[-1]["view"].children
    assert launch.label == "Launch"
    press = Talk(HOST)
    await launch.callback(press)
    assert [n for n, *_ in press.response.calls] == ["defer"]
    assert (
        press.edits[-1]["content"]
        == "Started First Flight as Ace. Watch it on the page."
    )
    assert press.edits[-1]["view"] is None, "the button goes once the battle starts"
    ((push,),) = rig.delivered
    assert push["event"] == "launch.started" and push["data"]["pilot"] == "Ace"
    rig.control.battles.cancel(push["data"]["launch_id"])
    await asyncio.sleep(0.3)


async def test_launch_refused_leaves_the_button_and_says_why(rig):
    rig.control.pages_open = lambda: 0
    press = Talk(HOST)
    from jedimaster.bot.reply import Button

    await rig.door.pressed(
        press, Button("Launch", "solo_launch", "training|rebel|alpha.tie|Ace")
    )
    ((text, options),) = press.said()
    assert text.startswith("Open the launcher's page first")
    assert press.edits == [] and options["ephemeral"] is True


async def test_a_step_by_another_account_is_refused_privately(rig):
    kind = Talk(STRANGER)
    await rig.door.picked(kind, next(iter(rig.core.solo(HOST).pickers)), ["melee"])
    assert kind.texts()[0].startswith("This launcher answers its host only")


# ---- the options -------------------------------------------------------------


async def test_options_arrive_as_two_messages_and_a_pick_sets_one(rig):
    talk = Talk(HOST)
    await slash(rig, ITEMS[1], talk)
    ((first, options), (second, more)) = talk.said()
    assert len(options["view"].children) == 5 and len(more["view"].children) == 2
    assert first.startswith("Game options")
    assert more["ephemeral"] is True
    select = options["view"].children[0]
    pick = Talk(HOST)
    await choose(select, "hard", pick)
    assert pick.texts() == ["Difficulty set to Hard."]
    assert rig.control.store.get_options(str(HOST)) == {"difficulty": "hard"}
    assert rig.delivered[-1][0]["event"] == "options.changed"


# ---- the battle's end ----------------------------------------------------------


ENDED = {
    "event": "launch.ended",
    "data": {
        "launch_id": 3,
        "status": "flown",
        "line": "flown: completed, score 10, kills 2, losses 0, goals met at 61 s",
    },
}


class Person:
    def __init__(self, refuse=None):
        self.sent = []
        self.refuse = refuse

    async def send(self, text):
        if self.refuse:
            raise self.refuse
        self.sent.append(text)


async def test_the_end_goes_to_the_linked_account_as_one_direct_message(
    rig, monkeypatch
):
    who = Person()
    asked = []

    async def fetch(user_id):
        asked.append(user_id)
        return who

    monkeypatch.setattr(rig.door.client, "fetch_user", fetch)
    await rig.door.on_push(ENDED)
    assert asked == [HOST] and who.sent == [ENDED["data"]["line"]]


async def test_the_end_reaches_the_door_through_the_control_s_listeners(
    rig, monkeypatch
):
    who = Person()

    async def fetch(user_id):
        return who

    monkeypatch.setattr(rig.door.client, "fetch_user", fetch)
    await rig.control._publish(ENDED)
    assert len(who.sent) == 1


async def test_a_launch_from_a_server_channel_still_ends_in_the_direct_message(
    rig, monkeypatch
):
    who = Person()

    async def fetch(user_id):
        return who

    monkeypatch.setattr(rig.door.client, "fetch_user", fetch)
    start = Talk(HOST, in_server=True)
    await rig.door.pressed(
        start,
        __import__("jedimaster.bot.reply", fromlist=["Button"]).Button(
            "Launch", "solo_launch", "training|rebel|alpha.tie|Ace"
        ),
    )
    rig.control.battles.cancel(1)
    for _ in range(100):
        if who.sent:
            break
        await asyncio.sleep(0.05)
    assert who.sent == ["cancelled"]


async def test_other_pushes_and_an_unlinked_bot_send_nothing(rig, monkeypatch):
    who = Person()

    async def fetch(user_id):
        return who

    monkeypatch.setattr(rig.door.client, "fetch_user", fetch)
    await rig.door.on_push({"event": "launch.started", "data": {}})
    rig.linker.unlink()
    await rig.door.on_push(ENDED)
    assert who.sent == []


@pytest.mark.parametrize(
    "refusal",
    [
        discord.Forbidden(
            type("R", (), {"status": 403, "reason": "x"})(),
            "Cannot send messages to this user",
        ),
        discord.HTTPException(
            type("R", (), {"status": 500, "reason": "x"})(), "server error"
        ),
        RuntimeError("not logged in"),
    ],
    ids=["forbidden", "http", "not logged in"],
)
async def test_a_refused_message_is_a_warning_and_never_a_crash(
    rig, monkeypatch, caplog, refusal
):
    async def fetch(user_id):
        return Person(refuse=refusal)

    monkeypatch.setattr(rig.door.client, "fetch_user", fetch)
    with caplog.at_level(logging.INFO):
        await rig.door.on_push(ENDED)
    warnings = [r for r in caplog.records if r.levelno == logging.WARNING]
    assert len(warnings) == 1 and str(HOST) in warnings[0].message
    assert "Cannot send" not in warnings[0].message, "only the error's type is named"
