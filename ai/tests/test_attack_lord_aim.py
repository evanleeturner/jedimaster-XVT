"""Fence for attack's Sith Lord aim: the Lord steers at its lead and fires by
it (``lord.aim``), keeps its correction and the tick of its look in its
notebook, and starts the correction again at a new target; an Apprentice
leaves the turn out. Every rank's plain lead is ``test_attack_lead.py``'s.

The craft is ``attack_setup.py``'s, flown at speed 300 with one group of
rebel lasers; its order's target crosses along +x 0x4000 ahead at a type
8's size and speed.
"""

from __future__ import annotations

import pytest
from attack_setup import FOE
from attack_setup import ME

from jedimaster_ai import cannons
from jedimaster_ai import geometry
from jedimaster_ai import lord
from jedimaster_ai import notebook
from jedimaster_ai.behaviors import attack
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import Question
from jedimaster_ai.world import Tracer

REBEL_LASER = 137
LORD = 7
APPRENTICE = 6
TICK = 1000


@pytest.fixture
def fighter(craft):
    def make(**changes):
        mine = {"speed": 300, "cannon_type": (REBEL_LASER, 0), "skill_level": LORD}
        return craft(**(mine | changes))

    return make


@pytest.fixture
def crosser(foe):
    def make(**changes):
        crossing = {
            "y": 0x4000,
            "yaw": 0x4000,
            "pitch": geometry.LEVEL,
            "speed": 300,
            "size": 400,
        }
        return foe(**(crossing | changes))

    return make


def memory_of(decision) -> attack.Memory:
    return attack.Memory.read(notebook.decode(decision.notebook).scratch)


def behind(target) -> Tracer:
    """Return the craft's tracer, past the target's line 200 behind it."""
    return Tracer(
        40,
        ME,
        REBEL_LASER,
        target.x - 200,
        target.y + 300,
        0,
        0,
        geometry.LEVEL,
        2300,
        90,
        146,
    )


def asked_with(shown, me, target, *tracers, tick=TICK) -> Question:
    question = shown(me, target, tick=tick)
    return Question(question.tick, question.craft, question.contacts, tracers=tracers)


def steer_of(me, target):
    steer = attack.fight(me, Question(TICK, (me,), ()), [target]).actions.steer
    return steer.yaw, steer.pitch


def test_a_lord_follows_the_turn_an_apprentice_leaves_out(fighter, crosser) -> None:
    turning = crosser(yaw_turned=0x4000, turn_ticks=59)
    meeting = cannons.meet(fighter(), turning, REBEL_LASER, True)
    lead = geometry.bearing(meeting.dx, meeting.dy, meeting.dz)
    assert steer_of(fighter(), turning) == lead
    assert steer_of(fighter(skill_level=APPRENTICE), turning) != lead


def test_a_lord_fires_only_with_its_nose_on_the_lead(fighter, crosser, ask) -> None:
    target = crosser()
    at_target = ask(fighter(), target)
    assert at_target.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    yaw, pitch = at_target.actions.steer.yaw, at_target.actions.steer.pitch
    on_lead = ask(fighter(yaw=yaw, pitch=pitch), target)
    assert on_lead.actions.lasers != (HOLD_FIRE, HOLD_FIRE)


def test_a_look_keeps_the_correction_and_its_tick(fighter, crosser, shown) -> None:
    target = crosser()
    first = attack.think(
        fighter(), asked_with(shown, fighter(), target, behind(target))
    )
    kept = memory_of(first)
    assert kept.fix.ahead > 0
    assert kept.looked == TICK
    again = fighter(notebook=first.notebook)
    second = attack.think(again, asked_with(shown, again, target, tick=TICK + 8))
    assert memory_of(second).fix == kept.fix
    assert memory_of(second).looked == TICK + 8


def test_a_new_target_starts_the_correction_at_0(fighter, crosser, shown) -> None:
    target = crosser()
    first = attack.think(
        fighter(), asked_with(shown, fighter(), target, behind(target))
    )
    assert memory_of(first).fix != lord.Offset()
    other = crosser(object_index=FOE + 1)
    again = fighter(notebook=first.notebook)
    second = attack.think(again, asked_with(shown, again, other, tick=TICK + 8))
    assert memory_of(second).target == FOE + 1
    assert memory_of(second).fix == lord.Offset()


def test_a_lords_memory_reads_back_as_written() -> None:
    memory = attack.Memory(target=FOE, fix=lord.Offset(-600, 5, 600), looked=0xFFFFFFFF)
    assert attack.Memory.read(memory.write()) == memory
