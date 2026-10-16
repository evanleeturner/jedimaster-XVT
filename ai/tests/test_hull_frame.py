"""Fence for the frame a model sits in: the engine's answers, read from the
file its own test writes, and what the program can check of them today.

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
# The engine truncates its sine and cosine to 1.15 (1 unit each), multiplies
# them (1 unit of rounding), and the roll turn sums three rounded products per
# component (3 units); the exact unit vector is the reference for both sides.
ENGINE_FORWARD_UNITS = 2 + 1 + 3
FORWARD_TOLERANCE = PROGRAM_FORWARD_ERROR + ENGINE_FORWARD_UNITS * Q15_UNIT
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
