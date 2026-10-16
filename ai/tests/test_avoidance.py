"""Fence for avoidance: a craft steering clear of the bulky craft in its way.

The craft flies at the origin facing +y (yaw 0), level; its behavior steers
it straight on unless a test says otherwise. The bulky craft in the way is
a bulk freighter, its clearance a ball round its position; a starship's or
platform's clearance runs off its hull, tested last with retail boxes.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import tactics
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import bearing
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.geometry import QUARTER_TURN
from jedimaster_ai.geometry import sweeps
from jedimaster_ai.geometry import TURN
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
BULKY = 20
STARSHIP = 4
FREIGHTER = 3
BULK_FREIGHTER_SIZE = 4960
FIGHTER_SIZE = 901
# The nearest two bulky craft of one group started, in the flights measured.
SPAWN_NEAREST = 17685
AHEAD = Steer(0, LEVEL, 0)
NOTEBOOK = bytes(range(NOTEBOOK_BYTES))


@pytest.fixture
def ship(contact):
    def make(y, **changes):
        theirs = {
            "object_index": BULKY,
            "genus": FREIGHTER,
            "size": BULK_FREIGHTER_SIZE,
            "x": 0,
            "y": y,
            "z": 0,
        }
        return contact(**(theirs | changes))

    return make


@pytest.fixture
def steer(view, contact):
    """Return the decision after avoidance, from a decision steering as given."""

    def decide(
        *others,
        heading=AHEAD,
        target=None,
        own_genus=0,
        own_target=NO_OBJECT,
        nose=0,
        own_size=0,
    ):
        me = view(object_index=ME, x=0, y=0, z=0, yaw=nose, pitch=LEVEL, roll=0)
        own = contact(
            object_index=ME,
            genus=own_genus,
            size=own_size,
            x=0,
            y=0,
            z=0,
            target=own_target,
        )
        actions = Actions(steer=heading, throttle=0x8000, target=target)
        question = Question(1000, (me,), (own, *others))
        return avoidance.steer_clear(me, question, Decision(NOTEBOOK, actions))

    return decide


@pytest.fixture
def freighter(ship):
    def make(index, x, y):
        return ship(
            y, x=x, object_index=index, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE
        )

    return make


def keep_of(ship) -> int:
    return tactics.clearance(ship)


def test_a_frigate_dead_ahead_turns_the_craft_past_its_edge(ship, steer) -> None:
    bulky = ship(16000)
    edge = avoidance.edge_angle(16000, keep_of(bulky))
    decision = steer(bulky)
    assert decision.actions.steer == Steer(edge + avoidance.EDGE_PAD, LEVEL, 0)
    assert decision.actions.throttle == 0x8000
    assert decision.notebook == NOTEBOOK


def test_the_nose_picks_the_side_so_the_turn_never_crosses_the_frigate(
    ship, steer
) -> None:
    """The behavior's heading lies a little left of the bulky dead ahead
    and the nose a little right: the craft goes round the right, the side
    its nose is on; the left edge would swing its nose across the bulky
    (AI5e's Y-wing in raw10). With the nose dead on, the heading's side."""
    bulky = ship(16000)
    clockwise = avoidance.edge_angle(16000, keep_of(bulky)) + avoidance.EDGE_PAD
    left = Steer(TURN - 0x200, LEVEL, 0)
    turned = steer(bulky, heading=left, nose=0x800)
    assert turned.actions.steer == Steer(clockwise, LEVEL, 0)
    dead_on = steer(bulky, heading=left, nose=0)
    assert dead_on.actions.steer == Steer(TURN - clockwise, LEVEL, 0)


def test_a_frigate_past_the_look_ahead_is_not_yet_in_the_way(ship, steer) -> None:
    keep = keep_of(ship(0))
    assert steer(ship(keep + avoidance.LOOK_AHEAD)).actions.steer == AHEAD
    assert steer(ship(keep + avoidance.LOOK_AHEAD - 1)).actions.steer != AHEAD


def test_a_heading_past_the_edge_is_kept(ship, steer) -> None:
    bulky = ship(16000)
    edge = avoidance.edge_angle(16000, keep_of(bulky))
    beside = Steer(edge, LEVEL, 0)
    assert steer(bulky, heading=beside).actions.steer == beside
    inside = Steer(edge - 1, LEVEL, 0)
    assert steer(bulky, heading=inside).actions.steer != inside
    over = Steer(0, LEVEL - edge, 0)
    assert steer(bulky, heading=over).actions.steer == over


def test_the_craft_turns_to_the_side_its_heading_lies_on(ship, steer) -> None:
    bulky = ship(16000)
    turn = avoidance.edge_angle(16000, keep_of(bulky)) + avoidance.EDGE_PAD
    right = steer(bulky, heading=Steer(0x100, LEVEL, 0))
    assert right.actions.steer.yaw == turn
    left = steer(bulky, heading=Steer(TURN - 0x100, LEVEL, 0))
    assert left.actions.steer.yaw == TURN - turn


def test_inside_a_clearance_the_craft_turns_outward(ship, steer) -> None:
    decision = steer(ship(5000))
    assert decision.actions.steer.yaw == QUARTER_TURN + avoidance.EDGE_PAD
    assert angle_between(decision.actions.steer.yaw, 0) > QUARTER_TURN


def test_its_target_is_left_to_its_behavior(ship, steer) -> None:
    assert steer(ship(16000), target=BULKY).actions.steer == AHEAD
    assert steer(ship(16000), own_target=BULKY).actions.steer == AHEAD
    assert steer(ship(16000), target=NO_OBJECT, own_target=BULKY).actions.steer != AHEAD


@pytest.mark.parametrize(
    ("genus", "turned"), [(0, False), (1, False), (3, True), (4, True), (5, True)]
)
def test_only_bulky_craft_are_steered_around(ship, steer, genus, turned) -> None:
    assert (steer(ship(10000, genus=genus)).actions.steer != AHEAD) == turned


def test_a_bulky_craft_steers_clear_of_other_bulky_craft(ship, steer) -> None:
    bulky = ship(16000)
    edge = avoidance.edge_angle(16000, keep_of(bulky))
    decision = steer(bulky, own_genus=STARSHIP)
    assert decision.actions.steer == Steer(edge + avoidance.EDGE_PAD, LEVEL, 0)
    assert steer(ship(16000), own_genus=FREIGHTER).actions.steer != AHEAD


@pytest.mark.parametrize("own_genus", [0, FREIGHTER, STARSHIP])
def test_no_craft_steers_clear_of_a_small_craft(ship, steer, own_genus) -> None:
    fighter = ship(3000, genus=0, size=FIGHTER_SIZE)
    assert steer(fighter, own_genus=own_genus).actions.steer == AHEAD


def test_a_bulky_group_starts_outside_its_neighbours_look_ahead(ship, steer) -> None:
    """Two freighters of one group were seen to start 17,685 units apart at
    the least (three retail missions flown, one networked); dead ahead at
    that distance a freighter's neighbour is not yet in its way, so a
    group's starting formation is not turned apart."""
    freighter = ship(SPAWN_NEAREST, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    assert keep_of(freighter) + avoidance.LOOK_AHEAD < SPAWN_NEAREST
    assert steer(freighter, own_genus=FREIGHTER).actions.steer == AHEAD


def test_in_the_way_names_the_nearest_and_skips_one(view, contact, ship) -> None:
    me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
    near = ship(8000, object_index=21, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    question = Question(1000, (me,), (ship(16000), near))
    found = avoidance.in_the_way(me, question, 0, LEVEL, NO_OBJECT)
    assert (found.contact.object_index, found.distance) == (21, 8000)
    assert found.clearance == keep_of(near)
    assert (found.yaw, found.edge) == (0, avoidance.edge_angle(8000, keep_of(near)))
    assert (
        avoidance.in_the_way(me, question, 0, LEVEL, 21).contact.object_index == BULKY
    )
    assert avoidance.in_the_way(me, question, QUARTER_TURN, LEVEL, NO_OBJECT) is None


def test_a_craft_that_does_not_steer_keeps_drifting(ship, steer) -> None:
    decision = steer(ship(16000), heading=None)
    assert decision.actions == Actions(throttle=0x8000)


def test_of_two_in_the_way_the_nearest_decides(ship, steer) -> None:
    freighter = ship(8000, object_index=21, genus=3, size=4960)
    edge = avoidance.edge_angle(8000, keep_of(freighter))
    decision = steer(ship(16000), freighter)
    assert decision.actions.steer.yaw == edge + avoidance.EDGE_PAD


def test_every_order_behavior_steers_clear(view, contact, ship) -> None:
    me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
    own = contact(object_index=ME, genus=0, x=0, y=0, z=0, target=NO_OBJECT)
    straight_on = {
        me.order_code: lambda view, question: Decision(NOTEBOOK, Actions(steer=AHEAD))
    }
    answer = think(Question(1000, (me,), (own, ship(16000))), straight_on)
    assert answer.craft[0].actions.steer != AHEAD


def test_a_break_steers_clear_of_its_target_too(freighter, steer) -> None:
    """Inside its target's clearance, a craft breaks off eastward from a
    freighter to its west, and another freighter north-east lies in that
    way. Going round that one on the nose's side would point it back toward
    its target (yaw 64,886, AI5e-2's answer); the target counts while the
    heading asked does not aim at it, so it turns on round, away from both.
    A heading aimed at the target is the behavior's (AI5e-3: in the gap
    toy, breaks turned into their own target)."""
    target, beside = freighter(9, -6000, 0), freighter(10, 8000, 5000)
    east = Steer(QUARTER_TURN, LEVEL, 0)
    turned = steer(target, beside, heading=east, target=9).actions.steer.yaw
    assert turned == 0x4A00
    assert angle_between(turned, 3 * QUARTER_TURN) > QUARTER_TURN
    west = Steer(3 * QUARTER_TURN, LEVEL, 0)
    assert steer(target, beside, heading=west, target=9).actions.steer == west


def test_the_turn_back_to_a_target_goes_the_long_way_round_a_neighbour(
    freighter, steer
) -> None:
    """Inside a neighbour's clearance, a craft turns back toward its target
    beyond it; the shortest way swings its nose across the neighbour (the
    escort network game's bombers 9 and 10, AI5e-3's fourth cause), so it
    turns the long way, a quarter turn at a time. A neighbour outside its
    clearance is no wall: the heading is kept."""
    target = freighter(9, -7000, -5000)
    back = Steer(bearing(-7000, -5000, 0)[0], LEVEL, 0)
    assert sweeps(0, back.yaw, bearing(-3000, 5000, 0)[0])
    inside = freighter(10, -3000, 5000)
    turned = steer(target, inside, heading=back, target=9).actions.steer
    assert turned == Steer(QUARTER_TURN, LEVEL, 0)
    outside = freighter(10, -6000, 10000)
    assert steer(target, outside, heading=back, target=9).actions.steer == back


def test_between_two_the_craft_takes_the_widest_pass(freighter, steer) -> None:
    """Between two freighters, inside both clearances, with each an eighth
    of a turn off the nose: a turn out either way swings the nose across
    one, so it flies on between them, on the heading whose nearer pass is
    widest (3,278 and 3,335 units), not halfway between their bearings
    (2,693 from the nearer: AI5e-3's draft ranked by angle and grazed the
    nearer freighter in the gap toy)."""
    nearer, farther = freighter(9, 3000, 3000), freighter(10, -5000, 5000)
    assert steer(nearer, farther).actions.steer.yaw == 0xF800


def test_a_nose_dead_on_a_craft_it_is_inside_turns_out_past_both(
    freighter, steer
) -> None:
    """The nose points dead at a freighter whose clearance the craft is
    inside, a second freighter to the right: going round the first on the
    heading's side would point it at the second (yaw 17,408); the search
    turns it left, out past both edges. A wall dead on the nose blocks
    neither way (AI5e-3's draft found no heading at all)."""
    ahead, right = freighter(10, 0, 5000), freighter(11, 7000, 0)
    assert steer(ahead, right).actions.steer.yaw == 0xBC00


# ---- A starship's or platform's hull (ruling 94) ----

# Retail boxes (``test_hull.py``'s source) and sizes (``test_tactics.py``).
ISD = {
    "genus": STARSHIP,
    "size": 65396,
    "box_min": (-19140, -40940, -7132),
    "box_max": (19140, 24456, 12570),
}
SSD = {
    "genus": STARSHIP,
    "size": 327683,
    "box_min": (-52487, -162468, -11526),
    "box_max": (52487, 165215, 13158),
}
# A fighter keeps 4,096 and three quarters of its own size off a hull.
FIGHTER_KEEP = 4096 + FIGHTER_SIZE * 3 // 4
EAST = Steer(QUARTER_TURN, LEVEL, 0)


@pytest.fixture
def destroyer(ship):
    """Return a Super Star Destroyer, or the model named, level at yaw 0, its
    length along y, its flank ``off`` east of the craft."""

    def make(off, z=0, model=SSD):
        x = model["box_max"][0] + off
        return ship(0, x=x, z=z, yaw=0, pitch=LEVEL, roll=0, **model)

    return make


def test_toward_or_along_a_big_ships_flank_the_craft_keeps_its_heading(
    destroyer, steer
) -> None:
    """20,000 off a Super Star Destroyer's flank, a heading 45 degrees toward
    it is not yet in the way: the ball its clearance once kept round its
    position, 249,858 units, turned it (to yaw 0xFC00, measured at
    ``796786e``). 8,000 off, a heading along the flank is free."""
    toward = Steer(0x2000, LEVEL, 0)
    far = destroyer(20000)
    assert tactics.clearance(far) == 249858
    assert steer(far, heading=toward, own_size=FIGHTER_SIZE).actions.steer == toward
    assert steer(destroyer(8000), own_size=FIGHTER_SIZE).actions.steer == AHEAD


def test_a_heading_into_a_big_ships_flank_turns_along_it(
    destroyer, steer, caplog
) -> None:
    """A heading 22.5 degrees into the flank 8,000 off turns to 4.5 degrees
    the other side of north, the nose's side: the headings into the hull's
    keep spread 88.9 degrees either side of east, and EDGE_PAD is 5.6."""
    caplog.set_level("DEBUG")
    into = Steer(0x1000, LEVEL, 0)
    turned = steer(destroyer(8000), heading=into, own_size=FIGHTER_SIZE)
    assert turned.actions.steer == Steer(0xFCCA, LEVEL, 0)
    assert (
        f"avoidance.steer object={ME} around={BULKY} distance=8000 "
        f"clearance={FIGHTER_KEEP} yaw={0xFCCA}"
    ) in caplog.text


def test_a_big_ship_counts_within_look_ahead_of_its_hulls_keep(
    destroyer, steer
) -> None:
    far = FIGHTER_KEEP + avoidance.LOOK_AHEAD
    assert (
        steer(destroyer(far), heading=EAST, own_size=FIGHTER_SIZE).actions.steer == EAST
    )
    near = steer(destroyer(far - 1), heading=EAST, own_size=FIGHTER_SIZE)
    assert near.actions.steer != EAST


def test_inside_a_big_ships_keep_the_craft_turns_away_from_its_hull(
    destroyer, steer
) -> None:
    into = Steer(0x3000, LEVEL, 0)
    turned = steer(destroyer(3000), heading=into, nose=0x3000, own_size=FIGHTER_SIZE)
    assert turned.actions.steer.yaw == TURN - avoidance.EDGE_PAD


def test_a_starship_keeps_its_own_reach_off_another_hull(destroyer, steer) -> None:
    """40,000 off a Star Destroyer's flank, another Star Destroyer is inside
    its keep, 4,096 and three quarters of its own 65,396; a fighter there
    is not yet in the way."""
    isd = destroyer(40000, model=ISD)
    big = steer(isd, heading=EAST, own_genus=STARSHIP, own_size=ISD["size"])
    assert big.actions.steer.yaw == TURN - avoidance.EDGE_PAD
    assert steer(isd, heading=EAST, own_size=FIGHTER_SIZE).actions.steer == EAST


def test_over_a_big_ships_deck_a_level_heading_is_free(destroyer, steer) -> None:
    """6,000 over the middle of a Super Star Destroyer's deck, every heading
    into its keep points down."""
    deck = destroyer(-SSD["box_max"][0], z=-(SSD["box_max"][2] + 6000))
    assert steer(deck, heading=EAST, own_size=FIGHTER_SIZE).actions.steer == EAST


def test_past_a_hulls_edge_a_heading_passes_at_its_keep(
    view, contact, destroyer
) -> None:
    """The search weighs a hull as the ball that looks as wide: a heading
    EDGE_PAD past the edge passes at the keep, a 256th of a turn nearer cuts
    into it, and from inside the keep every heading cuts into it."""
    me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
    own = contact(object_index=ME, genus=0, size=FIGHTER_SIZE, x=0, y=0, z=0)
    outside = Question(1000, (me,), (own, destroyer(8000)))
    found = avoidance.in_the_way(me, outside, EAST.yaw, LEVEL, NO_OBJECT)
    past = found.yaw + found.edge + avoidance.EDGE_PAD
    assert found.passes(past % TURN) == FIGHTER_KEEP
    assert found.passes((past - 0x100) % TURN) < FIGHTER_KEEP
    inside = Question(1000, (me,), (own, destroyer(3000)))
    wall = avoidance.in_the_way(me, inside, EAST.yaw, LEVEL, NO_OBJECT)
    assert max(wall.passes(yaw) for yaw in range(0, TURN, 0x100)) == 3000
