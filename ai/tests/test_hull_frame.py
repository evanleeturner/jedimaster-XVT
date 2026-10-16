"""Fence for the frame a model sits in: the engine's answers, read from the
file its own test writes, and the program's frame (``hull``) against them:
the same axes, the same world point for each model point and back, and the
nearest part of a box.

The rows of ``data/hull_frame.txt`` are made by the engine's integer math
(``engine/tests/test_ai_hull_frame.c``, which asserts every value exactly and
writes the file again with ``--write``). One row is one point of a model box
in one pose: the pose, the pose's cached side, up and forward axes (1.15 fixed
point, one is 32,767), the model point in the model's own axes, the local point
the engine takes it as, and the world point.

Dials the comparisons run under: integers only on the program's side; the
engine's sine and cosine are libm's, truncated to 1.15 (``trig2.c``); the
reference for an angle is the exact unit vector.
"""

from __future__ import annotations

import pathlib

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import hull
from jedimaster_ai.geometry import forward

DATA = pathlib.Path(__file__).resolve().parent / "data" / "hull_frame.txt"
COLUMNS = (
    "case yaw pitch roll position_x position_y position_z side_x side_y side_z "
    "up_x up_y up_z fwd_x fwd_y fwd_z model_x model_y model_z local_side local_up "
    "local_fwd world_x world_y world_z"
).split()
LEVEL_PITCH = 0x4000
ROLLS = (0x2000, 0xC000)
Row = dict[str, int]

# One 1.15 unit, in the 65,536ths ``geometry.forward`` answers in: one is
# 32,767 there, 65,536 here.
Q15_UNIT = 65536 / 32767
# The program's sine is within 20 65,536ths of the exact one (test_geometry);
# a heading's x and y are two sines multiplied and shifted down, so each error
# adds, with 1 for the shift's floor. Its z is one sine.
PROGRAM_SINE_ERROR = 20
PROGRAM_FORWARD_ERROR = 2 * PROGRAM_SINE_ERROR + 1
# How far the engine's cached axes sit from the exact unit vectors, in 1.15
# units, measured: the reference is the exact vector from double-precision sine
# and cosine; the engine's path is fview_calcrotatemove then
# fview_calcrotateorient, its sine and cosine truncated to 1.15, and the roll
# turns all three rows through a matrix of rounded terms, so the forward axis
# carries the roll's roundings too. Over 2.2 million random poses (three seeds)
# the worst gap was 25.8 for forward, 10.9 for side and 10.8 for up; 32 is that
# worst with a quarter's margin, and holds for every axis.
ENGINE_AXIS_UNITS = 32
FORWARD_TOLERANCE = PROGRAM_FORWARD_ERROR + ENGINE_AXIS_UNITS * Q15_UNIT
# A world point is the position plus three products of a local coordinate and
# an axis, each shifted down by 15 bits and rounded by at most 1.
WORLD_POINT_TOLERANCE = 3


def read_rows() -> list[Row]:
    rows = []
    for line in DATA.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        values = [int(word) for word in line.split()]
        assert len(values) == len(COLUMNS)
        rows.append(dict(zip(COLUMNS, values, strict=True)))
    return rows


ROWS = read_rows()


def poses() -> list[Row]:
    """Return the first row of each case, the poses."""
    found: dict[int, Row] = {}
    for row in ROWS:
        found.setdefault(row["case"], row)
    return list(found.values())


def axis(row: Row, name: str) -> tuple[int, int, int]:
    return row[f"{name}_x"], row[f"{name}_y"], row[f"{name}_z"]


def test_the_file_holds_the_cases_rows_and_columns() -> None:
    assert DATA.read_text().startswith("# ")
    assert len(ROWS) == 70
    assert [len([r for r in ROWS if r["case"] == case]) for case in range(7)] == [
        10
    ] * 7
    assert all(abs(row["position_x"]) > 1000 for row in ROWS)


@pytest.mark.parametrize("pose", poses(), ids=lambda pose: f"case{pose['case']}")
def test_the_forward_axis_is_the_heading_the_program_computes(pose: Row) -> None:
    heading = forward(pose["yaw"], pose["pitch"])
    for found, cached in zip(heading, axis(pose, "fwd"), strict=True):
        assert abs(found - cached * Q15_UNIT) <= FORWARD_TOLERANCE


def test_a_model_point_is_taken_as_side_up_and_forward() -> None:
    for row in ROWS:
        assert row["local_side"] == row["model_x"]
        assert row["local_up"] == row["model_z"]
        assert row["local_fwd"] == -row["model_y"]


def test_the_world_point_is_the_position_plus_the_turned_local_point() -> None:
    local = ("local_side", "local_up", "local_fwd")
    for row in ROWS:
        for at, axis_name in enumerate("xyz"):
            turned = sum(
                row[local_name] * axis(row, name)[at] / 32768
                for local_name, name in zip(local, ("side", "up", "fwd"), strict=True)
            )
            found = row[f"world_{axis_name}"] - row[f"position_{axis_name}"]
            assert abs(found - turned) <= WORLD_POINT_TOLERANCE


def test_the_roll_turns_the_side_axis_up_for_a_positive_roll() -> None:
    by_roll = {
        pose["roll"]: pose
        for pose in poses()
        if pose["pitch"] == LEVEL_PITCH and pose["roll"] in ROLLS
    }
    assert by_roll[0x2000]["side_z"] > 20000
    assert by_roll[0xC000]["side_z"] < -32000


def test_the_nose_up_and_nose_down_poses_point_along_z() -> None:
    near_up = next(pose for pose in poses() if pose["pitch"] == 0x0100)
    near_down = next(pose for pose in poses() if pose["pitch"] == 0x7F00)
    assert near_up["fwd_z"] > 32000
    assert near_down["fwd_z"] < -32000


# The program's frame against the engine's: ``hull`` turns the same model
# points into the same world points, and back.

# ``hull.axes`` against the exact axes, in 65,536ths: a rolled component sums
# a product of three sines and a product of two, each sine within
# PROGRAM_SINE_ERROR, with a floor after each of the two shifts. Measured over
# this file's poses: 11.6.
PROGRAM_AXIS_ERROR = 5 * PROGRAM_SINE_ERROR + 2
# How far apart one component of the program's axes and the engine's can be,
# in 65,536ths, each against the exact axes. Measured: 31.2.
AXIS_TOLERANCE = PROGRAM_AXIS_ERROR + ENGINE_AXIS_UNITS * Q15_UNIT
# The cases whose box keeps every face at least 250 units from its position
# and from each inside point: the clamp to the box cannot be moved by the
# turns' rounding there (each test asserts it).
BIG_BOX_CASES = (4, 5, 6)
STARSHIP = 4
Vector = tuple[int, int, int]


def model(row: Row) -> Vector:
    return row["model_x"], row["model_y"], row["model_z"]


def world_offset(row: Row) -> Vector:
    return (
        row["world_x"] - row["position_x"],
        row["world_y"] - row["position_y"],
        row["world_z"] - row["position_z"],
    )


def boxes() -> dict[int, tuple[Vector, Vector]]:
    """Return each case's box: the smallest and largest of its model points,
    since its other points lie inside its corners."""
    found: dict[int, tuple[Vector, Vector]] = {}
    for row in ROWS:
        point = model(row)
        low, high = found.get(row["case"], (point, point))
        found[row["case"]] = (
            (min(low[0], point[0]), min(low[1], point[1]), min(low[2], point[2])),
            (max(high[0], point[0]), max(high[1], point[1]), max(high[2], point[2])),
        )
    return found


BOXES = boxes()


def is_corner(row: Row) -> bool:
    low, high = BOXES[row["case"]]
    return all(at in (lo, hi) for at, lo, hi in zip(model(row), low, high, strict=True))


def ship(contact, row: Row):
    """Return a starship contact in a row's pose, at its position, with its
    case's box."""
    low, high = BOXES[row["case"]]
    return contact(
        x=row["position_x"],
        y=row["position_y"],
        z=row["position_z"],
        yaw=row["yaw"],
        pitch=row["pitch"],
        roll=row["roll"],
        genus=STARSHIP,
        box_min=low,
        box_max=high,
    )


def turned_tolerance(point: Vector) -> float:
    """Return how far apart the program's and the engine's turn of one point
    can be, per component: a component sums three products of a coordinate
    and an axis component, which differ by at most AXIS_TOLERANCE; the engine
    rounds its three products (WORLD_POINT_TOLERANCE) and the program floors
    its one sum."""
    return sum(abs(at) for at in point) * AXIS_TOLERANCE / 65536 + (
        WORLD_POINT_TOLERANCE + 1
    )


def back_tolerance(world: Vector, model_point: Vector) -> float:
    """Return how far the program's model point, read back from a world
    point the engine made from model_point, can be from model_point, per
    component: the program's axes over the world point, plus the engine's own
    error on that point carried through one more turn, which a unit axis
    lengthens by at most the square root of 3 (2, with margin)."""
    return turned_tolerance(world) + 2 * turned_tolerance(model_point)


@pytest.mark.parametrize("pose", poses(), ids=lambda pose: f"case{pose['case']}")
def test_the_programs_axes_are_the_engines(pose: Row) -> None:
    found = hull.axes(pose["yaw"], pose["pitch"], pose["roll"])
    for name, ours in zip(("side", "up", "fwd"), found, strict=True):
        for at, cached in zip(ours, axis(pose, name), strict=True):
            assert abs(at - cached * Q15_UNIT) <= AXIS_TOLERANCE


def test_a_model_point_lands_where_the_engine_puts_it(contact) -> None:
    for row in ROWS:
        found = hull.to_world(ship(contact, row), model(row))
        for at, engine in zip(found, world_offset(row), strict=True):
            assert abs(at - engine) <= turned_tolerance(model(row))


def test_the_engines_world_point_reads_back_as_its_model_point(contact) -> None:
    for row in ROWS:
        found = hull.to_model(ship(contact, row), world_offset(row))
        tolerance = back_tolerance(world_offset(row), model(row))
        for at, wanted in zip(found, model(row), strict=True):
            assert abs(at - wanted) <= tolerance


@pytest.mark.parametrize("case", BIG_BOX_CASES)
def test_a_craft_inside_the_box_is_0_from_the_hull(view, contact, case) -> None:
    """A craft on the engine's world point of a point inside the box is 0
    from the hull, and its offset is the way to the position."""
    low, high = BOXES[case]
    for row in [r for r in ROWS if r["case"] == case and not is_corner(r)]:
        inside = zip(model(row), low, high, strict=True)
        margin = min(min(at - lo, hi - at) for at, lo, hi in inside)
        assert margin > back_tolerance(world_offset(row), model(row))
        craft = view(x=row["world_x"], y=row["world_y"], z=row["world_z"])
        away = world_offset(row)
        assert hull.nearest(craft, ship(contact, row)) == hull.Nearest(
            0, -away[0], -away[1], -away[2]
        )


@pytest.mark.parametrize("case", BIG_BOX_CASES)
def test_a_craft_twice_a_corner_out_meets_the_hull_at_that_corner(
    view, contact, case
) -> None:
    """A craft at twice a corner's world offset is past the box on every
    axis (each box holds its position), so the box's nearest point is that
    corner: the corner's own length away, the way there the engine's corner
    offset reversed. The craft's model point is within back_tolerance of
    twice the corner, its gap to the corner so too, and a unit axis turns
    that gap at most 2 times longer per component."""
    for row in [r for r in ROWS if r["case"] == case and is_corner(r)]:
        corner = model(row)
        away = world_offset(row)
        doubled = (2 * away[0], 2 * away[1], 2 * away[2])
        reading = back_tolerance(doubled, (2 * corner[0], 2 * corner[1], 2 * corner[2]))
        assert min(abs(at) for at in corner) > reading
        craft = view(
            x=row["position_x"] + doubled[0],
            y=row["position_y"] + doubled[1],
            z=row["position_z"] + doubled[2],
        )
        near = hull.nearest(craft, ship(contact, row))
        assert abs(near.distance - geometry.distance(*corner)) <= 2 * reading
        offset_tolerance = turned_tolerance(corner) + 2 * reading + 1
        for at, wanted in zip((near.dx, near.dy, near.dz), away, strict=True):
            assert abs(at + wanted) <= offset_tolerance
