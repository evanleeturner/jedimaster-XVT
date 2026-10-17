"""Fence for attack: guided and unguided warheads in a dogfight.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target
(``attack_setup``).
"""

from __future__ import annotations

from attack_setup import FOE
from attack_setup import ME
from attack_setup import phase_of

from jedimaster_ai import notebook
from jedimaster_ai import warheads
from jedimaster_ai.behaviors import attack
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import Books
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import Lock
from jedimaster_ai.world import Steer
from jedimaster_ai.world import Warhead

# A craft whose engine declared its lock, with one launcher of concussion
# missiles ready.
LOCKED = {
    "warhead_launcher_count": 1,
    "warhead_type": (warheads.CONCUSSION_MISSILE, 0),
    "warheads_left": (4, 0),
    "launcher_cooldown": (0, 0),
    "lock_ticks": 708,
    "lock_needed": 708,
    "lock_state": Lock.DECLARED,
}


# A launcher whose wait runs: the engine made the warhead the last look asked
# for, and this look comes 16 ticks after it.
MADE = {"launcher_cooldown": (456, 0)}


def memory_of(decision) -> attack.Memory:
    return attack.Memory.read(notebook.decode(decision.notebook).scratch)


def test_the_memory_keeps_the_runs_shots_fresh_mark_and_reason() -> None:
    memory = attack.Memory(
        target=FOE, signature=3, ordered=1, shots=5, fresh=1, held=6, pending=1
    )
    assert attack.Memory.read(memory.write()) == memory


def test_a_declared_lock_fires_a_guided_warhead(craft, foe, ask, caplog) -> None:
    caplog.set_level("INFO")
    decision = ask(craft(**LOCKED), foe(), target=FOE)
    assert decision.actions.warhead == Warhead(0)
    assert decision.actions.target == FOE
    assert (
        f"attack.warhead object={ME} target={FOE} launcher=0 paired=0 type=144 "
        "lock=708 coming=0 shots=1" in caplog.text
    )
    memory = memory_of(decision)
    assert (memory.shots, memory.fresh, memory.pending) == (0, 0, 1)
    assert memory.held == warheads.Held.NONE


def test_the_next_look_counts_the_shot_once_the_launcher_waits(craft, foe, ask) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    made = LOCKED | MADE | {"notebook": first.notebook}
    memory = memory_of(ask(craft(**made), foe(), target=FOE, tick=1016))
    assert (memory.shots, memory.fresh, memory.pending) == (1, 1, 0)


def test_the_next_look_counts_nothing_when_the_engine_made_none(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    first = ask(craft(**LOCKED), foe(), target=FOE)
    unmade = LOCKED | {"notebook": first.notebook}
    again = ask(craft(**unmade), foe(), target=FOE, tick=1016)
    assert f"attack.warhead_unmade object={ME} target={FOE} launcher=0" in caplog.text
    memory = memory_of(again)
    assert (memory.shots, memory.fresh) == (0, 0)
    assert again.actions.warhead == Warhead(0)
    assert memory.pending == 1


def test_an_unmade_shot_is_forgotten_when_the_craft_then_holds(craft, foe, ask) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    lapsed = LOCKED | {"lock_state": Lock.COUNTING, "notebook": first.notebook}
    held = ask(craft(**lapsed), foe(), target=FOE, tick=1016)
    assert held.actions.warhead is None
    assert (memory_of(held).shots, memory_of(held).pending) == (0, 0)


def test_a_sith_lords_quick_look_settles_the_shot_the_same(craft, foe, ask) -> None:
    sith = LOCKED | {"skill_level": 6}
    first = ask(craft(**sith), foe(), target=FOE)
    later = 1000 + attack.FIGHT_INTERVALS[4]
    made = sith | MADE | {"notebook": first.notebook}
    memory = memory_of(ask(craft(**made), foe(), target=FOE, tick=later))
    assert (memory.shots, memory.fresh, memory.pending) == (1, 1, 0)


def test_a_new_target_drops_the_shot_the_last_look_asked_for(craft, foe, ask) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    nearer = foe(object_index=FOE + 1, object_signature=1, y=0x2000)
    later = 1000 + attack.LOOK_AGAIN_TICKS[2]
    changes = LOCKED | MADE | {"notebook": first.notebook, "order_target_mask": 0b110}
    decision = ask(craft(**changes), foe(), nearer, target=FOE + 1, tick=later)
    memory = memory_of(decision)
    assert memory.target == FOE + 1
    assert (memory.shots, memory.fresh, memory.pending) == (0, 0, 0)


def test_a_hurt_top_pilots_guided_pair_shows_on_its_line(craft, foe, ask, caplog):
    caplog.set_level("INFO")
    hurt = LOCKED | {"books": Books.TARGETS_LEFT | Books.HURT}
    decision = ask(craft(**hurt), foe(), target=FOE)
    assert decision.actions.warhead == Warhead(0, paired=True)
    assert (
        f"attack.warhead object={ME} target={FOE} launcher=0 paired=1 type=144 "
        "lock=708 coming=0 shots=1" in caplog.text
    )


def test_a_warhead_fires_with_the_target_off_the_cannons_cone(craft, foe, ask) -> None:
    actions = ask(craft(**LOCKED), foe(x=0x3000, y=0x3000), target=FOE).actions
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert actions.warhead == Warhead(0)


def test_after_a_shot_at_a_fighter_the_lock_must_lapse_and_the_run_is_done(
    craft, foe, ask
) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    made = LOCKED | MADE | {"notebook": first.notebook}
    settled = ask(craft(**made), foe(), target=FOE, tick=1016)
    assert memory_of(settled).fresh == 1
    held = ask(craft(**LOCKED, notebook=settled.notebook), foe(), target=FOE, tick=1488)
    assert held.actions.warhead is None
    assert memory_of(held).held == warheads.Held.FRESH
    counting = LOCKED | {"lock_state": Lock.COUNTING, "notebook": held.notebook}
    lapsed = ask(craft(**counting), foe(), target=FOE, tick=1504)
    assert memory_of(lapsed).fresh == 0
    again = ask(craft(**LOCKED, notebook=lapsed.notebook), foe(), target=FOE, tick=1520)
    assert again.actions.warhead is None
    assert memory_of(again).held == warheads.Held.CAPS


def test_a_big_ship_takes_six_shots_in_one_run(craft, foe, ask) -> None:
    ship = foe(genus=4, hull_strength=90_000)
    changes = LOCKED | {"warheads_left": (9, 0)}
    fired = []
    for step in range(8):
        tick = 1000 + 500 * step
        decision = ask(craft(**changes), ship, target=FOE, tick=tick)
        fired.append(decision.actions.warhead is not None)
        waiting = changes | MADE | {"notebook": decision.notebook}
        settled = ask(craft(**waiting), ship, target=FOE, tick=tick + 16)
        changes = changes | {"notebook": settled.notebook}
    assert fired == [True] * 6 + [False] * 2
    assert memory_of(decision).held == warheads.Held.CAPS
    assert memory_of(settled).shots == 6


def test_a_run_starts_again_after_a_break(craft, foe, ask) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    counting = LOCKED | MADE | {"lock_state": Lock.COUNTING, "notebook": first.notebook}
    broke = ask(craft(**counting), foe(y=0x800), target=FOE, tick=1016)
    assert phase_of(broke) == attack.BREAK
    back = ask(craft(**LOCKED, notebook=broke.notebook), foe(), target=FOE, tick=1032)
    assert phase_of(back) == attack.ATTACK
    assert back.actions.warhead == Warhead(0)
    assert memory_of(back).shots == 0
    assert memory_of(back).pending == 1


def test_a_new_target_starts_a_new_run(craft, foe, ask) -> None:
    first = ask(craft(**LOCKED), foe(), target=FOE)
    nearer = foe(object_index=FOE + 1, object_signature=1, y=0x2000)
    later = 1000 + attack.LOOK_AGAIN_TICKS[2]
    changes = LOCKED | {"notebook": first.notebook, "order_target_mask": 0b110}
    decision = ask(craft(**changes), foe(), nearer, target=FOE + 1, tick=later)
    assert decision.actions.target == FOE + 1
    assert decision.actions.warhead == Warhead(0)


def test_a_held_reason_is_logged_once_until_it_changes(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    bomber = LOCKED | {"warhead_type": (warheads.SPACE_BOMB, 0)}
    first = ask(craft(**bomber), foe(), target=FOE)
    second = ask(craft(**bomber, notebook=first.notebook), foe(), target=FOE, tick=1016)
    empty = LOCKED | {"warheads_left": (0, 0), "notebook": second.notebook}
    ask(craft(**empty), foe(), target=FOE, tick=1032)
    lines = [m for m in caplog.messages if m.startswith("attack.warhead_held")]
    assert lines == [
        f'attack.warhead_held object={ME} target={FOE} reason="kind" lock=708 '
        "lock_state=2 coming=0 coming_damage=0 shots=0",
        f'attack.warhead_held object={ME} target={FOE} reason="launcher" lock=708 '
        "lock_state=2 coming=0 coming_damage=0 shots=0",
    ]


def test_a_craft_with_no_launchers_holds_no_reason(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    decision = ask(craft(**LOCKED | {"warhead_launcher_count": 0}), foe(), target=FOE)
    assert decision.actions.warhead is None
    assert memory_of(decision).held == warheads.Held.NONE
    assert "attack.warhead_held" not in caplog.text


def test_a_weave_fires_no_warhead(craft, foe, ask) -> None:
    chaser = foe(object_index=FOE + 1, y=-0x2000, target=ME)
    decision = ask(craft(**LOCKED), foe(x=0x3000, y=0x3000), chaser, target=FOE)
    assert phase_of(decision) == attack.EVADE
    assert decision.actions.warhead is None


# A Jedi (skill level 5) with no lock, flying at speed 225 with one launcher
# of proton torpedoes, ready; its target a corvette 20,000 ahead crossing to
# +x at speed 150. The shot's meeting point lies 6,622 to the side, at yaw
# 3,336 (``warheads.lead``, its line harvested).
UNGUIDED = {
    "skill_level": 5,
    "speed": 225,
    "warhead_launcher_count": 1,
    "warhead_type": (warheads.PROTON_TORPEDO, 0),
    "warheads_left": (9, 0),
    "launcher_cooldown": (0, 0),
    "lock_ticks": 0,
    "lock_needed": 708,
    "lock_state": Lock.NONE,
}
CORVETTE = {
    "genus": 4,
    "size": 6011,
    "hull_strength": 90_000,
    "y": 20_000,
    "yaw": 0x4000,
    "pitch": LEVEL,
    "speed": 150,
}
LEAD_YAW = 3336


def test_a_jedi_steers_ahead_of_a_crossing_ship_while_the_aim_holds_its_shot(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    decision = ask(craft(**UNGUIDED), foe(**CORVETTE))
    assert decision.actions.steer == Steer(LEAD_YAW, LEVEL, 0)
    assert decision.actions.target == FOE
    assert decision.actions.warhead is None
    assert memory_of(decision).held == warheads.Held.AIM
    assert (
        f'attack.warhead_held object={ME} target={FOE} reason="aim" lock=0 '
        "lock_state=0 coming=0 coming_damage=0 shots=0" in caplog.text
    )


def test_with_its_nose_on_the_point_a_jedi_fires_unguided(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    decision = ask(craft(**UNGUIDED, yaw=LEAD_YAW), foe(**CORVETTE))
    assert decision.actions.steer == Steer(LEAD_YAW, LEVEL, 0)
    assert decision.actions.warhead == Warhead(0)
    assert (
        f"attack.unguided object={ME} target={FOE} launcher=0 paired=0 type=143 "
        "lock=0 ticks=575 miss=0 shots=1" in caplog.text
    )
    assert "attack.warhead " not in caplog.text
    memory = memory_of(decision)
    assert (memory.shots, memory.fresh, memory.pending) == (0, 0, 1)
    assert memory.held == warheads.Held.NONE


def test_a_hurt_jedis_unguided_pair_shows_on_its_line(craft, foe, ask, caplog):
    caplog.set_level("INFO")
    hurt = UNGUIDED | {"yaw": LEAD_YAW, "books": Books.TARGETS_LEFT | Books.HURT}
    decision = ask(craft(**hurt), foe(**CORVETTE))
    assert decision.actions.warhead == Warhead(0, paired=True)
    assert (
        f"attack.unguided object={ME} target={FOE} launcher=0 paired=1 type=143 "
        "lock=0 ticks=575 miss=0 shots=1" in caplog.text
    )


def test_while_its_launcher_reloads_a_jedi_steers_at_the_ship(craft, foe, ask) -> None:
    reloading = UNGUIDED | {"launcher_cooldown": (100, 0)}
    decision = ask(craft(**reloading, yaw=LEAD_YAW), foe(**CORVETTE))
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert memory_of(decision).held == warheads.Held.LAUNCHER


def test_below_jedi_it_steers_at_the_ship_and_waits_for_its_lock(
    craft, foe, ask
) -> None:
    decision = ask(craft(**UNGUIDED | {"skill_level": 4}), foe(**CORVETTE))
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert memory_of(decision).held == warheads.Held.LOCK


def test_a_locked_jedi_steers_at_the_ship_and_fires_guided(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    locked = UNGUIDED | {"lock_ticks": 708, "lock_state": Lock.DECLARED}
    decision = ask(craft(**locked), foe(**CORVETTE), target=FOE)
    assert decision.actions.steer == Steer(0, LEVEL, 0)
    assert decision.actions.warhead == Warhead(0)
    assert "attack.warhead object=" in caplog.text
    assert "attack.unguided" not in caplog.text


def test_a_jedi_fires_six_unguided_shots_in_a_run_then_steers_at_the_ship(
    craft, foe, ask
) -> None:
    changes = UNGUIDED | {"yaw": LEAD_YAW}
    fired = []
    for step in range(8):
        tick = 1000 + 500 * step
        decision = ask(craft(**changes), foe(**CORVETTE), tick=tick)
        fired.append(decision.actions.warhead is not None)
        waiting = changes | MADE | {"notebook": decision.notebook}
        settled = ask(craft(**waiting), foe(**CORVETTE), tick=tick + 16)
        changes = changes | {"notebook": settled.notebook}
    assert fired == [True] * 6 + [False] * 2
    assert memory_of(decision).held == warheads.Held.CAPS
    assert decision.actions.steer == Steer(0, LEVEL, 0)


# Idea E: a Sith Lord (skill level 7) with no lock leads a crossing fighter
# with its unguided shot as a Jedi leads a ship, and fires once in the run.
LORD = UNGUIDED | {"skill_level": 7}
CROSSING_FIGHTER = {
    "genus": 0,
    "size": 400,
    "hull_strength": 400,
    "y": 8_000,
    "yaw": 0x4000,
    "pitch": LEVEL,
    "speed": 150,
}
FIGHTER_LEAD_YAW = 3334


def test_a_sith_lord_steers_ahead_of_a_crossing_fighter_while_the_aim_holds(
    craft, foe, ask
) -> None:
    decision = ask(craft(**LORD), foe(**CROSSING_FIGHTER))
    assert decision.actions.steer == Steer(FIGHTER_LEAD_YAW, LEVEL, 0)
    assert memory_of(decision).held == warheads.Held.AIM


def test_with_its_nose_on_the_point_a_sith_lord_fires_unguided_at_a_fighter(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("INFO")
    decision = ask(craft(**LORD, yaw=FIGHTER_LEAD_YAW), foe(**CROSSING_FIGHTER))
    assert decision.actions.warhead == Warhead(0)
    assert (
        f"attack.unguided object={ME} target={FOE} launcher=0 paired=0 type=143 "
        "lock=0 ticks=229 miss=0 shots=1" in caplog.text
    )


def test_a_sith_lord_fires_one_unguided_shot_a_run_at_a_fighter(
    craft, foe, ask
) -> None:
    changes = LORD | {"yaw": FIGHTER_LEAD_YAW}
    fired = []
    for step in range(3):
        tick = 1000 + 500 * step
        decision = ask(craft(**changes), foe(**CROSSING_FIGHTER), tick=tick)
        fired.append(decision.actions.warhead is not None)
        waiting = changes | MADE | {"notebook": decision.notebook}
        settled = ask(craft(**waiting), foe(**CROSSING_FIGHTER), tick=tick + 16)
        changes = changes | {"notebook": settled.notebook}
    assert fired == [True, False, False]
    assert memory_of(decision).held == warheads.Held.CAPS
