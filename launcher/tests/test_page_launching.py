"""The launch and option commands: their answers, their refusals, their checks.

Purpose:
    Prove ``launch.menu``, ``pilots.list``, ``launch.solo``, ``launch.cancel``,
    ``options.get`` and ``options.set`` answer as the control list says; that
    a launch is refused in plain words and in the order the module promises;
    that every value is checked again whatever the sender claimed; that the
    description holds the player's stored options; and that every reply and
    push satisfies the schema.

Flow:
    A ``Control`` over the made-up solo install with the stand-in engine;
    ``handle`` called with request text; replies checked against the schema;
    the description the engine received is read back.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_page_launching.py``
"""

from __future__ import annotations

import asyncio
import json
import logging

import pytest
from pagedata import check_valid
from pagedata import request
from solodata import make_control
from solodata import make_solo_install
from solodata import PLAYER
from solodata import result_of
from solodata import solo_args

from jedimaster.page.control import Control
from jedimaster.page.launching import BUSY
from jedimaster.page.launching import NO_ENGINE
from jedimaster.page.launching import NO_INSTALL
from jedimaster.page.launching import NO_MISSION
from jedimaster.page.launching import NO_PAGE
from jedimaster.page.launching import NO_PILOT
from jedimaster.page.launching import NO_PILOT_FOLDER
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)


def ask(control, command, args=None, ident=1):
    outcome = control.handle(request(ident, command, args))
    return check_valid(outcome.reply), outcome.pushes


def refusal(control, **changes):
    reply, pushes = ask(control, "launch.solo", solo_args(**changes))
    result = result_of(reply)
    assert result["started"] is False and result["launch_id"] == 0
    assert pushes == []
    return result["message"]


async def finish(control):
    for _ in range(300):
        if not control.battles.busy():
            return
        await asyncio.sleep(0.05)
    raise AssertionError("the battle did not end")


def test_launch_menu_answers_with_the_solo_list(tmp_path):
    control, _ = make_control(tmp_path)
    reply, _ = ask(
        control, "launch.menu", {"mission_type": "training", "side": "rebel"}
    )
    result = result_of(reply)
    assert result["resolved"] is True
    assert [e["file"] for e in result["entries"]] == ["alpha.tie", "charlie.tie"]


def test_launch_menu_refuses_a_side_that_does_not_fit(tmp_path):
    control, _ = make_control(tmp_path)
    reply, _ = ask(control, "launch.menu", {"mission_type": "melee", "side": "rebel"})
    assert reply["error"]["code"] == "bad_arguments"
    assert "no side" in reply["error"]["message"]


def test_pilots_list_answers_with_the_folders_pilots(tmp_path):
    control, _ = make_control(tmp_path)
    reply, _ = ask(control, "pilots.list")
    assert result_of(reply) == {"found": True, "pilots": ["Ace", "Bravo Two", "Cee"]}


def test_pilots_list_says_not_found_without_a_game_program(tmp_path):
    control = Control(
        make_solo_install(tmp_path), SettingsStore(tmp_path / "s.json"), "0.1.0"
    )
    reply, _ = ask(control, "pilots.list")
    assert result_of(reply) == {"found": False, "pilots": []}


def test_the_refusals_come_in_the_promised_order(tmp_path):
    control = Control(
        None, SettingsStore(tmp_path / "s.json"), "0.1.0"
    )  # no engine, no page, no install
    assert refusal(control) == NO_ENGINE
    control, flight = make_control(tmp_path)
    control.pages_open = lambda: 0
    control.install = control.launching.install = None
    assert refusal(control) == NO_PAGE
    control.pages_open = lambda: 1
    assert refusal(control) == NO_INSTALL


async def test_busy_is_refused_before_no_page(tmp_path):
    control, _ = make_control(tmp_path, "hang")
    ask(control, "launch.solo", solo_args())
    control.pages_open = lambda: 0
    assert refusal(control) == BUSY
    control.battles.cancel(1)
    await finish(control)


def test_the_no_page_reason_names_the_page_and_its_cancel_button(tmp_path):
    control, _ = make_control(tmp_path)
    control.pages_open = lambda: 0
    message = refusal(control)
    assert message == NO_PAGE
    assert message == (
        "Open the launcher's page first: it shows each battle with a Cancel button."
    )


def test_the_no_engine_reason_is_the_one_in_words(tmp_path):
    control = Control(
        make_solo_install(tmp_path), SettingsStore(tmp_path / "s.json"), "0.1.0"
    )
    control.pages_open = lambda: 1
    assert refusal(control) == "This launcher was started without the game program."


def test_a_mission_off_the_list_is_refused(tmp_path):
    control, _ = make_control(tmp_path)
    for mission in ("bravo.tie", "nothing.tie", "../x", "charlie.tie.bak"):
        assert refusal(control, mission=mission) == NO_MISSION


def test_a_mission_of_another_kinds_list_is_refused(tmp_path):
    control, _ = make_control(tmp_path)
    assert refusal(control, mission="delta.tie") == NO_MISSION
    assert refusal(control, side="imperial") == NO_MISSION


def test_a_pilot_the_game_does_not_have_is_refused(tmp_path):
    control, _ = make_control(tmp_path)
    assert refusal(control, pilot="Nobody") == NO_PILOT


def test_a_pilot_folder_that_cannot_be_read_is_refused(tmp_path):
    control, flight = make_control(tmp_path)
    for plt in flight.pilot_folder.iterdir():
        plt.unlink()
    flight.pilot_folder.rmdir()
    assert refusal(control) == NO_PILOT_FOLDER


def test_a_side_that_does_not_fit_the_kind_is_a_bad_argument(tmp_path):
    control, _ = make_control(tmp_path)
    reply, _ = ask(
        control, "launch.solo", solo_args(mission_type="melee", side="rebel")
    )
    assert reply["error"]["code"] == "bad_arguments"


@pytest.mark.parametrize(
    "changes",
    [
        {"player": "abc"},
        {"player": "1" * 21},
        {"player": ""},
        {"mission_type": "arena", "side": "none"},
        {"side": "neutral"},
        {"mission": ""},
        {"mission": "x" * 65},
        {"pilot": "x" * 13},
    ],
)
def test_a_launch_with_a_bad_argument_is_refused_by_the_list(tmp_path, changes):
    control, _ = make_control(tmp_path)
    reply, _ = ask(control, "launch.solo", solo_args(**changes))
    assert reply["error"]["code"] == "bad_arguments"


def test_no_path_or_program_can_be_sent_in_a_launch(tmp_path):
    control, _ = make_control(tmp_path)
    extra = {**solo_args(), "engine": "/bin/true"}
    reply, _ = ask(control, "launch.solo", extra)
    assert reply["error"]["code"] == "bad_arguments"


async def test_the_description_holds_the_mission_the_pilot_and_the_players_options(
    tmp_path,
):
    copy = tmp_path / "copy.json"
    control, _ = make_control(tmp_path, "done", f"--copy={copy}")
    ask(
        control,
        "options.set",
        {"player": PLAYER, "name": "difficulty", "value": "hard"},
    )
    ask(
        control, "options.set", {"player": PLAYER, "name": "collisions", "value": "off"}
    )
    ask(control, "options.set", {"player": "9", "name": "difficulty", "value": "easy"})
    reply, pushes = ask(control, "launch.solo", solo_args(pilot="bravo two"))
    await finish(control)
    description = json.loads(copy.read_text())
    assert description["game_type"] == "training" and description["side"] == "rebel"
    assert description["mission"] == "train/alpha.tie"
    assert description["pilot"] == "Bravo Two", (
        "the folder's spelling, not the sender's"
    )
    assert description["settings"] == {"difficulty": "hard", "collisions": False}
    assert (
        result_of(reply)["message"]
        == "Started First Flight as Bravo Two. Watch it on the page."
    )
    assert pushes[0]["data"]["title"] == "First Flight"


async def test_no_options_set_means_no_settings_in_the_description(tmp_path):
    copy = tmp_path / "copy.json"
    control, _ = make_control(tmp_path, "done", f"--copy={copy}")
    ask(control, "launch.solo", solo_args())
    await finish(control)
    assert "settings" not in json.loads(copy.read_text())


async def test_the_flights_ai_program_rides_in_the_description(tmp_path):
    program = {"python": "/usr/bin/python3", "path": "/srv/ai/jedimaster_ai"}
    for root, given in ((tmp_path / "with", program), (tmp_path / "without", None)):
        root.mkdir()
        copy = root / "copy.json"
        control, _ = make_control(root, "done", f"--copy={copy}", ai_program=given)
        ask(control, "launch.solo", solo_args())
        await finish(control)
        assert json.loads(copy.read_text()).get("ai_program") == given


async def test_an_easy_cheat_outside_a_campaign_is_sent_as_easy_and_the_answer_says_so(
    tmp_path,
):
    copy = tmp_path / "copy.json"
    control, _ = make_control(tmp_path, "done", f"--copy={copy}")
    ask(
        control,
        "options.set",
        {"player": PLAYER, "name": "difficulty", "value": "easy_cheat"},
    )
    reply, _ = ask(control, "launch.solo", solo_args())
    await finish(control)
    assert json.loads(copy.read_text())["settings"] == {"difficulty": "easy"}
    assert result_of(reply)["message"].endswith(
        "Easy with cheats is for campaigns only, so this battle was sent as easy."
    )


async def test_an_easy_cheat_in_a_campaign_is_kept(tmp_path):
    copy = tmp_path / "copy.json"
    control, _ = make_control(tmp_path, "done", f"--copy={copy}")
    ask(
        control,
        "options.set",
        {"player": PLAYER, "name": "difficulty", "value": "easy_cheat"},
    )
    reply, _ = ask(
        control,
        "launch.solo",
        solo_args(mission_type="campaign", mission="camp.cmp"),
    )
    await finish(control)
    assert json.loads(copy.read_text())["settings"] == {"difficulty": "easy_cheat"}
    assert "campaigns only" not in result_of(reply)["message"]


async def test_a_sequence_is_launched_as_its_file(tmp_path):
    copy = tmp_path / "copy.json"
    control, _ = make_control(tmp_path, "done", f"--copy={copy}")
    ask(
        control,
        "launch.solo",
        solo_args(mission_type="tournament", side="none", mission="FIRST.TOU"),
    )
    await finish(control)
    description = json.loads(copy.read_text())
    assert (description["game_type"], description["side"], description["mission"]) == (
        "tournament",
        "none",
        "tourn/first.tou",
    )


def test_options_start_empty_and_are_per_player(tmp_path):
    control, _ = make_control(tmp_path)
    reply, _ = ask(control, "options.get", {"player": PLAYER})
    assert result_of(reply)["options"] == dict.fromkeys(
        [
            "difficulty",
            "collisions",
            "craft_jumping",
            "random_setup",
            "battle_length",
            "mission_time_limit",
            "continue_sequence",
        ]
    )
    ask(
        control,
        "options.set",
        {"player": PLAYER, "name": "battle_length", "value": "3"},
    )
    mine, _ = ask(control, "options.get", {"player": PLAYER})
    other, _ = ask(control, "options.get", {"player": "42"})
    assert result_of(mine)["options"]["battle_length"] == "3"
    assert result_of(other)["options"]["battle_length"] is None


def test_options_get_with_no_player_reads_the_linked_account(tmp_path):
    control, _ = make_control(tmp_path)
    ask(control, "options.set", {"player": PLAYER, "name": "collisions", "value": "on"})
    reply, _ = ask(control, "options.get", {"player": None})
    assert result_of(reply)["player"] is None
    control.host_player = lambda: PLAYER
    reply, _ = ask(control, "options.get", {"player": None})
    assert (result_of(reply)["player"], result_of(reply)["options"]["collisions"]) == (
        PLAYER,
        "on",
    )


def test_setting_an_option_pushes_options_changed_with_the_new_values(tmp_path):
    control, _ = make_control(tmp_path)
    reply, pushes = ask(
        control,
        "options.set",
        {"player": PLAYER, "name": "difficulty", "value": "medium"},
    )
    (push,) = pushes
    check_valid(push)
    assert push["event"] == "options.changed"
    assert push["data"] == result_of(reply)
    assert push["data"]["options"]["difficulty"] == "medium"


def test_null_leaves_an_option_as_the_game_has_it(tmp_path):
    control, _ = make_control(tmp_path)
    ask(
        control,
        "options.set",
        {"player": PLAYER, "name": "difficulty", "value": "hard"},
    )
    reply, _ = ask(
        control, "options.set", {"player": PLAYER, "name": "difficulty", "value": None}
    )
    assert result_of(reply)["options"]["difficulty"] is None
    assert control.store.get_options(PLAYER) == {}


def test_options_are_kept_in_the_settings_file_and_survive_a_restart(tmp_path):
    control, _ = make_control(tmp_path)
    ask(
        control,
        "options.set",
        {"player": PLAYER, "name": "mission_time_limit", "value": "1"},
    )
    again = SettingsStore(control.store.path)
    assert again.get_options(PLAYER) == {"mission_time_limit": 1}


@pytest.mark.parametrize(
    "args",
    [
        {"player": PLAYER, "name": "difficulty", "value": "impossible"},
        {"player": PLAYER, "name": "collisions", "value": "true"},
        {"player": PLAYER, "name": "nothing", "value": "on"},
        {"player": "x", "name": "collisions", "value": "on"},
        {"player": PLAYER, "name": "collisions", "value": 1},
        {"player": PLAYER, "name": "collisions"},
    ],
)
def test_options_set_refuses_what_is_off_the_table(tmp_path, args):
    control, _ = make_control(tmp_path)
    reply, pushes = ask(control, "options.set", args)
    assert reply["error"]["code"] == "bad_arguments"
    assert pushes == []
    assert control.store.get_options(PLAYER) == {}


@pytest.mark.parametrize("player", ["x", "", "1" * 21, 7])
def test_options_get_refuses_a_player_that_is_not_an_account_id(tmp_path, player):
    control, _ = make_control(tmp_path)
    reply, _ = ask(control, "options.get", {"player": player})
    assert reply["error"]["code"] == "bad_arguments"


def test_a_command_off_the_list_is_refused_not_run(tmp_path, caplog):
    control, _ = make_control(tmp_path)
    with caplog.at_level(logging.WARNING):
        reply, pushes = ask(
            control, "launch.anything", {"engine": "/bin/true", "args": ["x"]}
        )
    assert reply["error"]["code"] == "unknown_command"
    assert pushes == [] and not control.battles.busy()
    assert any("unknown_command" in r.message for r in caplog.records)
