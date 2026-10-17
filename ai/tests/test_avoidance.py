"""Fence for avoidance: a craft steering clear of the bulky craft in its way.

The craft flies at the origin facing +y (yaw 0), level; its behavior steers
it straight on unless a test says otherwise. The bulky craft in the way is
a bulk freighter, its clearance a ball round its position; a starship's or
platform's clearance runs off its hull, tested last with retail boxes.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import hull
from jedimaster_ai import tactics
from jedimaster_ai.behaviors import avoidance
from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import patrol
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import bearing
from jedimaster_ai.geometry import HALF_TURN
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.geometry import QUARTER_TURN
from jedimaster_ai.geometry import sweeps
from jedimaster_ai.geometry import TURN
from jedimaster_ai.think import think
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Books
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

ME = 4
BULKY = 20
STARSHIP = 4
FREIGHTER = 3
CIRCLE = 2
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


def test_over_a_big_ships_deck_a_dive_keeps_its_yaw_and_levels_off(
    destroyer, steer, caplog
) -> None:
    """6,000 over the middle of a Super Star Destroyer's deck, 1,229 over its
    keep, every yaw enters the keep: the pitches into it run from its far
    corner, 179,370 off and 71 under level (16,455), to straight down,
    24,611 and 8,157 either side. A 45-degree dive at any yaw keeps its yaw
    and takes 24,611 - 8,157 - 1,024 = 15,430, 954 above level."""
    caplog.set_level("DEBUG")
    deck = destroyer(-SSD["box_max"][0], z=-(SSD["box_max"][2] + 6000))
    for yaw in (0, 0x2000, QUARTER_TURN, 0x8000, 0xC000):
        dive = Steer(yaw, LEVEL + 0x2000, 0)
        flown = steer(deck, heading=dive, own_size=FIGHTER_SIZE)
        assert flown.actions.steer == Steer(yaw, 15430, 0)
    assert (
        f"avoidance.level object={ME} around={BULKY} distance=6000 "
        f"clearance={FIGHTER_KEEP} pitch=15430"
    ) in caplog.text
    assert "avoidance.steer" not in caplog.text


def test_under_a_big_ships_hull_a_climb_levels_off_downward(destroyer, steer) -> None:
    """6,000 under the floor, the pitches into the keep run from straight up
    to its far corner, 70 above level (16,314): a 45-degree climb takes
    8,157 + 8,157 + 1,024 = 17,338, 954 below level."""
    above = destroyer(-SSD["box_max"][0], z=-SSD["box_min"][2] + 6000)
    climb = Steer(0x1000, LEVEL - 0x2000, 0)
    flown = steer(above, heading=climb, own_size=FIGHTER_SIZE)
    assert flown.actions.steer == Steer(0x1000, 17338, 0)


def test_inside_a_big_ships_keep_over_its_deck_a_dive_levels_off(
    destroyer, steer, caplog
) -> None:
    """3,000 over the deck, inside its keep of 4,771, the headings into the
    keep lie a quarter turn either side of straight down: a dive takes
    32,768 - 16,384 - 1,024 = 15,360."""
    caplog.set_level("DEBUG")
    deck = destroyer(-SSD["box_max"][0], z=-(SSD["box_max"][2] + 3000))
    dive = Steer(0x1000, LEVEL + 0x2000, 0)
    flown = steer(deck, heading=dive, own_size=FIGHTER_SIZE)
    assert flown.actions.steer == Steer(0x1000, 15360, 0)
    assert "distance=3000 clearance=4771 pitch=15360" in caplog.text


def test_a_freighter_in_the_way_of_the_level_off_turns_its_yaw(
    destroyer, ship, steer
) -> None:
    """A bulk freighter 16,000 dead ahead at the craft's height: its ball's
    edge, 29.2 degrees (5,324), leaves out the 45-degree dive but takes in
    the level-off's pitch, so at that pitch the craft turns past it, 5,324
    + 1,024 = 6,348."""
    deck = destroyer(-SSD["box_max"][0], z=-(SSD["box_max"][2] + 6000))
    ahead = ship(16000, object_index=21)
    dive = Steer(0, LEVEL + 0x2000, 0)
    flown = steer(deck, ahead, heading=dive, own_size=FIGHTER_SIZE)
    assert flown.actions.steer == Steer(6348, 15430, 0)


def test_inside_the_keeps_of_two_hulls_only_the_nearer_levels_it_off(
    view, destroyer, steer
) -> None:
    """3,000 over a Super Star Destroyer's deck and 4,000 under a Star
    Destroyer's floor, inside both keeps: the nearer, the deck, levels the
    dive off to 15,360, which climbs into the Star Destroyer's keep, every
    yaw of it. No yaw clears that, so the yaw is kept; weighed for a yaw
    turn, the roof's keep would turn the craft away from a yaw a quarter
    turn off the roof's nearest point, straight up, which names no side."""
    me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
    deck = destroyer(-SSD["box_max"][0], z=-(SSD["box_max"][2] + 3000))
    roof = destroyer(
        -ISD["box_max"][0],
        z=-ISD["box_min"][2] + 4000,
        model=ISD | {"object_index": 22},
    )
    looks = hull.seen(me, roof, FIGHTER_KEEP)
    assert looks.yaw_edge == hull.ALL_ROUND
    assert angle_between(15360, looks.pitch) < looks.pitch_edge
    dive = Steer(0x1000, LEVEL + 0x2000, 0)
    flown = steer(deck, roof, heading=dive, own_size=FIGHTER_SIZE)
    assert flown.actions.steer == Steer(0x1000, 15360, 0)


def test_over_two_decks_the_nearer_levels_the_heading_off(ship, steer) -> None:
    """6,000 over a flat hull 40,000 wide and 1,000 deep, and 10,000 over a
    small one under it, 2,000 a side: a 45-degree dive enters both keeps,
    every yaw of each (the small one's pitches 27,548 and 5,220 either
    side). The nearer decides, 24,759 - 8,009 - 1,024 = 15,726, which
    clears both; the small one's edge, 27,548 - 5,220 - 1,024 = 21,304,
    would still dive at the flat one."""
    flat = {"box_min": (-20000, -20000, -500), "box_max": (20000, 20000, 500)}
    small = {"box_min": (-1000, -1000, -1000), "box_max": (1000, 1000, 1000)}
    level = {"x": 0, "yaw": 0, "pitch": LEVEL, "roll": 0, "genus": STARSHIP}
    upper = ship(0, z=-6500, **level, **flat)
    lower = ship(0, z=-11000, object_index=22, **level, **small)
    dive = Steer(0x1000, LEVEL + 0x2000, 0)
    flown = steer(upper, lower, heading=dive, own_size=FIGHTER_SIZE)
    assert flown.actions.steer == Steer(0x1000, 15726, 0)


def test_a_patrols_cruise_over_a_big_ships_deck_levels_off(
    view, contact, destroyer
) -> None:
    """A Circle craft 6,000 over the deck cruises to its waypoint 45 degrees
    down to the east: its steer carries the pitch avoidance levels the dive
    off to (``avoidance.clear_heading``), which the patrol reads its
    starship's throttle beside (``patrol.cruise``)."""
    point = (0x10000, 0, -0x10000)
    me = view(
        object_index=ME,
        x=0,
        y=0,
        z=0,
        yaw=QUARTER_TURN,
        pitch=LEVEL,
        roll=0,
        order_code=CIRCLE,
        order_codes=(CIRCLE, 0, 0, 0),
        order_throttle=0x8000,
        waypoint_mask=1,
        waypoint_x=(point[0], *[0] * 7),
        waypoint_y=(point[1], *[0] * 7),
        waypoint_z=(point[2], *[0] * 7),
    )
    own = contact(
        object_index=ME, genus=0, size=FIGHTER_SIZE, x=0, y=0, z=0, target=NO_OBJECT
    )
    model = SSD | {"target": NO_OBJECT}
    deck = destroyer(-SSD["box_max"][0], z=-(SSD["box_max"][2] + 6000), model=model)
    question = Question(1000, (me,), (own, deck))
    yaw, pitch = bearing(*point)
    leveled = avoidance.clear_heading(me, question, yaw, pitch, NO_OBJECT)
    assert leveled == (yaw, 15430)
    assert patrol.think(me, question).actions.steer == Steer(yaw, 15430, 0)


def test_over_a_big_ships_deck_it_is_in_the_way_of_a_dive_at_every_yaw(
    view, contact, destroyer
) -> None:
    """The attack's break weighs its heading with ``in_the_way``: over the
    deck, a dive at any yaw names the ship, which looks as wide as a ball of
    its distance, 6,000; a level heading is still free."""
    me = view(object_index=ME, x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)
    own = contact(object_index=ME, genus=0, size=FIGHTER_SIZE, x=0, y=0, z=0)
    deck = destroyer(-SSD["box_max"][0], z=-(SSD["box_max"][2] + 6000))
    question = Question(1000, (me,), (own, deck))
    for yaw in range(0, TURN, 0x2000):
        found = avoidance.in_the_way(me, question, yaw, LEVEL + 0x2000, NO_OBJECT)
        assert found.contact.object_index == BULKY and found.all_round
        assert found.as_ball == 6000
    assert avoidance.in_the_way(me, question, 0, LEVEL, NO_OBJECT) is None


def test_past_the_pitch_edge_lies_on_the_level_side_within_up_and_down(
    contact,
) -> None:
    """Built by hand, a clearance whose pitch spread is the only way out:
    level with the craft, the asked pitch's side, up for a level pitch;
    a spread wider than the way to straight up or down stops there."""

    def ahead(pitch, edge):
        return avoidance.Obstacle(
            contact(), 0, 0, 0, hull.ALL_ROUND, pitch, edge, (0, 0, 0), 0
        )

    assert ahead(LEVEL, 1000).past_pitch(LEVEL + 1) == LEVEL + 2024
    assert ahead(LEVEL, 1000).past_pitch(LEVEL) == LEVEL - 2024
    assert ahead(LEVEL + 100, 20000).past_pitch(LEVEL + 200) == 0
    assert ahead(LEVEL - 100, 20000).past_pitch(LEVEL - 200) == HALF_TURN


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


# Retail 8xicw12's parked hangar starship (type 53, flight group 8) and its two
# Drop Off carriers' points, as the W4-0c landing flight logged them (issue
# #311): fg 9's point lies 6,817 under its box, fg 10's 3,033 off it, under it
# and past its side, both inside its keep of 4,096 and a carrier's reach.
PARKED = {
    "object_index": 8,
    "genus": STARSHIP,
    "size": 65396,
    "x": 0,
    "y": -10240,
    "z": 3072,
    "yaw": 0,
    "pitch": LEVEL,
    "roll": 0,
    "box_min": (-19139, -40940, -7132),
    "box_max": (19140, 24456, 12569),
}
FG9_POINT = (12288, -8192, -10877)
FG10_POINT = (20480, -6144, -6781)
# The parked starship's box in the world: its east, north and south faces,
# its floor.
EAST_FACE = 19140
NORTH_FACE = 30700
SOUTH_FACE = -34696
FLOOR = -4060
# Where obj 3 (fg 9's carrier) first turned off toward the keep's edge, t77,751.
OBJ3_TURNED = (32264, -24764, -10264)


@pytest.fixture
def approach(view, contact):
    """Return the heading and the decision after avoidance of a bulk
    freighter at a place on Drop Off's approach, steering dead on its drop
    point as ``dropoff`` does (``off`` turns it aside), its nose on that
    heading unless named; the parked starship and the others given about."""

    def decide(at, point, *others, books=Books.DROP_POINT, off=(0, 0), nose=None):
        yaw, pitch = bearing(point[0] - at[0], point[1] - at[1], point[2] - at[2])
        heading = Steer((yaw + off[0]) % TURN, pitch + off[1], 0)
        me = view(
            object_index=ME,
            x=at[0],
            y=at[1],
            z=at[2],
            yaw=heading.yaw if nose is None else nose,
            pitch=pitch,
            roll=0,
            books=books,
            drop_point=point,
        )
        own = contact(
            object_index=ME,
            genus=FREIGHTER,
            size=BULK_FREIGHTER_SIZE,
            x=at[0],
            y=at[1],
            z=at[2],
        )
        actions = Actions(steer=heading, throttle=0xFFFF, target=NO_OBJECT)
        question = Question(1000, (me,), (own, contact(**PARKED), *others))
        decision = avoidance.steer_clear(me, question, Decision(NOTEBOOK, actions))
        return heading, decision

    return decide


def test_a_drop_off_carrier_flies_straight_in_under_a_parked_starship(
    approach, caplog
) -> None:
    """Where the W4-0c landing flight's carrier for fg 9 first turned off
    toward the parked starship's keep, its straight line to its point keeps
    6,603 off the box, past its reach of 3,720: it flies in. Without the
    drop point booked it turns aside, as every carrier did."""
    caplog.set_level("DEBUG")
    heading, flown = approach(OBJ3_TURNED, FG9_POINT)
    assert flown.actions.steer == heading
    assert (
        f"avoidance.way_in object={ME} around=8 off=6817 least=6603 clear=1"
        in caplog.text
    )
    heading, turned = approach(OBJ3_TURNED, FG9_POINT, books=0)
    assert turned.actions.steer != heading


def test_a_point_nearer_the_hull_than_the_carriers_reach_is_flown_to(
    approach, caplog
) -> None:
    """fg 10's point lies 3,033 off the box, less than the carrier's reach: a
    line closing on the box all the way there flies in (its carrier where it
    first came near the keep, t92,418); one that passes 2,722 under the box
    first, from the ship's far side (t119,313), steers round."""
    caplog.set_level("DEBUG")
    heading, flown = approach((33919, 9822, -6782), FG10_POINT)
    assert flown.actions.steer == heading
    assert "off=3033 least=3033 clear=1" in caplog.text
    heading, turned = approach((5162, -45215, -6782), FG10_POINT)
    assert turned.actions.steer != heading
    assert "off=3033 least=2722 clear=0" in caplog.text


def test_a_line_past_a_hulls_edge_is_measured_straight_off_it(approach, caplog) -> None:
    """A line passing the box's lower east edge 2,700 out from each of its
    two faces passes 3,818 off the box, past the carrier's reach, though
    nearer than that to each face's plane: it flies in to a point 7,200 off
    the east face."""
    caplog.set_level("DEBUG")
    edge = (EAST_FACE + 2700, -20000, FLOOR - 2700)
    start = (edge[0] - 10000, edge[1], edge[2] - 10000)
    point = (edge[0] + 4500, edge[1], edge[2] + 4500)
    heading, flown = approach(start, point)
    assert flown.actions.steer == heading
    assert "off=7200 least=3818 clear=1" in caplog.text


def test_a_line_through_the_hull_steers_round(approach, caplog) -> None:
    """From off the parked starship's north face the line to fg 9's point
    enters its box: the carrier steers round."""
    caplog.set_level("DEBUG")
    heading, turned = approach((15000, NORTH_FACE + 6000, 15000), FG9_POINT)
    assert turned.actions.steer != heading
    assert "off=6817 least=0 clear=0" in caplog.text


def test_only_a_heading_dead_on_the_point_goes_in(approach) -> None:
    """A carrier whose decision steers elsewhere, as one sent home with its
    drop point still booked, keeps the parked starship's keep: a yaw or a
    pitch 5 units off the point is not the approach, 4 is."""
    for off in ((4, 0), (0, 4)):
        heading, flown = approach(OBJ3_TURNED, FG9_POINT, off=off)
        assert flown.actions.steer == heading
    for off in ((5, 0), (0, 5)):
        heading, turned = approach(OBJ3_TURNED, FG9_POINT, off=off)
        assert turned.actions.steer != heading


def test_another_bulky_craft_on_the_line_is_steered_round(approach, ship) -> None:
    """A freighter halfway along the line, its clearance short of the point,
    turns the carrier aside: only a craft whose clearance holds the point is
    passed."""
    half = [(a + b) // 2 for a, b in zip(OBJ3_TURNED, FG9_POINT, strict=True)]
    crossing = ship(half[1], x=half[0], z=half[2], object_index=21)
    heading, turned = approach(OBJ3_TURNED, FG9_POINT, crossing)
    assert turned.actions.steer != heading


def test_a_craft_whose_clearance_misses_the_point_is_steered_round(
    approach, ship
) -> None:
    """A freighter 7,600 beside the line's middle: the heading clips its
    clearance, though the line passes 3,878 off its body, past the carrier's
    reach; its clearance does not hold the point, so the carrier steers
    round it."""
    beside = ship(-22327, x=17424, z=-10571, object_index=21)
    heading, turned = approach(OBJ3_TURNED, FG9_POINT, beside)
    assert turned.actions.steer != heading


def test_every_craft_whose_clearance_holds_the_point_is_passed(
    approach, ship, caplog
) -> None:
    """A carrier parked 5,000 beside fg 9's point holds it in its clearance
    too: 15,000 short of the point on obj 3's line, both are in the way, and
    a line keeping 6,603 off the starship's box and 1,279 off the parked
    carrier's ball, its point's own distance, flies in past both."""
    caplog.set_level("DEBUG")
    parked = ship(-4344, x=15480, z=-10877, object_index=21)
    heading, flown = approach((23829, -17767, -10523), FG9_POINT, parked)
    assert flown.actions.steer == heading
    assert "around=8 off=6817 least=6603 clear=1" in caplog.text
    assert "around=21 off=1279 least=1279 clear=1" in caplog.text


def test_a_point_inside_the_box_is_never_flown_to(approach, caplog) -> None:
    """A point inside the parked starship's box lies 0 off it: a carrier off
    its south face steers round."""
    caplog.set_level("DEBUG")
    heading, turned = approach(
        (3000, SOUTH_FACE - 6000, -9000), (0, -10240, FLOOR + 1000)
    )
    assert turned.actions.steer != heading
    assert "off=0 least=0 clear=0" in caplog.text


def test_the_turn_in_never_swings_the_nose_across_the_hull(approach) -> None:
    """5,000 off the parked starship's east face, inside its keep, a carrier
    nosed south turns in to a point 3,000 off the face to its south-west;
    nosed north-north-west, the shortest turn there swings its nose across
    the hull, due west, and it steers round instead."""
    at = (EAST_FACE + 5000, -20000, 0)
    point = (EAST_FACE + 3000, -22000, 0)
    heading, flown = approach(at, point, nose=QUARTER_TURN * 2)
    assert flown.actions.steer == heading
    heading, turned = approach(at, point, nose=0xF000)
    assert turned.actions.steer != heading


def test_a_carrier_diving_from_over_a_parked_starship_levels_off(
    approach, caplog
) -> None:
    """1,543 over the parked starship's keep (its top 9,359 under the carrier,
    the keep 4,096 and the carrier's reach 3,720), the dive to fg 9's point
    under the ship enters the box (least=0), and every yaw enters the keep:
    the pitches into it run from its far corner, 75,848 off and 214 under
    level (16,598), to straight down, 24,683 and 8,085 either side. The
    carrier keeps its yaw and takes 24,683 - 8,085 - 1,024 = 15,574. It once
    kept the dive: the yaws in the way were a quarter turn either side of
    yaw 16,384, the yaw straight down, and the dive's lay 17,750 off it."""
    caplog.set_level("DEBUG")
    heading, flown = approach((16000, 20000, 25000), FG9_POINT)
    assert heading == Steer(34134, 25776, 0)
    assert flown.actions.steer == Steer(34134, 15574, 0)
    assert "avoidance.way_in object=4 around=8 off=6817 least=0 clear=0" in caplog.text
    assert (
        f"avoidance.level object={ME} around=8 distance=9359 clearance=7816 pitch=15574"
    ) in caplog.text
