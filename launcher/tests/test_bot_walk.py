"""The Single player walk and the game options menus, as ``core`` decides them.

Purpose:
    Prove each step of the walk offers what the launcher's lists hold and
    nothing else (no ``&`` mission, ``*`` marked, sequences as files, menus
    cut at 25 and five), that picks carry what came before, that Launch asks
    the launcher and reports its words, that the options menus list only the
    option's choices and mark the current one (the default when unset), and that every step is
    for the linked account only.

Flow:
    A ``Core`` over the solo install with the stand-in engine and a linked
    account; replies read as plain data; the requests the core sent are
    recorded and checked against the schema.

Invariants:
    - No Discord; no game data.

Call:
    ``pytest tests/test_bot_walk.py``
"""

from __future__ import annotations

import asyncio
import json
import logging

import pytest
from botdata import HOST
from botdata import make_bot
from botdata import STRANGER
from pagedata import check_valid
from solodata import make_ai_program

from jedimaster.bot.core import HOST_ONLY
from jedimaster.bot.reply import menus
from jedimaster.bot.reply import Option
from jedimaster.launch.options import OPTIONS

logger = logging.getLogger(__name__)


@pytest.fixture
def bot(tmp_path):
    made = make_bot(tmp_path, "done")
    made.link_account(HOST)
    made.sent = []
    real = made.control.handle

    def recording(text):
        outcome = real(text)
        made.sent.append((json.loads(text), outcome))
        check_valid(outcome.reply)
        return outcome

    made.control.handle = recording
    return made


def labels(picker):
    return [(o.label, o.value, o.description) for o in picker.options]


def test_solo_offers_the_six_kinds_in_one_menu(bot):
    reply = bot.core.solo(HOST)
    (picker,) = reply.pickers
    assert picker.kind == "solo_kind"
    assert [o.value for o in picker.options] == [
        "training", "melee", "tournament", "combat", "battle", "campaign",
    ]  # fmt: skip
    assert bot.sent == []
    assert reply.opens_page is True, "the Launch at the end needs an open page"
    assert bot.core.pick_solo(HOST, "kind", "", "training").opens_page is False


def test_a_kind_with_a_side_asks_for_the_side(bot):
    reply = bot.core.pick_solo(HOST, "kind", "", "training")
    (picker,) = reply.pickers
    assert (picker.kind, picker.key) == ("solo_side", "training")
    assert [o.value for o in picker.options] == ["rebel", "imperial"]
    assert reply.edit is True


def test_a_kind_without_a_side_goes_straight_to_its_missions(bot):
    reply = bot.core.pick_solo(HOST, "kind", "", "melee")
    (picker,) = reply.pickers
    assert (picker.kind, picker.key) == ("solo_mission", "melee|none")
    assert labels(picker) == [("Open: Open Field", "delta.tie", "delta.tie")]


def test_the_missions_leave_out_ampersand_and_list_star_plainly(bot):
    reply = bot.core.pick_solo(HOST, "side", "training", "rebel")
    (picker,) = reply.pickers
    assert picker.key == "training|rebel"
    assert labels(picker) == [
        ("Basic: First Flight", "alpha.tie", "alpha.tie"),
        ("Advanced: Third Flight", "charlie.tie", "charlie.tie"),
    ]


def test_a_sequence_is_picked_as_its_file(bot):
    reply = bot.core.pick_solo(HOST, "kind", "", "tournament")
    assert labels(reply.pickers[0]) == [("First Tournament", "first.tou", "first.tou")]


def test_the_pilots_come_from_the_pilot_folder(bot):
    reply = bot.core.pick_solo(HOST, "mission", "training|rebel", "alpha.tie")
    (picker,) = reply.pickers
    assert picker.key == "training|rebel|alpha.tie"
    assert [o.value for o in picker.options] == ["Ace", "Bravo Two", "Cee"]
    assert "First Flight" in reply.text


def test_a_pilot_pick_offers_launch_with_everything_picked(bot):
    reply = bot.core.pick_solo(HOST, "pilot", "training|rebel|alpha.tie", "Bravo Two")
    (button,) = reply.buttons
    assert (button.label, button.kind) == ("Launch", "solo_launch")
    assert button.key == "training|rebel|alpha.tie|Bravo Two"
    assert reply.pickers == () and reply.edit is True


def test_a_mission_not_on_the_list_is_not_followed(bot):
    reply = bot.core.pick_solo(HOST, "mission", "training|rebel", "bravo.tie")
    assert "Start again" in reply.text and reply.pickers == () and reply.edit is False


@pytest.mark.parametrize(
    "step, key, value",
    [
        ("kind", "", "arena"),
        ("kind", "x", "melee"),
        ("side", "melee", "rebel"),
        ("side", "training", "none"),
        ("side", "", "rebel"),
        ("mission", "melee", "delta.tie"),
        ("mission", "training|none", "alpha.tie"),
        ("pilot", "training|rebel", "Ace"),
        ("pilot", "training|rebel|alpha.tie", ""),
        ("wander", "", "melee"),
    ],
)
def test_a_pick_that_does_not_fit_its_step_is_not_followed(bot, step, key, value):
    reply = bot.core.pick_solo(HOST, step, key, value)
    assert "Start again" in reply.text
    assert reply.pickers == () and reply.buttons == ()


def test_a_list_the_install_lacks_says_so(bot):
    reply = bot.core.pick_solo(HOST, "side", "combat", "rebel")
    assert reply.text == "The game's list of Combat, rebel missions was not found."


def test_an_empty_list_says_so(bot):
    (bot.control.install / "Melee/MISSION.LST").write_bytes(b"")
    reply = bot.core.pick_solo(HOST, "kind", "", "melee")
    assert reply.text == "The list of Melee missions is empty."
    assert reply.pickers == ()


def test_no_pilots_says_to_make_one(bot):
    for plt in bot.control.launching.flight.pilot_folder.iterdir():
        plt.unlink()
    reply = bot.core.pick_solo(HOST, "mission", "training|rebel", "alpha.tie")
    assert reply.text == "The game has no pilots yet. Make one in the game first."


def test_an_unreadable_pilot_folder_says_so(bot):
    folder = bot.control.launching.flight.pilot_folder
    for plt in folder.iterdir():
        plt.unlink()
    folder.rmdir()
    reply = bot.core.pick_solo(HOST, "mission", "training|rebel", "alpha.tie")
    assert reply.text == "The launcher cannot read the game's pilot folder."


def test_no_game_program_is_named_before_the_pilot_folder(bot):
    bot.control.launching.flight = None
    reply = bot.core.pick_solo(HOST, "mission", "training|rebel", "alpha.tie")
    assert reply.text == "This launcher was started without the game program."
    assert reply.pickers == ()


def test_menus_are_cut_at_25_to_a_menu_and_five_menus(bot):
    options = [Option(f"m{n}", f"v{n}") for n in range(130)]
    pickers, left_out = menus("solo_mission", "k", "Missions", options)
    assert [len(p.options) for p in pickers] == [25, 25, 25, 25, 25]
    assert left_out == 5
    assert pickers[1].placeholder == "Missions 26 to 50"
    assert menus("x", "k", "T", [])[0] == ()


def test_a_long_list_of_missions_says_how_many_are_shown(bot):
    from listdata import crlf

    lines = []
    for n in range(130):
        lines += [str(n), f"M{n}.TIE", f"Mission {n}"]
    (bot.control.install / "Melee/MISSION.LST").write_bytes(crlf(*lines))
    reply = bot.core.pick_solo(HOST, "kind", "", "melee")
    assert len(reply.pickers) == 5
    assert reply.text.endswith("Showing the first 125 of 130.")


async def test_launch_asks_the_launcher_and_reports_its_words(bot):
    reply = bot.core.launch_solo(HOST, "training|rebel|alpha.tie|Ace")
    request, _ = bot.sent[-1]
    assert request["command"] == "launch.solo"
    assert request["args"] == {
        "player": str(HOST),
        "mission_type": "training",
        "side": "rebel",
        "mission": "alpha.tie",
        "pilot": "Ace",
    }
    assert reply.text == "Started First Flight as Ace. Watch it on the page."
    assert reply.edit is True
    assert reply.opens_page is False
    assert [p["event"] for p in reply.pushes] == ["launch.started"]
    bot.control.battles.cancel(1)
    await asyncio.sleep(0.3)


def test_a_refused_launch_says_why_and_keeps_the_button(bot):
    bot.control.pages_open = lambda: 0
    reply = bot.core.launch_solo(HOST, "training|rebel|alpha.tie|Ace")
    assert reply.text.startswith("Open the launcher's page first")
    assert reply.edit is False and reply.pushes == []
    assert reply.opens_page is True


def test_a_launch_refused_for_another_reason_does_not_ask_for_the_page(bot):
    reply = bot.core.launch_solo(HOST, "training|rebel|alpha.tie|Nobody")
    assert reply.text == "That pilot is not among the game's pilots."
    assert reply.opens_page is False


def test_a_game_and_ai_program_that_do_not_match_refuse_the_launch_in_words(bot):
    battles = bot.control.battles
    make_ai_program(bot.folder / "ai" / "jedimaster_ai", wire=100)
    reply = bot.core.launch_solo(HOST, "training|rebel|alpha.tie|Ace")
    assert reply.text == (
        "This game build speaks AI version 11 and its AI program speaks AI "
        'version 100, so computer craft could not fly. Run "jedimaster update" '
        "for a matching pair."
    )
    assert reply.edit is False and reply.pushes == [] and reply.opens_page is False
    assert not battles.busy()


def test_the_launcher_checks_what_the_bot_sent(bot):
    reply = bot.core.launch_solo(HOST, "training|rebel|bravo.tie|Ace")
    assert (
        reply.text == "That mission is not in the game's list for this kind of battle."
    )
    reply = bot.core.launch_solo(HOST, "training|rebel|alpha.tie|Nobody")
    assert reply.text == "That pilot is not among the game's pilots."


def test_a_launch_key_that_is_not_four_parts_is_not_sent(bot):
    reply = bot.core.launch_solo(HOST, "training|rebel|alpha.tie")
    assert "Start again" in reply.text and bot.sent == []


def test_a_launch_with_a_side_that_does_not_fit_is_refused_by_the_list(bot):
    reply = bot.core.launch_solo(HOST, "melee|rebel|delta.tie|Ace")
    assert reply.text == "The launcher refused that request."


async def test_nothing_in_a_reply_is_a_path_or_the_program(bot, tmp_path):
    texts = []
    for reply in (
        bot.core.pick_solo(HOST, "mission", "training|rebel", "alpha.tie"),
        bot.core.pick_solo(HOST, "pilot", "training|rebel|alpha.tie", "Ace"),
        bot.core.launch_solo(HOST, "training|rebel|alpha.tie|Ace"),
    ):
        texts += [reply.text] + [
            o.label + o.value + o.description for p in reply.pickers for o in p.options
        ]
        texts += [b.key for b in reply.buttons]
    joined = "\n".join(texts)
    assert str(tmp_path) not in joined and "engine.py" not in joined
    bot.control.battles.cancel(1)
    await asyncio.sleep(0.3)


# ---- the game options ------------------------------------------------------


def test_options_is_eight_menus_five_then_three_each_marking_its_default(bot):
    reply = bot.core.options(HOST)
    (second,) = reply.then
    pickers = reply.pickers + second.pickers
    assert [p.key for p in pickers] == [
        "difficulty", "collisions", "craft_jumping", "random_setup",
        "battle_length", "mission_time_limit", "continue_sequence", "sith",
    ]  # fmt: skip
    assert len(reply.pickers) == 5 and len(second.pickers) == 3
    for picker in pickers:
        spec = OPTIONS[picker.key]
        assert picker.kind == "option"
        assert [o.value for o in picker.options] == [c.text for c in spec.choices]
        assert [o.value for o in picker.options if o.default] == [spec.default]
        assert {o.description for o in picker.options} == {spec.help}
        assert len(picker.options) <= 25


def test_every_menu_holds_only_its_options_choices_and_the_words_hold_no_leftover(bot):
    reply = bot.core.options(HOST)
    for part in (reply, *reply.then):
        for picker in part.pickers:
            texts = [c.text for c in OPTIONS[picker.key].choices]
            assert [o.value for o in picker.options] == texts
            assert [o.label for o in picker.options] == [
                c.words for c in OPTIONS[picker.key].choices
            ]


def test_the_words_name_each_default_when_nothing_is_set(bot):
    text = bot.core.options(HOST).text
    for line in (
        "Difficulty: Medium.",
        "Collisions: On.",
        "Craft jumping: On.",
        "Random setup: Off.",
        "Battle length: 3 wins.",
        "Battle or campaign: Continue.",
    ):
        assert line in text
    assert "The mission's own limit." in text


def test_the_time_limit_menu_holds_all_its_values_in_words(bot):
    reply = bot.core.options(HOST)
    limit = reply.then[0].pickers[0]
    words = [o.label for o in limit.options]
    assert words[:3] == ["No limit", "The mission's own limit", "1 minute"]
    assert words[-1] == "20 minutes" and len(words) == 22


def test_the_current_choice_is_marked_and_the_words_name_it(bot):
    bot.core.pick_option(HOST, "difficulty", "hard")
    bot.core.pick_option(HOST, "collisions", "off")
    reply = bot.core.options(HOST)
    difficulty, collisions = reply.pickers[0], reply.pickers[1]
    assert [o.label for o in difficulty.options if o.default] == ["Hard"]
    assert [o.label for o in collisions.options if o.default] == ["Off"]
    assert "Difficulty: Hard." in reply.text and "Collisions: Off." in reply.text
    assert "Craft jumping: On." in reply.text


def test_a_pick_sets_the_option_for_that_player_and_tells_the_pages(bot):
    reply = bot.core.pick_option(HOST, "battle_length", "4")
    assert reply.text == "Battle length set to 4 wins."
    assert [p["event"] for p in reply.pushes] == ["options.changed"]
    assert reply.opens_page is True
    assert bot.control.store.get_options(str(HOST)) == {"battle_length": 4}


def test_picking_the_default_stores_it_and_says_so_in_the_usual_words(bot):
    reply = bot.core.pick_option(HOST, "difficulty", "medium")
    assert reply.text == "Difficulty set to Medium."
    assert bot.control.store.get_options(str(HOST)) == {"difficulty": "medium"}


def test_an_unknown_option_or_value_is_not_set(bot):
    assert "Start again" in bot.core.pick_option(HOST, "speed", "fast").text
    reply = bot.core.pick_option(HOST, "difficulty", "impossible")
    assert reply.text == "The launcher refused that request."
    assert bot.control.store.get_options(str(HOST)) == {}


# ---- who is answered -------------------------------------------------------


@pytest.mark.parametrize(
    "ask",
    [
        lambda c, u: c.solo(u),
        lambda c, u: c.pick_solo(u, "kind", "", "melee"),
        lambda c, u: c.launch_solo(u, "training|rebel|alpha.tie|Ace"),
        lambda c, u: c.options(u),
        lambda c, u: c.pick_option(u, "difficulty", "hard"),
        lambda c, u: c.missions(u, None),
    ],
    ids=["solo", "pick_solo", "launch_solo", "options", "pick_option", "missions"],
)
def test_another_account_is_refused_and_nothing_is_sent(bot, ask, caplog):
    with caplog.at_level(logging.WARNING):
        reply = ask(bot.core, STRANGER)
    assert reply.text == HOST_ONLY
    assert reply.pickers == () and reply.buttons == () and reply.then == ()
    assert bot.sent == []
    assert bot.control.store.get_options(str(STRANGER)) == {}
    assert "not the linked account" in caplog.text


def test_show_mission_without_a_type_offers_the_six_kinds(bot):
    reply = bot.core.missions(HOST, None)
    (picker,) = reply.pickers
    assert picker.kind == "missions_kind"
    assert [o.value for o in picker.options][:2] == ["training", "melee"]
    assert bot.sent == []


def test_the_walk_can_be_used_in_a_server_too(bot):
    assert bot.core.solo(HOST, in_dm=False).pickers
    assert bot.core.options(HOST, in_dm=False).pickers


def test_a_battle_end_goes_to_the_linked_account_only(bot):
    push = {
        "event": "launch.ended",
        "data": {"launch_id": 1, "status": "flown", "line": "flown: x"},
    }
    assert bot.core.end_notice(push) == (HOST, "flown: x")
    assert bot.core.end_notice({"event": "launch.started", "data": {}}) is None
    bot.linker.unlink()
    assert bot.core.end_notice(push) is None


def test_the_launcher_asks_the_bot_for_the_linked_account_as_a_player(bot):
    assert bot.control.host_player() == str(HOST)
    bot.linker.unlink()
    assert bot.control.host_player() is None


def test_a_long_title_is_cut_to_discords_limit_with_dots(bot):
    from listdata import crlf

    (bot.control.install / "Melee/MISSION.LST").write_bytes(
        crlf("1", "A.TIE", "T" * 150)
    )
    (option,) = bot.core.pick_solo(HOST, "kind", "", "melee").pickers[0].options
    assert len(option.label) == 100 and option.label.endswith("...")
