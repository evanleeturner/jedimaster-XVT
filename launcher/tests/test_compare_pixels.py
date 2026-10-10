"""The pixel comparison's own pieces: moments, crop, differences, pictures.

Purpose:
    Prove the parts of ``tools/compare_pixels.py`` that do not need a
    browser: reading the moments file, cropping a screen to the panel,
    finding the pixels that differ and naming them, the difference picture,
    and the refusal to write a picture inside the repository.

Flow:
    Made-up screens (640 by 480 two-byte pixels) and made-up moments.

Invariants:
    - No game data and no browser.

Call:
    ``pytest tests/test_compare_pixels.py``
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import compare_pixels as cp  # noqa: E402

logger = logging.getLogger(__name__)

WIDTH, HEIGHT = 640, 480


def screen(pixels: dict[tuple[int, int], int]) -> bytes:
    """Return a black 640 by 480 screen with the given (x, y) pixels set."""
    data = bytearray(WIDTH * HEIGHT * 2)
    for (x, y), value in pixels.items():
        at = (y * WIDTH + x) * 2
        data[at], data[at + 1] = value & 0xFF, value >> 8
    return bytes(data)


def test_the_crop_starts_at_84_96_and_is_360_by_240():
    data = screen({(84, 96): 0x1234, (443, 335): 0xABCD, (83, 96): 7, (444, 335): 7})
    panel = cp.crop_panel(data)
    assert len(panel) == 360 * 240 * 2
    assert cp.pixel_at(panel, 0) == 0x1234
    assert cp.pixel_at(panel, 360 * 240 - 1) == 0xABCD
    assert sum(1 for at in range(360 * 240) if cp.pixel_at(panel, at)) == 2


def test_a_screen_of_the_wrong_size_is_refused():
    with pytest.raises(ValueError):
        cp.crop_panel(b"\0" * 10)


def test_differences_are_the_pixel_numbers_that_differ():
    a = cp.crop_panel(screen({(85, 96): 5, (84, 97): 6}))
    b = cp.crop_panel(screen({(85, 96): 5, (84, 97): 9, (90, 100): 1}))
    assert cp.differences(a, b) == [360, 4 * 360 + 6]
    assert cp.differences(a, a) == []
    with pytest.raises(ValueError):
        cp.differences(a, b[:-2])


def test_a_difference_is_described_by_position_and_both_values():
    a = cp.crop_panel(screen({(87, 97): 0x00FF}))
    b = cp.crop_panel(screen({(87, 97): 0x1000}))
    text = cp.describe(a, b, cp.differences(a, b))
    assert text == "(3,1) engine 00ff page 1000"


def test_only_the_first_few_differences_are_described():
    a = cp.crop_panel(screen({}))
    b = cp.crop_panel(screen({(84 + x, 96): 1 for x in range(10)}))
    text = cp.describe(a, b, cp.differences(a, b))
    assert text.count("engine") == 4


def test_the_difference_picture_marks_the_differing_pixels_red():
    from test_briefing_under import png_rgba

    a = cp.crop_panel(screen({(84, 96): 0xFFFF, (85, 96): 0xFFFF}))
    width, height, rgba = png_rgba(cp.difference_picture(a, [1]))
    assert (width, height) == (360, 240)
    assert tuple(rgba[0:4]) == (255 // 3, 255 // 3, 255 // 3, 255)
    assert tuple(rgba[4:8]) == (255, 0, 0, 255)
    assert tuple(rgba[8:12]) == (0, 0, 0, 255)


def test_a_16_bit_pixel_widens_by_repeating_its_top_bits():
    assert cp.widen(0xFFFF) == (255, 255, 255)
    assert cp.widen(0) == (0, 0, 0)
    assert cp.widen(0x8410) == (132, 130, 132)


def test_moments_are_read_skipping_comments_and_blank_lines(tmp_path):
    path = tmp_path / "moments.txt"
    path.write_text(
        "# header\n\nTrain/A.TIE train 0 2 21 99\nCombat/B.TIE combat 1 2\n"
    )
    moments = cp.read_moments(path)
    assert moments[0] == cp.Moment("Train/A.TIE", "train", 0, (2, 21, 99))
    assert moments[1].directory == "combat" and moments[1].team == 1
    assert cp.moment_key(moments[0]) == "Train_A.TIE.t0"
    assert (
        cp.moment_key(cp.Moment("Bop/TRAIN/a.tie", "train", 3, ()))
        == "Bop_TRAIN_a.tie.t3"
    )


@pytest.mark.parametrize("line", ["Train/A.TIE train 0", "Train/A.TIE nowhere 0 2"])
def test_a_bad_moments_line_is_refused(tmp_path, line):
    path = tmp_path / "moments.txt"
    path.write_text(line + "\n")
    with pytest.raises(ValueError):
        cp.read_moments(path)


def test_directory_words_become_mission_types():
    assert cp.DIRECTORY_TYPES["train"] == "training"
    assert cp.DIRECTORY_TYPES["tourn"] == "tournament"
    assert cp.DIRECTORY_TYPES["combat"] == "combat"


def test_a_folder_inside_the_repository_is_refused_for_pictures():
    assert not cp.outside_repository(cp.ROOT / "x")
    assert not cp.outside_repository(cp.ROOT.parent / "engine")
    assert cp.outside_repository(Path("/elsewhere/folder"))
    assert cp.main(["--engine", "e", "--lists", "l", "--moments", "m", "--install", "i"]
                   + ["--diffs", str(cp.ROOT / "diffs")]) == 2  # fmt: skip
