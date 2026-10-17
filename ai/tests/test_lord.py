"""Fence for lord: the Sith Lord's own rules, at skill level 7 alone.

Which level is a Lord's, and the books that send a Lord home: every home
book but the abort, logged when the abort is set aside. The Lord's aim: at
the meeting point, moved by a correction its tracers walk in, firing
within its cone. The callers' tests hold the rest:
``test_attack_fight_back.py`` and ``test_home.py`` the fight at any hull,
``test_progress.py`` and ``test_dropoff.py`` the abort,
``test_warheads.py`` the unguided shot at any craft,
``test_attack_lord_aim.py`` the aim in the dogfight, ``test_cannons.py``
the meeting point and the plain lead.

The aim's tests hold its rules, never its tuned constants' values: a
tuning harness and a flight set those.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import cannons
from jedimaster_ai import geometry
from jedimaster_ai import lord
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import Books
from jedimaster_ai.world import Question
from jedimaster_ai.world import Tracer

# The books that send a craft home (``progress.HOME_BOOKS``).
HOME = Books.ABORTED | Books.DEPARTURE_DUE | Books.ORDERS_DONE


@pytest.mark.parametrize(("level", "is_lord"), [(5, False), (6, False), (7, True)])
def test_only_skill_level_7_is_a_lord(view, level, is_lord) -> None:
    assert lord.is_lord(view(skill_level=level)) is is_lord


@pytest.mark.parametrize("level", [0, 5, 6])
def test_every_other_rank_keeps_every_home_book(view, level) -> None:
    assert lord.home_books(view(skill_level=level), HOME) == HOME


def test_a_lord_sets_its_abort_aside_and_keeps_the_rest(view, caplog) -> None:
    caplog.set_level("DEBUG")
    craft = view(skill_level=7)
    assert lord.home_books(craft, HOME) == Books.DEPARTURE_DUE | Books.ORDERS_DONE
    assert f"lord.stays object={craft.object_index} books={int(HOME)}" in caplog.text


def test_a_lord_with_only_its_abort_has_no_home_book(view) -> None:
    assert lord.home_books(view(skill_level=7), Books.ABORTED) == 0


def test_a_lord_with_no_abort_logs_nothing(view, caplog) -> None:
    caplog.set_level("DEBUG")
    books = Books.DEPARTURE_DUE
    assert lord.home_books(view(skill_level=7), books) == books
    assert "lord.stays" not in caplog.text


# The Lord's aim: the Lord at the origin facing +y at speed 300 with one
# group of rebel lasers; its target, a type 8's size and speed, 0x4000 ahead
# and crossing along +x. A tracer flies at a rebel laser's speed and the
# Lord's.
ME = 4
FOE = 9
REBEL_LASER = 137
TRACER_SPEED = 2300
TICK = 1000
LOOK = 8
FREIGHTER = 3
NO_FIX = lord.Offset()


@pytest.fixture
def lord_craft(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "skill_level": 7,
            "x": 0,
            "y": 0,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "speed": 300,
            "cannon_type": (REBEL_LASER, 0),
            "cannon_group_count": 1,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def crosser(contact):
    def make(**changes):
        theirs = {
            "object_index": FOE,
            "genus": 0,
            "x": 0,
            "y": 0x4000,
            "z": 0,
            "yaw": 0x4000,
            "pitch": LEVEL,
            "speed": 300,
            "size": 400,
        }
        return contact(**(theirs | changes))

    return make


def tracer(**changes) -> Tracer:
    """Return the Lord's tracer: by default flying +y, 300 past the
    target's line and 200 behind it, its pass about two ticks ago."""
    fields = {
        "object_index": 40,
        "source": ME,
        "object_type": REBEL_LASER,
        "x": -200,
        "y": 0x4000 + 300,
        "z": 0,
        "yaw": 0,
        "pitch": LEVEL,
        "speed": TRACER_SPEED,
        "age": 90,
        "life_left": 146,
    }
    return Tracer(**(fields | changes))


def asked(me, *tracers) -> Question:
    return Question(TICK, (me,), (), tracers=tracers)


def lord_aim(me, target, *tracers, fix=NO_FIX, since=TICK - LOOK):
    plain = cannons.aim(me, target, 4)
    return lord.aim(me, asked(me, *tracers), target, plain, fix, since)


def test_a_lord_follows_the_turn_the_plain_lead_leaves_out(
    lord_craft, crosser, caplog
) -> None:
    caplog.set_level("DEBUG")
    me = lord_craft()
    straight = crosser()
    aim, fix = lord_aim(me, straight)
    # With no turn record and no correction, the Jedi's plain lead.
    assert aim == cannons.aim(me, straight, 4)
    assert fix == lord.Offset()
    assert f"lord.lead object={ME} target={FOE} " in caplog.text
    turning = crosser(yaw_turned=0x4000, turn_ticks=59)
    aim, _ = lord_aim(me, turning)
    meeting = cannons.meet(me, turning, REBEL_LASER, True)
    assert (aim.yaw, aim.pitch) == geometry.bearing(meeting.dx, meeting.dy, meeting.dz)
    plain = cannons.aim(me, turning, 4)
    assert (aim.yaw, aim.pitch) != (plain.yaw, plain.pitch)


def test_a_lord_fires_with_its_nose_on_the_lead(lord_craft, crosser) -> None:
    aim, _ = lord_aim(lord_craft(), crosser())
    on_it, _ = lord_aim(lord_craft(yaw=aim.yaw, pitch=aim.pitch), crosser())
    assert on_it.lined_up


@pytest.mark.parametrize(
    ("craft_changes", "target_changes"),
    [({}, {"genus": FREIGHTER}), ({"cannon_type": (56, 0)}, {})],
)
def test_at_a_bulky_target_or_with_no_known_shot_the_plain_aim_holds(
    lord_craft, crosser, caplog, craft_changes, target_changes
) -> None:
    caplog.set_level("DEBUG")
    me, target = lord_craft(**craft_changes), crosser(**target_changes)
    fix = lord.Offset(100, 0, 0)
    assert lord_aim(me, target, tracer(), fix=fix) == (
        cannons.aim(me, target, 4),
        fix,
    )
    assert "lord.lead" not in caplog.text


def test_out_of_its_range_a_lord_on_its_lead_holds_fire(lord_craft, crosser) -> None:
    # Fast and across: 0x2000 off tier 4's range, 32,768, as every craft.
    target = crosser(y=34000)
    lead, _ = lord_aim(lord_craft(), target)
    aimed, _ = lord_aim(lord_craft(yaw=lead.yaw, pitch=lead.pitch), target)
    assert not aimed.lined_up


def test_a_disabling_lord_leads_with_its_ion_cannons(lord_craft, crosser) -> None:
    me = lord_craft(cannon_type=(REBEL_LASER, cannons.ION_CANNON), cannon_group_count=2)
    target = crosser()
    plain = cannons.aim(me, target, 4)
    ion, _ = lord.aim(me, asked(me), target, plain, lord.Offset(), 0, disabling=True)
    meeting = cannons.meet(me, target, cannons.ION_CANNON, True)
    assert (ion.yaw, ion.pitch) == geometry.bearing(meeting.dx, meeting.dy, meeting.dz)


def test_a_tracer_passing_behind_moves_the_aim_ahead(lord_craft, crosser) -> None:
    me, target = lord_craft(), crosser()
    (miss,) = lord.passes(me, asked(me, tracer()), target, TICK - LOOK)
    assert miss.ahead > abs(miss.side)
    _, fix = lord_aim(me, target, tracer())
    assert fix.ahead > 0
    plain_lead, _ = lord_aim(me, target)
    fixed, _ = lord_aim(me, target, fix=fix)
    # Ahead of a target crossing along +x is toward +x: a larger yaw.
    assert geometry.signed(fixed.yaw - plain_lead.yaw) > 0


def test_a_tracer_passing_left_moves_the_aim_right(lord_craft, crosser) -> None:
    me, target = lord_craft(), crosser()
    # Climbing straight up beside the target's left, +y of its +x heading.
    up_left = tracer(x=0, y=0x4000 + 150, z=300, pitch=0)
    (miss,) = lord.passes(me, asked(me, up_left), target, TICK - LOOK)
    assert miss.side > abs(miss.ahead)
    assert abs(miss.up) < miss.side


def test_a_correction_turns_with_its_target(lord_craft, crosser) -> None:
    me = lord_craft()
    fix = lord.Offset(300, 0, 0)
    for heading, way in ((0x4000, 1), (0xC000, -1)):
        target = crosser(yaw=heading)
        plain_lead, _ = lord_aim(me, target)
        fixed, _ = lord_aim(me, target, fix=fix)
        assert way * geometry.signed(fixed.yaw - plain_lead.yaw) > 0


def test_a_tracer_counts_at_one_look_alone(lord_craft, crosser) -> None:
    me, target = lord_craft(), crosser()
    question = asked(me, tracer())
    assert len(lord.passes(me, question, target, TICK - LOOK)) == 1
    # Its pass came about two ticks ago: before a one-tick window.
    assert lord.passes(me, question, target, TICK - 1) == []
    coming = tracer(y=0x4000 - 300)
    assert lord.passes(me, asked(me, coming), target, TICK - LOOK) == []


def test_other_tracers_and_far_passes_count_for_nothing(lord_craft, crosser) -> None:
    me, target = lord_craft(), crosser()
    theirs = tracer(source=ME + 1)
    far = tracer(x=-lord.NEAR_PASS - 500)
    assert lord.passes(me, asked(me, theirs, far), target, TICK - LOOK) == []


def test_a_look_moves_the_correction_by_its_share_of_the_mean_miss() -> None:
    misses = [lord.Offset(100, -40, 10), lord.Offset(300, -80, 30)]
    assert lord.mean(misses) == lord.Offset(200, -60, 20)
    numerator, denominator = lord.GAIN
    kept = lord.Offset(10, 20, 30)
    assert lord.correct(kept, misses) == lord.Offset(
        10 + 200 * numerator // denominator,
        20 - 60 * numerator // denominator,
        30 + 20 * numerator // denominator,
    )
    assert lord.correct(kept, []) == kept


def test_the_correction_holds_within_its_limit() -> None:
    far = lord.MAX_CORRECTION * 4 * lord.GAIN[1]
    held = lord.correct(lord.Offset(), [lord.Offset(far, -far, far)])
    limit = lord.MAX_CORRECTION
    assert held == lord.Offset(limit, -limit, limit)


def test_a_tracer_passing_below_moves_the_aim_up(lord_craft, crosser) -> None:
    me, target = lord_craft(), crosser()
    under = tracer(x=0, z=-200)
    (miss,) = lord.passes(me, asked(me, under), target, TICK - LOOK)
    assert miss.up > abs(miss.side)


def test_a_lord_pitched_off_its_lead_holds_fire(lord_craft, crosser) -> None:
    aim, _ = lord_aim(lord_craft(), crosser())
    high = aim.pitch - 2 * cannons.AIM_CONE[4]
    off, _ = lord_aim(lord_craft(yaw=aim.yaw, pitch=high), crosser())
    assert not off.lined_up


def test_a_correction_is_turned_by_the_targets_heading_at_the_meeting(
    lord_craft, crosser
) -> None:
    # Turning right a quarter turn in 59 ticks off +x, the target flies
    # toward -y by the meeting: ahead of it there swings the aim back toward
    # the craft's nose, a smaller yaw, where its heading now would give a
    # larger one.
    me = lord_craft()
    target = crosser(yaw_turned=0x4000, turn_ticks=59)
    plain_lead, _ = lord_aim(me, target)
    fixed, _ = lord_aim(me, target, fix=lord.Offset(lord.MAX_CORRECTION, 0, 0))
    assert geometry.signed(fixed.yaw - plain_lead.yaw) < 0
