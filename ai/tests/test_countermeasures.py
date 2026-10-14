"""Fence for countermeasures: a starfighter answers the homing shots closing
on it, by the 1997 AI's ranges, whatever its order."""

from __future__ import annotations

import dataclasses
import logging
import re

import pytest

from jedimaster_ai import progress
from jedimaster_ai.behaviors import countermeasures
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Books
from jedimaster_ai.world import Countermeasure
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import THREATS_MAX

ME = 4
TORPEDO = 143
# The engine's object types of the concussion missiles (laser.h).
MISSILE = 144
ADVANCED_MISSILE = 149


@pytest.fixture
def fighter(view):
    """A tier-0 starfighter at the origin with three flares ready."""

    def make(*shots, **changes):
        mine = {
            "object_index": ME,
            "x": 0,
            "y": 0,
            "z": 0,
            "skill": 0,
            "countermeasure": Countermeasure.FLARE,
            "countermeasures_left": 3,
            "countermeasure_working": 1,
        }
        return with_shots(view(**(mine | changes)), *shots)

    return make


def with_shots(view, *shots):
    """The view with these homing shots aimed at it, each (object index,
    object type, x, decoyed), or with y and z after."""
    padded = [shot + (0, 0)[len(shot) - 4 :] for shot in shots]
    padded += [(0, 0, 0, False, 0, 0)] * (THREATS_MAX - len(shots))
    return dataclasses.replace(
        view,
        threat_count=len(shots),
        threat_object=tuple(shot[0] for shot in padded),
        threat_type=tuple(shot[1] for shot in padded),
        threat_x=tuple(shot[2] for shot in padded),
        threat_y=tuple(shot[4] for shot in padded),
        threat_z=tuple(shot[5] for shot in padded),
        threat_decoyed=tuple(int(shot[3]) for shot in padded),
    )


@pytest.fixture
def me(contact):
    """The fighter's own contact: a starfighter unless a genus is given."""

    def make(genus=0):
        return contact(object_index=ME, genus=genus, x=0, y=0, z=0)

    return make


def launches(view, own) -> bool:
    question = Question(1000, (view,), (own,))
    decision = countermeasures.answer(view, question, Decision(view.notebook))
    return decision.actions.countermeasure


def test_a_flare_answers_a_shot_inside_range(fighter, me) -> None:
    assert launches(fighter((20, TORPEDO, 2000, False)), me())


@pytest.mark.parametrize(
    ("skill", "reach"), [(0, 2048), (0x8000, 4096), (0xC000, 6144)], ids=[0, 1, 2]
)
def test_the_range_grows_with_skill(fighter, me, skill, reach) -> None:
    """The 1997 ranges, by the tier at the bottom of each skill band."""
    assert launches(fighter((20, TORPEDO, reach - 1, False), skill=skill), me())
    assert not launches(fighter((20, TORPEDO, reach, False), skill=skill), me())


@pytest.mark.parametrize("missile", [MISSILE, ADVANCED_MISSILE])
def test_a_concussion_missile_is_answered_from_three_times_as_far(
    fighter, me, missile
) -> None:
    far = 3 * 2048 - 1
    assert launches(fighter((20, missile, far, False)), me())
    assert not launches(fighter((20, missile, far + 1, False)), me())
    assert not launches(fighter((20, TORPEDO, far, False)), me())


def test_the_distance_counts_every_axis(fighter, me) -> None:
    # 2,061 units away, then 1,990: only the second is inside 2,048.
    assert not launches(fighter((20, TORPEDO, 1000, False, 1000, 1500)), me())
    assert launches(fighter((20, TORPEDO, 1000, False, 1000, 1400)), me())


def test_a_shot_out_of_range_is_passed_over_for_one_inside(fighter, me) -> None:
    assert launches(
        fighter((20, TORPEDO, 9000, False), (21, TORPEDO, 500, False)), me()
    )


def test_a_flare_waits_while_one_homes_on_the_first_shot_in_range(fighter, me) -> None:
    """1997 weighed only the first shot in range: the second, not decoyed,
    waits for the next think."""
    me_ = me()
    assert not launches(fighter((20, TORPEDO, 500, True)), me_)
    assert not launches(
        fighter((20, TORPEDO, 500, True), (21, TORPEDO, 400, False)), me_
    )


def test_chaff_answers_a_shot_a_flare_already_chases(fighter, me) -> None:
    chaff = fighter((20, TORPEDO, 500, True), countermeasure=Countermeasure.CHAFF)
    assert launches(chaff, me())


@pytest.mark.parametrize("genus", [1, 3, 4])
def test_only_a_starfighter_answers(fighter, me, genus) -> None:
    assert not launches(fighter((20, TORPEDO, 500, False)), me(genus))


def test_a_craft_missing_from_the_contacts_does_not_answer(fighter, contact) -> None:
    stranger = contact(object_index=ME + 1, genus=0)
    assert not launches(fighter((20, TORPEDO, 500, False)), stranger)


@pytest.mark.parametrize(
    "changes",
    [
        {"countermeasure_cooldown": 1},
        {"countermeasures_left": 0},
        {"countermeasure_working": 0},
        {"countermeasure": Countermeasure.NONE},
        {"countermeasure": 3},
        {"countermeasure": Countermeasure.CHAFF, "chaff_seconds": 1},
    ],
    ids=[
        "cooling",
        "none left",
        "not working",
        "no type",
        "cluster mine",
        "chaff running",
    ],
)
def test_a_countermeasure_not_ready_launches_nothing(fighter, me, changes) -> None:
    assert not launches(fighter((20, TORPEDO, 500, False), **changes), me())


def test_no_shot_launches_nothing(fighter, me) -> None:
    assert not launches(fighter(), me())


def test_the_rest_of_the_decision_is_kept(fighter, me) -> None:
    view = fighter((20, TORPEDO, 500, False))
    before = Decision(
        b"\x07" * len(view.notebook), Actions(throttle=9, target=NO_OBJECT)
    )
    after = countermeasures.answer(view, Question(1000, (view,), (me(),)), before)
    assert after == Decision(
        before.notebook, Actions(throttle=9, target=NO_OBJECT, countermeasure=True)
    )


def test_the_weighing_is_logged(fighter, me, caplog) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    launches(fighter((20, MISSILE, 5000, False)), me())
    assert (
        "countermeasures.threat object=4 threat=20 type=144 distance=5000 "
        "range=6144 kind=2 launch=1"
    ) in caplog.messages


def test_a_craft_going_home_answers_too(fighter, me, caplog) -> None:
    """Real case: 1997's fly-home plans never launched; a craft whose orders
    are done goes home and still answers the missile on its tail. The
    view's log names the missile; the answer's carries action bit 13."""
    caplog.set_level(logging.DEBUG, logger="jedimaster_ai")
    view = fighter(
        (20, MISSILE, 3000, False),
        books=Books.ORDERS_DONE | Books.MAY_JUMP,
        countermeasure=Countermeasure.CHAFF,
    )
    assert progress.decide(view).home
    answer = think(Question(1000, (view,), (me(),)))
    assert answer.craft[0].actions.countermeasure
    assert (
        "think.threat object=4 threat=20 type=144 decoyed=0 x=3000 y=0 z=0"
        in caplog.messages
    )
    answered = re.search(r"think\.answer object=4 actions=(\d+) ", caplog.text)
    assert int(answered.group(1)) & 1 << 13
